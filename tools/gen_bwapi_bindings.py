#!/usr/bin/env python3
"""Generate pybind11 bindings and a .pyi stub for BWAPI by parsing its headers with libclang.

The generated files are committed, so this only needs to be re-run when the BWAPI headers change
or when the generator itself changes:

    .venv/bin/python tools/gen_bwapi_bindings.py

Outputs:
    src/python/generated/*.cpp   pybind11 method/constant/enum bindings (compiled into the bot)
    python/bwapi.pyi             type stub for IDE completion and type checking

Hand-written pieces (casters, Position types, the Type<> base, module wiring) live in
src/python/BWAPIBindings.h and src/python/BWAPIModule.cpp.
"""

import keyword
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

import clang.cindex as ci
from clang.cindex import CursorKind as K
from clang.cindex import TypeKind as T

ROOT = Path(__file__).resolve().parent.parent
BWAPI_INCLUDE = ROOT / "3rdparty/openbw/bwapi/bwapi/include"
CPP_OUT = ROOT / "src/python/generated"
STUB_OUT = ROOT / "python/bwapi.pyi"

# The pip `libclang` wheel lags behind the system libc++ headers, so prefer the system libclang.
SYSTEM_LIBCLANG = Path("/Library/Developer/CommandLineTools/usr/lib/libclang.dylib")

# (C++ class, Python name, kind). "interface" classes are owned by BWAPI and exposed by pointer;
# "type" classes derive from BWAPI::Type<> and get the shared base bindings; "value" is a plain struct.
CLASSES = [
    ("Game", "Game", "interface"),
    ("UnitInterface", "Unit", "interface"),
    ("PlayerInterface", "Player", "interface"),
    ("ForceInterface", "Force", "interface"),
    ("BulletInterface", "Bullet", "interface"),
    ("RegionInterface", "Region", "interface"),
    ("UnitCommand", "UnitCommand", "value"),
    ("Event", "Event", "value"),
    ("BulletType", "BulletType", "type"),
    ("Color", "Color", "type"),
    ("DamageType", "DamageType", "type"),
    ("Error", "Error", "type"),
    ("ExplosionType", "ExplosionType", "type"),
    ("GameType", "GameType", "type"),
    ("Order", "Order", "type"),
    ("PlayerType", "PlayerType", "type"),
    ("Race", "Race", "type"),
    ("TechType", "TechType", "type"),
    ("UnitCommandType", "UnitCommandType", "type"),
    ("UnitSizeType", "UnitSizeType", "type"),
    ("UnitType", "UnitType", "type"),
    ("UpgradeType", "UpgradeType", "type"),
    ("WeaponType", "WeaponType", "type"),
]
PY_NAME = {cxx: py for cxx, py, _ in CLASSES}
INTERFACES = {cxx for cxx, _, kind in CLASSES if kind == "interface"}

# Namespaces whose constants (UnitTypes::Protoss_Probe, ...) and functions (allUnitTypes()) are exposed.
CONSTANT_NAMESPACES = [
    "Positions", "WalkPositions", "TilePositions", "BulletTypes", "Colors", "DamageTypes", "Errors",
    "ExplosionTypes", "GameTypes", "Orders", "PlayerTypes", "Races", "TechTypes", "UnitCommandTypes",
    "UnitSizeTypes", "UnitTypes", "UpgradeTypes", "WeaponTypes",
]

# C++ enum -> Python enum name.
ENUMS = {
    "BWAPI::CoordinateType::Enum": "CoordinateType",
    "BWAPI::Text::Size::Enum": "TextSize",
    "BWAPI::MouseButton": "MouseButton",
    "BWAPI::Key": "Key",
    "BWAPI::Flag::Enum": "Flag",
    "BWAPI::EventType::Enum": "EventType",
}
# Text::Enum holds control characters for coloured text, so it is exposed as str constants.
TEXT_ENUM = "BWAPI::Text::Enum"

# Methods that are unsafe or meaningless from Python. Methods with unsupported types are skipped
# automatically, so this list only needs entries that *would* convert.
SKIP = {
    "Game": {"setAIModule", "createSinglePlayerGame", "createMultiPlayerGame", "startGame",
             "switchToPlayer", "setCharacterName", "setGameType"},
}

# Pointer returns are typed `X | None` in the stub, except these, which BWAPI never returns null from
# (self() and enemy() can be null in replays, but never for a bot playing a 1v1 game).
NON_NULL_RETURNS = {"UnitInterface::getPlayer", "Game::neutral", "Game::self", "Game::enemy"}

# Collection classes are converted to Python sets by custom casters rather than bound.
SET_TYPES = {"Unitset": "Unit", "Playerset": "Player", "Forceset": "Force", "Bulletset": "Bullet",
             "Regionset": "Region"}
POINT_SCALES = {1: "Position", 8: "WalkPosition", 32: "TilePosition"}
# Value structs bound by hand in BWAPIBindings.h (bind_exact_positions)
HAND_BOUND_VALUES = {"ExactPosition", "ExactPositionDifference"}


class Unsupported(Exception):
    pass


# ---------------------------------------------------------------------------------------------
# Parsing


def parse():
    if SYSTEM_LIBCLANG.exists():
        ci.Config.set_library_file(str(SYSTEM_LIBCLANG))
    sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
    resource_dir = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()
    args = ["-x", "c++", "-std=c++17", "-isysroot", sdk, "-resource-dir", resource_dir,
            f"-I{sdk}/usr/include/c++/v1", f"-I{BWAPI_INCLUDE}"]
    tu = ci.Index.create().parse(str(BWAPI_INCLUDE / "BWAPI.h"), args=args,
                                 options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    errors = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errors:
        sys.exit("libclang failed to parse BWAPI headers:\n" + "\n".join(map(str, errors)))
    return tu


def qualified_name(cursor):
    parts = []
    while cursor is not None and cursor.kind != K.TRANSLATION_UNIT:
        if cursor.spelling:
            parts.append(cursor.spelling)
        cursor = cursor.semantic_parent
    return "::".join(reversed(parts))


def collect(tu):
    classes, namespaces, enums = {}, {}, {}

    def visit(cursor):
        for child in cursor.get_children():
            if child.location.file and not str(child.location.file).startswith(str(BWAPI_INCLUDE)):
                continue
            name = qualified_name(child)
            if child.kind == K.NAMESPACE:
                namespaces.setdefault(name, []).append(child)
                visit(child)
            elif child.kind == K.CLASS_DECL and child.is_definition():
                classes.setdefault(name, child)
            elif child.kind == K.ENUM_DECL and child.is_definition():
                enums.setdefault(name, child)

    visit(tu.cursor)
    return classes, namespaces, enums


# ---------------------------------------------------------------------------------------------
# Type mapping (C++ -> Python stub annotation). Raises Unsupported for types we cannot convert.


def strip_ref(t):
    t = t.get_canonical()
    if t.kind in (T.LVALUEREFERENCE, T.RVALUEREFERENCE):
        t = t.get_pointee().get_canonical()
    return t


def record_name(t):
    return t.get_declaration().spelling


def template_args(t):
    return [t.get_template_argument_type(i) for i in range(t.get_num_template_arguments())]


def py_type(t, *, nullable=False):
    t = strip_ref(t)
    k = t.kind
    if k == T.VOID:
        return "None"
    if k == T.BOOL:
        return "bool"
    if k in (T.INT, T.UINT, T.SHORT, T.USHORT, T.LONG, T.ULONG, T.LONGLONG, T.ULONGLONG, T.UCHAR):
        return "int"
    if k in (T.CHAR_S, T.SCHAR):
        return "str"
    if k in (T.FLOAT, T.DOUBLE):
        return "float"
    if k == T.ENUM:
        name = qualified_name(t.get_declaration())
        if name in ENUMS:
            return ENUMS[name]
        raise Unsupported(name)
    if k == T.POINTER:
        pointee = t.get_pointee().get_canonical()
        if pointee.kind in (T.CHAR_S, T.SCHAR):
            raise Unsupported("const char *")
        name = record_name(pointee)
        if name in INTERFACES:
            return PY_NAME[name] + (" | None" if nullable else "")
        raise Unsupported(t.spelling)
    if k == T.RECORD:
        name = record_name(t)
        args = template_args(t)
        if name in PY_NAME:
            return PY_NAME[name]
        if name in SET_TYPES:
            return f"set[{SET_TYPES[name]}]"
        if name in HAND_BOUND_VALUES:
            return name
        if name == "Point":
            # The default scale (1) is omitted from canonical spellings: Point<int> is Position.
            m = re.fullmatch(r"BWAPI::Point<int(?:, (\d+))?>", t.spelling.removeprefix("const "))
            scale = int(m.group(1) or 1) if m else None
            if scale in POINT_SCALES:
                return POINT_SCALES[scale]
            raise Unsupported(t.spelling)
        if name == "basic_string":
            return "str"
        if name == "SetContainer":
            return f"set[{py_type(args[0])}]"
        if name in ("deque", "vector", "list"):
            return f"list[{py_type(args[0])}]"
        if name in ("map", "unordered_map"):
            return f"dict[{py_type(args[0])}, {py_type(args[1])}]"
        if name == "pair":
            return f"tuple[{py_type(args[0])}, {py_type(args[1])}]"
        if name == "UnaryFilter" and record_name(args[0].get_pointee()) == "UnitInterface":
            return "Callable[[Unit], bool] | None"
        if name == "BestFilter" and record_name(args[0].get_pointee()) == "UnitInterface":
            return "Callable[[Unit, Unit], Unit]"
        if name == "function":
            fn = args[0].get_canonical()
            params = ", ".join(py_type(a) for a in fn.argument_types())
            return f"Callable[[{params}], {py_type(fn.get_result())}]"
        raise Unsupported(t.spelling)
    raise Unsupported(t.spelling)


def is_pointer_return(t):
    return t.get_canonical().kind == T.POINTER


def is_ref_return(t):
    return t.get_canonical().kind == T.LVALUEREFERENCE


def is_unit_filter(t):
    t = strip_ref(t)
    return t.kind == T.RECORD and record_name(t) in ("UnaryFilter", "BestFilter")


# ---------------------------------------------------------------------------------------------
# Method model


@dataclass
class Param:
    name: str
    cxx_type: str  # canonical C++ spelling, used for overload_cast and lambdas
    py_type: str
    default: str | None  # C++ default expression


@dataclass
class Method:
    name: str
    params: list[Param]
    ret_py: str
    static: bool = False
    const: bool = False
    variadic_text: bool = False  # printf-style method exposed as taking a plain str
    ctor: bool = False
    policy: str | None = None
    doc: str = ""
    overloaded: bool = False
    extra_args: list = field(default_factory=list)


def py_ident(name, index):
    if not name:
        return f"arg{index}"
    return name + "_" if keyword.iskeyword(name) else name


def default_expr(param_cursor):
    tokens = [t.spelling for t in param_cursor.get_tokens()]
    if "=" not in tokens:
        return None
    return "".join(tokens[tokens.index("=") + 1:])


def clean_doc(cursor):
    raw = cursor.brief_comment or ""
    raw = re.sub(r"<[^>]+>", "", raw)
    raw = re.sub(r"@\w+", "", raw)
    return " ".join(raw.split())


def build_method(cursor, class_name):
    """Return a Method for a C++ method/constructor, or raise Unsupported."""
    is_ctor = cursor.kind == K.CONSTRUCTOR
    params = []
    args = list(cursor.get_arguments())
    variadic = cursor.type.kind == T.FUNCTIONPROTO and cursor.type.is_function_variadic()
    if variadic:
        # printf-style: (..., const char *format, ...) -> (..., text: str)
        if not args or strip_ref(args[-1].type).kind != T.POINTER:
            raise Unsupported("variadic")
        args = args[:-1]
    for i, a in enumerate(args):
        if a.type.spelling == "va_list" or "__va_list" in a.type.get_canonical().spelling:
            raise Unsupported("va_list")
        default = default_expr(a)
        pyt = py_type(a.type, nullable=default == "nullptr")
        params.append(Param(py_ident(a.spelling, i), a.type.get_canonical().spelling, pyt, default))
    if variadic:
        params.append(Param("text", "const std::string &", "str", None))

    if is_ctor:
        ret_py, policy = "None", None
    else:
        ret = cursor.result_type
        ret_py = py_type(ret, nullable=f"{class_name}::{cursor.spelling}" not in NON_NULL_RETURNS)
        policy = "reference" if is_pointer_return(ret) else "copy" if is_ref_return(ret) else None
    return Method(
        name="__init__" if is_ctor else cursor.spelling,
        params=params,
        ret_py=ret_py,
        static=cursor.is_static_method(),
        const=cursor.is_const_method(),
        variadic_text=variadic,
        ctor=is_ctor,
        policy=policy,
        doc=clean_doc(cursor),
    )


def class_methods(class_cursor, cxx_name, report):
    """Collect bindable public methods of a class, keyed by name (overloads grouped)."""
    skip = SKIP.get(cxx_name, set())
    candidates = [c for c in class_cursor.get_children()
                  if c.access_specifier == ci.AccessSpecifier.PUBLIC
                  and c.kind in (K.CXX_METHOD, K.CONSTRUCTOR)
                  and not c.spelling.startswith("operator")
                  and not (c.kind == K.CONSTRUCTOR and (c.is_copy_constructor() or c.is_move_constructor()))]
    overload_count = {}
    for c in candidates:
        overload_count[c.spelling] = overload_count.get(c.spelling, 0) + 1
    methods = []
    for c in candidates:
        if c.spelling in skip:
            report.append(f"{cxx_name}::{c.spelling} (skip list)")
            continue
        if c.availability == ci.AvailabilityKind.DEPRECATED:
            report.append(f"{cxx_name}::{c.spelling} (deprecated)")
            continue
        try:
            m = build_method(c, cxx_name)
        except Unsupported as e:
            report.append(f"{cxx_name}::{c.spelling} ({e})")
            continue
        m.overloaded = overload_count[c.spelling] > 1
        methods.append(m)
    return methods


def class_fields(class_cursor):
    fields = []
    for c in class_cursor.get_children():
        if c.kind == K.FIELD_DECL and c.access_specifier == ci.AccessSpecifier.PUBLIC:
            try:
                fields.append((c.spelling, py_type(c.type, nullable=True)))
            except Unsupported:
                pass
    return fields


def has_operator(class_cursor, op):
    return any(c.kind == K.CXX_METHOD and c.spelling == op for c in class_cursor.get_children())


# ---------------------------------------------------------------------------------------------
# C++ emission


def cxx_arg_list(m):
    out = []
    for p in m.params:
        if p.default is None:
            out.append(f'py::arg("{p.name}")')
        elif p.default == "nullptr" and "Callable" in p.py_type:
            out.append(f'py::arg("{p.name}") = py::none()')
        elif p.default == "nullptr":
            out.append(f'py::arg("{p.name}") = nullptr')
        else:
            out.append(f'py::arg("{p.name}") = {p.default}')
    return out


def emit_method(cls_var, cxx_class, m):
    extras = cxx_arg_list(m)
    if m.policy:
        extras.append(f"py::return_value_policy::{m.policy}")
    extra = "".join(", " + e for e in extras)

    if m.ctor:
        types = ", ".join(p.cxx_type for p in m.params)
        return f"    {cls_var}.def(py::init<{types}>(){extra});"

    if m.variadic_text:
        fixed = m.params[:-1]
        sig = ", ".join([f"{cxx_class} &self"] + [f"{p.cxx_type} {p.name}" for p in fixed]
                        + ["const std::string &text"])
        call_args = ", ".join([p.name for p in fixed] + ['"%s"', "text.c_str()"])
        return f'    {cls_var}.def("{m.name}", []({sig}) {{ self.{m.name}({call_args}); }}{extra});'

    if m.overloaded:
        types = ", ".join(p.cxx_type for p in m.params)
        const = ", py::const_" if m.const else ""
        ptr = f"py::overload_cast<{types}>(&{cxx_class}::{m.name}{const})"
    else:
        ptr = f"&{cxx_class}::{m.name}"
    fn = "def_static" if m.static else "def"
    return f'    {cls_var}.{fn}("{m.name}", {ptr}{extra});'


def write_class_file(cxx_name, py_name, kind, methods, fields, eq, ne):
    holder = "BWAPIInterfaceClass" if kind == "interface" else "BWAPIValueClass"
    lines = [
        "// Generated by tools/gen_bwapi_bindings.py - do not edit.",
        '#include "BWAPIBindings.h"',
        "",
        "using namespace BWAPI;",
        "",
        f"void def_{py_name}({holder}<{cxx_name}> &c)",
        "{",
    ]
    lines += [emit_method("c", cxx_name, m) for m in methods]
    for name, _ in fields:
        lines.append(f'    c.def_readwrite("{name}", &{cxx_name}::{name});')
    if eq:
        lines.append("    c.def(py::self == py::self);")
        if ne:
            lines.append("    c.def(py::self != py::self);")
    lines.append("}")
    (CPP_OUT / f"bwapi_{py_name}.cpp").write_text("\n".join(lines) + "\n")


def enum_constants(enum_cursor):
    return [(c.spelling, c.enum_value) for c in enum_cursor.get_children()
            if c.kind == K.ENUM_CONSTANT_DECL]


def py_attr_names(name):
    """Names to expose a C++ identifier under; Python keywords get a trailing-underscore alias."""
    return [name, name + "_"] if keyword.iskeyword(name) else [name]


def namespace_members(ns_cursors, report):
    constants, functions = [], []
    for ns in ns_cursors:
        for c in ns.get_children():
            if c.kind == K.VAR_DECL:
                try:
                    constants.append((c.spelling, py_type(c.type)))
                except Unsupported as e:
                    report.append(f"{qualified_name(c)} ({e})")
            elif c.kind == K.FUNCTION_DECL and not c.spelling.startswith("operator"):
                try:
                    ret = py_type(c.result_type)
                    if list(c.get_arguments()):
                        raise Unsupported("namespace function with arguments")
                except Unsupported as e:
                    report.append(f"{qualified_name(c)}() ({e})")
                    continue
                functions.append((c.spelling, ret, is_ref_return(c.result_type), clean_doc(c)))
    return constants, functions


def write_constants_file(namespaces, enums, report):
    lines = [
        "// Generated by tools/gen_bwapi_bindings.py - do not edit.",
        '#include "BWAPIBindings.h"',
        "",
        "using namespace BWAPI;",
        "",
        "void def_enums(py::module_ &m)",
        "{",
    ]
    for cxx_enum, py_name in ENUMS.items():
        lines.append(f'    py::enum_<{cxx_enum}>(m, "{py_name}")')
        for name, _ in enum_constants(enums[cxx_enum]):
            scope = cxx_enum.rsplit("::", 1)[0] if cxx_enum.endswith("::Enum") else "BWAPI"
            for alias in py_attr_names(name):
                if keyword.iskeyword(alias):
                    continue  # enum members must be valid attribute names; the _ alias covers it
                lines.append(f'        .value("{alias}", {scope}::{name})')
        lines[-1] += ";"
    lines.append('    auto text = m.def_submodule("Text", "Control characters for coloured text");')
    for name, value in enum_constants(enums[TEXT_ENUM]):
        lines.append(f'    text.attr("{name}") = std::string(1, static_cast<char>({value}));')
    lines += ["}", "", "void def_constants(py::module_ &m)", "{"]

    stub_namespaces = []
    for ns_name in CONSTANT_NAMESPACES:
        constants, functions = namespace_members(namespaces[f"BWAPI::{ns_name}"], report)
        var = f"ns_{ns_name}"
        lines.append(f'    auto {var} = m.def_submodule("{ns_name}");')
        for name, _ in constants:
            for alias in py_attr_names(name):
                lines.append(f'    {var}.attr("{alias}") = {ns_name}::{name};')
        for name, _, by_ref, _ in functions:
            policy = ", py::return_value_policy::copy" if by_ref else ""
            lines.append(f'    {var}.def("{name}", &{ns_name}::{name}{policy});')
        stub_namespaces.append((ns_name, constants, functions))
    lines.append("}")
    (CPP_OUT / "bwapi_constants.cpp").write_text("\n".join(lines) + "\n")
    return stub_namespaces


def write_module_file(class_specs):
    """The generated half of module init: declare all classes, then add their members."""
    lines = [
        "// Generated by tools/gen_bwapi_bindings.py - do not edit.",
        '#include "BWAPIBindings.h"',
        "",
        "using namespace BWAPI;",
        "",
        "void def_enums(py::module_ &m);",
        "void def_constants(py::module_ &m);",
    ]
    for cxx, py_name, kind, *_ in class_specs:
        holder = "BWAPIInterfaceClass" if kind == "interface" else "BWAPIValueClass"
        lines.append(f"void def_{py_name}({holder}<{cxx}> &c);")
    lines += [
        "",
        "void bind_bwapi_generated(py::module_ &m)",
        "{",
        "    // Declare every class before adding methods so default arguments and signatures",
        "    // can refer to any BWAPI type.",
        "    bind_points(m);",
        "    bind_exact_positions(m);",
        "    def_enums(m);",
    ]
    for cxx, py_name, kind, *_ in class_specs:
        if kind == "interface":
            lines.append(f'    auto cls_{py_name} = declare_interface<{cxx}>(m, "{py_name}");')
        elif kind == "type":
            lines.append(f'    auto cls_{py_name} = declare_type<{cxx}>(m, "{py_name}");')
        else:
            lines.append(f'    auto cls_{py_name} = BWAPIValueClass<{cxx}>(m, "{py_name}");')
    for _, py_name, *_ in class_specs:
        lines.append(f"    def_{py_name}(cls_{py_name});")
    lines += ["    def_constants(m);", "}"]
    (CPP_OUT / "bwapi_module.cpp").write_text("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------------------------
# Stub emission

STUB_HEADER = '''\
# Generated by tools/gen_bwapi_bindings.py - do not edit.
"""BWAPI bindings for Python (OpenBW BWAPI 4.x).

Names follow the C++ API (camelCase), so https://bwapi.github.io/ applies directly.
Unitset/Playerset/etc. are plain Python sets; UnitFilter arguments are callables.
Position, WalkPosition and TilePosition are immutable and hashable.
"""

from collections.abc import Callable, Iterator
from typing import ClassVar, overload

#: The current game, set by the bot host before any callback runs. (None outside a game, e.g. offline tests.)
Broodwar: Game

'''

POINT_STUB = '''\
class {name}:
    SCALE: ClassVar[int]
    @property
    def x(self) -> int: ...
    @property
    def y(self) -> int: ...
    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self, x: int, y: int) -> None: ...
    @overload
    def __init__(self, other: Position | WalkPosition | TilePosition) -> None:
        """Convert from another position type, scaling coordinates."""
    def isValid(self) -> bool: ...
    def makeValid(self) -> {name}:
        """Return a copy clamped to the map bounds."""
    def getDistance(self, other: {name}) -> float: ...
    def getApproxDistance(self, other: {name}) -> int: ...
    def getLength(self) -> float: ...
    def __add__(self, other: {name}) -> {name}: ...
    def __sub__(self, other: {name}) -> {name}: ...
    def __mul__(self, v: int) -> {name}: ...
    def __rmul__(self, v: int) -> {name}: ...
    def __truediv__(self, v: int) -> {name}: ...
    def __floordiv__(self, v: int) -> {name}: ...
    def __mod__(self, v: int) -> {name}: ...
    def __neg__(self) -> {name}: ...
    def __bool__(self) -> bool:
        """Same as isValid(), matching the C++ explicit operator bool."""
    def __iter__(self) -> Iterator[int]: ...
    def __lt__(self, other: {name}) -> bool: ...
    def __hash__(self) -> int: ...

'''

EXACT_POSITION_STUB = '''\
class ExactPositionDifference:
    """The difference between two exact positions (ignoring heading and velocity)."""
    @property
    def x(self) -> int: ...
    @property
    def y(self) -> int: ...
    def __init__(self, x: int, y: int) -> None: ...
    def __lt__(self, other: ExactPositionDifference) -> bool: ...
    def __hash__(self) -> int: ...

class ExactPosition:
    """A unit position, heading and velocity with the full precision of the BW engine: x and y include 8 bits of
    subpixel precision, the heading is in 1/256ths of a circle and the lower 8 bits of velocities are fractional."""
    @property
    def x(self) -> int: ...
    @property
    def y(self) -> int: ...
    @property
    def heading(self) -> int: ...
    @property
    def velocityX(self) -> int: ...
    @property
    def velocityY(self) -> int: ...
    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self, x: int, y: int, heading: int, velocityX: int, velocityY: int) -> None: ...
    def pos(self) -> Position: ...
    def __sub__(self, other: ExactPosition) -> ExactPositionDifference: ...
    def __lt__(self, other: ExactPosition) -> bool: ...
    def __hash__(self) -> int: ...

'''

TYPE_BASE_STUB = '''\
    def __init__(self, id: int = ...) -> None: ...
    def getID(self) -> int: ...
    def getName(self) -> str: ...
    def toString(self) -> str: ...
    def isValid(self) -> bool: ...
    @staticmethod
    def getType(name: str) -> {name}:
        """Look up by name, ignoring case, spaces and underscores."""
    def __int__(self) -> int: ...
    def __index__(self) -> int: ...
    def __lt__(self, other: {name}) -> bool: ...
    def __hash__(self) -> int: ...
'''

INTERFACE_BASE_STUB = '''\
    def __hash__(self) -> int: ...
'''


def stub_method(m, indent="    "):
    out = []
    if m.overloaded:
        out.append(f"{indent}@overload")
    if m.static:
        out.append(f"{indent}@staticmethod")
    params = [] if m.static else ["self"]
    for p in m.params:
        params.append(f"{p.name}: {p.py_type}" + (" = ..." if p.default is not None else ""))
    sig = f"{indent}def {m.name}({', '.join(params)}) -> {m.ret_py}:"
    if m.doc:
        out.append(sig)
        out.append(f'{indent}    """{m.doc}"""')
    else:
        out.append(sig + " ...")
    return out


def write_stub(class_specs, stub_namespaces, enums):
    lines = [STUB_HEADER]
    for name in POINT_SCALES.values():
        lines.append(POINT_STUB.format(name=name))
    lines.append(EXACT_POSITION_STUB)

    for cxx, py_name, kind, methods, fields, eq in class_specs:
        lines.append(f"class {py_name}:")
        # Type classes with their own constructors (Color(r, g, b)) overload the base Type(id) one
        extra_ctors = kind == "type" and any(m.ctor for m in methods)
        if kind == "type":
            base = TYPE_BASE_STUB.format(name=py_name).splitlines()
            if extra_ctors:
                # mypy requires overloads to be adjacent, so they go straight after the base __init__
                ctors = []
                for m in methods:
                    if m.ctor:
                        m.overloaded = True
                        ctors += stub_method(m)
                base = ["    @overload", base[0], *ctors, *base[1:]]
            lines += base
        elif kind == "interface":
            lines.append(INTERFACE_BASE_STUB)
        if py_name == "Game":
            lines.append("    def registerEvent(self, action: Callable[[Game], None], "
                         "condition: Callable[[Game], bool] | None = ..., timesToRun: int = ..., "
                         "framesToCheck: int = ...) -> None: ...")
        for name, pyt in fields:
            lines.append(f"    {name}: {pyt}")
        # Overload markers must be consistent per name after filtering.
        by_name = {}
        for m in methods:
            by_name.setdefault(m.name, []).append(m)
        for group in by_name.values():
            for m in group:
                if m.ctor and extra_ctors:
                    continue
                m.overloaded = len(group) > 1
                lines += stub_method(m)
        lines.append("")

    for cxx_enum, py_name in ENUMS.items():
        lines.append(f"class {py_name}:")
        for name, _ in enum_constants(enums[cxx_enum]):
            alias = name + "_" if keyword.iskeyword(name) else name
            lines.append(f"    {alias}: ClassVar[{py_name}]")
        lines += [
            f"    __members__: ClassVar[dict[str, {py_name}]]",
            f"    def __init__(self, value: int) -> None: ...",
            "    @property",
            "    def name(self) -> str: ...",
            "    @property",
            "    def value(self) -> int: ...",
            "    def __int__(self) -> int: ...",
            "    def __index__(self) -> int: ...",
            "",
        ]

    lines.append("class Text:")
    lines.append('    """Control characters for coloured text, e.g. Text.Green + "ok"."""')
    for name, _ in enum_constants(enums[TEXT_ENUM]):
        lines.append(f"    {name}: ClassVar[str]")
    lines.append("")

    for ns_name, constants, functions in stub_namespaces:
        lines.append(f"class {ns_name}:")
        for name, pyt in constants:
            alias = name + "_" if keyword.iskeyword(name) else name
            lines.append(f"    {alias}: ClassVar[{pyt}]")
        for name, ret, _, doc in functions:
            lines.append("    @staticmethod")
            lines.append(f"    def {name}() -> {ret}: ..." + (f"  # {doc}" if doc else ""))
        lines.append("")

    STUB_OUT.parent.mkdir(parents=True, exist_ok=True)
    STUB_OUT.write_text("\n".join(lines).rstrip() + "\n")


# ---------------------------------------------------------------------------------------------


def main():
    tu = parse()
    classes, namespaces, enums = collect(tu)
    CPP_OUT.mkdir(parents=True, exist_ok=True)
    for old in CPP_OUT.glob("*.cpp"):
        old.unlink()

    report = []
    class_specs = []
    for cxx, py_name, kind in CLASSES:
        cursor = classes[f"BWAPI::{cxx}"]
        methods = class_methods(cursor, cxx, report)
        if kind == "type":
            # The Type<> base supplies construction from an id; drop the generated duplicate.
            methods = [m for m in methods if not m.ctor or len(m.params) != 1 or m.params[0].py_type != "int"]
        fields = class_fields(cursor) if kind == "value" else []
        eq = kind == "value" and has_operator(cursor, "operator==")
        ne = kind == "value" and has_operator(cursor, "operator!=")
        write_class_file(cxx, py_name, kind, methods, fields, eq, ne)
        class_specs.append((cxx, py_name, kind, methods, fields, eq))

    write_module_file(class_specs)
    stub_namespaces = write_constants_file(namespaces, enums, report)
    write_stub(class_specs, stub_namespaces, enums)

    total = sum(len(spec[3]) for spec in class_specs)
    print(f"Generated {total} methods across {len(class_specs)} classes into {CPP_OUT.relative_to(ROOT)}")
    print(f"Wrote {STUB_OUT.relative_to(ROOT)}")
    if report:
        print(f"Skipped {len(report)}:")
        for line in report:
            print("  " + line)


if __name__ == "__main__":
    main()

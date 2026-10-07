#pragma once

// Shared pieces of the BWAPI Python bindings: type casters, holder types and helpers used by both
// the hand-written module code (BWAPIModule.cpp) and the generated files in generated/.

#include <BWAPI.h>

#include <pybind11/functional.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <functional>
#include <sstream>
#include <string>

namespace py = pybind11;

// Units, players, the game etc. are owned by BWAPI. Python only borrows them, so the holder never deletes.
template<class T>
using BWAPIInterfaceClass = py::class_<T, std::unique_ptr<T, py::nodelete>>;

template<class T>
using BWAPIValueClass = py::class_<T>;

namespace pybind11::detail
{
    // BWAPI's set containers (Unitset, UnitType::set, ...) become Python sets, and accept any iterable.
    template<typename Set, typename Key>
    struct bwapi_set_caster
    {
        PYBIND11_TYPE_CASTER(Set, const_name("set[") + make_caster<Key>::name + const_name("]"));

        bool load(handle src, bool convert)
        {
            if (!isinstance<iterable>(src) || isinstance<str>(src)) return false;
            value.clear();
            for (auto item : reinterpret_borrow<iterable>(src))
            {
                make_caster<Key> conv;
                if (!conv.load(item, convert)) return false;
                value.insert(cast_op<Key>(conv));
            }
            return true;
        }

        template<typename S>
        static handle cast(S &&src, return_value_policy, handle parent)
        {
            // Pointer elements are BWAPI-owned and borrowed; value elements are small and copied.
            constexpr auto policy = std::is_pointer_v<Key> ? return_value_policy::reference : return_value_policy::copy;
            set result;
            for (auto &&item : src)
            {
                auto obj = reinterpret_steal<object>(make_caster<Key>::cast(item, policy, parent));
                if (!obj) return {};
                result.add(obj);
            }
            return result.release();
        }
    };

    template<> struct type_caster<BWAPI::Unitset> : bwapi_set_caster<BWAPI::Unitset, BWAPI::Unit> {};
    template<> struct type_caster<BWAPI::Playerset> : bwapi_set_caster<BWAPI::Playerset, BWAPI::Player> {};
    template<> struct type_caster<BWAPI::Forceset> : bwapi_set_caster<BWAPI::Forceset, BWAPI::Force> {};
    template<> struct type_caster<BWAPI::Bulletset> : bwapi_set_caster<BWAPI::Bulletset, BWAPI::Bullet> {};
    template<> struct type_caster<BWAPI::Regionset> : bwapi_set_caster<BWAPI::Regionset, BWAPI::Region> {};

    template<typename T, typename H>
    struct type_caster<BWAPI::SetContainer<T, H>> : bwapi_set_caster<BWAPI::SetContainer<T, H>, T> {};

    // Base for filter casters: BWAPI filters have no default constructor, so PYBIND11_TYPE_CASTER can't be used.
    template<typename Filter>
    struct bwapi_filter_caster
    {
        Filter value{nullptr};

        template<typename T_> using cast_op_type = pybind11::detail::cast_op_type<T_>;
        operator Filter *() { return &value; }
        operator Filter &() { return value; }

        static handle cast(const Filter &, return_value_policy, handle) { return none().release(); }
    };

    // UnitFilter arguments accept None or any callable taking a Unit and returning something truthy.
    template<>
    struct type_caster<BWAPI::UnitFilter> : bwapi_filter_caster<BWAPI::UnitFilter>
    {
        static constexpr auto name = const_name("Callable[[Unit], bool] | None");

        bool load(handle src, bool)
        {
            if (src.is_none())
            {
                value = BWAPI::UnitFilter(nullptr);
                return true;
            }
            if (!PyCallable_Check(src.ptr())) return false;

            auto fn = reinterpret_borrow<function>(src);
            value = BWAPI::UnitFilter(std::function<bool(BWAPI::Unit)>([fn](BWAPI::Unit unit)
            {
                object result = fn(unit);
                int truth = PyObject_IsTrue(result.ptr());
                if (truth < 0) throw error_already_set();
                return truth != 0;
            }));
            return true;
        }
    };

    // BestUnitFilter arguments accept a callable picking the better of two units.
    template<>
    struct type_caster<BWAPI::BestUnitFilter> : bwapi_filter_caster<BWAPI::BestUnitFilter>
    {
        static constexpr auto name = const_name("Callable[[Unit, Unit], Unit]");

        bool load(handle src, bool)
        {
            if (!PyCallable_Check(src.ptr())) return false;

            auto fn = reinterpret_borrow<function>(src);
            value = BWAPI::BestUnitFilter(std::function<BWAPI::Unit(BWAPI::Unit, BWAPI::Unit)>([fn](BWAPI::Unit a, BWAPI::Unit b)
            {
                return fn(a, b).template cast<BWAPI::Unit>();
            }));
            return true;
        }
    };
}

// Declares an interface class (Unit, Player, ...). Equality and hashing are by identity of the
// underlying BWAPI object, so wrappers can be compared and used as dict keys / set members.
template<class T>
BWAPIInterfaceClass<T> declare_interface(py::module_ &m, const char *name)
{
    BWAPIInterfaceClass<T> cls(m, name);
    cls.def("__eq__", [](const T &a, const T &b) { return &a == &b; }, py::is_operator());
    cls.def("__ne__", [](const T &a, const T &b) { return &a != &b; }, py::is_operator());
    cls.def("__hash__", [](const T &a) { return std::hash<const void *>()(&a); });
    return cls;
}

// Declares a BWAPI::Type<> subclass (UnitType, Race, ...) with the members common to all of them.
template<class T>
BWAPIValueClass<T> declare_type(py::module_ &m, const char *name)
{
    BWAPIValueClass<T> cls(m, name);
    cls.def(py::init<int>(), py::arg("id") = T().getID());
    cls.def("getID", [](const T &t) { return t.getID(); });
    cls.def("getName", [](const T &t) { return t.getName(); });
    cls.def("toString", [](const T &t) { return t.getName(); });
    cls.def("isValid", [](const T &t) { return t.isValid(); });
    cls.def_static("getType", [](const std::string &typeName) { return T::getType(typeName); }, py::arg("name"));
    cls.def("__int__", [](const T &t) { return t.getID(); });
    cls.def("__index__", [](const T &t) { return t.getID(); });
    cls.def("__hash__", [](const T &t) { return std::hash<int>()(t.getID()); });
    cls.def("__eq__", [](const T &a, const T &b) { return a.getID() == b.getID(); }, py::is_operator());
    cls.def("__ne__", [](const T &a, const T &b) { return a.getID() != b.getID(); }, py::is_operator());
    cls.def("__lt__", [](const T &a, const T &b) { return a.getID() < b.getID(); }, py::is_operator());
    cls.def("__str__", [](const T &t) { return t.getName(); });
    std::string typeName = name;
    cls.def("__repr__", [typeName](const T &t) { return "<" + typeName + " " + t.getName() + ">"; });
    cls.def(py::pickle([](const T &t) { return py::make_tuple(t.getID()); },
                       [](const py::tuple &state) { return T(state[0].cast<int>()); }));
    return cls;
}

// Declares Position / WalkPosition / TilePosition. Unlike C++, they are immutable and hashable in Python
// so they can be used safely as dict keys and set members.
template<int Scale>
BWAPIValueClass<BWAPI::Point<int, Scale>> declare_point(py::module_ &m, const char *name)
{
    using P = BWAPI::Point<int, Scale>;
    BWAPIValueClass<P> cls(m, name);
    std::string typeName = name;

    cls.attr("SCALE") = Scale;
    cls.def(py::init([]() { return P(0, 0); }));
    cls.def(py::init<int, int>(), py::arg("x"), py::arg("y"));
    cls.def(py::init([](const P &other) { return P(other); }), py::arg("other"));
    cls.def_readonly("x", &P::x);
    cls.def_readonly("y", &P::y);
    cls.def("isValid", [](const P &p) { return p.isValid(); });
    cls.def("makeValid", [](P p) { return p.makeValid(); });
    cls.def("getDistance", [](const P &a, const P &b) { return a.getDistance(b); }, py::arg("other"));
    cls.def("getApproxDistance", [](const P &a, const P &b) { return a.getApproxDistance(b); }, py::arg("other"));
    cls.def("getLength", [](const P &p) { return p.getLength(); });
    cls.def("__add__", [](const P &a, const P &b) { return a + b; }, py::is_operator());
    cls.def("__sub__", [](const P &a, const P &b) { return a - b; }, py::is_operator());
    cls.def("__mul__", [](const P &a, int v) { return a * v; }, py::is_operator());
    cls.def("__rmul__", [](const P &a, int v) { return a * v; }, py::is_operator());
    // C++ semantics: integer division, and division by zero yields an off-map sentinel instead of raising.
    cls.def("__truediv__", [](const P &a, int v) { return a / v; }, py::is_operator());
    cls.def("__floordiv__", [](const P &a, int v) { return a / v; }, py::is_operator());
    cls.def("__mod__", [](const P &a, int v) { return a % v; }, py::is_operator());
    cls.def("__neg__", [](const P &a) { return P(-a.x, -a.y); });
    cls.def("__eq__", [](const P &a, const P &b) { return a == b; }, py::is_operator());
    cls.def("__ne__", [](const P &a, const P &b) { return a != b; }, py::is_operator());
    cls.def("__lt__", [](const P &a, const P &b) { return a < b; }, py::is_operator());
    cls.def("__hash__", [](const P &p) { return py::hash(py::make_tuple(p.x, p.y)); });
    cls.def("__bool__", [](const P &p) { return p.isValid(); });
    cls.def("__iter__", [](const P &p) { return py::iter(py::make_tuple(p.x, p.y)); });
    cls.def("__str__", [](const P &p)
    {
        std::ostringstream os;
        os << p;
        return os.str();
    });
    cls.def("__repr__", [typeName](const P &p)
    {
        return typeName + "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
    });
    cls.def(py::pickle([](const P &p) { return py::make_tuple(p.x, p.y); },
                       [](const py::tuple &state) { return P(state[0].cast<int>(), state[1].cast<int>()); }));
    return cls;
}

template<int Scale, int OtherScale>
void add_point_conversion(BWAPIValueClass<BWAPI::Point<int, Scale>> &cls)
{
    cls.def(py::init([](const BWAPI::Point<int, OtherScale> &other) { return BWAPI::Point<int, Scale>(other); }),
            py::arg("other"));
}

// Declares ExactPosition / ExactPositionDifference from Stardust's BWAPI fork: a unit's position with subpixel
// precision, heading and velocity. Like the point types, they are immutable and hashable in Python.
inline void bind_exact_positions(py::module_ &m)
{
    using D = BWAPI::ExactPositionDifference;
    BWAPIValueClass<D> difference(m, "ExactPositionDifference");
    difference.def(py::init([](int32_t x, int32_t y) { return D{x, y}; }), py::arg("x"), py::arg("y"));
    difference.def_readonly("x", &D::x);
    difference.def_readonly("y", &D::y);
    difference.def("__eq__", [](const D &a, const D &b) { return a == b; }, py::is_operator());
    difference.def("__ne__", [](const D &a, const D &b) { return a != b; }, py::is_operator());
    difference.def("__lt__", [](const D &a, const D &b) { return a < b; }, py::is_operator());
    difference.def("__hash__", [](const D &d) { return py::hash(py::make_tuple(d.x, d.y)); });
    difference.def("__str__", [](const D &d)
    {
        std::ostringstream os;
        os << d;
        return os.str();
    });
    difference.def("__repr__", [](const D &d)
    {
        return "ExactPositionDifference(" + std::to_string(d.x) + ", " + std::to_string(d.y) + ")";
    });

    using E = BWAPI::ExactPosition;
    BWAPIValueClass<E> exact(m, "ExactPosition");
    exact.def(py::init<>());
    exact.def(py::init<uint32_t, uint32_t, int8_t, int32_t, int32_t>(),
              py::arg("x"), py::arg("y"), py::arg("heading"), py::arg("velocityX"), py::arg("velocityY"));
    exact.def_readonly("x", &E::x);
    exact.def_readonly("y", &E::y);
    exact.def_readonly("heading", &E::heading);
    exact.def_readonly("velocityX", &E::velocityX);
    exact.def_readonly("velocityY", &E::velocityY);
    exact.def("pos", &E::pos);
    exact.def("__sub__", [](const E &a, const E &b) { return a - b; }, py::is_operator());
    exact.def("__eq__", [](const E &a, const E &b) { return a == b; }, py::is_operator());
    exact.def("__ne__", [](const E &a, const E &b) { return a != b; }, py::is_operator());
    exact.def("__lt__", [](const E &a, const E &b) { return a < b; }, py::is_operator());
    exact.def("__hash__", [](const E &e)
    {
        return py::hash(py::make_tuple(e.x, e.y, e.heading, e.velocityX, e.velocityY));
    });
    exact.def("__str__", [](const E &e)
    {
        std::ostringstream os;
        os << e;
        return os.str();
    });
    exact.def("__repr__", [](const E &e)
    {
        return "ExactPosition(" + std::to_string(e.x) + ", " + std::to_string(e.y) + ", "
               + std::to_string(e.heading) + ", " + std::to_string(e.velocityX) + ", "
               + std::to_string(e.velocityY) + ")";
    });
    exact.def(py::pickle([](const E &e) { return py::make_tuple(e.x, e.y, e.heading, e.velocityX, e.velocityY); },
                         [](const py::tuple &t)
                         {
                             return E(t[0].cast<uint32_t>(), t[1].cast<uint32_t>(), t[2].cast<int8_t>(),
                                      t[3].cast<int32_t>(), t[4].cast<int32_t>());
                         }));
}

inline void bind_points(py::module_ &m)
{
    auto position = declare_point<1>(m, "Position");
    auto walkPosition = declare_point<8>(m, "WalkPosition");
    auto tilePosition = declare_point<32>(m, "TilePosition");

    add_point_conversion<1, 8>(position);
    add_point_conversion<1, 32>(position);
    add_point_conversion<8, 1>(walkPosition);
    add_point_conversion<8, 32>(walkPosition);
    add_point_conversion<32, 1>(tilePosition);
    add_point_conversion<32, 8>(tilePosition);
}

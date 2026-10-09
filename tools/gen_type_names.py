"""Generates 3rdparty/openbw/bwapi/bwapi/OpenBWData/BW/TypeNames.h: display names for OpenBW's unit, upgrade and tech
types, from the enums in 3rdparty/openbw/openbw/bwenums.h ("Protoss_High_Templar" becomes "High Templar").

Usage: python3 tools/gen_type_names.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENUMS = ROOT / "3rdparty" / "openbw" / "openbw" / "bwenums.h"
OUTPUT = ROOT / "3rdparty" / "openbw" / "bwapi" / "bwapi" / "OpenBWData" / "BW" / "TypeNames.h"

PREFIXES = ("Terran_", "Zerg_", "Protoss_", "Hero_", "Special_", "Critter_", "Spell_")
RENAMES = {
    "Terran_Siege_Tank_Tank_Mode": "Siege Tank",
    "Terran_Siege_Tank_Siege_Mode": "Siege Tank",
    "Terran_Vulture_Spider_Mine": "Spider Mine",
    "Zerg_Infested_Terran": "Infested Terran",
    "Zerg_Infested_Command_Center": "Infested CC",
}


def enum_values(text: str, name: str) -> list[str]:
    body = re.search(r"enum struct " + name + r"\s*:\s*int\s*\{(.*?)\};", text, re.S)
    if not body:
        raise SystemExit(f"enum {name} not found in {ENUMS}")
    return [entry.split("=")[0].strip() for entry in body.group(1).split(",") if entry.strip()]


def display(identifier: str) -> str:
    if identifier in RENAMES:
        return RENAMES[identifier]
    for prefix in PREFIXES:
        if identifier.startswith(prefix):
            identifier = identifier[len(prefix):]
            break
    return identifier.replace("_", " ")


def main() -> int:
    text = ENUMS.read_text()
    lines = ["// Display names for OpenBW's types, generated from bwenums.h by tools/gen_type_names.py - do not edit.",
             "#pragma once", "", "namespace BW {", ""]
    for enum, function in (("UnitTypes", "UnitTypeName"), ("UpgradeTypes", "UpgradeTypeName"),
                           ("TechTypes", "TechTypeName")):
        names = [display(value) for value in enum_values(text, enum)]
        lines.append(f"inline const char *{function}(int id)")
        lines.append("{")
        lines.append("    static const char *const names[] = {")
        lines += [f'            "{name}",' for name in names]
        lines.append("    };")
        lines.append(f"    return id >= 0 && id < {len(names)} ? names[id] : \"?\";")
        lines.append("}")
        lines.append("")
    lines += ["}", ""]
    OUTPUT.write_text("\n".join(lines))
    print(f"{OUTPUT.relative_to(ROOT)} written")
    return 0


if __name__ == "__main__":
    sys.exit(main())

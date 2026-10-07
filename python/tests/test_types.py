import pickle

import bwapi
from bwapi import (Colors, Errors, Orders, Races, TechTypes, Text, UnitCommand, UnitType, UnitTypes, UpgradeTypes,
                   WeaponTypes)


def test_unit_type_data():
    zealot = UnitTypes.Protoss_Zealot
    assert (zealot.mineralPrice(), zealot.gasPrice(), zealot.supplyRequired()) == (100, 0, 4)
    assert zealot.whatBuilds() == (UnitTypes.Protoss_Gateway, 1)
    assert zealot.groundWeapon() == WeaponTypes.Psi_Blades
    assert WeaponTypes.Psi_Blades.damageAmount() == 8
    assert UnitTypes.Protoss_Dragoon.requiredUnits() == {
        UnitTypes.Protoss_Gateway: 1,
        UnitTypes.Protoss_Cybernetics_Core: 1,
    }


def test_race_helpers():
    assert Races.Protoss.getWorker() == UnitTypes.Protoss_Probe
    assert Races.Zerg.getSupplyProvider() == UnitTypes.Zerg_Overlord
    assert Races.Terran.getResourceDepot() == UnitTypes.Terran_Command_Center


def test_tech_and_upgrades():
    assert TechTypes.Psionic_Storm.energyCost() == 75
    assert UpgradeTypes.Protoss_Ground_Weapons.mineralPrice() == 100
    assert UpgradeTypes.Protoss_Ground_Weapons.mineralPrice(2) == 150


def test_lookup_by_name():
    assert UnitType.getType("protoss probe") == UnitTypes.Protoss_Probe
    assert UnitType.getType("not a unit") == UnitTypes.Unknown


def test_type_sets_are_python_sets():
    all_types = UnitTypes.allUnitTypes()
    assert isinstance(all_types, set)
    assert UnitTypes.Protoss_Probe in all_types
    assert {t.getName() for t in UnitTypes.Protoss_Gateway.buildsWhat()} == {
        "Protoss_Zealot", "Protoss_Dragoon", "Protoss_High_Templar", "Protoss_Dark_Templar",
    }


def test_types_are_hashable_comparable_and_picklable():
    probe = UnitTypes.Protoss_Probe
    assert {probe: 1}[UnitType(probe.getID())] == 1
    assert probe != UnitTypes.Protoss_Zealot
    assert probe != WeaponTypes.Psi_Blades  # different BWAPI types never compare equal
    assert sorted([UnitTypes.Zerg_Zergling, UnitTypes.Terran_Marine])[0] == UnitTypes.Terran_Marine
    assert pickle.loads(pickle.dumps(probe)) == probe
    assert int(probe) == probe.getID()
    assert str(probe) == "Protoss_Probe"


def test_keyword_names_have_underscore_aliases():
    assert UnitTypes.None_ == getattr(UnitTypes, "None")
    assert Orders.None_ == getattr(Orders, "None")
    assert bwapi.CoordinateType.None_ != bwapi.CoordinateType.Map


def test_text_colors_and_enums():
    assert Text.White == "\x04"
    assert Colors.Blue.blue() > Colors.Blue.red()
    assert bwapi.Color(10, 20, 30).isValid()
    assert bwapi.TextSize.Large.name == "Large"
    assert Errors.Insufficient_Supply.getName() == "Insufficient_Supply"


def test_unit_command_defaults():
    command = UnitCommand()
    assert command.getTarget() is None
    assert command.getType() == bwapi.UnitCommandTypes.None_


def test_no_game_offline():
    assert bwapi.Broodwar is None

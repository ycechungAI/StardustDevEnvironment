"""Port of Players/Players.{h,cpp}: per-player grids and upgraded unit stats."""

from __future__ import annotations

import bwapi
from bwapi import DamageTypes, TechType, UnitSizeTypes, UnitType, UnitTypes, UpgradeType, WeaponType, WeaponTypes
from stardust.cpp import cdiv
from stardust.instrumentation import log
from stardust.players.grid import Grid
from stardust.players.upgrade_tracker import UpgradeTracker

_player_to_upgrade_tracker: dict[bwapi.Player, UpgradeTracker] = {}
_player_to_grid: dict[bwapi.Player, Grid] = {}


def _get_upgrade_tracker(player: bwapi.Player) -> UpgradeTracker:
    tracker = _player_to_upgrade_tracker.get(player)
    if tracker is None:
        if player is None:
            log.get("ERROR: Trying to get an upgrade tracker for a null player")
        tracker = _player_to_upgrade_tracker[player] = UpgradeTracker(player)
    return tracker


def initialize() -> None:
    _player_to_upgrade_tracker.clear()
    _player_to_grid.clear()


def update() -> None:
    for player, tracker in list(_player_to_upgrade_tracker.items()):
        tracker.update(grid(player))


def grid(player: bwapi.Player) -> Grid:
    result = _player_to_grid.get(player)
    if result is None:
        result = _player_to_grid[player] = Grid(_get_upgrade_tracker(player), player)
    return result


def weapon_damage(player: bwapi.Player, weapon: WeaponType) -> int:
    return _get_upgrade_tracker(player).weapon_damage(weapon)


def weapon_range(player: bwapi.Player, weapon: WeaponType) -> int:
    return _get_upgrade_tracker(player).weapon_range(weapon)


def unit_ground_cooldown(player: bwapi.Player, unit_type: UnitType) -> int:
    # Handle weird units that don't have proper cooldowns
    if unit_type in (UnitTypes.Protoss_Scarab, UnitTypes.Protoss_Reaver):
        return 60
    if unit_type in (UnitTypes.Protoss_Interceptor, UnitTypes.Protoss_Carrier):
        return 38
    # Assume marines are in bunkers
    if unit_type == UnitTypes.Terran_Bunker:
        return _get_upgrade_tracker(player).unit_ground_cooldown(UnitTypes.Terran_Marine)
    return _get_upgrade_tracker(player).unit_ground_cooldown(unit_type)


def unit_air_cooldown(player: bwapi.Player, unit_type: UnitType) -> int:
    # Handle weird units that don't have proper cooldowns
    if unit_type in (UnitTypes.Protoss_Interceptor, UnitTypes.Protoss_Carrier):
        return 38
    # Assume marines are in bunkers
    if unit_type == UnitTypes.Terran_Bunker:
        return _get_upgrade_tracker(player).unit_air_cooldown(UnitTypes.Terran_Marine)
    return _get_upgrade_tracker(player).unit_air_cooldown(unit_type)


def unit_top_speed(player: bwapi.Player, unit_type: UnitType) -> float:
    return _get_upgrade_tracker(player).unit_top_speed(unit_type)


def unit_bw_top_speed(player: bwapi.Player, unit_type: UnitType) -> int:
    return _get_upgrade_tracker(player).unit_bw_top_speed(unit_type)


def unit_armor(player: bwapi.Player, unit_type: UnitType) -> int:
    return _get_upgrade_tracker(player).unit_armor(unit_type)


def unit_sight_range(player: bwapi.Player, unit_type: UnitType) -> int:
    return _get_upgrade_tracker(player).unit_sight_range(unit_type)


def attack_damage(attacking_player: bwapi.Player, attacking_unit: UnitType, target_player: bwapi.Player,
                  target_unit: UnitType, weapon_override: WeaponType = WeaponTypes.None_) -> int:
    weapon = weapon_override
    if weapon == WeaponTypes.None_:
        weapon = attacking_unit.airWeapon() if target_unit.isFlyer() else attacking_unit.groundWeapon()
    if weapon == WeaponTypes.None_:
        return 0

    hits = attacking_unit.maxAirHits() if target_unit.isFlyer() else attacking_unit.maxGroundHits()
    damage = (weapon_damage(attacking_player, weapon) - unit_armor(target_player, target_unit)) * hits

    # (C++ integer division truncates toward zero; damage can be negative against heavy armor)
    damage_type = weapon.damageType()
    size = target_unit.size()
    if damage_type == DamageTypes.Concussive:
        if size == UnitSizeTypes.Large:
            damage = cdiv(damage, 4)
        elif size == UnitSizeTypes.Medium:
            damage = cdiv(damage, 2)
    elif damage_type == DamageTypes.Explosive:
        if size == UnitSizeTypes.Small:
            damage = cdiv(damage, 2)
        elif size == UnitSizeTypes.Medium:
            damage = cdiv(damage * 3, 4)

    return min(128, damage)


def has_researched(player: bwapi.Player, tech_type: TechType) -> bool:
    return _get_upgrade_tracker(player).has_researched(tech_type)


def set_has_researched(player: bwapi.Player, tech_type: TechType) -> None:
    import stardust.strategist.opponent_economic_model as opponent_economic_model

    tracker = _get_upgrade_tracker(player)
    if player == bwapi.Broodwar.enemy() and not tracker.has_researched(tech_type):
        log.get(f"Enemy has researched {tech_type}")
    tracker.set_has_researched(tech_type)
    if player == bwapi.Broodwar.enemy():
        opponent_economic_model.opponent_researched(tech_type)


def upgrade_level(player: bwapi.Player, upgrade_type: UpgradeType) -> int:
    return _get_upgrade_tracker(player).upgrade_level(upgrade_type)


def set_weapon_range(player: bwapi.Player, weapon: WeaponType, weapon_range: int) -> None:
    _get_upgrade_tracker(player).set_weapon_range(weapon, weapon_range, grid(player))

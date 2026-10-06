"""Port of Players/UpgradeTracker.{h,cpp}: caches a player's upgraded unit and weapon stats, and keeps the player's
grid in sync when they improve. Values are only ever raised (we may learn of upgrades before BWAPI reports them)."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import TechType, UnitType, UnitTypes, UpgradeType, UpgradeTypes, WeaponType, WeaponTypes
from stardust.cpp import to_int

if TYPE_CHECKING:
    from stardust.players.grid import Grid
    from stardust.units.unit import Unit


class UpgradeTracker:
    def __init__(self, player: bwapi.Player) -> None:
        self.player = player
        self._weapon_damage: dict[WeaponType, int] = {}
        self._weapon_range: dict[WeaponType, int] = {}
        self._unit_ground_cooldown: dict[UnitType, int] = {}
        self._unit_air_cooldown: dict[UnitType, int] = {}
        self._unit_top_speed: dict[UnitType, float] = {}
        self._unit_bw_top_speed: dict[UnitType, int] = {}
        self._unit_armor: dict[UnitType, int] = {}
        self._unit_sight_range: dict[UnitType, int] = {}
        self._has_researched: dict[TechType, bool] = {}
        self._upgrade_level: dict[UpgradeType, int] = {}

    def _known_units(self) -> set[Unit]:
        from stardust.units import units

        return set(units.all_mine()) if self.player == bwapi.Broodwar.self() else set(units.all_enemy())

    @staticmethod
    def _uses_weapon(unit: Unit, weapon: WeaponType, carrier_uses_interceptor: bool) -> bool:
        weapon_unit_type = unit.type
        if unit.type == UnitTypes.Terran_Bunker:
            weapon_unit_type = UnitTypes.Terran_Marine
        if carrier_uses_interceptor and unit.type == UnitTypes.Protoss_Carrier:
            weapon_unit_type = UnitTypes.Protoss_Interceptor
        return (unit.last_position_valid and not unit.being_manufactured_or_carried and not unit.immobile
                and (weapon_unit_type.groundWeapon() == weapon or weapon_unit_type.airWeapon() == weapon))

    def update(self, grid: Grid) -> None:
        """Updates the items that have been queried previously."""
        player = self.player

        for weapon, damage in list(self._weapon_damage.items()):
            current = player.damage(weapon)
            if current > damage:
                # Update the grid for all known units with this weapon type
                for unit in self._known_units():
                    if self._uses_weapon(unit, weapon, True):
                        grid.unit_weapon_damage_upgraded(unit.type, unit.last_position, weapon, damage, current)
                self._weapon_damage[weapon] = current

        for weapon, weapon_range in list(self._weapon_range.items()):
            current = player.weaponMaxRange(weapon)
            if current > weapon_range:
                for unit in self._known_units():
                    if self._uses_weapon(unit, weapon, False):
                        grid.unit_weapon_range_upgraded(unit.type, unit.last_position, weapon, weapon_range, current)
                self._weapon_range[weapon] = current

        for unit_type, cooldown in list(self._unit_ground_cooldown.items()):
            current = self._ground_cooldown_now(unit_type)
            if current > cooldown:
                self._unit_ground_cooldown[unit_type] = current

        for unit_type, cooldown in list(self._unit_air_cooldown.items()):
            current = unit_type.airWeapon().damageCooldown()
            if current > cooldown:
                self._unit_air_cooldown[unit_type] = current

        for unit_type, top_speed in list(self._unit_top_speed.items()):
            current_speed = player.topSpeed(unit_type)
            if current_speed > top_speed:
                self._unit_top_speed[unit_type] = current_speed
                self._unit_bw_top_speed.pop(unit_type, None)

        for unit_type, armor in list(self._unit_armor.items()):
            current = player.armor(unit_type)
            if current > armor:
                self._unit_armor[unit_type] = current

        for unit_type, sight_range in list(self._unit_sight_range.items()):
            current = player.sightRange(unit_type)
            if current > sight_range:
                for unit in self._known_units():
                    if (unit.last_position_valid and not unit.being_manufactured_or_carried and not unit.immobile
                            and unit.type == unit_type):
                        grid.unit_sight_range_upgraded(unit.type, unit.last_position, sight_range, current)
                self._unit_sight_range[unit_type] = current

        for tech_type, researched in list(self._has_researched.items()):
            if not researched and player.hasResearched(tech_type):
                self._has_researched[tech_type] = True

        for upgrade_type, level in list(self._upgrade_level.items()):
            current = player.getUpgradeLevel(upgrade_type)
            if current and current > level:
                self._upgrade_level[upgrade_type] = current

    def weapon_damage(self, weapon: WeaponType) -> int:
        damage = self._weapon_damage.get(weapon)
        if damage is None:
            damage = self._weapon_damage[weapon] = self.player.damage(weapon)
        return damage

    def weapon_range(self, weapon: WeaponType) -> int:
        # For interceptors and scarabs, return the range of the carrier and reaver
        if weapon in (WeaponTypes.Pulse_Cannon, WeaponTypes.Scarab):
            return 256
        weapon_range = self._weapon_range.get(weapon)
        if weapon_range is None:
            weapon_range = self._weapon_range[weapon] = self.player.weaponMaxRange(weapon)
        return weapon_range

    def _ground_cooldown_now(self, unit_type: UnitType) -> int:
        current = unit_type.groundWeapon().damageCooldown()
        if unit_type == UnitTypes.Zerg_Zergling and self.upgrade_level(UpgradeTypes.Adrenal_Glands) > 0:
            current = min(max(current // 2, 5), 250)
        return current

    def unit_ground_cooldown(self, unit_type: UnitType) -> int:
        cooldown = self._unit_ground_cooldown.get(unit_type)
        if cooldown is None:
            cooldown = self._unit_ground_cooldown[unit_type] = self._ground_cooldown_now(unit_type)
        return cooldown

    def unit_air_cooldown(self, unit_type: UnitType) -> int:
        cooldown = self._unit_air_cooldown.get(unit_type)
        if cooldown is None:
            cooldown = self._unit_air_cooldown[unit_type] = unit_type.airWeapon().damageCooldown()
        return cooldown

    def unit_top_speed(self, unit_type: UnitType) -> float:
        speed = self._unit_top_speed.get(unit_type)
        if speed is None:
            speed = self._unit_top_speed[unit_type] = self.player.topSpeed(unit_type)
        return speed

    def unit_bw_top_speed(self, unit_type: UnitType) -> int:
        speed = self._unit_bw_top_speed.get(unit_type)
        if speed is None:
            speed = self._unit_bw_top_speed[unit_type] = to_int(self.unit_top_speed(unit_type) * 256.0)
        return speed

    def unit_armor(self, unit_type: UnitType) -> int:
        armor = self._unit_armor.get(unit_type)
        if armor is None:
            armor = self._unit_armor[unit_type] = self.player.armor(unit_type)
        return armor

    def unit_sight_range(self, unit_type: UnitType) -> int:
        sight_range = self._unit_sight_range.get(unit_type)
        if sight_range is None:
            sight_range = self._unit_sight_range[unit_type] = self.player.sightRange(unit_type)
        return sight_range

    def has_researched(self, tech_type: TechType) -> bool:
        researched = self._has_researched.get(tech_type)
        if researched is None:
            researched = self._has_researched[tech_type] = self.player.hasResearched(tech_type)
        return researched

    def set_has_researched(self, tech_type: TechType) -> None:
        self._has_researched[tech_type] = True

    def upgrade_level(self, upgrade_type: UpgradeType) -> int:
        level = self._upgrade_level.get(upgrade_type)
        if level is None:
            level = self._upgrade_level[upgrade_type] = self.player.getUpgradeLevel(upgrade_type)
        return level

    def set_weapon_range(self, weapon: WeaponType, weapon_range: int, grid: Grid) -> None:
        current = self._weapon_range.get(weapon, self.player.weaponMaxRange(weapon))
        if weapon_range <= current:
            return

        # Update the grid for all known units with this weapon type
        for unit in self._known_units():
            if self._uses_weapon(unit, weapon, False):
                grid.unit_weapon_range_upgraded(unit.type, unit.last_position, weapon, current, weapon_range)

        self._weapon_range[weapon] = weapon_range

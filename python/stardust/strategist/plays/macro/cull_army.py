"""Port of Strategist/Plays/Macro/CullArmy.{h,cpp}: culls some of our army if we need supply room for something
else."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import UnitTypes
from stardust.map import game_map
from stardust.strategist.play import Play, PlayUnitRequirement, UnitCallback

if TYPE_CHECKING:
    from stardust.units.my_unit import MyUnit


class CullArmy(Play):
    def __init__(self, supply_needed: int) -> None:
        super().__init__("CullArmy")
        self.supply_needed = supply_needed
        self._units: list[MyUnit] = []

    def update(self) -> None:
        # Clear dead units and count how much supply we have in the current set of units
        self._units = [unit for unit in self._units if unit.exists()]
        supply_count = sum(unit.type.supplyRequired() for unit in self._units)

        if self.supply_needed <= 0:
            self.status.complete = True
            return

        # Reserve units if needed
        needed_units = (self.supply_needed - supply_count + 11) // 4
        if needed_units > 0:
            my_main = game_map.get_my_main()
            assert my_main is not None
            self.status.unit_requirements.append(
                PlayUnitRequirement(needed_units, UnitTypes.Protoss_Dragoon, my_main.get_position()))

        # Micro the units: everybody attacks the next unit
        for unit, next_unit in zip(self._units, self._units[1:]):
            # If the unit is stuck, unstick it
            if unit.unstick():
                continue

            # If the unit is not ready (i.e. is already in the middle of an attack), don't touch it
            if not unit.is_ready():
                continue

            # Attack the next unit in the list
            unit.attack_unit(next_unit)

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        units_copy = self._units
        self._units = []
        for unit in units_copy:
            removed_unit_callback(unit)

    def add_unit(self, unit: MyUnit) -> None:
        self._units.append(unit)

    def remove_unit(self, unit: MyUnit) -> None:
        self._units = [other for other in self._units if other is not unit]

"""Port of General/EnemyArmy.{h,cpp}: a group of enemy units out on the map."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import Position
from stardust.cpp import cdiv

if TYPE_CHECKING:
    from stardust.units.unit import Unit

# Distance threshold to center of army required to add another unit
_ADD_THRESHOLD = 480


class EnemyArmy:
    __slots__ = ("center", "units")

    def __init__(self, unit: Unit) -> None:
        self.center = unit.sim_position
        self.units: set[Unit] = {unit}

    def try_add_unit(self, unit: Unit) -> bool:
        dist = unit.get_distance(self.center)
        count = len(self.units)
        if dist > _ADD_THRESHOLD + (count * 120) // (count + 10):
            return False

        self.units.add(unit)

        count = len(self.units)
        self.center = Position(cdiv(self.center.x * (count - 1) + unit.sim_position.x, count),
                               cdiv(self.center.y * (count - 1) + unit.sim_position.y, count))
        return True

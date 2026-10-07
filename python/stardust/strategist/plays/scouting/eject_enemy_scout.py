"""Port of Strategist/Plays/Scouting/EjectEnemyScout.{h,cpp}: uses a dragoon to chase an enemy worker scout (or
overlord) out of our main."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwem
from bwapi import UnitTypes, WalkPosition
from stardust import common
from stardust.map import game_map
from stardust.strategist.play import Play, PlayUnitRequirement, UnitCallback
from stardust.units import units

if TYPE_CHECKING:
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit


def _in_main_area(unit: Unit) -> bool:
    if not unit.last_position_valid:
        return False
    return bwem.Instance().GetArea(WalkPosition(unit.last_position)) in game_map.get_my_main_areas()


class EjectEnemyScout(Play):
    def __init__(self) -> None:
        super().__init__("EjectEnemyScout")
        self._scout: Unit | None = None
        self._dragoon: MyUnit | None = None
        self._has_had_scout = False

    def update(self) -> None:
        # First check if our current enemy scout should be cleared
        if self._scout is not None and (not self._scout.exists() or not _in_main_area(self._scout)):
            self._scout = None

        # Next scan to get an enemy scout or combat unit
        enemy_combat_unit_in_base = False
        new_scout: Unit | None = None
        for unit in units.all_enemy():
            if not unit.exists():
                continue

            if unit.type.isWorker() or unit.type == UnitTypes.Zerg_Overlord:
                if _in_main_area(unit):
                    new_scout = unit
            elif unit.can_attack_ground() and _in_main_area(unit):
                enemy_combat_unit_in_base = True
                break

        if self._scout is None:
            self._scout = new_scout
            if new_scout is not None:
                self._has_had_scout = True

        scout = self._scout
        dragoon = self._dragoon

        # End the play if there is no scout after frame 10000
        if scout is None and common.current_frame > 10000:
            self.status.complete = True
            return

        # Release the dragoon if there is no scout or an enemy combat unit in the base
        if dragoon is not None and (enemy_combat_unit_in_base or scout is None):
            self.status.removed_units.append(dragoon)
            return

        # If there is a scout and no enemy unit in our base, make sure we get a dragoon
        if scout is not None and dragoon is None and not enemy_combat_unit_in_base:
            my_main = game_map.get_my_main()
            assert my_main is not None
            self.status.unit_requirements.append(
                PlayUnitRequirement(1, UnitTypes.Protoss_Dragoon, my_main.get_position(), 1500))

        # If we have a dragoon and a scout, attack!
        # (Stardust checks unstick and readiness twice, so unstick can be called twice.)
        if scout is not None and dragoon is not None and not dragoon.unstick() and dragoon.is_ready():
            if dragoon.unstick():
                pass
            elif not dragoon.is_ready():
                pass
            else:
                dragoon.attack_unit(scout)

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        if self._dragoon is not None:
            movable_unit_callback(self._dragoon)

    def add_unit(self, unit: MyUnit) -> None:
        self._dragoon = unit

    def remove_unit(self, unit: MyUnit) -> None:
        self._dragoon = None

    def has_ejected_scout(self) -> bool:
        """Whether the play has ejected a scout."""
        return (self._has_had_scout and self._scout is None) or (common.current_frame > 5000 and self._scout is None)

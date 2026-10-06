"""Port of Strategist/Plays/Offensive/AttackIslandExpansion.{h,cpp}: attacks an enemy island expansion with our
carriers."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import UnitTypes
from stardust.general import general
from stardust.general.squads.attack_base_squad import AttackBaseSquad
from stardust.map import game_map
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_NORMAL, Play, PlayUnitRequirement, ProductionGoals, add_goal
from stardust.units import units

if TYPE_CHECKING:
    from stardust.map.base import Base


class AttackIslandExpansion(Play):
    def __init__(self, base: Base) -> None:
        super().__init__(f"Attack island expansion @ {base.get_tile_position()}")
        self.base = base
        self._squad = AttackBaseSquad(base)
        general.add_squad(self._squad)

    def get_squad(self) -> AttackBaseSquad:
        return self._squad

    def update(self) -> None:
        squad = self._squad

        # Complete the play when the base is no longer owned by the enemy
        if self.base.owner != bwapi.Broodwar.enemy():
            self.status.complete = True
            return

        # Update detection - release observers when no longer needed, request observers when needed
        need_detection = squad.needs_detection() or units.has_enemy_built(UnitTypes.Terran_Vulture_Spider_Mine)
        detectors = squad.get_detectors()
        if not need_detection and detectors:
            self.status.removed_units.extend(detectors)
        elif need_detection and not detectors:
            self.status.unit_requirements.append(
                PlayUnitRequirement(1, UnitTypes.Protoss_Observer, squad.get_target_position()))

            # Release the squad units until we get the detector
            self.status.removed_units = squad.get_units()
            return

        # Take all carriers
        my_main = game_map.get_my_main()
        assert my_main is not None
        self.status.unit_requirements.append(
            PlayUnitRequirement(10, UnitTypes.Protoss_Carrier, my_main.get_position()))

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        # Build an observer if we need one
        for unit_requirement in self.status.unit_requirements:
            if unit_requirement.type != UnitTypes.Protoss_Observer:
                continue
            if unit_requirement.count < 1:
                continue

            add_goal(prioritized_production_goals, PRIORITY_NORMAL,
                     UnitProductionGoal(self.label, unit_requirement.type, unit_requirement.count, 1))

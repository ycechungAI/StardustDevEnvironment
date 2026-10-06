"""Port of Strategist/Plays/Offensive/AttackExpansion.{h,cpp}: attacks a weakly-defended enemy expansion with a
separate squad."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import UnitTypes
from stardust.general import general
from stardust.general.squads.attack_base_squad import AttackBaseSquad
from stardust.general.unit_cluster import combat_sim
from stardust.players import players
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_NORMAL, Play, PlayUnitRequirement, ProductionGoals, add_goal
from stardust.units import units

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.map.path_finding.navigation_grid import GridNode


def _safe_grid_node(grid_node: GridNode) -> bool:
    """Prefer units that have a safe path to the base."""
    return grid_node.cost < 1200 or players.grid(bwapi.Broodwar.enemy()).ground_threat(grid_node.center()) == 0


class AttackExpansion(Play):
    def __init__(self, base: Base, enemy_defense_value: int) -> None:
        super().__init__(f"Attack expansion @ {base.get_tile_position()}")
        self.base = base
        self.enemy_defense_value = enemy_defense_value
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

        # Gather enemy threats at the base
        enemy_value = 0
        for unit in units.enemy_at_base(self.base):
            enemy_value += combat_sim.unit_value(unit)

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

        # Reserve enough units to attack the base
        our_value = sum(combat_sim.unit_value(unit) for unit in squad.get_units())

        requested_units = 0
        dragoon_value = combat_sim.unit_value(UnitTypes.Protoss_Dragoon)
        while our_value < enemy_value * 2:
            requested_units += 1
            our_value += dragoon_value

        # Ensure we have at least three units whenever we attack a base
        requested_units = max(requested_units, 3 - len(squad.get_units()))

        # TODO: Request zealot or dragoon when we have that capability
        if requested_units > 0:
            self.status.unit_requirements.append(
                PlayUnitRequirement(requested_units, UnitTypes.Protoss_Dragoon, self.base.get_position(),
                                    allow_from_vanguard_cluster=False, grid_node_predicate=_safe_grid_node,
                                    allow_failing_grid_node_predicate=True))

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        # Build an observer if we need one
        for unit_requirement in self.status.unit_requirements:
            if unit_requirement.type != UnitTypes.Protoss_Observer:
                continue
            if unit_requirement.count < 1:
                continue

            add_goal(prioritized_production_goals, PRIORITY_NORMAL,
                     UnitProductionGoal(self.label, unit_requirement.type, unit_requirement.count, 1))

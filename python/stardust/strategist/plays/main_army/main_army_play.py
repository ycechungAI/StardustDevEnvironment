"""Port of Strategist/Plays/MainArmy/MainArmyPlay.{h,cpp}: the base class of the plays controlling our main army."""

from __future__ import annotations

from abc import abstractmethod
from typing import TYPE_CHECKING

from bwapi import UnitTypes
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_NORMAL, Play, PlayUnitRequirement, ProductionGoals, add_goal
from stardust.units import units

if TYPE_CHECKING:
    from stardust.general.squad import Squad


class MainArmyPlay(Play):
    @abstractmethod
    def is_defensive(self) -> bool: ...

    @abstractmethod
    def get_squad(self) -> Squad:
        """Main army plays always have a squad."""

    def receives_unassigned_units(self) -> bool:
        return True

    def update(self) -> None:
        import stardust.strategist.strategies as strategies
        from stardust.strategist.strategy_engines.pv_p.pv_p import PvP

        # Update detection - release observers when no longer needed, request observers when needed
        squad = self.get_squad()
        if squad is not None:
            desired_detectors = 0
            if (units.has_enemy_built(UnitTypes.Protoss_Dark_Templar)
                    or strategies.is_our_strategy(PvP.OurStrategy.AntiDarkTemplarRush)):
                desired_detectors = 1
            if squad.needs_detection():
                desired_detectors = 2

            detectors = squad.get_detectors()
            if len(detectors) < desired_detectors:
                self.status.unit_requirements.append(PlayUnitRequirement(
                    desired_detectors - len(detectors), UnitTypes.Protoss_Observer, squad.get_target_position()))

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        # Produce to fulfill any unit requirements
        for unit_requirement in self.status.unit_requirements:
            if unit_requirement.count < 1:
                continue
            add_goal(prioritized_production_goals, PRIORITY_NORMAL,
                     UnitProductionGoal(self.label, unit_requirement.type, unit_requirement.count,
                                        (unit_requirement.count + 1) // 2))

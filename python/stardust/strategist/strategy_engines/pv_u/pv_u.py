"""Port of Strategist/StrategyEngines/PvU.h and PvU/PvU.cpp: the strategy engine against Random, used until the enemy
race is known."""

from __future__ import annotations

from enum import Enum

from bwapi import UnitTypes
from stardust import opponent
from stardust.builder import building_placement
from stardust.instrumentation import log
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_MAINARMY, MineralReservations, Play, ProductionGoals, add_goal
from stardust.strategist.plays.macro.saturate_bases import SaturateBases
from stardust.strategist.plays.main_army.defend_my_main import DefendMyMain
from stardust.strategist.plays.main_army.forge_fast_expand import ForgeFastExpand
from stardust.strategist.plays.scouting.early_game_worker_scout import EarlyGameWorkerScout
from stardust.strategist.strategy_engine import StrategyEngine


class PvU(StrategyEngine):
    class OurStrategy(Enum):
        """Against random we either open zealots or FFE. Values are Stardust's strategy names."""
        TwoGateZealots = "TwoGateZealots"
        ForgeFastExpand = "ForgeFastExpand"

    def __init__(self) -> None:
        self.our_strategy = PvU.OurStrategy.TwoGateZealots

    def initialize(self, plays: list[Play], transitioning_from_random: bool, opening_override: str) -> None:
        if opening_override:
            plays.clear()

        plays.append(SaturateBases())
        plays.append(EarlyGameWorkerScout())

        opening = opening_override
        if not opening:
            opening = opponent.select_opening_ucb1([PvU.OurStrategy.ForgeFastExpand.value,
                                                    PvU.OurStrategy.TwoGateZealots.value])
        if opening == PvU.OurStrategy.ForgeFastExpand.value and building_placement.has_forge_gateway_wall():
            plays.append(ForgeFastExpand())
            self.our_strategy = PvU.OurStrategy.ForgeFastExpand
        else:
            plays.append(DefendMyMain())
        log.get(f"Selected opening {self.our_strategy.value}")

        opponent.add_my_strategy_change(self.our_strategy.value)
        opponent.add_enemy_strategy_change("RandomUnknown")

    def update_plays(self, plays: list[Play]) -> None:
        # This engine never updates plays - it transitions to another engine once the enemy race is known
        pass

    def update_production(self, plays: list[Play], prioritized_production_goals: ProductionGoals,
                          mineral_reservations: MineralReservations) -> None:
        # Always start with two-gate zealots until we know what race the opponent is
        add_goal(prioritized_production_goals, PRIORITY_MAINARMY,
                 UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, -1, 2))

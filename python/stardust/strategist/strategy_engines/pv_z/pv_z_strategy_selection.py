"""Port of Strategist/StrategyEngines/PvZ/PvZStrategySelection.cpp: PvZ::chooseOurStrategy."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import UnitTypes, UpgradeTypes
from stardust import common
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.strategist.plays.main_army.attack_enemy_base import AttackEnemyBase
from stardust.strategist.plays.main_army.defend_my_main import DefendMyMain
from stardust.strategist.strategy_engine import get_main_army_play
from stardust.units import units

if TYPE_CHECKING:
    from stardust.strategist.play import Play
    from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ


def choose_our_strategy(engine: PvZ, new_enemy_strategy: PvZ.ZergStrategy, plays: list[Play]) -> PvZ.OurStrategy:
    from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ

    S = PvZ.ZergStrategy
    O = PvZ.OurStrategy  # noqa: E741
    frame = common.current_frame
    all_in_strategies = (S.WorkerRush, S.ZerglingRush, S.ZerglingAllIn)

    enemy_strategy_stable_for = 0
    if new_enemy_strategy == engine.enemy_strategy:
        enemy_strategy_stable_for = frame - engine.enemy_strategy_changed

    def can_transition_from_anti_all_in() -> bool:
        strategy_is_all_in = new_enemy_strategy in all_in_strategies

        # Count our total combat units
        main_army_play = get_main_army_play(plays)
        completed_units = main_army_play.get_squad().get_unit_count_by_type() if main_army_play is not None else {}
        incomplete_units = main_army_play.assigned_incomplete_units if main_army_play is not None else {}
        unit_count = (completed_units.get(UnitTypes.Protoss_Zealot, 0)
                      + incomplete_units.get(UnitTypes.Protoss_Zealot, 0)
                      + completed_units.get(UnitTypes.Protoss_Dragoon, 0)
                      + incomplete_units.get(UnitTypes.Protoss_Dragoon, 0))

        # Estimate how many combat units we need
        if enemy_strategy_stable_for < 240 or strategy_is_all_in:
            required_units = 20 if bwapi.Broodwar.self().getUpgradeLevel(UpgradeTypes.Singularity_Charge) == 0 else 15
        else:
            required_units = 2 + units.count_enemy(UnitTypes.Zerg_Zergling) // 3

        return unit_count >= required_units

    def normal_transition() -> PvZ.OurStrategy | None:
        """The transitions of the Normal strategy (which Defensive falls through to)."""
        if new_enemy_strategy == S.SunkenContain:
            return O.AntiSunkenContain

        if new_enemy_strategy in all_in_strategies and not can_transition_from_anti_all_in():
            return O.AntiAllIn

        # Transition to mid-game when the enemy has lair tech or we are on two bases
        # TODO: Also transition to mid-game in other cases
        if new_enemy_strategy in (S.Lair, S.MutaRush) or units.count_completed(UnitTypes.Protoss_Nexus) > 1:
            return O.MidGame

        return None

    strategy = engine.our_strategy
    for _ in range(10):
        # Each case either returns, moves to another strategy or keeps the current one (None)
        next_strategy: PvZ.OurStrategy | None = None
        if strategy == O.EarlyGameDefense:
            # Transition appropriately as soon as we have an idea of what the enemy is doing
            if new_enemy_strategy == S.Unknown:
                return strategy
            elif new_enemy_strategy == S.SunkenContain:
                next_strategy = O.AntiSunkenContain
            elif new_enemy_strategy in all_in_strategies:
                next_strategy = O.AntiAllIn
            elif new_enemy_strategy == S.PoolBeforeHatchery:
                next_strategy = O.Defensive
            elif new_enemy_strategy in (S.HatcheryBeforePool, S.HydraBust):
                next_strategy = O.Normal
            elif new_enemy_strategy == S.Turtle:
                next_strategy = O.FastExpansion
            elif new_enemy_strategy in (S.Lair, S.MutaRush):
                next_strategy = O.MidGame
        elif strategy == O.AntiAllIn:
            if new_enemy_strategy == S.SunkenContain:
                next_strategy = O.AntiSunkenContain

            # Transition to normal when we consider it safe to do so
            elif can_transition_from_anti_all_in():
                next_strategy = O.EarlyGameDefense if new_enemy_strategy == S.Unknown else O.Normal
        elif strategy == O.AntiSunkenContain:
            if new_enemy_strategy != S.SunkenContain:
                next_strategy = O.Normal
        elif strategy in (O.SairSpeedlot, O.FFEDragoons):
            main_army_play = get_main_army_play(plays)
            if main_army_play is not None:
                # If the enemy is doing a rush and our main army play has transitioned to defending the main, switch
                # to anti all-in strategy
                if engine.enemy_strategy == S.ZerglingRush and type(main_army_play) is DefendMyMain:
                    next_strategy = O.AntiAllIn
                elif strategy == O.SairSpeedlot:
                    # Transition to mid game when we have gone on the attack, indicating the opening is done
                    if type(main_army_play) is AttackEnemyBase:
                        next_strategy = O.MidGame

                # Transition to mid game when:
                # - enemy has mutalisks
                # - we have a third
                # - we are past frame 12000
                elif (units.count_enemy(UnitTypes.Zerg_Mutalisk) > 1
                      or units.count_completed(UnitTypes.Protoss_Nexus) > 2
                      or frame > 12000):
                    next_strategy = O.MidGame
        elif strategy == O.FastExpansion:
            if new_enemy_strategy == S.SunkenContain:
                next_strategy = O.AntiSunkenContain
            else:
                # Transition to normal when the expansion is taken
                natural = game_map.get_my_natural()
                if natural is None or natural.owned_since != -1:
                    next_strategy = O.Normal
        elif strategy == O.Defensive:
            if new_enemy_strategy == S.SunkenContain:
                next_strategy = O.AntiSunkenContain
            elif new_enemy_strategy in all_in_strategies:
                next_strategy = O.AntiAllIn

            # Transition to normal when we either detect another Zerg opening or when there are six units in the
            # vanguard cluster
            elif new_enemy_strategy in (S.Turtle, S.Lair, S.MutaRush):
                next_strategy = O.Normal
            else:
                main_army_play = get_main_army_play(plays)
                vanguard = (main_army_play.get_squad().vanguard_cluster()
                            if main_army_play is not None and type(main_army_play) is AttackEnemyBase else None)
                if vanguard is not None and len(vanguard.units) >= 6:
                    next_strategy = O.Normal
                else:
                    # (Stardust falls through to the Normal case here)
                    next_strategy = normal_transition()
        elif strategy == O.Normal:
            next_strategy = normal_transition()
        elif strategy == O.MidGame:
            pass

        if next_strategy is None:
            return strategy
        strategy = next_strategy

    log.get(f"ERROR: Loop in strategy selection, ended on {strategy.value}")
    return strategy

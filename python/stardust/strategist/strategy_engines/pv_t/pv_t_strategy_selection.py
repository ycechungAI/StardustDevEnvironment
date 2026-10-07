"""Port of Strategist/StrategyEngines/PvT/PvTStrategySelection.cpp: PvT::chooseOurStrategy."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import UnitTypes, UpgradeTypes
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.strategist.plays.main_army.attack_enemy_base import AttackEnemyBase
from stardust.strategist.plays.main_army.defend_my_main import DefendMyMain
from stardust.strategist.strategy_engine import get_main_army_play
from stardust.units import units

if TYPE_CHECKING:
    from stardust.strategist.play import Play
    from stardust.strategist.strategy_engines.pv_t.pv_t import PvT


def choose_our_strategy(engine: PvT, new_enemy_strategy: PvT.TerranStrategy, plays: list[Play]) -> PvT.OurStrategy:
    from stardust.strategist.strategy_engines.pv_t.pv_t import PvT

    S = PvT.TerranStrategy
    O = PvT.OurStrategy  # noqa: E741
    rush_strategies = (S.WorkerRush, S.ProxyRush, S.MarineRush, S.BlockScouting)
    mid_game_strategies = (S.MidGameMech, S.MidGameBio, S.MidGameBioMech)

    def can_transition_from_anti_marine_rush() -> bool:
        # Transition immediately if we've discovered a different enemy strategy
        if new_enemy_strategy not in rush_strategies:
            return True

        # Require Dragoon Range
        # TODO: This is probably much too conservative
        if bwapi.Broodwar.self().getUpgradeLevel(UpgradeTypes.Singularity_Charge) == 0:
            return False

        # Count total combat units
        main_army_play = get_main_army_play(plays)
        completed_units = main_army_play.get_squad().get_unit_count_by_type() if main_army_play is not None else {}
        incomplete_units = main_army_play.assigned_incomplete_units if main_army_play is not None else {}
        unit_count = (completed_units.get(UnitTypes.Protoss_Zealot, 0)
                      + incomplete_units.get(UnitTypes.Protoss_Zealot, 0)
                      + completed_units.get(UnitTypes.Protoss_Dragoon, 0)
                      + incomplete_units.get(UnitTypes.Protoss_Dragoon, 0))

        # Transition when we have at least 6 units
        return unit_count >= 6

    def is_carrier_switch_feasible() -> bool:
        # (Disabled in Stardust: it returns false before checking that the enemy is mech, we are on three bases and
        # the enemy doesn't have many wraiths or goliaths.)
        return False

    strategy = engine.our_strategy
    for _ in range(10):
        # Each case either returns, moves to another strategy or keeps the current one (None)
        next_strategy: PvT.OurStrategy | None = None
        if strategy == O.ForgeExpandGoons:
            main_army_play = get_main_army_play(plays)
            if main_army_play is not None:
                # If the enemy is doing a rush and our main army play has transitioned to defending the main, switch
                # to anti all-in strategy
                if (engine.enemy_strategy in (S.MarineRush, S.ProxyRush)
                        and type(main_army_play) is DefendMyMain):
                    next_strategy = O.AntiMarineRush

                # If the enemy is doing a bunker contain and our main army play has transitioned to defending the
                # main, switch to anti bunker contain
                elif engine.enemy_strategy == S.BunkerContain and type(main_army_play) is DefendMyMain:
                    next_strategy = O.AntiBunkerContain

                # Transition to mid game when we have gone on the attack, indicating the opening is done
                elif type(main_army_play) is AttackEnemyBase:
                    next_strategy = O.MidGame
        elif strategy == O.EarlyGameDefense:
            # Transition appropriately as soon as we have an idea of what the enemy is doing
            if new_enemy_strategy == S.Unknown:
                return strategy
            elif new_enemy_strategy == S.BunkerContain:
                next_strategy = O.AntiBunkerContain
            elif new_enemy_strategy in rush_strategies:
                next_strategy = O.AntiMarineRush
            elif new_enemy_strategy in (S.TwoFactory, S.MarinePressure):
                next_strategy = O.Defensive
            elif new_enemy_strategy == S.FastExpansion:
                next_strategy = O.FastExpansion
            elif new_enemy_strategy in (S.WallIn, S.NormalOpening):
                next_strategy = O.NormalOpening
            elif new_enemy_strategy in mid_game_strategies:
                next_strategy = O.MidGame
        elif strategy == O.AntiBunkerContain:
            if new_enemy_strategy != S.BunkerContain:
                next_strategy = O.NormalOpening
        elif strategy == O.AntiMarineRush:
            # Transition to normal when we consider it safe to do so
            if can_transition_from_anti_marine_rush():
                if new_enemy_strategy == S.Unknown:
                    next_strategy = O.EarlyGameDefense
                elif new_enemy_strategy == S.MarinePressure:
                    next_strategy = O.Defensive
                else:
                    next_strategy = O.NormalOpening
        elif strategy == O.FastExpansion:
            # Transition to normal when the expansion is taken
            natural = game_map.get_my_natural()
            if natural is None or natural.owned_since != -1:
                next_strategy = O.NormalOpening
        elif strategy == O.Defensive:
            if new_enemy_strategy == S.BunkerContain:
                next_strategy = O.AntiBunkerContain
            elif new_enemy_strategy in rush_strategies:
                next_strategy = O.AntiMarineRush

            # Transition to normal when we either detect another opening or when there are enough units in the
            # vanguard cluster
            elif new_enemy_strategy in mid_game_strategies:
                next_strategy = O.NormalOpening
            else:
                vanguard_unit_requirement = 10 if new_enemy_strategy == S.MarinePressure else 6

                main_army_play = get_main_army_play(plays)
                if main_army_play is not None and main_army_play.is_defensive():
                    vanguard = main_army_play.get_squad().vanguard_cluster()
                    if vanguard is not None and len(vanguard.units) >= vanguard_unit_requirement:
                        next_strategy = O.NormalOpening
        elif strategy == O.NormalOpening:
            if new_enemy_strategy == S.BunkerContain:
                next_strategy = O.AntiBunkerContain
            elif new_enemy_strategy in rush_strategies and not can_transition_from_anti_marine_rush():
                next_strategy = O.AntiMarineRush

            # Transition to mid-game when the enemy has done so or we are on two bases
            # TODO: This is very vaguely defined
            elif (new_enemy_strategy in mid_game_strategies
                  or units.count_completed(UnitTypes.Protoss_Nexus) > 1):
                next_strategy = O.MidGame
        elif strategy == O.MidGame:
            if is_carrier_switch_feasible():
                next_strategy = O.LateGameCarriers
        elif strategy == O.LateGameCarriers:
            # TODO: May want to give up on carriers in some situations in the future
            pass

        if next_strategy is None:
            return strategy
        strategy = next_strategy

    log.get(f"ERROR: Loop in strategy selection, ended on {strategy.value}")
    return strategy

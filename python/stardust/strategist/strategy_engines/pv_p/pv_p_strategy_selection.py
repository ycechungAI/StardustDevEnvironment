"""Port of Strategist/StrategyEngines/PvP/PvPStrategySelection.cpp: PvP::chooseOurStrategy."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import UnitTypes, UpgradeTypes
from stardust import common
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.strategist.plays.macro.hidden_base import HiddenBase
from stardust.strategist.plays.main_army.attack_enemy_base import AttackEnemyBase
from stardust.strategist.plays.main_army.defend_my_main import DefendMyMain
from stardust.strategist.plays.scouting.eject_enemy_scout import EjectEnemyScout
from stardust.strategist.strategy_engine import get_main_army_play, get_play
from stardust.units import units
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType

if TYPE_CHECKING:
    from stardust.strategist.play import Play
    from stardust.strategist.strategy_engines.pv_p.pv_p import PvP


def choose_our_strategy(engine: PvP, new_enemy_strategy: PvP.ProtossStrategy, plays: list[Play]) -> PvP.OurStrategy:
    from stardust.strategist.strategy_engines.pv_p.pv_p import PvP

    S = PvP.ProtossStrategy
    O = PvP.OurStrategy  # noqa: E741
    frame = common.current_frame
    rush_strategies = (S.WorkerRush, S.ProxyRush, S.ZealotRush, S.ZealotAllIn)

    def can_transition_from_anti_zealot_rush() -> bool:
        # Count total combat units
        main_army_play = get_main_army_play(plays)
        completed_units = main_army_play.get_squad().get_unit_count_by_type() if main_army_play is not None else {}
        incomplete_units = main_army_play.assigned_incomplete_units if main_army_play is not None else {}
        completed_dragoons = completed_units.get(UnitTypes.Protoss_Dragoon, 0)
        unit_count = (completed_units.get(UnitTypes.Protoss_Zealot, 0)
                      + incomplete_units.get(UnitTypes.Protoss_Zealot, 0)
                      + completed_dragoons
                      + incomplete_units.get(UnitTypes.Protoss_Dragoon, 0))

        # Transition immediately if we've misdetected a proxy rush
        if engine.enemy_strategy == S.ProxyRush and new_enemy_strategy not in rush_strategies:
            return True

        # Transition immediately if we're past frame 4500 and haven't seen an enemy zealot yet
        if frame > 4500 and not units.has_enemy_built(UnitTypes.Protoss_Zealot):
            return True

        # Transition immediately if we've discovered a different enemy strategy and have at least three completed
        # dragoons
        if (new_enemy_strategy != S.BlockScouting and new_enemy_strategy not in rush_strategies
                and completed_dragoons > 2):
            if units.count_enemy(UnitTypes.Protoss_Zealot) <= unit_count:
                return True

        # Transition immediately if we have at least two dragoons and the enemy has expanded
        if units.count_enemy(UnitTypes.Protoss_Nexus) > 1 and completed_dragoons > 1:
            return True

        # If our strategy is 2-gate DT, don't require using an anti-rush strategy if the zealot counts are reasonably
        # close
        if (engine.our_strategy == O.TwoGateDT
                and units.count_enemy(UnitTypes.Protoss_Zealot) <= ((unit_count * 5) // 4)):
            return True

        # Require Dragoon Range to be started
        # TODO: This is probably much too conservative
        if (bwapi.Broodwar.self().getUpgradeLevel(UpgradeTypes.Singularity_Charge) == 0
                and not units.is_being_upgraded_or_researched(UpgradeOrTechType.of(UpgradeTypes.Singularity_Charge))):
            return False

        # Transition when we have at least 10 units
        return unit_count >= 10

    def can_transition_from_anti_dark_templar_rush() -> bool:
        main_army_play = get_main_army_play(plays)
        if main_army_play is None:
            return False

        return main_army_play.get_squad().has_detection()

    def is_dt_expand_feasible() -> bool:
        if engine.enemy_strategy == S.DarkTemplarRush:
            return False
        override = game_map.map_specific_override()
        if override.has_backdoor_natural():
            return False

        frame_cutoff = 9000 if get_play(plays, HiddenBase) is None else 10500
        if engine.our_strategy != O.DTExpand and frame > frame_cutoff:
            return False

        # Make sure our main choke is easily defensible
        choke = game_map.get_my_main_choke()
        if choke is None or not choke.is_narrow_choke or not choke.is_ramp:
            return False
        my_main = game_map.get_my_main()
        my_natural = game_map.get_my_natural()
        if (not override.has_backdoor_natural() and my_main is not None and my_natural is not None
                and bwapi.Broodwar.getGroundHeight(my_main.get_tile_position())
                <= bwapi.Broodwar.getGroundHeight(my_natural.get_tile_position())):
            return False

        return not (units.has_enemy_built(UnitTypes.Protoss_Forge)
                    or units.has_enemy_built(UnitTypes.Protoss_Photon_Cannon)
                    or units.has_enemy_built(UnitTypes.Protoss_Robotics_Facility)
                    or units.has_enemy_built(UnitTypes.Protoss_Observatory)
                    or units.has_enemy_built(UnitTypes.Protoss_Observer))

    def normal_transition() -> PvP.OurStrategy | None:
        """The transitions of the Normal strategy (which Defensive falls through to); None to keep the strategy, the
        current strategy itself to re-evaluate it."""
        if new_enemy_strategy in rush_strategies and not can_transition_from_anti_zealot_rush():
            return O.AntiZealotRush

        if new_enemy_strategy == S.DarkTemplarRush and not can_transition_from_anti_dark_templar_rush():
            # If we are later in the opening, wait until the enemy has actually built a DT before transitioning
            # (As in Stardust, this re-evaluates the same strategy otherwise, until the loop limit is hit.)
            if frame < 9000 or units.count_enemy(UnitTypes.Protoss_Dark_Templar) > 0:
                return O.AntiDarkTemplarRush
            return strategy

        if new_enemy_strategy == S.DragoonAllIn and is_dt_expand_feasible():
            return O.DTExpand

        # Transition to mid-game when the enemy has done so or we are on two bases
        # TODO: This is very vaguely defined
        if new_enemy_strategy == S.MidGame or units.count_completed(UnitTypes.Protoss_Nexus) > 1:
            return O.MidGame

        # TODO: Define conditions to go 3-gate robo based on opponent model
        return None

    strategy = engine.our_strategy
    for _ in range(10):
        # Each case either returns, moves to another strategy (continue) or keeps the current one (break)
        next_strategy: PvP.OurStrategy | None = None
        if strategy == O.ForgeExpandDT:
            main_army_play = get_main_army_play(plays)
            if main_army_play is not None:
                # If the enemy is doing a rush and our main army play has transitioned to defending the main, switch
                # to anti all-in strategy
                if (engine.enemy_strategy in (S.ZealotRush, S.ProxyRush)
                        and type(main_army_play) is DefendMyMain):
                    next_strategy = O.AntiZealotRush

                # Transition to mid game when we have gone on the attack, indicating the opening is done
                elif type(main_army_play) is AttackEnemyBase:
                    next_strategy = O.MidGame
        elif strategy == O.TwoGateDT:
            if new_enemy_strategy in rush_strategies and not can_transition_from_anti_zealot_rush():
                next_strategy = O.AntiZealotRush
            else:
                # If we failed to eject the enemy scout early enough, abandon DTs and go for a normal strategy
                eject_scout_play = get_play(plays, EjectEnemyScout) if frame > 7500 else None
                if eject_scout_play is not None and not eject_scout_play.has_ejected_scout():
                    next_strategy = O.Normal

                # Transition to DTExpand when our DTs are completed
                elif units.count_completed(UnitTypes.Protoss_Dark_Templar) > 1:
                    next_strategy = O.DTExpand
        elif strategy == O.EarlyGameDefense:
            # Transition appropriately as soon as we have an idea of what the enemy is doing
            if new_enemy_strategy == S.Unknown:
                return strategy
            elif new_enemy_strategy in rush_strategies:
                next_strategy = O.AntiZealotRush
            elif new_enemy_strategy == S.TwoGate:
                next_strategy = O.Defensive
            elif new_enemy_strategy == S.Turtle:
                next_strategy = O.FastExpansion
            elif new_enemy_strategy == S.DarkTemplarRush:
                next_strategy = O.AntiDarkTemplarRush
            elif new_enemy_strategy in (S.FastExpansion, S.EarlyForge, S.NoZealotCore, S.OneZealotCore,
                                        S.BlockScouting, S.EarlyRobo):
                next_strategy = O.Normal
            elif new_enemy_strategy == S.DragoonAllIn:
                next_strategy = O.DTExpand if is_dt_expand_feasible() else O.Normal
            elif new_enemy_strategy == S.MidGame:
                next_strategy = O.MidGame
        elif strategy == O.AntiZealotRush:
            # Transition to normal when we consider it safe to do so
            if can_transition_from_anti_zealot_rush():
                next_strategy = O.EarlyGameDefense if new_enemy_strategy == S.Unknown else O.Normal
        elif strategy == O.AntiDarkTemplarRush:
            # Transition when we consider it safe to do so
            if can_transition_from_anti_dark_templar_rush():
                # If we have a DT, transition to DTExpand
                if units.count_all(UnitTypes.Protoss_Dark_Templar) > 0:
                    next_strategy = O.DTExpand
                else:
                    next_strategy = O.Normal
        elif strategy == O.FastExpansion:
            # Transition to normal when the expansion is taken
            natural = game_map.get_my_natural()
            if natural is None or natural.owned_since != -1:
                next_strategy = O.Normal
        elif strategy == O.Defensive:
            if new_enemy_strategy in rush_strategies:
                next_strategy = O.AntiZealotRush
            elif new_enemy_strategy == S.DarkTemplarRush:
                next_strategy = O.AntiDarkTemplarRush
            elif new_enemy_strategy == S.DragoonAllIn:
                next_strategy = O.DTExpand if is_dt_expand_feasible() else O.Normal

            # Transition to normal when we either detect another opening or when there are six units in the vanguard
            # cluster
            elif new_enemy_strategy in (S.Turtle, S.MidGame):
                next_strategy = O.Normal
            else:
                main_army_play = get_main_army_play(plays)
                vanguard = (main_army_play.get_squad().vanguard_cluster()
                            if main_army_play is not None and main_army_play.is_defensive() else None)
                if vanguard is not None and len(vanguard.units) >= 6:
                    next_strategy = O.Normal
                else:
                    # (Stardust falls through to the Normal case here)
                    next_strategy = normal_transition()
        elif strategy == O.Normal:
            next_strategy = normal_transition()
        elif strategy == O.ThreeGateRobo:
            if new_enemy_strategy in rush_strategies and not can_transition_from_anti_zealot_rush():
                next_strategy = O.AntiZealotRush
            elif new_enemy_strategy == S.DragoonAllIn and is_dt_expand_feasible():
                next_strategy = O.DTExpand

            # Transition to mid-game when the enemy has done so or we are on two bases
            # TODO: This is very vaguely defined
            elif new_enemy_strategy == S.MidGame or units.count_completed(UnitTypes.Protoss_Nexus) > 1:
                next_strategy = O.MidGame
        elif strategy == O.DTExpand:
            # Transition to normal if a DT expand is no longer feasible
            if not is_dt_expand_feasible():
                next_strategy = O.Normal
            else:
                # Transition to mid-game when we have taken our natural
                natural = game_map.get_my_natural()
                if natural is None or natural.owned_since != -1:
                    next_strategy = O.MidGame
        elif strategy == O.MidGame:
            if new_enemy_strategy == S.DarkTemplarRush and not can_transition_from_anti_dark_templar_rush():
                next_strategy = O.AntiDarkTemplarRush

        if next_strategy is None:
            return strategy
        strategy = next_strategy

    log.get(f"ERROR: Loop in strategy selection, ended on {strategy.value}")
    return strategy

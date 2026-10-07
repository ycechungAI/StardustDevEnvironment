"""Port of Strategist/StrategyEngines/PvP.h and PvP/PvP.cpp: the Protoss vs. Protoss strategy engine.

The enemy strategy recognizer and our strategy selection are in pv_p_enemy_strategy_recognizer and
pv_p_strategy_selection. Enum values are Stardust's strategy names (as written to the opponent model).
"""

from __future__ import annotations

from enum import Enum
from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import UnitType, UnitTypes, UpgradeTypes, WalkPosition
from stardust import common, config, opponent
from stardust.builder import building_placement
from stardust.builder.block import Location
from stardust.builder.building_placement import BuildLocation, Neighbourhood
from stardust.general.unit_cluster.unit_cluster import Activity
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist import opponent_economic_model
from stardust.strategist.play import PRIORITY_DEPOTS, PRIORITY_EMERGENCY, PRIORITY_LOWEST, PRIORITY_MAINARMY, \
    PRIORITY_NORMAL, MineralReservations, Play, ProductionGoals, add_goal
from stardust.strategist.plays.defensive.anti_cannon_rush import AntiCannonRush
from stardust.strategist.plays.macro.hidden_base import HiddenBase
from stardust.strategist.plays.macro.saturate_bases import SaturateBases
from stardust.strategist.plays.main_army.attack_enemy_base import AttackEnemyBase
from stardust.strategist.plays.main_army.defend_my_main import DefendMyMain
from stardust.strategist.plays.main_army.forge_fast_expand import ForgeFastExpand
from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay
from stardust.strategist.plays.scouting.early_game_worker_scout import EarlyGameWorkerScout
from stardust.strategist.plays.scouting.eject_enemy_scout import EjectEnemyScout
from stardust.strategist.strategy_engine import StrategyEngine, before_play_index, get_main_army_play, get_play
from stardust.strategist.strategy_engines.common import attack_plays, defensive_cannons, expansions, upgrades
from stardust.strategist.strategy_engines.common import strategy_engine as common_engine
from stardust.units import units
from stardust.util import unit_util
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType
from stardust.workers import workers

if TYPE_CHECKING:
    from bwapi import Position
    from stardust.map.base import Base

_OUTPUT_DETECTION_DEBUG = False


def _upgrade(upgrade_type: bwapi.UpgradeType) -> UpgradeOrTechType:
    return UpgradeOrTechType.of(upgrade_type)


def _unit_counts(plays: list[Play]) -> tuple[dict[UnitType, int], dict[UnitType, int]]:
    """The completed unit counts of the main army squad and the incomplete units assigned to the main army play."""
    main_army_play = get_main_army_play(plays)
    if main_army_play is None:
        return {}, {}
    return main_army_play.get_squad().get_unit_count_by_type(), main_army_play.assigned_incomplete_units


class PvP(StrategyEngine):
    class ProtossStrategy(Enum):
        Unknown = "Unknown"
        WorkerRush = "WorkerRush"  # Detected by seeing more than two workers in our main without other combat units
        ProxyRush = "ProxyRush"  # Detected by seeing fewer buildings than expected in main
        ZealotRush = "ZealotRush"  # Detected by seeing early gates or early zealots
        EarlyForge = "EarlyForge"  # Detected by seeing a forge before core or second gate, can indicate forge expand,
        # "fake" forge expand, or cannon rush
        TwoGate = "TwoGate"  # Two or more gateways before core
        NoZealotCore = "NoZealotCore"  # One gateway before core
        OneZealotCore = "OneZealotCore"  # One gateway and one (or more) zealots before core
        FastExpansion = "FastExpansion"  # Natural expansion taken early
        BlockScouting = "BlockScouting"  # The enemy has blocked our scout from getting into their main, suspect
        # shenanigans
        ZealotAllIn = "ZealotAllIn"  # Builds that get a lot of zealots before other tech
        DragoonAllIn = "DragoonAllIn"  # Builds that get a lot of dragoons before other tech
        EarlyRobo = "EarlyRobo"  # Builds that get a robotics bay fairly early
        Turtle = "Turtle"  # Early cannons or FFE
        DarkTemplarRush = "DarkTemplarRush"  # Dark templar or dark templar tech
        MidGame = "MidGame"  # Generic for when the opponent has transitioned out of their opening
        # TODO: Mid- and late game

    class OurStrategy(Enum):
        ForgeExpandDT = "ForgeExpandDT"  # FFE strategy we use against Random and as a counter to 4-gate / other
        # player DT expand
        TwoGateDT = "TwoGateDT"  # Build that starts with 2-gate zealots and transitions into DT
        EarlyGameDefense = "EarlyGameDefense"  # We don't have enough scouting data yet
        AntiZealotRush = "AntiZealotRush"  # For fast rushes, proxy rushes or any serious early pressure, defends main
        # until it can get tech out
        AntiDarkTemplarRush = "AntiDarkTemplarRush"  # For responding to a DT rush
        FastExpansion = "FastExpansion"  # For when the opponent plays a greedy or turtle strategy
        Defensive = "Defensive"  # For when the opponent is playing an aggressive strategy that isn't considered a
        # zealot rush
        Normal = "Normal"  # Normal non-greedy and non-cautious opening
        ThreeGateRobo = "3GateRobo"  # 3 gate robo opening
        DTExpand = "DTExpand"  # Reaction to dragoon all-in, gets one DT, moves out and expands behind it
        MidGame = "MidGame"  # When we have reached the mid-game
        # TODO: Various mid-game and late-game strategies

    def __init__(self) -> None:
        self.enemy_strategy = PvP.ProtossStrategy.Unknown
        self.our_strategy = PvP.OurStrategy.EarlyGameDefense
        self.enemy_strategy_changed = 0

    def get_enemy_strategy(self) -> str:
        return self.enemy_strategy.value

    def get_our_strategy(self) -> str:
        return self.our_strategy.value

    def is_enemy_rushing(self) -> bool:
        return self.enemy_strategy in (PvP.ProtossStrategy.WorkerRush, PvP.ProtossStrategy.ProxyRush,
                                       PvP.ProtossStrategy.ZealotRush)

    def is_enemy_proxy(self) -> bool:
        return self.enemy_strategy == PvP.ProtossStrategy.ProxyRush

    def recognize_enemy_strategy(self) -> PvP.ProtossStrategy:
        from stardust.strategist.strategy_engines.pv_p import pv_p_enemy_strategy_recognizer

        return pv_p_enemy_strategy_recognizer.recognize_enemy_strategy(self)

    def choose_our_strategy(self, new_enemy_strategy: PvP.ProtossStrategy, plays: list[Play]) -> PvP.OurStrategy:
        from stardust.strategist.strategy_engines.pv_p import pv_p_strategy_selection

        return pv_p_strategy_selection.choose_our_strategy(self, new_enemy_strategy, plays)

    def initialize(self, plays: list[Play], transitioning_from_random: bool, opening_override: str) -> None:
        if opening_override:
            plays.clear()

        if transitioning_from_random:
            plays.insert(before_play_index(plays, MainArmyPlay), EjectEnemyScout())

            if get_play(plays, ForgeFastExpand) is not None:
                self.our_strategy = PvP.OurStrategy.ForgeExpandDT
        else:
            plays.append(SaturateBases())
            plays.append(EarlyGameWorkerScout())
            plays.append(EjectEnemyScout())

            opening = opening_override
            if not opening:
                if config.VS_HUMAN:
                    opening = PvP.OurStrategy.EarlyGameDefense.value
                else:
                    opening = opponent.select_opening_ucb1([PvP.OurStrategy.EarlyGameDefense.value,
                                                            PvP.OurStrategy.TwoGateDT.value,
                                                            PvP.OurStrategy.ForgeExpandDT.value])
            if opening == PvP.OurStrategy.ForgeExpandDT.value and building_placement.has_forge_gateway_wall():
                plays.append(ForgeFastExpand())
                self.our_strategy = PvP.OurStrategy.ForgeExpandDT
            else:
                plays.append(DefendMyMain())

                if opening == PvP.OurStrategy.TwoGateDT.value:
                    self.our_strategy = PvP.OurStrategy.TwoGateDT
            log.get(f"Selected opening {self.our_strategy.value}")

            # plays.append(HiddenBase())

        opponent.add_my_strategy_change(self.our_strategy.value)
        opponent.add_enemy_strategy_change(self.enemy_strategy.value)

    def update_plays(self, plays: list[Play]) -> None:
        frame = common.current_frame
        me = bwapi.Broodwar.self()

        if opponent_economic_model.enabled():
            cherryvis.set_board_value(
                "modelled-earliest-dt",
                str(opponent_economic_model.earliest_unit_production_frame(UnitTypes.Protoss_Dark_Templar)))
            cherryvis.set_board_value(
                "modelled-earliest-nexus",
                str(opponent_economic_model.earliest_unit_production_frame(UnitTypes.Protoss_Nexus)))
            zealots = opponent_economic_model.worst_case_unit_count(UnitTypes.Protoss_Zealot)
            dragoons = opponent_economic_model.worst_case_unit_count(UnitTypes.Protoss_Dragoon)

            cherryvis.set_board_value("modelled-zealots-now", f"[{zealots[0]},{zealots[1]}]")
            cherryvis.set_board_value("modelled-dragoons-now", f"[{dragoons[0]},{dragoons[1]}]")

        new_enemy_strategy = self.recognize_enemy_strategy()
        new_strategy = self.choose_our_strategy(new_enemy_strategy, plays)

        if self.enemy_strategy != new_enemy_strategy:
            message = (f"Enemy strategy changed from {self.enemy_strategy.value} to "
                       f"{new_enemy_strategy.value}")
            if config.LOGGING_ENABLED:
                log.get(message)
            if config.CHERRYVIS_ENABLED:
                cherryvis.log(message)

            self.enemy_strategy = new_enemy_strategy
            self.enemy_strategy_changed = frame
            opponent.add_enemy_strategy_change(self.enemy_strategy.value)

        if self.our_strategy != new_strategy:
            message = f"Our strategy changed from {self.our_strategy.value} to {new_strategy.value}"
            if config.LOGGING_ENABLED:
                log.get(message)
            if config.CHERRYVIS_ENABLED:
                cherryvis.log(message)

            self.our_strategy = new_strategy
            opponent.add_my_strategy_change(self.our_strategy.value)

        our_strategy = self.our_strategy
        enemy_strategy = self.enemy_strategy
        if common_engine.has_enemy_stolen_our_gas():
            defend_our_main = True
        elif our_strategy == PvP.OurStrategy.ForgeExpandDT:
            # Attack when we have goon range and a completed DT
            defend_our_main = (units.count_all(UnitTypes.Protoss_Dark_Templar) == 0
                               or me.getUpgradeLevel(UpgradeTypes.Singularity_Charge) == 0)
        elif our_strategy == PvP.OurStrategy.TwoGateDT:
            # TODO
            # Attack with initial zealot groups via a different play that ignores sim
            # Transition to normal aggressive play after the first two DTs are out
            defend_our_main = False
        elif our_strategy in (PvP.OurStrategy.EarlyGameDefense, PvP.OurStrategy.AntiZealotRush,
                              PvP.OurStrategy.AntiDarkTemplarRush, PvP.OurStrategy.Defensive):
            defend_our_main = True
        elif our_strategy == PvP.OurStrategy.DTExpand:
            # Wait until we have at least completed two dark templar that aren't in our main
            main_areas = game_map.get_my_main_areas()
            bwem_map = bwem.Instance()
            dt_count = 0
            for unit in units.all_mine_completed_of_type(UnitTypes.Protoss_Dark_Templar):
                area = bwem_map.GetArea(WalkPosition(unit.last_position))
                if area not in main_areas:
                    dt_count += 1

            defend_our_main = dt_count < 2
        else:
            # FastExpansion, Normal, ThreeGateRobo, MidGame
            defend_our_main = self._defend_our_main_when_macroing(plays)

        attack_plays.update_attack_plays(plays, defend_our_main)

        # Ensure we have an anti cannon rush play if the enemy strategy warrants it
        if frame < 4000:
            # If we've detected a proxy, keep track of whether we have identified it as a zealot rush
            # Once we've either seen the proxied gateway or a zealot, we assume the enemy isn't doing a "proxy" cannon
            # rush
            zealot_proxy = (enemy_strategy == PvP.ProtossStrategy.ProxyRush
                            and (units.has_enemy_built(UnitTypes.Protoss_Gateway)
                                 or units.has_enemy_built(UnitTypes.Protoss_Zealot)))

            anti_cannon_rush_play = get_play(plays, AntiCannonRush)
            if (enemy_strategy == PvP.ProtossStrategy.EarlyForge
                    or (enemy_strategy == PvP.ProtossStrategy.ProxyRush and not zealot_proxy)
                    or enemy_strategy == PvP.ProtossStrategy.BlockScouting
                    or (enemy_strategy == PvP.ProtossStrategy.Unknown and frame > 2000)):
                if anti_cannon_rush_play is None:
                    plays.insert(0, AntiCannonRush())
            elif anti_cannon_rush_play is not None and (
                    enemy_strategy in (PvP.ProtossStrategy.FastExpansion, PvP.ProtossStrategy.NoZealotCore,
                                       PvP.ProtossStrategy.OneZealotCore, PvP.ProtossStrategy.ZealotRush,
                                       PvP.ProtossStrategy.TwoGate)
                    or zealot_proxy):
                anti_cannon_rush_play.safe_enemy_strategy_determined = True

        # Set the worker scout mode
        # This is just completing the play when we don't expect the scout to be able to gather any additional useful
        # information
        import stardust.strategist.strategist as strategist
        if strategist.get_worker_scout_status() in (strategist.WorkerScoutStatus.EnemyBaseScouted,
                                                    strategist.WorkerScoutStatus.MonitoringEnemyChoke):
            worker_scout_play = get_play(plays, EarlyGameWorkerScout)
            if worker_scout_play is not None and enemy_strategy in (
                    PvP.ProtossStrategy.DragoonAllIn, PvP.ProtossStrategy.DarkTemplarRush,
                    PvP.ProtossStrategy.EarlyRobo, PvP.ProtossStrategy.Turtle, PvP.ProtossStrategy.MidGame):
                worker_scout_play.status.complete = True

        common_engine.update_defend_base_plays(plays)
        common_engine.update_special_teams_plays(plays)
        expansions.default_expansions(plays)
        common_engine.scout_expos(plays, 12000)

    def _defend_our_main_when_macroing(self, plays: list[Play]) -> bool:
        """Whether to defend our main for the FastExpansion, Normal, ThreeGateRobo and MidGame strategies."""
        main_army_play = get_main_army_play(plays)
        if main_army_play is None:
            return True

        # Always use a defend play if the squad has no units
        squad = main_army_play.get_squad()
        vanguard = squad.vanguard_cluster()
        if vanguard is None:
            return True

        # Require range off of an FFE
        if (get_play(plays, ForgeFastExpand) is not None
                and bwapi.Broodwar.self().getUpgradeLevel(UpgradeTypes.Singularity_Charge) == 0):
            return True

        # Use a defend play if we are doing a 3 gate robo or the main army needs detection and doesn't have any
        # observers
        if ((self.our_strategy == PvP.OurStrategy.ThreeGateRobo
             and units.count_completed(UnitTypes.Protoss_Observer) == 0)
                or (squad.needs_detection() and not squad.get_detectors())):
            return True

        defend_our_main = False

        # Transition from a defend squad when the vanguard cluster has 3 units and can do so
        # Exception: always attack if the enemy strategy is fast expansion
        if type(main_army_play) is DefendMyMain:
            defend_our_main = (self.enemy_strategy != PvP.ProtossStrategy.FastExpansion
                               and (len(vanguard.units) < 3
                                    or not main_army_play.can_transition_to_attack()))

        # Transition to a defend squad if our attack squad has been pushed back into our main and we haven't yet taken
        # our natural
        natural = game_map.get_my_natural()
        if (type(main_army_play) is AttackEnemyBase
                and (natural is None or natural.owner != bwapi.Broodwar.self() or natural.resource_depot is None
                     or not natural.resource_depot.completed
                     or game_map.map_specific_override().has_backdoor_natural())):
            if vanguard.is_fleeing():
                def in_main(pos: Position) -> bool:
                    choke = game_map.get_my_main_choke()
                    if choke is not None and choke.center.getApproxDistance(pos) < 320:
                        return True

                    main_areas = game_map.get_my_main_areas()
                    return bwem.Instance().GetArea(WalkPosition(pos)) in main_areas

                if in_main(vanguard.center) or (vanguard.vanguard is not None
                                                and in_main(vanguard.vanguard.last_position)):
                    defend_our_main = True

        return defend_our_main

    def update_production(self, plays: list[Play], prioritized_production_goals: ProductionGoals,
                          mineral_reservations: MineralReservations) -> None:
        frame = common.current_frame
        goals = prioritized_production_goals

        common_engine.reserve_minerals_for_expansion(mineral_reservations)
        self._handle_natural_expansion(plays, goals)
        self._handle_detection(goals)

        if common_engine.handle_island_expansion_production(plays, goals):
            return

        completed_units, incomplete_units = _unit_counts(plays)

        zealot_count = (completed_units.get(UnitTypes.Protoss_Zealot, 0)
                        + incomplete_units.get(UnitTypes.Protoss_Zealot, 0))
        dragoon_count = (completed_units.get(UnitTypes.Protoss_Dragoon, 0)
                         + incomplete_units.get(UnitTypes.Protoss_Dragoon, 0))
        dt_count = units.count_all(UnitTypes.Protoss_Dark_Templar)

        in_progress_count = (units.count_incomplete(UnitTypes.Protoss_Zealot)
                             + units.count_incomplete(UnitTypes.Protoss_Dragoon)
                             + units.count_incomplete(UnitTypes.Protoss_Dark_Templar))

        if get_play(plays, ForgeFastExpand) is None:
            common_engine.handle_gas_steal_production(goals, zealot_count)

        # Main army production
        our_strategy = self.our_strategy
        if our_strategy == PvP.OurStrategy.ForgeExpandDT:
            # Keep a single zealot once we have a gateway until we are ready for dragoons
            if (zealot_count == 0
                    and units.count_all(UnitTypes.Protoss_Gateway) > 0
                    and units.count_completed(UnitTypes.Protoss_Cybernetics_Core) == 0):
                incomplete_cyber_core = units.all_mine_incomplete_of_type(UnitTypes.Protoss_Cybernetics_Core)
                if (not incomplete_cyber_core
                        or next(iter(incomplete_cyber_core)).estimated_completion_frame > (frame + 250)):
                    add_goal(goals, PRIORITY_NORMAL, UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, 1, 1))

            # First dragoon is important
            if dragoon_count == 0:
                add_goal(goals, PRIORITY_NORMAL, UnitProductionGoal("SE", UnitTypes.Protoss_Dragoon, 1, 1))

            # Get a DT at a timing that can be used to discourage a dragoon all-in
            if units.count_all(UnitTypes.Protoss_Dark_Templar) == 0:
                add_goal(goals, PRIORITY_NORMAL,
                         UnitProductionGoal("SE", UnitTypes.Protoss_Dark_Templar, 1, 1,
                                            frame=9000 - unit_util.build_time(UnitTypes.Protoss_Dark_Templar)))

            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Dragoon, -1, -1))
            upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Singularity_Charge), UnitTypes.Protoss_Dragoon, 1)
        elif our_strategy == PvP.OurStrategy.TwoGateDT:
            eject_scout_play = get_play(plays, EjectEnemyScout)
            has_ejected_scout = eject_scout_play is None or eject_scout_play.has_ejected_scout()

            # Build three zealots initially
            if (zealot_count < 3 and units.count_all(UnitTypes.Protoss_Assimilator) == 0
                    and units.count_all(UnitTypes.Protoss_Cybernetics_Core) == 0):
                add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE-2gateDT", UnitTypes.Protoss_Zealot, -1, 2))

            # Build one dragoon to eject the scout
            if not has_ejected_scout and dragoon_count < 1:
                add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE-2gateDT", UnitTypes.Protoss_Dragoon, 1, 2))

            # Build two DTs once the scout is ejected
            if has_ejected_scout and dt_count < 2:
                add_goal(goals, PRIORITY_MAINARMY,
                         UnitProductionGoal("SE-2gateDT", UnitTypes.Protoss_Dark_Templar, 2 - dt_count, -1))
                add_goal(goals, PRIORITY_MAINARMY,
                         UnitProductionGoal("SE-2gateDT", UnitTypes.Protoss_Zealot, -1, -1))
            else:
                add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE-2gateDT", UnitTypes.Protoss_Zealot, -1, 2))
        elif our_strategy == PvP.OurStrategy.EarlyGameDefense:
            # Start with one-gate core with two zealots until we have more scouting information
            common_engine.one_gate_core_opening(goals, dragoon_count, zealot_count, 2)

            upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Singularity_Charge), UnitTypes.Protoss_Dragoon, 2)
        elif our_strategy == PvP.OurStrategy.AntiZealotRush:
            # We get at least four zealots, but ensure we match enemy zealot production to avoid getting overrun
            # When they are doing a proxy, add in a couple of extra zealots to handle the ones in production
            enemy_zealots = units.count_enemy(UnitTypes.Protoss_Zealot) + 1
            if self.enemy_strategy == PvP.ProtossStrategy.ProxyRush:
                enemy_zealots += 2
            desired_zealots = max(4, enemy_zealots)
            zealots_required = desired_zealots - zealot_count

            common_engine.handle_anti_rush_production(goals, dragoon_count, zealot_count, zealots_required)
        elif our_strategy == PvP.OurStrategy.AntiDarkTemplarRush:
            # handleDetection takes care of ordering cannons and obs

            # Expand to our hidden base
            hidden_base_play = get_play(plays, HiddenBase)
            if (hidden_base_play is not None and hidden_base_play.base is not None
                    and hidden_base_play.base.owned_since == -1):
                build_location = BuildLocation(Location(hidden_base_play.base.get_tile_position()), 0, 0, 0)
                add_goal(goals, PRIORITY_DEPOTS,
                         UnitProductionGoal.at("SE-antidt", UnitTypes.Protoss_Nexus, build_location))

            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Dragoon, -1, -1))
        elif our_strategy == PvP.OurStrategy.FastExpansion:
            common_engine.one_gate_core_opening(goals, dragoon_count, zealot_count, 0)

            # Default upgrades
            PvP.handle_upgrades(goals)
        elif our_strategy in (PvP.OurStrategy.Defensive, PvP.OurStrategy.Normal):
            desired_zealots = 1

            # Modify desired zealots based on detected enemy strategy
            if self.enemy_strategy == PvP.ProtossStrategy.NoZealotCore:
                desired_zealots = 0
            elif self.enemy_strategy == PvP.ProtossStrategy.BlockScouting:
                desired_zealots = 3

            common_engine.one_gate_core_opening(goals, dragoon_count, zealot_count, desired_zealots)

            # Default upgrades
            PvP.handle_upgrades(goals)
        elif our_strategy == PvP.OurStrategy.ThreeGateRobo:
            if zealot_count == 0 and dragoon_count == 0:
                add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE-3grobo", UnitTypes.Protoss_Zealot, 1, 1))

            if dragoon_count < 2:
                add_goal(goals, PRIORITY_MAINARMY,
                         UnitProductionGoal("SE-3grobo", UnitTypes.Protoss_Dragoon, 2 - dragoon_count, 1))

            observer_count = (completed_units.get(UnitTypes.Protoss_Observer, 0)
                              + incomplete_units.get(UnitTypes.Protoss_Observer, 0))
            if observer_count == 0:
                add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE-3grobo", UnitTypes.Protoss_Observer, 1, 1))

            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE-3grobo", UnitTypes.Protoss_Dragoon, -1, -1))

            upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Singularity_Charge), UnitTypes.Protoss_Dragoon, 1)
        elif our_strategy == PvP.OurStrategy.MidGame:
            # Have one DT out on the map if we have a templar archives
            # Two if the enemy doesn't have mobile detection
            desired_dark_templar = 0
            if units.count_all(UnitTypes.Protoss_Templar_Archives) > 0:
                if (not units.has_enemy_built(UnitTypes.Protoss_Observer)
                        and not units.has_enemy_built(UnitTypes.Protoss_Observatory)):
                    desired_dark_templar = 2
                else:
                    desired_dark_templar = 1

            if desired_dark_templar > dt_count:
                add_goal(goals, PRIORITY_NORMAL,
                         UnitProductionGoal("SE-nodetect", UnitTypes.Protoss_Dark_Templar,
                                            desired_dark_templar - dt_count, -1))

            # Baseline production is one combat unit for every 6 workers (approximately 3 units per mining base)
            higher_priority_count = (workers.mineral_workers() // 6) - in_progress_count

            # Prefer goons
            common_engine.main_army_production(goals, UnitTypes.Protoss_Dragoon, -1, higher_priority_count)

            # Build zealots at lowest priority
            add_goal(goals, PRIORITY_LOWEST, UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, -1, -1))

            # Default upgrades
            PvP.handle_upgrades(goals)
        elif our_strategy == PvP.OurStrategy.DTExpand:
            hidden_base_play = get_play(plays, HiddenBase)

            if dt_count < 2:
                add_goal(goals, PRIORITY_NORMAL,
                         UnitProductionGoal("SE", UnitTypes.Protoss_Dark_Templar, 2 - dt_count, 2,
                                            Neighbourhood.HIDDEN_BASE if hidden_base_play is not None
                                            else Neighbourhood.ALL_MY_BASES))

            add_goal(goals, PRIORITY_MAINARMY,
                     UnitProductionGoal("SE", UnitTypes.Protoss_Dragoon, -1, -1,
                                        Neighbourhood.MAIN_BASE if hidden_base_play is not None
                                        else Neighbourhood.ALL_MY_BASES))

            # Make sure we get dragoon range to defend our choke effectively
            upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Singularity_Charge), UnitTypes.Protoss_Dragoon, 2)

    def _handle_natural_expansion(self, plays: list[Play], prioritized_production_goals: ProductionGoals) -> None:
        # Hop out if the natural has already been taken and the nexus is completed
        natural = game_map.get_my_natural()
        if natural is None:
            cherryvis.set_board_value("natural", "no-natural-base")
            return

        override = game_map.map_specific_override()
        me = bwapi.Broodwar.self()

        under_construction = False
        if natural.owned_since != -1:
            if natural.resource_depot is not None and not natural.resource_depot.completed:
                # Never cancel a backdoor natural
                if override.has_backdoor_natural():
                    cherryvis.set_board_value("natural", "incomplete-backdoor")
                    return
                under_construction = True
            else:
                cherryvis.set_board_value("natural", "complete")
                return
        else:
            # Hop out if we have taken a different base
            if len(game_map.get_my_bases()) > 1:
                cherryvis.set_board_value("natural", "complete-taken-other")
                return

        def take() -> None:
            expansions.take_natural_expansion(plays, prioritized_production_goals)

        # If we have a backdoor natural, expand if we are gas blocked
        # This generally happens when we are teching as a response to something
        if override.has_backdoor_natural() and self.our_strategy != PvP.OurStrategy.DTExpand:
            if me.minerals() > 400 and me.gas() < 100 and (me.supplyTotal() - me.supplyUsed()) >= 6:
                cherryvis.set_board_value("natural", "take-backdoor")

                take()
                return

        hidden_base_play = get_play(plays, HiddenBase)

        cancel = True
        our_strategy = self.our_strategy
        if our_strategy == PvP.OurStrategy.ForgeExpandDT:
            # Handled by the play
            cherryvis.set_board_value("natural", "forge-expand")
            return
        elif our_strategy == PvP.OurStrategy.TwoGateDT:
            # If there is a backdoor natural, take it once we start the citadel
            if override.has_backdoor_natural() and units.count_all(UnitTypes.Protoss_Citadel_of_Adun) > 0:
                cherryvis.set_board_value("natural", "take-backdoor")
                take()
                return

            # Handled by transition to DTExpand after DTs are created
            cherryvis.set_board_value("natural", "wait-for-dts")
            return
        elif our_strategy in (PvP.OurStrategy.EarlyGameDefense, PvP.OurStrategy.AntiZealotRush,
                              PvP.OurStrategy.Defensive):
            # Don't take our natural if the enemy could be rushing or doing an all-in
            cherryvis.set_board_value("natural", "wait-defensive")
        elif our_strategy == PvP.OurStrategy.AntiDarkTemplarRush:
            if under_construction and units.count_completed(UnitTypes.Protoss_Observer) > 0:
                cherryvis.set_board_value("natural", "take")
                take()
                return

            cherryvis.set_board_value("natural", "wait-defensive")
        elif our_strategy == PvP.OurStrategy.FastExpansion:
            cherryvis.set_board_value("natural", "take-fast-expo")
            take()
            return
        elif our_strategy in (PvP.OurStrategy.Normal, PvP.OurStrategy.ThreeGateRobo, PvP.OurStrategy.MidGame):
            # In this case we want to expand when we consider it safe to do so: we have an attacking or containing
            # army that is close to the enemy base
            decision = self._natural_decision_when_attacking(plays, natural, under_construction)
            if decision == "take":
                take()
                return
            if decision == "keep":
                cancel = False
        elif our_strategy == PvP.OurStrategy.DTExpand:
            if self._natural_decision_dt_expand(plays, natural, under_construction):
                take()
                return

        if cancel:
            expansions.cancel_natural_expansion(plays, prioritized_production_goals)

        # Take our hidden base expansion in cases where we have been prevented from taking our natural
        # It is queued at lowest priority so it will only be taken if we have an excess of minerals
        if common.current_frame > 12000:
            if (hidden_base_play is not None and hidden_base_play.base is not None
                    and hidden_base_play.base.owned_since == -1):
                build_location = BuildLocation(Location(hidden_base_play.base.get_tile_position()), 0, 0, 0)
                add_goal(prioritized_production_goals, PRIORITY_LOWEST,
                         UnitProductionGoal.at("SE-hiddenbase", UnitTypes.Protoss_Nexus, build_location))

    def _natural_decision_when_attacking(self, plays: list[Play], natural: Base,
                                         under_construction: bool) -> str:
        """For the Normal, ThreeGateRobo and MidGame strategies: "take" to take the natural, "keep" to neither take
        nor cancel it, "cancel" otherwise."""
        override = game_map.map_specific_override()
        me = bwapi.Broodwar.self()

        # Check for an attack play first - if we don't have one, we will cancel a constructing natural nexus
        # Exception is if our attack play went defensive because an observer isn't close enough to detect a DT
        main_army_play = get_play(plays, AttackEnemyBase)
        if main_army_play is None:
            keep = (units.count_enemy(UnitTypes.Protoss_Dark_Templar) > 0
                    and units.count_completed(UnitTypes.Protoss_Observer) > 0)

            cherryvis.set_board_value("natural", "no-attack-play")
            return "keep" if keep else "cancel"

        # From here we never cancel an already-constructing nexus
        if under_construction:
            cherryvis.set_board_value("natural", "take")
            return "take"

        # We never expand before a frame breakpoint unless the enemy has done so or is doing an opening that will not
        # give immediate pressure
        # The frame breakpoint is higher if the enemy is doing a dragoon all-in and lower if we have a backdoor natural
        natural_frame = 10000
        if override.has_backdoor_natural():
            natural_frame = 6500
        elif self.enemy_strategy == PvP.ProtossStrategy.DragoonAllIn:
            natural_frame = 12000
        if (common.current_frame < natural_frame
                and units.count_enemy(UnitTypes.Protoss_Nexus) < 2
                and self.enemy_strategy != PvP.ProtossStrategy.EarlyRobo
                and self.enemy_strategy != PvP.ProtossStrategy.Turtle):
            cherryvis.set_board_value("natural", "too-early")
            return "cancel"

        squad = main_army_play.get_squad()
        if len(squad.get_units()) < 5:
            cherryvis.set_board_value("natural", "attack-play-too-small")
            return "cancel"

        vanguard_cluster, dist = squad.vanguard_cluster_and_distance()
        if vanguard_cluster is None:
            cherryvis.set_board_value("natural", "no-vanguard-cluster")
            return "cancel"

        # Cluster should be past our own natural
        if not override.has_backdoor_natural():
            natural_dist = path_finding.get_ground_distance(natural.get_position(),
                                                            main_army_play.base.get_position())
            if natural_dist != -1 and dist > (natural_dist - 320):
                cherryvis.set_board_value("natural", "vanguard-cluster-not-beyond-natural")
                return "cancel"

        # (Stardust has a commented-out requirement for an observatory before expanding here.)

        # Always expand in this situation if we are gas blocked
        if me.minerals() > 500 and me.gas() < 100:
            cherryvis.set_board_value("natural", "take-gas-blocked")
            return "take"

        # Cluster should not be moving or fleeing
        # In other words, we want the cluster to be in some kind of stable attack or contain state
        if vanguard_cluster.current_activity == Activity.Moving or vanguard_cluster.is_fleeing():
            # We don't cancel a queued expansion in this case
            cherryvis.set_board_value("natural", "vanguard-cluster-not-attacking")
            return "keep"

        # If the cluster is attacking, it should be significantly closer to the enemy main, unless we are past frame
        # 12500
        if (common.current_frame < 12500
                and vanguard_cluster.current_activity == Activity.Attacking
                and vanguard_cluster.percentage_to_enemy_main < 0.7):
            cherryvis.set_board_value("natural", "vanguard-cluster-too-close")
            return "cancel"

        cherryvis.set_board_value("natural", "take")
        return "take"

    def _natural_decision_dt_expand(self, plays: list[Play], natural: Base, under_construction: bool) -> bool:
        """For the DTExpand strategy: whether to take the natural (otherwise it is cancelled)."""
        # We never cancel an already-constructing nexus
        if under_construction:
            cherryvis.set_board_value("natural", "take")
            return True

        # Take our natural as soon as the army has moved beyond it
        main_army_play = get_main_army_play(plays)
        if main_army_play is None or type(main_army_play) is not AttackEnemyBase:
            cherryvis.set_board_value("natural", "no-attack-play")
            return False

        squad = main_army_play.get_squad()
        vanguard_cluster = squad.vanguard_cluster()
        if vanguard_cluster is None:
            cherryvis.set_board_value("natural", "no-vanguard-cluster")
            return False

        # Ensure the cluster is at least 10 tiles further from the natural than it is from the main
        if not game_map.map_specific_override().has_backdoor_natural():
            my_main = game_map.get_my_main()
            assert my_main is not None
            dist_to_main = path_finding.get_ground_distance(my_main.get_position(), vanguard_cluster.center)
            dist_to_natural = path_finding.get_ground_distance(natural.get_position(), vanguard_cluster.center)
            if dist_to_natural < 500 or dist_to_main < (dist_to_natural + 500):
                cherryvis.set_board_value("natural", "vanguard-too-close")
                return False

        # Cluster should not be fleeing
        if vanguard_cluster.is_fleeing():
            cherryvis.set_board_value("natural", "vanguard-fleeing")
            return False

        cherryvis.set_board_value("natural", "take")
        return True

    @staticmethod
    def handle_upgrades(prioritized_production_goals: ProductionGoals) -> None:
        goals = prioritized_production_goals

        # Basic infantry skill upgrades are queued when we have enough of them and are still building them
        upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Leg_Enhancements), UnitTypes.Protoss_Zealot, 6)
        upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Singularity_Charge), UnitTypes.Protoss_Dragoon, 1)

        # Cases where we want the upgrade as soon as we start building one of the units
        upgrades.upgrade_when_unit_created(goals, _upgrade(UpgradeTypes.Gravitic_Drive), UnitTypes.Protoss_Shuttle,
                                           False, True)
        upgrades.upgrade_when_unit_created(goals, _upgrade(UpgradeTypes.Carrier_Capacity), UnitTypes.Protoss_Carrier,
                                           True)

        # Upgrade observer speed on three gas
        if units.count_completed(UnitTypes.Protoss_Assimilator) >= 3:
            upgrades.upgrade_when_unit_created(goals, _upgrade(UpgradeTypes.Gravitic_Boosters),
                                               UnitTypes.Protoss_Observer)

        upgrades.default_ground_upgrades(goals)

        # TODO: Air upgrades

    def _handle_detection(self, prioritized_production_goals: ProductionGoals) -> None:
        import stardust.strategist.strategist as strategist

        # The main army play will reactively request mobile detection when it sees a cloaked enemy unit
        # The logic here is to look ahead to make sure we already have detection available when we need it
        goals = prioritized_production_goals
        frame = common.current_frame
        enemy_strategy = self.enemy_strategy

        # Break out if we already have an observer
        if units.count_all(UnitTypes.Protoss_Observer) > 0:
            cherryvis.set_board_value("detection", "have-observer")
            return

        # Break out if we are going 3 gate robo
        if self.our_strategy == PvP.OurStrategy.ThreeGateRobo:
            cherryvis.set_board_value("detection", "3-gate-robo")
            return

        def build_observer(frame_needed: int = 0) -> None:
            priority = (PRIORITY_EMERGENCY if units.count_enemy(UnitTypes.Protoss_Dark_Templar) > 0
                        else PRIORITY_NORMAL)
            add_goal(goals, priority,
                     UnitProductionGoal("SE-detection", UnitTypes.Protoss_Observer, 1, 1, frame=frame_needed))

        # If the enemy is known to have produced a DT, get a cannon and observer
        if (enemy_strategy == PvP.ProtossStrategy.DarkTemplarRush
                or units.has_enemy_built(UnitTypes.Protoss_Dark_Templar)):
            if not defensive_cannons.has_cannon_at_wall():
                defensive_cannons.build_defensive_cannons(goals, True, 0, 1)
            build_observer()
            cherryvis.set_board_value("detection", "emergency-build-cannon-and-observer")
            return

        # Get an observer when we have a second gas
        if ((units.count_completed(UnitTypes.Protoss_Assimilator) > 1
             or (units.count_completed(UnitTypes.Protoss_Nexus) > 1 and frame > 10000))
                and strategist.pressure() < 0.4):
            cherryvis.set_board_value("detection", "macro-build-observer")
            build_observer()
            return

        # If our strategy is anti-zealot-rush and there is an enemy combat unit in our base, we have worse things to
        # worry about
        if self.our_strategy == PvP.OurStrategy.AntiZealotRush:
            bwem_map = bwem.Instance()
            main_areas = game_map.get_my_main_areas()
            for unit in units.all_enemy():
                if not unit.last_position_valid:
                    continue
                if not unit_util.is_combat_unit(unit.type):
                    continue
                if not unit.type.canAttack():
                    continue

                if bwem_map.GetArea(WalkPosition(unit.last_position)) in main_areas:
                    cherryvis.set_board_value("detection", "enemy-in-base")
                    return

            # Add some frame stops to ensure we don't build a cannon while the enemy is still producing zealots
            zealot_timings = len(units.get_enemy_unit_timings(UnitTypes.Protoss_Zealot))
            if ((frame < 7000 and zealot_timings >= 8)
                    or (frame < 8000 and zealot_timings >= 10)
                    or (frame < 9000 and zealot_timings >= 12)):
                cherryvis.set_board_value("detection", "anti-zealot-rush")
                return

        # Break out if we have detected a strategy that precludes a dark templar rush now
        zealot_strategy = (enemy_strategy in (PvP.ProtossStrategy.ProxyRush, PvP.ProtossStrategy.ZealotRush,
                                              PvP.ProtossStrategy.ZealotAllIn)
                           or (enemy_strategy == PvP.ProtossStrategy.TwoGate
                               and units.count_enemy(UnitTypes.Protoss_Zealot) > 5))
        if ((enemy_strategy == PvP.ProtossStrategy.EarlyForge and frame < 6000)
                or (zealot_strategy and frame < 6500)
                or (zealot_strategy and units.count_enemy(UnitTypes.Protoss_Nexus) > 1 and frame < 9000)
                or (enemy_strategy == PvP.ProtossStrategy.DragoonAllIn and frame < 8000)
                or (enemy_strategy == PvP.ProtossStrategy.EarlyRobo and frame < 8000)
                or (enemy_strategy == PvP.ProtossStrategy.FastExpansion and frame < 7000)
                or (enemy_strategy == PvP.ProtossStrategy.Turtle and frame < 8000)):
            cherryvis.set_board_value("detection", "strategy-exception")
            return

        # Initialize the expected DT completion frame based on previous game observations
        # We assume worst case until we have at least 10 games played against the opponent
        # If we have never lost to the opponent, we continue playing conservatively even if we have never seen a DT
        expected_completion_frame = 7300
        if opponent_economic_model.enabled():
            expected_completion_frame = (
                opponent_economic_model.earliest_unit_production_frame(UnitTypes.Protoss_Dark_Templar)
                + unit_util.build_time(UnitTypes.Protoss_Dark_Templar))
        if opponent.win_loss_ratio(0.0, 200) < 0.99:
            expected_completion_frame = min(opponent.min_value_in_previous_games("firstDarkTemplarCompleted",
                                                                                 expected_completion_frame, 15, 10),
                                            20000)

        # If we haven't found the enemy main, be conservative and assume we might see DTs 500 frames after earliest
        # completion
        enemy_main = game_map.get_enemy_main()
        if enemy_main is None:
            defensive_cannons.build_defensive_cannons(goals, not defensive_cannons.has_cannon_at_wall(),
                                                      expected_completion_frame + 500)
            cherryvis.set_board_value("detection", "cannon-at-worst-case")
            return

        # Otherwise compute when the enemy could get DTs based on our scouting information

        # First estimate when the enemy's templar archives will / might finish
        templar_archive_timings = units.get_enemy_unit_timings(UnitTypes.Protoss_Templar_Archives)
        if templar_archive_timings:
            # We've scouted a templar archives directly, so use its completion frame
            # It's unlikely the enemy is building a templar archives as a fake-out
            expected_completion_frame = (templar_archive_timings[0][0]
                                         + unit_util.build_time(UnitTypes.Protoss_Templar_Archives)
                                         + unit_util.build_time(UnitTypes.Protoss_Dark_Templar))
        else:
            # If we've scouted a citadel, so assume the templar archives is started as soon as the citadel finishes,
            # unless we've scouted the base in the meantime or our previous game data indicates it is likely to be a
            # fake-out
            citadel_timings = units.get_enemy_unit_timings(UnitTypes.Protoss_Citadel_of_Adun)
            if citadel_timings and expected_completion_frame < 10000:
                expected_completion_frame = (unit_util.build_time(UnitTypes.Protoss_Templar_Archives)
                                             + max(enemy_main.last_scouted,
                                                   citadel_timings[0][0]
                                                   + unit_util.build_time(UnitTypes.Protoss_Citadel_of_Adun))
                                             + unit_util.build_time(UnitTypes.Protoss_Dark_Templar))

        # Compute the transit time from the enemy's closest gateway
        my_main_choke = game_map.get_my_main_choke()
        if my_main_choke is not None:
            my_position = my_main_choke.center
        else:
            my_main = game_map.get_my_main()
            assert my_main is not None
            my_position = my_main.get_position()
        closest_gateway_frames = path_finding.expected_travel_time(enemy_main.get_position(), my_position,
                                                                   UnitTypes.Protoss_Dark_Templar,
                                                                   PathFindingOptions.UseNearestBWEMArea, 1.1)
        for unit in units.all_enemy_of_type(UnitTypes.Protoss_Gateway):
            if not unit.completed:
                continue

            frames = path_finding.expected_travel_time(unit.last_position, my_position,
                                                       UnitTypes.Protoss_Dark_Templar,
                                                       PathFindingOptions.UseNearestBWEMArea, 1.1, -1)
            if frames != -1 and frames < closest_gateway_frames:
                closest_gateway_frames = frames

        if _OUTPUT_DETECTION_DEBUG:
            cherryvis.log(f"detection: expected DT completion @ {expected_completion_frame}; DT at our choke @ "
                          f"{closest_gateway_frames + expected_completion_frame}")

        # Now sum everything up to get the frame where we need detection
        needed_frame = expected_completion_frame + closest_gateway_frames
        if needed_frame < 0:
            return
        if not templar_archive_timings:
            defensive_cannons.build_defensive_cannons(goals, not defensive_cannons.has_cannon_at_wall(), needed_frame)
            cherryvis.set_board_value("detection", f"cannon-at-{needed_frame}")
        else:
            build_observer(needed_frame)
            defensive_cannons.build_defensive_cannons(goals, not defensive_cannons.has_cannon_at_wall(), needed_frame,
                                                      1)
            cherryvis.set_board_value("detection", f"cannon-and-observer-at-{needed_frame}")

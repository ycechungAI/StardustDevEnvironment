"""Port of Strategist/StrategyEngines/PvT.h and PvT/PvT.cpp: the Protoss vs. Terran strategy engine.

The enemy strategy recognizer and our strategy selection are in pv_t_enemy_strategy_recognizer and
pv_t_strategy_selection. Enum values are Stardust's strategy names (as written to the opponent model).
"""

from __future__ import annotations

from enum import Enum

import bwapi
from bwapi import TechTypes, TilePosition, TilePositions, UnitType, UnitTypes, UpgradeTypes
from stardust import common, config, opponent
from stardust.builder import builder, building_placement
from stardust.builder.block import Location
from stardust.builder.building_placement import BuildLocation
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.players import players
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.producer.production_goals.upgrade_production_goal import UpgradeProductionGoal
from stardust.strategist.play import PRIORITY_EMERGENCY, PRIORITY_HIGHPRIORITYUPGRADES, PRIORITY_MAINARMY, \
    PRIORITY_NORMAL, PRIORITY_SPECIALTEAMS, MineralReservations, Play, ProductionGoals, add_goal
from stardust.strategist.plays.macro.saturate_bases import SaturateBases
from stardust.strategist.plays.main_army.attack_enemy_base import AttackEnemyBase
from stardust.strategist.plays.main_army.defend_my_main import DefendMyMain
from stardust.strategist.plays.main_army.forge_fast_expand import ForgeFastExpand
from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay
from stardust.strategist.plays.scouting.early_game_worker_scout import EarlyGameWorkerScout
from stardust.strategist.plays.scouting.eject_enemy_scout import EjectEnemyScout
from stardust.strategist.plays.special_teams.elevator import Elevator
from stardust.strategist.plays.special_teams.elevator_rush import ElevatorRush
from stardust.strategist.strategy_engine import StrategyEngine, before_play_index, get_main_army_play, get_play
from stardust.strategist.strategy_engines.common import attack_plays, expansions, upgrades
from stardust.strategist.strategy_engines.common import strategy_engine as common_engine
from stardust.units import units
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType
from stardust.workers import workers


def _upgrade(upgrade_type: bwapi.UpgradeType | bwapi.TechType) -> UpgradeOrTechType:
    return UpgradeOrTechType.of(upgrade_type)


def _unit_counts(plays: list[Play]) -> tuple[dict[UnitType, int], dict[UnitType, int]]:
    """The completed unit counts of the main army squad and the incomplete units assigned to the main army play."""
    main_army_play = get_main_army_play(plays)
    if main_army_play is None:
        return {}, {}
    return main_army_play.get_squad().get_unit_count_by_type(), main_army_play.assigned_incomplete_units


class PvT(StrategyEngine):
    class TerranStrategy(Enum):
        Unknown = "Unknown"
        WorkerRush = "WorkerRush"  # Detected by seeing more than two workers in our main without other combat units
        BunkerContain = "BunkerContain"  # Detected by seeing enemy building an early bunker at our natural
        ProxyRush = "ProxyRush"  # Detected by seeing fewer buildings than expected in main
        MarineRush = "MarineRush"  # Detected by seeing early barracks or early marines
        MarinePressure = "MarinePressure"  # For when we detect a marine rush but then lose it, indicating the enemy
        # might be saving up marines for a larger push
        WallIn = "WallIn"  # Detected by scout blocked by buildings
        BlockScouting = "BlockScouting"  # The enemy has blocked our scout from getting into their main, suspect
        # shenanigans
        FastExpansion = "FastExpansion"  # Natural expansion taken early
        TwoFactory = "TwoFactory"  # The enemy has built two factories early
        NormalOpening = "Normal"  # Normal opening
        MidGameMech = "MidGameMech"  # Opponent has transitioned from opening into a mech-heavy composition
        MidGameBio = "MidGameBio"  # Opponent has transitioned from opening into a bio-heavy composition
        MidGameBioMech = "MidGameBioMech"  # Opponent has transitioned from opening into a balanced composition
        # TODO: Learn more Terran openings
        # TODO: Mid- and late game

    class OurStrategy(Enum):
        ForgeExpandGoons = "ForgeExpandGoons"  # FFE strategy we use against Random
        EarlyGameDefense = "EarlyGameDefense"  # We don't have enough scouting data yet
        AntiBunkerContain = "AntiBunkerContain"
        AntiMarineRush = "AntiMarineRush"  # For fast rushes, proxy rushes or any serious early pressure, defends main
        # until it can get tech out
        FastExpansion = "FastExpansion"  # For when the opponent plays a greedy strategy
        Defensive = "Defensive"  # For when the opponent is playing an aggressive strategy that isn't considered a
        # zealot rush
        NormalOpening = "Normal"  # Normal non-greedy and non-cautious opening
        MidGame = "MidGame"  # When we have reached the mid-game
        LateGameCarriers = "LateGameCarriers"  # Late-game carrier strategy
        # TODO: More mid- and late-game strategies

    def __init__(self) -> None:
        self.enemy_strategy = PvT.TerranStrategy.Unknown
        self.our_strategy = PvT.OurStrategy.EarlyGameDefense
        self.enemy_strategy_changed = 0

    def get_enemy_strategy(self) -> str:
        return self.enemy_strategy.value

    def get_our_strategy(self) -> str:
        return self.our_strategy.value

    def is_enemy_rushing(self) -> bool:
        return self.enemy_strategy in (PvT.TerranStrategy.WorkerRush, PvT.TerranStrategy.ProxyRush,
                                       PvT.TerranStrategy.MarineRush)

    def is_enemy_proxy(self) -> bool:
        return self.enemy_strategy == PvT.TerranStrategy.ProxyRush

    def recognize_enemy_strategy(self) -> PvT.TerranStrategy:
        from stardust.strategist.strategy_engines.pv_t import pv_t_enemy_strategy_recognizer

        return pv_t_enemy_strategy_recognizer.recognize_enemy_strategy(self)

    def choose_our_strategy(self, new_enemy_strategy: PvT.TerranStrategy, plays: list[Play]) -> PvT.OurStrategy:
        from stardust.strategist.strategy_engines.pv_t import pv_t_strategy_selection

        return pv_t_strategy_selection.choose_our_strategy(self, new_enemy_strategy, plays)

    def initialize(self, plays: list[Play], transitioning_from_random: bool, opening_override: str) -> None:
        if opening_override:
            plays.clear()

        if transitioning_from_random:
            plays.insert(before_play_index(plays, MainArmyPlay), EjectEnemyScout())

            if get_play(plays, ForgeFastExpand) is not None:
                self.our_strategy = PvT.OurStrategy.ForgeExpandGoons
        else:
            plays.append(SaturateBases())
            plays.append(EarlyGameWorkerScout())
            plays.append(EjectEnemyScout())
            plays.append(DefendMyMain())

        # (Stardust has a commented-out fast expansion on some maps for the AIST S4 vs. Human match here.)

        opponent.add_my_strategy_change(self.our_strategy.value)
        opponent.add_enemy_strategy_change(self.enemy_strategy.value)

    def update_plays(self, plays: list[Play]) -> None:
        import stardust.strategist.strategist as strategist

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
            self.enemy_strategy_changed = common.current_frame
            opponent.add_enemy_strategy_change(self.enemy_strategy.value)

        if self.our_strategy != new_strategy:
            message = f"Our strategy changed from {self.our_strategy.value} to {new_strategy.value}"
            if config.LOGGING_ENABLED:
                log.get(message)
            if config.CHERRYVIS_ENABLED:
                cherryvis.log(message)

            self.our_strategy = new_strategy
            opponent.add_my_strategy_change(self.our_strategy.value)

        me = bwapi.Broodwar.self()
        our_strategy = self.our_strategy
        if common_engine.has_enemy_stolen_our_gas():
            defend_our_main = True
        elif our_strategy == PvT.OurStrategy.ForgeExpandGoons:
            # Attack when we have goon range
            defend_our_main = me.getUpgradeLevel(UpgradeTypes.Singularity_Charge) == 0
        elif our_strategy in (PvT.OurStrategy.EarlyGameDefense, PvT.OurStrategy.AntiMarineRush,
                              PvT.OurStrategy.Defensive):
            defend_our_main = True
        else:
            if our_strategy == PvT.OurStrategy.AntiBunkerContain:
                # Switch to attack when we have goon range
                # (As in Stardust, this falls through to the default case, which recomputes it.)
                defend_our_main = me.getUpgradeLevel(UpgradeTypes.Singularity_Charge) == 0

                # Elevator some units out of our main to attack the enemy main
                if get_play(plays, Elevator) is None:
                    plays.insert(before_play_index(plays, MainArmyPlay), Elevator(True, UnitTypes.Protoss_Dragoon))

            main_army_play = get_main_army_play(plays)
            if main_army_play is None:
                defend_our_main = True

            # Require range off of an FFE
            elif (get_play(plays, ForgeFastExpand) is not None
                  and me.getUpgradeLevel(UpgradeTypes.Singularity_Charge) == 0):
                defend_our_main = True
            else:
                defend_our_main = False

                # Transition from a defend squad when the vanguard cluster has 2 units
                if main_army_play.is_defensive():
                    vanguard = main_army_play.get_squad().vanguard_cluster()
                    defend_our_main = vanguard is None or len(vanguard.units) < 2

        attack_plays.update_attack_plays(plays, defend_our_main)

        # Set the worker scout to monitor the enemy choke if we've detected a non-proxy marine rush
        strategy_engine = strategist.get_strategy_engine()
        assert strategy_engine is not None
        if (strategist.get_worker_scout_status() == strategist.WorkerScoutStatus.EnemyBaseScouted
                and not strategy_engine.is_enemy_proxy()
                and our_strategy == PvT.OurStrategy.AntiMarineRush):
            worker_scout_play = get_play(plays, EarlyGameWorkerScout)
            if worker_scout_play is not None:
                worker_scout_play.monitor_enemy_choke()

        common_engine.update_defend_base_plays(plays)
        common_engine.update_special_teams_plays(plays)
        expansions.default_expansions(plays)
        common_engine.scout_expos(plays, 15000)

        # Add an elevator play if it makes sense
        if get_play(plays, Elevator) is None and Elevator.is_elevator_feasible(UnitTypes.Protoss_Dragoon):
            plays.insert(before_play_index(plays, MainArmyPlay), Elevator(False, UnitTypes.Protoss_Dragoon))

        # (Stardust has a commented-out carrier harass for the AIST S4 vs. Human match here.)

    def update_production(self, plays: list[Play], prioritized_production_goals: ProductionGoals,
                          mineral_reservations: MineralReservations) -> None:
        import stardust.strategist.strategist as strategist

        goals = prioritized_production_goals
        frame = common.current_frame

        common_engine.reserve_minerals_for_expansion(mineral_reservations)
        self._handle_natural_expansion(plays, goals)
        self._handle_detection(plays, goals)

        if common_engine.handle_island_expansion_production(plays, goals):
            return

        completed_units, incomplete_units = _unit_counts(plays)

        zealot_count = (completed_units.get(UnitTypes.Protoss_Zealot, 0)
                        + incomplete_units.get(UnitTypes.Protoss_Zealot, 0))
        dragoon_count = (completed_units.get(UnitTypes.Protoss_Dragoon, 0)
                         + incomplete_units.get(UnitTypes.Protoss_Dragoon, 0))

        in_progress_count = (units.count_incomplete(UnitTypes.Protoss_Zealot)
                             + units.count_incomplete(UnitTypes.Protoss_Dragoon)
                             + units.count_incomplete(UnitTypes.Protoss_Dark_Templar))

        if get_play(plays, ForgeFastExpand) is None:
            common_engine.handle_gas_steal_production(goals, zealot_count)

        def mid_and_late_game_main_army_production() -> None:
            # Baseline production is one combat unit for every 6 workers (approximately 3 units per mining base)
            higher_priority_count = (workers.mineral_workers() // 6) - in_progress_count

            # Counter tanks with speedlots once the enemy has at least four
            enemy_tanks = (units.count_enemy(UnitTypes.Terran_Siege_Tank_Siege_Mode)
                           + units.count_enemy(UnitTypes.Terran_Siege_Tank_Tank_Mode))
            if enemy_tanks > 2:
                # Keep proportionally more dragoons if the enemy has a lot of vultures
                desired_zealots = min((dragoon_count * 3) // 2, enemy_tanks * 3)
                if units.count_enemy(UnitTypes.Terran_Vulture) > enemy_tanks:
                    desired_zealots = min(dragoon_count, enemy_tanks * 2)

                if desired_zealots > zealot_count:
                    common_engine.main_army_production(goals, UnitTypes.Protoss_Zealot, desired_zealots - zealot_count,
                                                       higher_priority_count)

                upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Leg_Enhancements), UnitTypes.Protoss_Zealot, 0)

            common_engine.main_army_production(goals, UnitTypes.Protoss_Dragoon, -1, higher_priority_count)
            common_engine.main_army_production(goals, UnitTypes.Protoss_Zealot, -1, higher_priority_count)

        our_strategy = self.our_strategy
        if our_strategy == PvT.OurStrategy.ForgeExpandGoons:
            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Dragoon, -1, -1))
            upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Singularity_Charge), UnitTypes.Protoss_Dragoon, 1)
        elif our_strategy == PvT.OurStrategy.EarlyGameDefense:
            common_engine.one_gate_core_opening(goals, dragoon_count, zealot_count, 1)
            upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Singularity_Charge), UnitTypes.Protoss_Dragoon, 1)
        elif our_strategy == PvT.OurStrategy.AntiMarineRush:
            # Higher danger level if:
            # - Marines are on the map
            # - We have no scout left
            # - The strategy has been stable for 300 frames and our scout isn't monitoring the choke yet
            high_danger = (PvT._marines_have_moved_out() or strategist.is_worker_scout_complete()
                           or (strategist.get_worker_scout_status() != strategist.WorkerScoutStatus.MonitoringEnemyChoke
                               and self.enemy_strategy_changed < (frame - 300)))

            required_cannons = 0
            if high_danger:
                if (zealot_count + dragoon_count) < 3:
                    required_cannons = 2
                elif (zealot_count + dragoon_count) < 5:
                    required_cannons = 1

            completed_cannons = PvT._build_cannons(goals, required_cannons)

            # Determine how many zealots we want
            # Zealots are relatively useless against groups of kiting marines, so we want to transition to dragoons as
            # quickly as possible
            zealots_required = 0
            if high_danger:
                zealots_required = max(0, (3 if dragoon_count < 3 else 2) - (completed_cannons * 2) - zealot_count)
            common_engine.handle_anti_rush_production(goals, dragoon_count, zealot_count, zealots_required, 1)
        elif our_strategy == PvT.OurStrategy.AntiBunkerContain:
            # Build pure goons
            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE-anticontain", UnitTypes.Protoss_Dragoon, -1, -1))

            # Get goon range
            upgrades.upgrade(goals, _upgrade(UpgradeTypes.Singularity_Charge))

            # Get a shuttle
            if units.count_all(UnitTypes.Protoss_Shuttle) < 1:
                add_goal(goals, PRIORITY_SPECIALTEAMS, UnitProductionGoal("SE", UnitTypes.Protoss_Shuttle, 1, 1))
        elif our_strategy in (PvT.OurStrategy.FastExpansion, PvT.OurStrategy.Defensive,
                              PvT.OurStrategy.NormalOpening):
            # If any zealots are in production, and we don't have an emergency production goal, cancel them
            if units.count_incomplete(UnitTypes.Protoss_Zealot) > 0:
                common_engine.cancel_training_units(goals, UnitTypes.Protoss_Zealot)

            # Try to keep at least two army units in production while taking our natural
            higher_priority_count = 2 - in_progress_count
            common_engine.main_army_production(goals, UnitTypes.Protoss_Dragoon, -1, higher_priority_count)

            # Default upgrades
            self._handle_upgrades(goals)
        elif our_strategy == PvT.OurStrategy.MidGame:
            # (Stardust has commented-out DT harass production here.)

            # Against mech, get one shuttle unless the enemy has more than one goliath
            # Obs may get higher priority depending on what we have scouted
            if (units.count_all(UnitTypes.Protoss_Shuttle) < 1
                    and self.enemy_strategy == PvT.TerranStrategy.MidGameMech
                    and units.count_enemy(UnitTypes.Terran_Goliath) < 2):
                add_goal(goals, PRIORITY_SPECIALTEAMS, UnitProductionGoal("SE", UnitTypes.Protoss_Shuttle, 1, 1))

            # Against mech, build arbiters on three completed nexuses and a significant army size
            arbiter_count = units.count_all(UnitTypes.Protoss_Arbiter)
            if (self.enemy_strategy == PvT.TerranStrategy.MidGameMech and arbiter_count < 2
                    and units.count_completed(UnitTypes.Protoss_Nexus) > 2
                    and (zealot_count + dragoon_count) > 20):
                add_goal(goals, PRIORITY_NORMAL, UnitProductionGoal("SE", UnitTypes.Protoss_Arbiter, 1, 1))

            mid_and_late_game_main_army_production()

            # Default upgrades
            self._handle_upgrades(goals)
        elif our_strategy == PvT.OurStrategy.LateGameCarriers:
            # Produce unlimited carriers off of two stargates, at slightly higher priority than main army
            add_goal(goals, PRIORITY_NORMAL, UnitProductionGoal("SE", UnitTypes.Protoss_Carrier, -1, 2))

            mid_and_late_game_main_army_production()

            # Besides default upgrades, upgrade air weapons immediately
            weapon_level = bwapi.Broodwar.self().getUpgradeLevel(UpgradeTypes.Protoss_Air_Weapons)
            if weapon_level < 3:
                add_goal(goals, PRIORITY_NORMAL,
                         UpgradeProductionGoal("SE", _upgrade(UpgradeTypes.Protoss_Air_Weapons), weapon_level + 1, 1))
            self._handle_upgrades(goals)

    @staticmethod
    def _build_cannons(prioritized_production_goals: ProductionGoals, desired_count: int) -> int:
        """Queues cannons in the main for the anti marine rush strategy; returns the number of (nearly) completed
        cannons."""
        my_main = game_map.get_my_main()
        assert my_main is not None
        base_static_defense_locations = building_placement.base_static_defense_locations(my_main)
        if not base_static_defense_locations.is_valid():
            return 0

        locations = list(base_static_defense_locations.worker_defense_cannons)
        if base_static_defense_locations.start_block_cannon != TilePositions.Invalid:
            locations.insert(1, base_static_defense_locations.start_block_cannon)

        def build_at_tile(tile: TilePosition, unit_type: UnitType) -> None:
            if builder.is_pending_here(tile):
                return

            build_location = BuildLocation(Location(tile),
                                           building_placement.builder_frames(my_main.get_position(), tile, unit_type),
                                           0, 0)
            add_goal(prioritized_production_goals, PRIORITY_EMERGENCY,
                     UnitProductionGoal.at("SE-antirush", unit_type, build_location))

        completed_cannons = 0
        for location in locations:
            if not location.isValid():
                continue

            cannon = units.my_building_at(location)
            if cannon is not None and cannon.type == UnitTypes.Protoss_Photon_Cannon:
                desired_count -= 1

                if cannon.completed or cannon.estimated_completion_frame < (common.current_frame + 200):
                    completed_cannons += 1

                continue

            if desired_count > 0:
                pylon = units.my_building_at(base_static_defense_locations.power_pylon)
                if pylon is not None:
                    if pylon.completed:
                        build_at_tile(location, UnitTypes.Protoss_Photon_Cannon)
                        desired_count -= 1
                else:
                    build_at_tile(base_static_defense_locations.power_pylon, UnitTypes.Protoss_Pylon)
                    return completed_cannons

        return completed_cannons

    @staticmethod
    def _marines_have_moved_out() -> bool:
        # First check if there is any observed marine or barracks closer to our main than the enemy's natural choke
        enemy_natural_choke = game_map.get_enemy_natural_choke()
        if enemy_natural_choke is None:
            return True

        my_main = game_map.get_my_main()
        assert my_main is not None
        navigation_grid = path_finding.get_navigation_grid(my_main.get_position(), True)
        if navigation_grid is None:
            return True

        natural_choke_cost = navigation_grid.node(enemy_natural_choke.center).cost
        for enemy_unit in units.all_enemy():
            if enemy_unit.type in (UnitTypes.Terran_Marine, UnitTypes.Terran_Barracks):
                if navigation_grid.node(enemy_unit.last_position).cost < natural_choke_cost:
                    return True

        return False

    def _handle_natural_expansion(self, plays: list[Play], prioritized_production_goals: ProductionGoals) -> None:
        # Hop out if the natural has already been taken
        natural = game_map.get_my_natural()
        if natural is None or natural.owned_since != -1:
            cherryvis.set_board_value("natural", "complete")
            return

        # Don't expand while we are setting up an elevator rush
        elevator_rush = get_play(plays, ElevatorRush)
        if elevator_rush is not None and elevator_rush.get_squad().empty():
            cherryvis.set_board_value("natural", "wait-elevator")
            return

        # If we have a backdoor natural, expand when our second goon is being produced or we have lots of money
        if game_map.map_specific_override().has_backdoor_natural():
            if bwapi.Broodwar.self().minerals() > 450 or units.count_all(UnitTypes.Protoss_Dragoon) > 1:
                cherryvis.set_board_value("natural", "take-backdoor")

                expansions.take_natural_expansion(plays, prioritized_production_goals)
                return

        our_strategy = self.our_strategy
        if our_strategy == PvT.OurStrategy.ForgeExpandGoons:
            # Handled by the play
            cherryvis.set_board_value("natural", "forge-expand")
            return
        elif our_strategy in (PvT.OurStrategy.EarlyGameDefense, PvT.OurStrategy.AntiBunkerContain,
                              PvT.OurStrategy.AntiMarineRush, PvT.OurStrategy.Defensive):
            # Don't take our natural if the enemy could be rushing or doing an all-in
            cherryvis.set_board_value("natural", "wait-defensive")
        elif our_strategy == PvT.OurStrategy.FastExpansion:
            cherryvis.set_board_value("natural", "take-fast-expo")
            expansions.take_natural_expansion(plays, prioritized_production_goals)
            return
        else:
            # Expand as soon as our main army transitions to attack
            if get_play(plays, AttackEnemyBase) is None:
                cherryvis.set_board_value("natural", "no-attack-play")
            else:
                cherryvis.set_board_value("natural", "take")
                expansions.take_natural_expansion(plays, prioritized_production_goals)
                return

        expansions.cancel_natural_expansion(plays, prioritized_production_goals)

    def _handle_upgrades(self, prioritized_production_goals: ProductionGoals) -> None:
        goals = prioritized_production_goals

        # For PvT dragoon range is important to get early, so get it first unless the enemy is rushing us
        upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Singularity_Charge), UnitTypes.Protoss_Dragoon,
                                  2 if self.our_strategy == PvT.OurStrategy.AntiMarineRush else 0)

        # Basic infantry skill upgrades are queued when we have enough of them and are still building them
        upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Leg_Enhancements), UnitTypes.Protoss_Zealot, 6)

        # Cases where we want the upgrade as soon as we start building one of the units
        upgrades.upgrade_when_unit_created(goals, _upgrade(UpgradeTypes.Gravitic_Boosters), UnitTypes.Protoss_Observer,
                                           False, False, PRIORITY_HIGHPRIORITYUPGRADES)
        upgrades.upgrade_when_unit_created(goals, _upgrade(UpgradeTypes.Gravitic_Drive), UnitTypes.Protoss_Shuttle,
                                           False, units.count_completed(UnitTypes.Protoss_Assimilator) < 3)
        upgrades.upgrade_when_unit_created(goals, _upgrade(UpgradeTypes.Carrier_Capacity), UnitTypes.Protoss_Carrier,
                                           True, False, PRIORITY_HIGHPRIORITYUPGRADES)
        upgrades.upgrade_when_unit_created(goals, _upgrade(UpgradeTypes.Khaydarin_Core), UnitTypes.Protoss_Arbiter,
                                           False, False, PRIORITY_HIGHPRIORITYUPGRADES)
        upgrades.upgrade_when_unit_created(goals, _upgrade(TechTypes.Stasis_Field), UnitTypes.Protoss_Arbiter,
                                           False, False, PRIORITY_HIGHPRIORITYUPGRADES)

        # Get observer sight range on three gases
        if units.count_completed(UnitTypes.Protoss_Assimilator) >= 3:
            upgrades.upgrade_when_unit_created(goals, _upgrade(UpgradeTypes.Sensor_Array), UnitTypes.Protoss_Observer,
                                               False, False, PRIORITY_HIGHPRIORITYUPGRADES)

        upgrades.default_ground_upgrades(goals)

        # TODO: Air upgrades

    def _handle_detection(self, plays: list[Play], prioritized_production_goals: ProductionGoals) -> None:
        # The main army play will reactively request mobile detection when it sees a cloaked enemy unit
        # The logic here is to look ahead to make sure we already have detection available when we need it
        frame = common.current_frame

        # Break out if we are already building an observer
        if units.count_incomplete(UnitTypes.Protoss_Observer) > 0:
            return

        # Get the number of observers in our main army
        observers_with_main_army = 0
        main_army_play = get_main_army_play(plays)
        if main_army_play is not None:
            observers_with_main_army = len(main_army_play.get_squad().get_detectors())

        # In PvT, since the enemy can snipe observers with comsat relatively easily, try to keep 2 observers with the
        # main army
        if observers_with_main_army >= 2:
            return

        def build_observer(priority: int = PRIORITY_NORMAL) -> None:
            add_goal(prioritized_production_goals, priority,
                     UnitProductionGoal("SE-detection", UnitTypes.Protoss_Observer, 1, 1))

        # Build an observer if the enemy has cloaked wraith tech
        if (units.has_enemy_built(UnitTypes.Terran_Control_Tower)
                or players.has_researched(bwapi.Broodwar.enemy(), TechTypes.Cloaking_Field)):
            build_observer(PRIORITY_SPECIALTEAMS)
            return

        # In all other cases, wait until we are on two bases
        if units.count_completed(UnitTypes.Protoss_Nexus) < 2:
            return

        # Get obs immediately if we've seen a spider mine
        if units.has_enemy_built(UnitTypes.Terran_Vulture_Spider_Mine):
            build_observer(PRIORITY_SPECIALTEAMS)
            return

        # Get obs earlier if we've seen a vulture or tank, indicating mech play and likely spider mines
        if frame > 10000 and (units.has_enemy_built(UnitTypes.Terran_Siege_Tank_Tank_Mode)
                              or units.has_enemy_built(UnitTypes.Terran_Siege_Tank_Siege_Mode)
                              or units.has_enemy_built(UnitTypes.Terran_Vulture)):
            build_observer()
            return

        # Otherwise start getting obs at frame 14000
        if frame > 14000:
            build_observer()
            return

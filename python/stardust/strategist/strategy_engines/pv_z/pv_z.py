"""Port of Strategist/StrategyEngines/PvZ.h and PvZ/PvZ.cpp: the Protoss vs. Zerg strategy engine.

The enemy strategy recognizer and our strategy selection are in pv_z_enemy_strategy_recognizer and
pv_z_strategy_selection. Enum values are Stardust's strategy names (as written to the opponent model).
"""

from __future__ import annotations

from enum import Enum
from typing import TYPE_CHECKING

import bwapi
from bwapi import TechTypes, UnitType, UnitTypes, UpgradeTypes
from stardust import common, config, opponent
from stardust.builder import building_placement
from stardust.cpp import INT_MAX, fdiv
from stardust.general.unit_cluster.unit_cluster import Activity
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.players import players
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_BASEDEFENSE, PRIORITY_DEPOTS, PRIORITY_MAINARMY, PRIORITY_NORMAL, \
    PRIORITY_SPECIALTEAMS, MineralReservations, Play, ProductionGoals, add_goal
from stardust.strategist.plays.macro.saturate_bases import SaturateBases
from stardust.strategist.plays.main_army.attack_enemy_base import AttackEnemyBase
from stardust.strategist.plays.main_army.defend_my_main import DefendMyMain
from stardust.strategist.plays.main_army.forge_fast_expand import ForgeFastExpand
from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay
from stardust.strategist.plays.scouting.early_game_worker_scout import EarlyGameWorkerScout
from stardust.strategist.plays.scouting.eject_enemy_scout import EjectEnemyScout
from stardust.strategist.plays.special_teams.corsairs import Corsairs
from stardust.strategist.plays.special_teams.elevator import Elevator
from stardust.strategist.strategy_engine import StrategyEngine, before_play_index, get_main_army_play, get_play
from stardust.strategist.strategy_engines.common import attack_plays, defensive_cannons, expansions, upgrades
from stardust.strategist.strategy_engines.common import strategy_engine as common_engine
from stardust.units import units
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.map.base import Base


def _upgrade(upgrade_type: bwapi.UpgradeType | bwapi.TechType) -> UpgradeOrTechType:
    return UpgradeOrTechType.of(upgrade_type)


def _unit_counts(plays: list[Play]) -> tuple[dict[UnitType, int], dict[UnitType, int]]:
    """The completed unit counts of the main army squad and the incomplete units assigned to the main army play."""
    main_army_play = get_main_army_play(plays)
    if main_army_play is None:
        return {}, {}
    return main_army_play.get_squad().get_unit_count_by_type(), main_army_play.assigned_incomplete_units


def _enemy_has_our_natural() -> bool:
    natural = game_map.get_my_natural()
    return natural is not None and natural.owner == bwapi.Broodwar.enemy()


class PvZ(StrategyEngine):
    class ZergStrategy(Enum):
        Unknown = "Unknown"
        WorkerRush = "WorkerRush"  # Detected by seeing more than two workers in our main without other combat units
        SunkenContain = "SunkenContain"  # Hatch and sunkens at our natural
        ZerglingRush = "ZerglingRush"  # Detected by seeing early pool or early lings
        PoolBeforeHatchery = "PoolBeforeHatchery"  # e.g. 9-pool or overpool
        HatcheryBeforePool = "HatcheryBeforePool"  # e.g. 10 or 12 hatch
        ZerglingAllIn = "ZerglingAllIn"  # Builds that get mass zerglings before other tech
        HydraBust = "HydraBust"  # Builds that get mass hydras
        Turtle = "Turtle"  # Mass sunkens
        Lair = "Lair"  # Have seen lair, indicates possibility of mutas or lurkers
        MutaRush = "MutaRush"  # Builds that get mutas quickly
        # TODO: Mid- and late game

    class OurStrategy(Enum):
        EarlyGameDefense = "EarlyGameDefense"  # We don't have scouting data yet
        AntiAllIn = "AntiAllIn"  # For fast rushes or any serious early pressure, defends main until it can get tech out
        AntiSunkenContain = "AntiSunkenContain"  # For when enemy builds sunkens at our natural
        SairSpeedlot = "SairSpeedlot"  # For when we do a FFE into sair/speedlot
        FFEDragoons = "FFEDragoons"  # For when we do a FFE into dragoons
        FastExpansion = "FastExpansion"  # For when we expand quickly
        Defensive = "Defensive"  # Cautious opening, for when we don't know if the opponent could be going for an
        # all-in
        Normal = "Normal"  # Normal non-greedy and non-cautious opening
        MidGame = "MidGame"  # When we have reached the mid-game
        # TODO: Various mid-game and late-game strategies

    def __init__(self) -> None:
        self.enemy_strategy = PvZ.ZergStrategy.Unknown
        self.our_strategy = PvZ.OurStrategy.EarlyGameDefense
        self.enemy_strategy_changed = 0

    def get_enemy_strategy(self) -> str:
        return self.enemy_strategy.value

    def get_our_strategy(self) -> str:
        return self.our_strategy.value

    def is_enemy_rushing(self) -> bool:
        return self.enemy_strategy in (PvZ.ZergStrategy.WorkerRush, PvZ.ZergStrategy.ZerglingRush)

    def recognize_enemy_strategy(self) -> PvZ.ZergStrategy:
        from stardust.strategist.strategy_engines.pv_z import pv_z_enemy_strategy_recognizer

        return pv_z_enemy_strategy_recognizer.recognize_enemy_strategy(self)

    def choose_our_strategy(self, new_enemy_strategy: PvZ.ZergStrategy, plays: list[Play]) -> PvZ.OurStrategy:
        from stardust.strategist.strategy_engines.pv_z import pv_z_strategy_selection

        return pv_z_strategy_selection.choose_our_strategy(self, new_enemy_strategy, plays)

    def initialize(self, plays: list[Play], transitioning_from_random: bool, opening_override: str) -> None:
        if opening_override:
            plays.clear()

        if transitioning_from_random:
            plays.insert(before_play_index(plays, MainArmyPlay), EjectEnemyScout())
            plays.insert(before_play_index(plays, MainArmyPlay), Corsairs())

            if get_play(plays, ForgeFastExpand) is not None:
                self.our_strategy = PvZ.OurStrategy.FFEDragoons
        else:
            plays.append(SaturateBases())
            plays.append(EarlyGameWorkerScout())
            plays.append(EjectEnemyScout())
            plays.append(Corsairs())

            opening = opening_override
            if not opening:
                opening = opponent.select_opening_ucb1([PvZ.OurStrategy.FFEDragoons.value,
                                                        PvZ.OurStrategy.SairSpeedlot.value,
                                                        PvZ.OurStrategy.EarlyGameDefense.value])
            if (opening in (PvZ.OurStrategy.SairSpeedlot.value, PvZ.OurStrategy.FFEDragoons.value)
                    and building_placement.has_forge_gateway_wall()):
                plays.append(ForgeFastExpand())
                if opening == PvZ.OurStrategy.SairSpeedlot.value:
                    self.our_strategy = PvZ.OurStrategy.SairSpeedlot
                else:
                    self.our_strategy = PvZ.OurStrategy.FFEDragoons
            else:
                plays.append(DefendMyMain())
            log.get(f"Selected opening {self.our_strategy.value}")

        opponent.add_my_strategy_change(self.our_strategy.value)
        opponent.add_enemy_strategy_change(self.enemy_strategy.value)

    def update_plays(self, plays: list[Play]) -> None:
        import stardust.strategist.strategist as strategist

        frame = common.current_frame
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

        me = bwapi.Broodwar.self()
        our_strategy = self.our_strategy
        if common_engine.has_enemy_stolen_our_gas():
            defend_our_main = True
        else:
            main_army_play = get_main_army_play(plays)

            def can_transition_to_attack(required_unit_count: int, require_dragoon: bool) -> bool:
                if main_army_play is None:
                    cherryvis.set_board_value("defend-main-reason", "no-main-army-play")
                    return False

                vanguard_cluster = main_army_play.get_squad().vanguard_cluster()
                if vanguard_cluster is None or vanguard_cluster.vanguard is None:
                    cherryvis.set_board_value("defend-main-reason", "no-vanguard")
                    return False
                vanguard = vanguard_cluster.vanguard

                radius = 200 if main_army_play.is_defensive() else 250
                has_dragoon = False
                count = 0
                for unit in vanguard_cluster.units:
                    if unit.is_static_ground_defense():
                        continue

                    if unit.get_distance(vanguard) < radius:
                        if unit.type == UnitTypes.Protoss_Dragoon:
                            has_dragoon = True
                        count += 1

                if count < required_unit_count:
                    cherryvis.set_board_value("defend-main-reason", "not-enough-units")
                    return False
                if require_dragoon and not has_dragoon:
                    cherryvis.set_board_value("defend-main-reason", "no-dragoon")
                    return False

                # If the enemy has done a sneak attack recently, don't leave our base until we have built defensive
                # cannons
                if self._is_ffe():
                    return True  # only relevant for non-FFE builds
                if frame > 10000:
                    return True  # only relevant in early game
                if _enemy_has_our_natural():
                    return True  # exception if the enemy has our natural
                sneak_attack = opponent.min_value_in_previous_games("sneakAttack", INT_MAX, 20, 0)
                if sneak_attack <= 10000 and units.count_completed(UnitTypes.Protoss_Photon_Cannon) < 2:
                    cherryvis.set_board_value("defend-main-reason", "no-anti-sneak-attack-cannons")
                    return False
                return True

            if our_strategy in (PvZ.OurStrategy.EarlyGameDefense, PvZ.OurStrategy.AntiAllIn):
                defend_our_main = True
            elif our_strategy == PvZ.OurStrategy.AntiSunkenContain:
                # Attack when we have +1 and speed
                defend_our_main = (me.getUpgradeLevel(UpgradeTypes.Protoss_Ground_Weapons) == 0
                                   or me.getUpgradeLevel(UpgradeTypes.Leg_Enhancements) == 0)

                # Elevator some units out of our main to attack the enemy main
                if get_play(plays, Elevator) is None:
                    plays.insert(before_play_index(plays, MainArmyPlay), Elevator(True, UnitTypes.Protoss_Zealot))
            elif our_strategy == PvZ.OurStrategy.SairSpeedlot:
                # Attack when we have +1 and speed or 6 corsairs and an army
                # This strategy transitions to mid game as soon as it has gone on the attack, so the opposite
                # transition is not needed
                defend_our_main = True
                if ((me.getUpgradeLevel(UpgradeTypes.Protoss_Ground_Weapons) > 0
                     and me.getUpgradeLevel(UpgradeTypes.Leg_Enhancements) > 0)
                        or (units.count_completed(UnitTypes.Protoss_Corsair) > 5
                            and can_transition_to_attack(5, False))):
                    defend_our_main = False
            elif our_strategy == PvZ.OurStrategy.FFEDragoons:
                if main_army_play is None:
                    defend_our_main = True

                # Transition to attack when we have 5 units in our army and range is started
                elif main_army_play.is_defensive():
                    defend_our_main = True
                    if ((me.getUpgradeLevel(UpgradeTypes.Singularity_Charge) > 0
                         or units.is_being_upgraded_or_researched(_upgrade(UpgradeTypes.Singularity_Charge)))
                            and can_transition_to_attack(5, True)):
                        defend_our_main = False
                else:
                    # TODO: Should we ever transition back to defense?
                    defend_our_main = False
            else:
                if main_army_play is None:
                    defend_our_main = True
                else:
                    defend_our_main = False

                    # Transition from defense when appropriate
                    if main_army_play.is_defensive():
                        if our_strategy == PvZ.OurStrategy.FastExpansion:
                            defend_our_main = not can_transition_to_attack(3, False)
                        else:
                            defend_our_main = not can_transition_to_attack(4, True)

        attack_plays.update_attack_plays(plays, defend_our_main)

        # Set the worker scout to monitor the enemy choke once the pool is finished if we are in a defensive mode
        if (strategist.get_worker_scout_status() == strategist.WorkerScoutStatus.EnemyBaseScouted
                and our_strategy in (PvZ.OurStrategy.AntiAllIn, PvZ.OurStrategy.Defensive)):
            worker_scout_play = get_play(plays, EarlyGameWorkerScout)
            if worker_scout_play is not None:
                pools = units.all_enemy_of_type(UnitTypes.Zerg_Spawning_Pool)
                if pools and next(iter(pools)).completed:
                    worker_scout_play.monitor_enemy_choke()

        common_engine.update_defend_base_plays(plays)
        common_engine.update_special_teams_plays(plays)
        # When doing an FFE, wait until at least frame 10000 to take a third
        if not self._is_ffe() or frame > 10000:
            expansions.default_expansions(plays)
        common_engine.scout_expos(plays, 10000)

    def update_production(self, plays: list[Play], prioritized_production_goals: ProductionGoals,
                          mineral_reservations: MineralReservations) -> None:
        goals = prioritized_production_goals
        frame = common.current_frame

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
        dt_count = (completed_units.get(UnitTypes.Protoss_Dark_Templar, 0)
                    + incomplete_units.get(UnitTypes.Protoss_Dark_Templar, 0))
        corsair_count = units.count_all(UnitTypes.Protoss_Corsair)

        in_progress_count = (units.count_incomplete(UnitTypes.Protoss_Zealot)
                             + units.count_incomplete(UnitTypes.Protoss_Dragoon)
                             + units.count_incomplete(UnitTypes.Protoss_Dark_Templar)
                             + units.count_incomplete(UnitTypes.Protoss_Corsair))

        if not self._is_ffe():
            common_engine.handle_gas_steal_production(goals, zealot_count)

        def build_anti_sneak_attack_cannons() -> None:
            # Not relevant when we are doing an FFE
            if self._is_ffe():
                cherryvis.set_board_value("anti-sneak-attack", "ffe")
                return

            # Only relevant in early game
            if frame > 10000:
                return

            # Check if the enemy has done a sneak attack recently
            sneak_attack = opponent.min_value_in_previous_games("sneakAttack", INT_MAX, 20, 0)
            if sneak_attack > 10000:
                return

            # Build three cannons
            defensive_cannons.build_defensive_cannons(goals, False, 0, 3)

        def desired_corsairs() -> int:
            # Limit to 7 + 1 for every 2 enemy mutalisks
            limit = 7 + (units.count_enemy(UnitTypes.Zerg_Mutalisk) // 2)
            desired = limit - corsair_count
            if desired <= 0:
                return 0

            # Always build if we have DTs to force enemy to defend its overlords
            if dt_count > 0:
                return desired

            # Always build if the enemy has air units that can threaten our bases
            if units.has_enemy_built(UnitTypes.Zerg_Mutalisk):
                return desired

            # Don't build if the enemy has defended its overlords
            grid = players.grid(bwapi.Broodwar.enemy())
            defended_overlords = 0
            undefended_overlords = 0
            for overlord in units.all_enemy_of_type(UnitTypes.Zerg_Overlord):
                if not overlord.last_position_valid:
                    continue

                if grid.air_threat(overlord.last_position) == 0:
                    undefended_overlords += 1
                else:
                    defended_overlords += 1
            if defended_overlords > undefended_overlords:
                return 0

            return desired

        # Main army production
        our_strategy = self.our_strategy
        me = bwapi.Broodwar.self()
        if our_strategy == PvZ.OurStrategy.EarlyGameDefense:
            # We start with two-gate zealots until we have more scouting information
            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, -1, 2))
        elif our_strategy == PvZ.OurStrategy.AntiAllIn:
            # Get at least six zealots before dragoons, more if our choke is hard to defend
            desired_zealots = 6
            main_choke = game_map.get_my_main_choke()
            if main_choke is not None and not main_choke.is_narrow_choke:
                desired_zealots = 10

            # Also bump up the number of zealots if the enemy has a lot of lings
            desired_zealots = max(desired_zealots, 2 + units.count_enemy(UnitTypes.Zerg_Zergling) // 3)

            zealots_required = desired_zealots - zealot_count

            common_engine.handle_anti_rush_production(goals, dragoon_count, zealot_count, zealots_required)

            # We can build anti-sneak-attack cannons when we have started goon range
            if units.is_being_upgraded_or_researched(_upgrade(UpgradeTypes.Singularity_Charge)):
                cherryvis.set_board_value("anti-sneak-attack", "started-goon-range")
                build_anti_sneak_attack_cannons()
            else:
                cherryvis.set_board_value("anti-sneak-attack", "not-started-goon-range")
        elif our_strategy == PvZ.OurStrategy.AntiSunkenContain:
            # Cancel goons, goon range
            common_engine.cancel_training_units(goals, UnitTypes.Protoss_Dragoon)
            if units.is_being_upgraded_or_researched(_upgrade(UpgradeTypes.Singularity_Charge)):
                for core in units.all_mine_completed_of_type(UnitTypes.Protoss_Cybernetics_Core):
                    core_unit = core.bwapi_unit
                    if core_unit is not None and core_unit.isUpgrading():
                        core_unit.cancelUpgrade()

            # Build pure zealots
            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE-anticontain", UnitTypes.Protoss_Zealot, -1, -1))

            # Get +1 weapons, then zealot speed
            upgrades.upgrade(goals, _upgrade(UpgradeTypes.Protoss_Ground_Weapons))
            if (units.is_being_upgraded_or_researched(_upgrade(UpgradeTypes.Protoss_Ground_Weapons))
                    or me.getUpgradeLevel(UpgradeTypes.Protoss_Ground_Weapons) > 0):
                upgrades.upgrade(goals, _upgrade(UpgradeTypes.Leg_Enhancements))

            # Get a shuttle
            if units.count_all(UnitTypes.Protoss_Shuttle) < 1:
                add_goal(goals, PRIORITY_SPECIALTEAMS, UnitProductionGoal("SE", UnitTypes.Protoss_Shuttle, 1, 1))

            # Take one expansion using a shuttle
            if units.count_all(UnitTypes.Protoss_Nexus) < 2:
                expansions.take_expansion_with_shuttle(plays)
        elif our_strategy == PvZ.OurStrategy.FastExpansion:
            # We've scouted a non-threatening opening, so generally skip zealots and go straight for dragoons
            # Build a couple of zealots though if we have seen zerglings on the way and have nothing to defend with
            if units.count_enemy(UnitTypes.Zerg_Zergling) > 0:
                unit_count = zealot_count + dragoon_count
                if unit_count < 2:
                    add_goal(goals, PRIORITY_BASEDEFENSE,
                             UnitProductionGoal("SE-fe", UnitTypes.Protoss_Zealot, 2 - unit_count, 2))

            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE-fe", UnitTypes.Protoss_Dragoon, -1, -1))
            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE-fe", UnitTypes.Protoss_Zealot, -1, -1))

            # Default upgrades
            PvZ.handle_upgrades(goals)

            # Build anti-sneak-attack cannons immediately
            build_anti_sneak_attack_cannons()
        elif our_strategy == PvZ.OurStrategy.SairSpeedlot:
            is_enemy_muta_build = (self.enemy_strategy == PvZ.ZergStrategy.MutaRush
                                   or units.count_enemy(UnitTypes.Zerg_Mutalisk) > 5)

            corsairs = max(7 - corsair_count, desired_corsairs())

            # First sair is prioritized highest
            if corsair_count == 0:
                add_goal(goals, PRIORITY_DEPOTS + 1, UnitProductionGoal("SE", UnitTypes.Protoss_Corsair, 1, 1))
                corsairs -= 1

            # First zealots are prioritized next
            if zealot_count < 2:
                add_goal(goals, PRIORITY_DEPOTS + 1,
                         UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, 2 - zealot_count, 1))

            if not is_enemy_muta_build or corsairs > 4:
                upgrades.upgrade(goals, _upgrade(UpgradeTypes.Protoss_Ground_Weapons), 1, PRIORITY_NORMAL)
                upgrades.upgrade(goals, _upgrade(UpgradeTypes.Protoss_Air_Weapons), 1, PRIORITY_NORMAL)
            if not is_enemy_muta_build:
                upgrades.upgrade(goals, _upgrade(UpgradeTypes.Leg_Enhancements), 1, PRIORITY_NORMAL)

            if corsairs > 0:
                add_goal(goals, PRIORITY_MAINARMY,
                         UnitProductionGoal("SE", UnitTypes.Protoss_Corsair, corsairs,
                                            2 if units.count_enemy(UnitTypes.Zerg_Mutalisk) > 2 else 1))

            if is_enemy_muta_build:
                add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Dragoon, -1, -1))

            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, -1, -1))
        elif our_strategy == PvZ.OurStrategy.FFEDragoons:
            # Scale our desired zealots based on enemy ling count
            # Start with one before goons
            unit_count = zealot_count + dragoon_count
            cannon_count = units.count_completed(UnitTypes.Protoss_Photon_Cannon)
            desired_zealots = max(1 - unit_count,
                                  (1 + (units.count_enemy(UnitTypes.Zerg_Zergling) + 2) // 3) - cannon_count)
            if zealot_count < desired_zealots:
                add_goal(goals, PRIORITY_BASEDEFENSE,
                         UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, desired_zealots - zealot_count, -1))

            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Dragoon, -1, -1))
            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, -1, -1))

            # Upgrade goon range at 1 dragoon
            upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Singularity_Charge), UnitTypes.Protoss_Dragoon, 1)

            # Upgrade +1 attack at 2 zealots
            upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Protoss_Ground_Weapons), UnitTypes.Protoss_Zealot,
                                      2)

            # Upgrade leg speed at 5 zealots
            upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Leg_Enhancements), UnitTypes.Protoss_Zealot, 5)
        elif our_strategy in (PvZ.OurStrategy.Defensive, PvZ.OurStrategy.Normal):
            # Scale our desired zealots based on enemy ling count
            # Start with three before goons
            unit_count = zealot_count + dragoon_count
            desired_zealots = max(3 - unit_count, 1 + (units.count_enemy(UnitTypes.Zerg_Zergling) + 2) // 3)
            if zealot_count < desired_zealots:
                add_goal(goals, PRIORITY_BASEDEFENSE,
                         UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, desired_zealots - zealot_count, 2))

                common_engine.cancel_training_units(goals, UnitTypes.Protoss_Dragoon, desired_zealots - zealot_count,
                                                    UnitTypes.Protoss_Zealot.buildTime())

            corsairs = desired_corsairs()
            if corsairs > 0:
                add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Corsair, 1, 1))

            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Dragoon, -1, -1))
            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, -1, -1))

            # Upgrade goon range at 2 dragoons
            upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Singularity_Charge), UnitTypes.Protoss_Dragoon, 2)

            # Upgrade zealots at varying cutoffs depending on whether we already have a forge
            if units.count_all(UnitTypes.Protoss_Forge) > 0:
                upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Protoss_Ground_Weapons),
                                          UnitTypes.Protoss_Zealot, 4)
            else:
                upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Protoss_Ground_Weapons),
                                          UnitTypes.Protoss_Zealot, 6)

            # Build anti-sneak-attack cannons on 4 completed units
            # Exception is if the enemy has taken our natural, in which case we skip this
            if _enemy_has_our_natural():
                cherryvis.set_board_value("anti-sneak-attack", "enemy-has-our-natural")
            elif (zealot_count + dragoon_count - in_progress_count) >= 4:
                cherryvis.set_board_value("anti-sneak-attack", "enough-units")
                build_anti_sneak_attack_cannons()
            else:
                cherryvis.set_board_value("anti-sneak-attack", "not-enough-units")
        elif our_strategy == PvZ.OurStrategy.MidGame:
            # TODO: Higher-tech units

            # Baseline production is one combat unit for every 6 workers (approximately 3 units per mining base)
            higher_priority_count = (workers.mineral_workers() // 6) - in_progress_count

            # Compute enemy unit mix
            enemy_lings = units.count_enemy(UnitTypes.Zerg_Zergling)
            enemy_hydras = units.count_enemy(UnitTypes.Zerg_Hydralisk)
            enemy_mutas = (units.count_enemy(UnitTypes.Zerg_Lurker_Egg)
                           + units.count_enemy(UnitTypes.Zerg_Lurker)
                           + units.count_enemy(UnitTypes.Zerg_Mutalisk))

            # Simulate future units if we've seen tech
            if enemy_hydras == 0 and units.count_enemy(UnitTypes.Zerg_Hydralisk_Den) > 0:
                enemy_hydras = 3
            if enemy_mutas == 0 and units.count_enemy(UnitTypes.Zerg_Spire) > 0:
                enemy_mutas = 3

            # Compute the enemy ling percentage, weighted to count mutas a bit higher
            enemy_ling_percentage = 1.0
            if (enemy_lings + enemy_hydras + enemy_mutas) > 0:
                enemy_ling_percentage = fdiv(enemy_lings, enemy_lings + enemy_hydras + enemy_mutas * 2)

            # Now use this to determine our unit mix
            # We prefer zealots more and more as the enemy ling percentage goes up, but still build some goons
            desired_zealot_ratio = enemy_ling_percentage * 0.8

            actual_zealot_ratio = 0.0
            if (zealot_count + dragoon_count) > 0:
                actual_zealot_ratio = zealot_count / (zealot_count + dragoon_count)

            corsairs = desired_corsairs()
            if corsairs > 0:
                to_build = 1
                stargates = 1

                # Boost the count to build at a time if the enemy has mutas
                if corsairs > 2 and units.count_enemy(UnitTypes.Zerg_Mutalisk) > 4:
                    to_build = 2
                    stargates = 2
                    if units.count_completed(UnitTypes.Protoss_Nexus) > 1:
                        to_build = 3

                common_engine.main_army_production(goals, UnitTypes.Protoss_Corsair, to_build, higher_priority_count,
                                                   stargates)
            if actual_zealot_ratio > desired_zealot_ratio:
                common_engine.main_army_production(goals, UnitTypes.Protoss_Dragoon, -1, higher_priority_count)
            common_engine.main_army_production(goals, UnitTypes.Protoss_Zealot, -1, higher_priority_count)

            # Make sure we get weapons upgrade for zealots
            upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Protoss_Ground_Weapons), UnitTypes.Protoss_Zealot,
                                      4)

            PvZ.handle_upgrades(goals)
            build_anti_sneak_attack_cannons()

    def _handle_natural_expansion(self, plays: list[Play], prioritized_production_goals: ProductionGoals) -> None:
        # Hop out if the natural has already been taken
        natural = game_map.get_my_natural()
        if natural is None or natural.owned_since != -1:
            cherryvis.set_board_value("natural", "complete")
            return

        def take() -> None:
            expansions.take_natural_expansion(plays, prioritized_production_goals)

        # If we have a backdoor natural, expand when our third goon is being produced or we have lots of money
        if game_map.map_specific_override().has_backdoor_natural():
            if bwapi.Broodwar.self().minerals() > 450 or units.count_all(UnitTypes.Protoss_Dragoon) > 2:
                cherryvis.set_board_value("natural", "take-backdoor")

                take()
                return

        our_strategy = self.our_strategy
        if our_strategy in (PvZ.OurStrategy.EarlyGameDefense, PvZ.OurStrategy.AntiAllIn,
                            PvZ.OurStrategy.AntiSunkenContain, PvZ.OurStrategy.Defensive):
            # Don't take our natural if the enemy could be rushing or doing an all-in
            cherryvis.set_board_value("natural", "wait-defensive")
        elif our_strategy in (PvZ.OurStrategy.SairSpeedlot, PvZ.OurStrategy.FFEDragoons):
            # Handled by the play
            cherryvis.set_board_value("natural", "forge-expand")
            return
        elif our_strategy == PvZ.OurStrategy.FastExpansion:
            cherryvis.set_board_value("natural", "take-fast-expo")
            take()
            return
        elif our_strategy in (PvZ.OurStrategy.Normal, PvZ.OurStrategy.MidGame):
            # In this case we want to expand when we consider it safe to do so: we have an attacking or containing
            # army that is close to the enemy base
            decision = self._natural_decision_when_attacking(plays, natural)
            if decision == "take":
                take()
                return
            if decision == "keep":
                return

        expansions.cancel_natural_expansion(plays, prioritized_production_goals)

    @staticmethod
    def _natural_decision_when_attacking(plays: list[Play], natural: Base) -> str:
        """For the Normal and MidGame strategies: "take" to take the natural, "keep" to neither take nor cancel it,
        "cancel" otherwise."""
        main_army_play = get_play(plays, AttackEnemyBase)
        if main_army_play is None:
            cherryvis.set_board_value("natural", "no-attack-play")
            return "cancel"

        corsair_count = units.count_completed(UnitTypes.Protoss_Corsair)
        squad = main_army_play.get_squad()
        squad_unit_count = len(squad.get_units())
        if squad_unit_count < 5 or (corsair_count == 0 and squad_unit_count < 8):
            cherryvis.set_board_value("natural", "attack-play-too-small")
            return "cancel"

        vanguard_cluster, dist = squad.vanguard_cluster_and_distance()
        if vanguard_cluster is None:
            cherryvis.set_board_value("natural", "no-vanguard-cluster")
            return "cancel"

        # Cluster should be past our own natural
        natural_dist = path_finding.get_ground_distance(natural.get_position(), main_army_play.base.get_position())
        if natural_dist != -1 and dist > (natural_dist - 320):
            cherryvis.set_board_value("natural", "vanguard-cluster-too-close")
            return "cancel"

        # Cluster should not be moving or fleeing
        # In other words, we want the cluster to be in some kind of stable attack or contain state
        if vanguard_cluster.current_activity == Activity.Moving or vanguard_cluster.is_fleeing():
            # We don't cancel a queued expansion in this case
            cherryvis.set_board_value("natural", "vanguard-cluster-not-attacking")
            return "keep"

        cherryvis.set_board_value("natural", "take")
        return "take"

    @staticmethod
    def handle_upgrades(prioritized_production_goals: ProductionGoals) -> None:
        goals = prioritized_production_goals
        me = bwapi.Broodwar.self()

        # Basic infantry skill upgrades are queued when we have enough of them and are still building them
        upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Leg_Enhancements), UnitTypes.Protoss_Zealot, 5)
        upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Singularity_Charge), UnitTypes.Protoss_Dragoon, 2)

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

        # Air weapon upgrades
        if not units.is_being_upgraded_or_researched(_upgrade(UpgradeTypes.Protoss_Air_Weapons)):
            # Keep one level ahead of enemy armor when we have 5 corsairs
            # (As in Stardust, this compares against our own level of the Zerg upgrade, which is always 0.)
            if (me.getUpgradeLevel(UpgradeTypes.Protoss_Air_Weapons)
                    <= me.getUpgradeLevel(UpgradeTypes.Zerg_Flyer_Carapace)):
                upgrades.upgrade_at_count(goals, _upgrade(UpgradeTypes.Protoss_Air_Weapons),
                                          UnitTypes.Protoss_Corsair, 5)

    def _handle_detection(self, prioritized_production_goals: ProductionGoals) -> None:
        # The main army play will reactively request mobile detection when it sees a cloaked enemy unit
        # The logic here is to look ahead to make sure we already have detection available when we need it

        # Break out if we already have an observer
        if (units.count_completed(UnitTypes.Protoss_Observer) > 0
                or units.count_incomplete(UnitTypes.Protoss_Observer) > 0):
            return

        enemy_has_lurker_tech = (units.has_enemy_built(UnitTypes.Zerg_Lurker_Egg)
                                 or units.has_enemy_built(UnitTypes.Zerg_Lurker)
                                 or players.has_researched(bwapi.Broodwar.enemy(), TechTypes.Lurker_Aspect))

        def build_observer() -> None:
            add_goal(prioritized_production_goals, PRIORITY_NORMAL,
                     UnitProductionGoal("SE", UnitTypes.Protoss_Observer, 1, 1))

        # If we are doing an FFE, only build an observer if we have seen lurkers
        if self._is_ffe():
            if enemy_has_lurker_tech:
                build_observer()
            return

        # If the opponent has done a lurker rush recently, build a cannon at the choke to protect our main
        lurker_in_main = opponent.min_value_in_previous_games("firstLurkerAtOurMain", INT_MAX, 20, 0)
        if lurker_in_main < 12000:
            defensive_cannons.build_defensive_cannons(prioritized_production_goals, True, lurker_in_main - 500, 2)

        # Build an observer when we are on two gas or the enemy has lurker tech
        if (units.count_completed(UnitTypes.Protoss_Assimilator) > 1
                or (units.count_completed(UnitTypes.Protoss_Nexus) > 1 and common.current_frame > 10000)
                or enemy_has_lurker_tech):
            build_observer()

    def _is_ffe(self) -> bool:
        """Whether we are executing a FFE strategy."""
        if self.our_strategy in (PvZ.OurStrategy.SairSpeedlot, PvZ.OurStrategy.FFEDragoons):
            return True

        return defensive_cannons.has_cannon_at_wall()

"""Port of Strategist/StrategyEngines/MapSpecific/PlasmaStrategyEngine.{h,cpp}: the strategy engine used on Plasma,
regardless of the enemy race."""

from __future__ import annotations

from enum import Enum

from bwapi import UnitTypes, UpgradeTypes
from stardust import config, opponent
from stardust.builder import builder, building_placement
from stardust.builder.block import Location
from stardust.builder.building_placement import BuildLocation
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_BASEDEFENSE, PRIORITY_DEPOTS, PRIORITY_EMERGENCY, PRIORITY_MAINARMY, \
    PRIORITY_NORMAL, MineralReservations, Play, ProductionGoals, add_goal
from stardust.strategist.plays.macro.saturate_bases import SaturateBases
from stardust.strategist.plays.main_army.attack_enemy_base import AttackEnemyBase
from stardust.strategist.plays.main_army.defend_my_main import DefendMyMain
from stardust.strategist.plays.scouting.early_game_worker_scout import EarlyGameWorkerScout
from stardust.strategist.strategy_engine import StrategyEngine, get_main_army_play
from stardust.strategist.strategy_engines.common import attack_plays, expansions, upgrades
from stardust.strategist.strategy_engines.common import strategy_engine as common_engine
from stardust.units import units
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType


class PlasmaStrategyEngine(StrategyEngine):
    class EnemyStrategy(Enum):
        Unknown = "Unknown"
        ProxyRush = "ProxyRush"  # Detected by seeing fewer buildings than expected in main
        Normal = "Normal"  # Final state when we are satisfied the enemy is not doing a rush

    def __init__(self) -> None:
        self.enemy_strategy = PlasmaStrategyEngine.EnemyStrategy.Unknown

    def get_enemy_strategy(self) -> str:
        return self.enemy_strategy.value

    def get_our_strategy(self) -> str:
        return "Plasma"

    def recognize_enemy_strategy(self) -> PlasmaStrategyEngine.EnemyStrategy:
        from stardust.strategist.strategy_engines.map_specific import plasma_enemy_strategy_recognizer

        return plasma_enemy_strategy_recognizer.recognize_enemy_strategy(self)

    def initialize(self, plays: list[Play], transitioning_from_random: bool, opening_override: str) -> None:
        if transitioning_from_random:
            return

        plays.append(SaturateBases())
        plays.append(EarlyGameWorkerScout())
        plays.append(DefendMyMain())

    def update_plays(self, plays: list[Play]) -> None:
        new_enemy_strategy = self.recognize_enemy_strategy()

        if self.enemy_strategy != new_enemy_strategy:
            message = (f"Enemy strategy changed from {self.enemy_strategy.value} to "
                       f"{new_enemy_strategy.value}")
            if config.LOGGING_ENABLED:
                log.get(message)
            if config.CHERRYVIS_ENABLED:
                cherryvis.log(message)

            self.enemy_strategy = new_enemy_strategy

        # On Plasma we don't currently use scouting information since we expect enemies to do weird stuff
        # So recall the worker scout as soon as we know the enemy main and race
        if game_map.get_enemy_starting_main() is not None and not opponent.is_unknown_race():
            for play in plays:
                if isinstance(play, EarlyGameWorkerScout):
                    play.status.complete = True
                    break

        # Determine whether to defend our main
        defend_our_main = (common_engine.has_enemy_stolen_our_gas()
                           or self.enemy_strategy in (PlasmaStrategyEngine.EnemyStrategy.Unknown,
                                                      PlasmaStrategyEngine.EnemyStrategy.ProxyRush))

        attack_plays.update_attack_plays(plays, defend_our_main)
        common_engine.update_defend_base_plays(plays)
        common_engine.update_special_teams_plays(plays)
        expansions.default_expansions(plays)
        common_engine.scout_expos(plays, 15000)

    def update_production(self, plays: list[Play], prioritized_production_goals: ProductionGoals,
                          mineral_reservations: MineralReservations) -> None:
        goals = prioritized_production_goals

        common_engine.reserve_minerals_for_expansion(mineral_reservations)
        PlasmaStrategyEngine._handle_natural_expansion(plays, goals)

        main_army_play = get_main_army_play(plays)
        completed_units = main_army_play.get_squad().get_unit_count_by_type() if main_army_play is not None else {}
        incomplete_units = main_army_play.assigned_incomplete_units if main_army_play is not None else {}
        zealot_count = (completed_units.get(UnitTypes.Protoss_Zealot, 0)
                        + incomplete_units.get(UnitTypes.Protoss_Zealot, 0))

        if self.enemy_strategy == PlasmaStrategyEngine.EnemyStrategy.Unknown:
            # Get two zealots before goons
            if zealot_count < 2:
                add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, 2 - zealot_count,
                                                                      2))

            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Dragoon, -1, -1))
        elif self.enemy_strategy == PlasmaStrategyEngine.EnemyStrategy.ProxyRush:
            # Get four zealots before starting the dragoon transition
            dragoon_count = (completed_units.get(UnitTypes.Protoss_Dragoon, 0)
                             + incomplete_units.get(UnitTypes.Protoss_Dragoon, 0))

            zealots_required = 4 - zealot_count

            # Get two zealots at highest priority
            if zealot_count < 2:
                add_goal(goals, PRIORITY_EMERGENCY,
                         UnitProductionGoal("SE-antirush", UnitTypes.Protoss_Zealot, 2 - zealot_count, 2))
                zealots_required -= 2 - zealot_count

            if zealots_required > 0:
                add_goal(goals, PRIORITY_BASEDEFENSE,
                         UnitProductionGoal("SE-antirush", UnitTypes.Protoss_Zealot, zealots_required, -1))

            # If the dragoon transition is just beginning, only order one so we keep producing zealots
            add_goal(goals, PRIORITY_MAINARMY,
                     UnitProductionGoal("SE", UnitTypes.Protoss_Dragoon, 1 if dragoon_count == 0 else -1, -1))

            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, -1, -1))
        elif self.enemy_strategy == PlasmaStrategyEngine.EnemyStrategy.Normal:
            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Dragoon, -1, -1))
            add_goal(goals, PRIORITY_MAINARMY, UnitProductionGoal("SE", UnitTypes.Protoss_Zealot, -1, -1))

        # Basic infantry skill upgrades are queued when we have enough of them and are still building them
        upgrades.upgrade_at_count(goals, UpgradeOrTechType.of(UpgradeTypes.Leg_Enhancements),
                                  UnitTypes.Protoss_Zealot, 6)
        upgrades.upgrade_at_count(goals, UpgradeOrTechType.of(UpgradeTypes.Singularity_Charge),
                                  UnitTypes.Protoss_Dragoon, 2)

        # Cases where we want the upgrade as soon as we start building one of the units
        upgrades.upgrade_when_unit_created(goals, UpgradeOrTechType.of(UpgradeTypes.Gravitic_Boosters),
                                           UnitTypes.Protoss_Observer)
        upgrades.upgrade_when_unit_created(goals, UpgradeOrTechType.of(UpgradeTypes.Gravitic_Drive),
                                           UnitTypes.Protoss_Shuttle, False, True)
        upgrades.upgrade_when_unit_created(goals, UpgradeOrTechType.of(UpgradeTypes.Carrier_Capacity),
                                           UnitTypes.Protoss_Carrier, True)

        upgrades.default_ground_upgrades(goals)

        # Get an observer when on 2 or more gas
        if (units.count_completed(UnitTypes.Protoss_Assimilator) > 1
                and units.count_completed(UnitTypes.Protoss_Observer) == 0
                and units.count_incomplete(UnitTypes.Protoss_Observer) == 0):
            add_goal(goals, PRIORITY_NORMAL, UnitProductionGoal("SE-2+gas", UnitTypes.Protoss_Observer, 1, 1))

    @staticmethod
    def _handle_natural_expansion(plays: list[Play], prioritized_production_goals: ProductionGoals) -> None:
        # Hop out if the natural has already been (or is being) taken
        natural = game_map.get_my_natural()
        if natural is None or natural.owned_since != -1:
            return
        if builder.is_pending_here(natural.get_tile_position()):
            return

        # Expand as soon as our army is attacking
        main_army_play = get_main_army_play(plays)
        if main_army_play is None or type(main_army_play) is not AttackEnemyBase:
            return

        my_main = game_map.get_my_main()
        assert my_main is not None
        build_location = BuildLocation(Location(natural.get_tile_position()),
                                       building_placement.builder_frames(my_main.mineral_line_center,
                                                                         natural.get_tile_position(),
                                                                         UnitTypes.Protoss_Nexus),
                                       0, 0)
        add_goal(prioritized_production_goals, PRIORITY_DEPOTS,
                 UnitProductionGoal.at("SE-natural", UnitTypes.Protoss_Nexus, build_location))

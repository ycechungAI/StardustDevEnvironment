"""Port of Strategist/StrategyEngines/Common/StrategyEngine.cpp: production and play helpers shared by the strategy
engines (static methods of StrategyEngine in Stardust).

Parameters passed by reference to be updated (zealotCount, highPriorityCount) are returned instead.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import UnitType, UnitTypes, UpgradeTypes, WalkPosition
from stardust import common, config
from stardust.builder import builder
from stardust.cpp import INT_MAX
from stardust.general.unit_cluster import combat_sim
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.producer.production_goals.upgrade_production_goal import UpgradeProductionGoal
from stardust.strategist.play import PRIORITY_BASEDEFENSE, PRIORITY_EMERGENCY, PRIORITY_MAINARMY, \
    PRIORITY_MAINARMYBASEPRODUCTION, MineralReservations, Play, ProductionGoals, add_goal
from stardust.strategist.plays.defensive.defend_base import DefendBase
from stardust.strategist.plays.macro.cull_army import CullArmy
from stardust.strategist.plays.main_army.attack_enemy_base import AttackEnemyBase
from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay
from stardust.strategist.plays.main_army.mop_up import MopUp
from stardust.strategist.plays.scouting.scout_enemy_expos import ScoutEnemyExpos
from stardust.strategist.plays.special_teams.dark_templar_harass import DarkTemplarHarass
from stardust.strategist.plays.special_teams.shuttle_harass import ShuttleHarass
from stardust.strategist.strategy_engine import before_play_index, get_main_army_play, get_play
from stardust.strategist.strategy_engines.common import upgrades
from stardust.units import units
from stardust.util import unit_util
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType

if TYPE_CHECKING:
    from stardust.map.base import Base

_CVIS_BOARD_VALUES = config.INSTRUMENTATION_ENABLED


def has_enemy_stolen_our_gas() -> bool:
    my_main = game_map.get_my_main()
    assert my_main is not None
    return any(geyser_or_refinery.refinery is not None
               and geyser_or_refinery.refinery.player != bwapi.Broodwar.self()
               for geyser_or_refinery in my_main.geysers_or_refineries())


def handle_gas_steal_production(prioritized_production_goals: ProductionGoals, zealot_count: int) -> int:
    """Ensures we have a zealot to kill a gas steal; returns the updated zealot count."""
    # Hop out now if we know it isn't a gas steal
    if not has_enemy_stolen_our_gas() or zealot_count > 0:
        return zealot_count

    # Ensure we have a zealot
    add_goal(prioritized_production_goals, PRIORITY_EMERGENCY,
             UnitProductionGoal("SE-gassteal", UnitTypes.Protoss_Zealot, 1, 1))
    return 1


def handle_anti_rush_production(prioritized_production_goals: ProductionGoals, dragoon_count: int,
                                zealot_count: int, zealots_required: int, zealot_producer_limit: int = 2) -> None:
    me = bwapi.Broodwar.self()

    # Cancel tech buildings we might have started unless we have an army
    if (dragoon_count + zealot_count) < 5:
        to_cancel = []
        for building in builder.all_pending_buildings():
            if building.type in (UnitTypes.Protoss_Stargate, UnitTypes.Protoss_Citadel_of_Adun,
                                 UnitTypes.Protoss_Templar_Archives):
                log.get(f"Cancelling {building.type}@{building.tile} because of recognized rush")
                to_cancel.append(building.tile)
        for tile in to_cancel:
            builder.cancel(tile)

    # Get two zealots at highest priority
    if zealots_required > 0 and (dragoon_count + zealot_count) < 2:
        add_goal(prioritized_production_goals, PRIORITY_EMERGENCY,
                 UnitProductionGoal("SE-antirush", UnitTypes.Protoss_Zealot, -1, zealot_producer_limit))

        # Cancel a building nexus (we don't want to fast expand)
        for nexus in builder.pending_buildings_of_type(UnitTypes.Protoss_Nexus):
            log.get(f"Cancelling {nexus.type}@{nexus.tile} because of recognized rush")
            builder.cancel(nexus.tile)

        # Cancel a building cybernetics core unless it is close to being finished or we don't need the minerals
        # This handles cases where we queue the core shortly before scouting the rush
        for core in builder.pending_buildings_of_type(UnitTypes.Protoss_Cybernetics_Core):
            if not core.is_construction_started() or (me.minerals() < 150
                                                      and core.expected_frames_until_completion() > 1000):
                log.get(f"Cancelling {core.type}@{core.tile} because of recognized rush")
                builder.cancel(core.tile)

        cancel_training_units(prioritized_production_goals, UnitTypes.Protoss_Dragoon,
                              2 - (dragoon_count + zealot_count), UnitTypes.Protoss_Zealot.buildTime())
    elif zealots_required > 0:
        percent_zealots_required = zealots_required / (zealot_count + zealots_required)

        # If we are supply-blocked, cancel a non-started cybernetics core
        # Otherwise start queueing dragoons if we are ready to start transitioning
        if (me.supplyTotal() - me.supplyUsed()) < 4 and units.count_incomplete(UnitTypes.Protoss_Pylon) == 0:
            for core in builder.pending_buildings_of_type(UnitTypes.Protoss_Cybernetics_Core):
                if not core.is_construction_started():
                    log.get(f"Cancelling {core.type}@{core.tile} because of upcoming supply block")
                    builder.cancel(core.tile)
        elif percent_zealots_required < 0.2 and dragoon_count == 0:
            add_goal(prioritized_production_goals, PRIORITY_BASEDEFENSE,
                     UnitProductionGoal("SE-antirush", UnitTypes.Protoss_Dragoon, 1, 1))

        if percent_zealots_required > 0.4:
            cancel_training_units(prioritized_production_goals, UnitTypes.Protoss_Dragoon, zealots_required,
                                  UnitTypes.Protoss_Zealot.buildTime())

        add_goal(prioritized_production_goals, PRIORITY_BASEDEFENSE,
                 UnitProductionGoal("SE-antirush", UnitTypes.Protoss_Zealot, -1 if zealots_required > 1 else 1, -1))
    elif dragoon_count < 2:
        add_goal(prioritized_production_goals, PRIORITY_BASEDEFENSE,
                 UnitProductionGoal("SE-antirush", UnitTypes.Protoss_Dragoon, 2, 2))
        return

    # End with dragoons
    add_goal(prioritized_production_goals, PRIORITY_MAINARMY,
             UnitProductionGoal("SE-antirush", UnitTypes.Protoss_Dragoon, -1, -1))

    # Upgrade goon range at 2 dragoons unless we are still behind in zealots
    if zealots_required <= 0 or zealot_count > 4 or dragoon_count > 10:
        upgrades.upgrade_at_count(prioritized_production_goals, UpgradeOrTechType.of(UpgradeTypes.Singularity_Charge),
                                  UnitTypes.Protoss_Dragoon, 2)


def handle_island_expansion_production(plays: list[Play], prioritized_production_goals: ProductionGoals) -> bool:
    me = bwapi.Broodwar.self()

    def set_culling(required_supply: int) -> None:
        if _CVIS_BOARD_VALUES:
            cherryvis.set_board_value("cull", str(max(0, required_supply)))

        # If we already have a cull army play, update the required supply
        # If it goes zero or negative, the play will complete itself
        cull_army = get_play(plays, CullArmy)
        if cull_army is not None:
            cull_army.supply_needed = required_supply
            return

        # Nothing to do if we don't need supply
        if required_supply <= 0:
            return

        # Create the play
        plays.insert(0, CullArmy(required_supply))

    # Only relevant when the main army is in mop-up mode
    if get_play(plays, MopUp) is None:
        set_culling(0)
        return False

    # Only relevant when the enemy has at least one island base
    if not any(base.island for base in game_map.get_enemy_bases()):
        set_culling(0)
        return False

    # Produce up to 10 carriers with relevant upgrades
    required_carriers = 10 - units.count_all(UnitTypes.Protoss_Carrier)
    weapon_level = me.getUpgradeLevel(UpgradeTypes.Protoss_Air_Weapons)
    if weapon_level < 3:
        add_goal(prioritized_production_goals, PRIORITY_MAINARMYBASEPRODUCTION,
                 UpgradeProductionGoal("SE-islandexpo", UpgradeOrTechType.of(UpgradeTypes.Protoss_Air_Weapons),
                                       weapon_level + 1, 1))
    if required_carriers > 0:
        add_goal(prioritized_production_goals, PRIORITY_MAINARMYBASEPRODUCTION,
                 UnitProductionGoal("SE-islandexpo", UnitTypes.Protoss_Carrier, required_carriers, 4))
        upgrades.upgrade_at_count(prioritized_production_goals, UpgradeOrTechType.of(UpgradeTypes.Carrier_Capacity),
                                  UnitTypes.Protoss_Carrier, 0)

        # Cull our main army if we need the supply
        set_culling(required_carriers * UnitTypes.Protoss_Carrier.supplyRequired()  # Supply needed by the carriers
                    - (400 - me.supplyUsed()))  # Supply room
    else:
        set_culling(0)

    return True


def cancel_training_units(prioritized_production_goals: ProductionGoals, unit_type: UnitType,
                          required_capacity: int = INT_MAX, remaining_training_time_threshold: int = 0) -> None:
    """Cancels units being trained to free up production capacity.

    Disabled in Stardust, as this can cause crashes because of BWAPI issue https://github.com/bwapi/bwapi/issues/864,
    so the implementation is not ported."""
    return


def one_gate_core_opening(prioritized_production_goals: ProductionGoals, dragoon_count: int, zealot_count: int,
                          desired_zealots: int) -> None:
    # If we don't want any zealots and have one less than half complete, cancel it
    if desired_zealots == 0 and units.count_incomplete(UnitTypes.Protoss_Zealot) > 0:
        cancel_training_units(prioritized_production_goals, UnitTypes.Protoss_Zealot, INT_MAX,
                              unit_util.build_time(UnitTypes.Protoss_Zealot) // 2)

    # If our core is done or we want no zealots just return dragoons
    if desired_zealots <= zealot_count or units.count_completed(UnitTypes.Protoss_Cybernetics_Core) > 0:
        add_goal(prioritized_production_goals, PRIORITY_MAINARMY,
                 UnitProductionGoal("SE-1gc", UnitTypes.Protoss_Dragoon, -1, -1))
        return

    if dragoon_count == 0:
        add_goal(prioritized_production_goals, PRIORITY_MAINARMY,
                 UnitProductionGoal("SE-1gc", UnitTypes.Protoss_Zealot, 1, 1))
        desired_zealots -= 1

        add_goal(prioritized_production_goals, PRIORITY_MAINARMY,
                 UnitProductionGoal("SE-1gc", UnitTypes.Protoss_Dragoon, 1, 1))

    if zealot_count < desired_zealots:
        add_goal(prioritized_production_goals, PRIORITY_MAINARMY,
                 UnitProductionGoal("SE-1gc", UnitTypes.Protoss_Zealot, desired_zealots - zealot_count, 1))

    add_goal(prioritized_production_goals, PRIORITY_MAINARMY,
             UnitProductionGoal("SE-1gc", UnitTypes.Protoss_Dragoon, -1, -1))


def main_army_production(prioritized_production_goals: ProductionGoals, unit_type: UnitType, count: int,
                         high_priority_count: int, producer_limit: int = -1) -> int:
    """Orders main army production, the first high_priority_count at a higher priority; returns the remaining
    high-priority count."""
    if count == -1:
        if high_priority_count > 0:
            add_goal(prioritized_production_goals, PRIORITY_MAINARMYBASEPRODUCTION,
                     UnitProductionGoal("SE-ma-base", unit_type, high_priority_count, producer_limit))
            high_priority_count = 0
        add_goal(prioritized_production_goals, PRIORITY_MAINARMY,
                 UnitProductionGoal("SE-ma", unit_type, -1, producer_limit))
        return high_priority_count

    if high_priority_count > 0:
        produce_at_high_priority = min(high_priority_count, count)

        add_goal(prioritized_production_goals, PRIORITY_MAINARMYBASEPRODUCTION,
                 UnitProductionGoal("SE-ma-base", unit_type, produce_at_high_priority, producer_limit))

        high_priority_count -= produce_at_high_priority
        count -= produce_at_high_priority

    if count > 0:
        add_goal(prioritized_production_goals, PRIORITY_MAINARMY,
                 UnitProductionGoal("SE-ma", unit_type, count, producer_limit))

    return high_priority_count


def update_defend_base_plays(plays: list[Play]) -> None:
    main_army_play = get_main_army_play(plays)
    my_main = game_map.get_my_main()
    my_natural = game_map.get_my_natural()

    # First gather the list of bases we want to defend
    bases_to_defend: dict[Base, int] = {}

    # Don't defend any bases if our main army play is defending our main
    if main_army_play is not None and not main_army_play.is_defensive():
        for base in game_map.get_my_bases():
            is_main_or_natural_in_early_game = (base is my_main or base is my_natural) and common.current_frame < 20000

            # Don't defend our main or natural with a DefendBase play if our main army is close to it
            if is_main_or_natural_in_early_game:
                if type(main_army_play) is AttackEnemyBase:
                    vanguard = main_army_play.get_squad().vanguard_cluster()
                    if vanguard is not None:
                        vanguard_dist = path_finding.get_ground_distance(vanguard.vanguard.last_position,
                                                                         base.get_position(),
                                                                         UnitTypes.Protoss_Zealot)
                        if vanguard_dist != -1 and vanguard_dist < 1500:
                            continue
            elif base.mineral_patch_count() < 3:
                continue

            # Gather the enemy units threatening the base
            enemy_value = sum(combat_sim.unit_value(unit) for unit in units.enemy_at_base(base))

            # If too many enemies are threatening the base, don't bother trying to defend it, unless it is our main or
            # natural in the early game
            if (not is_main_or_natural_in_early_game
                    and enemy_value > 5 * combat_sim.unit_value(UnitTypes.Protoss_Dragoon)):
                continue

            bases_to_defend[base] = enemy_value

    # Scan the plays and remove those that are no longer relevant
    for play in plays:
        if not isinstance(play, DefendBase):
            continue

        # If we no longer need to defend this base, remove the play
        if play.base not in bases_to_defend:
            play.status.complete = True
            continue

        play.enemy_value = bases_to_defend.pop(play.base)

    # Add missing plays
    for base, enemy_value in bases_to_defend.items():
        plays.insert(0, DefendBase(base, enemy_value))
        cherryvis.log(f"Added defend base play for base @ {WalkPosition(base.get_position())}")


def update_special_teams_plays(plays: list[Play]) -> None:
    # Ensure we have a DarkTemplarHarass play if we have any DTs
    dark_templar_harass_play = get_play(plays, DarkTemplarHarass)
    have_dark_templar = units.count_all(UnitTypes.Protoss_Dark_Templar) > 0
    if dark_templar_harass_play is not None and not have_dark_templar:
        dark_templar_harass_play.status.complete = True
    elif have_dark_templar and dark_templar_harass_play is None:
        plays.insert(before_play_index(plays, MainArmyPlay), DarkTemplarHarass())

    # Ensure we have a ShuttleHarass play if we have any shuttles
    shuttle_harass_play = get_play(plays, ShuttleHarass)
    have_shuttles = units.count_all(UnitTypes.Protoss_Shuttle) > 0
    if shuttle_harass_play is not None and not have_shuttles:
        shuttle_harass_play.status.complete = True
    elif have_shuttles and shuttle_harass_play is None:
        plays.insert(before_play_index(plays, MainArmyPlay), ShuttleHarass())


def scout_expos(plays: list[Play], starting_frame: int) -> None:
    if common.current_frame < starting_frame:
        return
    if get_play(plays, ScoutEnemyExpos) is not None:
        return

    plays.append(ScoutEnemyExpos())


def reserve_minerals_for_expansion(mineral_reservations: MineralReservations) -> None:
    # The idea here is to make sure we keep enough resources for an expansion if the total minerals left at our bases
    # is low
    total_minerals = sum(base.minerals for base in game_map.get_my_bases())
    if total_minerals < 800:
        mineral_reservations.append((400, 0))

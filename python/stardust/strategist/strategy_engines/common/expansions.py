"""Port of Strategist/StrategyEngines/Common/Expansions.cpp: expansion decisions shared by the strategy engines."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Races, UnitTypes, WalkPosition
from stardust import common, config
from stardust.builder import builder, building_placement
from stardust.builder.block import Location
from stardust.builder.building_placement import BuildLocation
from stardust.cpp import INT_MAX
from stardust.general.unit_cluster import combat_sim
from stardust.general.unit_cluster.unit_cluster import Activity
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_DEPOTS, Play, ProductionGoals, add_goal
from stardust.strategist.plays.macro.take_expansion import TakeExpansion
from stardust.strategist.plays.macro.take_island_expansion import TakeIslandExpansion
from stardust.strategist.plays.main_army.attack_enemy_base import AttackEnemyBase
from stardust.strategist.plays.main_army.mop_up import MopUp
from stardust.strategist.plays.special_teams.elevator import Elevator
from stardust.strategist.strategy_engine import before_play_index, get_main_army_play
from stardust.units import units
from stardust.util import unit_util
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.map.base import Base


def _my_main() -> Base:
    my_main = game_map.get_my_main()
    assert my_main is not None
    return my_main


def _add_island_expansion_play(plays: list[Play], base: Base, can_cancel: bool = True,
                               transfer_workers: bool = True) -> TakeIslandExpansion:
    play = TakeIslandExpansion(base, can_cancel, transfer_workers)

    # Taking an island base is always lower priority than an elevator
    index = before_play_index(plays, Elevator)
    plays.insert(0 if index == len(plays) else index + 1, play)

    return play


def _closest_untaken_island_base() -> tuple[Base | None, int]:
    closest_island_base: Base | None = None
    closest_island_base_dist = INT_MAX
    main_position = _my_main().get_position()
    for island_base in game_map.get_untaken_island_expansions():
        dist = main_position.getApproxDistance(island_base.get_position())
        if dist < closest_island_base_dist:
            closest_island_base = island_base
            closest_island_base_dist = dist
    return closest_island_base, closest_island_base_dist


def default_expansions(plays: list[Play]) -> None:
    import stardust.strategist.strategist as strategist

    me = bwapi.Broodwar.self()

    # This logic does not handle the first decision to take our natural expansion, so if this hasn't been done, bail
    # out now
    natural = game_map.get_my_natural()
    if natural is not None and ((natural.owned_since == -1 and len(game_map.get_my_bases()) == 1)
                                or (natural.resource_depot is not None and not natural.resource_depot.completed)):
        return

    # If the natural is "owned" by our opponent, it's probably because of some kind of proxy play
    # In this case, delay doing any expansions until mid-game
    if natural is not None and natural.owner != me and common.current_frame < 12000:
        return

    # Collect any existing TakeExpansion plays
    take_expansion_plays: list[TakeExpansion] = []
    take_island_expansion_plays: list[TakeIslandExpansion] = []
    for play in plays:
        if isinstance(play, TakeIslandExpansion):
            take_island_expansion_plays.append(play)
        elif isinstance(play, TakeExpansion):
            take_expansion_plays.append(play)

    def safe_to_island_expand() -> bool:
        """Whether we consider it safe to expand to an island."""
        # Only expand when our army is on the offensive
        main_army_play = get_main_army_play(plays)
        if main_army_play is None:
            return False
        if type(main_army_play) is MopUp:
            return True
        if type(main_army_play) is not AttackEnemyBase:
            return False

        # Ensure the vanguard cluster is at least 20 tiles away from our natural, or main if map has a backdoor natural
        vanguard_cluster = main_army_play.get_squad().vanguard_cluster()
        if vanguard_cluster is None:
            return False

        base = _my_main() if game_map.map_specific_override().has_backdoor_natural() else game_map.get_my_natural()
        if base is not None:
            natural_dist = path_finding.get_ground_distance(base.get_position(),
                                                            vanguard_cluster.vanguard.last_position)
            if natural_dist != -1 and natural_dist < 640:
                return False

        return True

    # Check if we want to cancel an active TakeIslandExpansionPlay
    if take_island_expansion_plays:
        if not safe_to_island_expand():
            for take_island_expansion_play in take_island_expansion_plays:
                if take_island_expansion_play.cancellable():
                    take_island_expansion_play.status.complete = True
                    log.get(f"Cancelled island expansion to {take_island_expansion_play.depot_position}")

        # For most island expansions, we would return here and leave just the island expo play enabled
        # But if we are taking an island expansion with a blocking neutral that takes a while to clear, drop through
        # and allow normal expansions
        if take_island_expansion_plays[0].frames_to_clear_blocker() < 750:
            return

    # Determine whether we want to expand now
    gas_starved = me.minerals() > 1500 and me.gas() < 500
    excess_mineral_assignments = 0
    if not gas_starved:
        # Expand if we have no bases with more than 3 available mineral assignments
        # Adjust by the number of pending expansions to avoid instability when a build worker is reserved for the
        # expansion
        available_mineral_assignments_threshold = 3 + len(take_expansion_plays)

        # If the enemy is contained, boost the threshold
        # This is for now only applied to terran, since we can lose games against protoss and zerg by overexpanding
        if bwapi.Broodwar.enemy().getRace() == Races.Terran and strategist.is_enemy_contained():
            available_mineral_assignments_threshold += 3

        for base in game_map.get_my_bases():
            excess_mineral_assignments = max(excess_mineral_assignments,
                                             workers.available_mineral_assignments(base)
                                             - available_mineral_assignments_threshold)

    reason_unsafe = ""

    def safe_to_expand() -> bool:
        """Whether we consider it safe to expand to a normal expansion."""
        nonlocal reason_unsafe

        # Only expand when our army is on the offensive
        main_army_play = get_main_army_play(plays)
        if main_army_play is None:
            reason_unsafe = "No main army play"
            return False
        if type(main_army_play) is MopUp:
            return True
        if type(main_army_play) is not AttackEnemyBase:
            reason_unsafe = "Main army play is defensive"
            return False

        # Get the vanguard cluster
        vanguard_cluster = main_army_play.get_squad().vanguard_cluster()
        if vanguard_cluster is None:
            reason_unsafe = "Main army play squad has no units"
            return False

        # Don't expand if the cluster has fewer than five units
        if len(vanguard_cluster.units) < 5:
            reason_unsafe = "Main army play vanguard cluster has fewer than 5 units"
            return False

        # Expand if we are gas blocked - we have the resources for the nexus anyway
        if me.minerals() > 500 and me.gas() < 100:
            return True

        # Cluster should not be moving or fleeing
        # In other words, we want the cluster to be in some kind of stable attack or contain state
        if vanguard_cluster.current_activity == Activity.Moving or vanguard_cluster.is_fleeing():
            reason_unsafe = "Main army play vanguard cluster is moving or fleeing"
            return False

        return True

    def enemy_combat_value_at_base(base: Base) -> int:
        return sum(combat_sim.unit_value(unit) for unit in units.enemy_at_base(base)
                   if unit.is_transport() or unit_util.can_attack_ground(unit.type))

    excess_idle_workers = workers.idle_worker_count() >= 10
    dragoon_value = combat_sim.unit_value(UnitTypes.Protoss_Dragoon)

    # Check if we want to cancel an active TakeExpansionPlay
    if take_expansion_plays:
        if excess_idle_workers:
            return

        safe = safe_to_expand()
        for take_expansion_play in take_expansion_plays:
            if not take_expansion_play.cancellable():
                continue
            if excess_mineral_assignments > 1:
                take_expansion_play.status.complete = True
                log.get(f"Cancelled expansion to {take_expansion_play.depot_position}: excess mineral assignments is "
                        f"now {excess_mineral_assignments}")
            if not safe:
                take_expansion_play.status.complete = True
                log.get(f"Cancelled expansion to {take_expansion_play.depot_position}: no longer safe: "
                        f"{reason_unsafe}")
            if take_expansion_play.enemy_value > 4 * dragoon_value:
                take_expansion_play.status.complete = True
                log.get(f"Cancelled expansion to {take_expansion_play.depot_position}: enemy combat value at "
                        f"expansion exceeds 4 dragoon equivalent")

        return

    # Take an island expansion in the following cases:
    # - We are on three bases and already have a robo facility - DISABLED
    # - We are contained on one base and it is after frame 16000
    if (not take_island_expansion_plays and excess_mineral_assignments == 0
            and common.current_frame > 16000 and units.count_all(UnitTypes.Protoss_Nexus) == 1
            and strategist.are_we_contained()):
        closest_island_base, closest_island_base_dist = _closest_untaken_island_base()
        if closest_island_base is not None and closest_island_base_dist < 2500 and safe_to_island_expand():
            play = _add_island_expansion_play(plays, closest_island_base)
            log.get(f"Queued island expansion to {play.depot_position}")
            if config.CHERRYVIS_ENABLED:
                cherryvis.log(f"Added TakeIslandExpansion play for base @ {WalkPosition(play.depot_position)}")
            return

    # Break out if we don't want to expand to a normal base
    if excess_mineral_assignments > 0 or (not safe_to_expand() and not excess_idle_workers):
        return

    # Determine if we want to consider a mineral-only base
    def should_take_mineral_only() -> bool:
        if gas_starved:
            return False

        # Take a mineral-only if we have an excess of gas
        if me.gas() > 1500:
            return True

        # Count the number of active gas and mineral-only bases we have
        gas_bases = 0
        mineral_only_bases = 0
        for base in game_map.get_my_bases():
            if base.gas > 0:
                gas_bases += 1
            elif base.minerals > 1000:
                mineral_only_bases += 1

        # Take a mineral-only base for every three gas bases
        return gas_bases - (mineral_only_bases * 3) > 2

    take_mineral_only = should_take_mineral_only()

    def try_expand_to(expansion: Base) -> bool:
        if expansion.owner is not None:
            # Skip bases owned by a different player or where we already have a nexus
            if expansion.owner != me or expansion.resource_depot is not None:
                return False

            # Don't re-take the base if it is almost mined out
            if expansion.minerals < 2000:
                return False

        if not take_mineral_only and expansion.gas == 0:
            return False

        # Don't take expansions that have a blocking neutral
        # We currently only handle this for island expansions
        if expansion.blocking_neutrals:
            return False

        # Don't take expansions that are blocked by the enemy and that we don't know how to unblock
        if expansion.blocked_by_enemy and not building_placement.base_static_defense_locations(expansion).is_valid():
            return False

        enemy_value = enemy_combat_value_at_base(expansion)
        if enemy_value > 4 * dragoon_value:
            return False

        play = TakeExpansion(expansion, enemy_value)
        plays.insert(0, play)

        log.get(f"Queued expansion to {play.depot_position}")
        if config.CHERRYVIS_ENABLED:
            cherryvis.log(f"Added TakeExpansion play for base @ {WalkPosition(play.depot_position)}")

        return True

    # Create a TakeExpansion play for the next expansion
    for base in game_map.get_my_bases():
        if try_expand_to(base):
            return
    for expansion in game_map.get_untaken_expansions():
        if try_expand_to(expansion):
            return


def take_natural_expansion(plays: list[Play], prioritized_production_goals: ProductionGoals) -> None:
    natural = game_map.get_my_natural()
    assert natural is not None

    # If the natural is blocked, use a TakeExpansion play, since it knows how to resolve it
    if natural.blocked_by_enemy:
        has_natural_play = any(isinstance(play, TakeExpansion) and play.depot_position == natural.get_tile_position()
                               for play in plays)

        if not has_natural_play:
            log.get("Added TakeExpansion play for natural to handle blocking enemy unit")
            if config.CHERRYVIS_ENABLED:
                cherryvis.log("Added TakeExpansion play for natural to handle blocking enemy unit")

            plays.insert(0, TakeExpansion(natural, 0))

        return

    # Otherwise just queue the natural nexus as any normal macro item
    build_location = BuildLocation(Location(natural.get_tile_position()),
                                   building_placement.builder_frames(_my_main().mineral_line_center,
                                                                     natural.get_tile_position(),
                                                                     UnitTypes.Protoss_Nexus),
                                   0, 0)
    add_goal(prioritized_production_goals, PRIORITY_DEPOTS,
             UnitProductionGoal.at("SE-natural", UnitTypes.Protoss_Nexus, build_location))


def cancel_natural_expansion(plays: list[Play], prioritized_production_goals: ProductionGoals) -> None:
    natural = game_map.get_my_natural()
    assert natural is not None

    # If we are taking the natural with a TakeExpansion play, don't cancel it
    # TODO: Refactor how plays are cancelled so we can actually do this safely
    for play in plays:
        if isinstance(play, TakeExpansion) and play.depot_position == natural.get_tile_position():
            return

    builder.cancel_base(natural)


def take_expansion_with_shuttle(plays: list[Play]) -> None:
    """Used when the enemy has us contained, so we want to take an expansion as if it were an island expansion."""
    # First abort if we have already queued an expansion
    if any(isinstance(play, TakeIslandExpansion) for play in plays):
        return

    # Now figure out which expansion to take
    transfer_workers = False

    # If the map has an island expansion, take it
    base_to_take, _ = _closest_untaken_island_base()
    if base_to_take is not None:
        transfer_workers = True

    # Otherwise use the hidden base
    if base_to_take is None:
        base_to_take = game_map.get_hidden_base()

    if base_to_take is None:
        return

    # Queue the play
    play = _add_island_expansion_play(plays, base_to_take, False, transfer_workers)
    log.get(f"Queued island expansion to {play.depot_position}")
    if config.CHERRYVIS_ENABLED:
        cherryvis.log(f"Added TakeIslandExpansion play for base @ {WalkPosition(play.depot_position)}")

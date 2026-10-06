"""Port of General/UnitCluster/Tactics/Regroup.cpp: orders a cluster to regroup.

There are several regrouping strategies that can be chosen based on the situation:
- Contain static: The enemy is considered to be mainly static, so retreat to a safe distance and attack anything that
  comes into range.
- Hold choke: We can stay at a choke and hold off the enemy.
- Stand ground: We can stop a safe distance from the enemy until we are reinforced.
- Flee: Move back towards our main base until we are reinforced.
- Attack blocking army: We want to flee, but there is an enemy army in the way, so attack it

The DEBUG_COMBATSIM_LOG messages are omitted (some of them don't compile in Stardust).
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, UnitTypes
from stardust.cpp import INT_MAX
from stardust.general.unit_cluster.unit_cluster import SubActivity, UnitCluster
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.players import players

if TYPE_CHECKING:
    from stardust.general.combat_sim_result import CombatSimResult
    from stardust.general.enemy_army import EnemyArmy
    from stardust.general.unit_cluster.unit_cluster import UnitsAndTargets
    from stardust.units.my_observer import MyObserver
    from stardust.units.unit import Unit


def _should_contain_static_defense(cluster: UnitCluster, units_and_targets: UnitsAndTargets,
                                   enemy_units: set[Unit], detectors: set[MyObserver],
                                   initial_sim_result: CombatSimResult, target_position: Position) -> bool:
    # Run a combat sim excluding enemy static defense
    filtered_units_and_targets: UnitsAndTargets = [
        (my_unit, target if target is not None and not target.is_static_ground_defense() else None)
        for my_unit, target in units_and_targets]
    filtered_enemy_units = {unit for unit in enemy_units if not unit.is_static_ground_defense()}

    sim_result = cluster.run_combat_sim(target_position, filtered_units_and_targets, filtered_enemy_units, detectors,
                                        False, initial_sim_result.narrow_choke)

    return (sim_result.my_percent_lost() <= 0.001
            or (sim_result.value_gain() > 0 and sim_result.percent_gain() > -0.05)
            or sim_result.percent_gain() > 0.2)


def _should_contain_choke(cluster: UnitCluster, units_and_targets: UnitsAndTargets, enemy_units: set[Unit],
                          detectors: set[MyObserver], initial_sim_result: CombatSimResult,
                          target_position: Position) -> bool:
    narrow_choke = initial_sim_result.narrow_choke
    if narrow_choke is None:
        return False

    # Never contain a choke that is covered by enemy static defense at both ends
    grid = players.grid(bwapi.Broodwar.enemy())
    if grid.static_ground_threat(narrow_choke.end1_center) > 0 and grid.static_ground_threat(narrow_choke.end2_center) > 0:
        return False

    sim_result = cluster.run_recorded_combat_sim(cluster.recent_regroup_sim_results, target_position,
                                                 units_and_targets, enemy_units, detectors, False, narrow_choke)

    distance_factor = 1.0

    def attack() -> bool:
        nonlocal distance_factor

        # Always attack if we don't lose anything
        if sim_result.my_percent_lost() <= 0.001:
            return True

        # Attack in cases where we think we will kill 50% more value than we lose
        if (sim_result.value_gain() > (sim_result.initial_mine - sim_result.final_mine) // 2
                and (sim_result.percent_gain() > -0.05 or sim_result.my_percentage_of_total() > 0.9)):
            return True

        # Compute the distance factor, an adjustment based on where our army is relative to our main and the target
        # position
        distance_factor = 1.2 - 0.4 * cluster.percentage_to_enemy_main

        # Give an extra bonus to narrow chokes close to our base
        if sim_result.narrow_choke is not None and cluster.percentage_to_enemy_main < 0.3:
            distance_factor *= 1.2

        # Attack if we expect to end the fight with a sufficiently larger army and aren't losing an unacceptable
        # percentage of it
        if (sim_result.my_percentage_of_total() > (1.0 - 0.45 * distance_factor)
                and sim_result.percent_gain() > -0.05 * distance_factor):
            return True

        # Attack if the percentage gain, adjusted for distance factor, is acceptable
        # A percentage gain here means the enemy loses a larger percentage of their army than we do
        return sim_result.percent_gain() > (1.0 - distance_factor - 0.1)

    sim_result.decision = attack()
    sim_result.distance_factor = distance_factor

    # What we decide depends on the current regroup activity
    sub_activity = cluster.current_sub_activity
    if sub_activity == SubActivity.None_:
        # This is the first regroup frame, so go with the sim
        return sim_result.decision

    if sub_activity == SubActivity.ContainChoke:
        # Continue the contain if the sim recommends it
        if sim_result.decision:
            return True

        consecutive_flee_frames, contain_frames, flee_frames = UnitCluster.consecutive_sim_results(
            cluster.recent_regroup_sim_results, 24)

        # Continue if the sim hasn't been stable for 6 frames
        if consecutive_flee_frames < 6:
            return True

        # Continue if the sim has recommended containing more than fleeing; otherwise abort
        return contain_frames > flee_frames

    if sub_activity in (SubActivity.StandGround, SubActivity.Flee, SubActivity.AttackBlockingArmy):
        if not sim_result.decision:
            return False

        consecutive_contain_frames, contain_frames, flee_frames = UnitCluster.consecutive_sim_results(
            cluster.recent_regroup_sim_results, 24)

        # Continue if the sim hasn't been stable for 6 frames
        if consecutive_contain_frames < 6:
            return False

        # Continue if the sim has recommended fleeing more than containing
        if flee_frames > contain_frames:
            return False

        # Continue if our number of units has increased in the past 60 frames.
        # This gives our reinforcements time to link up with the rest of the cluster before containing.
        for count, recent in enumerate(reversed(cluster.recent_regroup_sim_results)):
            if count >= 60:
                break
            if sim_result.my_unit_count > recent.my_unit_count:
                return False

        # Start the contain
        return True

    # ContainStaticDefense does not consider containing a choke
    return False


def _should_stand_ground(units_and_targets: UnitsAndTargets, initial_sim_result: CombatSimResult,
                         has_valid_target: bool) -> bool:
    # If none of our mobile units has a target, and none of our mobile units are in danger, stand ground
    if not has_valid_target:
        grid = players.grid(bwapi.Broodwar.enemy())
        any_threat = False
        for my_unit, _ in units_and_targets:
            if my_unit.type == UnitTypes.Protoss_Photon_Cannon:
                continue

            threat = (grid.air_threat(my_unit.last_position) if my_unit.is_flying
                      else grid.ground_threat(my_unit.last_position))
            if threat > 0:
                any_threat = True
                break

        if not any_threat:
            return True

    # For now just stand ground if the sim result indicates no damage to our units
    # We may want to make this less strict later
    return initial_sim_result.my_percent_lost() < 0.001


def regroup(cluster: UnitCluster, units_and_targets: UnitsAndTargets, enemy_units: set[Unit],
            detectors: set[MyObserver], sim_result: CombatSimResult, target_position: Position,
            has_valid_target: bool) -> None:
    from stardust.general import general

    def static_defense() -> bool:
        return any(unit.is_static_ground_defense() for unit in enemy_units)

    # We lazily-initialize a blocking enemy army if we are trying to flee
    blocking_enemy_army: EnemyArmy | None = None

    def set_appropriate_fleeing_activity() -> None:
        nonlocal blocking_enemy_army

        my_main = game_map.get_my_main()
        assert my_main is not None

        best_blocking_army_dist = INT_MAX
        for enemy_army in general.get_enemy_armies():
            # Ignore small armies
            if len(enemy_army.units) < 4:
                continue

            # Ignore armies that are a long way away from our cluster
            center_dist = cluster.center.getApproxDistance(enemy_army.center)
            if center_dist > 640:
                continue

            # Find the enemy unit closest to our cluster center
            enemy_vanguard: Unit | None = None
            best_dist = INT_MAX
            for unit in enemy_army.units:
                if not unit.last_position_valid or not unit.last_position_visible:
                    continue
                if unit.needs_detection() and not detectors:
                    continue

                dist = unit.get_distance(cluster.center)
                if dist < best_dist:
                    enemy_vanguard = unit
                    best_dist = dist
            if enemy_vanguard is None:
                continue

            # Army is blocking only if their vanguard is closer to our main than ours
            # A threshold is used that varies depending on whether the enemy unit is in a narrow choke
            threshold = 0 if game_map.is_in_narrow_choke(enemy_vanguard.get_tile_position()) else 64
            if (path_finding.get_ground_distance(cluster.vanguard.last_position, my_main.get_position())
                    < path_finding.get_ground_distance(enemy_vanguard.last_position, my_main.get_position())
                    + threshold):
                continue

            # Replace the current one if this one is closer
            if center_dist < best_blocking_army_dist:
                blocking_enemy_army = enemy_army
                best_blocking_army_dist = center_dist

        if blocking_enemy_army is not None:
            cluster.set_sub_activity(SubActivity.AttackBlockingArmy)
        else:
            cluster.set_sub_activity(SubActivity.Flee)

    def should_contain_static_defense() -> bool:
        return _should_contain_static_defense(cluster, units_and_targets, enemy_units, detectors, sim_result,
                                              target_position)

    def should_contain_choke() -> bool:
        return _should_contain_choke(cluster, units_and_targets, enemy_units, detectors, sim_result, target_position)

    def should_stand_ground() -> bool:
        return _should_stand_ground(units_and_targets, sim_result, has_valid_target)

    # First choose which regrouping mode we want to use
    sub_activity = cluster.current_sub_activity
    if sub_activity == SubActivity.None_:
        if static_defense() and should_contain_static_defense():
            cluster.set_sub_activity(SubActivity.ContainStaticDefense)
        elif should_contain_choke():
            cluster.set_sub_activity(SubActivity.ContainChoke)
        elif should_stand_ground():
            cluster.set_sub_activity(SubActivity.StandGround)
        else:
            set_appropriate_fleeing_activity()
    elif sub_activity == SubActivity.ContainStaticDefense:
        if not should_contain_static_defense():
            set_appropriate_fleeing_activity()
    elif sub_activity == SubActivity.ContainChoke:
        if not should_contain_choke():
            set_appropriate_fleeing_activity()
    elif sub_activity == SubActivity.StandGround:
        # We might be standing ground near a choke that we are now able to contain
        if should_contain_choke():
            cluster.set_sub_activity(SubActivity.ContainChoke)
        elif not should_stand_ground():
            # Flee if it is no longer safe to stand ground
            set_appropriate_fleeing_activity()
    else:
        # Flee or attack blocking army
        # We might be able to start containing static defense after fleeing a bit
        # We might also flee through a choke that we can contain from the other side
        if static_defense() and should_contain_static_defense():
            cluster.set_sub_activity(SubActivity.ContainStaticDefense)
        elif should_contain_choke():
            cluster.set_sub_activity(SubActivity.ContainChoke)
        elif should_stand_ground():
            # While fleeing we will often link up with reinforcements, or the enemy will not pursue, so it makes
            # sense to stand ground instead
            cluster.set_sub_activity(SubActivity.StandGround)
        else:
            set_appropriate_fleeing_activity()

    # Now execute the regrouping activity
    sub_activity = cluster.current_sub_activity
    if sub_activity == SubActivity.None_:
        log.get(f"WARNING: Cluster @ {bwapi.TilePosition(cluster.center)} has no valid regroup subactivity to execute")
    elif sub_activity == SubActivity.ContainStaticDefense:
        cluster.contain_static(enemy_units, target_position)
    elif sub_activity == SubActivity.ContainChoke:
        narrow_choke = sim_result.narrow_choke
        assert narrow_choke is not None
        end1_dist = path_finding.get_ground_distance(target_position, narrow_choke.end1_center,
                                                     UnitTypes.Protoss_Dragoon, PathFindingOptions.UseNearestBWEMArea)
        end2_dist = path_finding.get_ground_distance(target_position, narrow_choke.end2_center,
                                                     UnitTypes.Protoss_Dragoon, PathFindingOptions.UseNearestBWEMArea)
        choke_defend_end = narrow_choke.end2_center if end1_dist < end2_dist else narrow_choke.end1_center
        cluster.hold_choke(narrow_choke, choke_defend_end, units_and_targets)
    elif sub_activity == SubActivity.StandGround:
        cluster.stand_ground(enemy_units, target_position)
    elif sub_activity == SubActivity.Flee:
        cluster.flee(enemy_units)
    elif sub_activity == SubActivity.AttackBlockingArmy:
        # Choose new targets with the enemy army as the target position, then attack
        assert blocking_enemy_army is not None
        new_units_and_targets = cluster.select_targets(enemy_units, blocking_enemy_army.center)
        cluster.attack(new_units_and_targets, blocking_enemy_army.center)
        return

    # Cannons can't retreat, so just have them attack their target
    for my_unit, target in units_and_targets:
        if my_unit.type != UnitTypes.Protoss_Photon_Cannon:
            continue

        if target is not None:
            my_unit.attack_unit(target, units_and_targets, False, cluster.enemy_aoe_radius)

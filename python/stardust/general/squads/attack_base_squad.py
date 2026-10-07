"""Port of General/Squads/AttackBaseSquad.{h,cpp}: attacks an enemy base, deciding per cluster whether to attack or
regroup based on the combat sim.

The DEBUG_COMBATSIM / DEBUG_COMBATSIM_LOG messages are omitted.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from stardust import common
from stardust.general.squad import Squad
from stardust.general.unit_cluster.unit_cluster import Activity, SubActivity, UnitCluster
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.general.combat_sim_result import CombatSimResult
    from stardust.map.base import Base


def _pushed_back_to_natural(cluster: UnitCluster, threshold: int) -> tuple[bool, int]:
    """Whether the vanguard cluster has been pushed back to within the threshold of our natural, and its distance."""
    if not cluster.is_vanguard_cluster:
        return False, 0
    if game_map.map_specific_override().has_backdoor_natural():
        return False, 0

    natural = game_map.get_my_natural()
    if (natural is None or natural.owner != bwapi.Broodwar.self() or natural.resource_depot is None
            or not natural.resource_depot.completed):
        return False, 0

    dist = cluster.center.getApproxDistance(natural.get_position())
    return dist < threshold, dist


def _reinforcement_factor(cluster: UnitCluster, closest_reinforcements: float,
                          reinforcement_percentage: float) -> float:
    # Only adjust for reinforcements when our army is out on the field
    # TODO: Would also be nice to adjust for units in production when waiting to break out from our main
    if cluster.percentage_to_enemy_main < 0.2:
        return 1.0

    # The idea here is to return a lower factor if we have reinforcements arriving imminently, so we wait to start an
    # attack until the reinforcements arrive.

    # For distance, scale the effect from 0 (far away) to 0.5 (close), where "far away" is halfway across the map
    distance_factor = max(0.0, (closest_reinforcements - (cluster.percentage_to_enemy_main / 2.0))
                          / cluster.percentage_to_enemy_main)

    # Reduce it by the reinforcement percentage, as long as it is above 15%
    return (1.0 - distance_factor) * (1.0 - max(0.0, (reinforcement_percentage - 0.15) / 0.85))


def _should_attack(cluster: UnitCluster, sim_result: CombatSimResult, aggression: float = 1.0) -> bool:
    distance_factor = 1.0

    def attack() -> bool:
        nonlocal distance_factor

        # Always attack if we don't lose anything
        if sim_result.my_percent_lost() <= 0.001:
            return True

        # Special cases when the enemy has undetected units
        if sim_result.enemy_has_undetected_units:
            # Always retreat if we are close to the target and can't hurt anything
            # This usually means there are no detected units we can attack
            if cluster.vanguard_dist_to_target < 500 and sim_result.value_gain() <= 0:
                return False

            # Always attack if we are further from the target and aren't losing a large percentage of our army
            # This handles cases where our army would otherwise get pinned in its main by e.g. a single cloaked wraith
            if cluster.vanguard_dist_to_target >= 500 and sim_result.my_percent_lost() <= 0.15:
                return True

        # Attack in cases where we think we will kill 50% more value than we lose
        if (aggression > 0.99 and sim_result.value_gain() > (sim_result.initial_mine - sim_result.final_mine) // 2
                and (sim_result.percent_gain() > -0.05 or sim_result.my_percentage_of_total() > 0.9)):
            return True

        # Compute the distance factor, an adjustment based on where our army is relative to our main and the target
        # position
        distance_factor = 1.2 - 0.4 * cluster.percentage_to_enemy_main

        # Give an extra penalty to narrow chokes close to the enemy base
        if sim_result.narrow_choke is not None and cluster.percentage_to_enemy_main > 0.7:
            distance_factor *= 0.8

        # Attack if we expect to end the fight with a sufficiently larger army and aren't losing an unacceptable
        # percentage of it
        percent_of_total = sim_result.my_percentage_of_total() - (0.9 - 0.35 * distance_factor * aggression)
        if percent_of_total > 0.0 and sim_result.percent_gain() > (-0.05 * distance_factor * aggression
                                                                   - percent_of_total):
            return True

        # Attack if the percentage gain, adjusted for aggression and distance factor, is acceptable
        # A percentage gain here means the enemy loses a larger percentage of their army than we do
        return sim_result.percent_gain() > (0.2 / (aggression * distance_factor))

    result = attack()

    sim_result.distance_factor = distance_factor
    sim_result.aggression = aggression

    return result


def _natural_aggression(cluster: UnitCluster, aggression: float) -> float:
    """Increase aggression if we are trying to fight our way out of our natural."""
    pushed_back, natural_dist = _pushed_back_to_natural(cluster, 800)
    if pushed_back:
        # Scales linearly from 1.0 at 1000 distance to 2.0 at <500 distance
        aggression *= 1.0 + (1000.0 - max(float(natural_dist), 500.0)) / 500.0
    return aggression


def _should_start_attack(cluster: UnitCluster, sim_result: CombatSimResult, closest_reinforcements: float,
                         reinforcement_percentage: float) -> bool:
    aggression = _reinforcement_factor(cluster, closest_reinforcements, reinforcement_percentage)
    aggression = _natural_aggression(cluster, aggression)

    sim_result.decision = _should_attack(cluster, sim_result, aggression)
    return sim_result.decision


def _should_continue_attack(cluster: UnitCluster, sim_result: CombatSimResult, closest_reinforcements: float,
                            reinforcement_percentage: float) -> bool:
    aggression = 1.2

    # Increase aggression if we are close to maxed and have no significant reinforcements incoming
    supply_used = bwapi.Broodwar.self().supplyUsed()
    if supply_used > 300 and reinforcement_percentage < 0.15:
        aggression += 0.005 * (supply_used - 300)

    aggression = _natural_aggression(cluster, aggression)

    sim_result.decision = _should_attack(cluster, sim_result, aggression)
    if sim_result.decision:
        return True

    # TODO: Would probably be a good idea to run a retreat sim to see what the consequences of retreating are

    # If this is the first run of the combat sim for this fight, always abort immediately
    if len(cluster.recent_sim_results) < 2:
        return False

    previous_sim_result = cluster.recent_sim_results[-2]

    # If the enemy army strength has increased significantly, abort the attack immediately
    if sim_result.initial_enemy > int(previous_sim_result.initial_enemy * 1.2):
        return False

    # If the fight is now across a choke, abort the attack immediately
    # We probably want to try to hold the choke instead
    if sim_result.narrow_choke is not None and previous_sim_result.narrow_choke is None:
        return False

    consecutive_retreat_frames, attack_frames, regroup_frames = UnitCluster.consecutive_sim_results(
        cluster.recent_sim_results, 48)

    # Continue if the sim hasn't been stable for 6 frames
    if consecutive_retreat_frames < 6:
        return True

    # Continue if the sim has recommended attacking more than regrouping; otherwise abort
    return attack_frames > regroup_frames


def _should_stop_regrouping(cluster: UnitCluster, sim_result: CombatSimResult, closest_reinforcements: float,
                            reinforcement_percentage: float) -> bool:
    # Always attack if we are maxed and have no significant reinforcements incoming
    if bwapi.Broodwar.self().supplyUsed() > 380 and reinforcement_percentage < 0.075:
        sim_result.decision = True
        return True

    # Be more conservative if the battle is across a choke
    aggression = 0.8
    narrow_choke = sim_result.narrow_choke
    if narrow_choke is not None:
        # Scale based on width: 128 pixels wide gives no reduction, 48 or lower gives 0.2 reduction
        aggression -= max(0.0, 0.2 * min(1.0, (128.0 - narrow_choke.width) / 80.0))

        # Scale based on length: 0 pixels long gives no reduction, 128 or higher gives 0.35 reduction
        aggression -= max(0.0, 0.35 * min(1.0, narrow_choke.length / 128.0))
    elif cluster.current_sub_activity == SubActivity.ContainChoke:
        # Special case if we were previously containing the choke and now the battle is not across it any more
        # This indicates the enemy has moved across the choke, so consider this to be attacking instead
        aggression = 1.2
    elif cluster.current_sub_activity == SubActivity.StandGround:
        # This is considered a somewhat neutral case
        aggression = 1.0

    # Adjust the aggression for reinforcements
    aggression *= _reinforcement_factor(cluster, closest_reinforcements, reinforcement_percentage)

    # Adjust the aggression if we have been pushed back into our natural
    if _pushed_back_to_natural(cluster, 500)[0]:
        aggression *= 2.0

    sim_result.decision = _should_attack(cluster, sim_result, aggression)
    if not sim_result.decision:
        return False

    consecutive_attack_frames, attack_frames, regroup_frames = UnitCluster.consecutive_sim_results(
        cluster.recent_sim_results, 72)

    # Continue if the sim hasn't been stable for 12 frames
    if consecutive_attack_frames < 12:
        return False

    # Continue if the sim has recommended regrouping more than attacking
    if regroup_frames > attack_frames:
        return False

    # Continue if our number of units has increased in the past 72 frames.
    # This gives our reinforcements time to link up with the rest of the cluster before engaging.
    for count, recent in enumerate(reversed(cluster.recent_sim_results)):
        if count >= 72:
            break
        if sim_result.my_unit_count > recent.my_unit_count:
            return False

    # Start the attack
    return True


class AttackBaseSquad(Squad):
    def __init__(self, base: Base, play_label: str = "") -> None:
        label = f"{play_label}:" if play_label else ""
        super().__init__(f"{label}Attack base @ {base.get_tile_position()}")
        self.base = base
        self.ignore_combat_sim = False
        self.target_position = base.get_position()

    def execute_cluster(self, cluster: UnitCluster) -> None:
        # Look for enemies near this cluster
        radius = 640 + cluster.vanguard.get_distance(cluster.center)
        enemy_units = units.enemy_in_radius(cluster.center, radius)

        # If there are no enemies near the cluster, just move towards the target
        if not enemy_units:
            cluster.set_activity(Activity.Moving)
            cluster.move(self.target_position)
            return

        self.update_detection_needs(enemy_units)

        # Select targets
        units_and_targets = cluster.select_targets(enemy_units, self.target_position)

        # Scan the targets to see if any of our units have a valid target that has been seen recently
        has_valid_target = False
        for my_unit, target in units_and_targets:
            if target is None:
                continue

            # A stationary attacker is valid if we are close to their attack range
            if (target.last_position_valid and unit_util.is_stationary_attacker(target.type)
                    and my_unit.is_in_enemy_weapon_range(target, buffer=96)):
                has_valid_target = True
                break

            # Other targets are valid if they have been seen in the past 5 seconds
            if target.last_seen > common.current_frame - 120:
                has_valid_target = True
                break

        # Run combat sim
        sim_result = cluster.run_recorded_combat_sim(cluster.recent_sim_results, self.target_position,
                                                     units_and_targets, enemy_units, self.detectors)

        # TODO: If our units can't do any damage (e.g. ground-only vs. air, melee vs. kiting ranged units), do
        # something else

        # Find the closest reinforcements to our army
        # Here reinforcements are defined as a cluster further away from the target position that is moving
        closest_reinforcements = 0.0
        total_reinforcements = 0
        for other in self.clusters:
            if other.center == cluster.center:
                continue
            if other.percentage_to_enemy_main > cluster.percentage_to_enemy_main:
                continue
            if other.current_activity != Activity.Moving:
                continue

            total_reinforcements += len(other.units)

            if other.percentage_to_enemy_main > closest_reinforcements:
                closest_reinforcements = other.percentage_to_enemy_main
        reinforcement_percentage = total_reinforcements / (len(cluster.units) + total_reinforcements)

        sim_result.closest_reinforcements = closest_reinforcements
        sim_result.reinforcement_percentage = reinforcement_percentage

        # Make the final decision based on what state we are currently in
        if cluster.current_activity == Activity.Moving:
            attack = _should_start_attack(cluster, sim_result, closest_reinforcements, reinforcement_percentage)
        elif cluster.current_activity == Activity.Attacking:
            attack = _should_continue_attack(cluster, sim_result, closest_reinforcements, reinforcement_percentage)
        elif cluster.current_activity == Activity.Regrouping:
            attack = _should_stop_regrouping(cluster, sim_result, closest_reinforcements, reinforcement_percentage)
        else:
            log.get("ERROR: Unknown cluster activity")
            attack = False

        if attack or self.ignore_combat_sim:
            cluster.set_activity(Activity.Attacking)

            # Move instead if none of our units have a valid target
            if not has_valid_target:
                cluster.move(self.target_position)
                return

            cluster.attack(units_and_targets, self.target_position)
            return

        # Check if our cluster should try to link up with a closer cluster
        vanguard_cluster = self.current_vanguard_cluster
        if vanguard_cluster is not None and vanguard_cluster.center != cluster.center:
            # Link up if our vanguard unit is closer to the vanguard cluster than its current target
            link_up = False
            for my_unit, target in units_and_targets:
                if my_unit is not cluster.vanguard:
                    continue

                if target is None or not target.last_position_valid:
                    link_up = True
                    break

                if my_unit.is_flying:
                    dist_target = my_unit.get_distance(target.last_position)
                    dist_vanguard_cluster = my_unit.get_distance(vanguard_cluster.vanguard.last_position)
                else:
                    dist_target = path_finding.get_ground_distance(my_unit.last_position, target.last_position,
                                                                   my_unit.type,
                                                                   PathFindingOptions.UseNeighbouringBWEMArea)
                    dist_vanguard_cluster = path_finding.get_ground_distance(
                        my_unit.last_position, vanguard_cluster.vanguard.last_position, my_unit.type,
                        PathFindingOptions.UseNeighbouringBWEMArea)
                link_up = dist_target == -1 or dist_vanguard_cluster == -1 or dist_vanguard_cluster < dist_target
                break

            if link_up:
                cluster.set_activity(Activity.Moving)
                cluster.move(vanguard_cluster.vanguard.last_position)
                return

        # TODO: Run retreat sim?

        cluster.set_activity(Activity.Regrouping)
        cluster.regroup(units_and_targets, enemy_units, self.detectors, sim_result, self.target_position,
                        has_valid_target)

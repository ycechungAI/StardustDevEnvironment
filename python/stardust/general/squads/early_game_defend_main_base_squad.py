"""Port of General/Squads/EarlyGameDefendMainBaseSquad.{h,cpp}: defends our main base in the early game, holding the
main choke when we can and falling back to the mineral line (where our workers can help) when we can't.

The DEBUG_COMBATSIM messages are omitted.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Orders, Position, Positions, Races, UnitTypes, WalkPosition
from stardust import common
from stardust.cpp import INT_MAX
from stardust.general.squad import Squad
from stardust.general.squads.worker_defense_squad import WorkerDefenseSquad
from stardust.general.unit_cluster.unit_cluster import Activity, UnitCluster
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.general.combat_sim_result import CombatSimResult
    from stardust.map.base import Base
    from stardust.map.choke import Choke
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit


def _my_main() -> Base:
    my_main = game_map.get_my_main()
    assert my_main is not None
    return my_main


def _in_main(pos: Position) -> bool:
    choke = game_map.get_my_main_choke()
    if choke is not None and choke.center.getApproxDistance(pos) < 320:
        return True

    return bwem.Instance().GetArea(WalkPosition(pos)) in game_map.get_my_main_areas()


def _combat_unit_seen_recently(unit: Unit) -> bool:
    frame = common.current_frame
    if not unit.type.isBuilding() and unit.last_seen < frame - 48:
        return False
    if not unit_util.is_combat_unit(unit.type) and unit.last_seen_attacking < frame - 120:
        return False
    if not unit.is_transport() and not unit_util.can_attack_ground(unit.type):
        return False
    return True


def _add_enemy_units(enemy_units: set[Unit], choke: Choke | None, cluster_center: Position = Positions.Invalid,
                     cluster_radius: int = -1) -> bool:
    """Adds the enemy units we are defending against; returns whether there is an enemy in our base, i.e. one that
    has passed the choke."""
    # Get enemy combat units in our base
    for area in game_map.get_my_main_areas():
        enemy_units |= units.enemy_in_area(area, _combat_unit_seen_recently)

    # Determine if there is an enemy in our base, i.e. has passed the choke
    if choke is not None and choke.is_narrow_choke:
        enemy_in_our_base = any(not game_map.is_in_narrow_choke(unit.get_tile_position()) for unit in enemy_units)
    else:
        enemy_in_our_base = bool(enemy_units)

    # If there is a choke, get enemy combat units close to it
    if choke is not None:
        enemy_units |= units.enemy_in_radius(choke.center, 192, _combat_unit_seen_recently)

    # If we passed a cluster center and a valid radius, get enemy combat units close to it
    if cluster_center != Positions.Invalid and cluster_radius > 0:
        enemy_units |= units.enemy_in_radius(
            cluster_center, cluster_radius + UnitTypes.Terran_Marine.groundWeapon().maxRange() + 32)

    # If there are no enemy combat units, include enemy buildings to defend against gas steals or other cheese
    if not enemy_units:
        for area in game_map.get_my_main_areas():
            enemy_units |= units.enemy_in_area(area, lambda unit: unit.type.isBuilding() and not unit.is_flying)

        enemy_in_our_base = bool(enemy_units)

    return enemy_in_our_base


def _should_start_attack(cluster: UnitCluster, sim_result: CombatSimResult) -> bool:
    # Don't start an attack until we have 24 frames of recommending attack with the same number of friendly units
    count = 0
    for recent in reversed(cluster.recent_sim_results):
        if count >= 24:
            break
        if not recent.decision:
            return False
        if sim_result.my_unit_count != recent.my_unit_count:
            return False
        count += 1

    # TODO: check that the sim result has been stable
    return count >= 24


def _should_abort_attack(cluster: UnitCluster, sim_result: CombatSimResult) -> bool:
    # If this is the first run of the combat sim for this fight, always abort immediately
    if len(cluster.recent_sim_results) < 2:
        return True

    previous_sim_result = cluster.recent_sim_results[-2]

    # If the number of enemy units has increased, abort the attack: the enemy has reinforced or we have discovered
    # previously-unseen enemy units
    if sim_result.enemy_unit_count > previous_sim_result.enemy_unit_count:
        return True

    # Otherwise only abort the attack when the sim has been stable for a number of frames
    if len(cluster.recent_sim_results) < 12:
        return False

    for count, recent in enumerate(reversed(cluster.recent_sim_results)):
        if count >= 12:
            break
        if recent.decision:
            return False

    return True


def _blocked_friendly_unit(choke: Choke, squad_units: dict[MyUnit, UnitCluster]) -> bool:
    """Whether we have a unit currently trying to pass through this choke."""
    for my_unit in units.all_mine():
        if not my_unit.moving_to().isValid():
            continue
        if my_unit.get_distance(choke.center) > 320:
            continue

        # Don't include units in this squad as this can cause unstable behaviour
        if my_unit in squad_units:
            continue

        choke_path, _ = path_finding.get_choke_point_path(my_unit.last_position, my_unit.moving_to(), my_unit.type,
                                                          PathFindingOptions.UseNearestBWEMArea)
        if any(choke.choke is bwem_choke for bwem_choke in choke_path):
            return True

    return False


class EarlyGameDefendMainBaseSquad(Squad):
    def __init__(self) -> None:
        super().__init__("Defend main base")
        self._choke: Choke | None = None
        self._choke_defend_end = Position(0, 0)
        self._worker_defense_squad = WorkerDefenseSquad(_my_main())
        self._initialize_choke()

    def can_transition_to_attack(self) -> bool:
        # Don't transition if any clusters are not attacking
        if any(cluster.current_activity != Activity.Attacking for cluster in self.clusters):
            return False

        # Get the vanguard cluster
        vanguard = self.vanguard_cluster()
        if vanguard is None:
            return False

        # Don't transition if the cluster has more workers than other units
        workers = sum(1 for unit in vanguard.units if unit.type.isWorker())
        others = len(vanguard.units) - workers
        if workers > others:
            return False

        # Assert that the sim has recommended attacking for at least the past 72 frames
        if len(vanguard.recent_sim_results) < 72:
            return False
        for count, recent in enumerate(reversed(vanguard.recent_sim_results)):
            if count >= 72:
                break
            if not recent.decision:
                return False

        return True

    def disband(self) -> None:
        self._worker_defense_squad.disband()

    def _initialize_choke(self) -> None:
        base = _my_main()
        choke = self._choke = game_map.get_my_main_choke()

        if choke is not None:
            self.target_position = choke.center

            if choke.is_narrow_choke:
                grid = path_finding.get_navigation_grid(base.get_position())
                end1_node = grid.node(choke.end1_center) if grid is not None else None
                end2_node = grid.node(choke.end2_center) if grid is not None else None
                if (end1_node is not None and end1_node.next_node is not None
                        and end2_node is not None and end2_node.next_node is not None):
                    self._choke_defend_end = (choke.end1_center if end1_node.cost < end2_node.cost
                                              else choke.end2_center)
                else:
                    end1_dist = path_finding.get_ground_distance(base.get_position(), choke.end1_center,
                                                                 UnitTypes.Protoss_Dragoon,
                                                                 PathFindingOptions.UseNearestBWEMArea)
                    end2_dist = path_finding.get_ground_distance(base.get_position(), choke.end2_center,
                                                                 UnitTypes.Protoss_Dragoon,
                                                                 PathFindingOptions.UseNearestBWEMArea)
                    self._choke_defend_end = choke.end1_center if end1_dist < end2_dist else choke.end2_center
        else:
            self.target_position = base.mineral_line_center

    def execute(self) -> None:
        super().execute()

        if not self.clusters:
            enemy_units: set[Unit] = set()
            _add_enemy_units(enemy_units, self._choke)

            workers_and_targets = self._worker_defense_squad.select_targets(enemy_units)
            self._worker_defense_squad.execute(workers_and_targets, [])

    def execute_cluster(self, cluster: UnitCluster) -> None:
        if game_map.get_my_main_choke() is not self._choke:
            self._initialize_choke()
        choke = self._choke

        game = bwapi.Broodwar
        enemy_race = game.enemy().getRace()

        # Against terran, add units that are close to our cluster units
        # This handles cases where the enemy draws them out of the choke
        furthest_from_center = -1
        if enemy_race == Races.Terran:
            for unit in cluster.units:
                furthest_from_center = max(furthest_from_center, unit.last_position.getApproxDistance(cluster.center))

        enemy_units: set[Unit] = set()
        enemy_in_our_base = _add_enemy_units(enemy_units, choke, cluster.center, furthest_from_center)

        self.update_detection_needs(enemy_units)

        # Get worker targets
        # The returned list includes any worker that is being threatened by an enemy unit
        workers_and_targets = self._worker_defense_squad.select_targets(enemy_units)
        worker_targets = {target for _, target in workers_and_targets}

        # Select targets
        units_and_targets = cluster.select_targets(enemy_units, self.target_position)

        # Consider enemy units in a bit larger radius to the choke for the combat sim
        if choke is not None:
            enemy_units |= units.enemy_in_radius(choke.center, 256, _combat_unit_seen_recently)

        # Remove non-threatening units before running the combat sim
        # The rationale is that we want our units to still harass the enemy units even if we don't have enough power
        # to actually take them head-on. Eventually we would draw the enemy units into our mineral line where our
        # workers can help. We should actually pull workers in many situations though.
        def threatening_unit(enemy_unit: Unit) -> bool:
            # Attacking a building -> not threatening
            bwapi_unit = enemy_unit.bwapi_unit
            if bwapi_unit is not None and bwapi_unit.getOrder() == Orders.AttackUnit:
                order_target = bwapi_unit.getOrderTarget()
                if order_target is not None and order_target.getType().isBuilding():
                    return False

            # A target of a worker -> threatening
            if enemy_unit in worker_targets:
                return True

            # Near attack range of any of our cluster units -> threatening
            return any(my_unit.is_in_enemy_weapon_range(enemy_unit, buffer=32) for my_unit in cluster.units)

        enemy_units = {enemy_unit for enemy_unit in enemy_units if threatening_unit(enemy_unit)}

        # Run combat sim
        sim_result = cluster.run_recorded_combat_sim(cluster.recent_sim_results, self.target_position,
                                                     units_and_targets, enemy_units, self.detectors, False)

        # Make the attack / retreat decision based on the sim result
        # TODO: Needs tuning
        sim_result.decision = sim_result.my_percent_lost() <= 0.001 or sim_result.percent_gain() > -0.1

        # Make the final decision based on what state we are currently in
        attack = sim_result.decision

        # Currently regrouping, but want to attack: do so once the sim has stabilized
        if attack and cluster.current_activity == Activity.Regrouping:
            attack = _should_start_attack(cluster, sim_result)

        # Currently attacking, but want to regroup: make sure regrouping is safe
        if not attack and cluster.current_activity == Activity.Attacking:
            attack = not _should_abort_attack(cluster, sim_result)

        # If the enemy has cloaked units and we have a choke, always stay at the choke
        # Rationale: we want to plug the choke to keep the cloaked units from getting in until we get detection
        if self.enemies_needing_detection and choke is not None:
            attack = True

        # Hold the choke against Zerg if we have enough zealots to block it
        if (enemy_race == Races.Zerg and not attack and not enemy_in_our_base and choke is not None
                and choke.is_narrow_choke):
            zealot_count = sum(1 for my_unit, _ in units_and_targets
                               if my_unit.type == UnitTypes.Protoss_Zealot and my_unit.get_distance(choke.center) < 250)
            if zealot_count > choke.width // 30:
                attack = True

        if attack:
            cluster.set_activity(Activity.Attacking)

            # Reset our defensive position to the choke when all enemy units are out of our base
            if (self.enemies_needing_detection or not enemy_in_our_base) and choke is not None:
                self.target_position = choke.center

            # Check if the enemy has static defense (e.g. cannon rush)
            has_static_defense = any(unit_util.is_stationary_attacker(unit.type) for unit in enemy_units)

            # Choose the type of micro depending on whether the enemy has static defense, we are holding a narrow
            # choke, or neither
            if has_static_defense:
                cluster.contain_static(enemy_units, self.target_position)
            elif (not self.enemies_needing_detection and not enemy_in_our_base and choke is not None
                  and choke.is_narrow_choke):
                if (not enemy_units and not units.all_enemy_of_type(UnitTypes.Protoss_Dark_Templar)
                        and _blocked_friendly_unit(choke, self.unit_to_cluster)):
                    cluster.attack(units_and_targets, _my_main().mineral_line_center)
                else:
                    cluster.hold_choke(choke, self._choke_defend_end, units_and_targets)
            else:
                cluster.attack(units_and_targets, self.target_position)

            if cluster.is_vanguard_cluster:
                self._worker_defense_squad.execute(workers_and_targets, units_and_targets)

            return

        cluster.set_activity(Activity.Regrouping)

        # Ensure the target position is set to the mineral line center
        # This allows our workers to help with the defense
        self.target_position = _my_main().mineral_line_center

        # Micro our units according to the following flowchart:
        # - If there is a threatened worker, attack the unit threatening it.
        # - If we are at the mineral line center, attack it.
        # - Move to the mineral line center.
        for index, (unit, target) in enumerate(units_and_targets):
            # If the unit is stuck, unstick it
            if unit.unstick():
                continue

            # If the unit is not ready (i.e. is already in the middle of an attack), don't touch it
            if not unit.is_ready():
                continue

            # Set our target to the enemy attacking the closest threatened worker
            closest_threatened_worker_dist = INT_MAX
            for worker, worker_target in workers_and_targets:
                dist = unit.get_distance(worker)
                if dist < closest_threatened_worker_dist:
                    target = worker_target
                    units_and_targets[index] = (unit, target)
                    closest_threatened_worker_dist = dist
            if closest_threatened_worker_dist < INT_MAX:
                assert target is not None
                unit.attack_unit(target, units_and_targets, False)
                continue

            # Attack the target if we are at the mineral line center and in the enemy's attack range
            if (target is not None and unit.get_distance(self.target_position) < 32
                    and unit.is_in_enemy_weapon_range(target, buffer=32)):
                unit.attack_unit(target, units_and_targets, False)
                continue

            unit.move_to(self.target_position)

        if cluster.is_vanguard_cluster:
            self._worker_defense_squad.execute(workers_and_targets, units_and_targets)

    def can_add_unit_to_cluster(self, unit: MyUnit, cluster: UnitCluster, dist: int) -> bool:
        unit_in_main = _in_main(unit.last_position)
        cluster_in_main = _in_main(cluster.vanguard.last_position)
        if unit_in_main and cluster_in_main:
            return True
        if unit_in_main or cluster_in_main:
            return False
        return super().can_add_unit_to_cluster(unit, cluster, dist)

    def should_combine_clusters(self, first: UnitCluster, second: UnitCluster) -> bool:
        first_in_main = _in_main(first.vanguard.last_position)
        second_in_main = _in_main(second.vanguard.last_position)
        if first_in_main and second_in_main:
            return True
        if first_in_main or second_in_main:
            return False
        return super().should_combine_clusters(first, second)

    def should_remove_from_cluster(self, unit: MyUnit, cluster: UnitCluster) -> bool:
        if _in_main(unit.last_position):
            return False
        return super().should_remove_from_cluster(unit, cluster)

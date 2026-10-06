"""Port of General/Squad.{h,cpp}: a group of units with a common target, split into clusters.

Stardust keeps clusters in a std::set of pointers; here they are a list in creation order. executeDetectors and
executeArbiters are in unit_cluster/detectors.py and unit_cluster/arbiters.py.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

import bwapi
from bwapi import Position, Positions, UnitType, UnitTypes, WalkPosition
from stardust import config
from stardust.cpp import INT_MAX
from stardust.general.unit_cluster.unit_cluster import Activity, UnitCluster
from stardust.instrumentation import cherryvis, log
from stardust.players import players
from stardust.units.my_observer import MyObserver

if TYPE_CHECKING:
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit

_DEBUG_CLUSTER_MEMBERSHIP = config.INSTRUMENTATION_ENABLED

# Units are added to a cluster if they are within this distance of the cluster vanguard
_ADD_THRESHOLD = 480

# Clusters are combined if their centers are within this distance of each other, adjusted for cluster size
_COMBINE_THRESHOLD = 480

# Units are removed from a cluster if they are further than this distance from the cluster vanguard, adjusted for
# cluster size
_REMOVE_THRESHOLD = 600


class Squad:
    def __init__(self, label: str) -> None:
        self.label = label
        self.target_position = Positions.Invalid

        self.clusters: list[UnitCluster] = []
        self.unit_to_cluster: dict[MyUnit, UnitCluster] = {}

        self.current_vanguard_cluster: UnitCluster | None = None
        self.vanguard_cluster_dist_to_target_position = INT_MAX

        self.enemies_needing_detection: set[Unit] = set()
        self.detectors: set[MyObserver] = set()  # only mobile detectors
        self.arbiters: set[MyUnit] = set()

    def add_unit(self, unit: MyUnit) -> None:
        cherryvis.log(f"Added to squad: {self.label}", unit.id)

        if unit.type.isDetector() and not unit.type.isBuilding():
            assert isinstance(unit, MyObserver)
            self.detectors.add(unit)
        elif unit.type == UnitTypes.Protoss_Arbiter:
            self.arbiters.add(unit)
        else:
            self._add_unit_to_best_cluster(unit)

    def can_add_unit_to_cluster(self, unit: MyUnit, cluster: UnitCluster, dist: int) -> bool:
        return dist - cluster.ball_radius <= _ADD_THRESHOLD

    def should_combine_clusters(self, first: UnitCluster, second: UnitCluster) -> bool:
        dist = first.center.getApproxDistance(second.center)

        # Combine if the clusters are close enough together, adjusting for their "ball radius"
        if dist - first.ball_radius - second.ball_radius <= _COMBINE_THRESHOLD:
            return True

        # Also combine if the clusters are further apart but fit together in an arc formation
        return (dist - first.line_radius - second.line_radius <= _COMBINE_THRESHOLD
                and abs(first.percentage_to_enemy_main - second.percentage_to_enemy_main) < 0.05)

    def should_remove_from_cluster(self, unit: MyUnit, cluster: UnitCluster) -> bool:
        dist = unit.last_position.getApproxDistance(cluster.center)
        return (dist - cluster.ball_radius > _REMOVE_THRESHOLD
                and (unit.dist_to_target_position == -1 or cluster.vanguard.dist_to_target_position == -1
                     or dist - cluster.line_radius > _REMOVE_THRESHOLD
                     or abs(unit.dist_to_target_position - cluster.vanguard.dist_to_target_position) > 480))

    def _add_unit_to_best_cluster(self, unit: MyUnit) -> None:
        # Look for a suitable cluster to add this unit to
        best: UnitCluster | None = None
        best_dist = INT_MAX
        for cluster in self.clusters:
            dist = unit.last_position.getApproxDistance(cluster.vanguard.last_position)
            if dist < best_dist and self.can_add_unit_to_cluster(unit, cluster, dist):
                best_dist = dist
                best = cluster

        if best is not None:
            best.add_unit(unit)
            self.unit_to_cluster[unit] = best
            return

        new_cluster = self.create_cluster(unit)
        self.clusters.append(new_cluster)
        self.unit_to_cluster[unit] = new_cluster

    def create_cluster(self, unit: MyUnit) -> UnitCluster:
        return UnitCluster(unit)

    def remove_unit(self, unit: MyUnit) -> None:
        if unit.type.isDetector() and not unit.type.isBuilding():
            if not isinstance(unit, MyObserver) or unit not in self.detectors:
                return

            cherryvis.log(f"Removed from squad: {self.label}", unit.id)
            self.detectors.discard(unit)
            return

        if unit.type == UnitTypes.Protoss_Arbiter:
            if unit not in self.arbiters:
                return

            cherryvis.log(f"Removed from squad: {self.label}", unit.id)
            self.arbiters.discard(unit)
            return

        cluster = self.unit_to_cluster.pop(unit, None)
        if cluster is None:
            return

        cherryvis.log(f"Removed from squad: {self.label}", unit.id)

        if unit in cluster.units:
            cluster.remove_unit(unit, self.target_position)

            if _DEBUG_CLUSTER_MEMBERSHIP:
                cherryvis.log(f"Removed from cluster @ {WalkPosition(cluster.center)}", unit.id)

            if not cluster.units:
                self.clusters.remove(cluster)

    def update_clusters(self) -> None:
        # If we have no target position, skip this
        if not self.target_position.isValid():
            log.get(f"ERROR: Trying to update clusters of a squad with no target position. Squad: {self.label}")
            return

        # Remove dead detectors and arbiters
        self.detectors = {detector for detector in self.detectors if detector.exists()}
        self.arbiters = {arbiter for arbiter in self.arbiters if arbiter.exists()}

        # Update the clusters: remove dead units, recompute position data
        for cluster in list(self.clusters):
            cluster.update_positions(self.target_position)
            if not cluster.units:
                self.clusters.remove(cluster)

        # Find clusters that should be combined
        first_index = 0
        while first_index < len(self.clusters):
            first = self.clusters[first_index]
            second_index = first_index + 1
            first_removed = False
            while second_index < len(self.clusters):
                second = self.clusters[second_index]
                if not self.should_combine_clusters(first, second):
                    second_index += 1
                    continue

                # Combine into the cluster closest to the target
                if first.vanguard_dist_to_target < second.vanguard_dist_to_target:
                    combine_into, combine_from = first, second
                else:
                    combine_into, combine_from = second, first

                combine_into.absorb_cluster(combine_from, self.target_position)

                # Update cluster of the moved units
                for moved_unit in combine_from.units:
                    self.unit_to_cluster[moved_unit] = combine_into

                    if _DEBUG_CLUSTER_MEMBERSHIP:
                        cherryvis.log(f"Combined into cluster @ {WalkPosition(combine_into.center)}", moved_unit.id)

                # Delete the appropriate cluster
                if combine_into is first:
                    del self.clusters[second_index]
                else:
                    del self.clusters[first_index]
                    first_removed = True
                    break
            if not first_removed:
                first_index += 1

        # Find units that should be removed from their cluster
        cluster_index = 0
        while cluster_index < len(self.clusters):
            cluster = self.clusters[cluster_index]
            for unit in list(cluster.units):
                if unit not in cluster.units or not self.should_remove_from_cluster(unit, cluster):
                    continue

                cluster.remove_unit(unit, self.target_position)
                self._add_unit_to_best_cluster(unit)
            cluster_index += 1

        # Find the vanguard cluster
        self.current_vanguard_cluster = None
        self.vanguard_cluster_dist_to_target_position = INT_MAX
        for cluster in self.clusters:
            cluster.is_vanguard_cluster = False

            if cluster.vanguard_dist_to_target < self.vanguard_cluster_dist_to_target_position:
                self.vanguard_cluster_dist_to_target_position = cluster.vanguard_dist_to_target
                self.current_vanguard_cluster = cluster
        if self.current_vanguard_cluster is not None:
            self.current_vanguard_cluster.is_vanguard_cluster = True

        if config.INSTRUMENTATION_ENABLED_VERBOSE:
            for cluster in self.clusters:
                cherryvis.draw_circle(cluster.center.x, cluster.center.y, cluster.ball_radius,
                                      cherryvis.DrawColor.Teal)
                cherryvis.draw_circle(cluster.center.x, cluster.center.y, cluster.line_radius,
                                      cherryvis.DrawColor.Blue)
                cherryvis.draw_circle(cluster.vanguard.last_position.x, cluster.vanguard.last_position.y, 32,
                                      cherryvis.DrawColor.Grey)

    def get_target_position(self) -> Position:
        return self.target_position

    def needs_detection(self) -> bool:
        return bool(self.enemies_needing_detection)

    def has_detection(self) -> bool:
        if not self.detectors:
            return False
        if not self.enemies_needing_detection:
            return True

        # We consider the squad to have detection if an observer is near the vanguard unit
        vanguard_cluster = self.current_vanguard_cluster
        if vanguard_cluster is not None and any(detector.get_distance(vanguard_cluster.vanguard) < 120
                                                for detector in self.detectors):
            return True

        # Otherwise we consider the squad to have detection if one of the enemies needing detection has been detected
        grid = players.grid(bwapi.Broodwar.self())
        return any(grid.detection(enemy.last_position) > 0 for enemy in self.enemies_needing_detection)

    def get_detectors(self) -> set[MyObserver]:
        return self.detectors

    def get_arbiters(self) -> set[MyUnit]:
        return self.arbiters

    def execute(self) -> None:
        from stardust.general.unit_cluster import arbiters, detectors

        self.enemies_needing_detection.clear()

        for cluster in list(self.clusters):
            self.execute_cluster(cluster)

        detectors.execute_detectors(self)
        arbiters.execute_arbiters(self)

    def execute_cluster(self, cluster: UnitCluster) -> None:
        """Squad::execute(UnitCluster &): executes the squad's behaviour for one of its clusters."""

    def disband(self) -> None:
        pass

    def get_units(self) -> list[MyUnit]:
        result: list[MyUnit] = list(self.detectors)
        result.extend(self.arbiters)
        result.extend(self.unit_to_cluster)
        return result

    def empty(self) -> bool:
        return not self.detectors and not self.arbiters and not self.unit_to_cluster

    def combat_unit_count(self) -> int:
        return len(self.unit_to_cluster)

    def get_unit_count_by_type(self) -> dict[UnitType, int]:
        result: dict[UnitType, int] = {}
        for unit in self.unit_to_cluster:
            result[unit.type] = result.get(unit.type, 0) + 1
        for detector in self.detectors:
            result[detector.type] = result.get(detector.type, 0) + 1
        for arbiter in self.arbiters:
            result[arbiter.type] = result.get(arbiter.type, 0) + 1
        return result

    def has_cluster_with_activity(self, activity: Activity) -> bool:
        return any(cluster.current_activity == activity for cluster in self.clusters)

    def vanguard_cluster(self) -> UnitCluster | None:
        return self.current_vanguard_cluster

    def vanguard_cluster_and_distance(self) -> tuple[UnitCluster | None, int]:
        """vanguardCluster(int *distToTargetPosition): the vanguard cluster and its distance to the target position."""
        return self.current_vanguard_cluster, self.vanguard_cluster_dist_to_target_position

    def can_reassign_from_vanguard_cluster(self, unit: MyUnit) -> bool:
        # Allow if the unit isn't in the vanguard cluster
        cluster = self.unit_to_cluster.get(unit)
        if cluster is None:
            return True
        vanguard_cluster = self.current_vanguard_cluster
        if cluster is not vanguard_cluster:
            return True
        assert vanguard_cluster is not None

        # Allow if the vanguard cluster has lots of units
        if len(vanguard_cluster.units) > 20:
            return True

        # Allow if the vanguard cluster is not attacking or fleeing
        return vanguard_cluster.current_activity != Activity.Attacking and not vanguard_cluster.is_fleeing()

    def update_detection_needs(self, enemy_units: set[Unit]) -> None:
        for unit in enemy_units:
            if not unit.can_attack_air() and not unit.can_attack_ground():
                continue

            if unit.needs_detection():
                self.enemies_needing_detection.add(unit)

    def add_instrumentation(self, squad_array: list[Any]) -> None:
        cluster_array: list[Any] = []
        for cluster in self.clusters:
            cluster.add_instrumentation(cluster_array)

        squad_array.append({
            "label": self.label,
            "target_position_x": self.target_position.x,
            "target_position_y": self.target_position.y,
            "count_combatUnits": self.combat_unit_count(),
            "count_cannons": self.get_unit_count_by_type().get(UnitTypes.Protoss_Photon_Cannon, 0),
            "count_observers": len(self.detectors),
            "count_arbiters": len(self.arbiters),
            "clusters": cluster_array,
        })

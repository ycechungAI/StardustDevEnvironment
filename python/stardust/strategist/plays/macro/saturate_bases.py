"""Port of Strategist/Plays/Macro/SaturateBases.{h,cpp}: ensures all of our bases are saturated with workers."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import UnitTypes
from stardust.cpp import INT_MAX
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_LOWEST, PRIORITY_WORKERS, Play, ProductionGoals, add_goal
from stardust.units import units
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.map.base import Base


class SaturateBases(Play):
    def __init__(self, worker_limit: int = 75) -> None:
        super().__init__("SaturateBases")
        self._worker_limit = worker_limit
        self._workers_per_patch = 2

    def set_workers_per_patch(self, workers_per_patch: int) -> None:
        self._workers_per_patch = workers_per_patch

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        import stardust.strategist.strategist as strategist

        # Hard cap, defaults to 75 workers
        if units.count_all(UnitTypes.Protoss_Probe) >= self._worker_limit:
            return

        # On the first scan, gather all owned bases and how many workers they need
        full_bases: list[Base] = []
        base_clusters: list[tuple[list[Base], int]] = []
        for base in game_map.get_my_bases(bwapi.Broodwar.self()):
            desired_workers = (workers.available_mineral_assignments(base, self._workers_per_patch)
                               + workers.available_gas_assignments(base))

            # Reduce by one if this base is already building a worker
            # It's OK if this base goes to a negative number of workers
            if base.resource_depot is not None and base.resource_depot.is_training():
                desired_workers -= 1

            # TODO: Reduce by workers in the area assigned to other duties

            if desired_workers > 0:
                base_clusters.append(([base], desired_workers))
            else:
                full_bases.append(base)

        # Now assign full bases to base clusters if they can safely transfer workers
        for base in full_bases:
            best_cluster: list[Base] | None = None
            best_frames = INT_MAX
            for cluster_bases, _ in base_clusters:
                frames = path_finding.expected_travel_time(base.get_position(), cluster_bases[0].get_position(),
                                                           UnitTypes.Protoss_Probe, PathFindingOptions.Default,
                                                           default_if_inaccessible=-1)
                if frames == -1:
                    continue
                if frames < best_frames:
                    best_frames = frames
                    best_cluster = cluster_bases

            if best_cluster is not None and (best_frames <= 400 or strategist.is_enemy_contained()):
                best_cluster.append(base)

        # Now order the production
        for cluster_bases, required_workers in base_clusters:
            # Balance the production amongst the bases
            desired_production_per_base = required_workers // len(cluster_bases)
            remainder = required_workers % len(cluster_bases)
            for base in cluster_bases:
                count = desired_production_per_base
                if remainder > 0:
                    count += 1
                    remainder -= 1

                if count < 1:
                    continue
                if base.resource_depot is None:
                    continue
                if not base.resource_depot.completed:
                    continue

                add_goal(prioritized_production_goals, PRIORITY_WORKERS,
                         UnitProductionGoal(self.label, UnitTypes.Protoss_Probe, count, 1, base))

        # Finally make sure we always produce probes at low priority on one base
        # This will allow us to ramp up our natural faster
        my_main = game_map.get_my_main()
        my_natural = game_map.get_my_natural()
        if len(game_map.get_my_bases()) == 1 and my_main is not None and my_natural is not None:
            desired_probes = ((my_main.mineral_patch_count() + my_natural.mineral_patch_count()) * 2
                              + (len(my_main.geysers_or_refineries()) + len(my_natural.geysers_or_refineries())) * 3
                              - units.count_all(UnitTypes.Protoss_Probe))
            if desired_probes > 0:
                add_goal(prioritized_production_goals, PRIORITY_LOWEST,
                         UnitProductionGoal(self.label, UnitTypes.Protoss_Probe, desired_probes, 1, my_main))

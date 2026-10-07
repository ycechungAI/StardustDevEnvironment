"""Port of Workers/MineralLockingOptimization/MineralLockingOptimization.{h,cpp}: the simple gather optimizer backend,
which only ensures mineral locking (re-sending gather when a worker's order target drifts to another patch)."""

from __future__ import annotations

from typing import TYPE_CHECKING, cast

import bwapi
from bwapi import UnitTypes
from stardust import common
from stardust.cpp import INT_MAX
from stardust.map import game_map
from stardust.units import units

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.units.my_unit import MyUnit
    from stardust.units.my_worker import MyWorker
    from stardust.units.resource import Resource
    from stardust.workers.mining_optimization_v2.data_model.map_data import InitialSplitData


def initialize() -> None:
    pass


def game_end() -> None:
    pass


def update() -> None:
    pass


def optimize_start_of_mining_at_base(base: Base,
                                     workers_and_depots_and_resources: list[tuple[MyWorker, MyUnit, Resource]]) -> bool:
    return False


def optimize_start_of_mining(worker: MyWorker, depot: MyUnit, resource: Resource) -> None:
    resource_bwapi_unit = resource.get_bwapi_unit_if_visible()
    if resource_bwapi_unit is None:
        return

    unit = worker.bwapi_unit
    assert unit is not None
    order_target = unit.getOrderTarget()
    if (order_target is not None and order_target.getResources()
            and order_target != resource_bwapi_unit
            and worker.last_command_frame < common.current_frame - bwapi.Broodwar.getLatencyFrames()):
        worker.gather(resource_bwapi_unit)


def optimize_return_of_resource(worker: MyWorker, depot: MyUnit, resource: Resource | None) -> None:
    pass


def initial_worker_split() -> dict[MyWorker, tuple[Resource, Resource, InitialSplitData | None]]:
    from stardust.units.my_worker import MyWorker

    assignments: dict[MyWorker, tuple[Resource, Resource, InitialSplitData | None]] = {}

    base = game_map.get_my_main()
    assert base is not None

    # Sort the mineral patches by proximity to the nexus, then by position
    base_position = base.get_position()
    mineral_patches = sorted(
        base.mineral_patches(),
        key=lambda patch: (patch.get_distance_to_type(UnitTypes.Protoss_Nexus, base_position), patch.tile))

    # We are only interested in the first four patches. (Stardust's std::set of the remaining patches iterates in
    # pointer order; we use the sorted order.)
    available_patches = mineral_patches[:4]

    # Greedily take the closest matches until all probes are assigned
    # TODO: Should really be optimizing for 7th collection
    for _ in range(4):
        best_dist = INT_MAX
        best_worker: MyWorker | None = None
        best_patch: Resource | None = None
        for unit in units.all_mine_completed_of_type(UnitTypes.Protoss_Probe):
            worker = cast(MyWorker, unit)
            if worker in assignments:
                continue

            for patch in available_patches:
                dist = patch.get_distance(worker)
                if dist < best_dist:
                    best_dist = dist
                    best_worker = worker
                    best_patch = patch

        if best_worker is not None and best_patch is not None:
            assignments[best_worker] = (best_patch, best_patch, None)
            available_patches.remove(best_patch)

    return assignments


def average_rotation_time_for(resource: Resource) -> int | None:
    return None

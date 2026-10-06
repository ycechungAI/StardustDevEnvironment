"""Port of Workers/WorkerGatherOptimizer.h: selects the mining optimization "backend" and its income constants.

The constants are the forecast income rates, which change between backends. In reality these aren't as simple as a
single constant: patches with a single worker produce slightly more efficiently per worker than patches with two
workers, there is a difference between near and far patches, and we get a boost at full saturation from patch locking.
Generally a worker assigned alone to a patch returns minerals approximately every 150 frames, a worker sharing a patch
approximately every 173 frames, and workers at a fully saturated base approximately every 166 frames, but efficiency
is reduced when cannons are in the mineral line. This is heavily map and base dependent.

Stardust currently uses MiningOptimizationV2 (MINERALS_PER_WORKER_FRAME 0.0488, MINERALS_PER_GAS_UNIT 0.687). Until
that is ported, the port uses the MineralLockingOptimization backend with its constants.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import stardust.workers.mineral_locking_optimization.mineral_locking_optimization as _backend

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.units.my_unit import MyUnit
    from stardust.units.my_worker import MyWorker
    from stardust.units.resource import Resource
    from stardust.workers.mining_optimization_v2.data_model.map_data import InitialSplitData

MINERALS_PER_WORKER_FRAME = 0.0465
GAS_PER_WORKER_FRAME = 0.071
MINERALS_PER_GAS_UNIT = 0.655


# The backend functions are looked up when called, as the backend may still be initializing when this module is
# imported (they import each other through the map and units modules)

def initialize() -> None:
    _backend.initialize()


def update() -> None:
    _backend.update()


def game_end() -> None:
    _backend.game_end()


def optimize_start_of_mining_at_base(base: Base,
                                     workers_and_depots_and_resources: list[tuple[MyWorker, MyUnit, Resource]]) -> bool:
    return _backend.optimize_start_of_mining_at_base(base, workers_and_depots_and_resources)


def optimize_start_of_mining(worker: MyWorker, depot: MyUnit, resource: Resource) -> None:
    _backend.optimize_start_of_mining(worker, depot, resource)


def optimize_return_of_resource(worker: MyWorker, depot: MyUnit, resource: Resource | None) -> None:
    _backend.optimize_return_of_resource(worker, depot, resource)


def initial_worker_split() -> dict[MyWorker, tuple[Resource, Resource, InitialSplitData | None]]:
    return _backend.initial_worker_split()


def average_rotation_time_for(resource: Resource) -> int | None:
    return _backend.average_rotation_time_for(resource)

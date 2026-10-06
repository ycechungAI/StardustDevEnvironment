"""Port of Workers/Workers.{h,cpp}: assigns our workers to bases, mineral patches and refineries, and issues their
gathering orders (delegating the fine-grained timing to the gather optimizer backend).

Stardust's assignment maps are keyed by shared_ptr and so iterate in pointer order; Python dicts iterate in insertion
order instead. This affects which worker remove_gas_worker picks and the order of start-of-mining optimization.

Like std::map::operator[], lookups default-insert where the C++ does and the inserted entries are observable: Job.NONE
in the job map (iterated by issue_orders and idle_worker_count), empty worker sets for bases (iterated by the
reassignable worker counts) and None refineries (iterated by remove_gas_worker, so a mineral worker that was once
looked up there can be picked as the gas worker to remove).

The verbose assignment logging is compiled out in Stardust (CVIS_LOG_WORKER_ASSIGNMENTS_VERBOSE is false) and omitted.
"""

from __future__ import annotations

from enum import Enum
from typing import TYPE_CHECKING, cast

import bwapi
from bwapi import Orders, Position, UnitCommandTypes, UnitTypes, WalkPosition
from stardust import common, config
from stardust.cpp import INT_MAX
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map, no_go_areas
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.units import units
from stardust.units.my_unit import MyUnit
from stardust.units.my_worker import MyWorker
from stardust.util import boids
from stardust.workers import worker_gather_optimizer as optimizer

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.units.resource import Resource
    from stardust.units.unit import Unit
    from stardust.workers.mining_optimization_v2.data_model.map_data import InitialSplitData, InitialSplitRotation

_LOG_WORKER_ASSIGNMENTS = config.INSTRUMENTATION_ENABLED


class Job(Enum):
    NONE = 0
    MINERALS = 1
    GAS = 2
    RESERVED = 3


def _bw(worker: MyUnit) -> bwapi.Unit:
    unit = worker.bwapi_unit
    assert unit is not None
    return unit


class _InitialWorkerState:
    """Executes the first two mining rotations of a worker in the initial split, when the optimizer provides timing
    data for them."""

    def __init__(self, worker: MyWorker, first_patch: Resource, second_patch: Resource,
                 initial_split_data: InitialSplitData) -> None:
        self.worker = worker
        self.first_patch = first_patch
        self.second_patch = second_patch
        self.initial_split_data = initial_split_data
        self.second_rotation: InitialSplitRotation | None = None
        self.state = 0

    def _send_gather(self, patch: Resource) -> None:
        worker = self.worker
        patch_unit = patch.get_bwapi_unit_if_visible()
        if patch_unit is None:
            log.get(f"ERROR: Initial split patch {patch.tile} is not accessible")
            return

        if not worker.gather(patch_unit):
            log.get(f"ERROR: Initial split worker {worker} could not gather patch {patch.tile}: "
                    f"{bwapi.Broodwar.getLastError()}")
        else:
            cherryvis.log(f"Sent gather command to patch {patch}", worker.id)

    def _send_return(self) -> None:
        worker = self.worker
        if not worker.return_cargo():
            log.get(f"ERROR: Initial split worker {worker} could not return cargo: {bwapi.Broodwar.getLastError()}")
        else:
            cherryvis.log("Sent return cargo command", worker.id)

    def execute(self) -> bool:
        """Returns False when the worker is finished with the initial split (or it went wrong)."""
        worker = self.worker
        game = bwapi.Broodwar
        frame = common.current_frame
        first_rotation = self.initial_split_data.first_rotation
        state = self.state

        if state == 0:
            if game.getFrameCount() == 0:
                cherryvis.log(f"Patch assignments: {self.first_patch} and {self.second_patch}", worker.id)
                cherryvis.log(f"First rotation: {first_rotation}", worker.id)

            # For evaluating the delay frames, we use getFrameCount since our current frame has two frame 0s
            if game.getFrameCount() == first_rotation.delay_frames:
                self._send_gather(self.first_patch)
                self.state += 1

        elif state in (1, 4):
            active_rotation = first_rotation if state == 1 else self.second_rotation
            assert active_rotation is not None
            active_patch = self.first_patch if state == 1 else self.second_patch
            if frame + game.getLatencyFrames() in active_rotation.resend_frames:
                self._send_gather(active_patch)
            if _bw(worker).getOrder() == Orders.WaitForMinerals:
                if frame != active_rotation.gather_action_frame:
                    log.get(f"ERROR: Worker {worker} gather action frame incorrect; expected "
                            f"{active_rotation.gather_action_frame}")
                    return False
                self.state += 1

        elif state in (2, 5):
            if _bw(worker).getOrder() == Orders.ReturnMinerals:
                self.state += 1

        elif state == 3:
            if frame + game.getLatencyFrames() in first_rotation.resend_frames:
                self._send_return()
            if not worker.carrying_resource:
                if self.first_patch.tile != self.second_patch.tile:
                    self._send_gather(self.second_patch)

                second_rotation = self.initial_split_data.first_rotation_delivery_to_second_rotation.get(frame)
                if second_rotation is None:
                    log.get(f"ERROR: Worker {worker} first return action frame incorrect")
                    return False
                self.second_rotation = second_rotation
                cherryvis.log(f"Second rotation: {second_rotation}", worker.id)

                self.state += 1

        elif state == 6:
            second_rotation = self.second_rotation
            assert second_rotation is not None
            if frame + game.getLatencyFrames() in second_rotation.resend_frames:
                self._send_return()
            if not worker.carrying_resource:
                if frame not in second_rotation.return_action_frames_to_occurrences:
                    log.get(f"ERROR: Worker {worker} second return action frame incorrect")
                return False

        return True


_desired_gas_worker_delta = 0
_worker_job: dict[MyWorker, Job] = {}
_worker_base: dict[MyWorker, Base | None] = {}
_base_workers: dict[Base, set[MyWorker]] = {}
_worker_mineral_patch: dict[MyWorker, Resource | None] = {}
_mineral_patch_workers: dict[Resource, set[MyWorker]] = {}
_worker_refinery: dict[MyWorker, Resource | None] = {}
_refinery_workers: dict[Resource, set[MyWorker]] = {}

_initial_split_state: dict[MyWorker, _InitialWorkerState] = {}

_mineral_worker_count = 0
_gas_worker_count = (0, 0)


def _job(worker: MyWorker) -> Job:
    """workerJob[worker]: inserts Job.NONE for unknown workers."""
    return _worker_job.setdefault(worker, Job.NONE)


def _workers_at(base: Base) -> set[MyWorker]:
    """baseWorkers[base]: inserts an empty set for unknown bases."""
    return _base_workers.setdefault(base, set())


def _desired_refinery_workers(base: Base | None, refinery_tile: bwapi.TilePosition) -> int:
    if base is not None and base.gas_requires_four_workers(refinery_tile):
        return 4
    return 3


def _remove_from_resource(unit: MyWorker, worker_assignment: dict[MyWorker, Resource | None],
                          resource_assignment: dict[Resource, set[MyWorker]]) -> None:
    """Removes a worker's base and mineral patch or gas assignments."""
    if unit in _worker_base:
        base = _worker_base.pop(unit)
        if base is not None:
            if _LOG_WORKER_ASSIGNMENTS:
                cherryvis.log(f"Removed from base @ {WalkPosition(base.get_position())} (removeFromResource)", unit.id)
            _workers_at(base).discard(unit)

    if unit in worker_assignment:
        resource = worker_assignment.pop(unit)
        if resource is not None:
            resource_assignment.setdefault(resource, set()).discard(unit)


def _worker_lost(unit: MyWorker) -> None:
    """Cleans up data maps when a worker is lost."""
    _worker_job.pop(unit, None)
    _remove_from_resource(unit, _worker_mineral_patch, _mineral_patch_workers)
    _remove_from_resource(unit, _worker_refinery, _refinery_workers)


def _available_mineral_assignments_at_base(base: Base | None, workers_per_patch: int = 2) -> int:
    if base is None or base.owner != bwapi.Broodwar.self():
        return 0
    if base.resource_depot is None:
        return 0

    count = base.mineral_patch_count() * workers_per_patch
    for worker in _workers_at(base):
        if _job(worker) == Job.MINERALS:
            count -= 1

    return count


def _available_gas_assignments_at_base(base: Base | None) -> int:
    if base is None or base.owner != bwapi.Broodwar.self():
        return 0
    if base.resource_depot is None:
        return 0

    count = 0
    for geyser_or_refinery in base.geysers_or_refineries():
        if not geyser_or_refinery.has_my_completed_refinery():
            continue
        count += _desired_refinery_workers(base, geyser_or_refinery.tile)
    for worker in _workers_at(base):
        if _job(worker) == Job.GAS:
            count -= 1

    return count


def _assign_initial_mineral_workers() -> None:
    # Run the optimizer to get the initial split, which maps each worker to the first two patches they should mine
    assignments = optimizer.initial_worker_split()

    # Assign the workers to the first patch, and if the optimizer implementation provides initial split data,
    # initialize the state
    main_base = game_map.get_my_main()
    assert main_base is not None
    for worker, (first_patch, second_patch, initial_split_data) in assignments.items():
        _worker_job[worker] = Job.MINERALS
        _worker_base[worker] = main_base
        _workers_at(main_base).add(worker)
        _worker_mineral_patch[worker] = first_patch
        _mineral_patch_workers.setdefault(first_patch, set()).add(worker)

        if initial_split_data is not None and worker not in _initial_split_state:
            _initial_split_state[worker] = _InitialWorkerState(worker, first_patch, second_patch, initial_split_data)


def _assign_base_and_job(unit: MyWorker, preferred_job: Job) -> Base | None:
    """Assign a worker to the closest base with either free mineral or gas assignments depending on job."""
    import stardust.strategist.strategist as strategist

    # TODO: Prioritize empty bases over nearly-full bases

    best_frames = INT_MAX
    best_base: Base | None = None
    best_has_preferred_job = False
    best_has_non_preferred_job = False
    for base in game_map.get_my_bases():
        if base.resource_depot is None:
            continue

        if preferred_job == Job.MINERALS:
            has_preferred = _available_mineral_assignments_at_base(base) > 0
            if best_has_preferred_job and not has_preferred:
                continue

            has_non_preferred = _available_gas_assignments_at_base(base) > 0
        else:
            has_preferred = _available_gas_assignments_at_base(base) > 0
            if best_has_preferred_job and not has_preferred:
                continue

            has_non_preferred = _available_mineral_assignments_at_base(base) > 0

        if not has_preferred and not has_non_preferred:
            continue

        frames = path_finding.expected_travel_time(unit.last_position, base.get_position(), unit.type,
                                                   PathFindingOptions.UseNearestBWEMArea, default_if_inaccessible=-1)
        if frames == -1:
            continue

        # Logic if the base is far away (i.e. would require a worker transfer)
        # Exclusion if we don't have many workers yet, in which case this is probably our scout
        if frames > 500 and units.count_completed(UnitTypes.Protoss_Probe) > 20:
            # Don't transfer to a threatened base
            if units.enemy_at_base(base):
                continue

            # Require our main army to be on the map
            main_army_play = strategist.get_main_army_play()
            if main_army_play is None:
                continue

            vanguard_cluster = main_army_play.get_squad().vanguard_cluster()
            if vanguard_cluster is None:
                continue

            if vanguard_cluster.percentage_to_enemy_main < 0.6:
                continue

        depot = base.resource_depot
        if not depot.completed and depot.bwapi_unit is not None:
            frames = max(frames, depot.bwapi_unit.getRemainingBuildTime())

        if (frames < best_frames or (has_preferred and not best_has_preferred_job)
                or (has_non_preferred and not best_has_non_preferred_job)):
            best_frames = frames
            best_base = base
            best_has_preferred_job = has_preferred
            best_has_non_preferred_job = has_non_preferred

    job = Job.NONE
    if best_base is not None:
        if _LOG_WORKER_ASSIGNMENTS and _worker_base.get(unit) is not best_base:
            cherryvis.log(f"Assigned to base @ {WalkPosition(best_base.get_position())}", unit.id)

        _worker_base[unit] = best_base
        _workers_at(best_base).add(unit)

        if best_has_preferred_job:
            job = preferred_job
        elif best_has_non_preferred_job:
            job = Job.GAS if preferred_job == Job.MINERALS else Job.MINERALS

    if _LOG_WORKER_ASSIGNMENTS and _job(unit) != job:
        if job == Job.MINERALS:
            cherryvis.log("Assigned to Minerals", unit.id)
        elif job == Job.GAS:
            cherryvis.log("Assigned to Gas", unit.id)
        elif job == Job.NONE:
            cherryvis.log("Assigned to None", unit.id)

    _worker_job[unit] = job

    return best_base


def _assign_mineral_patch(unit: MyWorker) -> Resource | None:
    """Assign a worker to a mineral patch in its current base.
    We only call this when the worker is close to the base depot, so we can assume minimal pathing issues."""
    base = _worker_base.get(unit)
    if base is None:
        return None

    # First scan to figure out if there are any patches with no workers assigned
    patch_without_workers = any(not _mineral_patch_workers.setdefault(patch, set())
                                for patch in base.mineral_patches())

    # Now scan the patches and score them based on how quickly a single worker gathers from it (lower is faster)
    # If we have a patch without workers, we choose the patch without workers with the lowest score
    # If not, we choose the available patch with the highest score, since the second worker will help this one the
    # most
    best_score = INT_MAX if patch_without_workers else 0
    best: Resource | None = None
    for mineral_patch in base.mineral_patches():
        # Skip this patch if it isn't available
        if len(_mineral_patch_workers.setdefault(mineral_patch, set())) != (0 if patch_without_workers else 1):
            continue

        # If the optimization engine provides patch rotation times, use them for scoring, otherwise fall back to
        # distance from nexus
        rotation_time = optimizer.average_rotation_time_for(mineral_patch)
        if rotation_time is not None:
            score = rotation_time
        else:
            score = mineral_patch.get_distance_to_type(UnitTypes.Protoss_Nexus, base.get_position())

        # Update the best result if needed
        if (patch_without_workers and score < best_score) or (not patch_without_workers and score > best_score):
            best_score = score
            best = mineral_patch

    if best is not None:
        _worker_mineral_patch[unit] = best
        _mineral_patch_workers.setdefault(best, set()).add(unit)

    return best


def _assign_refinery(unit: MyWorker) -> Resource | None:
    """Assign a worker to a refinery in its current base.
    We only call this when the worker is close to the base depot, so we can assume minimal pathing issues."""
    base = _worker_base.get(unit)
    if base is None:
        return None

    best_dist = INT_MAX
    best_refinery: Resource | None = None
    for geyser_or_refinery in base.geysers_or_refineries():
        if not geyser_or_refinery.has_my_completed_refinery():
            continue

        workers = len(_refinery_workers.get(geyser_or_refinery, ()))
        if workers >= _desired_refinery_workers(base, geyser_or_refinery.tile):
            continue

        dist = geyser_or_refinery.get_distance(unit)
        if dist < best_dist:
            best_dist = dist
            best_refinery = geyser_or_refinery

    if best_refinery is not None:
        _worker_refinery[unit] = best_refinery
        _refinery_workers.setdefault(best_refinery, set()).add(unit)

    return best_refinery


def _assign_gas_worker() -> None:
    my_completed_refineries = units.my_completed_refineries()

    # Find worker closest to an available refinery
    best_dist = INT_MAX
    best_worker: MyWorker | None = None
    for unit in units.all_mine():
        if not unit.type.isWorker():
            continue

        worker = cast(MyWorker, unit)
        if not is_available_for_reassignment(worker, False, False):
            continue

        for refinery in my_completed_refineries:
            # (Stardust looks up the desired count using the worker's base, not the refinery's.)
            if (len(_refinery_workers.get(refinery, ()))
                    >= _desired_refinery_workers(_worker_base.get(worker), refinery.tile)):
                continue

            # We penalize depleted or nearly-depleted geysers by 10x
            dist = refinery.get_distance(worker)
            if refinery.current_amount < 100:
                dist *= 10
            if dist < best_dist:
                best_dist = dist
                best_worker = worker

    if best_worker is not None:
        _remove_from_resource(best_worker, _worker_mineral_patch, _mineral_patch_workers)
        _assign_base_and_job(best_worker, Job.GAS)
        _assign_refinery(best_worker)


def _remove_gas_worker() -> None:
    # Remove a gas worker at a base that has available mineral assignments
    for worker in list(_worker_refinery):
        if _available_mineral_assignments_at_base(_worker_base.get(worker)) > 0:
            _worker_job[worker] = Job.NONE
            _remove_from_resource(worker, _worker_refinery, _refinery_workers)
            return


def initialize() -> None:
    global _desired_gas_worker_delta
    _desired_gas_worker_delta = 0
    _worker_job.clear()
    _worker_base.clear()
    _base_workers.clear()
    _worker_mineral_patch.clear()
    _mineral_patch_workers.clear()
    _worker_refinery.clear()
    _refinery_workers.clear()
    _initial_split_state.clear()


def on_unit_destroy(unit: Unit) -> None:
    # Handle lost workers
    if unit.type.isWorker() and unit.player == bwapi.Broodwar.self():
        _worker_lost(cast(MyWorker, unit))


def on_mineral_patch_destroyed(mineral_patch: Resource) -> None:
    workers = _mineral_patch_workers.pop(mineral_patch, None)
    if workers is not None:
        for worker in workers:
            _worker_mineral_patch.pop(worker, None)


def update_assignments() -> None:
    global _mineral_worker_count, _gas_worker_count

    mineral_worker_count = 0
    gas_worker_count = [0, 0]

    def count_mineral_worker(worker: MyWorker, mineral_patch: Resource | None) -> None:
        nonlocal mineral_worker_count
        if mineral_patch is None:
            return
        if mineral_patch.destroyed:
            return
        if mineral_patch.get_distance(worker) > 200:
            return
        mineral_worker_count += 1

    def count_gas_worker(worker: MyWorker, refinery: Resource | None) -> None:
        if refinery is None:
            return
        if not refinery.has_my_completed_refinery():
            return
        if refinery.get_distance(worker) > 200:
            return

        if refinery.current_amount <= 0:
            gas_worker_count[1] += 1
        else:
            gas_worker_count[0] += 1

    # Special case on frame 0: try to optimize for earliest possible seventh mineral returned
    if common.current_frame == 0 and not _initial_split_state:
        _assign_initial_mineral_workers()

    for unit in units.all_mine():
        if not unit.type.isWorker():
            continue
        worker = cast(MyWorker, unit)

        if not worker.completed:
            continue
        if _job(worker) == Job.RESERVED:
            continue

        if _job(worker) == Job.MINERALS:
            # If the worker is already assigned to a mineral patch, we don't need to do any more
            mineral_patch = _worker_mineral_patch.get(worker)
            if mineral_patch is not None:
                count_mineral_worker(worker, mineral_patch)
                continue

            # Release from assigned base if it is mined out
            base = _worker_base.get(worker)
            if base is not None and _available_mineral_assignments_at_base(base) <= 0:
                if _LOG_WORKER_ASSIGNMENTS:
                    cherryvis.log(f"Removed from base @ {WalkPosition(base.get_position())} (mined out)", worker.id)
                _workers_at(base).discard(worker)
                _worker_base[worker] = None
        elif _job(worker) == Job.GAS:
            # If the worker is already assigned to a refinery, we don't need to do any more
            refinery = _worker_refinery.setdefault(worker, None)
            if refinery is not None and refinery.has_my_completed_refinery():
                count_gas_worker(worker, refinery)
                continue

        # If the worker doesn't have an assigned base, assign it one
        base = _worker_base.get(worker)
        if (_job(worker) == Job.NONE or base is None or base.resource_depot is None
                or _available_mineral_assignments_at_base(base) <= 0):
            new_base = _assign_base_and_job(worker, Job.GAS if _job(worker) == Job.GAS else Job.MINERALS)

            if base is not new_base:
                if base is not None:
                    if _LOG_WORKER_ASSIGNMENTS:
                        cherryvis.log(f"Removed from base @ {WalkPosition(base.get_position())} (new base)",
                                      worker.id)
                    _workers_at(base).discard(worker)
                base = new_base

            # Maybe we have none
            # Stop the worker so it doesn't continue on some no-longer-valid movement trajectory
            # TODO: Move the unit back to some base if we at some point implement stable worker transfers
            if base is None:
                if _bw(worker).getLastCommand().getType() != UnitCommandTypes.Stop:
                    worker.stop()
                continue

        # Assign a resource when the worker is close enough to the base
        if worker.get_distance(base.get_position()) <= 300:
            if _job(worker) == Job.MINERALS:
                mineral_patch = _assign_mineral_patch(worker)
                count_mineral_worker(worker, mineral_patch)

                if _LOG_WORKER_ASSIGNMENTS and mineral_patch is not None:
                    cherryvis.log(f"Assignment: {mineral_patch}", worker.id)
            else:
                refinery = _assign_refinery(worker)
                count_gas_worker(worker, refinery)

                if _LOG_WORKER_ASSIGNMENTS and refinery is not None:
                    cherryvis.log(f"Assignment: {refinery}", worker.id)

    # We assign 4 workers to bottom geysers, which would confuse our producer, since the fourth worker doesn't
    # contribute to higher income compared to a normal geyser
    # So reduce our gas worker counts by one for each refinery with 4 workers assigned to it
    excess_gas_workers = 0
    for refinery, workers in _refinery_workers.items():
        if len(workers) <= 3:
            continue
        excess_gas_workers += 1
        if refinery.current_amount <= 0:
            gas_worker_count[1] -= 1
        else:
            gas_worker_count[0] -= 1

    _mineral_worker_count = mineral_worker_count
    _gas_worker_count = (gas_worker_count[0], gas_worker_count[1])

    cherryvis.set_board_value("mineralWorkers", str(mineral_worker_count))
    cherryvis.set_board_value("gasWorkers", f"{gas_worker_count[0]}:{gas_worker_count[1]}+{excess_gas_workers}")


def _issue_cargo_orders(worker: MyWorker, base: Base, mineral_patch: Resource | None) -> None:
    """Orders for a worker carrying cargo (part of issueOrders)."""
    unit = _bw(worker)
    game = bwapi.Broodwar
    frame = common.current_frame
    depot = base.resource_depot
    assert depot is not None

    # If the worker is in a narrow choke, use the return cargo command to ensure it doesn't get stuck on other units
    if game_map.is_in_narrow_choke(worker.get_tile_position()):
        if (unit.getOrder() != Orders.ReturnMinerals and unit.getOrder() != Orders.ReturnGas
                and worker.last_command_frame > (frame - game.getLatencyFrames())):
            worker.return_cargo()
        return

    # Handle the special case when the worker is harvesting from a base without a completed depot
    if not depot.completed:
        # Find the nearest base with a completed depot
        closest_time = INT_MAX
        closest_base: Base | None = None
        for other_base in game_map.get_my_bases():
            if other_base is base:
                continue
            if other_base.resource_depot is None or not other_base.resource_depot.completed:
                continue

            time = path_finding.expected_travel_time(worker.last_position, other_base.get_position(), worker.type,
                                                     PathFindingOptions.Default, default_if_inaccessible=-1)
            if time != -1 and time < closest_time:
                closest_time = time
                closest_base = other_base

        # If there is one, and the worker can get there and back before the base depot is complete, deliver there
        if closest_base is not None:
            closest_depot = closest_base.resource_depot
            assert closest_depot is not None
            base_to_base_time = path_finding.expected_travel_time(base.get_position(), closest_base.get_position(),
                                                                  UnitTypes.Protoss_Probe, PathFindingOptions.Default,
                                                                  default_if_inaccessible=-1)
            depot_unit = depot.bwapi_unit
            if (base_to_base_time != -1 and depot_unit is not None
                    and closest_time + base_to_base_time < depot_unit.getRemainingBuildTime()):
                if worker.get_distance(closest_depot) > 800:
                    worker.move_to(closest_base.get_position())
                elif (unit.getOrder() != Orders.ReturnMinerals and unit.getOrder() != Orders.ReturnGas
                      and worker.last_command_frame > (frame - game.getLatencyFrames())):
                    worker.right_click(closest_depot.bwapi_unit)
                elif unit.isCarryingMinerals():
                    if isinstance(closest_depot, MyUnit):
                        optimizer.optimize_return_of_resource(worker, closest_depot, mineral_patch)
                return

        # There wasn't another base to return to, or it would take too long, so move towards the preferred base instead
        worker.move_to(base.get_position())
        return

    # Leave it alone if it already has the return order or is resetting after finishing gathering
    if unit.getOrder() in (Orders.ReturnMinerals, Orders.ReturnGas, Orders.ResetCollision):
        if isinstance(depot, MyUnit) and mineral_patch is not None:
            optimizer.optimize_return_of_resource(worker, depot, mineral_patch)
        return

    # Leave it alone if we have just ordered it to do something, as that indicates we are trying to optimize
    if worker.last_command_frame > (frame - game.getLatencyFrames()):
        return

    worker.return_cargo()


def issue_orders() -> None:
    game = bwapi.Broodwar
    frame = common.current_frame

    # Adjust number of gas workers to desired count
    for _ in range(_desired_gas_worker_delta):
        _assign_gas_worker()
    for _ in range(-_desired_gas_worker_delta):
        _remove_gas_worker()

    workers_for_which_to_optimize_start_of_mining: dict[Base, list[tuple[MyWorker, MyUnit, Resource]]] = {}
    for worker, job in list(_worker_job.items()):
        if job == Job.RESERVED:
            continue

        # Execute initial split workers until they are finished with the two first rotations
        initial_split = _initial_split_state.get(worker)
        if initial_split is not None:
            if initial_split.execute():
                continue
            del _initial_split_state[worker]

        unit = _bw(worker)

        # Move to avoid a no-go area
        # Move always if there is danger, otherwise only if we aren't mining
        tile = worker.get_tile_position()
        if (no_go_areas.is_no_go_tile(tile, no_go_areas.TypeFilter.ONLY_DANGER)
                or (no_go_areas.is_no_go_tile(tile) and unit.getOrder() != Orders.MiningMinerals)):
            if config.DEBUG_UNIT_ORDERS:
                cherryvis.log("Moving to avoid no-go area", worker.id)
            worker.move_to(boids.avoid_no_go_area(worker))
            continue

        if job in (Job.MINERALS, Job.GAS):
            # Skip if the worker doesn't have a valid base
            base = _worker_base.get(worker)
            if base is None or base.resource_depot is None:
                continue

            mineral_patch = _worker_mineral_patch.get(worker)

            # If the worker has cargo, return it
            if unit.isCarryingMinerals() or unit.isCarryingGas():
                _issue_cargo_orders(worker, base, mineral_patch)
                continue

            # Handle mining from an assigned mineral patch
            if mineral_patch is not None:
                # If the unit is currently mining, leave it alone
                # ResetCollision happens both on the frame where mining completes and LF after issuing a gather
                # command, so we need to differentiate there
                order = unit.getOrder()
                if (order == Orders.MiningMinerals or order == Orders.ReturnMinerals
                        or (order == Orders.ResetCollision
                            and (frame - worker.last_command_frame - 1) > game.getLatencyFrames())):
                    continue

                # If we don't have vision on the mineral patch, move towards it
                patch_unit = mineral_patch.get_bwapi_unit_if_visible()
                if patch_unit is None:
                    worker.move_to(mineral_patch.center)
                    continue

                # If the unit hasn't been ordered to gather, order it to do so
                if (order != Orders.MoveToMinerals and order != Orders.WaitForMinerals
                        and (worker.last_command_frame < (frame - game.getLatencyFrames() - 1)
                             or unit.getLastCommand().getType() != UnitCommandTypes.Gather)):
                    cherryvis.log(f"hasn't been ordered to gather; order is {order}", worker.id)
                    worker.gather(patch_unit)
                    continue

                depot = base.resource_depot
                if isinstance(depot, MyUnit):
                    workers_for_which_to_optimize_start_of_mining.setdefault(base, []).append(
                        (worker, depot, mineral_patch))
                continue

            # Handle gathering from an assigned refinery
            refinery = _worker_refinery.setdefault(worker, None)
            if refinery is not None and refinery.has_my_completed_refinery() and refinery.get_distance(worker) < 500:
                # If the unit is currently gathering, leave it alone
                if unit.getOrder() in (Orders.MoveToGas, Orders.WaitForGas, Orders.HarvestGas, Orders.Harvest1,
                                       Orders.ReturnGas):
                    continue

                # Otherwise click on the refinery
                refinery_unit = refinery.get_bwapi_unit_if_visible()
                if refinery_unit is not None:
                    worker.gather(refinery_unit)
                    continue

            # If the worker is a long way from its base, move towards it
            if worker.get_distance(base.get_position()) > 200:
                if config.DEBUG_UNIT_ORDERS:
                    cherryvis.log("moveTo: Assigned base (far away)", worker.id)
                worker.move_to(base.get_position())
                continue

            # For some reason the worker doesn't have anything to do
            # Clear its state so it gets a new assignment
            _remove_from_resource(worker, _worker_mineral_patch, _mineral_patch_workers)
            _remove_from_resource(worker, _worker_refinery, _refinery_workers)
            if config.DEBUG_UNIT_ORDERS:
                cherryvis.log("Worker has nothing to do, clearing state", worker.id)
            worker.move_to(base.get_position())

        elif job == Job.NONE:
            # Move towards the base if we aren't near it
            base = _worker_base.get(worker)
            if base is None or base.resource_depot is None:
                continue

            if worker.get_distance(base.get_position()) > 200:
                worker.move_to(base.get_position())

    for base, workers_and_depots_and_resources in workers_for_which_to_optimize_start_of_mining.items():
        if optimizer.optimize_start_of_mining_at_base(base, workers_and_depots_and_resources):
            continue

        for worker, depot, resource in workers_and_depots_and_resources:
            optimizer.optimize_start_of_mining(worker, depot, resource)


def is_available_for_reassignment(unit: MyWorker | None, allow_carry_minerals: bool, allow_mining: bool) -> bool:
    if unit is None or not unit.exists() or not unit.completed or not unit.type.isWorker():
        return False

    job = _job(unit)
    if job == Job.NONE:
        return True
    if job == Job.MINERALS:
        bwapi_unit = _bw(unit)
        if bwapi_unit.isCarryingGas():
            return False
        if not allow_carry_minerals and bwapi_unit.isCarryingMinerals():
            return False

        # We allow a mining worker if it has only been mining for less than 10 frames
        if (not allow_mining and bwapi_unit.getOrder() == Orders.MiningMinerals
                and (common.current_frame - unit.last_started_mining) > 10):
            return False

        return True

    return False


def get_closest_reassignable_worker(position: Position, allow_carry_minerals: bool) -> tuple[MyWorker | None, int]:
    """The best worker to reassign to a task at the given position, and its travel time there (INT_MAX if none)."""
    best_time = INT_MAX
    best_score = INT_MAX
    best_worker: MyWorker | None = None
    for unit in units.all_mine():
        if not unit.exists():
            continue
        if not unit.type.isWorker():
            continue
        worker = cast(MyWorker, unit)

        if not is_available_for_reassignment(worker, allow_carry_minerals, True):
            continue

        travel_time = path_finding.expected_travel_time(worker.last_position, position, worker.type,
                                                        PathFindingOptions.UseNearestBWEMArea,
                                                        default_if_inaccessible=-1)

        # Disallow carrying minerals in all cases if the travel time is excessive
        # Rationale: we might be sending the unit to build something at another base and we want to return minerals
        # first
        bwapi_unit = _bw(worker)
        if travel_time > 100 and bwapi_unit.isCarryingMinerals():
            continue

        # If the unit is currently mining, penalize it by 3 seconds to encourage selecting other workers
        score = travel_time
        if bwapi_unit.getOrder() in (Orders.MiningMinerals, Orders.WaitForMinerals):
            score += 72

        # If the unit is currently unassigned, give it a 4 second bonus to encourage selecting it
        if _job(worker) == Job.NONE:
            score -= 96

        if travel_time != -1 and score < best_score:
            best_time = travel_time
            best_score = score
            best_worker = worker

    return best_worker, best_time


def get_base_worker_count(base: Base) -> int:
    return len(_base_workers.get(base, ()))


def get_base_workers(base: Base) -> list[MyWorker]:
    return list(_base_workers.get(base, ()))


def base_mineral_worker_count(base: Base) -> int:
    return sum(1 for worker in _base_workers.get(base, ()) if _worker_job.get(worker) == Job.MINERALS)


def reserve_base_workers(workers: list[MyWorker], base: Base) -> None:
    """Appends the base's workers to the list and reserves all workers in it."""
    if base not in _base_workers:
        return

    workers.extend(_base_workers[base])

    for worker in workers:
        reserve_worker(worker)


def reserve_worker(unit: MyWorker | None) -> None:
    if unit is None or not unit.exists() or not unit.type.isWorker() or not unit.completed:
        return

    if _job(unit) == Job.RESERVED:
        return

    _worker_job[unit] = Job.RESERVED
    _remove_from_resource(unit, _worker_mineral_patch, _mineral_patch_workers)
    _remove_from_resource(unit, _worker_refinery, _refinery_workers)
    cherryvis.log("Reserved for non-mining duties", unit.id)


def release_worker(unit: MyWorker | None) -> None:
    if (unit is None or not unit.exists() or not unit.type.isWorker() or not unit.completed
            or _job(unit) != Job.RESERVED):
        return

    _worker_job[unit] = Job.NONE
    cherryvis.log("Released from non-mining duties", unit.id)


def available_mineral_assignments(base: Base | None = None, workers_per_patch: int = 2) -> int:
    if base is not None:
        return _available_mineral_assignments_at_base(base, workers_per_patch)

    return sum(_available_mineral_assignments_at_base(my_base, workers_per_patch)
               for my_base in game_map.get_my_bases())


def available_gas_assignments(base: Base | None = None) -> int:
    if base is not None:
        return _available_gas_assignments_at_base(base)

    return sum(_available_gas_assignments_at_base(my_base) for my_base in game_map.get_my_bases())


def set_desired_gas_worker_delta(gas_worker_delta: int) -> None:
    global _desired_gas_worker_delta
    _desired_gas_worker_delta = gas_worker_delta


def mineral_workers() -> int:
    return _mineral_worker_count


def gas_workers() -> tuple[int, int]:
    """Gas workers at refineries with gas remaining, and at depleted refineries."""
    return _gas_worker_count


def reassignable_mineral_workers() -> int:
    # Do an initial scan to find the number of gas slots and available mineral workers there are at each base
    bases_and_gas_slots_and_mineral_workers_available: list[tuple[Base, int, int]] = []
    for base, workers in list(_base_workers.items()):
        if base.owner != bwapi.Broodwar.self():
            continue
        if base.resource_depot is None:
            return 0

        gas_available = 0
        for refinery in base.geysers_or_refineries():
            if refinery.has_my_completed_refinery():
                gas_available += _desired_refinery_workers(base, refinery.tile)

        mineral_workers_available = 0
        for worker in workers:
            if _job(worker) == Job.MINERALS:
                mineral_workers_available += 1
            if _job(worker) == Job.GAS:
                gas_available -= 1

        bases_and_gas_slots_and_mineral_workers_available.append(
            (base, max(gas_available, 0), mineral_workers_available))

    # Now count the number of mineral workers that can be transferred to gas, allowing transfer to close bases
    result = 0
    for base, gas_available, mineral_workers_available in bases_and_gas_slots_and_mineral_workers_available:
        if gas_available == 0:
            continue
        if mineral_workers_available >= gas_available:
            result += gas_available
            continue

        # Check if we can borrow mineral workers from a nearby base
        borrowed_mineral_workers = 0
        for other_base, other_gas_available, other_mineral_workers_available in \
                bases_and_gas_slots_and_mineral_workers_available:
            if base is other_base:
                continue
            if other_mineral_workers_available <= other_gas_available:
                continue
            frames = path_finding.expected_travel_time(base.get_position(), other_base.get_position(),
                                                       UnitTypes.Protoss_Probe, PathFindingOptions.Default,
                                                       default_if_inaccessible=-1)
            if frames == -1:
                continue
            if frames < 400:
                borrowed_mineral_workers += other_mineral_workers_available - other_gas_available

        result += min(gas_available, mineral_workers_available + borrowed_mineral_workers)

    return result


def reassignable_gas_workers() -> int:
    result = 0
    for base, workers in list(_base_workers.items()):
        if base.owner != bwapi.Broodwar.self():
            continue
        if base.resource_depot is None:
            return 0

        minerals_available = base.mineral_patch_count() * 2
        if minerals_available == 0:
            continue

        gas_workers_available = 0
        for worker in workers:
            if _job(worker) == Job.GAS:
                gas_workers_available += 1
            if _job(worker) == Job.MINERALS:
                minerals_available -= 1

        # (minerals_available is a size_t in Stardust: oversaturated minerals wrap around to a huge value.)
        result += gas_workers_available if minerals_available < 0 else min(minerals_available, gas_workers_available)

    return result


def idle_worker_count() -> int:
    return sum(1 for job in _worker_job.values() if job == Job.NONE)


def minerals_and_assigned_workers() -> dict[Resource, set[MyWorker]]:
    return _mineral_patch_workers


def get_workers_assigned_to(resource: Resource) -> set[MyWorker]:
    if resource.is_minerals:
        return set(_mineral_patch_workers.setdefault(resource, set()))
    return set(_refinery_workers.setdefault(resource, set()))


def get_other_worker_mining(resource: Resource, worker: MyWorker) -> MyWorker | None:
    for unit in _mineral_patch_workers.setdefault(resource, set()):
        if unit is not worker and unit.exists():
            return unit
    return None


def set_worker_mineral_patch(worker: MyWorker, resource: Resource, base: Base) -> None:
    _remove_from_resource(worker, _worker_mineral_patch, _mineral_patch_workers)

    _worker_base[worker] = base
    _workers_at(base).add(worker)
    _worker_mineral_patch[worker] = resource
    _mineral_patch_workers.setdefault(resource, set()).add(worker)

    if _LOG_WORKER_ASSIGNMENTS:
        cherryvis.log(f"Forced reassignment to {resource} in base @ {WalkPosition(base.get_position())}", worker.id)

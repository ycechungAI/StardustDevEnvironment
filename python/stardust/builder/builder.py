"""Port of Builder/Builder.{h,cpp}: queues buildings on builder workers and orders the workers to construct them.

Builder queues are keyed by worker; Stardust iterates them in pointer order, this port in insertion order. Reserved
builders are kept in insertion order too (a dict used as an ordered set).
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Errors, Position, TilePosition, UnitType, UnitTypes, WalkPosition
from stardust import common, config
from stardust.builder import building_placement
from stardust.builder.building import Building
from stardust.cpp import INT_MAX
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.players import players
from stardust.units import units
from stardust.util import geo
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.units.my_worker import MyWorker

_pending_buildings: list[Building] = []
_builder_queues: dict[MyWorker, list[Building]] = {}
_reserved_builders: dict[MyWorker, None] = {}


def _build(building: Building) -> None:
    """Ensures the worker is being ordered to build this building."""
    builder = building.builder
    if building.unit is not None or builder is None or not builder.exists():
        return

    building.add_no_go_area_when_needed()

    # If the builder is constructing, we have already given it the build command and are waiting for the building to
    # appear
    builder_unit = builder.bwapi_unit
    assert builder_unit is not None
    if builder_unit.isConstructing():
        return

    # If we are close to the build position and want to build now, issue the build command
    game = bwapi.Broodwar
    dist = geo.edge_to_edge_distance(builder.type, builder.last_position, building.type, building.get_position())
    if dist <= 24 and building.desired_start_frame - common.current_frame <= game.getRemainingLatencyFrames():
        # Return immediately if issuing the build command succeeded
        if builder.build(building.type, building.tile):
            if config.LOGGING_ENABLED:
                log.debug(f"Issued successful build command for builder {builder.id} to build {building}")

            building.build_command_success_frames += 1
            return

        if config.LOGGING_ENABLED:
            log.debug(f"Builder {builder.id} could not build {building}: {game.getLastError()}")

        if game.getLastError() != Errors.Insufficient_Minerals and game.getLastError() != Errors.Insufficient_Gas:
            building.build_command_failure_frames += 1

    # Move towards the build position
    if config.DEBUG_UNIT_ORDERS:
        cherryvis.log(f"moveTo: Build location {WalkPosition(building.get_position())}", builder.id)
    builder.move_to(building.get_position())


def _release_builder(building: Building) -> None:
    """Releases the builder from constructing the given building. The builder may still have more buildings in its
    queue."""
    builder = building.builder
    if builder is None:
        return

    building.builder = None

    # Remove the building from the builder's queue
    builder_queue = _builder_queues.setdefault(builder, [])
    builder_queue[:] = [queued for queued in builder_queue if queued is not building]

    # When the builder has no more buildings in its queue, release it back to Workers, unless it is reserved
    if not builder_queue:
        if builder not in _reserved_builders:
            cherryvis.log("Releasing from non-mining duties (builder queue empty)", builder.id)
            workers.release_worker(builder)

        del _builder_queues[builder]


def _write_instrumentation() -> None:
    if not config.INSTRUMENTATION_ENABLED:
        return

    values = []
    for pending_building in _pending_buildings:
        value = f"{pending_building.type} {pending_building.tile}"
        if pending_building.builder is not None:
            value += f" {pending_building.builder}"
        values.append(value)
    cherryvis.set_board_list_value("builder", values)


def initialize() -> None:
    _pending_buildings.clear()
    _builder_queues.clear()


def _remove_pending(building: Building) -> None:
    _release_builder(building)
    building.remove_no_go_area()
    _pending_buildings.remove(building)


def update() -> None:
    # Clear dead reserved builders
    for builder in [builder for builder in _reserved_builders if not builder.exists()]:
        del _reserved_builders[builder]

    # Prune the list of pending buildings
    pylons = 0
    for building in list(_pending_buildings):
        # Remove the building if:
        # - The building has completed
        # - The building has died while under construction
        # - The builder has died before starting construction
        # TODO: Cancel dying buildings
        if building.unit is not None:
            gone = not building.unit.exists() or building.unit.completed
        else:
            assert building.builder is not None
            gone = not building.builder.exists()
        if gone:
            building_placement.on_building_cancelled(building)
            _remove_pending(building)
            continue

        # Check for buildings our worker is unable to build
        if building.unit is None:
            # If we've sent successful build commands for the past two seconds without the building starting, assume
            # the enemy is blocking it with something
            if building.build_command_success_frames >= 48:
                log.get(f"Build of {building} failed due to enemy blocking")

                if building.type == UnitTypes.Protoss_Nexus:
                    base = game_map.base_near(Position(building.tile))
                    if base is not None and not base.blocked_by_enemy:
                        log.get(f"Base @ {base.get_tile_position()} is blocked by an enemy unit")
                        base.blocked_by_enemy = True
            elif building.build_command_failure_frames >= 240:
                log.get(f"Build of {building} failed due to own unit blocking or other error")
            else:
                if (common.current_frame - building.desired_start_frame) == 480:
                    log.get(f"WARNING: Builder took a long time to start {building}")
                continue

            # TODO: At some point we should restore the building placement, at least in some cases
            _remove_pending(building)
            continue

        if building.type == UnitTypes.Protoss_Pylon:
            pylons += 1

    if config.LOGGING_ENABLED and pylons > 10:
        log.get("ERROR: Excessive pylon production")

    # Check for started buildings
    # TODO: Flip this around and only check the unit list when we have sent the build order
    if _pending_buildings:
        for unit in units.all_mine():
            if not unit.type.isBuilding() or unit.completed:
                continue
            for pending_building in _pending_buildings:
                if pending_building.unit is not None:
                    continue
                if pending_building.type != unit.type:
                    continue
                if pending_building.tile != unit.get_tile_position():
                    continue

                pending_building.construction_started(unit)
                _release_builder(pending_building)
                pending_building.remove_no_go_area()

    # Remove dead builders
    # The buildings themselves are pruned earlier
    for builder in [builder for builder in _builder_queues if not builder.exists()]:
        del _builder_queues[builder]

    _write_instrumentation()


def issue_orders() -> None:
    for builder_queue in _builder_queues.values():
        _build(builder_queue[0])


def build(unit_type: UnitType, tile: TilePosition, builder: MyWorker, start_frame: int = 0) -> None:
    # Check we don't already have a building here
    existing = units.my_building_at(tile)
    if existing is not None:
        log.get(f"ERROR: Can't build {unit_type} at {tile} as we already have {existing.type}")
        return

    # Sanity check that we don't already have a pending building overlapping the tile
    # This happens if the producer orders two buildings on the same frame where one is using a build location converted
    # from the other. When we detect this, we can just return; the producer will use a different build location on
    # the next frame.
    for pending_building in _pending_buildings:
        if pending_building.is_construction_started():
            continue
        if geo.overlaps_tiles(tile, unit_type.tileWidth(), unit_type.tileHeight(), pending_building.tile,
                              pending_building.type.tileWidth(), pending_building.type.tileHeight()):
            return

    building = Building(unit_type, tile, builder, start_frame)
    _pending_buildings.append(building)
    _builder_queues.setdefault(builder, []).append(building)

    workers.reserve_worker(builder)

    log.debug(f"Queued {building} to start at {start_frame} for builder {builder.id}; builder queue length: "
              f"{len(_builder_queues[builder])}")

    building_placement.on_building_queued(building)


def cancel(tile: TilePosition) -> None:
    for building in _pending_buildings:
        if building.tile != tile:
            continue

        # If construction has started, cancel it
        if building.unit is not None:
            log.get(f"Cancelling construction of {building.unit}")
            building.unit.cancel_construction()

        building.remove_no_go_area()

        building_placement.on_building_cancelled(building)

        _release_builder(building)
        _pending_buildings.remove(building)
        return


def get_builder_unit(tile: TilePosition, unit_type: UnitType) -> tuple[MyWorker | None, int]:
    """The best worker to build the given building, and its expected arrival frame (-1 if there is none)."""
    build_position = Position(tile) + Position(unit_type.tileWidth() * 16, unit_type.tileHeight() * 16)

    # First get the closest worker currently available for reassignment
    best_mineral_worker, best_mineral_worker_travel_time = workers.get_closest_reassignable_worker(
        build_position, not unit_type.isResourceDepot())

    # Next get the best existing builder
    best_builder: MyWorker | None = None
    best_builder_travel_time = INT_MAX
    for builder, queue in _builder_queues.items():
        if not builder.exists():
            continue
        if not queue:
            continue

        total_travel_time = 0

        # Sum up the travel time between the existing queued buildings
        last_position = builder.last_position
        for building in queue:
            total_travel_time += path_finding.expected_travel_time(last_position, building.get_position(),
                                                                   builder.type, PathFindingOptions.UseNearestBWEMArea)
            last_position = building.get_position()

        # Add in the travel time to this next building
        expected_travel_time = path_finding.expected_travel_time(last_position, build_position, builder.type,
                                                                 PathFindingOptions.UseNearestBWEMArea,
                                                                 default_if_inaccessible=-1)
        if expected_travel_time == -1:
            continue  # Builder might be on an island
        total_travel_time += expected_travel_time

        # Give a bonus to already-building workers, as we don't want to take a lot of workers off minerals
        if total_travel_time < best_builder_travel_time:
            best_builder_travel_time = total_travel_time
            best_builder = builder

    # Finally get the best reserved builder
    best_reserved: MyWorker | None = None
    best_reserved_travel_time = INT_MAX
    for reserved_builder in _reserved_builders:
        # Don't consider this again if it already has a queue
        if reserved_builder in _builder_queues:
            continue

        expected_travel_time = path_finding.expected_travel_time(reserved_builder.last_position, build_position,
                                                                 reserved_builder.type,
                                                                 PathFindingOptions.UseNearestBWEMArea,
                                                                 default_if_inaccessible=-1)
        if expected_travel_time == -1:
            continue  # Reserved builder might be on an island

        # Give a bonus to reserved builders (similar to above)
        if expected_travel_time < best_reserved_travel_time:
            best_reserved_travel_time = expected_travel_time
            best_reserved = reserved_builder

    # Pick the best worker, giving existing builders a 6-second bonus
    if best_reserved_travel_time < best_builder_travel_time:
        best_builder = best_reserved
        best_builder_travel_time = best_reserved_travel_time

    if best_mineral_worker_travel_time < (best_builder_travel_time - 144):
        chosen = best_mineral_worker
        travel_time = best_mineral_worker_travel_time
    else:
        chosen = best_builder
        travel_time = best_builder_travel_time

    return chosen, (common.current_frame + travel_time if chosen is not None else -1)


def all_pending_buildings() -> list[Building]:
    return _pending_buildings


def pending_buildings_of_type(unit_type: UnitType) -> list[Building]:
    return [building for building in _pending_buildings if building.type == unit_type]


def cancel_base(base: Base) -> None:
    cancel(base.get_tile_position())

    # Also cancel assimilator if it is building
    for pending_building in pending_buildings_of_type(UnitTypes.Protoss_Assimilator):
        if base.has_geyser_or_refinery_at(pending_building.tile):
            cancel(pending_building.tile)


def is_pending_here(tile: TilePosition) -> bool:
    return any(pending_building.tile == tile for pending_building in _pending_buildings)


def pending_here(tile: TilePosition) -> Building | None:
    for building in _pending_buildings:
        if building.tile == tile:
            return building
    return None


def has_pending_building(builder: MyWorker) -> bool:
    return bool(_builder_queues.get(builder))


def add_reserved_builder(builder: MyWorker) -> None:
    _reserved_builders[builder] = None


def release_reserved_builder(builder: MyWorker) -> None:
    _reserved_builders.pop(builder, None)


def is_in_enemy_static_threat_range(tile: TilePosition, unit_type: UnitType) -> bool:
    grid = players.grid(bwapi.Broodwar.enemy())

    width = unit_type.tileWidth()
    height = unit_type.tileHeight()
    for offset in (TilePosition(0, 0), TilePosition(width, 0), TilePosition(width, height), TilePosition(0, height)):
        if grid.static_ground_threat(WalkPosition(tile + offset)) > 0:
            return True

    return False


def frames_until_completed(tile: TilePosition, default_value: int) -> int:
    """If there is a building at this tile, the frames until it is completed (0 if already completed); otherwise the
    given default value."""
    unit = units.my_building_at(tile)
    if unit is not None:
        if unit.completed:
            return 0
        return unit.estimated_completion_frame - common.current_frame

    pending_building = pending_here(tile)
    if pending_building is not None:
        return pending_building.expected_frames_until_completion()

    return default_value

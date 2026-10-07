"""Port of Builder/BuildingPlacement.{h,cpp} and Builder/BuildingPlacement_Walls.cpp: block-based building placement,
base static defense locations and the natural Forge/Gateway wall.

Block finding logic:
- Start with our starting block, which provides 2 gateways, 2-4 tech buildings and 3-4 cannons
- Then progressively try to place smaller and smaller blocks

Pylon placement logic:
- First pylon is always in start block
- In PvP, next pylon gives power to the choke cannon
- Subsequent pylons are ordered by their distance to the mineral line
- Producer takes first pylon that either provides required building locations or keeps a minimum number of build
  locations available

Porting notes:
- Stardust's std::set<TilePosition>/<WalkPosition> iterate in (x, y) order, which decides ties in many of the searches
  here, so the port iterates such sets sorted. Internal tile sets hold (x, y) tuples.
- BuildLocationCmp is a lexicographic comparison, so it is a sort key here.
- The DEBUG_PLACEMENT per-option debug logging of the wall search is omitted.
"""

from __future__ import annotations

import math
from collections import deque
from collections.abc import Callable
from enum import IntEnum
from typing import TYPE_CHECKING

import numpy as np

import bwapi
import bwem
from bwapi import Position, Positions, Races, TilePosition, TilePositions, UnitType, UnitTypes, WalkPosition
from stardust import common, config
from stardust.builder.block import Block, Location
from stardust.builder.blocks.block_10x3 import Block10x3
from stardust.builder.blocks.block_10x6 import Block10x6
from stardust.builder.blocks.block_12x5 import Block12x5
from stardust.builder.blocks.block_12x8 import Block12x8
from stardust.builder.blocks.block_13x6 import Block13x6
from stardust.builder.blocks.block_14x3 import Block14x3
from stardust.builder.blocks.block_14x6 import Block14x6
from stardust.builder.blocks.block_16x5 import Block16x5
from stardust.builder.blocks.block_16x8 import Block16x8
from stardust.builder.blocks.block_17x6 import Block17x6
from stardust.builder.blocks.block_18x3 import Block18x3
from stardust.builder.blocks.block_18x6 import Block18x6
from stardust.builder.blocks.block_2x2 import Block2x2
from stardust.builder.blocks.block_2x4 import Block2x4
from stardust.builder.blocks.block_4x2 import Block4x2
from stardust.builder.blocks.block_4x4 import Block4x4
from stardust.builder.blocks.block_4x5 import Block4x5
from stardust.builder.blocks.block_4x8 import Block4x8
from stardust.builder.blocks.block_5x2 import Block5x2
from stardust.builder.blocks.block_5x4 import Block5x4
from stardust.builder.blocks.block_6x3 import Block6x3
from stardust.builder.blocks.block_8x2 import Block8x2
from stardust.builder.blocks.block_8x5 import Block8x5
from stardust.builder.blocks.block_8x8 import Block8x8
from stardust.builder.blocks.start_above_and_below_left import StartAboveAndBelowLeft
from stardust.builder.blocks.start_bottom_horizontal import StartBottomHorizontal
from stardust.builder.blocks.start_bottom_left_horizontal import StartBottomLeftHorizontal
from stardust.builder.blocks.start_compact_left import StartCompactLeft
from stardust.builder.blocks.start_compact_left_horizontal import StartCompactLeftHorizontal
from stardust.builder.blocks.start_compact_right import StartCompactRight
from stardust.builder.blocks.start_compact_right_vertical import StartCompactRightVertical
from stardust.builder.blocks.start_normal_left import StartNormalLeft
from stardust.builder.blocks.start_normal_right import StartNormalRight
from stardust.builder.blocks.start_top_left_horizontal import StartTopLeftHorizontal
from stardust.builder.forge_gateway_wall import ForgeGatewayWall
from stardust.cpp import INT_MAX, clog, clog10, cround, fdiv, to_int
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.units import units
from stardust.util import geo, unit_util

if TYPE_CHECKING:
    from stardust.builder.building import Building
    from stardust.map.base import Base
    from stardust.map.choke import Choke
    from stardust.units.resource import Resource
    from stardust.units.unit import Unit

_CVIS_HEATMAPS = config.INSTRUMENTATION_ENABLED

Tile = tuple[int, int]


class Neighbourhood(IntEnum):
    MAIN_BASE = 0
    ALL_MY_BASES = 1
    HIDDEN_BASE = 2


NEIGHBOURHOOD_COUNT = 3
ALL_NEIGHBOURHOODS = (Neighbourhood.MAIN_BASE, Neighbourhood.ALL_MY_BASES, Neighbourhood.HIDDEN_BASE)


class BuildLocation:
    """Data about a build location."""

    __slots__ = ("location", "builder_frames", "frames_until_powered", "distance_to_exit", "is_tech", "powers_medium",
                 "powers_large")

    def __init__(self, location: Location, builder_frames: int, frames_until_powered: int, distance_to_exit: int,
                 is_tech: bool = False) -> None:
        self.location = location
        self.builder_frames = builder_frames  # Approximately how many frames the builder will take to get here
        self.frames_until_powered = frames_until_powered  # Approximately how many frames until this is powered
        self.distance_to_exit = distance_to_exit  # Approximate ground distance to the neighbourhood exit
        self.is_tech = is_tech  # Whether this build location is for a tech building
        self.powers_medium: list[BuildLocation] = []  # For a pylon, what medium build locations it would power
        self.powers_large: list[BuildLocation] = []  # For a pylon, what large build locations it would power


BuildLocationSet = list[BuildLocation]
BuildLocations = list[list[BuildLocationSet]]  # [neighbourhood][size 0-4]


class BaseStaticDefenseLocations:
    __slots__ = ("power_pylon", "worker_defense_cannons", "start_block_cannon")

    def __init__(self, power_pylon: TilePosition, worker_defense_cannons: list[TilePosition],
                 start_block_cannon: TilePosition) -> None:
        self.power_pylon = power_pylon
        self.worker_defense_cannons = worker_defense_cannons
        self.start_block_cannon = start_block_cannon

    def is_valid(self) -> bool:
        return self.power_pylon != TilePositions.Invalid and bool(self.worker_defense_cannons)


def _empty_build_locations() -> BuildLocations:
    return [[[] for _ in range(5)] for _ in range(NEIGHBOURHOOD_COUNT)]


def _tile_key(tile: TilePosition) -> Tile:
    return tile.x, tile.y


# Used in e.g. mining tests to force us to use start blocks for all starting locations
_use_start_blocks_for_all_starting_locations: bool | None = None

# Stores a bitmask for each tile
# 1: not buildable
# 2: adjacent to not buildable
# 4: reserved for block
# 8: adjacent to reserved for block
_tile_availability: list[int] = []

_start_block: Block | None = None
_base_to_start_block: dict[Base, Block] = {}
_blocks: list[Block] = []
_base_static_defenses: dict[Base, BaseStaticDefenseLocations] = {}
_empty_base_static_defenses = BaseStaticDefenseLocations(TilePositions.Invalid, [], TilePositions.Invalid)

_forge_gateway_wall: ForgeGatewayWall | None = None

_choke_cannon_block: Block | None = None
_choke_cannon_placement = TilePositions.Invalid

_update_required = True
_neighbourhood_areas: dict[Neighbourhood, set[bwem.Area]] = {}
_area_origins: dict[bwem.Area, Position] = {}
_area_exits: dict[bwem.Area, Position] = {}
_available_build_locations: BuildLocations = _empty_build_locations()

_build_away_from_exit = False
_hidden_base: Base | None = None

_available_geysers: BuildLocationSet = []


def _map_size() -> tuple[int, int]:
    game = bwapi.Broodwar
    return game.mapWidth(), game.mapHeight()


def _my_main() -> Base:
    main = game_map.get_my_main()
    assert main is not None
    return main


def _get_or_create_forge_gateway_wall() -> ForgeGatewayWall:
    global _forge_gateway_wall, _update_required

    if _forge_gateway_wall is None:
        wall = _forge_gateway_wall = create_forge_gateway_wall(True, _my_main())

        if wall.is_valid():
            # Remove any blocks that are overlapping the wall
            def overlaps(block: Block) -> bool:
                # Consider a buffer around the block
                top_left = block.top_left + TilePosition(-1, -1)
                width = block.width() + 2
                height = block.height() + 2

                def visit(tile: TilePosition, x: int, y: int) -> bool:
                    return geo.overlaps_tiles(tile, x, y, top_left, width, height)

                return (visit(wall.pylon, 2, 2) or visit(wall.forge, 3, 2) or visit(wall.gateway, 4, 3)
                        or any(visit(tile, 2, 2) for tile in wall.cannons)
                        or any(visit(tile, 2, 2) for tile in wall.natural_cannons))

            _blocks[:] = [block for block in _blocks if not overlaps(block)]

            _update_required = True

            if wall.natural_cannons:
                natural = game_map.map_specific_override().natural_for_wall_placement(_my_main())
                if natural is None:
                    natural = game_map.get_my_natural()
                assert natural is not None
                # (Stardust default-constructs the entry if it is missing, so its start block cannon is tile 0,0.)
                natural_defenses = _base_static_defenses.get(natural)
                if natural_defenses is None:
                    natural_defenses = _base_static_defenses[natural] = BaseStaticDefenseLocations(
                        TilePosition(0, 0), [], TilePosition(0, 0))
                natural_defenses.power_pylon = wall.pylon
                natural_defenses.worker_defense_cannons = list(wall.natural_cannons)

            if _CVIS_HEATMAPS:
                map_width, map_height = _map_size()
                wall_heatmap = [0] * (map_width * map_height)
                wall.add_to_heatmap(wall_heatmap)
                cherryvis.add_heatmap("Wall", wall_heatmap, map_width, map_height)
        else:
            log.get("WARNING: No wall available")

    return _forge_gateway_wall


def _initialize_tile_availability() -> None:
    game = bwapi.Broodwar
    map_width, map_height = _map_size()
    _tile_availability[:] = [0] * (map_width * map_height)
    availability = _tile_availability

    def mark_adjacent(tile_x: int, tile_y: int) -> None:
        if tile_x < 0 or tile_y < 0 or tile_x >= map_width or tile_y >= map_height:
            return
        availability[tile_x + tile_y * map_width] |= 2

    for tile_x in range(map_width):
        for tile_y in range(map_height):
            if not game_map.is_walkable(tile_x, tile_y) or not game.isBuildable(tile_x, tile_y):
                availability[tile_x + tile_y * map_width] = 1
                mark_adjacent(tile_x - 1, tile_y - 1)
                mark_adjacent(tile_x + 0, tile_y - 1)
                mark_adjacent(tile_x + 1, tile_y - 1)
                mark_adjacent(tile_x + 1, tile_y + 0)
                mark_adjacent(tile_x + 1, tile_y + 1)
                mark_adjacent(tile_x + 0, tile_y + 1)
                mark_adjacent(tile_x - 1, tile_y + 1)
                mark_adjacent(tile_x - 1, tile_y + 0)

    for base in game_map.all_bases():
        base_tile = base.get_tile_position()
        for tile_x in range(base_tile.x - 1, base_tile.x + 5):
            for tile_y in range(base_tile.y - 1, base_tile.y + 4):
                if tile_x < 0 or tile_y < 0 or tile_x >= map_width or tile_y >= map_height:
                    continue

                if (tile_x == base_tile.x - 1 or tile_y == base_tile.y - 1 or tile_x == base_tile.x + 4
                        or tile_y == base_tile.y + 3):
                    availability[tile_x + tile_y * map_width] = 2
                else:
                    availability[tile_x + tile_y * map_width] = 1

        # If the geyser is on the left, mark the top row and the row above it towards the nexus unbuildable
        # Building something here will interfere with gas collection
        for geyser_resource in base.geysers_or_refineries():
            geyser_tile = geyser_resource.tile

            if geyser_tile.x >= (base_tile.x - 2):
                continue
            if geyser_tile.y < base_tile.y:
                continue
            if geyser_tile.y > (base_tile.y + 3):
                continue

            for dx in (4, 5, 6):
                availability[geyser_tile.x + dx + geyser_tile.y * map_width] = 1
            for dx in (4, 5, 6):
                availability[geyser_tile.x + dx + (geyser_tile.y - 1) * map_width] = 1


def _update_neighbourhoods() -> None:
    _neighbourhood_areas.clear()
    _area_origins.clear()
    _area_exits.clear()

    main = _my_main()

    # Main base
    main_areas = set(game_map.get_my_main_areas())
    _neighbourhood_areas[Neighbourhood.MAIN_BASE] = main_areas
    game_map.map_specific_override().modify_main_base_building_placement_areas(main_areas)

    main_choke = game_map.get_my_main_choke()
    main_exit = main_choke.center if main_choke is not None else main.get_position()
    origin = main.mineral_line_center
    for area in main_areas:
        _area_origins[area] = origin
        _area_exits[area] = main_exit

    # All other owned bases
    # We use the main choke exit as the exit to promote building closer to our main
    all_my_bases_areas = set(main_areas)
    _neighbourhood_areas[Neighbourhood.ALL_MY_BASES] = all_my_bases_areas
    for base in game_map.get_my_bases():
        if base is main:
            continue
        if base.island:
            continue
        if base.resource_depot is None:
            continue

        all_my_bases_areas.add(base.get_area())
        _area_origins[base.get_area()] = base.mineral_line_center
        _area_exits[base.get_area()] = main_exit

    # Hidden base
    hidden_base = _hidden_base
    if hidden_base is not None:
        def add_area(area: bwem.Area) -> None:
            _neighbourhood_areas.setdefault(Neighbourhood.HIDDEN_BASE, set()).add(area)
            _area_origins[area] = hidden_base.mineral_line_center
            _area_exits[area] = hidden_base.mineral_line_center

        add_area(hidden_base.get_area())

        # Include nearby areas in case the base area itself has no building locations
        for choke in hidden_base.get_area().ChokePoints():
            if hidden_base.get_position().getApproxDistance(Position(choke.Center()) + Position(4, 4)) < 640:
                first, second = choke.GetAreas()
                add_area(first)
                add_area(second)


def _add_base_static_defense(base: Base, power_pylon: TilePosition, worker_defense_cannons: list[TilePosition],
                             start_block_cannon: TilePosition = TilePositions.Invalid) -> None:
    map_width, map_height = _map_size()

    def mark_location(tile: TilePosition) -> None:
        for tile_x in range(tile.x - 1, tile.x + 3):
            for tile_y in range(tile.y - 1, tile.y + 3):
                if tile_x < 0 or tile_y < 0 or tile_x >= map_width or tile_y >= map_height:
                    continue

                if tile_x == tile.x - 1 or tile_y == tile.y - 1 or tile_x == tile.x + 2 or tile_y == tile.y + 2:
                    _tile_availability[tile_x + tile_y * map_width] |= 8
                else:
                    _tile_availability[tile_x + tile_y * map_width] |= 4

    mark_location(power_pylon)
    for cannon in worker_defense_cannons:
        mark_location(cannon)

    # (std::map::emplace: an existing entry is kept.)
    if base not in _base_static_defenses:
        _base_static_defenses[base] = BaseStaticDefenseLocations(power_pylon, list(worker_defense_cannons),
                                                                 start_block_cannon)


def _find_start_block(base: Base) -> None:
    global _start_block

    invalid = TilePositions.Invalid
    start_blocks: list[Block] = [
        StartNormalLeft(invalid, invalid),
        StartNormalRight(invalid, invalid),
        StartCompactLeft(invalid, invalid),
        StartCompactRight(invalid, invalid),
        StartCompactLeftHorizontal(invalid, invalid),
        StartCompactRightVertical(invalid, invalid),
        StartBottomLeftHorizontal(invalid, invalid),
        StartTopLeftHorizontal(invalid, invalid),
        StartBottomHorizontal(invalid, invalid),
        StartAboveAndBelowLeft(invalid, invalid),
    ]

    for block_type in start_blocks:
        block = block_type.try_create(base.get_tile_position(), _tile_availability)
        if block is not None:
            if base is game_map.get_my_main():
                _start_block = block
            _base_to_start_block[base] = block
            _blocks.append(block)

            # Check if this start block has a location for a defensive cannon
            cannon = TilePositions.Invalid
            for location in block.small:
                if location.tile == block.power_pylon:
                    continue
                cannon = location.tile
                break

            _add_base_static_defense(base, block.power_pylon, block.cannons, cannon)
            return

    log.get(f"WARNING: No start block available for {base.get_tile_position()}")


def _find_base_static_defenses() -> None:
    for base in game_map.all_bases():
        # Main has its locations handled by the start block
        if base is game_map.get_my_main() and base in _base_static_defenses:
            main_defenses = _base_static_defenses[base]

            # Start by scoring the positions by how well they defend the mineral line
            main_choke = game_map.get_my_main_choke()
            choke_to_mineral_line_dist = (main_choke.center.getApproxDistance(base.mineral_line_center)
                                          if main_choke is not None else 0)
            tiles_and_score: list[tuple[int, Tile, TilePosition]] = []
            for tile in main_defenses.worker_defense_cannons:
                pos = Position(tile) + Position(16, 16)
                score = pos.getApproxDistance(base.mineral_line_center) * 4
                if base.geysers_or_refineries():
                    score += pos.getApproxDistance(base.geysers_or_refineries()[0].center)
                if main_choke is not None:
                    score += (main_choke.center.getApproxDistance(pos) - choke_to_mineral_line_dist) * 2
                tiles_and_score.append((score, _tile_key(tile), tile))
            tiles_and_score.sort(key=lambda entry: (entry[0], entry[1]))

            # Next find the position that best defends the start block
            assert _start_block is not None
            start_block_pylon_pos = Position(_start_block.power_pylon) + Position(16, 16)
            choke_to_start_block_dist = (main_choke.center.getApproxDistance(start_block_pylon_pos)
                                         if main_choke is not None else 0)
            best_start_block_tile = TilePositions.Invalid
            best_score = INT_MAX
            for tile in main_defenses.worker_defense_cannons:
                pos = Position(tile) + Position(16, 16)
                score = pos.getApproxDistance(start_block_pylon_pos)
                if main_choke is not None:
                    score += main_choke.center.getApproxDistance(pos) - choke_to_start_block_dist
                if score < best_score:
                    best_start_block_tile = tile
                    best_score = score

            # Now assign the locations
            # We use the scores for how well they defend the mineral line, but put the cannon that best defends the
            # start block second
            first_tile = tiles_and_score[0][2]
            ordered = [first_tile]
            if best_start_block_tile != TilePositions.Invalid and best_start_block_tile != first_tile:
                ordered.append(best_start_block_tile)
            for _, _, tile in tiles_and_score:
                if tile == first_tile:
                    continue
                if tile == best_start_block_tile:
                    continue
                ordered.append(tile)
            main_defenses.worker_defense_cannons = ordered

            continue

        end1, end2, positions = initialize_base_defense_analysis(base)
        if end1 is None or end2 is None or not positions:
            continue
        position_tiles = {_tile_key(tile) for tile in positions}

        def use_position(tile: TilePosition) -> None:
            for x in range(tile.x - 1, tile.x + 2):
                for y in range(tile.y - 1, tile.y + 2):
                    position_tiles.discard((x, y))

        cannons: list[TilePosition] = []

        # Now place a cannon closest to each end
        def place_end(end: Resource) -> None:
            min_dist = INT_MAX
            best: Tile | None = None
            for tile_x, tile_y in sorted(position_tiles):
                dist = end.center.getApproxDistance(Position(tile_x * 32 + 16, tile_y * 32 + 16))
                if dist < min_dist:
                    min_dist = dist
                    best = (tile_x, tile_y)

            if best is not None:
                best_tile = TilePosition(*best)
                use_position(best_tile)
                cannons.append(best_tile)

        place_end(end1)
        place_end(end2)
        if not cannons:
            continue

        # Now place the pylon so that it powers both and is as far as possible from minerals and geyser
        geyser_x = 0
        geyser_y = 0
        geyser_count = 0
        for geyser in base.geysers_or_refineries():
            geyser_x += geyser.center.x
            geyser_y += geyser.center.y
            geyser_count += 1
        geyser_pos = (Positions.Invalid if geyser_count == 0
                      else Position(geyser_x // geyser_count, geyser_y // geyser_count))

        pylon = TilePositions.Invalid
        max_dist = 0
        for tile_x, tile_y in sorted(position_tiles):
            tile = TilePosition(tile_x, tile_y)

            # First ensure the position powers the cannons
            if not all(unit_util.powers(tile, cannon, UnitTypes.Protoss_Photon_Cannon) for cannon in cannons):
                continue

            # Next check the distances
            pos = Position(tile) + Position(16, 16)
            dist = pos.getApproxDistance(base.mineral_line_center)
            if geyser_count > 0:
                # Weight minerals twice as high as geyser
                dist *= 2
                dist += pos.getApproxDistance(geyser_pos)

            if dist > max_dist:
                max_dist = dist
                pylon = tile

        if not pylon.isValid():
            continue

        use_position(pylon)

        # TODO: Place additional cannons

        _add_base_static_defense(base, pylon, cannons)


def _find_blocks() -> None:
    invalid = TilePositions.Invalid
    normal_blocks: list[Block] = [
        Block18x6(invalid, invalid),
        Block16x8(invalid, invalid),
        Block17x6(invalid, invalid),
        Block14x6(invalid, invalid),
        Block12x8(invalid, invalid),
        Block16x5(invalid, invalid),
        Block18x3(invalid, invalid),
        Block13x6(invalid, invalid),
        Block10x6(invalid, invalid),
        Block8x8(invalid, invalid),
        Block14x3(invalid, invalid),
        Block12x5(invalid, invalid),
        Block10x3(invalid, invalid),
        Block8x5(invalid, invalid),
        Block4x8(invalid, invalid),
        Block6x3(invalid, invalid),
        Block4x5(invalid, invalid),
        Block8x2(invalid, invalid),
        Block5x4(invalid, invalid),
        Block5x2(invalid, invalid),
        Block4x4(invalid, invalid),
        Block4x2(invalid, invalid),
        Block2x4(invalid, invalid),
        Block2x2(invalid, invalid),
    ]

    map_width, map_height = _map_size()
    availability = _tile_availability

    for block_type in normal_blocks:
        width = block_type.width()
        height = block_type.height()
        center_x = (map_width - width) // 2
        center_y = (map_height - height) // 2

        def try_create(tile_x: int, tile_y: int) -> None:
            # Cheap pre-check of the top-left tile, which place() would reject anyway
            if tile_x < map_width and tile_y < map_height and availability[tile_x + tile_y * map_width] > 0:
                return
            block = block_type.try_create(TilePosition(tile_x, tile_y), availability)
            if block is not None:
                _blocks.append(block)

        for tile_x in range(0, center_x + 1):
            for tile_y in range(0, center_y + 1):
                try_create(tile_x, tile_y)

        for tile_x in range(map_width - width, center_x, -1):
            for tile_y in range(0, center_y + 1):
                try_create(tile_x, tile_y)

        for tile_x in range(0, center_x + 1):
            for tile_y in range(map_height - height, center_y, -1):
                try_create(tile_x, tile_y)

        for tile_x in range(map_width - width, center_x, -1):
            for tile_y in range(map_height - height, center_y, -1):
                try_create(tile_x, tile_y)


def _find_main_choke_cannon_placement() -> None:
    global _choke_cannon_block, _choke_cannon_placement

    main_choke = game_map.get_my_main_choke()
    if main_choke is None:
        return

    bwem_map = bwem.Instance()
    main_areas = _neighbourhood_areas.setdefault(Neighbourhood.MAIN_BASE, set())
    map_width, map_height = _map_size()

    # Gather main base blocks
    # At the same time, find the closest small building location that has detection on the choke center
    main_blocks: list[Block] = []
    closest_block_location = TilePositions.Invalid
    closest_block_location_block: Block | None = None
    closest_dist = INT_MAX
    for block in _blocks:
        # Ignore non-main-base
        if bwem_map.GetArea(WalkPosition(block.center())) not in main_areas:
            continue

        main_blocks.append(block)

        # Search for a close small location
        for location in block.small:
            if location.tile == block.power_pylon:
                continue
            if not unit_util.powers(block.power_pylon, location.tile, UnitTypes.Protoss_Photon_Cannon):
                continue

            dist = geo.edge_to_point_distance(UnitTypes.Protoss_Photon_Cannon,
                                              Position(location.tile) + Position(32, 32), main_choke.center)
            if dist < 3 * 32 or dist > 8 * 32:
                continue

            if dist < closest_dist:
                closest_dist = dist
                closest_block_location = location.tile
                closest_block_location_block = block

    # TODO: Check if there is a 2x2 block that fits?

    # Try to find a location for a cannon that fulfills the following conditions:
    # - Is in our main
    # - Is within detection range of the choke center
    # - Is powered by some block
    # - Only borders unbuildable or reserved tiles on one side, and in the latter case, leaves room on at least one
    #   side
    def is_unbuildable_or_reserved(tile: TilePosition) -> bool:
        def value_at(offset_x: int, offset_y: int) -> int:
            x = tile.x + offset_x
            if x < 0 or x >= map_width:
                return 1
            y = tile.y + offset_y
            if y < 0 or y >= map_height:
                return 1
            if not game_map.is_walkable(x, y):
                return _tile_availability[x + y * map_width] | 16
            return _tile_availability[x + y * map_width]

        # Check for overlap with unbuildable or reserved for block
        if (value_at(0, 0) & 5) != 0:
            return True
        if (value_at(1, 0) & 5) != 0:
            return True
        if (value_at(0, 1) & 5) != 0:
            return True
        if (value_at(1, 1) & 5) != 0:
            return True

        # Check all of the tiles around the cannon, counting how many times walkability or reserved for block
        # changes. Accept if it changes at most twice and has 3 or fewer reserved.
        last_value = value_at(-1, -1) & 20
        changes = 0
        reserved = 0
        for offset_x, offset_y in ((0, -1), (1, -1), (2, -1), (2, 0), (2, 1), (2, 2), (1, 2), (0, 2), (-1, 2),
                                   (-1, 1), (-1, 0), (-1, -1)):
            here = value_at(offset_x, offset_y) & 20
            if here != last_value:
                last_value = here
                changes += 1
            if (here & 4) != 0:
                reserved += 1

        return changes > 2 or reserved > 3

    closest_location = TilePositions.Invalid
    closest_location_block: Block | None = None
    choke_tile = TilePosition(main_choke.center)
    for x in range(choke_tile.x - 9, choke_tile.x + 10):
        for y in range(choke_tile.y - 9, choke_tile.y + 10):
            here = TilePosition(x, y)
            if not here.isValid():
                continue

            # Ensure it is in detection range of the choke center
            dist = geo.edge_to_point_distance(UnitTypes.Protoss_Photon_Cannon, Position(here) + Position(32, 32),
                                              main_choke.center)
            if dist < 3 * 32 or dist > 7 * 32:
                continue
            if dist > closest_dist:
                continue

            # Ensure it is in the main area
            if bwem_map.GetArea(WalkPosition(here) + WalkPosition(4, 4)) not in main_areas:
                continue

            # Ensure it can fit here
            if is_unbuildable_or_reserved(here):
                continue

            # Find the furthest pylon that powers it
            furthest_power_dist = 0
            furthest_power_block: Block | None = None
            for block in main_blocks:
                if not unit_util.powers(block.power_pylon, here, UnitTypes.Protoss_Photon_Cannon):
                    continue

                power_dist = Position(block.power_pylon).getApproxDistance(Position(here))
                if power_dist > furthest_power_dist:
                    furthest_power_dist = power_dist
                    furthest_power_block = block
            if furthest_power_block is None:
                continue

            closest_dist = dist
            closest_location = here
            closest_location_block = furthest_power_block

    # Use the best result
    if closest_location.isValid():
        _choke_cannon_placement = closest_location
        _choke_cannon_block = closest_location_block
    elif closest_block_location.isValid():
        assert closest_block_location_block is not None
        closest_block_location_block.tiles_reserved(closest_block_location,
                                                    UnitTypes.Protoss_Photon_Cannon.tileSize(), True)
        _choke_cannon_placement = closest_block_location
        _choke_cannon_block = closest_block_location_block
        if closest_dist > 7 * 32:
            log.get("WARNING: Best choke cannon placement is a bit too far away")
    else:
        log.get("WARNING: No choke cannon placement")


def _distance_to_exit(neighbourhood: Neighbourhood, exit_position: Position, tile: TilePosition,
                      unit_type: UnitType) -> int:
    dist = path_finding.get_ground_distance(Position(tile) + Position(unit_type.tileSize()) / 2, exit_position,
                                            UnitTypes.Protoss_Dragoon, PathFindingOptions.UseNearestBWEMArea)

    # For the main base neighbourhood, prefer not to place large buildings too close to the exit
    # Rationale: the main base exit is probably a choke and we don't want to have our buildings get in the way of our
    # choke defense
    if unit_type.tileWidth() == 4 and neighbourhood == Neighbourhood.MAIN_BASE and dist < 320:
        dist = 320 + (320 - dist)

    return dist


def _powered_after(tile: TilePosition, unit_type: UnitType, pending_pylons: list[Building]) -> int:
    """At how many frames from now the build position will be powered: 0 if already powered, the frames until the
    pylon completes if it will be powered by a pending pylon, otherwise -1."""
    if bwapi.Broodwar.hasPower(tile, unit_type):
        return 0

    result = -1
    for pending_pylon in pending_pylons:
        if (unit_util.powers(pending_pylon.tile, tile, unit_type)
                and (result == -1 or pending_pylon.expected_frames_until_completion() < result)):
            result = pending_pylon.expected_frames_until_completion()

    return result


def build_location_key(location: BuildLocation) -> tuple[bool, bool, bool, int, bool, bool, int, Tile]:
    """BuildLocationCmp as a sort key."""
    tile = location.location.tile

    # Always sort the start block pylon before everything else
    is_start_block_pylon = _start_block is not None and _start_block.power_pylon == tile

    # If our enemy is Protoss, prioritize the cannon choke power pylon next
    # We use this for detection on our choke in reaction to DTs
    is_choke_cannon_pylon = (_choke_cannon_block is not None
                             and bwapi.Broodwar.enemy().getRace() == Races.Protoss
                             and not _build_away_from_exit
                             and _choke_cannon_block.power_pylon == tile)

    # Prioritize main geyser first
    is_main_geyser = _my_main().has_geyser_or_refinery_at(tile)

    # Finally prioritize based on distance
    # We weight builder distance more than distance from exit
    # Tech buildings prefer further away from the exit
    if location.is_tech or _build_away_from_exit:
        dist = location.builder_frames * 2 - location.distance_to_exit
    else:
        dist = location.builder_frames * 2 + location.distance_to_exit

    return (not is_start_block_pylon,
            not is_choke_cannon_pylon,
            not is_main_geyser,
            # Prioritize locations that will be powered first
            location.frames_until_powered,
            # For medium locations, prioritize locations without an exit first - the producer will keep looking if it
            # needs to place a building with an exit
            location.location.has_exit,
            # Then prioritize locations that have not been converted from a larger position
            location.location.converted,
            dist,
            (tile.x, tile.y))


def _update_available_build_locations() -> None:
    """Rebuilds the map of available build locations."""
    from stardust.builder import builder

    global _available_build_locations

    result = _empty_build_locations()
    bwem_map = bwem.Instance()

    # Gather our pending pylons
    pending_pylons = builder.pending_buildings_of_type(UnitTypes.Protoss_Pylon)

    # Not in Stardust: the same builder frames and exit distances are asked for many times in one update (e.g. every
    # pylon location uses its block's first one), so they are computed once each here
    builder_frames_cache: dict[tuple[Position, TilePosition, UnitType], int] = {}
    exit_distance_cache: dict[tuple[Neighbourhood, Position, TilePosition, UnitType], int] = {}

    def cached_builder_frames(origin: Position, tile: TilePosition, unit_type: UnitType) -> int:
        key = (origin, tile, unit_type)
        frames = builder_frames_cache.get(key)
        if frames is None:
            frames = builder_frames_cache[key] = builder_frames(origin, tile, unit_type)
        return frames

    def cached_distance_to_exit(neighbourhood: Neighbourhood, exit_position: Position, tile: TilePosition,
                                unit_type: UnitType) -> int:
        key = (neighbourhood, exit_position, tile, unit_type)
        dist = exit_distance_cache.get(key)
        if dist is None:
            dist = exit_distance_cache[key] = _distance_to_exit(neighbourhood, exit_position, tile, unit_type)
        return dist

    # Scan blocks to:
    # - Collect the powered (or soon-to-be-powered) medium and large build locations we have available
    # - Collect the next pylon to be built in each block
    for block in _blocks:
        # Consider medium building positions
        powered_medium: list[tuple[Location, int]] = []
        unpowered_medium: list[Location] = []
        for placement in block.medium:
            frames_to_power = _powered_after(placement.tile, UnitTypes.Protoss_Forge, pending_pylons)
            if frames_to_power == -1:
                unpowered_medium.append(placement)
            else:
                powered_medium.append((placement, frames_to_power))

        # Consider large building positions
        powered_large: list[tuple[Location, int]] = []
        unpowered_large: list[Location] = []
        for placement in block.large:
            frames_to_power = _powered_after(placement.tile, UnitTypes.Protoss_Gateway, pending_pylons)
            if frames_to_power == -1:
                unpowered_large.append(placement)
            else:
                powered_large.append((placement, frames_to_power))

        # If the block is full, continue now
        if not block.small and not powered_medium and not powered_large:
            continue

        area = bwem_map.GetArea(WalkPosition(block.center()))

        # Add data from the block to appropriate neighbourhoods
        for neighbourhood in ALL_NEIGHBOURHOODS:
            # Make sure we don't die if we for some reason have an unconfigured neighbourhood
            areas = _neighbourhood_areas.get(neighbourhood)
            if areas is None:
                continue

            # Check if this block fits in this neighbourhood
            if area not in areas:
                continue

            # Get the origin and exit
            assert area is not None
            origin = _area_origins.get(area)
            if origin is None:
                continue
            exit_position = _area_exits.get(area, origin)

            # Add pylons
            # (Stardust uses the block's first pylon location for the builder frames and exit distance of all of them.)
            for pylon_location in block.small:
                first_small_tile = block.small[0].tile
                pylon = BuildLocation(pylon_location,
                                      cached_builder_frames(origin, first_small_tile, UnitTypes.Protoss_Pylon),
                                      0,
                                      cached_distance_to_exit(neighbourhood, exit_position, first_small_tile,
                                                        UnitTypes.Protoss_Pylon))

                if pylon_location.tile == block.power_pylon:
                    for location in unpowered_medium:
                        pylon.powers_medium.append(BuildLocation(
                            location,
                            cached_builder_frames(origin, location.tile, UnitTypes.Protoss_Forge),
                            0,
                            cached_distance_to_exit(neighbourhood, exit_position, location.tile,
                                                    UnitTypes.Protoss_Forge),
                            True))
                    for location in unpowered_large:
                        pylon.powers_large.append(BuildLocation(
                            location,
                            cached_builder_frames(origin, location.tile, UnitTypes.Protoss_Gateway),
                            0,
                            cached_distance_to_exit(neighbourhood, exit_position, location.tile,
                                              UnitTypes.Protoss_Gateway)))

                result[neighbourhood][2].append(pylon)

            for location, powered_at in powered_medium:
                result[neighbourhood][3].append(BuildLocation(
                    location,
                    cached_builder_frames(origin, location.tile, UnitTypes.Protoss_Forge),
                    powered_at,
                    cached_distance_to_exit(neighbourhood, exit_position, location.tile, UnitTypes.Protoss_Forge),
                    True))

            for location, powered_at in powered_large:
                result[neighbourhood][4].append(BuildLocation(
                    location,
                    cached_builder_frames(origin, location.tile, UnitTypes.Protoss_Gateway),
                    powered_at,
                    cached_distance_to_exit(neighbourhood, exit_position, location.tile, UnitTypes.Protoss_Gateway)))

    for neighbourhood_locations in result:
        for size in range(2, 5):
            neighbourhood_locations[size].sort(key=build_location_key)

    _available_build_locations = result


def _update_frames_until_powered() -> None:
    from stardust.builder import builder

    # Gather our pending pylons
    pending_pylons = builder.pending_buildings_of_type(UnitTypes.Protoss_Pylon)

    # Not in Stardust: the same builder frames and exit distances are asked for many times in one update (e.g. every
    # pylon location uses its block's first one), so they are computed once each here
    builder_frames_cache: dict[tuple[Position, TilePosition, UnitType], int] = {}
    exit_distance_cache: dict[tuple[Neighbourhood, Position, TilePosition, UnitType], int] = {}

    def cached_builder_frames(origin: Position, tile: TilePosition, unit_type: UnitType) -> int:
        key = (origin, tile, unit_type)
        frames = builder_frames_cache.get(key)
        if frames is None:
            frames = builder_frames_cache[key] = builder_frames(origin, tile, unit_type)
        return frames

    def cached_distance_to_exit(neighbourhood: Neighbourhood, exit_position: Position, tile: TilePosition,
                                unit_type: UnitType) -> int:
        key = (neighbourhood, exit_position, tile, unit_type)
        dist = exit_distance_cache.get(key)
        if dist is None:
            dist = exit_distance_cache[key] = _distance_to_exit(neighbourhood, exit_position, tile, unit_type)
        return dist

    # Loop and update every location with a current frames_until_powered value
    for neighbourhood_locations in _available_build_locations:
        for size in (3, 4):
            locations = neighbourhood_locations[size]

            anything_updated = False
            for location in locations:
                if location.frames_until_powered > 0:
                    anything_updated = True
                    location.frames_until_powered = _powered_after(
                        location.location.tile,
                        UnitTypes.Protoss_Forge if size == 3 else UnitTypes.Protoss_Gateway,
                        pending_pylons)

            if anything_updated:
                locations.sort(key=build_location_key)


def _update_available_geysers() -> None:
    from stardust.builder import builder

    _available_geysers.clear()

    me = bwapi.Broodwar.self()
    for base in game_map.all_bases():
        if base.owner != me:
            continue
        depot = base.resource_depot
        if depot is None:
            continue
        if (not depot.completed and (depot.estimated_completion_frame - common.current_frame)
                > unit_util.build_time(UnitTypes.Protoss_Assimilator)):
            continue

        for geyser in base.geysers_or_refineries():
            if geyser.refinery is not None:
                continue
            if builder.is_pending_here(geyser.tile):
                continue

            # TODO: Order in some logical way
            _available_geysers.append(BuildLocation(Location(geyser.tile), 0, 0, 0))


def _dump_heatmap() -> None:
    """Values: no building 0, large building 2, medium building 3, pylon 4, defensive location 5 (8 for the first),
    choke cannon placement 10."""
    if not _CVIS_HEATMAPS:
        return

    map_width, map_height = _map_size()
    blocks_heatmap = [0] * (map_width * map_height)

    def add_location(tile: TilePosition, width: int, height: int, value: int) -> None:
        for y in range(tile.y, tile.y + height):
            if y > map_height - 1:
                log.get(f"ERROR: BUILD LOCATION OUT OF BOUNDS @ {tile}")
                continue
            for x in range(tile.x, tile.x + width):
                if x > map_width - 1:
                    log.get(f"ERROR: BUILD LOCATION OUT OF BOUNDS @ {tile}")
                    continue
                blocks_heatmap[x + y * map_width] = value

    for block in _blocks:
        for placement in block.large:
            if not placement.converted:
                add_location(placement.tile, 4, 3, 2)
        for placement in block.medium:
            if not placement.converted:
                add_location(placement.tile, 3, 2, 3)
        for placement in block.small:
            if not placement.converted:
                add_location(placement.tile, 2, 2, 4)

    for static_defenses in _base_static_defenses.values():
        add_location(static_defenses.power_pylon, 2, 2, 4)
        for index, cannon in enumerate(static_defenses.worker_defense_cannons):
            add_location(cannon, 2, 2, 8 if index == 0 else 5)

    if _choke_cannon_placement.isValid():
        add_location(_choke_cannon_placement, 2, 2, 10)

    cherryvis.add_heatmap("Blocks", blocks_heatmap, map_width, map_height)


def initialize() -> None:
    global _start_block, _choke_cannon_block, _choke_cannon_placement, _update_required, _build_away_from_exit
    global _hidden_base, _forge_gateway_wall, _available_build_locations

    _neighbourhood_areas.clear()
    _area_origins.clear()
    _area_exits.clear()
    _tile_availability.clear()
    _start_block = None
    _base_to_start_block.clear()
    _blocks.clear()
    _base_static_defenses.clear()
    _choke_cannon_block = None
    _choke_cannon_placement = TilePositions.Invalid
    _update_required = True
    _available_geysers.clear()
    _available_build_locations = _empty_build_locations()
    _build_away_from_exit = False
    _hidden_base = None
    _forge_gateway_wall = None

    _initialize_tile_availability()
    _update_neighbourhoods()
    if _use_start_blocks_for_all_starting_locations is True:
        for base in game_map.all_starting_locations():
            _find_start_block(base)
    elif _use_start_blocks_for_all_starting_locations is None:
        _find_start_block(_my_main())
    _find_base_static_defenses()
    _find_blocks()
    _find_main_choke_cannon_placement()

    if bwapi.Broodwar.enemy().getRace() == Races.Zerg:
        _get_or_create_forge_gateway_wall()

    _dump_heatmap()


def on_building_queued(building: Building) -> None:
    global _update_required
    for block in _blocks:
        _update_required = block.tiles_reserved(building.tile, building.type.tileSize()) or _update_required


def on_building_cancelled(building: Building) -> None:
    global _update_required
    for block in _blocks:
        _update_required = block.tiles_freed(building.tile, building.type.tileSize()) or _update_required


def on_unit_create(unit: Unit) -> None:
    global _update_required
    if not unit.type.isBuilding():
        return

    for block in _blocks:
        _update_required = block.tiles_used(unit.get_tile_position(), unit.type.tileSize()) or _update_required

    # Creation of depots indicates we've taken a new base
    _update_required = (unit.player == bwapi.Broodwar.self() and unit.type.isResourceDepot()) or _update_required


def on_unit_destroy(unit: Unit) -> None:
    global _update_required
    if not unit.type.isBuilding():
        return

    for block in _blocks:
        _update_required = block.tiles_freed(unit.get_tile_position(), unit.type.tileSize()) or _update_required

    # Destruction of depots indicates we've lost a base
    _update_required = _update_required or (unit.player == bwapi.Broodwar.self() and unit.type.isResourceDepot())


def on_main_choke_changed() -> None:
    global _update_required
    _find_main_choke_cannon_placement()
    _update_required = True


def update() -> None:
    import stardust.strategist.strategist as strategist

    global _build_away_from_exit, _hidden_base, _update_required

    # Build away from the exit if the enemy has units in our base or is doing a rush
    strategy_engine = strategist.get_strategy_engine()
    new_build_away_from_exit = ((strategy_engine is not None and strategy_engine.is_enemy_rushing())
                                or bool(units.enemy_at_base(_my_main())))
    if new_build_away_from_exit != _build_away_from_exit:
        _build_away_from_exit = new_build_away_from_exit
        _update_required = True

    if _hidden_base is None:
        _hidden_base = game_map.get_hidden_base()
        if _hidden_base is not None:
            _update_required = True

    if _update_required:
        _update_neighbourhoods()
        _update_available_build_locations()
        _update_required = False
    else:
        # We still need to update frames_until_powered each frame
        _update_frames_until_powered()

    _update_available_geysers()


def get_build_locations() -> BuildLocations:
    return _available_build_locations


def available_geysers() -> BuildLocationSet:
    return _available_geysers


def builder_frames(origin: Position, tile: TilePosition, unit_type: UnitType) -> int:
    """Approximately how many frames it will take a builder to reach the given build position."""
    # TODO: Update location origins depending on whether there are workers at a base
    return path_finding.expected_travel_time(origin, Position(tile) + Position(unit_type.tileSize()) / 2,
                                             bwapi.Broodwar.self().getRace().getWorker(),
                                             PathFindingOptions.UseNearestBWEMArea)


def base_static_defense_locations(base: Base | None) -> BaseStaticDefenseLocations:
    if base is None:
        return _empty_base_static_defenses
    return _base_static_defenses.get(base, _empty_base_static_defenses)


def main_block_static_defense_location() -> TilePosition:
    if _start_block is None:
        return TilePositions.Invalid

    for location in _start_block.small:
        if location.tile == _start_block.power_pylon:
            continue
        return location.tile

    return TilePositions.Invalid


def main_choke_cannon_locations() -> tuple[TilePosition, TilePosition]:
    """The power pylon and cannon location for detection at the main choke."""
    return (_choke_cannon_block.power_pylon if _choke_cannon_block is not None else TilePositions.Invalid,
            _choke_cannon_placement)


def is_in_neighbourhood(build_tile: TilePosition, neighbourhood: Neighbourhood) -> bool:
    if not build_tile.isValid():
        return False

    # Return true when we don't know where a neighbourhood is
    # Otherwise we might lock up completely if we ask for a location in an uninitialized neighbourhood
    areas = _neighbourhood_areas.get(neighbourhood)
    if areas is None:
        return True

    # Find the block containing the build tile
    for block in _blocks:
        if build_tile.x < block.top_left.x:
            continue
        if build_tile.x >= (block.top_left.x + block.width()):
            continue
        if build_tile.y < block.top_left.y:
            continue
        if build_tile.y >= (block.top_left.y + block.height()):
            continue

        return bwem.Instance().GetArea(WalkPosition(block.center())) in areas

    # Check if the tile is a nexus
    for base in game_map.all_bases():
        if build_tile != base.get_tile_position():
            continue

        if neighbourhood == Neighbourhood.MAIN_BASE:
            return base is game_map.get_my_main()
        if neighbourhood == Neighbourhood.ALL_MY_BASES:
            return True
        return base is game_map.get_hidden_base()

    log.get(f"WARNING: Tile {build_tile} not in a block")
    return False


def initialize_base_defense_analysis(base: Base) -> tuple[Resource | None, Resource | None, set[TilePosition]]:
    """The two end mineral patches of the base and the 2x2 tiles around the nexus where static defense fits."""
    # Find the end mineral patches, which are the patches furthest away from each other
    end1: Resource | None = None
    end2: Resource | None = None
    max_dist = 0
    patches = base.mineral_patches()
    for first in patches:
        for second in patches:
            dist = first.get_distance(second)
            if dist > max_dist:
                max_dist = dist
                end1 = first
                end2 = second

    # If for whatever reason this base has no mineral patches, continue
    if max_dist == 0:
        return None, None, set()

    map_width = bwapi.Broodwar.mapWidth()
    positions: set[TilePosition] = set()

    def add_position_if_valid(top_left: TilePosition) -> None:
        for y in range(top_left.y, top_left.y + 2):
            for x in range(top_left.x, top_left.x + 2):
                if not TilePosition(x, y).isValid():
                    return
                if (_tile_availability[x + y * map_width] & 1) == 1:
                    return
        positions.add(top_left)

    base_tile = base.get_tile_position()
    for x in range(-2, 5):
        add_position_if_valid(base_tile + TilePosition(x, -2))
        add_position_if_valid(base_tile + TilePosition(x, 3))
    for y in range(-1, 3):
        add_position_if_valid(base_tile + TilePosition(-2, y))
        add_position_if_valid(base_tile + TilePosition(4, y))

    return end1, end2, positions


def has_forge_gateway_wall() -> bool:
    return _get_or_create_forge_gateway_wall().is_valid()


def get_forge_gateway_wall() -> ForgeGatewayWall:
    return _get_or_create_forge_gateway_wall()


def set_use_start_blocks_for_all_starting_locations(value: bool) -> None:
    global _use_start_blocks_for_all_starting_locations
    _use_start_blocks_for_all_starting_locations = value


def start_block_for_base(base: Base) -> Block | None:
    return _base_to_start_block.get(base)


# ---------------------------------------------------------------------------------------------------------------------
# Forge/Gateway wall (BuildingPlacement_Walls.cpp)

_natural: Base | None = None
_main_choke: Choke | None = None
_natural_choke: Choke | None = None


class _PylonOption:
    __slots__ = ("pylon", "cannons", "score")

    def __init__(self, pylon: TilePosition, cannons: list[TilePosition], score: int) -> None:
        self.pylon = pylon
        self.cannons = cannons  # sorted by (x, y), like the std::set
        self.score = score


_pylon_options: list[_PylonOption] = []

_pathfinding_start_tile = TilePositions.Invalid
_pathfinding_end_tile = TilePositions.Invalid
_wall_tiles: set[Tile] = set()
_reserved_tiles: set[Tile] = set()
_neutral_walk_tiles: set[Tile] = set()
_mineral_field_tiles: set[Tile] = set()


class _ForgeGatewayWallOption:
    """Used when generating and scoring all of the forge + gateway options."""

    __slots__ = ("forge", "gateway", "gap_size", "gap_center", "gap_end1", "gap_end2")

    def __init__(self, forge: TilePosition = TilePositions.Invalid, gateway: TilePosition = TilePositions.Invalid,
                 gap_size: int = INT_MAX, gap_center: Position = Positions.Invalid,
                 gap_end1: Position = Positions.Invalid, gap_end2: Position = Positions.Invalid) -> None:
        self.forge = forge
        self.gateway = gateway
        self.gap_size = gap_size
        self.gap_center = gap_center
        self.gap_end1 = gap_end1
        self.gap_end2 = gap_end2

    def to_wall(self) -> ForgeGatewayWall:
        return ForgeGatewayWall(self.forge, self.gateway, TilePositions.Invalid, self.gap_size, self.gap_center,
                                self.gap_end1, self.gap_end2)


def _wall_natural() -> Base:
    assert _natural is not None
    return _natural


def _add_building_to_reserved_tiles(tile: TilePosition, unit_type: UnitType) -> None:
    for x in range(tile.x, tile.x + unit_type.tileWidth()):
        for y in range(tile.y, tile.y + unit_type.tileHeight()):
            _reserved_tiles.add((x, y))


def _walkable_tile(x: int, y: int) -> bool:
    game = bwapi.Broodwar
    return (0 <= x < game.mapWidth() and 0 <= y < game.mapHeight()
            and game_map.is_walkable(x, y)
            and (x, y) not in _reserved_tiles
            and (x, y) not in _wall_tiles)


def _buildable_tile(x: int, y: int) -> bool:
    return _walkable_tile(x, y) and bwapi.Broodwar.isBuildable(x, y)


def _buildable(unit_type: UnitType, tile: TilePosition) -> bool:
    width = unit_type.tileWidth()
    height = unit_type.tileHeight()
    for y in range(tile.y, tile.y + height):
        for x in range(tile.x, tile.x + width):
            if not _buildable_tile(x, y):
                return False

    # Also consider it not buildable if it is flush against one of the natural mineral fields
    for y in range(height):
        if (tile.x - 1, tile.y + y) in _mineral_field_tiles:
            return False
        if (tile.x + width, tile.y + y) in _mineral_field_tiles:
            return False
    for x in range(width):
        if (tile.x + x, tile.y - 1) in _mineral_field_tiles:
            return False
        if (tile.x + x, tile.y + height) in _mineral_field_tiles:
            return False

    return True


def _generate_pylon_options() -> None:
    _pylon_options.clear()
    natural = _wall_natural()
    assert _natural_choke is not None
    natural_tile = natural.get_tile_position()

    # Determine what direction the choke is in compared to the natural
    direction = geo.direction_from_building(natural_tile, UnitTypes.Protoss_Nexus.tileSize(), _natural_choke.center,
                                            True)

    # Generate the pylon positions we want to consider
    offsets: list[Tile]
    if direction == geo.Direction.up:
        offsets = [(-2, -1), (-1, -2), (0, -2), (1, -2), (2, -2), (3, -2), (4, -2), (4, -1)]
    elif direction == geo.Direction.down:
        offsets = [(-2, 2), (-2, 3), (-1, 3), (0, 3), (1, 3), (2, 3), (3, 3), (4, 2), (4, 3), (5, 2), (5, 3)]
    elif direction == geo.Direction.left:
        offsets = [(x, y) for x in range(-3, -1) for y in range(-1, 4)]
    elif direction == geo.Direction.right:
        offsets = [(x, y) for x in range(4, 6) for y in range(-2, 4)]
    else:
        return
    options = sorted({(natural_tile.x + dx, natural_tile.y + dy) for dx, dy in offsets})

    end1, end2, initial_positions = initialize_base_defense_analysis(natural)
    if end1 is None or end2 is None or not initial_positions:
        return
    initial_position_tiles = {_tile_key(tile) for tile in initial_positions}

    # Now consider each possibility and score them based on how well-placed the cannons can be
    for pylon_x, pylon_y in options:
        pylon = TilePosition(pylon_x, pylon_y)

        # Skip pylons in the mineral line or unbuildable
        if not _buildable(UnitTypes.Protoss_Pylon, pylon):
            continue
        if natural.is_in_mineral_line(pylon) or natural.is_in_mineral_line(pylon + TilePosition(1, 1)):
            continue

        # Create copy of positions set so we can remove invalid options
        positions = set(initial_position_tiles)

        def use_position(tile: Tile) -> None:
            for x in range(tile[0] - 1, tile[0] + 2):
                for y in range(tile[1] - 1, tile[1] + 2):
                    positions.discard((x, y))

        use_position((pylon_x, pylon_y))

        cannons: set[Tile] = set()

        # Now place a cannon closest to each end
        def place_end(end: Resource) -> int:
            min_dist = INT_MAX
            best: Tile | None = None
            for tile in sorted(positions):
                if not unit_util.powers(pylon, TilePosition(*tile), UnitTypes.Protoss_Photon_Cannon):
                    continue

                dist = end.center.getApproxDistance(Position(tile[0] * 32 + 16, tile[1] * 32 + 16))
                if dist < min_dist:
                    min_dist = dist
                    best = tile

            if best is not None:
                use_position(best)
                cannons.add(best)

            return min_dist

        score = place_end(end1)
        score += place_end(end2)
        if len(cannons) == 2:
            _pylon_options.append(_PylonOption(pylon, [TilePosition(*tile) for tile in sorted(cannons)], score))

    _pylon_options.sort(key=lambda option: option.score)


def _add_wall_tiles(tile: TilePosition, size: TilePosition) -> None:
    for x in range(tile.x, tile.x + size.x):
        for y in range(tile.y, tile.y + size.y):
            _wall_tiles.add((x, y))


def _remove_wall_tiles(tile: TilePosition, size: TilePosition) -> None:
    for x in range(tile.x, tile.x + size.x):
        for y in range(tile.y, tile.y + size.y):
            _wall_tiles.discard((x, y))


def _valid_pathfinding_tile(tile: TilePosition) -> bool:
    return _walkable_tile(tile.x, tile.y) and tile not in _wall_natural().mineral_line_tiles


def _path_length(alternate_start_tile: TilePosition = TilePositions.Invalid) -> int:
    start_tile = _pathfinding_start_tile
    if alternate_start_tile != TilePositions.Invalid:
        start_tile = alternate_start_tile

    return len(path_finding.search(start_tile, _pathfinding_end_tile, _valid_pathfinding_tile))


def _has_path_with_building(tile: TilePosition, size: TilePosition, max_path_length: int = 0,
                            alternate_start_tile: TilePosition = TilePositions.Invalid) -> bool:
    _add_wall_tiles(tile, size)
    length = _path_length(alternate_start_tile)
    _remove_wall_tiles(tile, size)

    if length == 0:
        return False
    if max_path_length > 0 and length > max_path_length:
        return False

    return True


def _center(tile: TilePosition) -> Position:
    return Position(tile) + Position(16, 16)


def _walkable_walk(walk_x: int, walk_y: int) -> bool:
    game = bwapi.Broodwar
    return (0 <= walk_x < game.mapWidth() * 4 and 0 <= walk_y < game.mapHeight() * 4
            and game.isWalkable(walk_x, walk_y) and (walk_x, walk_y) not in _neutral_walk_tiles)


def _walkable_above(tile: TilePosition, tolerance_above: int, tolerance_left: int, tolerance_right: int) -> bool:
    if not tile.isValid():
        return False
    start_x = tile.x * 4
    start_y = tile.y * 4
    return all(_walkable_walk(start_x + x, start_y - y)
               for x in range(-tolerance_left, 4 + tolerance_right) for y in range(1, tolerance_above + 1))


def _walkable_below(tile: TilePosition, tolerance_below: int, tolerance_left: int, tolerance_right: int) -> bool:
    if not tile.isValid():
        return False
    start_x = tile.x * 4
    start_y = tile.y * 4
    return all(_walkable_walk(start_x + x, start_y + 3 + y)
               for x in range(-tolerance_left, 4 + tolerance_right) for y in range(1, tolerance_below + 1))


def _walkable_left(tile: TilePosition, tolerance_left: int, tolerance_above: int, tolerance_below: int) -> bool:
    if not tile.isValid():
        return False
    start_x = tile.x * 4
    start_y = tile.y * 4
    return all(_walkable_walk(start_x - x, start_y + y)
               for y in range(-tolerance_above, 4 + tolerance_below) for x in range(1, tolerance_left + 1))


def _walkable_right(tile: TilePosition, tolerance_right: int, tolerance_above: int, tolerance_below: int) -> bool:
    if not tile.isValid():
        return False
    start_x = tile.x * 4
    start_y = tile.y * 4
    return all(_walkable_walk(start_x + 3 + x, start_y + y)
               for y in range(-tolerance_above, 4 + tolerance_below) for x in range(1, tolerance_right + 1))


def _add_building_option(x: int, y: int, building: UnitType, building_options: set[Tile], tight: bool) -> None:
    # Collect the possible build locations covering this tile
    tiles: set[Tile] = set()

    is_forge = building == UnitTypes.Protoss_Forge
    is_gate = building == UnitTypes.Protoss_Gateway
    here = TilePosition(x, y)

    # Blocked on top
    if (is_forge and not _walkable_above(here, 1, 1, 1)) or (not tight and not game_map.is_terrain_walkable(x, y - 1)):
        for i in range(building.tileWidth()):
            tiles.add((x - i, y))

    # Blocked on left
    if (is_forge and not _walkable_left(here, 1, 1, 1)) or (not tight and not game_map.is_terrain_walkable(x - 1, y)):
        for i in range(building.tileHeight()):
            tiles.add((x, y - i))

    # Blocked on bottom
    if ((is_forge and not _walkable_below(here, 1, 1, 1))
            or (is_gate and not _walkable_below(here, 2, 0, 1))
            or (not tight and not game_map.is_terrain_walkable(x, y + 1))):
        this_y = y - building.tileHeight() + 1
        for i in range(building.tileWidth()):
            tiles.add((x - i, this_y))

    # Blocked on right
    if ((is_forge and not _walkable_right(here, 1, 1, 1))
            or (is_gate and not _walkable_right(here, 1, 0, 2))
            or (not tight and not game_map.is_terrain_walkable(x + 1, y))):
        this_x = x - building.tileWidth() + 1
        for i in range(building.tileHeight()):
            tiles.add((this_x, y - i))

    # Add all valid positions to the options set
    for tile_x, tile_y in tiles:
        tile = TilePosition(tile_x, tile_y)
        if not tile.isValid():
            continue
        if not _buildable(building, tile):
            continue
        building_options.add((tile_x, tile_y))


def _add_building_geo(tile: TilePosition, unit_type: UnitType, geo_tiles: set[Tile]) -> None:
    # Compute walkable pixels in each dimension
    pixels_left = (unit_type.tileWidth() * 16) - unit_type.dimensionLeft()
    pixels_right = (unit_type.tileWidth() * 16) - unit_type.dimensionRight() - 1
    pixels_top = (unit_type.tileHeight() * 16) - unit_type.dimensionUp()
    pixels_bottom = (unit_type.tileHeight() * 16) - unit_type.dimensionDown() - 1

    # Compute offset with first fully-unwalkable walktile in each dimension
    left = (pixels_left + 7) // 8
    right = (unit_type.tileWidth() * 4) - ((pixels_right + 7) // 8) - 1
    top = (pixels_top + 7) // 8
    bottom = (unit_type.tileHeight() * 4) - ((pixels_bottom + 7) // 8) - 1

    start_x = tile.x * 4
    start_y = tile.y * 4

    # Top and bottom rows
    for x in range(left, right + 1):
        geo_tiles.add((start_x + x, start_y + top))
        geo_tiles.add((start_x + x, start_y + bottom))

    # Left and right columns, ignoring corners that were already handled above
    for y in range(top + 1, bottom):
        geo_tiles.add((start_x + left, start_y + y))
        geo_tiles.add((start_x + right, start_y + y))


def _walk_centers(walk_tiles: set[Tile]) -> np.ndarray:
    """Centers of the walk tiles in (x, y) order, as an n x 2 int64 array."""
    return np.array([(x * 8 + 4, y * 8 + 4) for x, y in sorted(walk_tiles)], dtype=np.int64).reshape(-1, 2)


def _add_wall_option(forge: TilePosition, gateway: TilePosition, end_geo: set[Tile] | None,
                     wall_options: list[_ForgeGatewayWallOption], seen: set[tuple[Tile, Tile]]) -> None:
    # Check if we've already considered this wall
    key = (_tile_key(forge), _tile_key(gateway))
    if key in seen:
        return
    seen.add(key)

    # Buildings overlap
    if geo.overlaps_tiles(forge, 3, 2, gateway, 4, 3):
        wall_options.append(_ForgeGatewayWallOption(forge, gateway))
        return

    # Buildings cannot be placed
    if not _buildable(UnitTypes.Protoss_Forge, forge) or not _buildable(UnitTypes.Protoss_Gateway, gateway):
        wall_options.append(_ForgeGatewayWallOption(forge, gateway))
        return

    # Set up the sets of positions we are comparing between
    geo1: set[Tile] = set()
    geo2: set[Tile]
    if end_geo is not None:
        _add_building_geo(forge, UnitTypes.Protoss_Forge, geo1)
        _add_building_geo(gateway, UnitTypes.Protoss_Gateway, geo1)
        geo2 = end_geo
    else:
        _add_building_geo(forge, UnitTypes.Protoss_Forge, geo1)
        geo2 = set()
        _add_building_geo(gateway, UnitTypes.Protoss_Gateway, geo2)

    # Find the closest pair of walk tile centers. Stardust iterates both sets in order, taking a strictly closer pair
    # or, at equal distance, a pair whose midpoint is strictly closer to the natural center.
    nat_center = _wall_natural().get_position()
    firsts = _walk_centers(geo1)
    seconds = _walk_centers(geo2)
    if len(firsts) == 0 or len(seconds) == 0:
        log.debug(f"Error scoring wall forge {forge}, gateway {gateway}, geo1 size {len(geo1)}, geo2 size "
                  f"{len(geo2)}")
        wall_options.append(_ForgeGatewayWallOption(forge, gateway))
        return

    deltas = firsts[:, None, :] - seconds[None, :, :]
    squared = (deltas * deltas).sum(axis=2)
    best_squared = int(squared.min())
    best_nat_dist = math.inf
    best_pair: tuple[int, int] = (0, 0)
    for i, j in np.argwhere(squared == best_squared):
        first_x, first_y = int(firsts[i, 0]), int(firsts[i, 1])
        second_x, second_y = int(seconds[j, 0]), int(seconds[j, 1])
        center_x = (first_x + second_x) // 2
        center_y = (first_y + second_y) // 2
        nat_dist = math.sqrt((center_x - nat_center.x) ** 2 + (center_y - nat_center.y) ** 2)
        if nat_dist < best_nat_dist:
            best_nat_dist = nat_dist
            best_pair = (int(i), int(j))
    best_dist = math.sqrt(best_squared)
    first = Position(int(firsts[best_pair[0], 0]), int(firsts[best_pair[0], 1]))
    second = Position(int(seconds[best_pair[1], 0]), int(seconds[best_pair[1], 1]))
    best_center = Position((first.x + second.x) // 2, (first.y + second.y) // 2)

    # Gap must be at least 64
    if best_dist < 64.0:
        wall_options.append(_ForgeGatewayWallOption(forge, gateway))
        return

    wall_options.append(_ForgeGatewayWallOption(forge, gateway, math.floor(best_dist / 16.0) - 2, best_center,
                                                first, second))


def _vector_angle(p0: Position, p1: Position) -> float:
    """The angle with the x-axis of the vector defined by points p0, p1."""
    # Infinite slope has an arctan of pi/2
    if p0.x == p1.x:
        return math.pi / 2

    # Angle is the arctan of the slope
    return math.atan((p1.y - p0.y) / (p1.x - p0.x))


def _angular_distance(a0: Position, a1: Position, b0: Position, b1: Position) -> float:
    """The angular distance between the vectors a and b (as defined by points a0, a1, b0, b1)."""
    a = _vector_angle(a0, a1)
    b = _vector_angle(b0, b1)
    return min(abs(a - b), math.pi - abs(a - b))


def _side_of_line(line_end1: Position, line_end2: Position, point: Position) -> bool:
    return (((point.x - line_end1.x) * (line_end2.y - line_end1.y))
            - ((point.y - line_end1.y) * (line_end2.x - line_end1.x))) < 0


def _analyze_wall_geo(wall: ForgeGatewayWall) -> None:
    game = bwapi.Broodwar
    natural = _wall_natural()

    # Pull the gap ends so they are on the closest unwalkable positions
    unwalkable_positions: set[Tile] = set()

    def add_building_to_unwalkable_positions(unit_type: UnitType, tile: TilePosition) -> None:
        building_center = Position(tile) + Position(unit_type.tileWidth() * 16, unit_type.tileHeight() * 16)
        for x in range(building_center.x - unit_type.dimensionLeft(),
                       building_center.x + unit_type.dimensionRight() + 1):
            for y in range(building_center.y - unit_type.dimensionUp(),
                           building_center.y + unit_type.dimensionDown() + 1):
                unwalkable_positions.add((x, y))

    def walkable_pos(pos: Position) -> bool:
        if not pos.isValid():
            return False
        if (pos.x, pos.y) in unwalkable_positions:
            return False
        walk = WalkPosition(pos)
        if (walk.x, walk.y) in _neutral_walk_tiles:
            return False
        if not game.isWalkable(walk):
            return False
        return True

    def pull_to_walkable(end: Position, other: Position) -> Position:
        dist_total = end.getApproxDistance(other)
        xdiff = other.x - end.x
        ydiff = other.y - end.y

        # If the end is walkable at the start, we pull in the opposite direction until we hit something that isn't
        # walkable
        if walkable_pos(end):
            for dist_stop in range(1, dist_total + 1):
                pos = Position(end.x - cround((dist_stop / dist_total) * xdiff),
                               end.y - cround((dist_stop / dist_total) * ydiff))
                if walkable_pos(pos):
                    continue
                return pos

            log.get(f"WARNING: Pulling wall gap ends did not find unwalkable moving backwards from {end} vs. {other}")
            return end

        previous = end
        for dist_stop in range(1, dist_total + 1):
            pos = Position(end.x + cround((dist_stop / dist_total) * xdiff),
                           end.y + cround((dist_stop / dist_total) * ydiff))
            if not walkable_pos(pos):
                previous = pos
                continue
            return previous

        log.get(f"WARNING: Pulling wall gap ends did not find a walkable position between {end} and {other}")
        return end

    add_building_to_unwalkable_positions(UnitTypes.Protoss_Forge, wall.forge)
    add_building_to_unwalkable_positions(UnitTypes.Protoss_Gateway, wall.gateway)
    wall.gap_end1 = pull_to_walkable(wall.gap_end1, wall.gap_end2)
    wall.gap_end2 = pull_to_walkable(wall.gap_end2, wall.gap_end1)
    wall.gap_center = (wall.gap_end1 + wall.gap_end2) / 2
    gap_length = wall.gap_end1.getDistance(wall.gap_end2)
    wall.gap_size = math.ceil((gap_length / 16.0) - 0.00001)

    # Compute probe blocking positions
    probes = math.ceil(((gap_length - 15.0) / 38.0) - 0.0001)
    space_between = (gap_length - float(23 * probes)) / float(probes + 1)
    xdiff = wall.gap_end2.x - wall.gap_end1.x
    ydiff = wall.gap_end2.y - wall.gap_end1.y
    for i in range(probes):
        dist_stop = (23.0 * i) + (space_between * (i + 1)) + 11.5
        wall.probe_blocking_positions.add(Position(wall.gap_end1.x + cround((dist_stop / gap_length) * xdiff),
                                                   wall.gap_end1.y + cround((dist_stop / gap_length) * ydiff)))

    # Compute the sets of tiles inside and outside the wall

    # We start by setting forge and gateway building tiles as inside the wall
    def add_building_to_inside_tiles(unit_type: UnitType, tile: TilePosition) -> None:
        for x in range(tile.x, tile.x + unit_type.tileWidth()):
            for y in range(tile.y, tile.y + unit_type.tileHeight()):
                wall.tiles_inside_wall.add(TilePosition(x, y))

    add_building_to_inside_tiles(UnitTypes.Protoss_Forge, wall.forge)
    add_building_to_inside_tiles(UnitTypes.Protoss_Gateway, wall.gateway)

    # Then we trace the wall gap as inside the wall
    gap_tiles = geo.find_tiles_between(TilePosition(wall.gap_end1), TilePosition(wall.gap_end2))
    wall.tiles_inside_wall.update(gap_tiles)

    bwem_map = bwem.Instance()

    # Then we flood-fill inside tiles from the natural center, not going outside the natural area unless close to the
    # gap center
    gap_center_tile = TilePosition(wall.gap_center)
    natural_area = natural.get_area()
    queue: deque[TilePosition] = deque()
    visited = set(wall.tiles_inside_wall)

    def visit_inside(tile: TilePosition) -> None:
        if tile in visited:
            return
        visited.add(tile)

        if not tile.isValid():
            return
        if not game_map.is_walkable(tile.x, tile.y):
            return

        if tile.getApproxDistance(gap_center_tile) > 6 and bwem_map.GetNearestArea(tile) is not natural_area:
            return

        wall.tiles_inside_wall.add(tile)

        queue.append(TilePosition(tile.x - 1, tile.y))
        queue.append(TilePosition(tile.x + 1, tile.y))
        queue.append(TilePosition(tile.x, tile.y - 1))
        queue.append(TilePosition(tile.x, tile.y + 1))

    queue.append(natural.get_tile_position() + TilePosition(-1, -1))
    while queue:
        visit_inside(queue.popleft())

    # Mark the outside edge of the forge and gateway as being outside the wall
    def outside(tile: TilePosition) -> bool:
        if not tile.isValid():
            return False
        if not game_map.is_walkable(tile.x, tile.y):
            return False
        return tile not in wall.tiles_inside_wall

    def mark_outside_building_edge(unit_type: UnitType, tile: TilePosition) -> None:
        for x in range(tile.x, tile.x + unit_type.tileWidth()):
            for y in range(tile.y, tile.y + unit_type.tileHeight()):
                if (outside(TilePosition(x - 1, y)) or outside(TilePosition(x + 1, y))
                        or outside(TilePosition(x, y - 1)) or outside(TilePosition(x, y + 1))):
                    wall.tiles_outside_wall.add(TilePosition(x, y))
                    wall.tiles_outside_but_close_to_wall.add(TilePosition(x, y))
        for x in range(tile.x, tile.x + unit_type.tileWidth()):
            for y in range(tile.y, tile.y + unit_type.tileHeight()):
                here = TilePosition(x, y)
                if here in wall.tiles_outside_wall:
                    wall.tiles_inside_wall.discard(here)

    mark_outside_building_edge(UnitTypes.Protoss_Forge, wall.forge)
    mark_outside_building_edge(UnitTypes.Protoss_Gateway, wall.gateway)

    # Mark the outside bordering tiles to the gap tiles as outside the wall
    def mark_if_outside(tile: TilePosition) -> None:
        if not outside(tile):
            return
        wall.tiles_outside_wall.add(tile)
        wall.tiles_outside_but_close_to_wall.add(tile)

    for tile in gap_tiles:
        mark_if_outside(TilePosition(tile.x - 1, tile.y))
        mark_if_outside(TilePosition(tile.x + 1, tile.y))
        mark_if_outside(TilePosition(tile.x, tile.y - 1))
        mark_if_outside(TilePosition(tile.x, tile.y + 1))

    # Flood-fill the outside tiles until we reach 10 tiles away
    outside_queue: deque[tuple[TilePosition, int]] = deque()
    outside_visited: set[TilePosition] = set()

    def visit_outside(tile: TilePosition, dist: int) -> None:
        if tile in outside_visited:
            return
        outside_visited.add(tile)

        if not tile.isValid():
            return
        if not game_map.is_walkable(tile.x, tile.y):
            return

        if tile in wall.tiles_inside_wall:
            return

        wall.tiles_outside_wall.add(tile)
        if dist < 3:
            wall.tiles_outside_but_close_to_wall.add(tile)

        if dist == 10:
            return

        outside_queue.append((TilePosition(tile.x - 1, tile.y), dist + 1))
        outside_queue.append((TilePosition(tile.x + 1, tile.y), dist + 1))
        outside_queue.append((TilePosition(tile.x, tile.y - 1), dist + 1))
        outside_queue.append((TilePosition(tile.x, tile.y + 1), dist + 1))

    for tile in sorted(wall.tiles_outside_wall, key=_tile_key):
        outside_queue.append((tile, 0))
    while outside_queue:
        tile, dist = outside_queue.popleft()
        visit_outside(tile, dist)


def _gateway_spawn_position(wall: ForgeGatewayWall, building_tile: TilePosition,
                            building_type: UnitType) -> TilePosition:
    # The possible spawn tiles in the order they are considered
    offsets = ((0, 3), (1, 3), (2, 3), (3, 3), (4, 3), (4, 2), (4, 1), (4, 0), (4, -1), (3, -1), (2, -1), (1, -1),
               (0, -1), (-1, -1), (-1, 0), (-1, 1), (-1, 2), (-1, 3))

    # Return the first tile that is:
    # - Valid
    # - Walkable
    # - Not overlapping any existing building
    # - Not overlapping the building being scored
    result = TilePositions.Invalid
    _add_wall_tiles(building_tile, building_type.tileSize())

    for dx, dy in offsets:
        tile = wall.gateway + TilePosition(dx, dy)
        if not tile.isValid() or not game_map.is_walkable(tile.x, tile.y):
            continue
        if (tile.x, tile.y) in _wall_tiles:
            continue

        result = tile
        break

    _remove_wall_tiles(building_tile, building_type.tileSize())
    return result


def _initialize_pathfinding_tiles() -> None:
    global _pathfinding_end_tile, _pathfinding_start_tile

    bwem_map = bwem.Instance()
    natural = _wall_natural()
    natural_area = natural.get_area()
    assert _natural_choke is not None and _main_choke is not None

    # End tile

    # Initialize 5 tiles away from the choke in the opposite direction of the natural
    p0 = Position(_natural_choke.center)
    p1 = natural.get_position()

    # Special case where the slope is infinite
    if p0.x == p1.x:
        start = TilePosition(p0 + Position(0, 160 if p0.y > p1.y else -160))
    else:
        # First get the slope, m = (y1 - y0)/(x1 - x0)
        m = (p1.y - p0.y) / (p1.x - p0.x)

        # Now the equation for a new x is x0 +- d/sqrt(1 + m^2)
        x = p0.x + (1.0 if p0.x > p1.x else -1.0) * 160.0 / math.sqrt(1 + m * m)

        # And y is m(x - x0) + y0
        y = m * (x - p0.x) + p0.y

        start = TilePosition(Position(to_int(x), to_int(y)))

    # Now find the closest valid tile that is not in the natural area
    _pathfinding_end_tile = start
    best_dist = math.inf
    for x_tile in range(start.x - 5, start.x + 5):
        for y_tile in range(start.y - 5, start.y + 5):
            tile = TilePosition(x_tile, y_tile)
            if not tile.isValid():
                continue
            if not _valid_pathfinding_tile(tile):
                continue

            area = bwem_map.GetArea(tile)
            if area is not None and area is not natural_area:
                dist = tile.getDistance(start)
                if dist < best_dist:
                    best_dist = dist
                    _pathfinding_end_tile = tile

    # Start tile

    # Initialize to a tile between the main choke and natural center, closer to the main choke
    main_choke_center = TilePosition(_main_choke.center)
    _pathfinding_start_tile = (main_choke_center + main_choke_center + main_choke_center
                               + natural.get_tile_position()) / 4

    if (bwem_map.GetArea(_pathfinding_start_tile) is None
            or not game_map.is_terrain_walkable(_pathfinding_start_tile.x, _pathfinding_start_tile.y)):
        tile_best = TilePositions.Invalid
        dist_best = INT_MAX
        for x_tile in range(_pathfinding_start_tile.x - 2, _pathfinding_start_tile.x + 2):
            for y_tile in range(_pathfinding_start_tile.y - 2, _pathfinding_start_tile.y + 2):
                tile = TilePosition(x_tile, y_tile)
                dist = tile.getApproxDistance(_pathfinding_end_tile)

                if bwem_map.GetArea(tile) is natural_area and dist < dist_best:
                    tile_best = tile
                    dist_best = dist

        if tile_best.isValid():
            _pathfinding_start_tile = tile_best


def _initialize_neutrals() -> None:
    _neutral_walk_tiles.clear()

    for unit in bwapi.Broodwar.getStaticNeutralUnits():
        unit_type = unit.getType()
        if unit_type == UnitTypes.Zerg_Egg or unit_type == UnitTypes.Zerg_Lurker_Egg:
            width, height = 4, 4
        elif unit_type.isMineralField():
            width, height = 8, 4
        elif unit_type == UnitTypes.Resource_Vespene_Geyser:
            width, height = 16, 8
        else:
            continue

        left = unit.getPosition().x - unit_type.dimensionLeft()
        top = unit.getPosition().y - unit_type.dimensionUp()
        walk = WalkPosition(Position(left, top))
        for walk_x in range(width):
            for walk_y in range(height):
                _neutral_walk_tiles.add((walk.x + walk_x, walk.y + walk_y))

    _mineral_field_tiles.clear()
    for mineral_patch in _wall_natural().mineral_patches():
        _mineral_field_tiles.add((mineral_patch.tile.x, mineral_patch.tile.y))
        _mineral_field_tiles.add((mineral_patch.tile.x + 1, mineral_patch.tile.y))


def _overlaps_natural_area(tile: TilePosition, building: UnitType) -> bool:
    bwem_map = bwem.Instance()
    natural_area = _wall_natural().get_area()

    for x in range(tile.x, tile.x + building.tileWidth()):
        for y in range(tile.y, tile.y + building.tileHeight()):
            test = TilePosition(x, y)
            area = bwem_map.GetArea(test)
            if area is None:
                area = bwem_map.GetNearestArea(test)
            if area is natural_area:
                return True

    return False


def _are_forge_and_gateway_touching(forge: TilePosition, gateway: TilePosition) -> bool:
    return (gateway.x - 3) <= forge.x <= (gateway.x + 4) and (gateway.y - 2) <= forge.y <= (gateway.y + 3)


def _analyze_choke_geo_and_find_building_options(end1_geo: set[Tile], end2_geo: set[Tile],
                                                 end1_forge_options: set[Tile], end1_gateway_options: set[Tile],
                                                 end2_forge_options: set[Tile], end2_gateway_options: set[Tile],
                                                 tight: bool) -> None:
    game = bwapi.Broodwar
    natural = _wall_natural()
    assert _natural_choke is not None

    def process_end_geo(tile: TilePosition, geo_tiles: set[Tile]) -> None:
        """Adds unwalkable walk positions that border a walkable walk position."""
        for walk_x in range(4):
            for walk_y in range(4):
                x = tile.x * 4 + walk_x
                y = tile.y * 4 + walk_y
                if not _walkable_walk(x, y) and (_walkable_walk(x + 1, y) or _walkable_walk(x, y + 1)
                                                  or _walkable_walk(x - 1, y) or _walkable_walk(x, y - 1)):
                    geo_tiles.add((x, y))

    # Get elevation of natural, we want our wall to be at the same elevation
    elevation = game.getGroundHeight(natural.get_tile_position())

    end1 = TilePosition(_natural_choke.choke.Pos(bwem.ChokePoint.end1))
    end2 = TilePosition(_natural_choke.choke.Pos(bwem.ChokePoint.end2))

    diff_x = end1.x - end2.x
    diff_y = end1.y - end2.y

    def consider(x: int, y: int, geo_tiles: set[Tile], forge_options: set[Tile], gateway_options: set[Tile]) -> None:
        tile = TilePosition(x, y)
        if not tile.isValid():
            return
        process_end_geo(tile, geo_tiles)
        if game.getGroundHeight(tile) != elevation:
            return

        _add_building_option(x, y, UnitTypes.Protoss_Forge, forge_options, tight)
        _add_building_option(x, y, UnitTypes.Protoss_Gateway, gateway_options, tight)

    if -2 <= diff_y <= 2:
        # Make sure end1 is the left side
        if end1.x > end2.x:
            end1, end2 = end2, end1

        # Straight vertical wall

        # Find options on left side
        for x in range(end1.x - 2, end1.x + 3):
            for y in range(end1.y - 5, end1.y + 6):
                consider(x, y, end1_geo, end1_forge_options, end1_gateway_options)

        # Find options on right side
        for x in range(end2.x - 2, end2.x + 3):
            for y in range(end2.y - 5, end2.y + 6):
                consider(x, y, end2_geo, end2_forge_options, end2_gateway_options)
    elif -2 <= diff_x <= 2:
        # Make sure end1 is the top side
        if end1.y > end2.y:
            end1, end2 = end2, end1

        # Straight horizontal wall

        # Find options on top side
        for x in range(end1.x - 5, end1.x + 6):
            for y in range(end1.y - 2, end1.y + 3):
                consider(x, y, end1_geo, end1_forge_options, end1_gateway_options)

        # Find options on bottom side
        for x in range(end2.x - 5, end2.x + 6):
            for y in range(end2.y - 2, end2.y + 3):
                consider(x, y, end2_geo, end2_forge_options, end2_gateway_options)
    else:
        # Make sure end1 is the left side
        if end1.x > end2.x:
            end1, end2 = end2, end1

        # Diagonal wall
        end1_center = _center(end1)
        end2_center = _center(end2)

        # Follow the slope perpendicular to the choke on both ends
        m = (-1.0) / ((end2_center.y - end1_center.y) / (end2_center.x - end1_center.x))
        for xdelta in range(-4, 5):
            for ydelta in range(-3, 4):
                # Find options on left side
                x = end1.x + xdelta
                y = end1.y + cround(xdelta * m) + ydelta

                tile = TilePosition(x, y)
                if not tile.isValid():
                    continue

                forge_center = Position(tile) + Position(48, 32)
                gateway_center = Position(tile) + Position(64, 48)
                forge_ok = forge_center.getDistance(end1_center) < (0.8 * forge_center.getDistance(end2_center))
                gate_ok = gateway_center.getDistance(end1_center) < (0.8 * gateway_center.getDistance(end2_center))
                if not forge_ok and not gate_ok:
                    continue

                process_end_geo(tile, end1_geo)
                if game.getGroundHeight(tile) != elevation:
                    continue

                if forge_ok:
                    _add_building_option(x, y, UnitTypes.Protoss_Forge, end1_forge_options, tight)
                if gate_ok:
                    _add_building_option(x, y, UnitTypes.Protoss_Gateway, end1_gateway_options, tight)

                # Find options on right side
                x = end2.x + xdelta
                y = end2.y + cround(xdelta * m) + ydelta

                tile = TilePosition(x, y)
                if not tile.isValid():
                    continue

                forge_center = Position(tile) + Position(48, 32)
                gateway_center = Position(tile) + Position(64, 48)
                forge_ok = forge_center.getDistance(end2_center) < (0.8 * forge_center.getDistance(end1_center))
                gate_ok = gateway_center.getDistance(end2_center) < (0.8 * gateway_center.getDistance(end1_center))
                if not forge_ok and not gate_ok:
                    continue

                process_end_geo(tile, end2_geo)
                if game.getGroundHeight(tile) != elevation:
                    continue

                if forge_ok:
                    _add_building_option(x, y, UnitTypes.Protoss_Forge, end2_forge_options, tight)
                if gate_ok:
                    _add_building_option(x, y, UnitTypes.Protoss_Gateway, end2_gateway_options, tight)


def _generate_wall_options(wall_options: list[_ForgeGatewayWallOption], end1_geo: set[Tile], end2_geo: set[Tile],
                           end1_forge_options: set[Tile], end1_gateway_options: set[Tile],
                           end2_forge_options: set[Tile], end2_gateway_options: set[Tile],
                           max_gap_size: int) -> None:
    seen: set[tuple[Tile, Tile]] = set()

    def add(forge: Tile, gateway: Tile, end_geo: set[Tile] | None) -> None:
        _add_wall_option(TilePosition(*forge), TilePosition(*gateway), end_geo, wall_options, seen)

    # Forge on one side
    for forge_options, gateway_options, other_geo in ((end1_forge_options, end2_gateway_options, end2_geo),
                                                      (end2_forge_options, end1_gateway_options, end1_geo)):
        for forge in sorted(forge_options):
            # Gateway on the other side
            for gate in sorted(gateway_options):
                add(forge, gate, None)

            # Gateway above forge
            for dx in range(-3, 3):
                add(forge, (forge[0] + dx, forge[1] - 3), other_geo)

    # Gateway on one side, forge below gateway
    for gateway_options, other_geo in ((end1_gateway_options, end2_geo), (end2_gateway_options, end1_geo)):
        for gateway in sorted(gateway_options):
            for dx in range(-2, 4):
                add((gateway[0] + dx, gateway[1] + 3), gateway, other_geo)

    # Prune invalid options, we don't need to store them any more
    wall_options[:] = [option for option in wall_options
                       if option.gap_center != Positions.Invalid and option.gap_size <= max_gap_size]


def _powers_wall_buildings(tile: TilePosition, wall: ForgeGatewayWall) -> bool:
    if not unit_util.powers(tile, wall.gateway, UnitTypes.Protoss_Gateway):
        return False
    if not unit_util.powers(tile, wall.forge, UnitTypes.Protoss_Forge):
        return False
    return all(unit_util.powers(tile, cannon, UnitTypes.Protoss_Photon_Cannon) for cannon in wall.cannons)


def _building_center(tile: TilePosition, unit_type: UnitType) -> Position:
    return Position(tile) + Position(unit_type.tileWidth() * 16, unit_type.tileHeight() * 16)


def _get_pylon_placement_from_pylon_options(wall: ForgeGatewayWall, optimal_path_length: int) -> TilePosition:
    forge_center = _building_center(wall.forge, UnitTypes.Protoss_Forge)
    gateway_center = _building_center(wall.gateway, UnitTypes.Protoss_Gateway)
    nat_center = _wall_natural().get_position()
    nat_side_of_forge_gateway_line = _side_of_line(forge_center, gateway_center, nat_center)

    for pylon_option in _pylon_options:
        tile = pylon_option.pylon
        if not _powers_wall_buildings(tile, wall):
            continue

        if not all(_buildable(UnitTypes.Protoss_Photon_Cannon, nat_cannon) for nat_cannon in pylon_option.cannons):
            continue

        if not _buildable(UnitTypes.Protoss_Pylon, tile):
            continue

        spawn = _gateway_spawn_position(wall, tile, UnitTypes.Protoss_Pylon)
        if not spawn.isValid():
            continue

        # Ensure there is a valid path through the wall
        if not _has_path_with_building(tile, UnitTypes.Protoss_Pylon.tileSize(), optimal_path_length * 2):
            continue

        # Ensure there is a valid path from the gateway spawn position
        if _side_of_line(forge_center, gateway_center, _center(spawn)) == nat_side_of_forge_gateway_line:
            if not _has_path_with_building(tile, UnitTypes.Protoss_Pylon.tileSize(), 0, spawn):
                continue

        return tile

    return TilePositions.Invalid


def _get_pylon_placement(wall: ForgeGatewayWall, optimal_path_length: int) -> TilePosition:
    forge_center = _building_center(wall.forge, UnitTypes.Protoss_Forge)
    gateway_center = _building_center(wall.gateway, UnitTypes.Protoss_Gateway)
    start_tile_center = Position(_pathfinding_start_tile) + Position(16, 16)
    nat_center = _wall_natural().get_position()
    nat_side_of_forge_gateway_line = _side_of_line(forge_center, gateway_center, nat_center)
    nat_side_of_gap_line = _side_of_line(wall.gap_end1, wall.gap_end2, nat_center)

    centroid = (forge_center + gateway_center) / 2
    dist_centroid_nat = centroid.getDistance(nat_center)

    best_pylon_dist = 0.0
    best_pylon = TilePositions.Invalid

    for x in range(wall.gateway.x - 10, wall.gateway.x + 11):
        for y in range(wall.gateway.y - 10, wall.gateway.y + 11):
            tile = TilePosition(x, y)
            if not tile.isValid():
                continue
            if not _powers_wall_buildings(tile, wall):
                continue

            if not _buildable(UnitTypes.Protoss_Pylon, tile):
                continue

            pylon_center = Position(tile) + Position(32, 32)
            pylon_top_left_with_buffer = Position(tile) + Position(-8, -8)
            pylon_bottom_right_with_buffer = Position(TilePosition(x + 2, y + 2)) + Position(8, 8)
            if ((_side_of_line(forge_center, gateway_center, pylon_top_left_with_buffer)
                 != nat_side_of_forge_gateway_line
                 or _side_of_line(forge_center, gateway_center, pylon_bottom_right_with_buffer)
                 != nat_side_of_forge_gateway_line)
                    and (_side_of_line(wall.gap_end1, wall.gap_end2, pylon_top_left_with_buffer) != nat_side_of_gap_line
                         or _side_of_line(wall.gap_end1, wall.gap_end2, pylon_bottom_right_with_buffer)
                         != nat_side_of_gap_line)):
                continue

            if pylon_center.getDistance(nat_center) > dist_centroid_nat:
                continue

            spawn = _gateway_spawn_position(wall, tile, UnitTypes.Protoss_Pylon)
            if not spawn.isValid():
                continue

            dist = fdiv(pylon_center.getDistance(start_tile_center), pylon_center.getDistance(nat_center))
            if dist > best_pylon_dist:
                # Ensure there is a valid path through the wall
                if not _has_path_with_building(tile, UnitTypes.Protoss_Pylon.tileSize(), optimal_path_length * 2):
                    continue

                # Ensure there is a valid path from the gateway spawn position
                if _side_of_line(forge_center, gateway_center, _center(spawn)) == nat_side_of_forge_gateway_line:
                    if not _has_path_with_building(tile, UnitTypes.Protoss_Pylon.tileSize(), 0, spawn):
                        continue

                best_pylon_dist = dist
                best_pylon = tile

    return best_pylon


_PylonPlacer = Callable[[ForgeGatewayWall, int], TilePosition]


def _get_best_wall_option(wall_options: list[_ForgeGatewayWallOption], optimal_path_length: int,
                          pylon_placer: _PylonPlacer) -> _ForgeGatewayWallOption:
    best_wall_option = _ForgeGatewayWallOption()

    nat_center = _wall_natural().get_position()

    best_wall_quality = math.inf
    best_dist_centroid = 0.0

    for wall in wall_options:
        # Check if there is a pylon location
        _add_wall_tiles(wall.forge, UnitTypes.Protoss_Forge.tileSize())
        _add_wall_tiles(wall.gateway, UnitTypes.Protoss_Gateway.tileSize())

        pylon = pylon_placer(wall.to_wall(), optimal_path_length)

        _remove_wall_tiles(wall.forge, UnitTypes.Protoss_Forge.tileSize())
        _remove_wall_tiles(wall.gateway, UnitTypes.Protoss_Gateway.tileSize())

        if not pylon.isValid():
            continue

        # Center of each building
        forge_center = Position(wall.forge) + (Position(UnitTypes.Protoss_Forge.tileSize()) / 2)
        gateway_center = Position(wall.gateway) + (Position(UnitTypes.Protoss_Gateway.tileSize()) / 2)

        # Prefer walls that create a longer path
        # (A size_t in Stardust: a shorter path wraps around to a huge increase, so the guard below never applies.)
        dist_increase = _path_length() - optimal_path_length
        if dist_increase < 0:
            dist_increase += 2**64

        # Prefer walls that are slightly crooked, so we get better cannon placements
        # For walls where the forge and gateway are touching, measure this by comparing the slope of the wall building
        # centers to the slope of the gap, rounded to 15 degree increments. Otherwise compute it based on the relative
        # distance between the natural center and the forge and gateway.
        if _are_forge_and_gateway_touching(wall.forge, wall.gateway):
            straightness = math.floor(_angular_distance(forge_center, gateway_center, wall.gap_end1, wall.gap_end2)
                                      / (math.pi / 12))
        else:
            dist_forge = nat_center.getDistance(forge_center)
            dist_gateway = nat_center.getDistance(gateway_center)
            straightness = to_int(math.floor(fdiv(2.0 * max(dist_forge, dist_gateway), min(dist_forge, dist_gateway))))

        # Combine the gap size and the previous two values into a measure of wall quality
        # Distance increase is capped at 10 tiles and scaled to a factor of 0.8 - 1.2
        # Straightness target is 2, above or below cause the final result to increase
        wall_quality = (wall.gap_size
                        * (0.8 + 0.4 * (max(0.0, 10.0 - float(dist_increase)) / 10.0))
                        * (1.0 + abs(2 - straightness) / 2.0))

        # Compute the centroid of the wall buildings
        # If the other scores are equal, we prefer a centroid farther away from the natural
        # In all cases we require the centroid to be at least 6 tiles away
        centroid = (forge_center + gateway_center) / 2
        dist_centroid_nat = centroid.getDistance(nat_center)
        if dist_centroid_nat < 192.0:
            continue

        if (wall_quality < best_wall_quality
                or (wall_quality == best_wall_quality and dist_centroid_nat > best_dist_centroid)):
            best_wall_option = wall
            best_wall_quality = wall_quality
            best_dist_centroid = dist_centroid_nat

    return best_wall_option


def _overlaps_probe_blocking_location(wall: ForgeGatewayWall, tile: TilePosition, unit_type: UnitType) -> bool:
    return any(geo.overlaps_tiles(TilePosition(position), 1, 1, tile, unit_type.tileWidth(), unit_type.tileHeight())
               for position in wall.probe_blocking_positions)


def _get_cannon_placement(wall: ForgeGatewayWall, optimal_path_length: int,
                          unused_natural_cannons: set[Tile]) -> TilePosition:
    forge_center = Position(wall.forge) + (Position(UnitTypes.Protoss_Forge.tileSize()) / 2)
    gateway_center = Position(wall.gateway) + (Position(UnitTypes.Protoss_Gateway.tileSize()) / 2)
    centroid = (forge_center + gateway_center) / 2
    nat_center = _wall_natural().get_position()
    nat_side_of_forge_gateway_line = _side_of_line(forge_center, gateway_center, nat_center)

    forge_gateway_touching = _are_forge_and_gateway_touching(wall.forge, wall.gateway)

    start_tile = wall.pylon if wall.pylon.isValid() else TilePosition(centroid)

    def walkable(x: int, y: int) -> bool:
        return _walkable_tile(x, y)

    dist_best = 0.0
    tile_best = TilePositions.Invalid
    for x in range(start_tile.x - 10, start_tile.x + 11):
        for y in range(start_tile.y - 10, start_tile.y + 11):
            tile = TilePosition(x, y)

            # Natural cannons come "pre-validated"
            prevalidated = (x, y) in unused_natural_cannons

            cannon_center = Position(tile) + Position(32, 32)
            spawn = TilePosition(0, 0)
            if not prevalidated:
                if not tile.isValid():
                    continue
                if wall.pylon.isValid() and not unit_util.powers(wall.pylon, tile, UnitTypes.Protoss_Photon_Cannon):
                    continue
                if not _buildable(UnitTypes.Protoss_Photon_Cannon, tile):
                    continue
                if not _overlaps_natural_area(tile, UnitTypes.Protoss_Pylon):
                    continue
                if _overlaps_probe_blocking_location(wall, tile, UnitTypes.Protoss_Photon_Cannon):
                    continue

                if any(_side_of_line(forge_center, gateway_center, cannon_center + Position(dx, dy))
                       != nat_side_of_forge_gateway_line for dx, dy in ((16, 16), (16, -16), (-16, 16), (-16, -16))):
                    continue

                spawn = _gateway_spawn_position(wall, tile, UnitTypes.Protoss_Photon_Cannon)
                if not spawn.isValid():
                    continue

            bordering_tiles = sum(1 for bx, by in ((x - 1, y), (x - 1, y + 1), (x, y - 1), (x + 1, y - 1), (x + 2, y),
                                                   (x + 2, y + 1), (x, y + 2), (x + 1, y + 2))
                                  if not walkable(bx, by))

            # When forge and gateway are touching, use the wall centroid as the distance measurement
            # Otherwise, use the smallest distance to either of the buildings
            dist_to_wall = (centroid.getDistance(cannon_center) if forge_gateway_touching
                            else min(gateway_center.getDistance(cannon_center), forge_center.getDistance(cannon_center)))

            # When forge and gateway are touching, prefer locations closer to the door
            # Otherwise, prefer locations further from the door so we don't put them in the gap
            door_distance = wall.gap_center.getDistance(cannon_center)
            dist_to_door = clog10(door_distance) if forge_gateway_touching else fdiv(1.0, clog(door_distance))

            # Compute a factor based on how many bordering tiles there are
            bordering_factor = 0.95 ** bordering_tiles

            # Putting it all together
            dist = fdiv(1.0, dist_to_wall * dist_to_door * bordering_factor)

            if dist > dist_best:
                if not prevalidated:
                    # Ensure there is still a valid path through the wall
                    if not _has_path_with_building(tile, UnitTypes.Protoss_Photon_Cannon.tileSize(),
                                                   optimal_path_length * 3):
                        continue

                    # Ensure there is a valid path from the gateway spawn position
                    if _side_of_line(forge_center, gateway_center, _center(spawn)) == nat_side_of_forge_gateway_line:
                        if not _has_path_with_building(tile, UnitTypes.Protoss_Photon_Cannon.tileSize(), 0, spawn):
                            continue

                tile_best = tile
                dist_best = dist

    return tile_best


def _create_wall(tight: bool, max_gap_size: int) -> ForgeGatewayWall:
    _wall_tiles.clear()

    # Initialize pathfinding
    optimal_path_length = _path_length()

    # Step 1: Analyze choke geo and find potential forge and gateway options
    end1_geo: set[Tile] = set()
    end2_geo: set[Tile] = set()
    end1_forge_options: set[Tile] = set()
    end1_gateway_options: set[Tile] = set()
    end2_forge_options: set[Tile] = set()
    end2_gateway_options: set[Tile] = set()
    _analyze_choke_geo_and_find_building_options(end1_geo, end2_geo, end1_forge_options, end1_gateway_options,
                                                 end2_forge_options, end2_gateway_options, tight)

    # Step 2: Generate possible combinations
    wall_options: list[_ForgeGatewayWallOption] = []
    _generate_wall_options(wall_options, end1_geo, end2_geo, end1_forge_options, end1_gateway_options,
                           end2_forge_options, end2_gateway_options, max_gap_size)

    # Return if we have no valid wall
    if not wall_options:
        return ForgeGatewayWall()

    pylon_placer: _PylonPlacer = _get_pylon_placement_from_pylon_options

    # Step 3: Select the best wall and do some calculations we'll need later
    best_wall = _get_best_wall_option(wall_options, optimal_path_length, pylon_placer).to_wall()

    # If there is no valid wall, try again with separate pylons for wall and natural
    if not best_wall.forge.isValid():
        # Add the natural pylon and cannons to the reserved tiles
        natural_defense_locations = base_static_defense_locations(_wall_natural())
        if natural_defense_locations.is_valid():
            _add_building_to_reserved_tiles(natural_defense_locations.power_pylon, UnitTypes.Protoss_Pylon)
            for cannon_tile in natural_defense_locations.worker_defense_cannons:
                _add_building_to_reserved_tiles(cannon_tile, UnitTypes.Protoss_Photon_Cannon)

        # Try again with the alternate pylon placer
        pylon_placer = _get_pylon_placement
        best_wall = _get_best_wall_option(wall_options, optimal_path_length, pylon_placer).to_wall()

    # Abort if there is no valid wall
    if not best_wall.forge.isValid():
        return ForgeGatewayWall()

    _add_wall_tiles(best_wall.forge, UnitTypes.Protoss_Forge.tileSize())
    _add_wall_tiles(best_wall.gateway, UnitTypes.Protoss_Gateway.tileSize())

    # Step 4: Find initial cannons
    # We do this before finding the pylon so the pylon doesn't interfere too much with optimal cannon placement
    # If we can't place the pylon later, we will roll back cannons until we can
    unused_natural_cannons: set[Tile] = set()
    for _ in range(2):
        cannon = _get_cannon_placement(best_wall, optimal_path_length, unused_natural_cannons)
        if cannon.isValid():
            _add_wall_tiles(cannon, UnitTypes.Protoss_Photon_Cannon.tileSize())
            best_wall.cannons.append(cannon)

    # Step 5: Find a pylon position and finalize the wall selection
    pylon = pylon_placer(best_wall, optimal_path_length)
    while not pylon.isValid():
        # Undo cannon placement until we can place the pylon
        if not best_wall.cannons:
            break

        cannon = best_wall.cannons.pop()
        _remove_wall_tiles(cannon, UnitTypes.Protoss_Photon_Cannon.tileSize())

        pylon = pylon_placer(best_wall, optimal_path_length)

    # Return invalid wall if no pylon location can be found
    if not pylon.isValid():
        log.get("ERROR: Wall generation: Could not find valid pylon, but this should have been checked when picking "
                "the best wall")
        return ForgeGatewayWall()

    best_wall.pylon = pylon
    _add_wall_tiles(pylon, UnitTypes.Protoss_Pylon.tileSize())

    # Add the natural cannons
    # (Stardust then sorts them "furthest from the wall first" with a comparator that is always false, so they keep
    # their (x, y) order.)
    for pylon_option in _pylon_options:
        if pylon_option.pylon != best_wall.pylon:
            continue

        best_wall.natural_cannons = list(pylon_option.cannons)
        unused_natural_cannons.update(_tile_key(cannon) for cannon in pylon_option.cannons)

        for natural_cannon in best_wall.natural_cannons:
            _add_wall_tiles(natural_cannon, UnitTypes.Protoss_Photon_Cannon.tileSize())
        break

    # Step 6: Analyze the wall geo and remove cannons overlapping probe blocking positions
    _analyze_wall_geo(best_wall)
    best_wall.cannons = [cannon for cannon in best_wall.cannons
                         if not _overlaps_probe_blocking_location(best_wall, cannon, UnitTypes.Protoss_Photon_Cannon)]

    # Step 7: Find remaining cannon positions (up to 6 in total)

    # Find location closest to the wall that is behind it
    # Only return powered buildings
    # Prefer a location that is close to the door
    # Prefer a location that doesn't leave space around it
    # Allow using the natural cannons
    for _ in range(len(best_wall.cannons), 6):
        cannon = _get_cannon_placement(best_wall, optimal_path_length, unused_natural_cannons)
        if not cannon.isValid():
            break

        _add_wall_tiles(cannon, UnitTypes.Protoss_Photon_Cannon.tileSize())
        best_wall.cannons.append(cannon)
        unused_natural_cannons.discard(_tile_key(cannon))

    return best_wall


def create_forge_gateway_wall(tight: bool, base: Base) -> ForgeGatewayWall:
    global _main_choke, _natural_choke, _natural

    # Ensure we have the ability to make a wall
    _main_choke, _natural_choke = game_map.get_starting_base_chokes(base)

    if _main_choke is _natural_choke:
        _natural = base
    else:
        _natural = game_map.map_specific_override().natural_for_wall_placement(base)
        if _natural is None:
            _natural = game_map.get_starting_base_natural(base)

    if _natural is None or _main_choke is None or _natural_choke is None:
        log.get("Wall cannot be created; missing natural, main choke, or natural choke")
        return ForgeGatewayWall()

    # Map-specific hard-coded walls
    map_specific_wall = game_map.map_specific_override().get_wall(base.get_tile_position())
    if map_specific_wall is not None:
        if map_specific_wall.is_valid():
            _analyze_wall_geo(map_specific_wall)
        return map_specific_wall

    # Initialize reserved tiles
    # These are tiles in the natural we don't want to block
    _reserved_tiles.clear()
    _add_building_to_reserved_tiles(_natural.get_tile_position(), UnitTypes.Protoss_Nexus)

    # Initialize pathfinding tiles
    _initialize_pathfinding_tiles()

    # Initialize neutrals that can be used in the wall
    _initialize_neutrals()

    # Select possible locations for the pylon that are close to the choke and provide natural cannon locations
    _generate_pylon_options()

    # Create the wall
    wall = _create_wall(tight, 6)

    # Fall back to non-tight if a tight wall could not be found
    if not wall.is_valid():
        return ForgeGatewayWall()

    return wall

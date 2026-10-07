"""Port of Map/PathFinding/PathFinding{.h,.cpp,_BWEM.cpp,_Grids.cpp,_Search.cpp}: ground distances, chokepoint
paths, navigation grids and tile-level A* search.

C++ out-parameters become return values: get_choke_point_path returns (path, length).

Note on expected_travel_time: C++ has an overload taking an int as the fifth argument, meaning
default_if_inaccessible. Callers must pass that by keyword here.
"""

from __future__ import annotations

import heapq
from collections.abc import Callable
from enum import IntFlag
from itertools import count

import bwapi
import bwem
from bwapi import Position, Positions, TilePosition, TilePositions, UnitType, UnitTypes, WalkPosition
from stardust.cpp import USHRT_MAX, to_int
from stardust.map.choke import Choke
from stardust.map.path_finding import navigation_grid
from stardust.map.path_finding.navigation_grid import GridNode, NavigationGrid
from stardust.util import geo


class PathFindingOptions(IntFlag):
    Default = 0
    UseNearestBWEMArea = 1 << 0
    UseNeighbouringBWEMArea = 1 << 1


# ---------------------------------------------------------------------------------------------------------------------
# BWEM-based paths (PathFinding_BWEM.cpp)

# Positions for searching outwards from a point at tile resolution (as in Stardust, (0, -32) and (0, -64) repeat)
_TILE_SPIRAL = (
    Position(-32, 0), Position(0, -32), Position(32, 0), Position(0, -32),
    Position(-32, -32), Position(32, -32), Position(32, 32), Position(-32, 32),
    Position(-64, 0), Position(0, -64), Position(64, 0), Position(0, -64),
    Position(-64, -32), Position(-32, -64), Position(32, -64), Position(64, -32),
    Position(64, 32), Position(32, 64), Position(-32, 64), Position(-64, 32),
)


def _valid_choke(bwem_choke: bwem.ChokePoint, min_choke_width: int, allow_mineral_walk: bool) -> bool:
    from stardust.map import game_map

    choke_data = game_map.choke(bwem_choke)
    assert choke_data is not None
    if choke_data.width < min_choke_width:
        return False
    if allow_mineral_walk and choke_data.requires_mineral_walk:
        return True
    return not bwem_choke.Blocked() and not choke_data.requires_mineral_walk


def _adjust_for_bwem_path_finding(position: Position, options: PathFindingOptions) -> Position:
    """Adjusts the position so it can be used for BWEM pathfinding given the options."""
    # If we are allowing using the nearest area, accept the input position
    if options & PathFindingOptions.UseNearestBWEMArea:
        return position

    # If the input position has an area, accept it
    bwem_map = bwem.Instance()
    if bwem_map.GetArea(WalkPosition(position)) is not None:
        return position

    # If we want to use a neighbour, try to find one and adjust the position
    if options & PathFindingOptions.UseNeighbouringBWEMArea:
        for offset in _TILE_SPIRAL:
            here = position + offset
            if here.isValid() and bwem_map.GetArea(WalkPosition(here)) is not None:
                return here

    return Positions.Invalid


def _custom_choke_point_path(start: Position, end: Position, options: PathFindingOptions,
                             unit_type: UnitType) -> tuple[list[bwem.ChokePoint], int]:
    """A BWEM-style chokepoint path with additional constraints beyond what BWEM provides, like choke width and
    mineral walking, using an algorithm similar to BWEB's tile-resolution path finding."""
    bwem_map = bwem.Instance()
    if options & PathFindingOptions.UseNearestBWEMArea:
        start_area = bwem_map.GetNearestArea(WalkPosition(start))
        target_area = bwem_map.GetNearestArea(WalkPosition(end))
    else:
        start_area = bwem_map.GetArea(WalkPosition(start))
        target_area = bwem_map.GetArea(WalkPosition(end))
    if start_area is None or target_area is None:
        return [], -1

    if start_area == target_area:
        return [], start.getApproxDistance(end)

    def choke_to(c: bwem.ChokePoint, from_area: bwem.Area) -> bwem.Area:
        first, second = c.GetAreas()
        return second if from_area == first else first

    # (dist, sequence, choke, to area, parent); sequence breaks distance ties first-in-first-out
    sequence = count()
    node_queue: list[tuple[int, int, bwem.ChokePoint, bwem.Area, bwem.ChokePoint | None]] = []
    width = unit_type.width()
    is_worker = unit_type.isWorker()
    for c in start_area.ChokePoints():
        if _valid_choke(c, width, is_worker):
            heapq.heappush(node_queue, (start.getApproxDistance(Position(c.Center())), next(sequence), c,
                                        choke_to(c, start_area), None))

    parent_map: dict[bwem.ChokePoint, bwem.ChokePoint | None] = {}
    while node_queue:
        dist, _, c, to_area, parent = heapq.heappop(node_queue)

        # If already has a parent, continue
        if c in parent_map:
            continue
        parent_map[c] = parent

        # If at target, return path.
        # We're ignoring the distance from this last choke to the target position; it's an unlikely edge case that
        # there is an alternate choke giving a significantly better result.
        if to_area == target_area:
            path = []
            current: bwem.ChokePoint | None = c
            while current is not None:
                path.append(current)
                current = parent_map.get(current)
            path.reverse()
            return path, dist + end.getApproxDistance(Position(c.Center()))

        # Add valid connected chokes we haven't visited yet
        for next_choke in to_area.ChokePoints():
            if _valid_choke(next_choke, width, is_worker) and next_choke not in parent_map:
                heapq.heappush(node_queue, (
                    dist + Position(next_choke.Center()).getApproxDistance(Position(c.Center())), next(sequence),
                    next_choke, choke_to(next_choke, to_area), c))

    return [], -1


def get_ground_distance(start: Position, end: Position, unit_type: UnitType = UnitTypes.Protoss_Dragoon,
                        options: PathFindingOptions = PathFindingOptions.Default) -> int:
    """Ground distance in pixels, or -1 if end is not reachable from start."""
    # Use grid cost if we have one, regardless of input options
    grid = get_navigation_grid(end)
    if grid is not None:
        cost = grid.cost_at(start)
        if cost < USHRT_MAX:
            return cost

    # Adjust the start and end positions based on the options
    if (_adjust_for_bwem_path_finding(start, options) == Positions.Invalid
            or _adjust_for_bwem_path_finding(end, options) == Positions.Invalid):
        return start.getApproxDistance(end)

    _, dist = get_choke_point_path(start, end, unit_type, options)
    return dist


def get_choke_point_path(start: Position, end: Position, unit_type: UnitType = UnitTypes.Protoss_Dragoon,
                         options: PathFindingOptions = PathFindingOptions.Default
                         ) -> tuple[list[bwem.ChokePoint], int]:
    """The chokepoints on the ground path from start to end, and the path length (-1 if unreachable)."""
    from stardust.map import game_map

    # Adjust the start and end positions based on the options
    adjusted_start = _adjust_for_bwem_path_finding(start, options)
    adjusted_end = _adjust_for_bwem_path_finding(end, options)
    if adjusted_start == Positions.Invalid or adjusted_end == Positions.Invalid:
        return [], -1

    # Start with the BWEM path
    bwem_path, path_length = bwem.Instance().GetPath(adjusted_start, adjusted_end)

    # We can always use BWEM's default pathfinding if:
    # - The minimum choke width is equal to or greater than the unit width
    # - The map doesn't have mineral walking chokes or the unit can't mineral walk
    # An exception to the second case is Plasma, where BWEM doesn't mark the mineral walking chokes as blocked
    can_use_bwem_path = (max(unit_type.width(), unit_type.height()) <= game_map.min_choke_width()
                         and game_map.map_specific_override().can_use_bwem_path(unit_type))

    # If we can't automatically use it, validate the chokes
    if not can_use_bwem_path and bwem_path:
        can_use_bwem_path = all(_valid_choke(c, unit_type.width(), unit_type.isWorker()) for c in bwem_path)

    if can_use_bwem_path:
        return bwem_path, path_length

    # Otherwise do our own path analysis
    return _custom_choke_point_path(adjusted_start, adjusted_end, options, unit_type)


def separating_narrow_choke(start: Position, end: Position, unit_type: UnitType = UnitTypes.Protoss_Dragoon,
                            options: PathFindingOptions = PathFindingOptions.Default) -> Choke | None:
    """The first narrow choke on the ground path from start to end."""
    from stardust.map import game_map

    path, _ = get_choke_point_path(start, end, unit_type, options)
    for bwem_choke in path:
        this_choke = game_map.choke(bwem_choke)
        if this_choke is not None and this_choke.is_narrow_choke:
            return this_choke
    return None


def expected_travel_time(start: Position, end: Position, unit_type: UnitType,
                         options: PathFindingOptions = PathFindingOptions.Default, penalty_factor: float = 1.4,
                         default_if_inaccessible: int = 0) -> int:
    """Expected frames to travel from start to end (ground distance with a penalty factor for ground units)."""
    if unit_type.topSpeed() < 0.0001:
        return 0

    if unit_type.isFlyer():
        return to_int(start.getApproxDistance(end) / unit_type.topSpeed())

    dist = get_ground_distance(start, end, unit_type, options)
    if dist == -1:
        return default_if_inaccessible
    return to_int(dist * penalty_factor / unit_type.topSpeed())


def next_grid_or_choke_waypoint(start: Position, end: Position, grid: NavigationGrid | None, nodes_ahead: int,
                                verify_walkability: bool = False) -> Position:
    from stardust.map import game_map

    # First try to use the grid
    if grid is not None:
        # Advance the desired number of nodes
        node: GridNode | None = grid.node(start)
        for _ in range(nodes_ahead):
            if node is None:
                break
            node = node.next_node

        if node is not None:
            start_tile = TilePosition(start)
            return start + Position(node.tile - start_tile)

    # Next try to get a choke-point path
    path, length = get_choke_point_path(start, end, UnitTypes.Protoss_Dragoon, PathFindingOptions.Default)
    if length == -1:
        return Positions.Invalid

    # Find the next waypoint that is at least four tiles away
    waypoint = Positions.Invalid
    for bwem_choke in path:
        c = game_map.choke(bwem_choke)
        if c is not None and start.getApproxDistance(c.center) > 128:
            waypoint = c.center
            break
    if not waypoint.isValid():
        waypoint = end

    # Compute the desired result
    vector = waypoint - start
    result = start + geo.scale_vector(vector, nodes_ahead * 32)
    if result == Positions.Invalid:
        return end  # This means the unit is at the target
    if not verify_walkability:
        return result

    # Verify that we can walk to this waypoint
    extent = start.getApproxDistance(result)
    for dist in range(16, extent, 16):
        pos = start + geo.scale_vector(vector, dist)
        if not pos.isValid() or not game_map.is_walkable(pos.x >> 5, pos.y >> 5):
            return Positions.Invalid

    return result


# ---------------------------------------------------------------------------------------------------------------------
# Navigation grids (PathFinding_Grids.cpp)

# Goal tile -> (grid including enemy buildings as blocking, grid ignoring them)
_goal_to_navigation_grid: dict[TilePosition, tuple[NavigationGrid, NavigationGrid]] = {}


def _create_navigation_grid(goal_center: TilePosition, goal_top_left: TilePosition = TilePositions.Invalid,
                            goal_size: TilePosition = TilePositions.Invalid) -> None:
    if goal_center in _goal_to_navigation_grid:
        return  # std::map::emplace doesn't replace existing entries
    _goal_to_navigation_grid[goal_center] = (NavigationGrid(goal_center, goal_top_left, goal_size),
                                             NavigationGrid(goal_center, goal_top_left, goal_size))


def clear_grids() -> None:
    _goal_to_navigation_grid.clear()


def initialize_grids() -> None:
    from stardust.map import game_map

    navigation_grid.initialize_globals(bwapi.Broodwar.mapWidth(), bwapi.Broodwar.mapHeight())

    for base in game_map.all_bases():
        _create_navigation_grid(TilePosition(base.get_position()), base.get_tile_position(),
                                UnitTypes.Protoss_Nexus.tileSize())
    my_main_choke = game_map.get_my_main_choke()
    for c in game_map.all_chokes():
        if c is not my_main_choke and not c.is_narrow_choke:
            continue
        _create_navigation_grid(TilePosition(c.center))


def get_navigation_grid(goal: Position | TilePosition, ignore_enemy_buildings: bool = False) -> NavigationGrid | None:
    tile = goal if isinstance(goal, TilePosition) else TilePosition(goal)
    grids = _goal_to_navigation_grid.get(tile)
    if grids is None:
        return None
    grid = grids[1] if ignore_enemy_buildings else grids[0]
    grid.update()
    return grid


def add_blocking_object(unit_type: UnitType, tile: TilePosition, is_enemy_building: bool = False) -> None:
    size = unit_type.tileSize()
    for grid, grid_ignoring_enemy_buildings in _goal_to_navigation_grid.values():
        grid.add_blocking_object(tile, size)
        if not is_enemy_building:
            grid_ignoring_enemy_buildings.add_blocking_object(tile, size)


def add_blocking_tiles(tiles: set[TilePosition]) -> None:
    for grid, grid_ignoring_enemy_buildings in _goal_to_navigation_grid.values():
        grid.add_blocking_tiles(tiles)
        grid_ignoring_enemy_buildings.add_blocking_tiles(tiles)


def remove_blocking_object(unit_type: UnitType, tile: TilePosition, is_enemy_building: bool = False) -> None:
    size = unit_type.tileSize()
    for grid, grid_ignoring_enemy_buildings in _goal_to_navigation_grid.values():
        grid.remove_blocking_object(tile, size)
        if not is_enemy_building:
            grid_ignoring_enemy_buildings.remove_blocking_object(tile, size)


def remove_blocking_tiles(tiles: set[TilePosition]) -> None:
    for grid, grid_ignoring_enemy_buildings in _goal_to_navigation_grid.values():
        grid.remove_blocking_tiles(tiles)
        grid_ignoring_enemy_buildings.remove_blocking_tiles(tiles)


def check_grid_path(start: TilePosition, end: TilePosition, predicate: Callable[[GridNode], bool]) -> bool:
    """Walks the navigation grid path from start towards end, returning False if the predicate rejects a node."""
    grid = get_navigation_grid(end)
    if grid is None:
        return True

    node: GridNode | None = grid.node(start)
    for _ in range(1000):
        assert node is not None
        if node.cost < 90:
            return True
        next_node = node.next_node
        if next_node is None:
            return False
        if not predicate(node):
            return False
        node = next_node
    return True


# ---------------------------------------------------------------------------------------------------------------------
# Tile-level A* search (PathFinding_Search.cpp)

_START = (-2, -2)  # Parent marker of the start tile (C++ uses TilePositions::Invalid)
_parents: list[tuple[int, int] | None] = []


def initialize_search() -> None:
    global _parents
    _parents = [None] * (bwapi.Broodwar.mapWidth() * bwapi.Broodwar.mapHeight())


def search(start: TilePosition, end: TilePosition,
           tile_validator: Callable[[TilePosition], bool] | None = None,
           close_enough_to_end: Callable[[TilePosition], bool] | None = None,
           max_backtracking: int = 500) -> list[TilePosition]:
    """A* from start to end over tiles accepted by tile_validator; returns the tiles after start up to the end (or
    the first tile close_enough_to_end accepts), or an empty list if there is no path."""
    width = bwapi.Broodwar.mapWidth()
    height = bwapi.Broodwar.mapHeight()
    end_x, end_y = end.x, end.y

    # Estimated distance using diagonal moves
    def dist_to_end(x: int, y: int) -> int:
        diff1 = abs(x - end_x)
        diff2 = abs(y - end_y)
        if diff1 > diff2:
            diff1, diff2 = diff2, diff1
        return diff2 * 10 + diff1 * 4

    # Not in Stardust: validators don't change during a search, so each tile is only checked once
    valid_cache: dict[int, bool] = {}

    def tile_valid(x: int, y: int) -> bool:
        if tile_validator is None:
            return True
        index = x + y * width
        valid = valid_cache.get(index)
        if valid is None:
            valid = valid_cache[index] = tile_validator(TilePosition(x, y))
        return valid

    parents = _parents
    parents[:] = [None] * len(parents)

    start_dist = dist_to_end(start.x, start.y)
    dist_cutoff = start_dist + max_backtracking * 10

    # Priority: lowest estimated cost, then the most recently created node (C++ PathNodeComparator)
    ids = count()
    queue: list[tuple[int, int, int, int, int]] = []  # (estimated cost, -id, dist, x, y)

    def visit(node_x: int, node_y: int, node_dist: int, dx: int, dy: int, diagonal: bool) -> None:
        x = node_x + dx
        y = node_y + dy
        if x < 0 or y < 0 or x >= width or y >= height:
            return
        if parents[x + y * width] is not None:
            return
        if not tile_valid(x, y):
            return

        # Don't allow diagonal connections between blocked tiles
        if diagonal and (not tile_valid(x, node_y) or not tile_valid(node_x, y)):
            return

        dist = dist_to_end(x, y)
        if dist > dist_cutoff:
            return

        new_dist = node_dist + (14 if diagonal else 10)
        heapq.heappush(queue, (new_dist + dist, -next(ids), new_dist, x, y))
        parents[x + y * width] = (node_x, node_y)

    heapq.heappush(queue, (start_dist, -next(ids), 0, start.x, start.y))
    parents[start.x + start.y * width] = _START
    while queue:
        _, _, dist, x, y = heapq.heappop(queue)

        # Return path if we are at the destination
        if (x == end_x and y == end_y) or (close_enough_to_end is not None
                                           and close_enough_to_end(TilePosition(x, y))):
            result = []
            tile: tuple[int, int] = (x, y)
            while tile != (start.x, start.y):
                result.append(TilePosition(*tile))
                parent = parents[tile[0] + tile[1] * width]
                assert parent is not None
                tile = parent
            result.reverse()
            return result

        visit(x, y, dist, 1, 0, False)
        visit(x, y, dist, 0, 1, False)
        visit(x, y, dist, -1, 0, False)
        visit(x, y, dist, 0, -1, False)
        visit(x, y, dist, 1, 1, True)
        visit(x, y, dist, -1, 1, True)
        visit(x, y, dist, -1, -1, True)
        visit(x, y, dist, 1, -1, True)

    return []

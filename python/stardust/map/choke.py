"""Port of Map/Choke.{h,cpp}: chokepoint analysis on top of BWEM.

For narrow chokes Stardust computes the real width, the lengthwise ends ("end centers" and "exits"), the side of the
choke every half-tile of the map is on (used by the combat sim), the tiles in the choke, the high-ground tile of ramps,
and positions where one or two units block an enemy worker scout. The verbose heatmap dumps
(DUMP_NARROW_CHOKE_HEATMAPS) are omitted.
"""

from __future__ import annotations

import bisect
import math
from collections import deque

import numpy as np

import bwapi
import bwem
from bwapi import Position, Positions, TilePosition, TilePositions, UnitType, UnitTypes, WalkPosition
from stardust.cpp import INT_MAX, to_int
from stardust.instrumentation import log
from stardust.util import geo

_NARROW_CHOKE_THRESHOLD = 128
_PI = 3.14159265358979323846
_CHOKE_SIDE_ANGLE_THRESHOLD = _PI / 4.0  # 45 degrees

# Static terrain walkability at half-tile resolution, shared by all chokes on the same map
_half_tile_walkable_cache: tuple[str, np.ndarray] | None = None


def _half_tile_walkable() -> np.ndarray:
    """[y, x] -> whether all four walk tiles of the half tile are walkable."""
    global _half_tile_walkable_cache
    game = bwapi.Broodwar
    map_hash = game.mapHash()
    if _half_tile_walkable_cache is None or _half_tile_walkable_cache[0] != map_hash:
        walk_width, walk_height = game.mapWidth() * 4, game.mapHeight() * 4
        walkable = np.array([[game.isWalkable(x, y) for x in range(walk_width)] for y in range(walk_height)], dtype=bool)
        half = walkable.reshape(walk_height // 2, 2, walk_width // 2, 2).all(axis=(1, 3))
        _half_tile_walkable_cache = (map_hash, half)
    return _half_tile_walkable_cache[1]


def _normalize_angle(angle: float) -> float:
    while angle < 0:
        angle += _PI
    while angle > _PI:
        angle -= _PI
    return angle


def _angle_diff(first: float, second: float) -> float:
    diff = abs(first - second)
    return min(diff, _PI - diff)


def _squared_distance(first: tuple[int, int] | Position, second: tuple[int, int] | Position) -> int:
    first_x, first_y = first
    second_x, second_y = second
    x = first_x - second_x
    y = first_y - second_y
    return x * x + y * y


def _walk_position_border(x: int, y: int) -> list[Position]:
    px, py = x * 8, y * 8
    return [Position(px + dx, py + dy) for dx, dy in (
        (0, 0), (1, 0), (2, 0), (3, 0), (3, 1), (3, 2), (3, 3), (2, 3), (1, 3), (0, 3), (0, 2), (0, 1))]


def _passable(our_unit: UnitType, enemy_unit: UnitType, pos: Position, wall: Position) -> bool:
    left = pos.x - our_unit.dimensionLeft() - 1
    top = pos.y - our_unit.dimensionUp() - 1
    right = pos.x + our_unit.dimensionRight() + 1
    bottom = pos.y + our_unit.dimensionDown() + 1

    enemy_left_of = left - enemy_unit.dimensionRight()
    enemy_right_of = right + enemy_unit.dimensionLeft()
    enemy_above = top - enemy_unit.dimensionDown()
    enemy_below = bottom + enemy_unit.dimensionUp()

    if wall.x < left:
        if wall.y < top:
            to_check = [(enemy_left_of, enemy_above), (enemy_left_of, pos.y), (pos.x, enemy_above)]
        elif wall.y > bottom:
            to_check = [(enemy_left_of, enemy_below), (enemy_left_of, pos.y), (pos.x, enemy_below)]
        else:
            to_check = [(enemy_left_of, enemy_above), (enemy_left_of, pos.y), (enemy_left_of, enemy_below)]
    elif wall.x > right:
        if wall.y < top:
            to_check = [(enemy_right_of, enemy_above), (enemy_right_of, pos.y), (pos.x, enemy_above)]
        elif wall.y > bottom:
            to_check = [(enemy_right_of, enemy_below), (enemy_right_of, pos.y), (pos.x, enemy_below)]
        else:
            to_check = [(enemy_right_of, enemy_above), (enemy_right_of, pos.y), (enemy_right_of, enemy_below)]
    else:
        if wall.y < top:
            to_check = [(enemy_right_of, enemy_above), (pos.x, enemy_above), (enemy_left_of, enemy_above)]
        elif wall.y > bottom:
            to_check = [(pos.x, enemy_below), (enemy_right_of, enemy_below), (enemy_left_of, enemy_below)]
        else:
            return False

    return all(geo.walkable(enemy_unit, Position(x, y)) for x, y in to_check)


def _blocks_choke(pos: Position, unit_type: UnitType) -> bool:
    """Does a unit of the given type at the given position block the choke?"""
    end1 = geo.find_closest_unwalkable_position(pos, 64)
    if end1 == Positions.Invalid:
        return False

    end2 = geo.find_closest_unwalkable_position_near(Position(pos.x + pos.x - end1.x, pos.y + pos.y - end1.y), pos, 32)
    if end2 == Positions.Invalid:
        return False

    if end1.getDistance(end2) < end1.getDistance(pos) * 1.2:
        return False

    return (not _passable(unit_type, UnitTypes.Protoss_Probe, pos, end1)
            and not _passable(unit_type, UnitTypes.Protoss_Probe, pos, end2))


class Choke:
    def __init__(self, choke: bwem.ChokePoint) -> None:
        self.choke = choke
        self.width = 0
        self.center = Position(choke.Center()) + Position(4, 4)
        self.is_narrow_choke = False
        self.length = 0
        self.end1_center = Positions.Invalid
        self.end2_center = Positions.Invalid
        self.end1_exit = Positions.Invalid
        self.end2_exit = Positions.Invalid
        # Each map half-tile near the choke gets a "side": -2 = side 1, 1 = side 2, 0 = inside the choke
        self.tile_side: list[int] = []
        self.choke_tiles: set[TilePosition] = set()  # Tiles inside and close to the ends of the choke
        self.is_ramp = False
        self.high_elevation_tile = TilePositions.Invalid
        # Minimum sets of positions where a probe / zealot blocks an enemy worker scout from getting in
        self.probe_block_scout_positions: set[Position] = set()
        self.zealot_block_scout_positions: set[Position] = set()
        self.requires_mineral_walk = False
        # Mineral patch to use when moving towards the first / second area in the chokepoint's GetAreas(), and a
        # start location that should give visibility of it
        self.first_area_mineral_patch: bwapi.Unit | None = None
        self.first_area_start_position = Positions.Invalid
        self.second_area_mineral_patch: bwapi.Unit | None = None
        self.second_area_start_position = Positions.Invalid

        game = bwapi.Broodwar

        # Estimate the choke width.
        # Because the ends are themselves walkable tiles, we need to add a bit of padding to estimate the actual
        # walkable width of the choke. We do further refinement of narrow chokes later.
        self.width = to_int(Position(choke.Pos(bwem.ChokePoint.end1)).getDistance(
            Position(choke.Pos(bwem.ChokePoint.end2)))) + 15

        # Check if the choke is a ramp
        # TODO (upstream): This could technically fail if the "top" tile is at a different height than the choke end
        first_area, second_area = choke.GetAreas()
        if game.getGroundHeight(TilePosition(first_area.Top())) != game.getGroundHeight(TilePosition(second_area.Top())):
            self.is_ramp = True

        # BWEM doesn't give us the information we need to properly fight at narrow chokes, so do some extra analysis
        if self.width < _NARROW_CHOKE_THRESHOLD:
            self._analyze_narrow_choke()

            if self.is_narrow_choke:
                if self.is_ramp:
                    self._compute_narrow_ramp_high_ground_position()

                self._compute_scout_blocking_positions(self.center, UnitTypes.Protoss_Probe,
                                                       self.probe_block_scout_positions)
                self._compute_scout_blocking_positions(self.center, UnitTypes.Protoss_Zealot,
                                                       self.zealot_block_scout_positions)

        # If the center is not walkable, find the nearest position that is
        wp_center = WalkPosition(self.center)
        if not game.isWalkable(wp_center):
            spiral = geo.Spiral()
            while wp_center.isValid() and not game.isWalkable(wp_center):
                spiral.next()
                wp_center = WalkPosition(self.center) + WalkPosition(spiral.x, spiral.y)
            self.center = Position(wp_center) + Position(4, 4)

    def set_as_main_choke(self) -> None:
        # Our main choke is always considered to be narrow regardless of its actual width
        if not self.is_narrow_choke:
            self._analyze_narrow_choke()
            if not self.is_narrow_choke:
                log.get("WARNING: Unable to analyze main choke as a narrow choke")

    def _analyze_narrow_choke(self) -> None:
        game = bwapi.Broodwar

        # Start by getting the sides closest to BWEM's center
        side1 = geo.find_closest_unwalkable_position(self.center, 64 + self.width // 2)
        if side1 == Positions.Invalid:
            return
        side2 = geo.find_closest_unwalkable_position(self.center, 64 + self.width // 2, side1)
        if side2 == Positions.Invalid:
            return

        # Compute the approximate angle of the choke
        angle = _normalize_angle(math.atan2(side1.y - side2.y, side1.x - side2.x) + _PI / 2.0)

        map_walk_width = game.mapWidth() * 8
        map_walk_height = game.mapHeight() * 8

        def is_valid_and_walkable(walk_x: int, walk_y: int) -> bool:
            if walk_x < 0 or walk_y < 0 or walk_x >= map_walk_width or walk_y >= map_walk_height:
                return False
            return game.isWalkable(walk_x, walk_y)

        # Find the walls of the choke by tracing along each side, collecting walk positions that follow the
        # approximate choke angle. Positions are (x, y) tuples, kept sorted to match C++ std::set iteration order.
        def trace_choke_side(initial: tuple[int, int]) -> list[tuple[int, int]]:
            positions = [initial]
            position_set = {initial}
            visited: set[tuple[int, int]] = set()
            queue: deque[tuple[tuple[int, int], int]] = deque()

            def visit(pos: tuple[int, int], steps: int) -> None:
                if pos in visited:
                    return

                # Skip nodes that haven't had a position added in 8 or more steps.
                # But don't add to the visited set, as we might get here again from a valid path.
                if steps > 8:
                    return

                visited.add(pos)

                # Find positions next to this one that have an unwalkable position somewhere around them
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    here = (pos[0] + dx, pos[1] + dy)
                    if not is_valid_and_walkable(*here) or here in visited:
                        continue

                    borders_unwalkable = False
                    added_position = False
                    for bx, by in ((1, -1), (1, 0), (1, 1), (-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1)):
                        border = (here[0] + bx, here[1] + by)

                        # First check if this is an unwalkable position
                        if is_valid_and_walkable(*border):
                            continue
                        borders_unwalkable = True

                        # Find the closest position to this one that is already part of the choke side
                        closest_dist = INT_MAX
                        closest: tuple[int, int] | None = None
                        for other in positions:
                            dist = geo.approximate_distance(other[0], border[0], other[1], border[1])
                            if 0 < dist < closest_dist:
                                closest_dist = dist
                                closest = other

                        # Compare the angle of this position to the closest existing position to the choke angle
                        if closest is not None:
                            angle_here = _normalize_angle(math.atan2(border[1] - closest[1], border[0] - closest[0]))
                            if _angle_diff(angle_here, angle) < _CHOKE_SIDE_ANGLE_THRESHOLD:
                                if border not in position_set:
                                    position_set.add(border)
                                    bisect.insort(positions, border)
                                added_position = True

                    if borders_unwalkable:
                        queue.append((here, 0 if added_position else steps + 1))

            x, y = initial
            if is_valid_and_walkable(x, y - 1):
                visit((x, y - 1), 0)
            elif is_valid_and_walkable(x, y + 1):
                visit((x, y + 1), 0)
            elif is_valid_and_walkable(x - 1, y):
                visit((x - 1, y), 0)
            elif is_valid_and_walkable(x + 1, y):
                visit((x + 1, y), 0)
            else:
                log.get(f"ERROR: No walkable position bordering side position for choke @ {WalkPosition(self.center)}")
                return positions

            while queue:
                visit(*queue.popleft())
            return positions

        side1_positions = trace_choke_side((side1.x >> 3, side1.y >> 3))
        side2_positions = trace_choke_side((side2.x >> 3, side2.y >> 3))

        # Get the choke squared width at each point on each side
        side1_squared_width: dict[tuple[int, int], int] = {}
        side2_squared_width: dict[tuple[int, int], int] = {}
        min_squared_width = INT_MAX
        for side1_pos in side1_positions:
            for side2_pos in side2_positions:
                width_here = _squared_distance(side1_pos, side2_pos)
                if side1_squared_width.get(side1_pos, INT_MAX) > width_here:
                    side1_squared_width[side1_pos] = width_here
                if side2_squared_width.get(side2_pos, INT_MAX) > width_here:
                    side2_squared_width[side2_pos] = width_here
                min_squared_width = min(min_squared_width, width_here)

        # Trim positions where the width is much longer than the minimum width
        width_cutoff = min_squared_width * 2
        side1_positions = [p for p in side1_positions if side1_squared_width.get(p, 0) <= width_cutoff]
        side2_positions = [p for p in side2_positions if side2_squared_width.get(p, 0) <= width_cutoff]

        if not side1_positions or not side2_positions:
            log.get(f"ERROR: Trimmed all positions from choke @ {WalkPosition(self.center)}")
            return

        # Now find the corners by getting the pairs of positions on each side that are furthest from each other
        def find_furthest_positions(positions: list[tuple[int, int]]) -> list[tuple[int, int]]:
            max_dist = -1
            corners = [positions[0], positions[0]]
            for pos in positions:
                for other in positions:
                    dist = _squared_distance(pos, other)
                    if dist > max_dist:
                        max_dist = dist
                        corners = [pos, other]
            return corners

        wp_side1_ends = find_furthest_positions(side1_positions)
        wp_side2_ends = find_furthest_positions(side2_positions)

        # Align the ends. Do this by matching the corners that give the most parallel angles.
        def end_angle(a: tuple[int, int], b: tuple[int, int]) -> float:
            return _normalize_angle(math.atan2(a[1] - b[1], a[0] - b[0]))

        current_end1_angle = end_angle(wp_side1_ends[0], wp_side2_ends[0])
        current_end2_angle = end_angle(wp_side1_ends[1], wp_side2_ends[1])
        opposite_end1_angle = end_angle(wp_side1_ends[0], wp_side2_ends[1])
        opposite_end2_angle = end_angle(wp_side1_ends[1], wp_side2_ends[0])
        if _angle_diff(opposite_end1_angle, opposite_end2_angle) < _angle_diff(current_end1_angle, current_end2_angle):
            wp_side2_ends.reverse()

        # Convert to positions by taking the closest points
        def find_closest_positions(wp_first: tuple[int, int], wp_second: tuple[int, int]) -> tuple[Position, Position]:
            min_dist = INT_MAX
            best = (Positions.Invalid, Positions.Invalid)
            second_border = _walk_position_border(*wp_second)
            for first_pos in _walk_position_border(*wp_first):
                for second_pos in second_border:
                    dist = _squared_distance(first_pos, second_pos)
                    if dist < min_dist:
                        min_dist = dist
                        best = (first_pos, second_pos)
            return best

        side1_end0, side2_end0 = find_closest_positions(wp_side1_ends[0], wp_side2_ends[0])
        side1_end1, side2_end1 = find_closest_positions(wp_side1_ends[1], wp_side2_ends[1])

        # Now that we have the corners, compute the center at each end
        self.end1_center = (side1_end0 + side2_end0) / 2
        self.end2_center = (side1_end1 + side2_end1) / 2

        # Compute the "exits", a position one tile further than the ends
        self.end1_exit = self.end1_center + geo.scale_vector(self.end1_center - self.end2_center, 32)
        self.end2_exit = self.end2_center + geo.scale_vector(self.end2_center - self.end1_center, 32)

        # In order to allow analysis of chokes along the map edges, we allow the sides and corners to be off the map.
        # If any of the computed results are invalid though, we bail out now.
        if not (self.end1_center.isValid() and self.end2_center.isValid()
                and self.end1_exit.isValid() and self.end2_exit.isValid()):
            log.get(f"WARNING: Choke positions out of bounds for choke @ {WalkPosition(self.center)}")
            return

        # We can now officially mark this as an analyzed narrow choke
        self.is_narrow_choke = True

        # The minimum width computed earlier is done with walk tiles, so adjust it to approximate the actual width
        self.width = to_int(math.sqrt(min_squared_width) * 8.0) - 8

        self.length = to_int(self.end1_center.getDistance(self.end2_center))

        # Initialize the center as the centroid of the corners, then pull it to the center of the choke there
        self.center = (side1_end0 + side1_end1 + side2_end0 + side2_end1) / 4
        center_side1 = geo.find_closest_unwalkable_position(self.center, 64)
        if center_side1 != Positions.Invalid:
            center_side2 = geo.find_closest_unwalkable_position_near(
                Position(self.center.x + self.center.x - center_side1.x, self.center.y + self.center.y - center_side1.y),
                self.center, 64, center_side1)
            if center_side2 != Positions.Invalid:
                self.center = (center_side1 + center_side2) / 2

        self._compute_choke_tiles((side1_end0, side1_end1), (side2_end0, side2_end1))

    def _compute_choke_tiles(self, side1_ends: tuple[Position, Position],
                             side2_ends: tuple[Position, Position]) -> None:
        """Assigns every half-tile reachable from the choke a side and collects the tiles in the choke.

        Done at half-tile resolution, as that is what the collision grid in the combat sim uses.
        """
        game = bwapi.Broodwar
        half_width = game.mapWidth() * 2
        half_height = game.mapHeight() * 2
        walkable = _half_tile_walkable()

        tile_side = [0] * (half_width * half_height)
        visited = [False] * (half_width * half_height)
        self.tile_side = tile_side

        queue: deque[tuple[int, int, int]] = deque()  # (side, x, y)
        unwalkable_queue: deque[tuple[int, int, int]] = deque()

        def add_half_tiles_between(start: Position, end: Position, side: int) -> None:
            def add_position(pos: Position) -> None:
                if not pos.isValid():
                    return
                x, y = pos.x >> 4, pos.y >> 4
                index = x + y * half_width
                tile_side[index] = side
                if visited[index]:
                    return
                visited[index] = True
                (queue if walkable[y, x] else unwalkable_queue).append((side, x, y))
                self.choke_tiles.add(TilePosition(x >> 1, y >> 1))

            add_position(start)
            diff = end - start
            dist = start.getApproxDistance(end)
            for d in range(8, dist, 8):
                v = geo.scale_vector(diff, d)
                if v == Positions.Invalid:
                    continue
                add_position(start + v)
            add_position(end)

        add_half_tiles_between(side1_ends[0], self.end1_exit, -2)
        add_half_tiles_between(self.end1_exit, side2_ends[0], -2)
        add_half_tiles_between(side1_ends[1], self.end2_exit, 1)
        add_half_tiles_between(self.end2_exit, side2_ends[1], 1)

        # Queue the center of the choke to be set to 0, unless the center is on one of the traced ends
        center_x, center_y = self.center.x >> 4, self.center.y >> 4
        if (0 <= center_x < half_width and 0 <= center_y < half_height
                and not visited[center_x + center_y * half_width]):
            queue.appendleft((0, center_x, center_y))

        # Now flood-fill
        while queue or unwalkable_queue:
            side, x, y = (queue or unwalkable_queue).popleft()
            current_index = x + y * half_width
            current_walkable = walkable[y, x]
            for next_x, next_y in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if not (0 <= next_x < half_width and 0 <= next_y < half_height):
                    continue
                next_index = next_x + next_y * half_width
                if visited[next_index]:
                    continue

                tile_side[next_index] = side
                visited[next_index] = True
                next_walkable = walkable[next_y, next_x]
                if side == 0:
                    if next_walkable:
                        queue.appendleft((side, next_x, next_y))
                elif current_walkable and next_walkable:
                    queue.append((side, next_x, next_y))
                else:
                    unwalkable_queue.append((side, next_x, next_y))

                if tile_side[current_index] == 0:
                    self.choke_tiles.add(TilePosition(next_x >> 1, next_y >> 1))

        # Finally re-trace the ends to set them to 0
        add_half_tiles_between(side1_ends[0], side2_ends[0], 0)
        add_half_tiles_between(side1_ends[1], side2_ends[1], 0)

    def _compute_narrow_ramp_high_ground_position(self) -> None:
        game = bwapi.Broodwar
        choke_center = self.center

        first_area, second_area = self.choke.GetAreas()
        first_area_elevation = game.getGroundHeight(TilePosition(first_area.Top()))
        second_area_elevation = game.getGroundHeight(TilePosition(second_area.Top()))
        low_ground_elevation = min(first_area_elevation, second_area_elevation)
        high_ground_elevation = max(first_area_elevation, second_area_elevation)

        # Generate a set of low-ground tiles near the choke, ignoring "holes"
        low_ground_tiles: set[TilePosition] = set()
        bwem_center_tile = TilePosition(self.choke.Center())
        for x in range(-5, 6):
            for y in range(-5, 6):
                tile = bwem_center_tile + TilePosition(x, y)
                if not tile.isValid() or game.getGroundHeight(tile) != low_ground_elevation:
                    continue
                if any(game.getGroundHeight(tile + TilePosition(dx, dy)) == low_ground_elevation
                       for dx, dy in ((1, 0), (0, 1), (-1, 0), (0, -1))):
                    low_ground_tiles.add(tile)

        def in_choke_center(pos: Position) -> bool:
            end1 = geo.find_closest_unwalkable_position(pos, 64)
            if end1 == Positions.Invalid:
                return False
            end2 = geo.find_closest_unwalkable_position_near(
                Position(pos.x + pos.x - end1.x, pos.y + pos.y - end1.y), pos, 32)
            if end2 == Positions.Invalid:
                return False
            if end1.getDistance(end2) < end1.getDistance(pos):
                return False
            return abs(end1.getDistance(pos) - end2.getDistance(pos)) <= 2.0

        # Find the nearest position to the choke center that is on the high-ground border.
        # This means that it is on the high ground and adjacent to one of the low-ground tiles found above.
        best_pos = Positions.Invalid
        best_dist = INT_MAX
        for x in range(-64, 65):
            for y in range(-64, 65):
                pos = choke_center + Position(x, y)
                if not pos.isValid():
                    continue
                if 0 < pos.x % 32 < 31 and 0 < pos.y % 32 < 31:
                    continue
                if not game.isWalkable(WalkPosition(pos)):
                    continue
                if game.getGroundHeight(TilePosition(pos)) != high_ground_elevation:
                    continue
                if not any(TilePosition(pos + Position(dx, dy)) in low_ground_tiles
                           for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    continue
                if not in_choke_center(pos):
                    continue

                dist = pos.getApproxDistance(choke_center)
                if dist < best_dist:
                    self.high_elevation_tile = TilePosition(pos)
                    best_dist = dist
                    best_pos = pos

        self._compute_scout_blocking_positions(best_pos, UnitTypes.Protoss_Probe, self.probe_block_scout_positions)
        self._compute_scout_blocking_positions(best_pos, UnitTypes.Protoss_Zealot, self.zealot_block_scout_positions)

    @staticmethod
    def _compute_scout_blocking_positions(center: Position, unit_type: UnitType, result: set[Position]) -> None:
        if not center.isValid() or len(result) == 1:
            return

        game = bwapi.Broodwar
        target_elevation = game.getGroundHeight(TilePosition(center))

        # Search for a position in the immediate vicinity of the center that blocks the choke with one unit.
        # Prefer at same elevation but return a lower elevation if that's all we have.
        best_low_ground = Positions.Invalid
        for x in range(6):
            for y in range(6):
                for xs in ((-1,) if x == 0 else (-1, 1)):
                    for ys in ((-1,) if y == 0 else (-1, 1)):
                        current = center + Position(x * xs, y * ys)
                        if not _blocks_choke(current, unit_type):
                            continue

                        # If this position is on the high-ground, return it
                        if game.getGroundHeight(TilePosition(current)) >= target_elevation:
                            result.clear()
                            result.add(current)
                            return

                        # Otherwise set it as the best low-ground option if applicable
                        if not best_low_ground.isValid():
                            best_low_ground = current

        if best_low_ground.isValid():
            result.clear()
            result.add(best_low_ground)
            return

        if result:
            return

        # Try with two units instead

        # First grab the ends of the choke at the given center point
        end1 = geo.find_closest_unwalkable_position(center, 64)
        if end1 == Positions.Invalid:
            return
        end2 = geo.find_closest_unwalkable_position_near(
            Position(center.x + center.x - end1.x, center.y + center.y - end1.y), center, 32)
        if end2 == Positions.Invalid:
            return
        if end1.getDistance(end2) < end1.getDistance(center) * 1.2:
            return

        # Now find the positions between the ends
        to_block = geo.find_walkable_positions_between(end1, end2)

        # Step 1: remove positions on both ends that the enemy worker cannot stand on because of unwalkable terrain
        for _ in range(2):
            while to_block and not geo.walkable(UnitTypes.Protoss_Probe, to_block[0]):
                to_block.pop(0)
            to_block.reverse()
        if not to_block:
            return  # C++ dereferences an empty vector here (undefined behaviour); bail out instead

        # Step 2: gather potential positions to place the unit that block the enemy unit locations at both ends
        candidate_positions: list[list[Position]] = [[], []]
        for i in range(2):
            enemy_position = to_block[0]
            for pos in to_block:
                # Is this a valid position for a probe?
                # We use a probe here because we sometimes mix probes and zealots and probes are larger
                if not pos.isValid() or not geo.walkable(UnitTypes.Protoss_Probe, pos):
                    continue

                # Does it block the enemy position?
                if not geo.overlaps(UnitTypes.Protoss_Probe, enemy_position, unit_type, pos):
                    break

                candidate_positions[i].append(pos)
            to_block.reverse()

        # Step 3: try to find a combination that blocks all positions.
        # Prefer a combination that puts both units on the high ground.
        # Prefer a combination that spaces out the units relatively evenly.
        best_pair = (Positions.Invalid, Positions.Invalid)
        best_low_ground_pair = (Positions.Invalid, Positions.Invalid)
        best_score = INT_MAX
        best_low_ground_score = INT_MAX
        for first in candidate_positions[0]:
            for second in candidate_positions[1]:
                # Skip if the two units overlap.
                # We use probes here because we sometimes mix probes and zealots and probes are larger.
                if geo.overlaps(UnitTypes.Protoss_Probe, first, UnitTypes.Protoss_Probe, second):
                    continue

                # Skip if any positions are not blocked by one of the units
                if any(not geo.overlaps(unit_type, first, UnitTypes.Protoss_Probe, pos)
                       and not geo.overlaps(unit_type, second, UnitTypes.Protoss_Probe, pos)
                       for pos in to_block):
                    continue

                score = max(geo.edge_to_point_distance(unit_type, first, end1),
                            geo.edge_to_point_distance(unit_type, second, end2),
                            geo.edge_to_edge_distance(unit_type, first, unit_type, second))

                # (Stardust checks the elevation of `first` twice; kept as-is)
                if (score < best_score
                        and game.getGroundHeight(TilePosition(first)) >= target_elevation
                        and game.getGroundHeight(TilePosition(first)) >= target_elevation):
                    best_score = score
                    best_pair = (first, second)
                elif score < best_low_ground_score:
                    best_low_ground_score = score
                    best_low_ground_pair = (first, second)

        if best_pair[0].isValid():
            result.update(best_pair)
        elif best_low_ground_pair[0].isValid():
            result.update(best_low_ground_pair)

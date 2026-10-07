"""Port of Util/Geo.{h,cpp}: distances, overlaps and BW movement math."""

import bisect
import math
from enum import IntEnum

import bwapi
import numpy as np
from bwapi import Position, Positions, TilePosition, UnitType, WalkPosition
from numpy.typing import NDArray
from stardust.cpp import cdiv, cround, f32, to_int

_PI = 3.14159265358979323846

# (radius, x, y) of every offset within 256 pixels, sorted by radius then x then y
_radius_positions: list[tuple[int, int, int]] = []

# Not in Stardust: _radius_positions as arrays, so the searches over it can use numpy
_radius_values: list[int] = []
_radius_dx: NDArray[np.int64] = np.zeros(0, dtype=np.int64)
_radius_dy: NDArray[np.int64] = np.zeros(0, dtype=np.int64)

# Not in Stardust: the map's terrain walkability at walk resolution, indexed [x, y]. Asking BWAPI one walk tile at a
# time made map analysis take minutes, and terrain walkability doesn't change during a game. Built on first use.
_walk_grid: NDArray[np.bool_] | None = None

# Copied from openbw's bwgame.h, used for BW direction math
_TAN_TABLE = (
    7, 13, 19, 26, 32, 38, 45, 51, 58, 65, 71, 78, 85, 92,
    99, 107, 114, 122, 129, 137, 146, 154, 163, 172, 181,
    190, 200, 211, 221, 233, 244, 256, 269, 283, 297, 312,
    329, 346, 364, 384, 405, 428, 452, 479, 509, 542, 578,
    619, 664, 716, 775, 844, 926, 1023, 1141, 1287, 1476,
    1726, 2076, 2600, 3471, 5211, 10429, 2**32 - 1,
)

_DIRECTION_TABLE = (
    (0, -256), (6, -256), (13, -256), (19, -255), (25, -255), (31, -254), (38, -253), (44, -252),
    (50, -251), (56, -250), (62, -248), (68, -247), (74, -245), (80, -243), (86, -241), (92, -239),
    (98, -237), (104, -234), (109, -231), (115, -229), (121, -226), (126, -223), (132, -220), (137, -216),
    (142, -213), (147, -209), (152, -206), (157, -202), (162, -198), (167, -194), (172, -190), (177, -185),
    (181, -181), (185, -177), (190, -172), (194, -167), (198, -162), (202, -157), (206, -152), (209, -147),
    (213, -142), (216, -137), (220, -132), (223, -126), (226, -121), (229, -115), (231, -109), (234, -104),
    (237, -98), (239, -92), (241, -86), (243, -80), (245, -74), (247, -68), (248, -62), (250, -56),
    (251, -50), (252, -44), (253, -38), (254, -31), (255, -25), (255, -19), (256, -13), (256, -6),
    (256, 0), (256, 6), (256, 13), (255, 19), (255, 25), (254, 31), (253, 38), (252, 44),
    (251, 50), (250, 56), (248, 62), (247, 68), (245, 74), (243, 80), (241, 86), (239, 92),
    (237, 98), (234, 104), (231, 109), (229, 115), (226, 121), (223, 126), (220, 132), (216, 137),
    (213, 142), (209, 147), (206, 152), (202, 157), (198, 162), (194, 167), (190, 172), (185, 177),
    (181, 181), (177, 185), (172, 190), (167, 194), (162, 198), (157, 202), (152, 206), (147, 209),
    (142, 213), (137, 216), (132, 220), (126, 223), (121, 226), (115, 229), (109, 231), (104, 234),
    (98, 237), (92, 239), (86, 241), (80, 243), (74, 245), (68, 247), (62, 248), (56, 250),
    (50, 251), (44, 252), (38, 253), (31, 254), (25, 255), (19, 255), (13, 256), (6, 256),
    (0, 256), (-6, 256), (-13, 256), (-19, 255), (-25, 255), (-31, 254), (-38, 253), (-44, 252),
    (-50, 251), (-56, 250), (-62, 248), (-68, 247), (-74, 245), (-80, 243), (-86, 241), (-92, 239),
    (-98, 237), (-104, 234), (-109, 231), (-115, 229), (-121, 226), (-126, 223), (-132, 220), (-137, 216),
    (-142, 213), (-147, 209), (-152, 206), (-157, 202), (-162, 198), (-167, 194), (-172, 190), (-177, 185),
    (-181, 181), (-185, 177), (-190, 172), (-194, 167), (-198, 162), (-202, 157), (-206, 152), (-209, 147),
    (-213, 142), (-216, 137), (-220, 132), (-223, 126), (-226, 121), (-229, 115), (-231, 109), (-234, 104),
    (-237, 98), (-239, 92), (-241, 86), (-243, 80), (-245, 74), (-247, 68), (-248, 62), (-250, 56),
    (-251, 50), (-252, 44), (-253, 38), (-254, 31), (-255, 25), (-255, 19), (-256, 13), (-256, 6),
    (-256, 0), (-256, -6), (-256, -13), (-255, -19), (-255, -25), (-254, -31), (-253, -38), (-252, -44),
    (-251, -50), (-250, -56), (-248, -62), (-247, -68), (-245, -74), (-243, -80), (-241, -86), (-239, -92),
    (-237, -98), (-234, -104), (-231, -109), (-229, -115), (-226, -121), (-223, -126), (-220, -132), (-216, -137),
    (-213, -142), (-209, -147), (-206, -152), (-202, -157), (-198, -162), (-194, -167), (-190, -172), (-185, -177),
    (-181, -181), (-177, -185), (-172, -190), (-167, -194), (-162, -198), (-157, -202), (-152, -206), (-147, -209),
    (-142, -213), (-137, -216), (-132, -220), (-126, -223), (-121, -226), (-115, -229), (-109, -231), (-104, -234),
    (-98, -237), (-92, -239), (-86, -241), (-80, -243), (-74, -245), (-68, -247), (-62, -248), (-56, -250),
    (-50, -251), (-44, -252), (-38, -253), (-31, -254), (-25, -255), (-19, -255), (-13, -256), (-6, -256),
)

_TEN_DISTANCE_OFFSETS_AROUND_PATCH = (
    (-54, -30), (-54, -29), (-54, -28), (-54, -27), (-54, -26), (-54, -25), (-54, -24), (-54, -23), (-54, -22),
    (-54, -21), (-54, -20), (-54, -19), (-54, -18), (-54, -17), (-54, -16), (-54, -15), (-54, -14), (-54, -13),
    (-54, -12), (-54, -11), (-54, -10), (-54, -9), (-54, -8), (-54, -7), (-54, -6), (-54, -5), (-54, -4), (-54, -3),
    (-54, -2), (-54, -1), (-54, 0), (-54, 1), (-54, 2), (-54, 3), (-54, 4), (-54, 5), (-54, 6), (-54, 7), (-54, 8),
    (-54, 9), (-54, 10), (-54, 11), (-54, 12), (-54, 13), (-54, 14), (-54, 15), (-54, 16), (-54, 17), (-54, 18),
    (-54, 19), (-54, 20), (-54, 21), (-54, 22), (-54, 23), (-54, 24), (-54, 25), (-54, 26), (-54, 27), (-54, 28),
    (-54, 29), (-53, -33), (-53, -32), (-53, -31), (-53, 30), (-53, 31), (-53, 32), (-52, -35), (-52, -34),
    (-52, 33), (-52, 34), (-51, -36), (-51, 35), (-50, -36), (-50, 35), (-49, -37), (-49, 36), (-48, -37), (-48, 36),
    (-47, -37), (-47, 36), (-46, -38), (-46, 37), (-45, -38), (-45, 37), (-44, -38), (-44, 37), (-43, -38),
    (-43, 37), (-42, -38), (-42, 37), (-41, -38), (-41, 37), (-40, -38), (-40, 37), (-39, -38), (-39, 37),
    (-38, -38), (-38, 37), (-37, -38), (-37, 37), (-36, -38), (-36, 37), (-35, -38), (-35, 37), (-34, -38),
    (-34, 37), (-33, -38), (-33, 37), (-32, -38), (-32, 37), (-31, -38), (-31, 37), (-30, -38), (-30, 37),
    (-29, -38), (-29, 37), (-28, -38), (-28, 37), (-27, -38), (-27, 37), (-26, -38), (-26, 37), (-25, -38),
    (-25, 37), (-24, -38), (-24, 37), (-23, -38), (-23, 37), (-22, -38), (-22, 37), (-21, -38), (-21, 37),
    (-20, -38), (-20, 37), (-19, -38), (-19, 37), (-18, -38), (-18, 37), (-17, -38), (-17, 37), (-16, -38),
    (-16, 37), (-15, -38), (-15, 37), (-14, -38), (-14, 37), (-13, -38), (-13, 37), (-12, -38), (-12, 37),
    (-11, -38), (-11, 37), (-10, -38), (-10, 37), (-9, -38), (-9, 37), (-8, -38), (-8, 37), (-7, -38), (-7, 37),
    (-6, -38), (-6, 37), (-5, -38), (-5, 37), (-4, -38), (-4, 37), (-3, -38), (-3, 37), (-2, -38), (-2, 37),
    (-1, -38), (-1, 37), (0, -38), (0, 37), (1, -38), (1, 37), (2, -38), (2, 37), (3, -38), (3, 37), (4, -38),
    (4, 37), (5, -38), (5, 37), (6, -38), (6, 37), (7, -38), (7, 37), (8, -38), (8, 37), (9, -38), (9, 37),
    (10, -38), (10, 37), (11, -38), (11, 37), (12, -38), (12, 37), (13, -38), (13, 37), (14, -38), (14, 37),
    (15, -38), (15, 37), (16, -38), (16, 37), (17, -38), (17, 37), (18, -38), (18, 37), (19, -38), (19, 37),
    (20, -38), (20, 37), (21, -38), (21, 37), (22, -38), (22, 37), (23, -38), (23, 37), (24, -38), (24, 37),
    (25, -38), (25, 37), (26, -38), (26, 37), (27, -38), (27, 37), (28, -38), (28, 37), (29, -38), (29, 37),
    (30, -38), (30, 37), (31, -38), (31, 37), (32, -38), (32, 37), (33, -38), (33, 37), (34, -38), (34, 37),
    (35, -38), (35, 37), (36, -38), (36, 37), (37, -38), (37, 37), (38, -38), (38, 37), (39, -38), (39, 37),
    (40, -38), (40, 37), (41, -38), (41, 37), (42, -38), (42, 37), (43, -38), (43, 37), (44, -38), (44, 37),
    (45, -38), (45, 37), (46, -37), (46, 36), (47, -37), (47, 36), (48, -37), (48, 36), (49, -36), (49, 35),
    (50, -36), (50, 35), (51, -35), (51, -34), (51, 33), (51, 34), (52, -33), (52, -32), (52, -31), (52, 30),
    (52, 31), (52, 32), (53, -30), (53, -29), (53, -28), (53, -27), (53, -26), (53, -25), (53, -24), (53, -23),
    (53, -22), (53, -21), (53, -20), (53, -19), (53, -18), (53, -17), (53, -16), (53, -15), (53, -14), (53, -13),
    (53, -12), (53, -11), (53, -10), (53, -9), (53, -8), (53, -7), (53, -6), (53, -5), (53, -4), (53, -3), (53, -2),
    (53, -1), (53, 0), (53, 1), (53, 2), (53, 3), (53, 4), (53, 5), (53, 6), (53, 7), (53, 8), (53, 9), (53, 10),
    (53, 11), (53, 12), (53, 13), (53, 14), (53, 15), (53, 16), (53, 17), (53, 18), (53, 19), (53, 20), (53, 21),
    (53, 22), (53, 23), (53, 24), (53, 25), (53, 26), (53, 27), (53, 28), (53, 29),
)


class Direction(IntEnum):
    up = 0
    down = 1
    left = 2
    right = 3
    upleft = 4
    downleft = 5
    upright = 6
    downright = 7
    error = 8


def initialize() -> None:
    global _radius_dx, _radius_dy, _walk_grid
    _radius_positions.clear()
    for x in range(-256, 257):
        for y in range(-256, 257):
            dist = approximate_distance(0, x, 0, y)
            if dist > 256:
                continue
            _radius_positions.append((dist, x, y))
    _radius_positions.sort()

    _radius_values[:] = [radius for radius, _, _ in _radius_positions]
    _radius_dx = np.array([x for _, x, _ in _radius_positions], dtype=np.int64)
    _radius_dy = np.array([y for _, _, y in _radius_positions], dtype=np.int64)
    _walk_grid = None


def _walkability() -> NDArray[np.bool_]:
    global _walk_grid
    if _walk_grid is None:
        _walk_grid = bwapi.Broodwar.getWalkabilityGrid()
    return _walk_grid


def _unwalkable_or_invalid(px: NDArray[np.int64], py: NDArray[np.int64]) -> NDArray[np.bool_]:
    """For arrays of pixel positions, whether each is off the map or on an unwalkable walk tile."""
    grid = _walkability()
    width, height = grid.shape
    valid = (px >= 0) & (py >= 0) & (px < width * 8) & (py < height * 8)
    walkable_here = grid[np.where(valid, px >> 3, 0), np.where(valid, py >> 3, 0)]
    result: NDArray[np.bool_] = ~(valid & walkable_here)
    return result


def approximate_distances(dx: NDArray[np.int64], dy: NDArray[np.int64]) -> NDArray[np.int64]:
    """approximate_distance over arrays of (non-negative) deltas."""
    lo = np.minimum(dx, dy)
    hi = np.maximum(dx, dy)
    min_calc = (3 * lo) >> 3
    result: NDArray[np.int64] = np.where(lo <= (hi >> 2), hi,
                                         (min_calc >> 5) + min_calc + hi - (hi >> 4) - (hi >> 6))
    return result


def approximate_distance(x1: int, x2: int, y1: int, y2: int) -> int:
    lo = abs(x1 - x2)
    hi = abs(y1 - y2)
    if hi < lo:
        lo, hi = hi, lo
    if lo <= (hi >> 2):
        return hi
    min_calc = (3 * lo) >> 3
    return (min_calc >> 5) + min_calc + hi - (hi >> 4) - (hi >> 6)


def edge_to_edge_distance(first_type: UnitType, first_center: Position,
                          second_type: UnitType, second_center: Position) -> int:
    first_left = first_center.x - first_type.dimensionLeft()
    first_top = first_center.y - first_type.dimensionUp()
    first_right = first_center.x + first_type.dimensionRight()
    first_bottom = first_center.y + first_type.dimensionDown()
    second_left = second_center.x - second_type.dimensionLeft()
    second_top = second_center.y - second_type.dimensionUp()
    second_right = second_center.x + second_type.dimensionRight()
    second_bottom = second_center.y + second_type.dimensionDown()

    x_dist = max(first_left - second_right - 1, second_left - first_right - 1, 0)
    y_dist = max(first_top - second_bottom - 1, second_top - first_bottom - 1, 0)
    return approximate_distance(x_dist, 0, y_dist, 0)


def edge_to_point_distance(unit_type: UnitType, center: Position, point: Position) -> int:
    left = center.x - unit_type.dimensionLeft()
    top = center.y - unit_type.dimensionUp()
    right = center.x + unit_type.dimensionRight()
    bottom = center.y + unit_type.dimensionDown()

    x_dist = max(left - point.x, point.x - right - 1, 0)
    y_dist = max(top - point.y, point.y - bottom - 1, 0)
    return approximate_distance(x_dist, 0, y_dist, 0)


def edge_to_tile_distance(unit_type: UnitType, top_left: TilePosition, tile: TilePosition) -> int:
    bottom_right = top_left + unit_type.tileSize()
    x_dist = max(top_left.x - tile.x - 1, tile.x - bottom_right.x - 1, 0)
    y_dist = max(top_left.y - tile.y - 1, tile.y - bottom_right.y - 1, 0)
    return approximate_distance(x_dist, 0, y_dist, 0)


def nearest_point_on_edge(point: Position, unit_type: UnitType, center: Position) -> Position:
    left = center.x - unit_type.dimensionLeft()
    top = center.y - unit_type.dimensionUp()
    right = center.x + unit_type.dimensionRight()
    bottom = center.y + unit_type.dimensionDown()
    return Position(left if point.x < left else (right if point.x > right else point.x),
                    top if point.y < top else (bottom if point.y > bottom else point.y))


def overlaps(first_type: UnitType, first_center: Position, second_type: UnitType, second_center: Position) -> bool:
    first_left = first_center.x - first_type.dimensionLeft()
    first_top = first_center.y - first_type.dimensionUp()
    first_right = first_center.x + first_type.dimensionRight()
    first_bottom = first_center.y + first_type.dimensionDown()
    second_left = second_center.x - second_type.dimensionLeft()
    second_top = second_center.y - second_type.dimensionUp()
    second_right = second_center.x + second_type.dimensionRight()
    second_bottom = second_center.y + second_type.dimensionDown()
    return (first_right >= second_left and second_right >= first_left
            and first_bottom >= second_top and second_bottom >= first_top)


def overlaps_point(unit_type: UnitType, center: Position, point: Position) -> bool:
    left = center.x - unit_type.dimensionLeft()
    top = center.y - unit_type.dimensionUp()
    right = center.x + unit_type.dimensionRight()
    bottom = center.y + unit_type.dimensionDown()
    return right >= point.x >= left and bottom >= point.y >= top


def overlaps_tiles(first_top_left: TilePosition, first_width: int, first_height: int,
                   second_top_left: TilePosition, second_width: int, second_height: int) -> bool:
    return (first_top_left.x < second_top_left.x + second_width
            and first_top_left.y < second_top_left.y + second_height
            and first_top_left.x + first_width > second_top_left.x
            and first_top_left.y + first_height > second_top_left.y)


def walkable(unit_type: UnitType, center: Position) -> bool:
    # Stardust checks isWalkable(x / 8, y / 8) for every pixel the unit covers. The walk tiles that covers are the
    # rectangle between the corners' walk tiles, and isWalkable is false off the map.
    grid = _walkability()
    left = cdiv(center.x - unit_type.dimensionLeft(), 8)
    right = cdiv(center.x + unit_type.dimensionRight(), 8)
    top = cdiv(center.y - unit_type.dimensionUp(), 8)
    bottom = cdiv(center.y + unit_type.dimensionDown(), 8)
    if left < 0 or top < 0 or right >= grid.shape[0] or bottom >= grid.shape[1]:
        return False
    return bool(grid[left:right + 1, top:bottom + 1].all())


def find_closest_unwalkable_position(start: Position, search_radius: int,
                                     further_from: Position = Positions.Invalid) -> Position:
    if search_radius > 256:
        return find_closest_unwalkable_position_near(start, start, search_radius, further_from)

    has_further_from = further_from != Positions.Invalid
    further_from_angle = math.atan2(start.y - further_from.y, start.x - further_from.x) if has_further_from else 0.0

    # Stardust walks _radius_positions in order and returns the first offset that is invalid or unwalkable (and, if
    # further_from is given, in the right direction), stopping past search_radius. This checks the same offsets in
    # the same order, a growing chunk at a time with numpy, since the answer is usually close. (np.arctan2 is
    # libm's atan2 on the platforms we use, so the angles match math.atan2.)
    end = bisect.bisect_right(_radius_values, search_radius)
    chunk_start = 0
    chunk_size = 256
    while chunk_start < end:
        chunk_end = min(end, chunk_start + chunk_size)
        xs = start.x + _radius_dx[chunk_start:chunk_end]
        ys = start.y + _radius_dy[chunk_start:chunk_end]
        found = _unwalkable_or_invalid(xs, ys)

        # If this is defined, we expect this position to be opposite the one we find here
        if has_further_from:
            angles = np.arctan2((ys - start.y).astype(np.float64), (xs - start.x).astype(np.float64))
            found &= ~(np.abs(angles - further_from_angle) > _PI / 4.0)

        hits = np.flatnonzero(found)
        if hits.size:
            return Position(int(xs[hits[0]]), int(ys[hits[0]]))

        chunk_start = chunk_end
        chunk_size *= 4

    return Positions.Invalid


def find_closest_unwalkable_position_near(start: Position, close_to: Position, search_radius: int,
                                          further_from: Position = Positions.Invalid) -> Position:
    has_further_from = further_from != Positions.Invalid
    further_from_angle = math.atan2(start.y - further_from.y, start.x - further_from.x) if has_further_from else 0.0

    # Stardust scans the square x-major and keeps the first position with the smallest distance to close_to among
    # the invalid or unwalkable ones (in the right direction, if further_from is given). Same, with numpy.
    side = 2 * search_radius + 1
    xs = np.repeat(np.arange(start.x - search_radius, start.x + search_radius + 1, dtype=np.int64), side)
    ys = np.tile(np.arange(start.y - search_radius, start.y + search_radius + 1, dtype=np.int64), side)
    found = _unwalkable_or_invalid(xs, ys)

    # If this is defined, we expect this position to be opposite the one we find here
    if has_further_from:
        angles = np.arctan2((ys - start.y).astype(np.float64), (xs - start.x).astype(np.float64))
        found &= ~(np.abs(angles - further_from_angle) > _PI / 2.0)

    hits = np.flatnonzero(found)
    if not hits.size:
        return Positions.Invalid

    dists = approximate_distances(np.abs(xs[hits] - close_to.x), np.abs(ys[hits] - close_to.y))
    best = hits[int(np.argmin(dists))]  # argmin returns the first of equal minimums, like Stardust's strict <
    return Position(int(xs[best]), int(ys[best]))


def _steps_between(start_x: int, start_y: int, end_x: int, end_y: int, dist_total: int) -> list[tuple[int, int]]:
    xdiff = end_x - start_x
    ydiff = end_y - start_y
    steps = []
    for dist_stop in range(dist_total + 1):
        fraction = dist_stop / dist_total if dist_total else 0.0
        steps.append((start_x + cround(fraction * xdiff), start_y + cround(fraction * ydiff)))
    return steps


def find_walkable_positions_between(start: Position, end: Position) -> list[Position]:
    game = bwapi.Broodwar
    result: list[Position] = []
    added: set[Position] = set()
    for x, y in _steps_between(start.x, start.y, end.x, end.y, start.getApproxDistance(end)):
        pos = Position(x, y)
        if not pos.isValid() or not game.isWalkable(WalkPosition(pos)) or pos in added:
            continue
        result.append(pos)
        added.add(pos)
    return result


def find_tiles_between(start: TilePosition, end: TilePosition) -> list[TilePosition]:
    result: list[TilePosition] = []
    added: set[TilePosition] = set()
    for x, y in _steps_between(start.x, start.y, end.x, end.y, start.getApproxDistance(end)):
        pos = TilePosition(x, y)
        if not pos.isValid() or pos in added:
            continue
        result.append(pos)
        added.add(pos)
    return result


def find_walk_tiles_between(start: WalkPosition, end: WalkPosition) -> list[WalkPosition]:
    result: list[WalkPosition] = []
    added: set[WalkPosition] = set()
    for x, y in _steps_between(start.x, start.y, end.x, end.y, start.getApproxDistance(end)):
        pos = WalkPosition(x, y)
        if not pos.isValid() or pos in added:
            continue
        result.append(pos)
        added.add(pos)
    return result


def center_of_unit(top_left: Position | TilePosition, unit_type: UnitType) -> Position:
    """Center of a unit given its top left. For buildings, top left is the top left of the tile placement."""
    pos = Position(top_left) if isinstance(top_left, TilePosition) else top_left
    if unit_type.isBuilding():
        return Position(pos.x + unit_type.tileWidth() * 16, pos.y + unit_type.tileHeight() * 16)
    return Position(pos.x + unit_type.dimensionLeft() + 1, pos.y + unit_type.dimensionUp() + 1)


def scale_vector(vector: Position, length: int) -> Position:
    magnitude = approximate_distance(vector.x, 0, vector.y, 0)
    if magnitude == 0:
        return Positions.Invalid

    # C++ float arithmetic
    scale = f32(f32(length) / f32(magnitude))
    return Position(to_int(f32(f32(vector.x) * scale)), to_int(f32(f32(vector.y) * scale)))


def perpendicular_vector(vector: Position, length: int) -> Position:
    return scale_vector(Position(-vector.y, vector.x), length)


def ten_distance_positions_around_patch(patch_center: Position) -> list[Position]:
    return [Position(patch_center.x + x, patch_center.y + y) for x, y in _TEN_DISTANCE_OFFSETS_AROUND_PATCH]


def direction_from_building(tile: TilePosition, size: TilePosition, pos: Position,
                            four_directions: bool = False) -> Direction:
    """Relative direction between a building and a position."""
    left = tile.x * 32
    right = (tile.x + size.x) * 32
    top = tile.y * 32
    bottom = (tile.y + size.y) * 32

    if left <= pos.x <= right and top <= pos.y <= bottom:
        return Direction.error

    if left <= pos.x <= right:
        return Direction.up if pos.y < top else Direction.down
    if top <= pos.y <= bottom:
        return Direction.left if pos.x < left else Direction.right
    if pos.x < left:
        if pos.y < top:
            if four_directions:
                return Direction.up if (top - pos.y) > (left - pos.x) else Direction.left
            return Direction.upleft
        if four_directions:
            return Direction.down if (pos.y - bottom) > (left - pos.x) else Direction.left
        return Direction.downleft
    if pos.y < top:
        if four_directions:
            return Direction.up if (top - pos.y) > (pos.x - right) else Direction.right
        return Direction.upright
    if four_directions:
        return Direction.down if (pos.y - bottom) > (pos.x - right) else Direction.right
    return Direction.downright


def bw_heading(bwapi_angle: float) -> int:
    """Converts a unit's BWAPI angle to BW's heading representation (1/256th of a circle)."""
    # The reverse of what BWAPI does in UnitUpdate when it converts the BW heading into an angle
    heading = cround((bwapi_angle * 128.0) / _PI) + 64
    if heading > 127:
        heading -= 256
    return heading


def bw_direction(vector: Position) -> int:
    """The direction along a vector in BW representation (1/256th of a circle)."""
    # Combination of the logic in openbw's xy_direction and atan
    if vector.x == 0:
        return 0 if vector.y <= 0 else -128

    raw = cdiv(vector.y * 256, vector.x)
    negative = raw < 0
    if negative:
        raw = -raw

    r = bisect.bisect_left(_TAN_TABLE, raw)
    return (64 if vector.x > 0 else -64) + (-r if negative else r)


def bw_angle_diff(a: int, b: int) -> int:
    """Difference between two angles in BW representation (1/256th of a circle)."""
    diff = a - b
    if diff > 127:
        diff -= 256
    if diff < -128:
        diff += 256
    return abs(diff)


def bw_angle_add(a: int, b: int) -> int:
    """Adds two angles in BW representation (1/256th of a circle)."""
    result = a + b
    while result > 127:
        result -= 256
    while result < -128:
        result += 256
    return result


def bw_movement(x: int, y: int, heading: int, desired_heading: int, turn_rate: int, speed: int, acceleration: int,
                top_speed: int) -> tuple[int, int, int, int]:
    """Simulates a frame of movement of a unit; returns the new (x, y, heading, speed).

    All values are in BW representation (positions and speeds are multiples of 1/256, angles are 1/256th of a circle).
    """
    # BW updates position, then heading, then speed
    dir_x, dir_y = _DIRECTION_TABLE[heading + 256 if heading < 0 else heading]
    x += (dir_x * speed) >> 8
    y += (dir_y * speed) >> 8

    if bw_angle_diff(heading, desired_heading) < turn_rate:
        heading = desired_heading
    else:
        first = bw_angle_add(heading, turn_rate)
        second = bw_angle_add(heading, -turn_rate)
        heading = first if bw_angle_diff(first, desired_heading) < bw_angle_diff(second, desired_heading) else second

    speed = max(0, min(top_speed, speed + acceleration))
    return x, y, heading, speed


class Spiral:
    """Iterates offsets in a square spiral around the origin: (0,0), (1,0), (1,1), (0,1), (-1,1), ..."""

    def __init__(self) -> None:
        self.x = 0
        self.y = 0
        self.radius = 1
        self._direction = 0

    def next(self) -> None:
        if self._direction == 0:
            self.x += 1
            if self.x == self.radius:
                self._direction += 1
        elif self._direction == 1:
            self.y += 1
            if self.y == self.radius:
                self._direction += 1
        elif self._direction == 2:
            self.x -= 1
            if -self.x == self.radius:
                self._direction += 1
        else:
            self.y -= 1
            if -self.y == self.radius:
                self._direction = 0
                self.radius += 1

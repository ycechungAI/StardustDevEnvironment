"""Port of Util/Boids.{h,cpp}: combining weighted movement vectors ("boids") into a walkable move target, avoiding
no-go areas and resolving collisions with terrain.

C++ overloads AddSeparation on whether the detection limit is a double (a factor of the unit widths) or an int (a
pixel distance): these are add_separation_factor and add_separation_limit. Separation is returned rather than
accumulated into reference arguments. The verbose boid drawing (DRAW_BOIDS) is omitted.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, Positions, TilePosition, UnitCommandTypes
from stardust.cpp import INT_MAX, to_int
from stardust.util import geo

if TYPE_CHECKING:
    from stardust.units.unit import Unit


def _is_valid_and_walkable(pos: Position) -> bool:
    from stardust.map import game_map

    return pos.isValid() and game_map.is_walkable(pos.x >> 5, pos.y >> 5)


def _walkable_position_along_vector(start: Position, vector: Position, min_dist: int) -> Position:
    extent = geo.approximate_distance(0, vector.x, 0, vector.y)
    furthest_walkable = Positions.Invalid
    for dist in range(16, extent, 16):
        pos = start + geo.scale_vector(vector, dist)
        if not _is_valid_and_walkable(pos):
            return furthest_walkable
        if dist >= min_dist:
            furthest_walkable = pos

    pos = start + vector
    if _is_valid_and_walkable(pos):
        furthest_walkable = pos
    return furthest_walkable


def _walkable_position_along_vector_with_collision(start: Position, vector: Position) -> tuple[Position, Position]:
    """The furthest walkable position along the vector, and the sum of the collision vectors along the way (invalid if
    the whole vector is walkable)."""
    from stardust.map import game_map

    extent = geo.approximate_distance(0, vector.x, 0, vector.y)
    furthest_walkable = start
    collision_vector = game_map.collision_vector(start.x >> 5, start.y >> 5)
    for dist in range(16, extent, 16):
        pos = start + geo.scale_vector(vector, dist)
        if not _is_valid_and_walkable(pos):
            return furthest_walkable, collision_vector
        furthest_walkable = pos
        collision_vector = collision_vector + game_map.collision_vector(pos.x >> 5, pos.y >> 5)

    pos = start + vector
    if _is_valid_and_walkable(pos):
        furthest_walkable = pos
        collision_vector = Positions.Invalid
    return furthest_walkable, collision_vector


def avoid_no_go_area(unit: Unit) -> Position:
    from stardust.map import game_map, no_go_areas

    # If the unit itself is not currently in a no-go area, just stay put
    if not no_go_areas.is_no_go(unit.tile_position_x, unit.tile_position_y):
        return unit.last_position

    # Find the closest tile that is walkable and not in a no-go area. We prefer tiles that are not in a mineral line.
    game = bwapi.Broodwar
    closest_dist = INT_MAX
    closest_is_outside_mineral_line = False
    closest_x = unit.last_position.x
    closest_y = unit.last_position.y
    for y in range(unit.tile_position_y - 5, unit.tile_position_y + 5):
        if y < 0 or y >= game.mapHeight():
            continue
        for x in range(unit.tile_position_x - 5, unit.tile_position_x + 5):
            if x < 0 or x >= game.mapWidth():
                continue
            if not unit.is_flying and not game_map.is_walkable(x, y):
                continue
            if no_go_areas.is_no_go(x, y):
                continue
            if unit.type.isWorker() and game_map.borders_mineral_patch(x, y):
                continue

            is_in_mineral_line = game_map.is_in_own_mineral_line(x, y)
            if is_in_mineral_line and closest_is_outside_mineral_line:
                continue

            dist = geo.approximate_distance(unit.tile_position_x, x, unit.tile_position_y, y)
            if dist < closest_dist or (not is_in_mineral_line and not closest_is_outside_mineral_line):
                closest_dist = dist
                closest_x = x * 32 + 16
                closest_y = y * 32 + 16
                closest_is_outside_mineral_line = not is_in_mineral_line

    return Position(closest_x, closest_y)


def _separation(unit: Unit, other: Unit, detection_limit: float, weight: float, separation_x: int,
                separation_y: int) -> tuple[int, int]:
    dist = unit.get_distance(other)

    # Push away with maximum force at 0 distance, no force at the detection limit
    dist_factor = 1.0 - dist / detection_limit
    center_dist = geo.approximate_distance(unit.last_position.x, other.last_position.x, unit.last_position.y,
                                           other.last_position.y)
    if center_dist == 0:
        return separation_x, separation_y
    scaling_factor = dist_factor * dist_factor * weight / center_dist
    separation_x -= to_int((other.last_position.x - unit.last_position.x) * scaling_factor)
    separation_y -= to_int((other.last_position.y - unit.last_position.y) * scaling_factor)
    return separation_x, separation_y


def add_separation_factor(unit: Unit, other: Unit, detection_limit_factor: float, weight: float, separation_x: int,
                          separation_y: int) -> tuple[int, int]:
    """Separation from another unit within the larger of the two unit widths times the factor."""
    detection_limit = max(unit.type.width(), other.type.width()) * detection_limit_factor
    if unit.get_distance(other) >= to_int(detection_limit):
        return separation_x, separation_y
    return _separation(unit, other, detection_limit, weight, separation_x, separation_y)


def add_separation_limit(unit: Unit, other: Unit, detection_limit: int, weight: float, separation_x: int,
                         separation_y: int) -> tuple[int, int]:
    """Separation from another unit within the given distance in pixels."""
    if unit.get_distance(other) >= detection_limit:
        return separation_x, separation_y
    return _separation(unit, other, float(detection_limit), weight, separation_x, separation_y)


def compute_position(unit: Unit, x: list[int], y: list[int], scale: int, min_dist: int = 16,
                     collision: bool = False) -> Position:
    """The move target for the summed vectors, or invalid if the unit cannot move that way."""
    from stardust.map import game_map, no_go_areas

    # Increase the minimum distance so it is at least the size of the unit; the scale must be at least the min dist
    min_dist = max(min_dist, max(unit.type.width(), unit.type.height()))
    if scale > 0:
        scale = max(scale, min_dist)

    # Start by combining into a (possibly scaled) vector
    total_x = sum(x)
    total_y = sum(y)
    vector = geo.scale_vector(Position(total_x, total_y), scale) if scale > 0 else Position(total_x, total_y)

    # If the desired target position is inside a no-go area, use special logic instead to avoid the no-go area
    tile = TilePosition(unit.last_position + vector)
    if tile.isValid() and no_go_areas.is_no_go_tile(tile):
        return avoid_no_go_area(unit)

    # If the vector is zero or invalid, the unit doesn't want to move, so return its current position
    if vector == Positions.Invalid or (total_x == 0 and total_y == 0):
        return unit.last_position

    # Flying units don't need to worry about walkability or collisions
    if unit.is_flying:
        game = bwapi.Broodwar
        pos = unit.last_position + vector
        # Snap to map edges
        return Position(max(0, min(pos.x, game.mapWidth() * 32 - 1)), max(0, min(pos.y, game.mapHeight() * 32 - 1)))

    # If we aren't worried about collisions, trace a path along the vector and move if there is a walkable position
    # far enough away
    if not collision:
        return _walkable_position_along_vector(unit.last_position, vector, min_dist)

    # We want to consider collisions, so trace the path along the vector as usual, but if we meet a collision, try to
    # resolve it. We do this by accumulating a collision vector along the path and applying it to the previous
    # position to get out of a collision.
    pos, collision_vector = _walkable_position_along_vector_with_collision(unit.last_position, vector)

    # If there was no collision, use the position
    if collision_vector == Positions.Invalid:
        return pos

    # There was a collision. Our approach to resolving collisions is to pull the last found walkable position based on
    # the average collision vector collected during the search. We prefer to pull it directly in the direction of the
    # collision vector, but in cases where this is in opposition to our desired movement, we may choose a
    # perpendicular vector instead.

    # Scale the vectors so we can apply them incrementally in our collision resolution
    collision_vector = geo.scale_vector(collision_vector, 16)
    if collision_vector == Positions.Invalid:
        return Positions.Invalid
    vector_step = geo.scale_vector(vector, 16)

    # Compute perpendicular vectors
    collision_angle = geo.bw_direction(collision_vector)
    vector_angle = geo.bw_direction(vector)
    assert unit.bwapi_unit is not None
    last_command = unit.bwapi_unit.getLastCommand()
    move_target = last_command.getTargetPosition() if last_command.type == UnitCommandTypes.Move else Positions.Invalid

    def get_perpendicular_vector(reference: Position, reference_angle: int, compare_angle: int) -> Position:
        perpendicular_vector = geo.perpendicular_vector(reference, 16)

        # There are two perpendicular vectors, so we need to pick the best one. We use the angle to determine it,
        # unless they are both very close, in which case we use the current move target.
        angle_diff = (geo.bw_angle_diff(compare_angle, geo.bw_angle_add(reference_angle, 64))
                      - geo.bw_angle_diff(compare_angle, geo.bw_angle_add(reference_angle, -64)))
        if abs(angle_diff) < 16:
            first_dist = move_target.getApproxDistance(pos + perpendicular_vector)
            second_dist = move_target.getApproxDistance(pos - perpendicular_vector)
            if first_dist < second_dist:
                return perpendicular_vector
            if second_dist < first_dist:
                return Position(-perpendicular_vector.x, -perpendicular_vector.y)

        if angle_diff < 0:
            return perpendicular_vector
        return Position(-perpendicular_vector.x, -perpendicular_vector.y)

    perpendicular_collision_vector = get_perpendicular_vector(collision_vector, collision_angle, vector_angle)
    perpendicular_vector = get_perpendicular_vector(vector, vector_angle, collision_angle)

    def adjust_for_collision(collision_step: Position, step: Position) -> Position | None:
        """Repeatedly add either a step of the vector or the collision vector to get to a walkable tile until we've
        reached the desired distance."""
        current = pos
        candidates = (step, collision_step, step + collision_step, collision_step + collision_step,
                      step + collision_step + collision_step)
        for _ in range(6):
            for candidate in candidates:
                next_position = current + candidate
                if _is_valid_and_walkable(next_position):
                    current = next_position
                    break
            else:
                return None

            if unit.last_position.getApproxDistance(current) >= min_dist:
                return current

        # Exceeded max number of tries
        return None

    # Try in this order
    for collision_step, step in ((collision_vector, vector_step),
                                 (collision_vector, perpendicular_vector),
                                 (perpendicular_collision_vector, vector_step),
                                 (collision_vector, collision_vector)):
        adjusted = adjust_for_collision(collision_step, step)
        if adjusted is not None:
            # If the final position borders an unwalkable tile, move the position to the center of the tile.
            # Otherwise we might generate a position where the unit can't actually fit.
            if game_map.unwalkable_proximity(adjusted.x >> 5, adjusted.y >> 5) == 1:
                adjusted = Position(((adjusted.x >> 5) << 5) + 16, ((adjusted.y >> 5) << 5) + 16)
            return adjusted

    return Positions.Invalid

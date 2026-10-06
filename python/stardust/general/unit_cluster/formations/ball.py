"""Port of General/UnitCluster/Formations/Ball.cpp: cluster movement as a ball.

When we have a navigation grid (which is usually the case), we implement flocking with three boids:
- Goal: moves the unit towards the target position
- Cohesion: keeps the cluster together
- Separation: keeps some space between the units

Collisions with unwalkable terrain are handled by having the unit ignore the boids and move only using the grid.
These cases are hopefully lessened by the fact that our navigation grid favours paths away from unwalkable tiles.
"""

from __future__ import annotations

import math
from typing import TYPE_CHECKING

from bwapi import Position, Positions, UnitTypes
from stardust.cpp import to_int
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.util import boids, geo

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster
    from stardust.units.my_unit import MyUnit

# TODO: These parameters need to be tuned
_GOAL_WEIGHT = 128.0
_COLLISION_WEIGHT = 64
_COHESION_WEIGHT = 64.0
_DEFAULT_SEPARATION_DETECTION_LIMIT_FACTOR = 2.0
_DEFAULT_SEPARATION_WEIGHT = 96.0


def move_as_ball(cluster: UnitCluster, target_position: Position, ball_units: set[MyUnit]) -> bool:
    # We require a grid
    grid = path_finding.get_navigation_grid(target_position)
    if grid is None:
        return False

    # Scaling factor for cohesion boid is based on the size of the squad
    cohesion_factor = _COHESION_WEIGHT / math.sqrt(cluster.area / math.pi)

    # Separation depends on whether the enemy has AOE attackers
    separation_detection_limit_factor = _DEFAULT_SEPARATION_DETECTION_LIMIT_FACTOR
    separation_weight = _DEFAULT_SEPARATION_WEIGHT
    if cluster.enemy_aoe_radius > 0:
        separation_detection_limit_factor = (cluster.enemy_aoe_radius + 16.0) / 32.0
        separation_weight = 160.0

    for unit in ball_units:
        # Get the waypoint to move towards
        # We attempt to move towards the third node ahead of us
        waypoint = path_finding.next_grid_or_choke_waypoint(unit.last_position, target_position, grid, 3, True)

        # If a valid waypoint couldn't be found, defer to normal unit move
        if not waypoint.isValid():
            unit.move_to(target_position)
            continue

        # We have a grid node, so compute our boids

        # Goal: initialize with the offset to the node, then scale to the desired weight
        goal_x = waypoint.x - unit.last_position.x
        goal_y = waypoint.y - unit.last_position.y
        if goal_x != 0 or goal_y != 0:
            goal_scale = _GOAL_WEIGHT / geo.approximate_distance(goal_x, 0, goal_y, 0)
            goal_x = to_int(goal_x * goal_scale)
            goal_y = to_int(goal_y * goal_scale)

        # Collision
        collision_x = 0
        collision_y = 0

        collision_vector = game_map.collision_vector(waypoint.x >> 5, waypoint.y >> 5)
        collision_vector = geo.scale_vector(collision_vector, _COLLISION_WEIGHT)
        if collision_vector != Positions.Invalid:
            collision_target = waypoint + collision_vector
            if game_map.collision_vector(collision_target.x >> 5, collision_target.y >> 5) == Positions.Origin:
                collision_x = collision_vector.x
                collision_y = collision_vector.y

        # Cohesion

        cohesion_x = 0
        cohesion_y = 0

        # We ignore the cohesion boid if this unit is separated from the vanguard unit by a narrow choke
        if unit is cluster.vanguard or path_finding.separating_narrow_choke(
                unit.last_position, cluster.vanguard.last_position, unit.type,
                PathFindingOptions.UseNeighbouringBWEMArea) is None:
            cohesion_x = to_int((cluster.center.x - unit.last_position.x) * cohesion_factor)
            cohesion_y = to_int((cluster.center.y - unit.last_position.y) * cohesion_factor)

        # Separation
        separation_x = 0
        separation_y = 0
        for other in ball_units:
            if other is unit:
                continue
            if other.is_flying:
                continue
            if other.type == UnitTypes.Protoss_Photon_Cannon:
                continue

            separation_x, separation_y = boids.add_separation_factor(
                unit, other, separation_detection_limit_factor, separation_weight, separation_x, separation_y)

        pos = boids.compute_position(unit, [goal_x, collision_x, separation_x, cohesion_x],
                                     [goal_y, collision_y, separation_y, cohesion_y], 80, 48)

        # Default to the goal node if the unit can't move in the direction it wants to
        if pos == Positions.Invalid:
            pos = waypoint

        unit.move_to(pos, True)

    return True

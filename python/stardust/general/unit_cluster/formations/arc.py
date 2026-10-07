"""Port of General/UnitCluster/Formations/Arc.cpp: forms the cluster into an arc around a pivot position."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import Position, Positions, UnitTypes
from stardust.map import game_map
from stardust.util import boids, geo, unit_util

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster

_SEPARATION_DETECTION_LIMIT_FACTOR = 1.25
_SEPARATION_WEIGHT = 32.0


def form_arc(cluster: UnitCluster, pivot: Position, desired_distance: int) -> bool:
    vanguard = cluster.vanguard

    # Don't form an arc if the cluster only consists of flying units
    if vanguard.is_flying:
        return False

    # Don't form an arc if the walkable width at the vanguard unit is too small
    # This indicates we are in a narrow space that isn't well-suited to forming an arc
    if game_map.walkable_width(vanguard.tile_position_x, vanguard.tile_position_y) < 7:
        return False

    vanguard_dist_to_pivot = vanguard.last_position.getApproxDistance(pivot)

    # Now micro the units
    for my_unit in cluster.units:
        if my_unit.type == UnitTypes.Protoss_Photon_Cannon:
            continue

        # If the unit is stuck, unstick it
        if my_unit.unstick():
            continue

        # If the unit is not ready (i.e. is already in the middle of an attack), don't touch it
        if not my_unit.is_ready():
            continue

        # Flying units go to the vanguard
        if my_unit.is_flying:
            my_unit.move_to(vanguard.last_position)
            continue

        # Units that are too far away move towards the vanguard
        dist_to_pivot = my_unit.last_position.getApproxDistance(pivot)
        if dist_to_pivot - vanguard_dist_to_pivot > 128:
            my_unit.move_to(vanguard.last_position)
            continue

        # Separation boid: push away from friendly units
        separation_x = 0
        separation_y = 0
        for other in cluster.units:
            if other is my_unit:
                continue
            if other.is_flying:
                continue
            if other.type == UnitTypes.Protoss_Photon_Cannon:
                continue

            separation_x, separation_y = boids.add_separation_factor(
                my_unit, other, _SEPARATION_DETECTION_LIMIT_FACTOR, _SEPARATION_WEIGHT, separation_x, separation_y)

        # Goal boid: attempt to keep the desired distance from the pivot
        goal_x = 0
        goal_y = 0

        # If we have a separation component, weight the goal at least twice as high
        separation_length = geo.approximate_distance(separation_x, 0, separation_y, 0)
        desired_dist_change = (my_unit.last_position.getApproxDistance(pivot) - desired_distance
                               + (0 if unit_util.is_ranged_unit(my_unit.type) else 32))
        if desired_dist_change > 0:
            vector = geo.scale_vector(pivot - my_unit.last_position,
                                      max(desired_dist_change, separation_length * 2))
        else:
            vector = geo.scale_vector(pivot - my_unit.last_position,
                                      -max(-desired_dist_change, separation_length * 2))

        if vector != Positions.Invalid:
            goal_x = vector.x
            goal_y = vector.y

        pos = boids.compute_position(my_unit, [goal_x, separation_x], [goal_y, separation_y], 0, 4)

        # If the unit can't move in the desired direction, move towards the vanguard unit
        if pos == Positions.Invalid:
            my_unit.move_to(vanguard.last_position)
        else:
            my_unit.move_to(pos, True)

    return True

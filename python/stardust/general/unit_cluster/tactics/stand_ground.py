"""Port of General/UnitCluster/Tactics/StandGround.cpp: stands ground out of range of the enemy.

Usually attempts to form an arc, but will either pull back or group up around the army center if this isn't
possible.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import Position, Positions, TilePosition, UnitTypes
from stardust.cpp import INT_MAX
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.util import geo

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster
    from stardust.units.unit import Unit


def stand_ground(cluster: UnitCluster, enemy_units: set[Unit], target_position: Position) -> None:
    vanguard = cluster.vanguard

    # Get the pivot point
    # Normally the closest enemy ground unit to the vanguard
    pivot = Positions.Invalid
    closest_dist = INT_MAX
    for unit in enemy_units:
        if unit.is_flying:
            continue
        if unit.type == UnitTypes.Protoss_Photon_Cannon:
            continue
        if not unit.last_position_valid:
            continue

        dist = vanguard.get_distance(unit)
        if dist < closest_dist:
            closest_dist = dist
            pivot = unit.last_position

    # If there was no enemy unit to use, pick a spot ahead of the vanguard towards the target position
    if pivot == Positions.Invalid:
        waypoint = path_finding.next_grid_or_choke_waypoint(vanguard.last_position, target_position,
                                                            path_finding.get_navigation_grid(target_position), 3, True)
        if waypoint != Positions.Invalid:
            pivot = vanguard.last_position + geo.scale_vector(waypoint - vanguard.last_position, 8 * 32)

    # Attempt to form the arc
    if pivot.isValid() and cluster.form_arc(pivot, min(15 * 32, vanguard.last_position.getApproxDistance(pivot))):
        return

    # We couldn't form an arc here
    # If the center of the cluster is walkable, move towards it
    # Otherwise move towards the vanguard with the assumption that the center will become walkable soon
    # (otherwise this results in forward motion as units move ahead and become the new vanguard)
    center_tile = TilePosition(cluster.center)
    if game_map.is_walkable(center_tile.x, center_tile.y):
        cluster.move(cluster.center)
    else:
        cluster.move(vanguard.last_position)

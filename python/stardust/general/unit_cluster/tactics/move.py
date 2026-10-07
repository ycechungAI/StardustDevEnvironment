"""Port of General/UnitCluster/Tactics/Move.cpp: moves the cluster towards a target position, flocking when
possible."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import Position, UnitTypes
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster
    from stardust.units.my_unit import MyUnit


def _normal_move(units: set[MyUnit], target_position: Position) -> None:
    for unit in units:
        unit.move_to(target_position)


def _is_movable(unit: MyUnit) -> bool:
    # If the unit is stuck, unstick it
    if unit.unstick():
        return False

    # If the unit is not ready (i.e. is already in the middle of an attack), don't touch it
    if not unit.is_ready():
        return False

    # Cannons don't move
    if unit.type == UnitTypes.Protoss_Photon_Cannon:
        return False

    return True


def _in_leaf_or_narrow_area(unit: MyUnit) -> bool:
    tile = unit.get_tile_position()
    return (game_map.is_in_narrow_choke(tile)
            or game_map.is_in_leaf_area(tile)
            or game_map.walkable_width(tile.x, tile.y) < 4)


def move(cluster: UnitCluster, target_position: Position) -> None:
    # Hook to allow the map-specific override to perform the move
    if game_map.map_specific_override().cluster_move(cluster, target_position):
        return

    vanguard = cluster.vanguard

    # Never try to flock if:
    # - This is not the vanguard cluster
    # - There is only one unit
    # - The cluster is all air units (vanguard will only be air if this is the case)
    # - The vanguard is in a leaf or narrow area
    if (not cluster.is_vanguard_cluster or len(cluster.units) == 1 or vanguard.is_flying
            or _in_leaf_or_narrow_area(vanguard)):
        _normal_move({unit for unit in cluster.units if _is_movable(unit)}, target_position)
        return

    # Split the units into those that can flock and those that can't
    flock_units: set[MyUnit] = set()
    no_flock_units: set[MyUnit] = set()
    for unit in cluster.units:
        if not _is_movable(unit):
            continue

        # Flying units and units in leaf or narrow areas don't flock
        if unit.is_flying or _in_leaf_or_narrow_area(unit):
            no_flock_units.add(unit)
            continue

        # Otherwise flock unless the path to the vanguard unit goes through a narrow choke
        if path_finding.separating_narrow_choke(unit.last_position, vanguard.last_position, unit.type,
                                                PathFindingOptions.UseNeighbouringBWEMArea) is not None:
            no_flock_units.add(unit)
        else:
            flock_units.add(unit)

    _normal_move(no_flock_units, target_position)
    if not cluster.move_as_ball(target_position, flock_units):
        _normal_move(flock_units, target_position)

"""Port of General/UnitCluster/Tactics/Attack.cpp: attacks the targets selected for the cluster's units, forming an
arc first when none of our units are in danger or in range yet."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, UnitTypes
from stardust.map import game_map
from stardust.players import players
from stardust.util import geo, unit_util

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster, UnitsAndTargets
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit


def attack(cluster: UnitCluster, units_and_targets: UnitsAndTargets, target_position: Position) -> None:
    vanguard = cluster.vanguard

    # If this map has chokes that may need to be cleared, check if this cluster needs to do so to reach its target
    override = game_map.map_specific_override()
    if override.has_attack_clearable_chokes():
        # We don't bother if any enemy unit is already in range of a unit in the cluster
        any_unit_in_range = any(target is not None and my_unit.is_in_our_weapon_range(target)
                                for my_unit, target in units_and_targets)

        if not any_unit_in_range and override.cluster_move(cluster, target_position):
            return

    # Form an arc if none of our units are in danger or in range yet
    grid = players.grid(bwapi.Broodwar.enemy())
    vanguard_target: Unit | None = None
    can_form_arc = True
    for my_unit, target in units_and_targets:
        threat = (grid.air_threat(my_unit.last_position) if my_unit.is_flying
                  else grid.ground_threat(my_unit.last_position))
        if threat > 0 or (target is not None and my_unit.is_in_our_weapon_range(target)):
            can_form_arc = False
            break

        if my_unit is vanguard:
            vanguard_target = target
    can_form_arc = can_form_arc and vanguard_target is not None and vanguard_target.can_attack(vanguard)

    # If it is a ground unit, check that our vanguard unit has a clear path to its target
    if can_form_arc and not vanguard.is_flying:
        assert vanguard_target is not None
        for tile in geo.find_tiles_between(vanguard.get_tile_position(), vanguard_target.get_tile_position()):
            if not game_map.is_walkable(tile.x, tile.y):
                can_form_arc = False
                break

    if can_form_arc:
        assert vanguard_target is not None
        pivot = vanguard.last_position + geo.scale_vector(
            vanguard_target.sim_position - vanguard.last_position,
            vanguard.last_position.getApproxDistance(vanguard_target.sim_position) + 64)

        # Determine the desired distance to the pivot
        # If our units are on average within one tile of where we want them, shorten the distance by one tile
        # Otherwise wait until they have formed up around the vanguard

        def effective_dist(unit: MyUnit) -> int:
            return unit.last_position.getApproxDistance(pivot) + (0 if unit_util.is_ranged_unit(unit.type) else 32)

        desired_distance = effective_dist(vanguard)

        accumulator = 0
        count = 0
        count_within_limit = 0
        for my_unit, _ in units_and_targets:
            if my_unit.type == UnitTypes.Protoss_Photon_Cannon:
                continue
            if my_unit.is_flying:
                continue

            dist = effective_dist(my_unit)
            if dist <= desired_distance + 24:
                count_within_limit += 1
            if count_within_limit >= 8:
                break
            accumulator += dist
            count += 1

        if count_within_limit >= 8 or (count > 0 and accumulator // count <= desired_distance + 24):
            desired_distance -= 32

        if cluster.form_arc(pivot, desired_distance):
            return

    # Micro each unit
    for my_unit, target in units_and_targets:
        # If the unit is stuck, unstick it
        if my_unit.unstick():
            continue

        # If the unit is not ready (i.e. is already in the middle of an attack), don't touch it
        if not my_unit.is_ready():
            continue

        # Attack target
        if target is not None:
            my_unit.attack_unit(target, units_and_targets, True, cluster.enemy_aoe_radius)
        elif my_unit.type == UnitTypes.Protoss_Photon_Cannon:
            pass
        else:
            my_unit.move_to(target_position)

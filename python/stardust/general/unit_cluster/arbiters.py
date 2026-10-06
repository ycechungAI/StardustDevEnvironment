"""Port of General/UnitCluster/Arbiters.cpp (Squad::executeArbiters): controls arbiters assigned to a squad. Other
special team usages of arbiters would be in a play.

Arbiters assigned to a squad stay with the vanguard cluster to cloak it and use stasis field when it is advantageous to
do so. Arbiters attempt to keep their distance from enemy anti-air units and science vessels, and flee from EMP.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, Positions, TechTypes, UnitType, UnitTypes
from stardust import common
from stardust.cpp import INT_MAX, to_int
from stardust.general.unit_cluster.unit_cluster import UnitCluster
from stardust.map import game_map, no_go_areas
from stardust.players import players
from stardust.units import units
from stardust.util import boids, geo

if TYPE_CHECKING:
    from stardust.general.squad import Squad
    from stardust.units.my_unit import MyUnit

_GOAL_WEIGHT = 64.0
_THREAT_WEIGHT = 160
_SEPARATION_DETECTION_LIMIT = 160
_SEPARATION_WEIGHT = 160.0

# The range around an arbiter it will consider using stasis
_STASIS_RANGE = 12


def _my_main_position() -> Position:
    my_main = game_map.get_my_main()
    assert my_main is not None
    return my_main.get_position()


def _desired_position(vanguard_cluster: UnitCluster | None) -> Position:
    """We try to stay behind the unit closest to the cluster center."""
    if vanguard_cluster is None:
        return _my_main_position()

    center_unit: MyUnit | None = None
    closest_dist = INT_MAX
    for unit in vanguard_cluster.units:
        dist = unit.get_distance(vanguard_cluster.center)
        if dist < closest_dist:
            closest_dist = dist
            center_unit = unit

    if center_unit is None:
        return _my_main_position()
    return center_unit.last_position


def _get_stasis_arbiter(arbiters: set[MyUnit]) -> tuple[MyUnit | None, int]:
    """The arbiter that should cast stasis, if any, and the total energy of the arbiters considered."""
    total_energy = 0
    if not players.has_researched(bwapi.Broodwar.self(), TechTypes.Stasis_Field):
        return None, total_energy

    best: MyUnit | None = None
    best_energy = 99  # Require enough for a stasis
    for arbiter in arbiters:
        total_energy += arbiter.energy

        # Cast at most one stasis per two seconds
        if (common.current_frame - arbiter.last_cast_frame) < 48:
            return None, total_energy

        if arbiter.energy > best_energy:
            best_energy = arbiter.energy
            best = arbiter

    return best, total_energy


def _get_stasis_position(arbiter: MyUnit | None, center_position: Position, total_energy: int) -> Position:
    if arbiter is None:
        return Positions.Invalid

    game = bwapi.Broodwar
    center_tile = bwapi.TilePosition(center_position)
    grid = players.grid(game.enemy())

    # Determine how many tanks we want to hit, based on the total energy of our arbiters
    # The more energy we have, the more tanks we want to hit with one cast
    minimum_targets = 4
    if total_energy > 300:
        minimum_targets = 2
    elif total_energy > 200:
        minimum_targets = 3

    best = Positions.Invalid
    best_value = minimum_targets - 1
    best_dist = INT_MAX
    for tile_y in range(arbiter.tile_position_y - _STASIS_RANGE, arbiter.tile_position_y + _STASIS_RANGE + 1):
        if tile_y < 0 or tile_y >= game.mapHeight():
            continue

        for tile_x in range(arbiter.tile_position_x - _STASIS_RANGE, arbiter.tile_position_x + _STASIS_RANGE + 1):
            if tile_x < 0 or tile_x >= game.mapWidth():
                continue

            # We only stasis open locations to avoid blocking our own units
            if game_map.unwalkable_proximity(tile_x, tile_y) < 4:
                continue

            # We only stasis when the vanguard cluster center is reasonably close to the target
            if geo.approximate_distance(tile_x, center_tile.x, tile_y, center_tile.y) > 20:
                continue

            # This tile is OK, check if any of its walk positions have a good tile
            for walk_y in range(tile_y << 2, (tile_y + 1) << 2):
                for walk_x in range(tile_x << 2, (tile_x + 1) << 2):
                    value = grid.stasis_range_at(walk_x, walk_y)
                    if value < best_value:
                        continue

                    pos = Position((walk_x << 3) + 4, (walk_y << 3) + 4)

                    dist = arbiter.get_distance(pos)
                    if value > best_value or dist < best_dist:
                        best_value = value
                        best_dist = dist
                        best = pos

    return best


def execute_arbiters(squad: Squad) -> None:
    desired_pos = _desired_position(squad.current_vanguard_cluster)
    enemy = bwapi.Broodwar.enemy()

    # Determine if one of our arbiters should cast stasis
    stasis_arbiter, total_energy = _get_stasis_arbiter(squad.arbiters)
    stasis_position = _get_stasis_position(stasis_arbiter, desired_pos, total_energy)

    # Boids:
    # - Goal (move towards desired position)
    # - Threat (move away from anything that threatens us)
    # - Separation (keep some distance between arbiters so they cloak as large an area as possible)
    # Additionally, we flee from no-go areas (mainly for EMP) and ensure we are always moving
    for arbiter in squad.arbiters:
        # Special case, if we are in a no-go area, move out of it
        if no_go_areas.is_no_go(arbiter.tile_position_x, arbiter.tile_position_y, no_go_areas.TypeFilter.ONLY_DANGER):
            arbiter.move_to(boids.avoid_no_go_area(arbiter))
            continue

        if arbiter is stasis_arbiter and stasis_position != Positions.Invalid:
            bwapi_unit = arbiter.bwapi_unit
            assert bwapi_unit is not None
            bwapi_unit.useTech(TechTypes.Stasis_Field, stasis_position)
            continue

        # Goal
        goal_x = desired_pos.x - arbiter.last_position.x
        goal_y = desired_pos.y - arbiter.last_position.y
        if goal_x != 0 or goal_y != 0:
            goal_scale = _GOAL_WEIGHT / geo.approximate_distance(goal_x, 0, goal_y, 0)
            goal_x = to_int(goal_x * goal_scale)
            goal_y = to_int(goal_y * goal_scale)

        # Threat
        threat_x = 0
        threat_y = 0

        def add_threat_for_type(unit_type: UnitType, radius: int) -> None:
            nonlocal threat_x, threat_y
            detection_limit = radius + 48

            for unit in units.all_enemy_of_type(unit_type):
                if not unit.exists():
                    continue
                if not unit.last_position_valid:
                    continue
                if not unit.can_attack(arbiter):
                    continue

                dist = arbiter.get_distance(unit)
                if dist > detection_limit:
                    continue

                vector = geo.scale_vector(arbiter.last_position - unit.last_position, _THREAT_WEIGHT)
                if vector == Positions.Invalid:
                    continue

                threat_x += vector.x
                threat_y += vector.y

        def air_range(unit_type: UnitType) -> int:
            return players.weapon_range(enemy, unit_type.airWeapon())

        add_threat_for_type(UnitTypes.Terran_Science_Vessel, 8 * 8)
        add_threat_for_type(UnitTypes.Terran_Goliath, air_range(UnitTypes.Terran_Goliath))
        add_threat_for_type(UnitTypes.Terran_Missile_Turret, air_range(UnitTypes.Terran_Missile_Turret))
        add_threat_for_type(UnitTypes.Terran_Marine, air_range(UnitTypes.Terran_Marine))
        add_threat_for_type(UnitTypes.Terran_Ghost, air_range(UnitTypes.Terran_Ghost))
        add_threat_for_type(UnitTypes.Terran_Bunker, air_range(UnitTypes.Terran_Marine) + 32)
        add_threat_for_type(UnitTypes.Terran_Wraith, air_range(UnitTypes.Terran_Wraith))
        add_threat_for_type(UnitTypes.Terran_Valkyrie, air_range(UnitTypes.Terran_Valkyrie))
        add_threat_for_type(UnitTypes.Terran_Battlecruiser, air_range(UnitTypes.Terran_Battlecruiser))

        # Separation
        separation_x = 0
        separation_y = 0
        for other in squad.arbiters:
            if other is arbiter:
                continue

            separation_x, separation_y = boids.add_separation_limit(
                arbiter, other, _SEPARATION_DETECTION_LIMIT, _SEPARATION_WEIGHT, separation_x, separation_y)

        pos = boids.compute_position(arbiter, [goal_x, threat_x, separation_x], [goal_y, threat_y, separation_y],
                                     64, 64)
        if pos == Positions.Invalid:
            pos = desired_pos

        # If the arbiter is close to its desired position and not under threat, allow it to attack
        if threat_x == 0 and threat_y == 0 and arbiter.get_distance(desired_pos) < 64:
            arbiter.attack_move(pos)
        else:
            arbiter.move_to(pos, True)

        # When we have multiple arbiters, the others stay with the cluster vanguard
        if squad.current_vanguard_cluster is not None:
            desired_pos = squad.current_vanguard_cluster.vanguard.last_position

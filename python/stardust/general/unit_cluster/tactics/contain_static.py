"""Port of General/UnitCluster/Tactics/ContainStatic.cpp: contains a base, here defined as somewhere the enemy has
static defense.

We basically just order our units to fan out just outside of enemy threat range, attacking anything that comes into
range of our units.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, Positions, TilePosition, UnitTypes
from stardust.cpp import INT_MAX
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.players import players
from stardust.util import boids, geo

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster
    from stardust.map.choke import Choke
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit

_GOAL_WEIGHT = 64
_GOAL_WEIGHT_IN_RANGE_OF_THREAT = 128
_SEPARATION_DETECTION_LIMIT_FACTOR = 1.5
_SEPARATION_WEIGHT = 96.0


def contain_static(cluster: UnitCluster, enemy_units: set[Unit], target_position: Position) -> None:
    # Perform target selection again to get targets we can attack safely
    units_and_targets = cluster.select_targets(enemy_units, target_position, True)

    grid = players.grid(bwapi.Broodwar.enemy())
    navigation_grid = path_finding.get_navigation_grid(target_position, True)
    my_main = game_map.get_my_main()
    assert my_main is not None

    # Do an initial scan to filter out not-ready units and gather their nearest threats
    # The important thing to track is whether any of our units is in range of static defense
    units_and_targets_and_threats: list[tuple[MyUnit, Unit | None, Unit | None]] = []
    any_in_range_of_static_defense = False
    for my_unit, target in units_and_targets:
        if my_unit.type == UnitTypes.Protoss_Photon_Cannon:
            continue

        # If the unit is stuck, unstick it
        if my_unit.unstick():
            continue

        # If the unit is not ready (i.e. is already in the middle of an attack), don't touch it
        if not my_unit.is_ready():
            continue

        # Determine if this unit is being threatened by something
        threat: Unit | None = None
        static_defense_threat = False
        closest_dist = INT_MAX
        my_range = my_unit.ground_range()
        for unit in enemy_units:
            if not unit.completed:
                continue
            if not unit.can_attack(my_unit):
                continue

            enemy_range = unit.ground_range()

            # For static defense, just take any we are almost in range of
            if unit.is_static_ground_defense():
                dist = my_unit.get_distance(unit)
                if dist > enemy_range + 16:
                    continue

                if dist < closest_dist or not static_defense_threat:
                    threat = unit
                    closest_dist = dist
                    static_defense_threat = True

                continue

            # Ignore non-static-defense if we have found a static defense threat
            if static_defense_threat:
                continue

            # Don't worry about units with a lower range
            if enemy_range <= my_range:
                continue

            dist = my_unit.get_distance(unit, unit.sim_position)

            # Don't worry about units that are further away from what we have already found
            if dist >= closest_dist:
                continue

            # Don't worry about units we can attack
            if dist <= my_range:
                continue

            # Don't worry about units that are well out of their attack range
            if dist > enemy_range + 32:
                continue

            threat = unit
            closest_dist = dist

        units_and_targets_and_threats.append((my_unit, target, threat))
        any_in_range_of_static_defense = any_in_range_of_static_defense or static_defense_threat

    # Now perform micro on the filtered unit list
    for my_unit, target, threat in units_and_targets_and_threats:
        # If no units are in range of static defense and this unit is in range of its target, attack
        if not any_in_range_of_static_defense and target is not None and my_unit.is_in_our_weapon_range(target):
            my_unit.attack_unit(target, units_and_targets, False, cluster.enemy_aoe_radius)
            continue

        # Move to maintain the contain

        # Goal boid: stay just out of range of enemy threats, oriented towards the target position
        pulling_back = False
        goal_x = 0
        goal_y = 0

        # If in range of a threat, just move away from it
        if threat is not None:
            scaled = geo.scale_vector(my_unit.last_position - threat.last_position, _GOAL_WEIGHT_IN_RANGE_OF_THREAT)
            if scaled != Positions.Invalid:
                goal_x = scaled.x
                goal_y = scaled.y
            pulling_back = True
        else:
            # Detect if we are inside a narrow choke that is threatened at one end but not the other
            # In this case we would normally want to contain inside the choke, which is undesirable
            inside_choke: Choke | None = None
            if game_map.is_in_narrow_choke(my_unit.get_tile_position()):
                for choke in game_map.all_chokes():
                    if not choke.is_narrow_choke:
                        continue
                    if my_unit.get_distance(choke.center) > 640:
                        continue
                    if my_unit.get_tile_position() in choke.choke_tiles:
                        inside_choke = choke

                if inside_choke is None:
                    log.get(f"ERROR: {my_unit}: isInNarrowChoke without being able to find choke!")
                elif (not (grid.static_ground_threat(inside_choke.end1_center) == 0
                           and grid.static_ground_threat(inside_choke.end2_center) > 0)
                      and not (grid.static_ground_threat(inside_choke.end2_center) == 0
                               and grid.static_ground_threat(inside_choke.end1_center) > 0)):
                    inside_choke = None

            # Get grid nodes if they are available
            node = navigation_grid.node(my_unit.get_tile_position()) if navigation_grid is not None else None
            next_node = node.next_node if node is not None else None
            second_node = next_node.next_node if next_node is not None else None

            if next_node is not None and second_node is not None:
                next_node_center = next_node.center()
                second_node_center = second_node.center()
            else:
                second_node_center = path_finding.next_grid_or_choke_waypoint(my_unit.last_position,
                                                                              target_position, None, 2)
                if second_node_center == Positions.Invalid:
                    second_node_center = my_unit.last_position + geo.scale_vector(
                        target_position - my_unit.last_position, 64)
                    if second_node_center == Positions.Invalid:
                        second_node_center = target_position

                next_node_center = Position((my_unit.last_position.x + second_node_center.x) >> 1,
                                            (my_unit.last_position.y + second_node_center.y) >> 1)

            # Move towards the second node if none are under threat or in a narrow choke
            # Move away from the second node if the next node is under threat or in a narrow choke
            # Do nothing if the first node is not under threat or in a narrow choke and the second node is
            # (Stardust checks the next node's tile for the narrow choke in both cases.)
            next_in_choke = (inside_choke is not None
                             and game_map.is_in_narrow_choke(TilePosition(next_node_center)))
            length = _GOAL_WEIGHT
            if next_node_center.isValid() and (grid.static_ground_threat(next_node_center) > 0 or next_in_choke):
                length = -_GOAL_WEIGHT
                pulling_back = True
            elif second_node_center.isValid() and (grid.static_ground_threat(second_node_center) > 0
                                                   or next_in_choke):
                length = 0
            if length != 0:
                scaled = geo.scale_vector(second_node_center - my_unit.last_position, length)
                if scaled != Positions.Invalid:
                    goal_x = scaled.x
                    goal_y = scaled.y

        # Separation boid
        separation_x = 0
        separation_y = 0
        if not pulling_back:
            for other, _ in units_and_targets:
                if other is my_unit:
                    continue

                separation_x, separation_y = boids.add_separation_factor(
                    my_unit, other, _SEPARATION_DETECTION_LIMIT_FACTOR, _SEPARATION_WEIGHT, separation_x,
                    separation_y)

        pos = boids.compute_position(my_unit, [goal_x, separation_x], [goal_y, separation_y], 80)

        # If the unit can't move in the desired direction, either move towards the target or towards our main
        # depending on whether we are pulling back
        if pos == Positions.Invalid:
            my_unit.move_to(my_main.get_position() if pulling_back else target_position)
        elif pulling_back and grid.static_ground_threat(pos) > 0:
            # This handles the case where the unit wants to pull back, but the position is threatened
            my_unit.move_to(my_main.get_position())
        else:
            my_unit.move_to(pos, True)

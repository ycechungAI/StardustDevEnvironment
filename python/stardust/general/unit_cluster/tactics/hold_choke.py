"""Port of General/UnitCluster/Tactics/HoldChoke.cpp: holds a choke against the enemy.

Basic idea:
- Melee units hold the line at the choke end
- Ranged units keep in range of the choke end
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, Positions, Races, UnitTypes
from stardust import common
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.util import boids, geo, unit_util

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster, UnitsAndTargets
    from stardust.map.choke import Choke
    from stardust.map.path_finding.navigation_grid import GridNode
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit

_GOAL_WEIGHT = 96
_SEPARATION_DETECTION_LIMIT_FACTOR = 1.0
_SEPARATION_WEIGHT = 64.0


def hold_choke(cluster: UnitCluster, choke: Choke, defend_end: Position, units_and_targets: UnitsAndTargets) -> None:
    game = bwapi.Broodwar
    map_width = game.mapWidth()

    far_end = choke.end2_center if choke.end1_center == defend_end else choke.end1_center
    center_dist = choke.center.getApproxDistance(defend_end)
    melee_dist = max(choke.width // 2, center_dist) + (
        UnitTypes.Terran_Marine.groundWeapon().maxRange() if game.enemy().getRace() == Races.Terran
        else UnitTypes.Protoss_Zealot.groundWeapon().maxRange())

    def in_same_side(first: Unit, second: Unit) -> bool:
        return (choke.tile_side[(first.last_position.x >> 4) + (first.last_position.y >> 4) * map_width * 2]
                == choke.tile_side[(second.last_position.x >> 4) + (second.last_position.y >> 4) * map_width * 2])

    # On the first pass, determine which types of units should attack
    melee_should_attack = False
    ranged_should_attack = False
    for my_unit, target in units_and_targets:
        if target is None:
            continue

        if my_unit.type == UnitTypes.Protoss_Photon_Cannon:
            continue

        # TODO: Check if the unit is in position

        # If the target is close enough to the defend end, attack with all units
        if target.get_distance(defend_end) <= max(choke.width // 2, min(my_unit.ground_range(), center_dist)):
            melee_should_attack = True
            ranged_should_attack = True
            break

        # For ranged units, attack when the target is in range and on the same side
        if unit_util.is_ranged_unit(my_unit.type):
            if not my_unit.is_in_our_weapon_range(target) or not in_same_side(my_unit, target):
                continue

            ranged_should_attack = True

            # We also attack with melee units if we don't outrange the target
            if my_unit.is_in_enemy_weapon_range(target, buffer=16):
                melee_should_attack = True
                break

            continue

        # For melee units vs. ranged units, attack when the target is through the choke or when we are in its attack
        # range
        if unit_util.is_ranged_unit(target.type):
            # Target is through the choke
            if (choke.center.getApproxDistance(target.last_position) >= center_dist
                    and target.last_position.getApproxDistance(defend_end)
                    < target.last_position.getApproxDistance(far_end)):
                ranged_should_attack = True
                melee_should_attack = True
                break

            # We are well within the target's attack range
            if my_unit.is_in_enemy_weapon_range(target, buffer=-16):
                ranged_should_attack = True
                melee_should_attack = True
                break

            continue

        # For melee units vs. melee units, attack if the target is about to be in range
        if not my_unit.is_in_our_weapon_range(target):
            continue

        ranged_should_attack = True
        melee_should_attack = True
        break

    # On the next pass, attack with any units that need to and collect the units that need to move
    # TODO: Include determination of whether the unit is in position
    def should_attack(my_unit: MyUnit, target: Unit) -> bool:
        # Special logic for dragoons
        if my_unit.type == UnitTypes.Protoss_Dragoon:
            # If on cooldown, attack to activate kiting logic if in the target's range and on the same side
            # Otherwise fall through to choke boids
            if my_unit.cooldown_until > common.current_frame + game.getRemainingLatencyFrames() + 2:
                return my_unit.is_in_enemy_weapon_range(target, buffer=64) and in_same_side(my_unit, target)

            # Otherwise attack when ranged should attack or when our target is in range
            # We will move to establish the contain while on cooldown
            return ranged_should_attack or my_unit.is_in_our_weapon_range(target)

        return ranged_should_attack if unit_util.is_ranged_unit(my_unit.type) else melee_should_attack

    units_and_move_targets: list[tuple[MyUnit, int, Position]] = []
    for my_unit, target in units_and_targets:
        if my_unit.type == UnitTypes.Protoss_Photon_Cannon:
            continue

        # If the unit is stuck, unstick it
        if my_unit.unstick():
            continue

        # If the unit is not ready (i.e. is already in the middle of an attack), don't touch it
        if not my_unit.is_ready():
            continue

        dist_to_choke_center = path_finding.get_ground_distance(my_unit.last_position, choke.center, my_unit.type,
                                                                PathFindingOptions.UseNearestBWEMArea)

        # Attack
        if target is not None and should_attack(my_unit, target):
            # If the target is not in our weapon range, use move logic instead if we are a long way from the choke
            if dist_to_choke_center > 300 and not my_unit.is_in_our_weapon_range(target):
                my_unit.move_to(choke.center)
                continue

            my_unit.attack_unit(target, units_and_targets, False, cluster.enemy_aoe_radius)
            continue

        # If the unit is a long way away from the choke, move towards it instead so our pathing kicks in
        if dist_to_choke_center > 300:
            my_unit.move_to(choke.center)
            continue

        # This unit should move, so add it to the list

        # Determine the position this unit is keeping its distance from
        defend_end_dist = my_unit.last_position.getApproxDistance(defend_end)
        far_end_dist = my_unit.last_position.getApproxDistance(far_end)
        dist_to_center = my_unit.last_position.getApproxDistance(choke.center)

        if unit_util.is_ranged_unit(my_unit.type):
            if far_end_dist < defend_end_dist or dist_to_center < defend_end_dist:
                # Unit is on wrong side of the defend end, closer to choke center
                # If the unit is far away, use the choke center as the reference point
                target_pos = choke.center if defend_end_dist > max(center_dist, 32) * 2 else defend_end
                dist_diff = defend_end_dist
            elif dist_to_center < center_dist or defend_end_dist < 32:
                # Unit is on wrong side of the defend end, closer to defend end, or very close to it
                if dist_to_center > 48:
                    target_pos = choke.center
                    dist_diff = -defend_end_dist - my_unit.ground_range()
                else:
                    target_pos = far_end
                    dist_diff = -defend_end_dist - my_unit.ground_range() - center_dist
            else:
                # Normal defend boid
                target_pos = defend_end
                dist_diff = defend_end_dist - my_unit.ground_range()
        else:
            if far_end_dist < defend_end_dist or dist_to_center < defend_end_dist:
                # Unit needs to move towards the defend end
                target_pos = defend_end
                dist_diff = defend_end_dist
            elif target is not None and my_unit.is_in_enemy_weapon_range(target, buffer=48):
                # Unit is in its target's attack range
                target_pos = target.last_position
                dist_diff = my_unit.get_distance(target, target.sim_position) - target.ground_range() - 48
            else:
                target_pos = choke.center
                dist_diff = dist_to_center - melee_dist

        units_and_move_targets.append((my_unit, dist_diff, target_pos))

    # Now execute move orders
    navigation_grid = path_finding.get_navigation_grid(choke.center)
    my_main = game_map.get_my_main()
    assert my_main is not None
    for my_unit, dist_diff, target_pos in units_and_move_targets:
        # Move to maintain the contain
        goal_x = 0
        goal_y = 0

        # Get position to move to, either towards or away from the target position
        scaled = Position(0, 0)
        if dist_diff > 0:
            # Use the grid node to get to the choke center
            current_node: GridNode | None = None
            next_node: GridNode | None = None
            if target_pos == choke.center:
                current_node = (navigation_grid.node(my_unit.get_tile_position()) if navigation_grid is not None
                                else None)
                next_node = current_node.next_node if current_node is not None else None

            if next_node is not None and current_node is not None:
                scaled = geo.scale_vector(next_node.center() - current_node.center(), min(_GOAL_WEIGHT, dist_diff))
            else:
                scaled = geo.scale_vector(target_pos - my_unit.last_position, min(_GOAL_WEIGHT, dist_diff))
        elif dist_diff < 0:
            scaled = geo.scale_vector(my_unit.last_position - target_pos, min(_GOAL_WEIGHT, -dist_diff))

        if scaled != Positions.Invalid:
            goal_x = scaled.x
            goal_y = scaled.y

        # Separation boid
        separation_x = 0
        separation_y = 0
        for other, other_dist_diff, _ in units_and_move_targets:
            if my_unit is other:
                continue

            # Don't move out of the way of units already at their desired position
            if 0 <= other_dist_diff < 5:
                continue

            # When we need to move back, don't move out of the way of units further away
            if dist_diff < 0 and other_dist_diff > dist_diff + 5:
                continue

            separation_x, separation_y = boids.add_separation_factor(
                my_unit, other, _SEPARATION_DETECTION_LIMIT_FACTOR, _SEPARATION_WEIGHT, separation_x, separation_y)

        pos = boids.compute_position(my_unit, [goal_x, separation_x], [goal_y, separation_y], 0)

        if pos != Positions.Invalid:
            my_unit.move_to(pos, True)
            continue

        # If the position is still invalid or unwalkable, move directly to the target position or back depending on
        # which way we are going
        if dist_diff > 0:
            my_unit.move_to(target_pos, True)
        elif dist_diff < 0:
            my_unit.move_to(my_main.get_position())
        else:
            my_unit.move_to(my_unit.last_position, True)

"""Port of General/UnitCluster/Detectors.cpp (Squad::executeDetectors): controls the squad's mobile detectors.

For now detectors just try to keep detection on the closest unit to them requiring detection. If there are no units
requiring detection, they try to stay on top of the squad vanguard unit (cluster vanguard unit closest to the target
position).

In both cases, they try to avoid being in areas where the enemy has both detection and an anti-air threat.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, Positions, TechTypes, UnitTypes
from stardust.cpp import INT_MAX
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.players import players
from stardust.units.my_observer import ObserverActivity
from stardust.util import geo, unit_util

if TYPE_CHECKING:
    from stardust.general.squad import Squad
    from stardust.units.my_observer import MyObserver
    from stardust.units.unit import Unit


def _halt_distance() -> int:
    return unit_util.halt_distance(UnitTypes.Protoss_Observer) + 16


def _my_main_position() -> Position:
    my_main = game_map.get_my_main()
    assert my_main is not None
    return my_main.get_position()


def _scaled_position(current_position: Position, vector: Position, length: int) -> Position:
    scaled_vector = geo.scale_vector(vector, length)
    if scaled_vector == Positions.Invalid:
        return Positions.Invalid
    return current_position + scaled_vector


def _move_away_from(detector: MyObserver, target: Position) -> None:
    # Move in the opposite direction
    behind = _scaled_position(detector.last_position, detector.last_position - target, _halt_distance())
    if behind.isValid():
        detector.move_to(behind)
        return

    # Default to main base location when we don't have anywhere better to go
    detector.move_to(_my_main_position())


def _move_towards(detector: MyObserver, target: Position, threat_direction: Position) -> None:
    # Check for threats one-and-a-half tiles ahead
    ahead = _scaled_position(detector.last_position, target - detector.last_position, 48)
    if ahead.isValid():
        # Avoid all threats if detected, and avoid threats regardless if a Terran opponent has a comsat
        grid = players.grid(bwapi.Broodwar.enemy())
        is_threat: Callable[[Position], bool]
        if players.has_researched(bwapi.Broodwar.enemy(), TechTypes.Scanner_Sweep):
            def is_threat(pos: Position) -> bool:
                return grid.air_threat(pos) > 0
        else:
            def is_threat(pos: Position) -> bool:
                return grid.air_threat(pos) > 0 and grid.detection(pos) > 0

        if is_threat(ahead):
            # Try to find the closest position to the target that is valid and safe
            candidates: list[Position] = []
            vector = ahead - detector.last_position
            perpendicular = Position(vector.y, vector.x)

            def scale_and_add(pos: Position) -> None:
                for length in range(48, 113, 32):
                    scaled_vector = geo.scale_vector(pos - detector.last_position, length)
                    if scaled_vector != Positions.Invalid:
                        candidates.append(detector.last_position + scaled_vector)

            def process_perpendicular_position(pos: Position) -> None:
                scale_and_add((ahead + pos) / 2)
                scale_and_add((ahead + ahead + pos) / 3)
                scale_and_add((ahead + pos + pos) / 3)
                candidates.append(pos)

            process_perpendicular_position(detector.last_position + perpendicular)
            process_perpendicular_position(detector.last_position - perpendicular)

            best = Positions.Invalid
            best_dist = INT_MAX
            for pos in candidates:
                if not pos.isValid():
                    continue
                if is_threat(pos):
                    continue

                dist = pos.getApproxDistance(target)
                if dist < best_dist:
                    best = pos
                    best_dist = dist
            if best != Positions.Invalid:
                detector.move_to(best)
                return

            _move_away_from(detector, threat_direction)
            return

    # Scale to our halt distance
    scaled_vector = geo.scale_vector(target - detector.last_position, _halt_distance())
    scaled_target = target if scaled_vector == Positions.Invalid else detector.last_position + scaled_vector
    if not scaled_target.isValid():
        scaled_target = target
    if not scaled_target.isValid():
        scaled_target = _my_main_position()
    detector.move_to(scaled_target)


def execute_detectors(squad: Squad) -> None:
    import stardust.opponent as opponent
    import stardust.strategist.strategist as strategist

    pending_detectors = set(squad.detectors)

    # Start by having detectors move towards any enemies requiring detection
    enemies_being_detected: set[Unit] = set()
    for detector in squad.detectors:
        # Try to find the nearest enemy requiring detection
        closest: Unit | None = None
        closest_dist = INT_MAX
        for enemy in squad.enemies_needing_detection:
            # Skip it if it is within 3 tiles of an enemy already being handled by another detector
            if any(enemy.get_distance(enemy_being_detected) < 96 for enemy_being_detected in enemies_being_detected):
                continue

            dist = detector.get_distance(enemy)
            if dist < closest_dist:
                closest = enemy
                closest_dist = dist

        # If we found one, either move towards it if it isn't in range, or away from it if we're too close
        if closest is not None:
            opponent.set_has_built_unit_requiring_detection()

            enemies_being_detected.add(closest)

            if closest_dist > 64:
                _move_towards(detector, closest.last_position, closest.last_position)
            else:
                _move_away_from(detector, closest.last_position)

            detector.set_activity(ObserverActivity.DetectingEnemy)

            pending_detectors.discard(detector)
    if not pending_detectors:
        return

    # If we don't have a vanguard cluster, just move any remaining detectors to our main
    vanguard_cluster = squad.current_vanguard_cluster
    if vanguard_cluster is None:
        for detector in pending_detectors:
            detector.set_activity(ObserverActivity.None_)
            _move_towards(detector, _my_main_position(), squad.target_position)
        return
    vanguard = vanguard_cluster.vanguard

    def move_to_army_vanguard(detector: MyObserver) -> None:
        center_to_vanguard_vector = vanguard.last_position - vanguard_cluster.center
        _move_towards(detector, vanguard.last_position,
                      vanguard_cluster.center + center_to_vanguard_vector + center_to_vanguard_vector)

    # Assign any detectors that are a long way away from the army, or have shield damage, to hang out with the vanguard
    assigned_one_to_escort = False
    for detector in list(pending_detectors):
        if detector.last_shields < detector.type.maxShields():
            assigned_one_to_escort = True
            detector.set_activity(ObserverActivity.EscortingArmy)
            move_to_army_vanguard(detector)
            pending_detectors.discard(detector)
            continue

        dist_to_target = detector.get_distance(squad.target_position)
        if dist_to_target > squad.vanguard_cluster_dist_to_target_position + 480:
            dist_to_vanguard = detector.get_distance(vanguard)
            if dist_to_vanguard > 640:
                assigned_one_to_escort = True
                detector.set_activity(ObserverActivity.EscortingArmy)
                move_to_army_vanguard(detector)
                pending_detectors.discard(detector)
                continue
    if not pending_detectors:
        return

    # If we have previously needed to detect an enemy, keep one detector with our vanguard in case we have to again
    if not assigned_one_to_escort and opponent.has_built_unit_requiring_detection():
        # Pick the closest one, but keep an observer that is already assigned to it
        best: MyObserver | None = None
        best_dist = INT_MAX
        for detector in pending_detectors:
            if assigned_one_to_escort and detector.get_activity() != ObserverActivity.EscortingArmy:
                continue

            dist = detector.get_distance(vanguard)

            if not assigned_one_to_escort and detector.get_activity() == ObserverActivity.EscortingArmy:
                assigned_one_to_escort = True
                best = detector
                best_dist = dist
                continue

            if dist < best_dist:
                best = detector
                best_dist = dist
        if best is not None:
            best.set_activity(ObserverActivity.EscortingArmy)
            move_to_army_vanguard(best)
            pending_detectors.discard(best)
    if not pending_detectors:
        return

    # We have one or more observers that don't have any other priorities, so try to get as much scouting information
    # as possible. On open ground, we try to scout the rear of the enemy army to get full tabs on units in the fog and
    # see reinforcements coming in. When containing the enemy at their natural, we try to scout their natural and into
    # their main to see reinforcements, cliffed tanks, etc.
    if strategist.is_enemy_contained():
        # The two places we want to scout are, in order of priority, the area around the enemy's main choke and the
        # target position
        choke = game_map.get_enemy_main_choke()
        if choke is not None:
            choke_position = choke.center
            enemy_main = game_map.get_enemy_main()
            if enemy_main is not None:
                scaled = _scaled_position(choke_position, enemy_main.get_position() - choke_position, 160)
                if scaled.isValid():
                    choke_position = scaled

            # Get the detector closest to the position
            closest_detector: MyObserver | None = None
            closest_detector_dist = INT_MAX
            for detector in pending_detectors:
                dist = detector.get_distance(choke_position)
                if dist < closest_detector_dist:
                    closest_detector = detector
                    closest_detector_dist = dist
            if closest_detector is not None:
                closest_detector.set_activity(ObserverActivity.ScoutingEnemyArmy)
                _move_towards(closest_detector, choke_position, squad.target_position)
                pending_detectors.discard(closest_detector)

        threat_direction = squad.target_position
        enemy_starting_main = game_map.get_enemy_starting_main()
        enemy_starting_natural = game_map.get_enemy_starting_natural()
        if enemy_starting_main is not None and enemy_starting_natural is not None:
            threat_direction = (enemy_starting_main.get_position() + enemy_starting_natural.get_position()) / 2

        for detector in pending_detectors:
            detector.set_activity(ObserverActivity.ScoutingEnemyArmy)
            _move_towards(detector, squad.target_position, threat_direction)
    else:
        tiles_ahead = 10
        for detector in pending_detectors:
            waypoint = path_finding.next_grid_or_choke_waypoint(vanguard.last_position, squad.target_position, None,
                                                                tiles_ahead, False)
            if waypoint.isValid():
                detector.set_activity(ObserverActivity.ScoutingEnemyArmy)
                _move_towards(detector, waypoint, squad.target_position)
            else:
                detector.set_activity(ObserverActivity.EscortingArmy)
                move_to_army_vanguard(detector)

            tiles_ahead += 10

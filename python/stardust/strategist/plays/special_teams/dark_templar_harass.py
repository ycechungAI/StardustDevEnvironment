"""Port of Strategist/Plays/SpecialTeams/DarkTemplarHarass.{h,cpp}: all of our dark templar, microed individually to
harass undefended enemy bases."""

from __future__ import annotations

from collections.abc import Callable, Iterable
from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import UnitTypes, WalkPosition
from stardust import common, config
from stardust.cpp import INT_MAX, f32, fdiv, to_int
from stardust.instrumentation import cherryvis
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.players import players
from stardust.strategist.play import Play, PlayUnitRequirement, ProductionGoals, UnitCallback
from stardust.units import units

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit


def _log(unit: MyUnit, message: str) -> None:
    if config.DEBUG_UNIT_ORDERS:
        cherryvis.log(message, unit.id)


def _has_path_without_detection(start: bwapi.Position, end: bwapi.Position) -> bool:
    grid = players.grid(bwapi.Broodwar.enemy())
    choke_path, _ = path_finding.get_choke_point_path(start, end, UnitTypes.Protoss_Dark_Templar,
                                                      PathFindingOptions.UseNearestBWEMArea)
    for bwem_choke in choke_path:
        choke = game_map.choke(bwem_choke)
        assert choke is not None
        if choke.width > 300:
            continue
        if grid.detection(choke.center) > 0:
            return False
    return True


def _get_base_to_harass(unit: MyUnit) -> Base | None:
    bwem_map = bwem.Instance()
    harassable_bases: list[tuple[Base, bool]] = []
    for base in game_map.get_enemy_bases():
        if base.island:
            continue
        if base.resource_depot is None or not base.resource_depot.completed:
            continue

        has_cannon = False
        for cannon in units.all_enemy_of_type(UnitTypes.Protoss_Photon_Cannon):
            if not cannon.completed and cannon.estimated_completion_frame > (common.current_frame + 120):
                continue
            if bwem_map.GetArea(WalkPosition(cannon.last_position)) == base.get_area():
                _log(unit, f"Discounting base @ {WalkPosition(base.get_position())} because of {cannon}")
                has_cannon = True
                break
        if has_cannon:
            continue

        has_observer = False
        for observer in units.all_enemy_of_type(UnitTypes.Protoss_Observer):
            # Don't go into the enemy main if they have an observer - we will get trapped
            if base is game_map.get_enemy_starting_main():
                has_observer = True
                break

            # Don't count observers we haven't seen in a while
            if not observer.last_position_valid or observer.last_seen < (common.current_frame - 120):
                continue

            if (bwem_map.GetArea(WalkPosition(observer.last_position)) == base.get_area()
                    or observer.get_distance(base.get_position()) < 320):
                _log(unit, f"Discounting base @ {WalkPosition(base.get_position())} because of {observer}")
                has_observer = True
                break
        if has_observer:
            continue

        if not _has_path_without_detection(unit.last_position, base.get_position()):
            _log(unit, f"Discounting base @ {WalkPosition(base.get_position())} because path has detection")
            continue

        worker_count = 0
        for worker in units.all_enemy_of_type(UnitTypes.Protoss_Probe):
            if not worker.last_position_valid:
                continue

            if bwem_map.GetArea(WalkPosition(worker.last_position)) == base.get_area():
                worker_count += 1

        harassable_bases.append((base, worker_count > 2))

    best_dist = INT_MAX
    best_base: Base | None = None
    for harassable_base, has_workers in harassable_bases:
        dist = unit.get_distance(harassable_base.get_position())
        if not has_workers:
            dist *= 2  # Penalize bases without workers
        if dist < best_dist:
            best_dist = dist
            best_base = harassable_base

    return best_base


def _get_target(my_unit: MyUnit, enemy_units: Iterable[Unit], allow_retreating: bool = True,
                dist_threshold: int = INT_MAX, predicate: Callable[[Unit], bool] | None = None) -> Unit | None:
    grid = players.grid(bwapi.Broodwar.enemy())

    best_target: Unit | None = None
    best_target_dist = INT_MAX
    best_target_health = INT_MAX
    for enemy in enemy_units:
        if not enemy.last_position_valid:
            continue
        if enemy.undetected:
            continue
        if grid.detection(enemy.last_position) > 0 and grid.ground_threat(enemy.last_position) > 0:
            continue
        if not my_unit.can_attack(enemy):
            continue

        dist = my_unit.get_distance(enemy)
        if dist > dist_threshold:
            continue

        if predicate is not None and not predicate(enemy):
            continue

        if dist < best_target_dist or (dist == best_target_dist
                                       and (enemy.last_health + enemy.last_shields) < best_target_health):
            if not allow_retreating:
                predicted_enemy_position = enemy.predict_position(1)
                if my_unit.get_distance(enemy, predicted_enemy_position) > dist:
                    continue

            best_target = enemy
            best_target_dist = dist
            best_target_health = enemy.last_health + enemy.last_shields

    if best_target_dist > dist_threshold:
        return None
    return best_target


def _move_to_base(unit: MyUnit, base: Base) -> None:
    # TODO: Avoid detection / threats somehow

    navigation_grid = path_finding.get_navigation_grid(base.get_position())
    if navigation_grid is not None:
        node = navigation_grid.node(unit.get_tile_position())
        next_node = node.next_node
        second_node = next_node.next_node if next_node is not None else None

        def blocking_predicate(enemy: Unit) -> bool:
            # Consider it to be blocking if the unit is on one of the next two path nodes
            tile_position = enemy.get_tile_position()
            if ((tile_position.x == node.x and tile_position.y == node.y)
                    or (next_node is not None and tile_position.x == next_node.x and tile_position.y == next_node.y)
                    or (second_node is not None
                        and tile_position.x == second_node.x and tile_position.y == second_node.y)):
                return True

            # Consider it blocking if we are in a narrow choke, the enemy is in our attack range, and the enemy is
            # closer to the goal
            return (game_map.is_in_narrow_choke(unit.get_tile_position())
                    and unit.is_in_our_weapon_range(enemy)
                    and navigation_grid.node(enemy.get_tile_position()).cost <= node.cost)

        target = _get_target(unit, units.all_enemy(), False, 100, blocking_predicate)
        if target is not None:
            _log(unit, f"Attacking blocking unit {target}")
            unit.attack_unit(target)
            return

    unit.move_to(base.get_position())


def _execute_unit(unit: MyUnit) -> None:
    frame = common.current_frame

    detection_nearby = False
    for observer in units.all_enemy_of_type(UnitTypes.Protoss_Observer):
        # Don't count observers we haven't seen in a while
        if not observer.last_position_valid or observer.last_seen < (frame - 120):
            continue

        if unit.get_distance(observer) < 480:
            _log(unit, f"Nearby observer: {observer}")
            detection_nearby = True
            break
    if not detection_nearby:
        for cannon in units.all_enemy_of_type(UnitTypes.Protoss_Photon_Cannon):
            if not cannon.completed:
                continue
            if unit.get_distance(cannon) < (8 * 32):  # actual range is 7 tiles, giving it an extra one for safety
                _log(unit, f"Nearby observer: {cannon}")
                detection_nearby = True
                break

    def can_kill_before_completed_predicate(target: Unit) -> bool:
        if target.completed:
            return False

        # Attack a target if we think we can kill it before it completes
        damage_per_attack = players.attack_damage(unit.player, unit.type, target.player, target.type)
        move_frames = to_int(unit.get_distance(target) * 1.4 / unit.type.topSpeed())
        next_attack = max(unit.cooldown_until - frame, move_frames)

        attacks = to_int(f32(fdiv(f32(target.last_health + target.last_shields), f32(damage_per_attack))))
        frames_to_kill = next_attack + attacks * unit.type.groundWeapon().damageCooldown()

        return (frame + frames_to_kill) < target.estimated_completion_frame

    if not detection_nearby:
        cannon_target = _get_target(unit, units.all_enemy_of_type(UnitTypes.Protoss_Photon_Cannon), False, 300,
                                    can_kill_before_completed_predicate)
        if cannon_target is not None:
            _log(unit, f"Attacking cannon {cannon_target}")
            unit.attack_unit(cannon_target)
            return

        worker_target = _get_target(unit, units.all_enemy_of_type(UnitTypes.Protoss_Probe), False, 300)
        if worker_target is not None:
            _log(unit, f"Attacking worker {worker_target}")
            unit.attack_unit(worker_target)
            return

    base = _get_base_to_harass(unit)
    if base is not None:
        if (unit.get_distance(base.mineral_line_center) > 320
                or game_map.is_in_narrow_choke(unit.get_tile_position())
                or bwem.Instance().GetArea(WalkPosition(unit.last_position)) != base.get_area()):
            _log(unit, f"Moving to harass base @ {WalkPosition(base.get_position())}")
            _move_to_base(unit, base)
            return

    # Prioritize completed nexuses, observatories we can kill before they complete, robo facilities, completed things,
    # everything else
    if not detection_nearby:
        def completed_predicate(target: Unit) -> bool:
            return target.completed

        other_target = _get_target(unit, units.all_enemy_of_type(UnitTypes.Protoss_Nexus), False, 500,
                                   completed_predicate)
        if other_target is None:
            other_target = _get_target(unit, units.all_enemy_of_type(UnitTypes.Protoss_Observatory), False, 500,
                                       can_kill_before_completed_predicate)
        if other_target is None:
            other_target = _get_target(unit, units.all_enemy_of_type(UnitTypes.Protoss_Robotics_Facility), False, 500)
        if other_target is None:
            other_target = _get_target(unit, units.all_enemy(), False, INT_MAX, completed_predicate)
        if other_target is None:
            other_target = _get_target(unit, units.all_enemy(), False)
        if other_target is not None:
            _log(unit, f"Attacking target {other_target}")
            unit.attack_unit(other_target)
            return

    next_expansions = game_map.get_untaken_expansions(bwapi.Broodwar.enemy())
    if next_expansions:
        _log(unit, f"Moving to next probable enemy expansion @ {WalkPosition(next_expansions[0].get_position())}")
        _move_to_base(unit, next_expansions[0])
        return


class DarkTemplarHarass(Play):
    def __init__(self) -> None:
        super().__init__("DarkTemplarHarass")
        self.units: dict[MyUnit, None] = {}  # std::set; insertion order stands in for pointer order

    def update(self) -> None:
        # Always request DTs so all created DTs get assigned to this play
        my_main = game_map.get_my_main()
        assert my_main is not None
        self.status.unit_requirements.append(PlayUnitRequirement(10, UnitTypes.Protoss_Dark_Templar,
                                                                 my_main.get_position()))

        # Micro each unit individually
        for unit in list(self.units):
            _execute_unit(unit)

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        pass

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        for unit in list(self.units):
            movable_unit_callback(unit)

    def add_unit(self, unit: MyUnit) -> None:
        self.units[unit] = None
        super().add_unit(unit)

    def remove_unit(self, unit: MyUnit) -> None:
        self.units.pop(unit, None)
        super().remove_unit(unit)

"""Port of Strategist/Plays/Scouting/ScoutEnemyExpos.{h,cpp}: keeps an eye on the expansions the enemy might take, with
an observer when we have one and otherwise a worker."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, TilePosition, UnitTypes, WalkPosition
from stardust import common, config
from stardust.cpp import INT_MAX, fdiv
from stardust.instrumentation import cherryvis
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.players import players
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_NORMAL, Play, PlayUnitRequirement, ProductionGoals, UnitCallback, \
    add_goal
from stardust.units import units
from stardust.units.my_worker import MyWorker
from stardust.util import geo, unit_util
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit

_CVIS_BOARD_VALUES = config.INSTRUMENTATION_ENABLED


def _scout_move(scout: MyUnit, pos: Position) -> None:
    """Moves the scout. For flying units, respects its halt distance."""
    # (As in Stardust, ground scouts get the direct move and then also the scaled one below.)
    if not scout.is_flying:
        scout.move_to(pos)

    # Always move towards a position at least halt distance away
    scaled_vector = geo.scale_vector(pos - scout.last_position, unit_util.halt_distance(scout.type) + 16)

    # Move to the scaled position if it is valid, otherwise use the position directly
    scaled_pos = pos if scaled_vector == bwapi.Positions.Invalid else scout.last_position + scaled_vector
    if not scaled_pos.isValid():
        scaled_pos = pos
    scout.move_to(scaled_pos)


def _release_worker(scout: MyUnit) -> None:
    assert isinstance(scout, MyWorker)
    workers.release_worker(scout)


class ScoutEnemyExpos(Play):
    def __init__(self) -> None:
        super().__init__("ScoutEnemyExpos")
        self._scout: MyUnit | None = None
        self._target_base: Base | None = None
        self._using_search_path = True
        self._worker_scout_has_died = False

    def update(self) -> None:
        # Clear the scout if it is dead
        if self._scout is not None and not self._scout.exists():
            if self._scout.type.isWorker():
                self._worker_scout_has_died = True
            self._scout = None

        # Clear the target base if it has been scouted
        if self._target_base is not None and (self._target_base.owner is not None
                                              or (common.current_frame - self._target_base.last_scouted) < 1000):
            self._target_base = None

        # Pick a target base
        # We score them based on three factors:
        # - How soon we expect the enemy to expand to the base (i.e. order of base in vector)
        # - How long it has been since we scouted the base
        # - How close our scout is to the base
        # Islands are considered in the same way, but sorted after other bases in priority
        if self._target_base is None:
            self._using_search_path = True

            best_score = 0.0
            score_factor = 0
            my_main = game_map.get_my_main()
            assert my_main is not None
            my_main_position = my_main.get_position()

            def handle_base(base: Base) -> None:
                nonlocal best_score, score_factor
                frames_since_scouted = common.current_frame - base.last_scouted
                score_factor += 1

                score = frames_since_scouted / score_factor

                scout = self._scout
                if scout is not None and scout.is_flying:
                    dist = scout.get_distance(base.get_position())
                elif scout is not None:
                    dist = path_finding.get_ground_distance(scout.last_position, base.get_position(), scout.type,
                                                            PathFindingOptions.UseNeighbouringBWEMArea)
                else:
                    dist = path_finding.get_ground_distance(my_main_position, base.get_position(),
                                                            UnitTypes.Protoss_Probe,
                                                            PathFindingOptions.UseNeighbouringBWEMArea)

                # This effectively skips island expansions until we have an observer to scout with
                if dist == -1:
                    return

                score = fdiv(score, dist / 2000.0)

                if score > best_score:
                    best_score = score
                    self._target_base = base

            enemy = bwapi.Broodwar.enemy()
            for base in game_map.get_untaken_expansions(enemy):
                handle_base(base)
            for base in game_map.get_untaken_island_expansions(enemy):
                handle_base(base)

        target_base = self._target_base
        if target_base is None:
            if self._scout is not None:
                if self._scout.type.isWorker():
                    _release_worker(self._scout)
                    self._scout = None
                else:
                    self.status.removed_units.append(self._scout)

            if _CVIS_BOARD_VALUES:
                cherryvis.set_board_value("ScoutEnemyExpos_target", "(none)")

            return

        if _CVIS_BOARD_VALUES:
            cherryvis.set_board_value("ScoutEnemyExpos_target", str(target_base.get_tile_position()))

        # TODO: Support scouting with a worker or other unit

        # We prefer to scout with an observer, so if we don't currently have one, request one
        # This play is lower-priority than combat plays, so it might get taken by another play if the enemy has cloaked
        # units
        if self._scout is None or self._scout.type != UnitTypes.Protoss_Observer:
            self.status.unit_requirements.append(PlayUnitRequirement(1, UnitTypes.Protoss_Observer,
                                                                     target_base.get_position()))

        # Request a worker scout if we have no scout and we haven't had a worker scout die on us before
        if self._scout is None and not self._worker_scout_has_died:
            worker, _ = workers.get_closest_reassignable_worker(target_base.get_position(), False)
            if worker is not None:
                workers.reserve_worker(worker)
                self._scout = worker

        # If we don't have a scout, clear the target base for next time
        scout = self._scout
        if scout is None:
            self._target_base = None
            return

        # Worker scouts just move to the target
        # TODO: Threat-aware pathing
        if not scout.is_flying:
            scout.move_to(target_base.get_position())
            return

        # This is a quick hack to prevent us from searching for non-existant paths every frame and timing out
        if not self._using_search_path:
            _scout_move(scout, target_base.get_position())

            if config.DEBUG_UNIT_ORDERS:
                cherryvis.log(f"Scout: target directly to scout tile {WalkPosition(target_base.get_position())}",
                              scout.id)
            return

        # Search for a path that avoids enemy threats
        tile = TilePosition(target_base.get_position())
        grid = players.grid(bwapi.Broodwar.enemy())

        def avoid_threat_tiles(t: TilePosition) -> bool:
            return grid.air_threat_at((t.x << 2) + 2, (t.y << 2) + 2) == 0

        # If the current position of the observer is a threat, move away from whatever is threatening it
        if not avoid_threat_tiles(scout.get_tile_position()):
            nearest_threat: Unit | None = None
            nearest_threat_dist = INT_MAX
            for unit in units.all_enemy():
                if not unit.last_position_valid:
                    continue
                if unit.type.isDetector() or unit.can_attack_air():
                    dist = scout.get_distance(unit)
                    if dist < nearest_threat_dist:
                        nearest_threat = unit
                        nearest_threat_dist = dist

            if nearest_threat is not None:
                scaled_vector = geo.scale_vector(scout.last_position - nearest_threat.last_position,
                                                 unit_util.halt_distance(scout.type) + 16)
                if scaled_vector.isValid():
                    scaled_pos = scout.last_position + scaled_vector
                    if scaled_pos.isValid():
                        scout.move_to(scaled_pos)

                        if config.DEBUG_UNIT_ORDERS:
                            cherryvis.log(f"Scout: move to {WalkPosition(scaled_pos)} to avoid {nearest_threat}",
                                          scout.id)
                        return

            my_main = game_map.get_my_main()
            assert my_main is not None
            scout.move_to(my_main.get_position())

            if config.DEBUG_UNIT_ORDERS:
                cherryvis.log("Scout: move to main to avoid unknown threat", scout.id)
            return

        path = path_finding.search(scout.get_tile_position(), tile, avoid_threat_tiles)

        if len(path) < 3:
            self._using_search_path = False
            _scout_move(scout, target_base.get_position())

            if config.DEBUG_UNIT_ORDERS:
                cherryvis.log(f"Scout: target directly to scout tile {WalkPosition(target_base.get_position())}",
                              scout.id)
        else:
            waypoint = Position(path[2]) + Position(16, 16)
            _scout_move(scout, waypoint)

            if config.DEBUG_UNIT_ORDERS:
                cherryvis.log(f"Scout: target next path waypoint {WalkPosition(waypoint)}", scout.id)

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        # Produce an observer if we have a unit requirement for it and have the prerequisites
        for unit_requirement in self.status.unit_requirements:
            if unit_requirement.type != UnitTypes.Protoss_Observer:
                continue
            if unit_requirement.count < 1:
                continue

            if (units.count_completed(UnitTypes.Protoss_Observatory) == 0
                    or units.count_completed(UnitTypes.Protoss_Robotics_Facility) == 0):
                continue

            add_goal(prioritized_production_goals, PRIORITY_NORMAL,
                     UnitProductionGoal(self.label, UnitTypes.Protoss_Observer, 1, 1))

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        if self._scout is not None:
            if self._scout.type.isWorker():
                _release_worker(self._scout)
            else:
                movable_unit_callback(self._scout)

    def add_unit(self, unit: MyUnit) -> None:
        if self._scout is not None and self._scout.type.isWorker():
            _release_worker(self._scout)

        self._scout = unit

    def remove_unit(self, unit: MyUnit) -> None:
        if self._scout is unit:
            self._scout = None

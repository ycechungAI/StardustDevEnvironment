"""Port of Strategist/Plays/Defensive/AntiCannonRush.{h,cpp}.

This play is added by the strategy engine when the enemy could be doing a cannon rush - either we have no scouting
information (scout hasn't arrived or enemy blocked it), have scouted an early forge, or have scouted a probable proxy.

There are various states the play can be in:
- Scouting our main
  - When we haven't observed any activity in our base yet
- Following enemy worker
  - When we have seen an enemy worker, but nothing dangerous yet
- Attacking enemy worker
  - When we have seen an enemy worker build something
- Attacking building
  - When there is a building we need to kill quickly, e.g. a warping cannon

This play handles attacking with workers where needed and ordering zealots when required, but the actual combat micro
of the zealots is handled by the main base defense play.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Position, TilePosition, TilePositions, UnitTypes, WalkPosition
from stardust import common, config, opponent
from stardust.cpp import INT_MAX
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_EMERGENCY, Play, ProductionGoals, UnitCallback, add_goal
from stardust.units import units
from stardust.util import unit_util
from stardust.workers import workers as worker_manager

if TYPE_CHECKING:
    from stardust.units.my_unit import MyUnit
    from stardust.units.my_worker import MyWorker
    from stardust.units.unit import Unit


def _clear_dead(worker_set: dict[MyWorker, None]) -> None:
    for worker in list(worker_set):
        if not worker.exists():
            del worker_set[worker]


def _release_all(worker_set: dict[MyWorker, None]) -> None:
    for worker in worker_set:
        cherryvis.log("Releasing from non-mining duties (AntiCannonRush no longer needs it)", worker.id)
        worker_manager.release_worker(worker)

    worker_set.clear()


class AntiCannonRush(Play):
    def __init__(self) -> None:
        super().__init__("AntiCannonRush")
        # Whether the enemy's current strategy is considered "safe" from a cannon rush perspective
        self.safe_enemy_strategy_determined = False

        self._scout: MyWorker | None = None
        self._tiles_to_scout: list[TilePosition] = []

        self._built_pylon = False
        self._built_cannon = False

        # std::set / std::map keyed by pointer; insertion order stands in for pointer order
        self._worker_attackers: dict[MyWorker, None] = {}
        self._cannons_and_attackers: dict[Unit, dict[MyWorker, None]] = {}

        # Compute the scout tiles
        game = bwapi.Broodwar
        bwem_map = bwem.Instance()
        areas = game_map.get_my_main_areas()
        my_main = game_map.get_my_main()
        assert my_main is not None
        reference_height = game.getGroundHeight(my_main.get_tile_position())
        for y in range(game.mapHeight() - 1):
            for x in range(game.mapWidth() - 1):
                if (not game_map.is_walkable(x, y)
                        or not game_map.is_walkable(x + 1, y)
                        or not game_map.is_walkable(x, y + 1)
                        or not game_map.is_walkable(x + 1, y + 1)):
                    continue

                here = TilePosition(x, y)
                if game.getGroundHeight(here) != reference_height:
                    continue
                if bwem_map.GetArea(here) not in areas:
                    continue

                self._tiles_to_scout.append(here)

    def update(self) -> None:
        frame = common.current_frame

        # Clear scout if it has died
        if self._scout is not None and not self._scout.exists():
            self._scout = None

        # Clear worker attackers that have died
        _clear_dead(self._worker_attackers)

        # Clear enemy cannons if they are dead
        for cannon, attackers in list(self._cannons_and_attackers.items()):
            # Clear cannon attackers that have died
            _clear_dead(attackers)

            if not cannon.exists():
                _release_all(attackers)
                del self._cannons_and_attackers[cannon]

        # Collect all enemy units in our main areas
        areas = game_map.get_my_main_areas()
        bwem_map = bwem.Instance()
        enemy_workers: list[Unit] = []
        pylons: list[Unit] = []
        for unit in units.all_enemy():
            if not unit.last_position_valid:
                continue
            if (not unit.type.isWorker()
                    and unit.type != UnitTypes.Protoss_Pylon
                    and unit.type != UnitTypes.Protoss_Photon_Cannon):
                continue

            if bwem_map.GetArea(WalkPosition(unit.last_position)) not in areas:
                continue

            if unit.type.isWorker():
                enemy_workers.append(unit)
            elif unit.type == UnitTypes.Protoss_Pylon:
                pylons.append(unit)

                if not self._built_pylon:
                    log.get("Found enemy pylon in our base")
                    self._built_pylon = True

                    start_frame = ((frame if unit.completed else unit.estimated_completion_frame)
                                   - unit_util.build_time(unit.type))
                    opponent.set_game_value("pylonInOurMain", start_frame)
            elif unit.type == UnitTypes.Protoss_Photon_Cannon:
                if unit not in self._cannons_and_attackers:
                    self._cannons_and_attackers[unit] = {}
                    self._built_cannon = True
                    log.get("Found enemy cannon in our base")

                    # Built cannon implies built pylon, even if we haven't seen it
                    if not self._built_pylon:
                        self._built_pylon = True
                        start_frame = ((frame if unit.completed else unit.estimated_completion_frame)
                                       - unit_util.build_time(unit.type)
                                       - unit_util.build_time(UnitTypes.Protoss_Pylon))
                        opponent.set_game_value("pylonInOurMain", start_frame)

        # Disband when we are fairly certain the cannon rush is not happening or is over
        if (((frame >= 4000 or self.safe_enemy_strategy_determined) and not self._built_pylon)
                or (frame >= 7000 and not pylons and not self._cannons_and_attackers)):
            self.status.complete = True
            return

        # Use our previous game results to determine when the play should go "active", i.e. reserve a worker
        # We start our scouting 500 frames before we've previously seen a pylon start
        # An exception is if we have never lost to this opponent, in which case we play it safe
        if opponent.win_loss_ratio(0.0, 200) < 0.99:
            worst_case_pylon_frame = opponent.min_value_in_previous_games("pylonInOurMain", INT_MAX, 15)
            if not self._built_pylon and worst_case_pylon_frame > (frame + 500):
                return

        # Gather units that should attack
        units_and_targets: list[tuple[MyUnit, Unit | None]] = []

        # Attack the enemy worker if needed
        if len(enemy_workers) == 1:
            enemy_worker = enemy_workers[0]

            # Take the scout if available
            if self._scout is not None:
                self._worker_attackers[self._scout] = None
                self._scout = None

            while len(self._worker_attackers) < (2 if (pylons or self._cannons_and_attackers) else 1):
                attacker, _ = worker_manager.get_closest_reassignable_worker(enemy_worker.last_position, True)
                if attacker is None:
                    break

                worker_manager.reserve_worker(attacker)
                self._worker_attackers[attacker] = None

            for worker_attacker in self._worker_attackers:
                units_and_targets.append((worker_attacker, enemy_worker))
        else:
            _release_all(self._worker_attackers)

        # Reserve attackers for enemy cannons
        completion_cutoff = frame + unit_util.build_time(UnitTypes.Protoss_Photon_Cannon) - 100
        for cannon, attackers in self._cannons_and_attackers.items():
            # Reserve attackers if we have just seen it start to build
            if len(attackers) < 4 and cannon.estimated_completion_frame > completion_cutoff:
                while len(attackers) < 4:
                    attacker, _ = worker_manager.get_closest_reassignable_worker(cannon.last_position, True)
                    if attacker is None:
                        break

                    worker_manager.reserve_worker(attacker)
                    attackers[attacker] = None

            for cannon_attacker in attackers:
                units_and_targets.append((cannon_attacker, cannon))

        # Issue attack orders
        if units_and_targets:
            for my_unit, target in units_and_targets:
                assert target is not None
                if config.DEBUG_UNIT_ORDERS:
                    cherryvis.log(f"Attacking {target}", my_unit.id)
                my_unit.attack_unit(target, units_and_targets)
            return

        # Get the next tile to scout
        tile = self._get_next_scout_tile()

        # If there is none, we are done for now
        if tile == TilePositions.Invalid:
            if self._scout is not None:
                cherryvis.log("Releasing from non-mining duties (AntiCannonRush no tiles to scout)", self._scout.id)
                worker_manager.release_worker(self._scout)
            return

        # Ensure we have a worker scout
        if self._scout is None:
            self._scout, _ = worker_manager.get_closest_reassignable_worker(Position(tile) + Position(16, 16), False)
            if self._scout is None:
                return

            worker_manager.reserve_worker(self._scout)

        if config.DEBUG_UNIT_ORDERS:
            cherryvis.log(f"Move to next AntiCannonRush tile @ {WalkPosition(tile) + WalkPosition(2, 2)}",
                          self._scout.id)
        self._scout.move_to(Position(tile) + Position(16, 16))

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        # If the enemy has cannons, build zealots at emergency priority
        if self._cannons_and_attackers:
            add_goal(prioritized_production_goals, PRIORITY_EMERGENCY,
                     UnitProductionGoal(self.label, UnitTypes.Protoss_Zealot, -1, 2))

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        if self._scout is not None and self._scout.exists():
            cherryvis.log("Releasing from non-mining duties (AntiCannonRush disband)", self._scout.id)
            worker_manager.release_worker(self._scout)

        _release_all(self._worker_attackers)

        for attackers in self._cannons_and_attackers.values():
            _release_all(attackers)

    def _get_next_scout_tile(self) -> TilePosition:
        my_main = game_map.get_my_main()
        assert my_main is not None
        main_center = my_main.get_position()

        best_frame = INT_MAX
        best_dist = INT_MAX
        best_tile = TilePositions.Invalid
        for tile in self._tiles_to_scout:
            last_seen = game_map.last_seen_tile(tile)
            if last_seen > best_frame:
                continue

            tile_center = Position(tile) + Position(16, 16)
            dist = main_center.getApproxDistance(tile_center)
            if self._scout is not None:
                dist += self._scout.last_position.getApproxDistance(tile_center)
            if last_seen < best_frame or dist < best_dist:
                best_tile = tile
                best_frame = last_seen
                best_dist = dist

        return best_tile

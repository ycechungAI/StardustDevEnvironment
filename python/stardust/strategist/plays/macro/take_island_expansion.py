"""Port of Strategist/Plays/Macro/TakeIslandExpansion.{h,cpp}: takes an island expansion (or an expansion we can only
reach by air), shuttling a builder and then some workers to it."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, TilePosition, UnitCommandTypes, UnitTypes
from stardust import common
from stardust.builder import builder
from stardust.cpp import INT_MAX
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.players import players
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_NORMAL, PlayUnitRequirement, ProductionGoals, UnitCallback, add_goal
from stardust.strategist.plays.macro.take_expansion import TakeExpansion
from stardust.units import units
from stardust.util import geo
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.units.my_unit import MyUnit
    from stardust.units.my_worker import MyWorker


def _bw(unit: MyUnit) -> bwapi.Unit:
    bwapi_unit = unit.bwapi_unit
    assert bwapi_unit is not None
    return bwapi_unit


def _unload_near(shuttle: MyUnit, target: Position) -> None:
    # Ensure the target is walkable
    tile = TilePosition(target)
    spiral = geo.Spiral()
    while not game_map.is_walkable(tile.x, tile.y):
        spiral.next()
        tile = TilePosition(target) + TilePosition(spiral.x, spiral.y)
        if not tile.isValid():
            tile = TilePosition(target)
            break

    pos = Position(tile) + Position(16, 16)
    if shuttle.get_distance(pos) < 128:
        if _bw(shuttle).getLastCommand().getType() == UnitCommandTypes.Unload_All_Position:
            return
        shuttle.unload_all(pos)
    else:
        shuttle.move_to(pos)


class TakeIslandExpansion(TakeExpansion):
    def __init__(self, base: Base, can_cancel: bool = True, transfer_workers: bool = True) -> None:
        super().__init__(base, 0)
        self._can_cancel = can_cancel
        self._shuttle: MyUnit | None = None
        self._worker_transfer_state = 0 if transfer_workers else 2  # 0 = picking up, 1 = unloading, 2 = done
        self._worker_transfer: list[MyWorker] = []

    def update(self) -> None:
        base = self.base
        nexus = units.my_building_at(self.depot_position)
        my_main = game_map.get_my_main()
        assert my_main is not None

        # Clean up dead or unneeded workers
        if self.builder is not None and not self.builder.exists():
            self.builder = None
        remaining: list[MyWorker] = []
        for worker in self._worker_transfer:
            if worker.exists() and (_bw(worker).isLoaded() or worker.get_distance(base.get_position()) > 300):
                remaining.append(worker)
            elif worker.exists():
                # The worker has been unloaded near the island base, so unreserve it
                workers.release_worker(worker)
        self._worker_transfer = remaining

        # If the shuttle dies, we usually want to cancel the play
        if self._shuttle is not None and not self._shuttle.exists():
            self._shuttle = None

            if nexus is None and (self.builder is None or self.builder.get_distance(base.get_position()) > 300):
                self.status.complete = True
                return

        # Handle the case where we've discovered the expansion is already taken by the opponent
        if base.owner == bwapi.Broodwar.enemy():
            # If the builder is loaded in the shuttle, tell the shuttle to unload it near our main
            if self._shuttle is not None and self.builder is not None and _bw(self.builder).isLoaded():
                _unload_near(self._shuttle, my_main.mineral_line_center)
                return

            # Otherwise treat the play as complete
            self.status.complete = True
            return

        if nexus is None:
            shuttle = self._shuttle
            if shuttle is None:
                self.status.unit_requirements.append(
                    PlayUnitRequirement(1, UnitTypes.Protoss_Shuttle, my_main.get_position()))
                return

            if self.builder is None:
                self.builder, _ = workers.get_closest_reassignable_worker(shuttle.last_position, False)
                if self.builder is None:
                    return

                workers.reserve_worker(self.builder)
                self.builder.move_to(shuttle.last_position)
                shuttle.load(_bw(self.builder))
            build_worker = self.builder
            build_worker_unit = _bw(build_worker)

            if build_worker_unit.isLoaded():
                _unload_near(shuttle, base.get_position())
                return

            # Jump out now if the shuttle hasn't loaded the builder yet
            if build_worker.get_distance(base.get_position()) > 128:
                return

            # The shuttle has transferred the builder and doesn't need to transfer workers, release it
            if self._worker_transfer_state == 2:
                self.status.removed_units.append(shuttle)

            # Ensure the builder clears a blocking neutral
            for blocking_neutral in base.blocking_neutrals:
                if not blocking_neutral.exists():
                    continue

                last_command = build_worker_unit.getLastCommand()
                if blocking_neutral.getType().isMineralField():
                    # No need to do anything if the builder is gathering
                    if (last_command.getType() == UnitCommandTypes.Gather
                            and last_command.getTarget() == blocking_neutral):
                        return

                    build_worker.gather(blocking_neutral)
                else:
                    # No need to do anything if the builder is attacking
                    if (last_command.getType() == UnitCommandTypes.Attack_Unit
                            and last_command.getTarget() == blocking_neutral):
                        return

                    build_worker.attack(blocking_neutral)

                return

            builder.build(UnitTypes.Protoss_Nexus, self.depot_position, build_worker)
        else:
            if self.builder is not None:
                workers.release_worker(self.builder)
                self.builder = None

            if nexus.completed and self._shuttle is None:
                self.status.complete = True

        # Have the shuttle transfer some probes from a nearby base
        shuttle = self._shuttle
        if shuttle is None:
            return

        if self._worker_transfer_state == 0:
            # Find the base to transfer from
            best_base: Base | None = None
            best_score = INT_MAX
            for my_base in game_map.get_my_bases():
                if my_base.island:
                    continue

                available_workers = workers.base_mineral_worker_count(my_base)
                if available_workers < 8:
                    continue

                score = shuttle.get_distance(my_base.get_position()) - available_workers * 200
                if score < best_score:
                    best_score = score
                    best_base = my_base

            if best_base is None:
                return

            # First ensure the shuttle has arrived at the base
            if not self._worker_transfer and shuttle.get_distance(best_base.mineral_line_center) > 320:
                shuttle.move_to(best_base.mineral_line_center)
                return

            # Now wait until the nexus is sufficiently complete
            # We try to approximately handle transit time + 300 frames to load & unload the workers
            if nexus is None:
                return
            if not nexus.completed and not self._worker_transfer:
                frames = 600 + path_finding.expected_travel_time(base.get_position(), best_base.get_position(),
                                                                 UnitTypes.Protoss_Shuttle)
                if nexus.estimated_completion_frame - common.current_frame > frames:
                    return

            # Then reserve the appropriate number of workers
            desired_workers = min(8, (len(self._worker_transfer) + workers.base_mineral_worker_count(best_base)) // 2)
            while desired_workers > len(self._worker_transfer):
                transfer_worker, _ = workers.get_closest_reassignable_worker(shuttle.last_position, False)
                if transfer_worker is None or shuttle.get_distance(transfer_worker) > 500:
                    break

                workers.reserve_worker(transfer_worker)
                self._worker_transfer.append(transfer_worker)

            # Then load all of the workers
            all_loaded = True
            for worker in self._worker_transfer:
                if not _bw(worker).isLoaded():
                    worker.move_to(shuttle.last_position)

                    # Load the first not-loaded worker
                    if all_loaded:
                        shuttle.load(_bw(worker))

                    all_loaded = False

            # State transition when all desired workers are loaded
            if all_loaded and len(self._worker_transfer) == desired_workers:
                self._worker_transfer_state = 1
        elif self._worker_transfer_state == 1:
            _unload_near(shuttle, base.mineral_line_center)

            # State transition when all workers are unloaded
            if not any(_bw(worker).isLoaded() for worker in self._worker_transfer):
                self.status.removed_units.append(shuttle)
                self._worker_transfer_state = 2

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        # TODO: Avoid stranding the worker on the island
        if self.builder is not None:
            workers.release_worker(self.builder)
        if self._shuttle is not None:
            removed_unit_callback(self._shuttle)

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        for unit_requirement in self.status.unit_requirements:
            if unit_requirement.count < 1:
                continue

            # We only build a shuttle if we don't already have one
            # If we have one, we'll wait until it is available for island expo duties
            if unit_requirement.type != UnitTypes.Protoss_Shuttle:
                continue
            if units.count_all(UnitTypes.Protoss_Shuttle) > 0:
                continue

            add_goal(prioritized_production_goals, PRIORITY_NORMAL,
                     UnitProductionGoal(self.label, unit_requirement.type, unit_requirement.count, 1))

    def add_unit(self, unit: MyUnit) -> None:
        if unit.type == UnitTypes.Protoss_Shuttle:
            self._shuttle = unit

    def remove_unit(self, unit: MyUnit) -> None:
        if self._shuttle is unit:
            self._shuttle = None

    def cancellable(self) -> bool:
        if not self._can_cancel:
            return False
        if self._shuttle is None:
            return True
        if self.builder is not None:
            return False
        return not self._worker_transfer

    def frames_to_clear_blocker(self) -> int:
        for blocking_neutral in self.base.blocking_neutrals:
            if not blocking_neutral.exists():
                continue

            if blocking_neutral.getType().isMineralField():
                return 0

            hp = blocking_neutral.getInitialHitPoints()
            if blocking_neutral.isVisible():
                hp = blocking_neutral.getHitPoints()

            return (hp * UnitTypes.Protoss_Probe.groundWeapon().damageCooldown()) // players.attack_damage(
                bwapi.Broodwar.self(), UnitTypes.Protoss_Probe, blocking_neutral.getPlayer(),
                blocking_neutral.getType())

        return 0

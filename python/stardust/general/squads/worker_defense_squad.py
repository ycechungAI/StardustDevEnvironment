"""Port of General/Squads/WorkerDefenseSquad.{h,cpp}: uses a base's workers to fight off attackers (not a Squad)."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Races
from stardust import common
from stardust.builder import building_placement
from stardust.cpp import INT_MAX
from stardust.instrumentation import cherryvis
from stardust.units import units
from stardust.util import geo
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitsAndTargets
    from stardust.map.base import Base
    from stardust.units.my_worker import MyWorker
    from stardust.units.unit import Unit


class WorkerDefenseSquad:
    def __init__(self, base: Base) -> None:
        self._base = base
        self._units: list[MyWorker] = []  # All of the reserved workers

    def select_targets(self, enemy_units: set[Unit]) -> list[tuple[MyWorker, Unit]]:
        """Selects a target for each worker in the base."""
        result: list[tuple[MyWorker, Unit]] = []

        has_cannon_in_mineral_line = False
        defense_locations = building_placement.base_static_defense_locations(self._base)
        if defense_locations.is_valid():
            cannon = units.my_building_at(defense_locations.worker_defense_cannons[0])
            if (cannon is not None and cannon.completed and cannon.bwapi_unit is not None
                    and cannon.bwapi_unit.isPowered()):
                has_cannon_in_mineral_line = True

        def select_target(worker: MyWorker) -> Unit | None:
            closest_enemy: Unit | None = None
            closest_enemy_dist = INT_MAX
            for enemy in enemy_units:
                if not worker.can_attack(enemy):
                    continue
                if not worker.is_in_enemy_weapon_range(enemy, buffer=0 if has_cannon_in_mineral_line else 48):
                    continue

                dist = worker.get_distance(enemy)
                if dist < closest_enemy_dist:
                    closest_enemy = enemy
                    closest_enemy_dist = dist

            return closest_enemy

        # For the list of units we've already reserved, remove the dead ones and release units that no longer have a
        # target
        remaining: list[MyWorker] = []
        for worker in self._units:
            # Dead units
            if not worker.exists():
                continue

            target = select_target(worker)

            # Units not being threatened
            if target is None:
                cherryvis.log("Releasing from non-mining duties (not threatened)", worker.id)
                workers.release_worker(worker)
                continue

            result.append((worker, target))
            remaining.append(worker)
        self._units = remaining

        # For units not yet reserved, reserve those that are being threatened
        for worker in workers.get_base_workers(self._base):
            target = select_target(worker)
            if target is not None:
                workers.reserve_worker(worker)
                self._units.append(worker)
                result.append((worker, target))

        return result

    def execute(self, workers_and_targets: list[tuple[MyWorker, Unit]],
                combat_units_and_targets: UnitsAndTargets) -> None:
        game = bwapi.Broodwar
        health_limit = 10
        if game.enemy().getRace() == Races.Terran:
            health_limit = 12
        elif game.enemy().getRace() == Races.Protoss:
            health_limit = 16

        # Execute the micro for each worker with a target
        # Rules are:
        # - Get a mineral patch we can use for fleeing or kiting
        # - If there is no suitable patch, attack
        # - If we are on cooldown, mine from the patch (currently disabled)
        # - If we are low on health, mine from the patch
        # - If a friendly combat unit is moving to attack the target but is not yet in position, mine from the patch
        # - Otherwise attack
        for worker, target in workers_and_targets:
            if not worker.exists():
                continue

            # Select a mineral patch to use
            # We pick the patch that is furthest away, but closer to us than the enemy
            furthest_patch: bwapi.Unit | None = None
            furthest_patch_dist = 0
            for mineral_patch in self._base.mineral_patches():
                patch = mineral_patch.get_bwapi_unit_if_visible()
                if patch is None:
                    continue

                dist = geo.edge_to_edge_distance(worker.type, worker.last_position, patch.getType(),
                                                 patch.getPosition())
                if dist < 10:
                    continue
                if dist < furthest_patch_dist:
                    continue

                target_dist_to_patch = geo.edge_to_edge_distance(target.type, target.sim_position, patch.getType(),
                                                                 patch.getPosition())
                if target_dist_to_patch < dist:
                    continue

                furthest_patch = patch
                furthest_patch_dist = dist
            if furthest_patch is None:
                worker.attack(target.bwapi_unit, True)
                continue

            # Flee if we just attacked
            if worker.last_seen_attacking >= common.current_frame - 1:
                worker.gather(furthest_patch)
                continue

            # Flee if we are low on health
            if (worker.health + worker.shields) <= health_limit:
                worker.gather(furthest_patch)
                continue

            # Check if there is a combat unit attacking our target
            # If there is, and it isn't close to being in range yet, flee to give it time to get here
            # Don't do this if we are in the target's attack range
            if not worker.is_in_enemy_weapon_range(target):
                combat_unit_approaching = False
                for combat_unit, combat_unit_target in combat_units_and_targets:
                    if combat_unit_target is not target:
                        continue

                    if combat_unit.is_in_our_weapon_range(combat_unit_target, buffer=16):
                        # There is a unit in range, so clear and break
                        combat_unit_approaching = False
                        break

                    combat_unit_approaching = True
                if combat_unit_approaching:
                    worker.gather(furthest_patch)
                    continue

            # When on cooldown, kite if we are too close to our target
            if ((worker.cooldown_until - common.current_frame) > (game.getRemainingLatencyFrames() + 6)
                    and worker.get_distance(target) < worker.ground_range() - 2):
                worker.gather(furthest_patch)
                continue

            worker.attack(target.bwapi_unit, True)

    def disband(self) -> None:
        for unit in self._units:
            cherryvis.log("Releasing from non-mining duties (disband worker defense)", unit.id)
            workers.release_worker(unit)

        self._units.clear()

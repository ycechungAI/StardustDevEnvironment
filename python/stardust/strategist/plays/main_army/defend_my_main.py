"""Port of Strategist/Plays/MainArmy/DefendMyMain.{h,cpp}: our main army defends our main base in the early game,
pulling workers against worker rushes and handling gas steals."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Races, UnitType, UnitTypes, WalkPosition
from stardust import common
from stardust.cpp import INT_MAX
from stardust.general import general
from stardust.general.squads.early_game_defend_main_base_squad import EarlyGameDefendMainBaseSquad
from stardust.general.unit_cluster.unit_cluster import Activity
from stardust.instrumentation import cherryvis
from stardust.map import game_map
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_BASEDEFENSE, PRIORITY_EMERGENCY, Play, ProductionGoals, UnitCallback, \
    add_goal
from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay
from stardust.units import units
from stardust.util import unit_util
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.units.my_unit import MyUnit
    from stardust.units.my_worker import MyWorker
    from stardust.units.unit import Unit

_REGROUP_EMERGENCY_TIMEOUT = 30 * 24


class DefendMyMain(MainArmyPlay):
    def __init__(self) -> None:
        super().__init__("DefendMyMain")
        self._emergency_production = UnitTypes.None_
        self._squad = EarlyGameDefendMainBaseSquad()
        self._last_regroup_frame = 0
        self._reserved_gas_steal_attacker: MyUnit | None = None
        self._reserved_worker_gas_steal_attackers: list[MyWorker] = []
        self._pulled_workers: list[MyWorker] = []
        general.add_squad(self._squad)

    def is_defensive(self) -> bool:
        return True

    def get_squad(self) -> EarlyGameDefendMainBaseSquad:
        return self._squad

    def update(self) -> None:
        super().update()

        squad = self._squad
        frame = common.current_frame

        if self._reserved_gas_steal_attacker is not None and not self._reserved_gas_steal_attacker.exists():
            self._reserved_gas_steal_attacker = None

        self._reserved_worker_gas_steal_attackers = [worker for worker in self._reserved_worker_gas_steal_attackers
                                                     if worker.exists()]
        self._pulled_workers = [worker for worker in self._pulled_workers if worker.exists()]

        # Get enemy combat units in our base
        enemy_combat_units: set[Unit] = set()
        enemy_workers: set[Unit] = set()
        scout_harass = True
        require_dragoons = False
        gas_steal: Unit | None = None
        main_areas = game_map.get_my_main_areas()
        bwem_map = bwem.Instance()
        for unit in units.all_enemy():
            if not unit.last_position_valid:
                continue

            if bwem_map.GetArea(WalkPosition(unit.last_position)) not in main_areas:
                continue

            if unit.type.isWorker():
                enemy_workers.add(unit)
                if unit.last_seen_attacking > frame - 48:
                    enemy_combat_units.add(unit)
            elif unit_util.is_combat_unit(unit.type) and unit.type.canAttack():
                scout_harass = False
                enemy_combat_units.add(unit)
                require_dragoons = require_dragoons or (unit.is_flying
                                                        or unit.type == UnitTypes.Protoss_Dragoon
                                                        or unit.type == UnitTypes.Terran_Vulture)
            elif unit.type.isRefinery():
                gas_steal = unit

        if gas_steal is not None:
            # Reserve a gas steal attacker if we have three units in the squad
            unit_count = (1 if self._reserved_gas_steal_attacker is not None else 0) + len(squad.get_units())
            if unit_count > 2 and self._reserved_gas_steal_attacker is None:
                min_dist = INT_MAX
                for unit in squad.get_units():
                    dist = unit.get_distance(gas_steal)
                    if dist < min_dist:
                        min_dist = dist
                        self._reserved_gas_steal_attacker = unit

                if self._reserved_gas_steal_attacker is not None:
                    squad.remove_unit(self._reserved_gas_steal_attacker)
            elif unit_count < 3 and self._reserved_gas_steal_attacker is not None:
                # Release the reserved gas steal attacker if we need it for defense
                squad.add_unit(self._reserved_gas_steal_attacker)
                self._reserved_gas_steal_attacker = None

            # Pull workers to attack the gas steal
            # (Stardust's loop bound shrinks as workers are added, so it adds about half of the shortfall.)
            desired_workers = 4 if units.count_all(UnitTypes.Protoss_Zealot) > 0 else 2
            attackers = self._reserved_worker_gas_steal_attackers
            if len(attackers) < desired_workers:
                i = 0
                while i < desired_workers - len(attackers):
                    my_main = game_map.get_my_main()
                    assert my_main is not None
                    worker, _ = workers.get_closest_reassignable_worker(my_main.get_position(), False)
                    if worker is None:
                        break

                    workers.reserve_worker(worker)
                    attackers.append(worker)
                    i += 1
            elif len(attackers) > desired_workers:
                workers.release_worker(attackers.pop(0))

            # Execute attack with our reserved units
            if self._reserved_gas_steal_attacker is not None:
                self._reserved_gas_steal_attacker.attack_unit(gas_steal)
            for worker in attackers:
                worker.attack_unit(gas_steal)
        else:
            # Release the reserved gas steal attacker when it is no longer needed
            if self._reserved_gas_steal_attacker is not None:
                squad.add_unit(self._reserved_gas_steal_attacker)
                self._reserved_gas_steal_attacker = None

            # Release the worker gas steal attackers when they are no longer needed
            for worker_gas_steal_attacker in self._reserved_worker_gas_steal_attackers:
                workers.release_worker(worker_gas_steal_attacker)
            self._reserved_worker_gas_steal_attackers.clear()

        # If there are more than two workers, consider this to be a worker rush and add them to the set of combat
        # units. If the worker rush is hitting before we have started any combat units, pull workers. Once we have
        # pulled workers, keep some pulled until the enemy no longer has any workers left.
        desired_pulled_workers = 0
        if len(enemy_workers) > 2:
            scout_harass = False
            enemy_combat_units |= enemy_workers
            desired_pulled_workers = len(enemy_combat_units) + 2
        elif enemy_workers:
            desired_pulled_workers = min(len(self._pulled_workers), len(enemy_workers) + 2)

        # When pulling workers, pull two more than the enemy has combat units, but leave at least three mining
        # TODO: Select the workers based on which are best to use
        if desired_pulled_workers > len(self._pulled_workers):
            desired_pulled_workers -= len(self._pulled_workers)

            my_main = game_map.get_my_main()
            assert my_main is not None
            base_workers = workers.get_base_workers(my_main)
            desired_pulled_workers = min(desired_pulled_workers, len(base_workers) - 3)
            for worker in base_workers:
                if desired_pulled_workers <= 0:
                    break

                workers.reserve_worker(worker)
                self._pulled_workers.append(worker)
                squad.add_unit(worker)
                desired_pulled_workers -= 1
        elif len(self._pulled_workers) > desired_pulled_workers:
            # (Stardust's loop bound shrinks as workers are released, so it releases about half of the excess.)
            i = 0
            while i < len(self._pulled_workers) - desired_pulled_workers:
                worker = self._pulled_workers.pop()
                squad.remove_unit(worker)
                workers.release_worker(worker)
                i += 1

        # Keep track of when the squad was last regrouping, considering an empty squad to be regrouping
        if (enemy_combat_units and not scout_harass
                and (not squad.get_units() or squad.has_cluster_with_activity(Activity.Regrouping))):
            self._last_regroup_frame = frame

        # Queue emergency production if:
        # - The squad has been regrouping recently
        # - The enemy has us outnumbered by more than two combat units
        # - We don't have any completed cannons (vs. Terran)
        if (0 < self._last_regroup_frame and self._last_regroup_frame > frame - _REGROUP_EMERGENCY_TIMEOUT
                and (bwapi.Broodwar.enemy().getRace() != Races.Terran
                     or units.count_completed(UnitTypes.Protoss_Photon_Cannon) == 0)
                and (squad.combat_unit_count() == 0
                     or (len(enemy_combat_units) + max(0, len(enemy_workers) - 1)) > squad.combat_unit_count() + 2)):
            desired_emergency_production: UnitType = (UnitTypes.Protoss_Dragoon if require_dragoons
                                                      else UnitTypes.Protoss_Zealot)
            if self._emergency_production != desired_emergency_production:
                self._emergency_production = desired_emergency_production
                cherryvis.log(f"DefendMyMain: emergency, producing {self._emergency_production}")
            return

        if self._emergency_production != UnitTypes.None_:
            self._emergency_production = UnitTypes.None_
            cherryvis.log("DefendMyMain: emergency cleared")

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        # If we have an emergency production type, produce an infinite number of them off two gateways
        if self._emergency_production != UnitTypes.None_:
            add_goal(prioritized_production_goals, PRIORITY_EMERGENCY,
                     UnitProductionGoal(self.label, self._emergency_production, -1, 2))
            return

        # Otherwise produce to fulfill our unit requirements
        for unit_requirement in self.status.unit_requirements:
            if unit_requirement.count < 1:
                continue
            add_goal(prioritized_production_goals, PRIORITY_BASEDEFENSE,
                     UnitProductionGoal(self.label, unit_requirement.type, unit_requirement.count,
                                        (unit_requirement.count + 1) // 2))

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        # Release pulled workers
        for worker in self._pulled_workers:
            self._squad.remove_unit(worker)
            workers.release_worker(worker)

        Play.disband(self, removed_unit_callback, movable_unit_callback)

        # Also move the reserved gas steal attacker if we have one
        if self._reserved_gas_steal_attacker is not None:
            movable_unit_callback(self._reserved_gas_steal_attacker)

        for worker_gas_steal_attacker in self._reserved_worker_gas_steal_attackers:
            workers.release_worker(worker_gas_steal_attacker)

    def can_transition_to_attack(self) -> bool:
        return self._squad.can_transition_to_attack()

    def remove_unit(self, unit: MyUnit) -> None:
        if unit is self._reserved_gas_steal_attacker:
            self._reserved_gas_steal_attacker = None

        Play.remove_unit(self, unit)

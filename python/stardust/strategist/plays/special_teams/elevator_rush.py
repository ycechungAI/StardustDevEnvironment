"""Port of Strategist/Plays/SpecialTeams/ElevatorRush.{h,cpp}: builds a proxy robo and shuttles dragoons into the
enemy main.

For now does a fairly hard-coded rush on two maps.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, TilePosition, UnitCommandTypes, UnitTypes
from stardust.builder import builder as building_builder
from stardust.cpp import INT_MAX
from stardust.general import general
from stardust.general.squads.attack_base_squad import AttackBaseSquad
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_SPECIALTEAMS, Play, PlayUnitRequirement, ProductionGoals, \
    UnitCallback, add_goal
from stardust.units import units
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.units.my_unit import MyUnit
    from stardust.units.my_worker import MyWorker


def _bw(unit: MyUnit) -> bwapi.Unit:
    bwapi_unit = unit.bwapi_unit
    assert bwapi_unit is not None
    return bwapi_unit


def _move_between(from_set: dict[MyUnit, None], to_set: dict[MyUnit, None], loaded: bool) -> None:
    """Moves units from one set to the other when their loaded state matches."""
    for unit in list(from_set):
        if _bw(unit).isLoaded() == loaded:
            to_set[unit] = None
            del from_set[unit]


class ElevatorRush(Play):
    def __init__(self) -> None:
        super().__init__("Elevator Rush")

        # This is set to true when we consider the elevator complete
        # We don't disband the play until all units are removed from the squad
        self.complete = False

        # (Default-constructed BWAPI points are (0, 0) on maps without a hard-coded rush.)
        self.pylon_tile = TilePosition(0, 0)
        self.robo_tile = TilePosition(0, 0)
        self.pickup_position = Position(0, 0)
        self.drop_position = Position(0, 0)

        self.builder: MyWorker | None = None
        self.shuttle: MyUnit | None = None
        enemy_starting_main = game_map.get_enemy_starting_main()
        assert enemy_starting_main is not None
        self.squad = AttackBaseSquad(enemy_starting_main, "ElevatorRush")
        # std::set<MyUnit>; insertion order stands in for pointer order
        self.transfer_queue: dict[MyUnit, None] = {}
        self.transferring: dict[MyUnit, None] = {}
        self.transferred: dict[MyUnit, None] = {}
        self.count = 0

        self.picking_up = True  # Whether the shuttle is currently picking up or dropping off

        # Really the squad should be allowed to reposition itself, but for now just have it always attack
        self.squad.ignore_combat_sim = True

        general.add_squad(self.squad)

        game = bwapi.Broodwar
        map_hash = game.mapHash()
        start_location = game.self().getStartLocation()

        # Destination
        if map_hash in ("4e24f217d2fe4dbfa6799bc57f74d8dc939d425b", "e39c1c81740a97a733d227e238bd11df734eaf96"):
            if start_location.y > 60:
                self.pylon_tile = TilePosition(11, 30)
                self.robo_tile = TilePosition(13, 30)
                self.pickup_position = Position(TilePosition(17, 28)) + Position(16, 16)
                self.drop_position = Position(TilePosition(20, 23)) + Position(16, 16)
            else:
                self.pylon_tile = TilePosition(83, 97)
                self.robo_tile = TilePosition(80, 97)
                self.pickup_position = Position(TilePosition(76, 97)) + Position(16, 16)
                self.drop_position = Position(TilePosition(75, 102)) + Position(16, 16)

        # Heartbreak Ridge
        if map_hash in ("6f8da3c3cc8d08d9cf882700efa049280aedca8c", "fe25d8b79495870ac1981c2dfee9368f543321e3",
                        "d9757c0adcfd61386dff8fe3e493e9e8ef9b45e3", "ecb9c70c5594a5c6882baaf4857a61824fba0cfa"):
            if start_location.x > 60:
                self.pylon_tile = TilePosition(15, 8)
                self.robo_tile = TilePosition(15, 10)
                self.pickup_position = Position(TilePosition(19, 12)) + Position(16, 16)
                self.drop_position = Position(TilePosition(21, 24)) + Position(16, 16)
            else:
                self.pylon_tile = TilePosition(111, 86)
                self.robo_tile = TilePosition(110, 84)
                self.pickup_position = Position(TilePosition(108, 83)) + Position(16, 16)
                self.drop_position = Position(TilePosition(108, 73)) + Position(16, 16)

    def get_squad(self) -> AttackBaseSquad:
        return self.squad

    def update(self) -> None:
        squad = self.squad

        # Mark the play complete when our shuttle dies
        if self.shuttle is not None and not self.shuttle.exists():
            self.complete = True
            self.shuttle = None

        # Handle the complete state
        # Here we are just waiting until there are no units remaining that this play needs to own
        if self.complete:
            if self.builder is not None:
                workers.release_worker(self.builder)
                self.builder = None

            # Move all transferred units to the squad
            for unit in self.transferred:
                squad.add_unit(unit)
            self.transferred.clear()

            self.status.removed_units.extend(self.transfer_queue)
            self.transfer_queue.clear()

            # TODO: Handle case where the shuttle has units in it
            if self.shuttle is not None:
                self.status.removed_units.append(self.shuttle)
                self.shuttle = None

            # Disband the play when the squad is empty or the enemy starting main is destroyed
            enemy_starting_main = game_map.get_enemy_starting_main()
            assert enemy_starting_main is not None
            if squad.empty() or enemy_starting_main.owner != bwapi.Broodwar.enemy():
                self.status.complete = True

            return

        # First check if we're finished building
        robo = units.my_building_at(self.robo_tile)
        if robo is None:
            # Start the play once we have two completed dragoons
            if units.count_completed(UnitTypes.Protoss_Dragoon) < 2:
                return

            # Make sure we have a builder
            if self.builder is not None and not self.builder.exists():
                self.builder = None
            if self.builder is None:
                self.builder, _ = workers.get_closest_reassignable_worker(
                    Position(self.pylon_tile) + Position(16, 16), False)
                if self.builder is None:
                    return
                workers.reserve_worker(self.builder)

            # Build the appropriate building
            pylon = units.my_building_at(self.pylon_tile)
            if pylon is None:
                building_builder.build(UnitTypes.Protoss_Pylon, self.pylon_tile, self.builder)
            else:
                building_builder.build(UnitTypes.Protoss_Robotics_Facility, self.robo_tile, self.builder)

            return

        # We don't need the builder once the robo is started
        if self.builder is not None:
            workers.release_worker(self.builder)
            self.builder = None

        # Wait until our robo is finished
        if not robo.completed:
            return

        # Ensure we have a shuttle
        if self.shuttle is None:
            self.status.unit_requirements.append(PlayUnitRequirement(1, UnitTypes.Protoss_Shuttle,
                                                                     self.pickup_position))

        # Clean up and micro transfer queue
        for unit in list(self.transfer_queue):
            if unit.exists():
                unit.move_to(self.pickup_position)
            else:
                # Remove units from the count if they were not near the pickup position when they died
                if unit.last_position.getApproxDistance(self.pickup_position) > 400:
                    self.count -= 1

                del self.transfer_queue[unit]

        # Request units
        # Start by rallying 4 until the shuttle is complete
        needed = (15 if self.shuttle is not None else 4) - self.count
        if needed > 0:
            self.status.unit_requirements.append(PlayUnitRequirement(needed, UnitTypes.Protoss_Dragoon,
                                                                     self.pickup_position))

        shuttle = self.shuttle
        if shuttle is None:
            return

        # Move units through the sets when their state changes
        _move_between(self.transfer_queue, self.transferring, True)
        _move_between(self.transferring, self.transferred, False)

        # Micro the shuttle
        # It switches between pickup and drop
        if self.picking_up:
            if len(self.transferring) == 2:
                self.picking_up = False
            else:
                closest_pickup: MyUnit | None = None
                closest_pickup_dist = INT_MAX
                for unit in self.transfer_queue:
                    dist = path_finding.get_ground_distance(unit.last_position, self.pickup_position, unit.type)
                    if dist != -1 and dist < closest_pickup_dist:
                        closest_pickup = unit
                        closest_pickup_dist = dist
                if closest_pickup is not None and closest_pickup_dist < 64:
                    shuttle.load(_bw(closest_pickup))
                elif self.transferring and closest_pickup_dist > 500:
                    # Transfer one unit if the next one is a long way away
                    self.picking_up = False
                else:
                    shuttle.move_to(self.pickup_position)
        if not self.picking_up:
            if not self.transferring:
                self.picking_up = True
                shuttle.move_to(self.pickup_position)
            else:
                if _bw(shuttle).getLastCommand().getType() != UnitCommandTypes.Unload_All_Position:
                    shuttle.unload_all(self.drop_position)

        # Micro transferred units
        for unit in self.transferred:
            unit.move_to(self.drop_position)

        # Move transferred units to the squad if the shuttle is empty and either:
        # - The squad is already attacking
        # - We have moved at least 8 of them
        # - We have no more waiting
        # TODO attack if any of our transferred units are under attack?
        if not self.transferring and (not squad.empty() or len(self.transferred) > 7 or not self.transfer_queue):
            for unit in self.transferred:
                squad.add_unit(unit)
            self.transferred.clear()

        # Move to complete mode when all of our sets are empty
        if not self.transfer_queue and not self.transferring and not self.transferred:
            self.complete = True

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        if self.builder is not None:
            workers.release_worker(self.builder)
        if self.shuttle is not None:
            removed_unit_callback(self.shuttle)
        for unit in self.transfer_queue:
            removed_unit_callback(unit)
        for unit in self.transferred:
            removed_unit_callback(unit)

        super().disband(removed_unit_callback, movable_unit_callback)

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        # Only produces a shuttle when the requirement hasn't been met
        for unit_requirement in self.status.unit_requirements:
            if unit_requirement.count < 1:
                continue
            if unit_requirement.type != UnitTypes.Protoss_Shuttle:
                continue

            add_goal(prioritized_production_goals, PRIORITY_SPECIALTEAMS,
                     UnitProductionGoal(self.label, unit_requirement.type, unit_requirement.count, 1))

    def add_unit(self, unit: MyUnit) -> None:
        if unit.type == UnitTypes.Protoss_Shuttle:
            self.shuttle = unit
        else:
            self.transfer_queue[unit] = None
            unit.move_to(self.pickup_position)
            self.count += 1

    def remove_unit(self, unit: MyUnit) -> None:
        if self.shuttle is unit:
            self.shuttle = None
        if unit in self.transfer_queue:
            del self.transfer_queue[unit]
            self.count -= 1
        self.transferring.pop(unit, None)
        self.transferred.pop(unit, None)

        super().remove_unit(unit)

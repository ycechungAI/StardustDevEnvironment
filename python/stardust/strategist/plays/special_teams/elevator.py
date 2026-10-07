"""Port of Strategist/Plays/SpecialTeams/Elevator.{h,cpp}: uses a shuttle to ferry units over a cliff into (or out of)
a main base."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, Positions, TilePosition, TilePositions, UnitCommandTypes, UnitType, UnitTypes
from stardust import common, config, opponent
from stardust.cpp import INT_MAX
from stardust.general import general
from stardust.general.squads.attack_base_squad import AttackBaseSquad
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.strategist.play import Play, PlayUnitRequirement, ProductionGoals, UnitCallback
from stardust.units import units
from stardust.util import geo, unit_util

if TYPE_CHECKING:
    import bwem
    from stardust.map.base import Base
    from stardust.map.choke import Choke
    from stardust.units.my_unit import MyUnit

_CVIS_ELEVATORTILES_HEATMAP = False
_CVIS_LOG_ELEVATOR_STATE = config.INSTRUMENTATION_ENABLED

_SHUTTLE_HALT_DISTANCE = unit_util.halt_distance(UnitTypes.Protoss_Shuttle) + 16


def _bw(unit: MyUnit) -> bwapi.Unit:
    bwapi_unit = unit.bwapi_unit
    assert bwapi_unit is not None
    return bwapi_unit


def _log_state(shuttle: MyUnit, message: str) -> None:
    if _CVIS_LOG_ELEVATOR_STATE:
        cherryvis.log(f"Elevator: {message}", shuttle.id)


def _valid_elevator_position(tile: TilePosition) -> bool:
    def valid_and_walkable(t: TilePosition) -> bool:
        return t.isValid() and game_map.is_walkable_tile(t)

    # The tile itself must be valid and walkable
    if not valid_and_walkable(tile):
        return False

    # At least three of its direct neighbours must be valid and walkable
    count = 0
    if valid_and_walkable(tile + TilePosition(1, 0)):
        count += 1
    if valid_and_walkable(tile + TilePosition(-1, 0)):
        count += 1
    if valid_and_walkable(tile + TilePosition(0, 1)):
        count += 1
    if valid_and_walkable(tile + TilePosition(0, -1)):
        count += 1
    return count >= 3


def _scaled_position(current_position: Position, vector: Position, length: int) -> Position:
    scaled_vector = geo.scale_vector(vector, length)
    if scaled_vector == Positions.Invalid:
        return Positions.Invalid

    return current_position + scaled_vector


def _shuttle_move(shuttle: MyUnit, pos: Position) -> None:
    # Always move towards a position at least halt distance away
    move_target = _scaled_position(shuttle.last_position, pos - shuttle.last_position, _SHUTTLE_HALT_DISTANCE)

    # If it is invalid, it means we are exactly on our target position, so just use our main until we get somewhere
    # else
    if move_target == Positions.Invalid:
        my_main = game_map.get_my_main()
        assert my_main is not None
        move_target = my_main.get_position()

    # If it is off the map, move directly to the target
    if not move_target.isValid():
        move_target = pos

    shuttle.move_to(move_target)


def _drop(shuttle: MyUnit, pos: Position) -> None:
    loaded_units = _bw(shuttle).getLoadedUnits()
    if not loaded_units:
        return

    if _bw(shuttle).getLastCommand().getType() == UnitCommandTypes.Unload:
        # Wait 12 frames after ordering an unload
        # (As in Stardust, this returns once the 12 frames have passed, not while waiting.)
        if shuttle.last_command_frame < (common.current_frame - 12):
            return

        # Move to the drop position on the 12th frame
        if shuttle.last_command_frame == (common.current_frame - 12):
            _shuttle_move(shuttle, pos)
            return

    # If we are close enough to the target position, start to drop
    # Otherwise move towards the position
    if shuttle.get_distance(pos) < 32:
        shuttle.unload(next(iter(loaded_units)))
    else:
        _shuttle_move(shuttle, pos)


def _pickup(shuttle: MyUnit, cargo: MyUnit) -> None:
    if _bw(shuttle).getLastCommand().getType() == UnitCommandTypes.Load:
        # Wait 12 frames after ordering a load
        if shuttle.last_command_frame < (common.current_frame - 12):
            return

        # Move to the load position on the 12th frame
        if shuttle.last_command_frame == (common.current_frame - 12):
            _shuttle_move(shuttle, cargo.last_position)
            return

    # If we are close enough to the cargo, start to load
    # Otherwise move towards its position
    if shuttle.get_distance(cargo) < 12:
        shuttle.load(_bw(cargo))
    else:
        _shuttle_move(shuttle, cargo.last_position)


def _move_between(from_set: dict[MyUnit, None], to_set: dict[MyUnit, None], loaded: bool) -> None:
    """Moves units from one set to the other when their loaded state matches."""
    for unit in list(from_set):
        if _bw(unit).isLoaded() == loaded:
            to_set[unit] = None
            del from_set[unit]


class Elevator(Play):
    def __init__(self, from_our_main: bool = False, unit_type: UnitType = UnitTypes.Protoss_Dragoon) -> None:
        super().__init__("Elevator")
        self.from_our_main = from_our_main
        self.unit_type = unit_type

        # This is set to true when we consider the elevator complete
        # We don't disband the play until all units are removed from the squad
        self.complete = False

        self.pickup_position = Positions.Invalid
        self.drop_position = Positions.Invalid

        self.shuttle: MyUnit | None = None
        self.squad: AttackBaseSquad | None = None
        # std::set<MyUnit>; insertion order stands in for pointer order
        self.transfer_queue: dict[MyUnit, None] = {}
        self.transferring: dict[MyUnit, None] = {}
        self.transferred: dict[MyUnit, None] = {}
        self.count = 0
        self.count_added_to_squad = 0

        self.picking_up = True  # Whether the shuttle is currently picking up or dropping off

    def get_squad(self) -> AttackBaseSquad | None:
        return self.squad

    def _set_positions(self) -> bool:
        positions = Elevator.select_positions(game_map.get_my_main() if self.from_our_main
                                              else game_map.get_enemy_starting_main())
        if not positions[0].isValid() or not positions[1].isValid():
            return False

        self.drop_position = Position(positions[1] if self.from_our_main else positions[0]) + Position(16, 16)
        self.pickup_position = Position(positions[0] if self.from_our_main else positions[1]) + Position(16, 16)
        return True

    def update(self) -> None:
        # Initialization when enemy base is known
        if self.squad is None:
            enemy_main = game_map.get_enemy_starting_main()
            if enemy_main is None:
                return

            # Get pickup and dropoff positions, aborting if they cannot be determined
            if not self._set_positions():
                self.status.complete = True
                return

            self.squad = AttackBaseSquad(enemy_main, "Elevator")
            # Really the squad should be allowed to reposition itself, but for now just have it always attack
            self.squad.ignore_combat_sim = not self.from_our_main
            general.add_squad(self.squad)
        squad = self.squad

        # If the drop position has become unwalkable, pick a new one
        # This could happen if the enemy builds something there
        if not _valid_elevator_position(TilePosition(self.drop_position)):
            if self._set_positions():
                if config.LOGGING_ENABLED:
                    log.get(f"Updated elevator tiles: {self.drop_position}, {self.pickup_position}")
            else:
                self.complete = True

                if config.LOGGING_ENABLED:
                    log.get("No longer valid elevator tiles; cancelling elevator")

        # Mark the play complete when our shuttle dies
        if self.shuttle is not None and not self.shuttle.exists():
            self.complete = True
            self.shuttle = None

        # Clean up and micro the transfer queue
        for unit in list(self.transfer_queue):
            if unit.exists():
                # If the unit is near the pickup position and is under attack, cancel the play
                # This might happen on maps where there isn't a pickup position sufficiently far away from the natural
                if unit.is_being_attacked() and unit.last_position.getApproxDistance(self.pickup_position) < 200:
                    self.complete = True

                unit.move_to(self.pickup_position)
            else:
                # Remove units from the count if they were not near the pickup position when they died
                if unit.last_position.getApproxDistance(self.pickup_position) > 400:
                    self.count -= 1

                del self.transfer_queue[unit]

        # Move units through the sets when their state changes
        _move_between(self.transfer_queue, self.transferring, True)
        _move_between(self.transferring, self.transferred, False)

        # Move transferred units to the squad if:
        # - The play is complete (no new units are being transferred)
        # - The squad is already attacking
        # - We have moved at least 8 of them
        # - We have no more waiting
        # - One of our transferred units is under attack
        if (self.complete or not squad.empty() or len(self.transferred) > 7 or not self.transfer_queue
                or any(unit.is_being_attacked() for unit in self.transferred)):
            for unit in self.transferred:
                squad.add_unit(unit)
                self.count_added_to_squad += 1
                opponent.increment_game_value("elevatoredUnits")
            self.transferred.clear()

        # Handle the complete state
        # Here we are just waiting until there are no units remaining that this play needs to own
        if self.complete:
            # Release all units from the transfer queue
            self.status.removed_units.extend(self.transfer_queue)
            self.transfer_queue.clear()

            # If units are in transit, drop them off
            if self.shuttle is not None and _bw(self.shuttle).getLoadedUnits():
                _drop(self.shuttle, self.drop_position)
                return

            # Release the shuttle
            if self.shuttle is not None:
                self.status.removed_units.append(self.shuttle)
                self.shuttle = None

            # Disband the play when the squad is empty or the enemy starting main is destroyed
            enemy_starting_main = game_map.get_enemy_starting_main()
            assert enemy_starting_main is not None
            if squad.empty() or enemy_starting_main.owner != bwapi.Broodwar.enemy():
                self.status.complete = True

            return

        # If we don't have a shuttle, the play hasn't started yet, so determine if it should
        # The shuttle dying triggers play completion, so execution won't get this far
        shuttle = self.shuttle
        if shuttle is None:
            # Never start the play after frame 12000
            if common.current_frame > 12000:
                self.complete = True
                return

            if not self.from_our_main and not Elevator.is_elevator_feasible(self.unit_type):
                return

            self.status.unit_requirements.append(PlayUnitRequirement(1, UnitTypes.Protoss_Shuttle,
                                                                     self.pickup_position))
            return

        # Request units
        if self.count < 12:
            dist = shuttle.get_distance(self.pickup_position)
            needed = -self.count
            if dist < 1280:
                needed += 4
            if dist < 320:
                needed += 8
            if needed > 0:
                self.status.unit_requirements.append(PlayUnitRequirement(needed, self.unit_type,
                                                                         self.pickup_position))

        # Micro the shuttle
        # It switches between pickup and drop
        if self.picking_up:
            if len(self.transferring) == 2:
                self.picking_up = False
                _log_state(shuttle, "Shuttle full, switching to drop-off state")
            else:
                closest_pickup: MyUnit | None = None
                closest_pickup_dist = INT_MAX
                for unit in self.transfer_queue:
                    dist = path_finding.get_ground_distance(unit.last_position, self.pickup_position, unit.type)
                    if dist != -1 and dist < closest_pickup_dist:
                        closest_pickup = unit
                        closest_pickup_dist = dist
                if closest_pickup is not None and closest_pickup_dist < 64:
                    _pickup(shuttle, closest_pickup)
                    _log_state(shuttle, f"Loading {closest_pickup}")
                elif self.transferring and closest_pickup_dist > 500:
                    # Transfer one unit if the next one is a long way away
                    self.picking_up = False
                    _log_state(shuttle, "Next unit is far away, switching to drop-off state")
                else:
                    _shuttle_move(shuttle, self.pickup_position)
                    _log_state(shuttle, "Waiting for unit to load")
        if not self.picking_up:
            if not self.transferring and not _bw(shuttle).getLoadedUnits():
                self.picking_up = True
                _shuttle_move(shuttle, self.pickup_position)
                _log_state(shuttle, "Shuttle empty, switching to pickup state")
            else:
                _drop(shuttle, self.drop_position)
                _log_state(shuttle, "Drop unit")

        # Micro transferred units
        for unit in self.transferred:
            unit.move_to(self.drop_position)

        # Move to complete mode when:
        # - we've transferred more than 10 units and all of our sets are empty
        # - half or more of our transferred units have died
        if ((self.count > 10 and not self.transfer_queue and not self.transferring and not self.transferred)
                or (self.count_added_to_squad > 2
                    and squad.combat_unit_count() <= (self.count_added_to_squad // 2))):
            self.complete = True

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        pass

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        if self.shuttle is not None:
            removed_unit_callback(self.shuttle)
        for unit in self.transfer_queue:
            removed_unit_callback(unit)
        for unit in self.transferred:
            removed_unit_callback(unit)

        super().disband(removed_unit_callback, movable_unit_callback)

    def can_reassign_unit(self, unit: MyUnit) -> bool:
        # Don't allow the shuttle to be reassigned
        if unit is self.shuttle:
            return False

        # Allow other units to be reassigned only if they are in the transfer queue
        return unit in self.transfer_queue

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

    @staticmethod
    def select_positions(base: Base | None) -> tuple[TilePosition, TilePosition]:
        """Gets the pair of positions to use for an elevator into or out of a base.
        The first position is in the base, the second outside it."""
        assert base is not None
        base_tile = TilePositions.Invalid
        map_tile = TilePositions.Invalid

        # Get base areas (std::set<const BWEM::Area*>: pointer order is BWEM's area order)
        area_set: set[bwem.Area] = set()
        if base.is_starting_base():
            area_set.update(game_map.get_starting_base_areas(base))
        else:
            area_set.add(base.get_area())
        areas = sorted(area_set, key=lambda area: area.Id())

        # Get all narrow chokes out of the base areas
        chokes: dict[Choke, None] = {}
        for area in areas:
            for bwem_choke in area.ChokePoints():
                choke = game_map.choke(bwem_choke)
                assert choke is not None
                if choke.is_narrow_choke:
                    chokes[choke] = None

        # Now score all of the base's edge tiles based on their proximity to the depot and choke(s)
        scored_base_tiles: list[tuple[TilePosition, bwem.Area, int]] = []
        areas_to_edge_positions = game_map.get_areas_to_edge_positions()
        for area in areas:
            edge_positions = areas_to_edge_positions.get(area)
            if edge_positions is None:
                continue
            for edge_position in sorted(edge_positions, key=lambda tile: (tile.x, tile.y)):
                # Must be a walkable tile with some space around it
                if not _valid_elevator_position(edge_position):
                    continue

                pos = Position(edge_position) + Position(16, 16)
                score = pos.getApproxDistance(base.get_position()) * len(chokes) * 2
                for choke in chokes:
                    score += pos.getApproxDistance(choke.center)
                scored_base_tiles.append((edge_position, area, score))
        # (std::sort is not stable; ties keep their insertion order here)
        scored_base_tiles.sort(key=lambda entry: entry[2], reverse=True)

        # Determine the areas to avoid when choosing a map tile
        # For starting bases we want to also avoid using the natural
        avoid_areas: set[bwem.Area] = set(areas)
        if base.is_starting_base():
            natural = game_map.get_starting_base_natural(base)
            if natural is not None:
                avoid_areas.add(natural.get_area())

        # Take the best tile that has a corresponding map tile no more than 8 tiles away
        edge_positions_to_area = game_map.get_edge_positions_to_area()
        found = False
        for tile, _, _ in scored_base_tiles:
            spiral = geo.Spiral()
            while spiral.radius <= 8:
                spiral.next()
                here = tile + TilePosition(spiral.x, spiral.y)

                # Must be a walkable tile with some space around it
                if not _valid_elevator_position(here):
                    continue

                # Must not be on an island or leaf area
                if game_map.is_on_island(here):
                    continue
                if game_map.is_in_leaf_area(here):
                    continue

                # Must be an edge tile
                edge_tile_area = edge_positions_to_area.get(here)
                if edge_tile_area is None:
                    continue

                # Must not be in one of our avoid areas
                if edge_tile_area in avoid_areas:
                    continue

                # We have a match
                base_tile = tile
                map_tile = here
                found = True
                break
            if found:
                break

        if _CVIS_ELEVATORTILES_HEATMAP and base_tile.isValid() and map_tile.isValid():
            # Dump to CherryVis if we found valid tiles
            map_width = bwapi.Broodwar.mapWidth()
            map_height = bwapi.Broodwar.mapHeight()
            elevator_cvis = [0] * (map_width * map_height)
            elevator_cvis[base_tile.x + base_tile.y * map_width] = 100
            elevator_cvis[map_tile.x + map_tile.y * map_width] = 100
            cherryvis.add_heatmap("ElevatorTiles", elevator_cvis, map_width, map_height)
        if config.LOGGING_ENABLED:
            if base_tile.isValid() and map_tile.isValid():
                log.get(f"Selected elevator tiles: {base_tile}, {map_tile}")
            else:
                log.get("Could not find elevator tiles")

        return base_tile, map_tile

    @staticmethod
    def is_elevator_feasible(elevator_unit_type: UnitType) -> bool:
        """Whether doing an elevator into the enemy main is feasible."""
        import stardust.strategist.strategist as strategist

        if common.current_frame > 12000:
            return False

        enemy_natural = game_map.get_enemy_starting_natural()
        if (not strategist.is_enemy_contained()
                or units.count_completed(elevator_unit_type) < 5
                or enemy_natural is None or enemy_natural.owner != bwapi.Broodwar.enemy()):
            return False

        for bunker in units.all_enemy_of_type(UnitTypes.Terran_Bunker):
            if bunker.get_distance(enemy_natural.get_position()) > 640:
                return False

        return True

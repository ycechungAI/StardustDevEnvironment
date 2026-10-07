"""Port of Strategist/Plays/SpecialTeams/ShuttleHarass.{h,cpp}: shuttles that pick up zealots from the main army and
drop them on siege tanks."""

from __future__ import annotations

import itertools
import sys
from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, Positions, UnitTypes, WalkPosition
from stardust import common, config
from stardust.cpp import INT_MAX
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.players import players
from stardust.strategist.play import Play, PlayUnitRequirement, ProductionGoals, UnitCallback
from stardust.units import units
from stardust.util import geo, unit_util

if TYPE_CHECKING:
    from stardust.players.grid import Grid
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit

_HALT_DISTANCE = unit_util.halt_distance(UnitTypes.Protoss_Shuttle) + 16

# Define some positions for use in searching outwards from a point at tile resolution
_SURROUNDING_POSITIONS = (
    Position(0, -48), Position(16, -48), Position(32, -32), Position(48, -16),
    Position(48, 0), Position(48, 16), Position(32, 32), Position(16, 48),
    Position(0, 48), Position(-16, 48), Position(-32, 32), Position(-48, 16),
    Position(-48, 0), Position(-48, -16), Position(-32, -32), Position(-16, -48),
)


def _log(unit: MyUnit, message: str) -> None:
    if config.DEBUG_UNIT_ORDERS:
        cherryvis.log(message, unit.id)


def _bw(unit: MyUnit) -> bwapi.Unit:
    bwapi_unit = unit.bwapi_unit
    assert bwapi_unit is not None
    return bwapi_unit


def _my_main_position() -> Position:
    my_main = game_map.get_my_main()
    assert my_main is not None
    return my_main.get_position()


def _scaled_position(current_position: Position, vector: Position, length: int) -> Position:
    scaled_vector = geo.scale_vector(vector, length)
    if scaled_vector == Positions.Invalid:
        return Positions.Invalid

    return current_position + scaled_vector


def _move_preserving_speed(shuttle: MyUnit, target: Position) -> None:
    dist = shuttle.last_position.getApproxDistance(target)
    if dist == 0:
        _log(shuttle, f"Move to {WalkPosition(target)}: on top; moving towards main")

        # We're directly on top of the target, so we just need to move away
        shuttle.move_to(_my_main_position())
        return

    # If the target is far enough away, just move directly
    if dist >= _HALT_DISTANCE:
        _log(shuttle, f"Move to {WalkPosition(target)}: far away; moving directly")
        shuttle.move_to(target)
        return

    # Otherwise get a position that overshoots the target sufficiently
    scaled_vector = geo.scale_vector(target - shuttle.last_position, _HALT_DISTANCE)
    scaled_target = shuttle.last_position + scaled_vector
    if not scaled_target.isValid():
        _log(shuttle, f"Move to {WalkPosition(target)}: near map edge; moving directly")
        shuttle.move_to(target)
        return

    _log(shuttle, f"Move to {WalkPosition(target)}: moving to {WalkPosition(scaled_target)}")
    shuttle.move_to(scaled_target)


def _move_avoiding_threats(grid: Grid, shuttle: MyUnit, target: Position) -> None:
    # Check for threats one-and-a-half tiles ahead
    ahead = _scaled_position(shuttle.last_position, target - shuttle.last_position, 48)
    if not ahead.isValid() or grid.air_threat(ahead) == 0:
        _move_preserving_speed(shuttle, target)
        return

    # Get the surrounding position closest to the target that is not under air threat
    best_dist = INT_MAX
    best = Positions.Invalid
    for offset in _SURROUNDING_POSITIONS:
        here = shuttle.last_position + offset
        if not here.isValid():
            continue
        if grid.air_threat(here) > 0:
            continue

        dist = here.getApproxDistance(target)
        if dist < best_dist:
            best = here
            best_dist = dist

    # Move towards the best tile if possible
    if best != Positions.Invalid:
        _move_preserving_speed(shuttle, best)
        return

    # We couldn't find a better tile to move to, so just move away from the target position
    behind = _scaled_position(shuttle.last_position, shuttle.last_position - target, 64)
    if behind.isValid():
        _move_preserving_speed(shuttle, behind)
    else:
        # Default to main base location when we don't have anywhere better to go
        shuttle.move_to(_my_main_position())


class ShuttleHarass(Play):
    def __init__(self) -> None:
        super().__init__("ShuttleHarass")
        # std::map<MyUnit, std::set<MyUnit>> and std::map<MyUnit, Unit>; insertion order stands in for pointer order
        self._shuttles_and_cargo: dict[MyUnit, dict[MyUnit, None]] = {}
        self._cargo_and_targets: dict[MyUnit, Unit | None] = {}

    def update(self) -> None:
        # Always request shuttles so all unassigned shuttles get assigned to this play
        # This play is always at lowest priority with respect to other plays utilizing shuttles
        self.status.unit_requirements.append(PlayUnitRequirement(10, UnitTypes.Protoss_Shuttle,
                                                                 _my_main_position()))

        # Micro dropped units
        for cargo_unit, cargo_target in list(self._cargo_and_targets.items()):
            # We are still loaded - wait until we get dropped
            if _bw(cargo_unit).isLoaded():
                _log(cargo_unit, "Waiting to be unloaded")
                continue

            # Target is dead
            if cargo_target is None or not cargo_target.exists():
                cargo_target = self._closest_tank(cargo_unit)
                if cargo_target is None:
                    _log(cargo_unit, "No targets available")
                    self.status.removed_units.append(cargo_unit)
                    continue

                _log(cargo_unit, f"Retargeted to {cargo_target}")
                self._cargo_and_targets[cargo_unit] = cargo_target

            # Attack the target
            cargo_unit.attack_unit(cargo_target)

        # Micro shuttles
        grid = players.grid(bwapi.Broodwar.enemy())
        import stardust.strategist.strategist as strategist
        main_army_play = strategist.get_main_army_play()
        main_army_squad = main_army_play.get_squad() if main_army_play is not None else None
        main_army_vanguard = main_army_squad.vanguard_cluster() if main_army_squad is not None else None
        for shuttle, cargo in self._shuttles_and_cargo.items():
            # If all cargo is loaded, update their target
            if len(cargo) >= 2 and all(_bw(unit).isLoaded() for unit in cargo):
                # Ensure we have a target
                target = self._cargo_and_targets.get(next(iter(cargo)))
                if target is not None:
                    if not target.exists():
                        _log(shuttle, "Clearing target as it no longer exists")
                        target = None
                    elif not target.last_position_valid:
                        _log(shuttle, "Clearing target as its last position is no longer valid")
                        target = None
                    elif shuttle.get_distance(target) > 48 and grid.air_threat(target.last_position) > 0:
                        _log(shuttle, "Clearing target as its last position is now under air threat")
                        target = None
                    elif (grid.ground_threat(target.last_position)
                          - grid.static_ground_threat(target.last_position)) > 30:
                        _log(shuttle, "Clearing target as its last position is now under non-static ground threat")
                        target = None

                if target is None:
                    target = self._best_drop_target(grid, shuttle)

                    if target is not None:
                        _log(shuttle, f"Selected target {target}")
                    for unit in cargo:
                        self._cargo_and_targets[unit] = target

                # If we don't have a target, hang around the vanguard cluster until we get one
                if target is None:
                    if main_army_vanguard is not None:
                        _move_avoiding_threats(grid, shuttle, main_army_vanguard.center)
                        _log(shuttle, f"Cargo loaded; no target; moving to vanguard @ "
                                      f"{WalkPosition(main_army_vanguard.center)}")
                    else:
                        _move_avoiding_threats(grid, shuttle, _my_main_position())
                        _log(shuttle, "Cargo loaded; no target; moving to main")
                    continue

            # If we are in the drop phase, drop loaded cargo on the target
            first_target: Unit | None = None
            if cargo:
                first_target = self._cargo_and_targets.get(next(iter(cargo)))
                if first_target is not None and not first_target.exists():
                    first_target = None
            if first_target is not None:
                # If all of our cargo is dropped, clear it and fall through to get new cargo
                if all(not _bw(unit).isLoaded() for unit in cargo):
                    _log(shuttle, "Cleared cargo")
                    cargo.clear()
                else:
                    # Drop loaded units on the target
                    dist_to_target = shuttle.get_distance(first_target)
                    if dist_to_target > 16:
                        _move_avoiding_threats(grid, shuttle, first_target.last_position)
                        _log(shuttle, f"Cargo loaded; moving to target {WalkPosition(first_target.last_position)}")
                    else:
                        for unit in cargo:
                            if _bw(unit).isLoaded():
                                shuttle.unload(_bw(unit))
                                break
                        _log(shuttle, f"Cargo loaded; unloading on target {WalkPosition(first_target.last_position)}")
                    continue

            # We have cargo awaiting pickup
            if cargo and any(not _bw(unit).isLoaded() for unit in cargo):
                for unit in cargo:
                    if not _bw(unit).isLoaded():
                        unit.move_to(shuttle.last_position)

                        dist_to_cargo = shuttle.get_distance(unit)
                        if dist_to_cargo > 100:
                            _move_avoiding_threats(grid, shuttle, unit.last_position)
                            _log(shuttle, f"Moving to cargo {unit}")
                        else:
                            shuttle.load(_bw(unit))
                            _log(shuttle, f"Loading cargo {unit}")
                        break

                continue

            # We need more cargo

            # If we don't have a main army vanguard, move the shuttle to our main base until we do
            if main_army_vanguard is None:
                _move_avoiding_threats(grid, shuttle, _my_main_position())
                _log(shuttle, "Waiting for vanguard")
                continue

            # Move to the vanguard center
            _move_avoiding_threats(grid, shuttle, main_army_vanguard.center)

            # If the shuttle is near the vanguard center, and there is a potential target, request a zealot
            vanguard_dist = shuttle.get_distance(main_army_vanguard.center)
            if (vanguard_dist < (500 + main_army_vanguard.line_radius)
                    and units.count_enemy(UnitTypes.Terran_Siege_Tank_Siege_Mode)):
                self.status.unit_requirements.append(PlayUnitRequirement(1, UnitTypes.Protoss_Zealot,
                                                                         shuttle.last_position, 640))
                _log(shuttle, "Requesting zealot")

    @staticmethod
    def _closest_tank(cargo_unit: MyUnit) -> Unit | None:
        """The siege tank closest by ground to a dropped unit whose target is dead."""
        target: Unit | None = None
        best_dist = INT_MAX
        for unit in itertools.chain(units.all_enemy_of_type(UnitTypes.Terran_Siege_Tank_Siege_Mode),
                                    units.all_enemy_of_type(UnitTypes.Terran_Siege_Tank_Tank_Mode)):
            if not unit.last_position_valid:
                continue
            if unit.bwapi_unit is not None and unit.bwapi_unit.isStasised():
                continue

            dist = path_finding.get_ground_distance(cargo_unit.last_position, unit.last_position, cargo_unit.type,
                                                    PathFindingOptions.UseNeighbouringBWEMArea)
            if dist != -1 and dist < best_dist:
                best_dist = dist
                target = unit
        return target

    @staticmethod
    def _best_drop_target(grid: Grid, shuttle: MyUnit) -> Unit | None:
        """The siege tank to drop the loaded cargo on."""
        target: Unit | None = None
        best_score = sys.float_info.max
        for unit in itertools.chain(units.all_enemy_of_type(UnitTypes.Terran_Siege_Tank_Siege_Mode),
                                    units.all_enemy_of_type(UnitTypes.Terran_Siege_Tank_Tank_Mode)):
            if not unit.last_position_valid:
                continue
            if unit.bwapi_unit is not None and unit.bwapi_unit.isStasised():
                continue

            # Skip tanks that have been repaired in the last 30 seconds
            if unit.last_heal_frame > (common.current_frame - 640):
                continue

            dist = shuttle.get_distance(unit)

            # Don't drop on units covered by anti-air
            if dist > 48 and grid.air_threat(unit.last_position) > 0:
                continue

            # Don't drop on units that are covered by excessive non-static ground threats
            if grid.ground_threat(unit.last_position) - grid.static_ground_threat(unit.last_position) > 30:
                continue

            # Prefer sieged tanks
            score = float(dist)
            if unit.type == UnitTypes.Terran_Siege_Tank_Tank_Mode:
                score *= 1.5

            # Prefer damaged tanks
            percent_damaged = unit.health / UnitTypes.Terran_Siege_Tank_Siege_Mode.maxHitPoints()
            score /= max(percent_damaged, 0.25)

            if score < best_score:
                best_score = score
                target = unit
        return target

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        pass

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        all_units: dict[MyUnit, None] = {}
        for shuttle, cargo in self._shuttles_and_cargo.items():
            all_units[shuttle] = None
            for cargo_unit in cargo:
                all_units[cargo_unit] = None
        for cargo_unit in self._cargo_and_targets:
            all_units[cargo_unit] = None

        for unit in all_units:
            movable_unit_callback(unit)

    def can_reassign_unit(self, unit: MyUnit) -> bool:
        # Allow shuttles to be reassigned only if they are not carrying anything
        if unit.type == UnitTypes.Protoss_Shuttle:
            return len(_bw(unit).getLoadedUnits()) == 0

        # Allow other units to be reassigned only if they are not being carried and don't have a target
        if _bw(unit).isLoaded():
            return False
        if unit not in self._cargo_and_targets:
            return True
        target = self._cargo_and_targets[unit]
        return target is None or not target.exists()

    def add_unit(self, unit: MyUnit) -> None:
        if unit.type == UnitTypes.Protoss_Shuttle:
            self._shuttles_and_cargo[unit] = {}
        else:
            # This unit will be the cargo of the nearest shuttle not currently "full"
            closest_shuttle: MyUnit | None = None
            closest_shuttle_dist = INT_MAX
            for shuttle, cargo in self._shuttles_and_cargo.items():
                if len(cargo) >= 2:
                    continue

                dist = shuttle.get_distance(unit)
                if dist < closest_shuttle_dist:
                    closest_shuttle_dist = dist
                    closest_shuttle = shuttle

            if closest_shuttle is not None:
                self._shuttles_and_cargo[closest_shuttle][unit] = None
            else:
                log.get(f"Error: Tried to add {unit} to shuttle harass, but no valid shuttle available")
                self.status.removed_units.append(unit)
        super().add_unit(unit)

    def remove_unit(self, unit: MyUnit) -> None:
        # Remove as cargo from any shuttles
        for cargo in self._shuttles_and_cargo.values():
            cargo.pop(unit, None)

        # Remove shuttle or cargo
        self._shuttles_and_cargo.pop(unit, None)
        self._cargo_and_targets.pop(unit, None)

        super().remove_unit(unit)

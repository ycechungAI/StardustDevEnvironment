"""Port of Strategist/Plays/Defensive/DefendBase.{h,cpp}: defends one of our bases with units and static defense."""

from __future__ import annotations

from collections import deque
from typing import TYPE_CHECKING

import bwapi
from bwapi import Races, TilePositions, UnitTypes, UpgradeTypes
from stardust import common
from stardust.builder import builder, building_placement
from stardust.builder.block import Location
from stardust.builder.building_placement import BuildLocation
from stardust.general import general
from stardust.general.squads.defend_base_squad import DefendBaseSquad
from stardust.general.unit_cluster import combat_sim
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.players import players
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_BASEDEFENSE, PRIORITY_LOWEST, PRIORITY_MAINARMY, \
    PRIORITY_MAINARMYBASEPRODUCTION, PRIORITY_NORMAL, Play, PlayUnitRequirement, ProductionGoals, add_goal
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.map.path_finding.navigation_grid import GridNode
    from stardust.units.my_unit import MyUnit


def _close_grid_node(grid_node: GridNode) -> bool:
    return grid_node.cost < 1200


def _safe_grid_node(grid_node: GridNode) -> bool:
    return grid_node.cost < 1200 or players.grid(bwapi.Broodwar.enemy()).ground_threat(grid_node.center()) == 0


class DefendBase(Play):
    def __init__(self, base: Base, enemy_value: int) -> None:
        super().__init__(f"Defend base @ {base.get_tile_position()}")
        self.base = base
        self.enemy_value = enemy_value
        self._squad = DefendBaseSquad(base)
        self._pylon_location = TilePositions.Invalid
        self._cannon_locations: deque[bwapi.TilePosition] = deque()
        self._pylon: MyUnit | None = None
        self._cannons: list[MyUnit] = []

        general.add_squad(self._squad)

        # Get the static defense locations for this base
        base_static_defense_locations = building_placement.base_static_defense_locations(base)
        if base_static_defense_locations.is_valid():
            self._pylon_location = base_static_defense_locations.power_pylon
            self._cannon_locations.extend(base_static_defense_locations.worker_defense_cannons)

            # Get any existing units - maybe we have had a defend base squad for this base before
            self._pylon = units.my_building_at(self._pylon_location)
            remaining: deque[bwapi.TilePosition] = deque()
            for location in self._cannon_locations:
                cannon = units.my_building_at(location)
                if cannon is not None:
                    self._cannons.append(cannon)
                else:
                    remaining.append(location)
            self._cannon_locations = remaining

    def get_squad(self) -> DefendBaseSquad:
        return self._squad

    def update(self) -> None:
        squad = self._squad
        base = self.base

        squad.enemy_units = set(units.enemy_at_base(base))

        # Clear dead static defense buildings
        if self._pylon is not None and not self._pylon.exists():
            self._pylon = None
        for cannon in list(self._cannons):
            if not cannon.exists():
                self._cannon_locations.appendleft(cannon.get_tile_position())
                self._cannons.remove(cannon)

        # Check for static defense buildings that have started
        if self._pylon is None:
            pending_building = builder.pending_here(self._pylon_location)
            if pending_building is not None:
                self._pylon = pending_building.unit
        remaining: deque[bwapi.TilePosition] = deque()
        for location in self._cannon_locations:
            pending_building = builder.pending_here(location)
            if pending_building is not None and pending_building.unit is not None:
                self._cannons.append(pending_building.unit)
            else:
                remaining.append(location)
        self._cannon_locations = remaining

        # Update detection - release observers when no longer needed, request observers when needed
        detectors = squad.get_detectors()
        if not squad.needs_detection() and detectors:
            self.status.removed_units.extend(detectors)
        elif squad.needs_detection() and not detectors:
            self.status.unit_requirements.append(
                PlayUnitRequirement(1, UnitTypes.Protoss_Observer, squad.get_target_position()))

        # Release any units in the squad if they are no longer required
        if self.enemy_value == 0 or (squad.needs_detection() and not detectors):
            self.status.removed_units = squad.get_units()
            return

        # Don't add ground units to defend an island base
        if base.island:
            return

        # Otherwise reserve enough units to adequately defend the base
        our_value = sum(combat_sim.unit_value(unit) for unit in squad.get_units())

        requested_units = 0
        zealot_value = combat_sim.unit_value(UnitTypes.Protoss_Zealot)
        while our_value < (self.enemy_value * 6) // 5:
            requested_units += 1
            our_value += zealot_value

        # Handle early-game defense of the main or natural a bit differently
        if (base is game_map.get_my_main() or base is game_map.get_my_natural()) and common.current_frame < 10000:
            # Take all units that are very close to the base
            self.status.unit_requirements.append(PlayUnitRequirement(
                requested_units, UnitTypes.Protoss_Zealot, base.get_position(), allow_from_vanguard_cluster=True,
                grid_node_predicate=_close_grid_node))
            self.status.unit_requirements.append(PlayUnitRequirement(
                requested_units, UnitTypes.Protoss_Dragoon, base.get_position(), allow_from_vanguard_cluster=True,
                grid_node_predicate=_close_grid_node))

            # Take zealots not in the vanguard cluster if we still need more units
            if requested_units > 0:
                self.status.unit_requirements.append(PlayUnitRequirement(
                    requested_units, UnitTypes.Protoss_Zealot, base.get_position(),
                    allow_from_vanguard_cluster=False))

            return

        # TODO: Request zealot or dragoon when we have that capability
        if requested_units > 0:
            import stardust.strategist.strategist as strategist

            # If we are under pressure, don't defend this base
            pressure = strategist.pressure()
            if pressure > 0.6:
                return

            # Only reserve units that have a safe path to the base
            self.status.unit_requirements.append(PlayUnitRequirement(
                requested_units, UnitTypes.Protoss_Dragoon, base.get_position(),
                allow_from_vanguard_cluster=pressure < 0.4, grid_node_predicate=_safe_grid_node))

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        base = self.base
        pylon_build_time = unit_util.build_time(UnitTypes.Protoss_Pylon)

        # Always ensure the pylon is built
        if (self._pylon is None and self._pylon_location.isValid()
                and builder.pending_here(self._pylon_location) is None):
            build_location = BuildLocation(Location(self._pylon_location),
                                           building_placement.builder_frames(base.get_position(), self._pylon_location,
                                                                             UnitTypes.Protoss_Pylon),
                                           0, 0)
            add_goal(prioritized_production_goals, PRIORITY_MAINARMY,
                     UnitProductionGoal.at(self.label, UnitTypes.Protoss_Pylon, build_location))

        # Build cannons if necessary and possible
        if self._cannon_locations:
            needed_cannons = self._desired_cannons() - len(self._cannons)
            i = 0
            while i < needed_cannons and i < len(self._cannon_locations):
                # Determine the "normal" and "low" priority levels
                # By default we use "main army" and "lowest", but bump them up if:
                # - it is the main or natural in the early game
                # - the enemy has a lot of mutalisks
                normal_priority = PRIORITY_MAINARMY
                low_priority = PRIORITY_LOWEST
                muta_threat = units.count_enemy(UnitTypes.Zerg_Mutalisk) > 4
                if ((base is game_map.get_my_main() or base is game_map.get_my_natural())
                        and common.current_frame < 12000):
                    if muta_threat:
                        normal_priority = PRIORITY_BASEDEFENSE
                        low_priority = PRIORITY_MAINARMYBASEPRODUCTION
                    else:
                        normal_priority = PRIORITY_MAINARMYBASEPRODUCTION
                        low_priority = PRIORITY_NORMAL
                elif muta_threat:
                    normal_priority = PRIORITY_MAINARMYBASEPRODUCTION
                    low_priority = PRIORITY_NORMAL

                # Use the low priority for the last cannon until the others are completed
                priority = normal_priority
                if i == needed_cannons - 1:
                    if i > 0:
                        priority = low_priority
                    elif any(not cannon.completed for cannon in self._cannons):
                        priority = low_priority

                location = self._cannon_locations[i]
                if builder.pending_here(location) is None:
                    frames_until_powered = 0
                    if self._pylon is None or not self._pylon.completed:
                        frames_until_powered = builder.frames_until_completed(self._pylon_location,
                                                                              pylon_build_time + 240)

                    build_location = BuildLocation(
                        Location(location),
                        building_placement.builder_frames(base.get_position(), location,
                                                          UnitTypes.Protoss_Photon_Cannon),
                        frames_until_powered, 0)
                    add_goal(prioritized_production_goals, priority,
                             UnitProductionGoal.at(self.label, UnitTypes.Protoss_Photon_Cannon, build_location))
                i += 1

        # Build an observer if we need one
        for unit_requirement in self.status.unit_requirements:
            if unit_requirement.type != UnitTypes.Protoss_Observer:
                continue
            if unit_requirement.count < 1:
                continue

            add_goal(prioritized_production_goals, PRIORITY_NORMAL,
                     UnitProductionGoal(self.label, unit_requirement.type, unit_requirement.count, 1))

    def _desired_cannons(self) -> int:
        # Desire no cannons if the pylon is not yet complete
        if self._pylon is None or not self._pylon.completed:
            return 0

        base = self.base
        needed_cannons = DefendBase.enemy_air_threat_cannons(base)

        # At expansions we always get cannons if the enemy is not contained
        my_main = game_map.get_my_main()
        my_natural = game_map.get_my_natural()
        if base is not my_main and base is not my_natural:
            needed_cannons = min(2, needed_cannons)

        # Always get one cannon outside our main if the enemy has at least one DT, they can quickly sneak in and kill
        # our workers
        if (base is not my_main
                and (base is not my_natural or game_map.map_specific_override().has_backdoor_natural())
                and units.count_enemy(UnitTypes.Protoss_Dark_Templar) > 0):
            needed_cannons = min(1, needed_cannons)

        return needed_cannons

    @staticmethod
    def enemy_air_threat_cannons(base_to_defend: Base) -> int:
        import stardust.opponent as opponent

        # Count enemy air units we want to defend against
        enemy_air_units = (units.count_enemy(UnitTypes.Zerg_Mutalisk)
                           + units.count_enemy(UnitTypes.Terran_Wraith)
                           + units.count_enemy(UnitTypes.Protoss_Scout))

        # Could the enemy have air units?

        # First check if we have observed anything that indicates an air threat or possible air threat
        enemy_air_threat = enemy_air_units > 0 or any(units.has_enemy_built(unit_type) for unit_type in (
            UnitTypes.Zerg_Spire, UnitTypes.Zerg_Mutalisk, UnitTypes.Zerg_Scourge, UnitTypes.Zerg_Greater_Spire,
            UnitTypes.Zerg_Guardian, UnitTypes.Zerg_Devourer,

            # Starport and anything with starport as a prerequisite
            UnitTypes.Terran_Starport, UnitTypes.Terran_Dropship, UnitTypes.Terran_Wraith, UnitTypes.Terran_Valkyrie,
            UnitTypes.Terran_Science_Facility, UnitTypes.Terran_Science_Vessel, UnitTypes.Terran_Control_Tower,
            UnitTypes.Terran_Physics_Lab, UnitTypes.Terran_Battlecruiser, UnitTypes.Terran_Covert_Ops,
            UnitTypes.Terran_Ghost, UnitTypes.Terran_Nuclear_Silo, UnitTypes.Terran_Nuclear_Missile,

            # Stargate and anything with stargate as a prerequisite
            UnitTypes.Protoss_Stargate, UnitTypes.Protoss_Corsair, UnitTypes.Protoss_Scout,
            UnitTypes.Protoss_Fleet_Beacon, UnitTypes.Protoss_Carrier, UnitTypes.Protoss_Arbiter_Tribunal,
            UnitTypes.Protoss_Arbiter))

        enemy_drop_threat = (players.upgrade_level(bwapi.Broodwar.enemy(), UpgradeTypes.Ventral_Sacs) > 0
                             # Terran is handled by air threat
                             # Robo and anything with robo as a prerequisite
                             or any(units.has_enemy_built(unit_type) for unit_type in (
                                 UnitTypes.Protoss_Robotics_Facility, UnitTypes.Protoss_Shuttle,
                                 UnitTypes.Protoss_Robotics_Support_Bay, UnitTypes.Protoss_Reaver,
                                 UnitTypes.Protoss_Observatory, UnitTypes.Protoss_Observer)))

        # Handle island expansions now
        if base_to_defend.island:
            if enemy_air_units > 0:
                return 2
            if enemy_air_threat or enemy_drop_threat:
                return 1
            return 0

        cannon_build_time = unit_util.build_time(UnitTypes.Protoss_Photon_Cannon)
        if units.count_completed(UnitTypes.Protoss_Forge) < 1:
            cannon_build_time += unit_util.build_time(UnitTypes.Protoss_Forge)

        # Next, if the enemy is Zerg, guard against mutas we haven't scouted
        # TODO: Do economic modelling to determine if the enemy could have mutas
        enemy_main = game_map.get_enemy_starting_main()
        if not enemy_air_threat and bwapi.Broodwar.enemy().getRace() == Races.Zerg and enemy_main is not None:
            # We use our opponent model to determine the worst-case completion frame, unless we have never lost a game
            # against this opponent, in which case we assume the worst
            expected_mutalisk_completion_frame = 8500
            if opponent.win_loss_ratio(0.0, 200) < 0.99:
                expected_mutalisk_completion_frame = min(
                    opponent.min_value_in_previous_games("firstMutaliskCompleted", 8500, 15, 10), 20000)

            flight_time = path_finding.expected_travel_time(base_to_defend.get_position(), enemy_main.get_position(),
                                                            UnitTypes.Zerg_Mutalisk) - 50

            if common.current_frame > expected_mutalisk_completion_frame + flight_time - cannon_build_time:
                enemy_air_threat = True

        # Main and natural are special cases, we only get cannons there to defend against air threats or sneak attacks
        if base_to_defend is game_map.get_my_main() or base_to_defend is game_map.get_my_natural():
            if enemy_air_units > 6:
                return max(1, 4 - units.count_all(UnitTypes.Protoss_Corsair) // 2)
            if enemy_air_threat:
                return max(1, 3 - units.count_all(UnitTypes.Protoss_Corsair))
            if enemy_drop_threat and common.current_frame > 8000:
                return 1
            return 0

        if enemy_air_units > 0:
            return 2
        if enemy_air_threat or enemy_drop_threat:
            return 1
        return 0

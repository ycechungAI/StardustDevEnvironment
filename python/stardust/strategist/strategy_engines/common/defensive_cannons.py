"""Port of Strategist/StrategyEngines/Common/DefensiveCannons.cpp: ordering defensive cannons at our choke and bases."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import TilePosition, TilePositions, UnitType, UnitTypes
from stardust.builder import builder, building_placement
from stardust.builder.block import Location
from stardust.builder.building_placement import BuildLocation
from stardust.map import game_map
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_EMERGENCY, PRIORITY_NORMAL, ProductionGoals, add_goal
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.map.base import Base


def build_defensive_cannons(prioritized_production_goals: ProductionGoals, at_choke: bool = True,
                            frame_needed: int = 0, at_bases: int = 0) -> None:
    if frame_needed < 0:
        return

    pylon_build_time = unit_util.build_time(UnitTypes.Protoss_Pylon)

    def build_cannon_at(pylon_tile: TilePosition, cannon_tile: TilePosition, base: Base) -> None:
        if not pylon_tile.isValid():
            return
        if not cannon_tile.isValid():
            return

        def build_at_tile(tile: TilePosition, unit_type: UnitType, frame: int) -> None:
            if units.my_building_at(tile) is not None:
                return
            if builder.is_pending_here(tile):
                return

            frames_until_powered = 0
            if unit_type != UnitTypes.Protoss_Pylon:
                frames_until_powered = builder.frames_until_completed(pylon_tile, pylon_build_time + 240)

            build_location = BuildLocation(Location(tile),
                                           building_placement.builder_frames(base.get_position(), tile, unit_type),
                                           frames_until_powered, 0)
            priority = (PRIORITY_EMERGENCY if (units.count_enemy(UnitTypes.Protoss_Dark_Templar) > 0
                                               or units.count_enemy(UnitTypes.Zerg_Lurker) > 0
                                               or units.count_enemy(UnitTypes.Zerg_Lurker_Egg) > 0)
                        else PRIORITY_NORMAL)
            add_goal(prioritized_production_goals, priority,
                     UnitProductionGoal.at("SE-defcan", unit_type, build_location, frame=frame))

        pylon = units.my_building_at(pylon_tile)
        if pylon is None:
            build_at_tile(pylon_tile, UnitTypes.Protoss_Pylon, max(0, frame_needed - pylon_build_time))

        start_frame = frame_needed
        if pylon is not None and not pylon.completed:
            start_frame = max(start_frame, pylon.completion_frame)

        build_at_tile(cannon_tile, UnitTypes.Protoss_Photon_Cannon, start_frame)

    # Check if we have a cannon at our choke
    pylon_location, cannon_location = building_placement.main_choke_cannon_locations()
    if pylon_location != TilePositions.Invalid:
        choke_cannon = units.my_building_at(cannon_location)

        # Build it if requested
        if (at_choke and choke_cannon is None
                and not builder.is_in_enemy_static_threat_range(cannon_location, UnitTypes.Protoss_Photon_Cannon)):
            my_main = game_map.get_my_main()
            assert my_main is not None
            build_cannon_at(pylon_location, cannon_location, my_main)
    elif at_choke:
        # If we ask for a cannon at the choke, but don't have a location for it, build one at our bases instead
        at_bases = max(at_bases, 1)

    if at_bases > 0:
        def build_at_base(base: Base | None, count: int = 1) -> None:
            if base is None or base.owner != bwapi.Broodwar.self():
                return
            if base.resource_depot is None or not base.resource_depot.completed:
                return

            base_static_defense_locations = building_placement.base_static_defense_locations(base)
            if base_static_defense_locations.is_valid():
                for location in base_static_defense_locations.worker_defense_cannons:
                    if count < 1:
                        break
                    count -= 1

                    if units.my_building_at(location) is not None:
                        continue

                    build_cannon_at(base_static_defense_locations.power_pylon, location, base)

        build_at_base(game_map.get_my_main(), at_bases)
        if not game_map.map_specific_override().has_backdoor_natural():
            # Always build two at natural
            build_at_base(game_map.get_my_natural(), 2)


def has_cannon_at_wall() -> bool:
    """Whether we have a wall with at least one powered cannon."""
    if not building_placement.has_forge_gateway_wall():
        return False

    wall = building_placement.get_forge_gateway_wall()

    def completed_unit(tile: TilePosition) -> bool:
        unit = units.my_building_at(tile)
        return unit is not None and unit.completed

    if not completed_unit(wall.pylon):
        return False

    return any(completed_unit(cannon_tile) for cannon_tile in wall.cannons)

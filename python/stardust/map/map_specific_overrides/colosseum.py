"""Port of Map/MapSpecificOverrides/Colosseum.{h,cpp}."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwem
from bwapi import Position, TilePosition, WalkPosition
from stardust import config
from stardust.builder.forge_gateway_wall import ForgeGatewayWall
from stardust.instrumentation import log
from stardust.map.map_specific_override import MapSpecificOverride

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.map.starting_location import StartingLocation
    from stardust.units.resource import Resource


class Colosseum(MapSpecificOverride):
    def modify_bases(self, bases: list[Base]) -> None:
        from stardust.map.base import Base
        from stardust.units import units

        # BWEM misses the natural for the top-left base when run in openbw

        # Hop out if we have the base
        for base in bases:
            if base.get_tile_position() == TilePosition(33, 17):
                return

        area = bwem.Instance().GetNearestArea(TilePosition(33, 17))
        assert area is not None

        mineral_patches: list[Resource] = []
        geysers: list[Resource] = []

        def add_resource(tile: TilePosition, resources: list[Resource]) -> None:
            resource = units.resource_at(tile)
            if resource is not None:
                resources.append(resource)
            elif config.LOGGING_ENABLED:
                log.get(f"ERROR: Cannot find our resource unit for resource @ {tile}")

        for tile in (TilePosition(40, 13), TilePosition(41, 14), TilePosition(41, 15), TilePosition(41, 17),
                     TilePosition(40, 18), TilePosition(41, 19)):
            add_resource(tile, mineral_patches)
        add_resource(TilePosition(35, 12), geysers)

        bases.append(Base(TilePosition(33, 17), area, mineral_patches, geysers))

    def has_backdoor_natural(self) -> bool:
        return True

    def modify_starting_location(self, starting_location: StartingLocation) -> StartingLocation:
        from stardust.map import game_map

        main_tile = starting_location.main.get_tile_position()
        if main_tile == TilePosition(8, 12):
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(91, 103)))
            starting_location.natural = game_map.base_near(Position(TilePosition(33, 17)))
        elif main_tile == TilePosition(116, 12):
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(421, 112)))
            starting_location.natural = game_map.base_near(Position(TilePosition(107, 38)))
        elif main_tile == TilePosition(8, 113):
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(100, 392)))
            starting_location.natural = game_map.base_near(Position(TilePosition(13, 85)))
        elif main_tile == TilePosition(116, 114):
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(387, 395)))
            starting_location.natural = game_map.base_near(Position(TilePosition(89, 107)))
        return starting_location

    def get_wall(self, start_tile: TilePosition) -> ForgeGatewayWall | None:
        # Colosseum has neutral creep that blocks where we would normally want to build our walls
        # This is not made available from BWAPI without vision on the tiles, so we are just hard-coding the walls
        if start_tile == TilePosition(8, 12):
            result = ForgeGatewayWall()

            result.pylon = TilePosition(15, 23)
            result.forge = TilePosition(17, 26)
            result.gateway = TilePosition(17, 23)
            result.cannons.append(TilePosition(15, 25))
            result.cannons.append(TilePosition(15, 27))
            result.cannons.append(TilePosition(17, 21))
            result.cannons.append(TilePosition(15, 21))
            result.cannons.append(TilePosition(19, 21))
            result.cannons.append(TilePosition(17, 19))

            result.gap_end1 = Position(TilePosition(20, 23)) + Position(16, 16)
            result.gap_end2 = Position(TilePosition(22, 22)) + Position(16, 16)

            return result
        if start_tile == TilePosition(116, 12):
            result = ForgeGatewayWall()

            result.pylon = TilePosition(113, 27)
            result.forge = TilePosition(107, 28)
            result.gateway = TilePosition(107, 25)
            result.cannons.append(TilePosition(110, 28))
            result.cannons.append(TilePosition(111, 26))
            result.cannons.append(TilePosition(111, 24))
            result.cannons.append(TilePosition(109, 23))
            result.cannons.append(TilePosition(113, 25))
            result.cannons.append(TilePosition(113, 23))

            result.gap_end1 = Position(TilePosition(107, 25)) + Position(16, 16)
            result.gap_end2 = Position(WalkPosition(423, 98)) + Position(4, 4)

            return result
        if start_tile == TilePosition(8, 113):
            result = ForgeGatewayWall()

            result.pylon = TilePosition(16, 103)
            result.forge = TilePosition(20, 102)
            result.gateway = TilePosition(18, 99)
            result.cannons.append(TilePosition(18, 102))
            result.cannons.append(TilePosition(16, 101))
            result.cannons.append(TilePosition(18, 104))
            result.cannons.append(TilePosition(16, 99))
            result.cannons.append(TilePosition(14, 102))
            result.cannons.append(TilePosition(14, 100))

            result.gap_end1 = Position(TilePosition(18, 99)) + Position(16, 16)
            result.gap_end2 = Position(TilePosition(18, 97)) + Position(16, 16)

            return result
        if start_tile == TilePosition(116, 114):
            result = ForgeGatewayWall()

            result.pylon = TilePosition(106, 102)
            result.forge = TilePosition(101, 102)
            result.gateway = TilePosition(101, 99)
            result.cannons.append(TilePosition(104, 102))
            result.cannons.append(TilePosition(105, 100))
            result.cannons.append(TilePosition(107, 100))
            result.cannons.append(TilePosition(103, 104))
            result.cannons.append(TilePosition(105, 104))
            result.cannons.append(TilePosition(108, 102))

            result.gap_end1 = Position(TilePosition(104, 99)) + Position(16, 16)
            result.gap_end2 = Position(TilePosition(105, 97)) + Position(16, 16)

            return result

        return ForgeGatewayWall()

    def natural_for_wall_placement(self, main: Base) -> Base | None:
        # The main is the closest base to the wall
        return main

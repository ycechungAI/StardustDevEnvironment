"""Port of Map/MapSpecificOverrides/Arcadia.{h,cpp}."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwem
from bwapi import Position, TilePosition, WalkPosition
from stardust import config
from stardust.instrumentation import log
from stardust.map.map_specific_override import MapSpecificOverride

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.map.starting_location import StartingLocation
    from stardust.units.resource import Resource


class Arcadia(MapSpecificOverride):
    def modify_bases(self, bases: list[Base]) -> None:
        from stardust.map.base import Base
        from stardust.units import units

        # Top-right min-only

        # Hop out if we have the base
        for base in bases:
            if base.get_tile_position() == TilePosition(113, 40):
                return

        area = bwem.Instance().GetNearestArea(TilePosition(113, 40))
        assert area is not None

        mineral_patches: list[Resource] = []

        def add_resource(tile: TilePosition, resources: list[Resource]) -> None:
            resource = units.resource_at(tile)
            if resource is not None:
                resources.append(resource)
            elif config.LOGGING_ENABLED:
                log.get(f"ERROR: Cannot find our resource unit for resource @ {tile}")

        for tile in (TilePosition(119, 36), TilePosition(120, 37), TilePosition(120, 39), TilePosition(121, 40),
                     TilePosition(121, 42), TilePosition(120, 43)):
            add_resource(tile, mineral_patches)

        bases.append(Base(TilePosition(113, 40), area, mineral_patches, []))

    def modify_starting_location(self, starting_location: StartingLocation) -> StartingLocation:
        from stardust.map import game_map

        main_tile = starting_location.main.get_tile_position()
        if main_tile == TilePosition(8, 6):
            starting_location.natural_choke = game_map.choke_near(Position(WalkPosition(91, 187)))
        elif main_tile == TilePosition(116, 6):
            starting_location.natural_choke = game_map.choke_near(Position(WalkPosition(428, 173)))
        elif main_tile == TilePosition(8, 117):
            starting_location.natural_choke = game_map.choke_near(Position(WalkPosition(104, 317)))
        elif main_tile == TilePosition(116, 117):
            starting_location.natural_choke = game_map.choke_near(Position(WalkPosition(412, 324)))
        return starting_location

    def natural_for_wall_placement(self, main: Base) -> Base | None:
        from stardust.map import game_map

        main_tile = main.get_tile_position()
        if main_tile == TilePosition(8, 6):
            return game_map.base_near(Position(TilePosition(10, 37)))
        if main_tile == TilePosition(116, 6):
            return game_map.base_near(Position(TilePosition(113, 39)))
        if main_tile == TilePosition(8, 117):
            return game_map.base_near(Position(TilePosition(10, 82)))
        if main_tile == TilePosition(116, 117):
            return game_map.base_near(Position(TilePosition(113, 81)))

        return None

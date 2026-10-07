"""Port of Map/MapSpecificOverrides/Katrina.{h,cpp}."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import Position, TilePosition, WalkPosition
from stardust.builder.forge_gateway_wall import ForgeGatewayWall
from stardust.map.map_specific_override import MapSpecificOverride

if TYPE_CHECKING:
    from stardust.map.starting_location import StartingLocation


class Katrina(MapSpecificOverride):
    def has_backdoor_natural(self) -> bool:
        return True

    def modify_starting_location(self, starting_location: StartingLocation) -> StartingLocation:
        from stardust.map import game_map

        main_tile = starting_location.main.get_tile_position()
        if main_tile == TilePosition(56, 5):
            starting_location.natural = game_map.base_near(Position(TilePosition(8, 7)))
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(255, 39)))
        elif main_tile == TilePosition(118, 49):
            starting_location.natural = game_map.base_near(Position(TilePosition(116, 7)))
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(473, 238)))
        elif main_tile == TilePosition(6, 72):
            starting_location.natural = game_map.base_near(Position(TilePosition(8, 117)))
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(37, 261)))
        elif main_tile == TilePosition(72, 119):
            starting_location.natural = game_map.base_near(Position(TilePosition(114, 116)))
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(254, 465)))
        return starting_location

    def get_wall(self, start_tile: TilePosition) -> ForgeGatewayWall | None:
        # We could wall next to the main, but it currently overlaps the main start block.
        # It is not considered worth it to pursue right now.
        return ForgeGatewayWall()

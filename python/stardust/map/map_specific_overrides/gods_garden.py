"""Port of Map/MapSpecificOverrides/GodsGarden.{h,cpp}."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import Position, TilePosition, WalkPosition
from stardust.builder.forge_gateway_wall import ForgeGatewayWall
from stardust.map.map_specific_override import MapSpecificOverride

if TYPE_CHECKING:
    from stardust.map.starting_location import StartingLocation


class GodsGarden(MapSpecificOverride):
    def has_backdoor_natural(self) -> bool:
        return True

    def modify_starting_location(self, starting_location: StartingLocation) -> StartingLocation:
        from stardust.map import game_map

        main_tile = starting_location.main.get_tile_position()
        if main_tile == TilePosition(7, 6):
            starting_location.natural = game_map.base_near(Position(TilePosition(43, 8)))
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(50, 99)))
        elif main_tile == TilePosition(115, 24):
            starting_location.natural = game_map.base_near(Position(TilePosition(115, 50)))
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(402, 40)))
        elif main_tile == TilePosition(9, 108):
            starting_location.natural = game_map.base_near(Position(TilePosition(8, 77)))
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(108, 467)))
        elif main_tile == TilePosition(116, 117):
            starting_location.natural = game_map.base_near(Position(TilePosition(85, 117)))
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(459, 386)))
        return starting_location

    def get_wall(self, start_tile: TilePosition) -> ForgeGatewayWall | None:
        # Forward natural choke is too wide
        return ForgeGatewayWall()

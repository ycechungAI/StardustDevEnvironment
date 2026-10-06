"""Port of Map/MapSpecificOverrides/CrossingField.{h,cpp}."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import Position, TilePosition, WalkPosition
from stardust.builder.forge_gateway_wall import ForgeGatewayWall
from stardust.map.map_specific_override import MapSpecificOverride

if TYPE_CHECKING:
    from stardust.map.starting_location import StartingLocation


class CrossingField(MapSpecificOverride):
    def has_backdoor_natural(self) -> bool:
        return True

    def modify_starting_location(self, starting_location: StartingLocation) -> StartingLocation:
        from stardust.map import game_map

        if starting_location.main.get_tile_position() == TilePosition(7, 27):
            starting_location.natural = game_map.base_near(Position(TilePosition(5, 6)))
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(28, 195)))
        elif starting_location.main.get_tile_position() == TilePosition(117, 66):
            starting_location.natural = game_map.base_near(Position(TilePosition(119, 87)))
            starting_location.main_choke = starting_location.natural_choke = game_map.choke_near(
                Position(WalkPosition(484, 186)))
        return starting_location

    def get_wall(self, start_tile: TilePosition) -> ForgeGatewayWall | None:
        # Forward natural choke is too wide
        return ForgeGatewayWall()

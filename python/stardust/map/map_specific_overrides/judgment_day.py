"""Port of Map/MapSpecificOverrides/JudgmentDay.{h,cpp}."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import Position, TilePosition, WalkPosition
from stardust.map.map_specific_override import MapSpecificOverride

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.map.starting_location import StartingLocation


class JudgmentDay(MapSpecificOverride):
    def modify_starting_location(self, starting_location: StartingLocation) -> StartingLocation:
        from stardust.map import game_map

        main_tile = starting_location.main.get_tile_position()
        if main_tile == TilePosition(7, 6):
            starting_location.natural = game_map.base_near(Position(TilePosition(9, 34)))
            starting_location.natural_choke = game_map.choke_near(Position(WalkPosition(151, 112)))
        elif main_tile == TilePosition(116, 7):
            starting_location.natural = game_map.base_near(Position(TilePosition(115, 34)))
            starting_location.natural_choke = game_map.choke_near(Position(WalkPosition(353, 111)))
        elif main_tile == TilePosition(8, 118):
            starting_location.natural = game_map.base_near(Position(TilePosition(9, 92)))
            starting_location.natural_choke = game_map.choke_near(Position(WalkPosition(157, 403)))
        elif main_tile == TilePosition(116, 118):
            starting_location.natural = game_map.base_near(Position(TilePosition(115, 92)))
            starting_location.natural_choke = game_map.choke_near(Position(WalkPosition(355, 405)))
        return starting_location

    def natural_for_wall_placement(self, main: Base) -> Base | None:
        from stardust.map import game_map

        main_tile = main.get_tile_position()
        if main_tile == TilePosition(7, 6):
            return game_map.base_near(Position(TilePosition(30, 24)))
        if main_tile == TilePosition(116, 7):
            return game_map.base_near(Position(TilePosition(94, 24)))
        if main_tile == TilePosition(8, 118):
            return game_map.base_near(Position(TilePosition(30, 102)))
        if main_tile == TilePosition(116, 118):
            return game_map.base_near(Position(TilePosition(95, 102)))

        return None

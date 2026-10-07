"""Port of Builder/Block.{h,cpp}: a block of build locations.

Concrete blocks (python/stardust/builder/blocks) are generated from Stardust's block headers by
tools/gen_blocks.py. Tile availability is a flat list indexed by x + y * map width, exactly like Stardust's vector, so
out-of-range tiles in start blocks (negative x) alias the same tiles they do in C++.
"""

from __future__ import annotations

from abc import ABC, abstractmethod

import bwapi
from bwapi import Position, Positions, TilePosition
from stardust.util import geo


class Location:
    __slots__ = ("tile", "converted", "has_exit")

    def __init__(self, tile: TilePosition, converted: bool = False, has_exit: bool = True) -> None:
        self.tile = tile
        self.converted = converted  # Whether this position is converted from a more-efficient position
        self.has_exit = has_exit  # Whether this position has an exit (only applicable to medium locations)

    def __repr__(self) -> str:
        return f"Location({self.tile}, converted={self.converted}, has_exit={self.has_exit})"


def _remove_overlapping(locations: list[Location], tile: TilePosition, size: TilePosition, width: int,
                        height: int) -> None:
    locations[:] = [location for location in locations
                    if not geo.overlaps_tiles(tile, size.x, size.y, location.tile, width, height)]


def _remove_used(locations: list[Location], width: int, height: int) -> None:
    from stardust.map import game_map

    def used(location: Location) -> bool:
        return any(not game_map.is_walkable(tile_x, tile_y)
                   for tile_x in range(location.tile.x, location.tile.x + width)
                   for tile_y in range(location.tile.y, location.tile.y + height))

    locations[:] = [location for location in locations if not used(location)]


class Block(ABC):
    def __init__(self, top_left: TilePosition, power_pylon: TilePosition) -> None:
        self.top_left = top_left
        self.power_pylon = power_pylon
        self.small: list[Location] = []
        self.medium: list[Location] = []
        self.large: list[Location] = []
        self.cannons: list[TilePosition] = []  # Only applicable to start blocks
        self._permanent_tile_reservations: list[tuple[TilePosition, TilePosition]] = []
        self.place_locations()

    @abstractmethod
    def width(self) -> int: ...

    @abstractmethod
    def height(self) -> int: ...

    def center(self) -> Position:
        if not self.top_left.isValid():
            return Positions.Invalid
        return Position(self.top_left) + Position(self.width() * 16, self.height() * 16)

    def allow_top_edge(self) -> bool:
        return True

    def allow_left_edge(self) -> bool:
        return True

    def allow_right_edge(self) -> bool:
        return True

    def allow_corner(self) -> bool:
        return True

    def tiles_reserved(self, tile: TilePosition, size: TilePosition, permanent: bool = False) -> bool:
        if not geo.overlaps_tiles(tile, size.x, size.y, self.top_left, self.width(), self.height()):
            return False

        _remove_overlapping(self.small, tile, size, 2, 2)
        _remove_overlapping(self.medium, tile, size, 3, 2)
        _remove_overlapping(self.large, tile, size, 4, 3)

        if permanent:
            self._permanent_tile_reservations.append((tile, size))

        return True

    def tiles_used(self, tile: TilePosition, size: TilePosition) -> bool:
        return self.tiles_reserved(tile, size)

    def tiles_freed(self, tile: TilePosition, size: TilePosition) -> bool:
        if not geo.overlaps_tiles(tile, size.x, size.y, self.top_left, self.width(), self.height()):
            return False

        # The freed tiles may free up some of the initial build locations, so place them again.
        # (Stardust appends the initial locations to the existing ones; duplicates are then possible, as there.)
        self.place_locations()
        self.remove_used()

        # However, we may have some permanent reservations, so re-apply them
        for permanent_tile, permanent_size in self._permanent_tile_reservations:
            self.tiles_reserved(permanent_tile, permanent_size)

        return True

    @abstractmethod
    def try_create(self, tile: TilePosition, tile_availability: list[int]) -> Block | None: ...

    def place(self, tile: TilePosition, tile_availability: list[int]) -> bool:
        game = bwapi.Broodwar
        map_width = game.mapWidth()
        map_height = game.mapHeight()
        width = self.width()
        height = self.height()
        allow_left_edge = self.allow_left_edge()
        allow_right_edge = self.allow_right_edge()
        allow_top_edge = self.allow_top_edge()
        allow_corner = self.allow_corner()

        def check_tile(tile_x: int, tile_y: int) -> bool:
            if tile_x >= map_width:
                return False
            if tile_y >= map_height:
                return False
            if tile_availability[tile_x + tile_y * map_width] > 0:
                return False
            if tile_x == 0 and not allow_left_edge:
                return False
            if tile_x == (map_width - 1) and not allow_right_edge:
                return False
            if tile_y == 0 and not allow_top_edge:
                return False
            if tile_x == 0 and tile_y == 0 and not allow_corner:
                return False
            if tile_x == (map_width - 1) and tile_y == 0 and not allow_corner:
                return False
            return True

        if not check_tile(tile.x, tile.y):
            return False
        if not check_tile(tile.x + width - 1, tile.y):
            return False
        if not check_tile(tile.x + width - 1, tile.y + height - 1):
            return False
        if not check_tile(tile.x, tile.y + height - 1):
            return False

        for tile_x in range(tile.x, tile.x + width):
            for tile_y in range(tile.y, tile.y + height):
                if not check_tile(tile_x, tile_y):
                    return False

        # Can be placed here, mark the tiles
        for tile_x in range(tile.x - 1, tile.x + width + 1):
            for tile_y in range(tile.y - 1, tile.y + height + 1):
                if tile_x < 0 or tile_y < 0 or tile_x >= map_width or tile_y >= map_height:
                    continue

                if tile_x == tile.x - 1 or tile_y == tile.y - 1 or tile_x == tile.x + width or tile_y == tile.y + height:
                    tile_availability[tile_x + tile_y * map_width] |= 8
                else:
                    tile_availability[tile_x + tile_y * map_width] |= 4

        return True

    def place_start_block(self, start_position: TilePosition, used_tiles: list[TilePosition],
                          border_tiles: list[TilePosition], tile_availability: list[int]) -> bool:
        game = bwapi.Broodwar
        map_width = game.mapWidth()
        map_height = game.mapHeight()

        # Tiles around the start position are not checked for availability
        ignore_availability: set[tuple[int, int]] = set()
        for tile_x in range(start_position.x - 2, start_position.x + 6):
            for tile_y in range(start_position.y - 2, start_position.y + 5):
                if tile_x < 0 or tile_y < 0 or tile_x >= map_width or tile_y >= map_height:
                    continue
                ignore_availability.add((tile_x, tile_y))

        def check_tile(tile: TilePosition) -> bool:
            if tile.x >= map_width:
                return False
            if tile.y >= map_height:
                return False
            if (tile.x, tile.y) not in ignore_availability:
                if tile_availability[tile.x + tile.y * map_width] > 0:
                    return False
            else:
                if tile_availability[tile.x + tile.y * map_width] == 1:
                    return False
            if tile.x == 0 and not self.allow_left_edge():
                return False
            if tile.x == (map_width - 1) and not self.allow_right_edge():
                return False
            if tile.y == 0 and not self.allow_top_edge():
                return False
            if tile.x == 0 and tile.y == 0 and not self.allow_corner():
                return False
            if tile.x == (map_width - 1) and tile.y == 0 and not self.allow_corner():
                return False
            return True

        for tile in used_tiles:
            if not check_tile(tile):
                return False

        for tile in used_tiles:
            tile_availability[tile.x + tile.y * map_width] |= 4
        for tile in border_tiles:
            tile_availability[tile.x + tile.y * map_width] |= 8

        return True

    @abstractmethod
    def place_locations(self) -> None: ...

    def remove_used(self) -> None:
        _remove_used(self.small, 2, 2)
        _remove_used(self.medium, 3, 2)
        _remove_used(self.large, 4, 3)

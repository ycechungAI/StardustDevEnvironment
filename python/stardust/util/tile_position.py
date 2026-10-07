"""Port of Util/TilePosition.h: a compact tile position (each coordinate fits in a byte) used by the mining
optimization data files. It orders and compares like Stardust's struct: by x, then y."""

from __future__ import annotations

from typing import NamedTuple

from bwapi import Position
from bwapi import TilePosition as BWAPITilePosition


class TilePosition(NamedTuple):
    x: int
    y: int

    def to_bwapi(self) -> BWAPITilePosition:
        return BWAPITilePosition(self.x, self.y)

    def to_position(self) -> Position:
        """The conversion to BWAPI::Position: the center of a mineral field at this tile."""
        return Position(BWAPITilePosition(self.x, self.y)) + Position(32, 16)

    def __str__(self) -> str:
        return f"({self.x},{self.y})"

    @staticmethod
    def from_bwapi(tile: BWAPITilePosition) -> TilePosition:
        if tile.x < 0 or tile.x > 255:
            raise ValueError("Failed to construct TilePosition from BWAPI::TilePosition: x out of bounds")
        if tile.y < 0 or tile.y > 255:
            raise ValueError("Failed to construct TilePosition from BWAPI::TilePosition: y out of bounds")
        return TilePosition(tile.x, tile.y)

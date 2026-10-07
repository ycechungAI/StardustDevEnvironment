"""Port of Workers/MiningOptimizationV2/DataModel/CannonPlacement.h: which cannons (by count and the last one's tile)
in a mineral line a recorded path applies to. All placements without cannons are equal."""

from __future__ import annotations

from stardust.util.tile_position import TilePosition


class CannonPlacement:
    __slots__ = ("cannon_count", "tile")

    def __init__(self, cannon_count: int = 0, tile: TilePosition = TilePosition(0, 0)) -> None:
        self.cannon_count = cannon_count
        self.tile = tile

    def _key(self) -> tuple[int, TilePosition | None]:
        return (0, None) if self.cannon_count == 0 else (self.cannon_count, self.tile)

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, CannonPlacement):
            return NotImplemented
        return self._key() == other._key()

    def __lt__(self, other: CannonPlacement) -> bool:
        if self.cannon_count == 0 or other.cannon_count == 0:
            return self.cannon_count < other.cannon_count
        return (self.cannon_count, self.tile) < (other.cannon_count, other.tile)

    def __hash__(self) -> int:
        return hash(self._key())

    def __str__(self) -> str:
        if self.cannon_count == 0:
            return "none"
        if self.cannon_count == 1:
            return f"1 cannon @ {self.tile}"
        return f"{self.cannon_count} cannons; last @ {self.tile}"

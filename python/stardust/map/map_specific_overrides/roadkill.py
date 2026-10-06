"""Port of Map/MapSpecificOverrides/Roadkill.h."""

from __future__ import annotations

from bwapi import Position, TilePosition
from stardust.map.map_specific_override import MapSpecificOverride


class Roadkill(MapSpecificOverride):
    def starting_worker_positions(self, start_position: TilePosition) -> list[Position]:
        if start_position != TilePosition(69, 6):
            return super().starting_worker_positions(start_position)

        return [Position(2318, 184), Position(2294, 184), Position(2270, 184), Position(2246, 184)]

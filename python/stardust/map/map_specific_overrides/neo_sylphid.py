"""Port of Map/MapSpecificOverrides/NeoSylphid.h."""

from __future__ import annotations

from bwapi import Position, TilePosition
from stardust.map.map_specific_override import MapSpecificOverride


class NeoSylphid(MapSpecificOverride):
    def starting_worker_positions(self, start_position: TilePosition) -> list[Position]:
        if start_position != TilePosition(62, 6):
            return super().starting_worker_positions(start_position)

        return [Position(2120, 192), Position(2090, 184), Position(2062, 184), Position(2038, 184)]

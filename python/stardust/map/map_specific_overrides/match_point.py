"""Port of Map/MapSpecificOverrides/MatchPoint.{h,cpp}."""

from __future__ import annotations

from bwapi import Position, TilePosition
from stardust.builder.forge_gateway_wall import ForgeGatewayWall
from stardust.map.map_specific_override import MapSpecificOverride


class MatchPoint(MapSpecificOverride):
    def get_wall(self, start_tile: TilePosition) -> ForgeGatewayWall | None:
        # BWEM doesn't generate a choke at the natural for Match Point, so we define the wall manually
        if start_tile.x != 100 or start_tile.y != 14:
            return None

        result = ForgeGatewayWall()

        result.pylon = TilePosition(104, 48)
        result.forge = TilePosition(100, 51)
        result.gateway = TilePosition(106, 51)
        result.cannons.append(TilePosition(100, 49))
        result.cannons.append(TilePosition(102, 49))
        result.cannons.append(TilePosition(109, 49))
        result.cannons.append(TilePosition(107, 49))
        result.cannons.append(TilePosition(98, 50))
        result.cannons.append(TilePosition(108, 47))

        result.natural_cannons.append(TilePosition(100, 44))
        result.natural_cannons.append(TilePosition(98, 46))

        result.gap_end1 = Position(TilePosition(102, 52)) + Position(16, 16)
        result.gap_end2 = Position(TilePosition(106, 52)) + Position(16, 16)

        return result

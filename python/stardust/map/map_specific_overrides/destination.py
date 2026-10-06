"""Port of Map/MapSpecificOverrides/Destination.{h,cpp}."""

from __future__ import annotations

from bwapi import TilePosition
from stardust.builder.forge_gateway_wall import ForgeGatewayWall
from stardust.map.map_specific_override import MapSpecificOverride


class Destination(MapSpecificOverride):
    def get_wall(self, start_tile: TilePosition) -> ForgeGatewayWall | None:
        # We don't have the skills to defend a non-standard wall like what would work on Destination
        return ForgeGatewayWall()

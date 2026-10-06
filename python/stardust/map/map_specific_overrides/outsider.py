"""Port of Map/MapSpecificOverrides/Outsider.{h,cpp}."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import TilePosition
from stardust.instrumentation import log
from stardust.map.map_specific_override import MapSpecificOverride

if TYPE_CHECKING:
    from stardust.map.base import Base

_ISLAND_AREA_TILES = (
    TilePosition(9, 8),
    TilePosition(29, 6),
    TilePosition(23, 119),
    TilePosition(120, 70),
    TilePosition(120, 51),
)

_BASE_TO_NEW_LOCATION = {
    (7, 116): TilePosition(7, 105),
    (35, 120): TilePosition(48, 120),
    (119, 73): TilePosition(119, 84),
    (119, 51): TilePosition(119, 40),
    (35, 5): TilePosition(48, 5),
    (7, 9): TilePosition(7, 20),
}


class Outsider(MapSpecificOverride):
    def add_island_areas(self, island_areas: set[bwem.Area]) -> None:
        """On Outsider, BWEM doesn't handle the blocking mineral fields, so the areas behind them can appear
        accessible. This hook manually marks all of the inaccessible areas as islands to work around this."""
        for area in bwem.Instance().Areas():
            tile = TilePosition(area.Top())
            for island_area_tile in _ISLAND_AREA_TILES:
                if island_area_tile.getApproxDistance(tile) < 4:
                    island_areas.add(area)
                    break

    def modify_main_base_building_placement_areas(self, areas: set[bwem.Area]) -> None:
        # On Outsider we have additional areas behind our main base that are initially blocked by mineral fields
        # We don't want to use them for building placement though
        main_area = bwem.Instance().GetArea(bwapi.Broodwar.self().getStartLocation())
        if main_area is None:
            log.get("ERROR: Start location doesn't have a BWEM area")
            return

        areas.clear()
        areas.add(main_area)

    def modify_bases(self, bases: list[Base]) -> None:
        # We currently don't have the functionality to take the bases behind the blocking mineral lines in the outside
        # ring
        # So we convert them into mineral-only bases on the accessible side
        # (As in Stardust, the base's cached center position and blocking neutrals are not updated.)
        bwem_map = bwem.Instance()
        for base in bases:
            tile = base.get_tile_position()
            new_tile = _BASE_TO_NEW_LOCATION.get((tile.x, tile.y))
            if new_tile is None:
                continue

            new_area = bwem_map.GetNearestArea(new_tile)
            assert new_area is not None
            base.island = False
            base._tile = new_tile
            base._bwem_area = new_area
            base._geysers_or_refineries.clear()
            base.update()
            base._analyze_mineral_line()

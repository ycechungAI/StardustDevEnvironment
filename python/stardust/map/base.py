"""Port of Map/Base.{h,cpp}: a base location with its resources, ownership and mineral line."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Position, TilePosition, UnitTypes
from stardust.cpp import INT_MAX
from stardust.instrumentation import log
from stardust.map.path_finding import path_finding
from stardust.util import geo

if TYPE_CHECKING:
    from stardust.units.resource import Resource
    from stardust.units.unit import Unit


def _get_blocking_neutrals(tile: TilePosition) -> list[bwapi.Unit]:
    result = []
    for unit in bwapi.Broodwar.getStaticNeutralUnits():
        if unit.getType().isMineralField():
            if geo.overlaps_tiles(tile - TilePosition(3, 3), 10, 9, unit.getInitialTilePosition(), 2, 1):
                result.append(unit)
        elif geo.overlaps_tiles(tile, 4, 3, unit.getInitialTilePosition(),
                                unit.getType().tileWidth(), unit.getType().tileHeight()):
            result.append(unit)
    return result


def _update_resources_in_switch_patch_range(mineral_patches: list[Resource]) -> None:
    for this_patch in mineral_patches:
        in_switch_range: set[Resource] = set()
        ten_distance_positions = geo.ten_distance_positions_around_patch(this_patch.center)
        for other_patch in mineral_patches:
            if this_patch is other_patch:
                continue
            # Game looks for a free patch within 8 tiles, ref: order_MoveToMinerals in bwgame.h
            if any(geo.edge_to_edge_distance(UnitTypes.Protoss_Probe, pos, UnitTypes.Resource_Mineral_Field,
                                             other_patch.center) <= 32 * 8
                   for pos in ten_distance_positions):
                in_switch_range.add(other_patch)
        this_patch.resources_in_switch_patch_range = in_switch_range


class Base:
    def __init__(self, tile: TilePosition, bwem_area: bwem.Area, mineral_patches: list[Resource],
                 geysers: list[Resource]) -> None:
        self.owner: bwapi.Player | None = None  # Who owns the base
        self.resource_depot: Unit | None = None  # The resource depot for the base, may be None
        self.owned_since = -1  # Frame the base last changed ownership
        self.last_scouted = -1  # When we have last seen this base
        self.blocked_by_enemy = False  # Do we suspect this base to be blocked by a hidden enemy unit
        self.requires_mineral_walk_from_enemy_start_locations = False  # Does the enemy need mineral walking to reach it
        self.island = True  # Whether this base is ground-connected to any main base; set to False below when it is
        self.mineral_line_center = Position(0, 0)  # Approximate center of the mineral line
        self.worker_defense_rally_patch: Resource | None = None  # Patch workers rally to when doing worker defense
        self.mineral_line_tiles: set[TilePosition] = set()  # All tiles considered to be part of the mineral line
        self.blocking_neutrals = _get_blocking_neutrals(tile)  # Neutrals to clear before building the nexus
        self.minerals = 0  # Current total count of minerals remaining
        self.gas = 0  # Current total count of gas remaining

        self._tile = tile
        self._center = Position(tile) + Position(64, 48)
        self._bwem_area = bwem_area
        self._mineral_patches = mineral_patches
        self._geysers_or_refineries = geysers

        # Call update to set the minerals and gas counts
        self.update()

        self._analyze_mineral_line()

        for start_location_tile in bwapi.Broodwar.getStartLocations():
            if path_finding.get_ground_distance(Position(tile), Position(start_location_tile),
                                                UnitTypes.Protoss_Probe) != -1:
                self.island = False
                break

        _update_resources_in_switch_patch_range(self._mineral_patches)

    @classmethod
    def from_bwem_base(cls, tile: TilePosition, bwem_base: bwem.Base) -> Base:
        from stardust.units import units

        def resources(neutrals: list[bwem.Mineral] | list[bwem.Geyser], name: str) -> list[Resource]:
            result = []
            for neutral in neutrals:
                resource = units.resource_at(neutral.TopLeft())
                if resource is not None:
                    result.append(resource)
                else:
                    log.get(f"ERROR: Cannot find our resource unit for {name} @ {neutral.TopLeft()}")
            # BWEM's order depends on the memory layout, so sort for better determinism
            result.sort(key=lambda r: r.tile)
            return result

        return cls(tile, bwem_base.GetArea(), resources(bwem_base.Minerals(), "mineral field"),
                   resources(bwem_base.Geysers(), "geyser"))

    def get_tile_position(self) -> TilePosition:
        return self._tile

    def get_position(self) -> Position:
        return self._center

    def get_area(self) -> bwem.Area:
        return self._bwem_area

    def mineral_patch_count(self) -> int:
        return len(self._mineral_patches)

    def geyser_count(self) -> int:
        return len(self._geysers_or_refineries)

    def mineral_patches(self) -> list[Resource]:
        return self._mineral_patches

    def geysers_or_refineries(self) -> list[Resource]:
        return self._geysers_or_refineries

    def is_starting_base(self) -> bool:
        return self._tile in bwapi.Broodwar.getStartLocations()

    def is_in_mineral_line(self, pos: TilePosition) -> bool:
        return pos in self.mineral_line_tiles

    def has_geyser_or_refinery_at(self, geyser_top_left: TilePosition) -> bool:
        return any(geyser.tile == geyser_top_left for geyser in self._geysers_or_refineries)

    def gas_requires_four_workers(self, geyser_top_left: TilePosition) -> bool:
        if not self.has_geyser_or_refinery_at(geyser_top_left):
            return False
        return (geyser_top_left.y - self._tile.y) > 5

    def update(self) -> None:
        # Count total minerals available and remove destroyed minerals
        self._mineral_patches[:] = [patch for patch in self._mineral_patches if not patch.destroyed]
        self.minerals = sum(patch.current_amount for patch in self._mineral_patches)

        # Count total gas available
        self.gas = sum(geyser.current_amount for geyser in self._geysers_or_refineries)

    def _analyze_mineral_line(self) -> None:
        self.mineral_line_tiles.clear()
        position = self.get_position()

        # Compute the approximate center of the mineral line
        count = self.mineral_patch_count()
        if count > 0:
            x = sum(mineral.center.x for mineral in self._mineral_patches)
            y = sum(mineral.center.y for mineral in self._mineral_patches)
            self.mineral_line_center = (position + Position(x // count, y // count) * 2) / 3
        else:
            self.mineral_line_center = position

        # Compute the tiles that are considered part of the mineral line.
        # We do this by tracing lines from each mineral patch to the center of the resource depot and adding all
        # surrounding tiles.
        depot_tile = TilePosition(position)

        def handle_tile(start: TilePosition) -> None:
            for pos in geo.find_tiles_between(start, depot_tile):
                if geo.edge_to_point_distance(UnitTypes.Protoss_Nexus, position, Position(pos) + Position(16, 16)) < 1:
                    continue
                for x in range(pos.x - 1, pos.x + 2):
                    for y in range(pos.y - 1, pos.y + 2):
                        here = TilePosition(x, y)
                        if here.isValid():
                            self.mineral_line_tiles.add(here)

        for mineral_patch in self._mineral_patches:
            handle_tile(mineral_patch.tile)
            handle_tile(mineral_patch.tile + TilePosition(1, 0))
        for geyser in self._geysers_or_refineries:
            for dy in range(2):
                for dx in range(4):
                    handle_tile(geyser.tile + TilePosition(dx, dy))

        # Compute the best mineral patch to use for rallying workers during worker defense.
        # Start with the closest patch to the mineral line center.
        closest_patch_dist = INT_MAX
        for mineral_patch in self._mineral_patches:
            dist = mineral_patch.get_distance(self.mineral_line_center)
            if dist < closest_patch_dist:
                self.worker_defense_rally_patch = mineral_patch
                closest_patch_dist = dist

        # Now try to get a neighbouring mineral patch that is further away from the depot
        rally_patch = self.worker_defense_rally_patch
        if rally_patch is not None:
            current_dist = rally_patch.get_distance_to_type(UnitTypes.Protoss_Nexus, self._center)
            for mineral_patch in self._mineral_patches:
                if mineral_patch is rally_patch or mineral_patch.get_distance(rally_patch) > 0:
                    continue
                if mineral_patch.get_distance_to_type(UnitTypes.Protoss_Nexus, self._center) > current_dist:
                    self.worker_defense_rally_patch = mineral_patch
                    break

    def __str__(self) -> str:
        return str(self._tile)

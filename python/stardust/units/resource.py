"""Port of Units/Resource.{h,cpp}: a mineral field or vespene geyser."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, UnitType, UnitTypes, WalkPosition
from stardust.util import geo

if TYPE_CHECKING:
    from stardust.units.unit import Unit


class Resource:
    __slots__ = ("id", "is_minerals", "tile", "center", "initial_amount", "current_amount", "last_seen_frame",
                 "tile_seen_last_frame", "destroyed", "refinery", "resources_in_switch_patch_range", "_bwapi_unit")

    def __init__(self, unit: bwapi.Unit) -> None:
        self.id = unit.getID()
        self.is_minerals = unit.getType().isMineralField()
        self.tile = unit.getTilePosition()
        self.center = unit.getPosition()
        self.initial_amount = unit.getResources()
        self.current_amount = unit.getResources()
        self.last_seen_frame = 0  # Frame when we last saw this resource
        self.tile_seen_last_frame = False  # For tracking when mineral fields are mined out and removed
        self.destroyed = False  # For mineral fields, when they are mined out and removed
        self.refinery: Unit | None = None  # For geysers, the refinery unit a player has built on it
        # For mineral fields, the other mineral fields that workers might try to switch to
        self.resources_in_switch_patch_range: set[Resource] = set()
        self._bwapi_unit: bwapi.Unit | None = unit

    def _type(self) -> UnitType:
        return UnitTypes.Resource_Mineral_Field if self.is_minerals else UnitTypes.Resource_Vespene_Geyser

    def has_my_completed_refinery(self) -> bool:
        if self.is_minerals or self.refinery is None:
            return False
        return self.refinery.completed and self.refinery.player == bwapi.Broodwar.self()

    def get_bwapi_unit_if_visible(self) -> bwapi.Unit | None:
        if self.refinery is not None and self.refinery.bwapi_unit is not None and self.refinery.bwapi_unit.isVisible():
            return self.refinery.bwapi_unit

        if self._bwapi_unit is not None and self._bwapi_unit.exists() and self._bwapi_unit.isVisible():
            return self._bwapi_unit

        self._bwapi_unit = None
        for unit in bwapi.Broodwar.getNeutralUnits():
            if unit.getTilePosition() != self.tile:
                continue
            if self.is_minerals and not unit.getType().isMineralField():
                continue
            if not self.is_minerals and unit.getType() != UnitTypes.Resource_Vespene_Geyser:
                continue
            if not unit.isVisible():
                continue
            self._bwapi_unit = unit
            break

        return self._bwapi_unit

    def get_distance(self, other: Unit | Position | Resource) -> int:
        """Edge-to-edge distance to a unit (at its last position) or resource, or edge-to-point to a position."""
        if isinstance(other, Position):
            return geo.edge_to_point_distance(self._type(), self.center, other)
        if isinstance(other, Resource):
            return geo.edge_to_edge_distance(self._type(), self.center, other._type(), other.center)
        return geo.edge_to_edge_distance(self._type(), self.center, other.type, other.last_position)

    def get_distance_to_type(self, other_type: UnitType, other_center: Position) -> int:
        return geo.edge_to_edge_distance(self._type(), self.center, other_type, other_center)

    def __str__(self) -> str:
        text = f"{'Minerals' if self.is_minerals else 'Gas'}:{self.id}@{WalkPosition(self.center)}"
        if self.destroyed:
            return text + " (destroyed)"
        if self.refinery is None or self.refinery.player == bwapi.Broodwar.self():
            text += f" {self.current_amount}/{self.initial_amount}"
        if self.refinery is not None:
            text += f" with refinery {self.refinery}"
        return text

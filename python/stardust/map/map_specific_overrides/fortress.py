"""Port of Map/MapSpecificOverrides/Fortress.{h,cpp}."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Position, WalkPosition
from stardust.cpp import INT_MAX
from stardust.map.map_specific_override import MapSpecificOverride

if TYPE_CHECKING:
    from stardust.map.choke import Choke


class Fortress(MapSpecificOverride):
    def has_mineral_walking(self) -> bool:
        return True

    def initialize_chokes(self, chokes: dict[bwem.ChokePoint, Choke]) -> None:
        bwem_map = bwem.Instance()
        for choke, choke_data in chokes.items():
            # On Fortress the mineral walking chokes are all considered blocked by BWEM
            if not choke.Blocked():
                continue

            choke_data.requires_mineral_walk = True

            # Find the two closest mineral patches to the choke
            choke_center = Position(choke.Center())
            closest_mineral_patch: bwapi.Unit | None = None
            second_closest_mineral_patch: bwapi.Unit | None = None
            closest_mineral_patch_dist = INT_MAX
            second_closest_mineral_patch_dist = INT_MAX
            for static_neutral in bwapi.Broodwar.getStaticNeutralUnits():
                if static_neutral.getType().isMineralField():
                    dist = static_neutral.getDistance(choke_center)
                    if dist <= closest_mineral_patch_dist:
                        second_closest_mineral_patch_dist = closest_mineral_patch_dist
                        closest_mineral_patch_dist = dist
                        second_closest_mineral_patch = closest_mineral_patch
                        closest_mineral_patch = static_neutral
                    elif dist < second_closest_mineral_patch_dist:
                        second_closest_mineral_patch_dist = dist
                        second_closest_mineral_patch = static_neutral
            assert closest_mineral_patch is not None and second_closest_mineral_patch is not None

            # Each entrance to a mineral walking base has two doors with a mineral patch behind each
            # So the choke closest to the base will have a mineral patch on both sides we can use
            # The other choke has a mineral patch on the way in, but not on the way out, so one will be null
            # We will use a random visible mineral patch on the map to handle getting out
            closest_area = bwem_map.GetNearestArea(
                WalkPosition(closest_mineral_patch.getTilePosition()) + WalkPosition(4, 2))
            second_closest_area = bwem_map.GetNearestArea(
                WalkPosition(second_closest_mineral_patch.getTilePosition()) + WalkPosition(4, 2))

            first_area, second_area = choke.GetAreas()
            if closest_area == first_area:
                choke_data.first_area_mineral_patch = closest_mineral_patch
            if closest_area == second_area:
                choke_data.second_area_mineral_patch = closest_mineral_patch
            if second_closest_area == first_area:
                choke_data.first_area_mineral_patch = second_closest_mineral_patch
            if second_closest_area == second_area:
                choke_data.second_area_mineral_patch = second_closest_mineral_patch

            # We use the door as the starting point regardless of which side is which
            blocking_neutral = choke.BlockingNeutral()
            assert blocking_neutral is not None
            door_position = blocking_neutral.Unit().getInitialPosition()
            choke_data.first_area_start_position = door_position
            choke_data.second_area_start_position = door_position

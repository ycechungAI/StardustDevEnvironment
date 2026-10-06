"""Port of Units/EnemyBunker.{h,cpp}: an enemy bunker, tracking how many marines are probably loaded and whether one
of our units is in its range."""

from __future__ import annotations

import bwapi
from stardust import common
from stardust.instrumentation import cherryvis
from stardust.units.unit import Unit


class EnemyBunker(Unit):
    __slots__ = ("loaded_marines", "my_unit_in_range", "frame_my_unit_in_range_last_changed")

    def __init__(self, unit: bwapi.Unit) -> None:
        super().__init__(unit)
        self.loaded_marines = 0  # How many marines we think are loaded into this bunker
        self.my_unit_in_range = False  # Whether one of our units is in the attack range of the bunker
        self.frame_my_unit_in_range_last_changed = -1

    def update(self, unit: bwapi.Unit | None) -> None:
        super().update(unit)
        self._update_my_unit_in_range()

    def update_unit_in_fog(self) -> None:
        super().update_unit_in_fog()
        self._update_my_unit_in_range()

    def _update_my_unit_in_range(self) -> None:
        from stardust.units import units

        # Look for a unit well inside the bunker's range that the bunker should be able to see.
        # We're not checking for detection on DTs or Observers since we want to be sure there is something the bunker
        # could shoot at.
        game = bwapi.Broodwar
        bunker_elevation = game.getGroundHeight(self.get_tile_position())
        current_my_unit_in_range = False
        for unit in units.all_mine():
            bwapi_unit = unit.bwapi_unit
            if bwapi_unit is None or bwapi_unit.isLoaded() or bwapi_unit.isStasised():
                continue
            if unit.type.hasPermanentCloak():
                continue
            if game.getGroundHeight(unit.get_tile_position()) > bunker_elevation:
                continue
            if self.is_in_our_weapon_range(unit, buffer=-32):
                current_my_unit_in_range = True
                break

        if current_my_unit_in_range != self.my_unit_in_range:
            self.my_unit_in_range = current_my_unit_in_range
            self.frame_my_unit_in_range_last_changed = common.current_frame
            cherryvis.log(f"{'Has' if self.my_unit_in_range else 'Does not have'} one of my units in range", self.id)

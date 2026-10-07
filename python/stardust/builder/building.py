"""Port of Builder/Building.{h,cpp}: a building that is either under construction or about to be constructed.

We always decide on the position and builder unit before storing the building. If something happens to invalidate
them before the building is started, we create a new building.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import Position, TilePosition, UnitType, UnitTypes
from stardust import common
from stardust.map import no_go_areas
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.units.my_unit import MyUnit
    from stardust.units.my_worker import MyWorker


def _no_go_area_buffer(building: Building) -> int:
    """The required no-go area buffer around a pending building location."""
    # We give cannons a 3-tile buffer, since they are often build where we have many units
    # All other buildings get a 1-tile buffer
    if building.type == UnitTypes.Protoss_Photon_Cannon:
        return 3
    return 1


class Building:
    def __init__(self, unit_type: UnitType, tile: TilePosition, builder: MyWorker | None,
                 desired_start_frame: int) -> None:
        self.type = unit_type  # The type of the building
        self.tile = tile  # The position of the building
        self.unit: MyUnit | None = None  # The building itself
        self.builder = builder  # The unit that will build the building
        self.desired_start_frame = desired_start_frame  # The desired start frame given by the producer
        self.build_command_success_frames = 0  # Number of frames in which a successful build command was issued
        self.build_command_failure_frames = 0  # Number of frames in which a failed build command was issued
        self.no_go_area_added = False  # Whether the no-go area has been added
        self._start_frame = -1

    def construction_started(self, started_unit: MyUnit) -> None:
        self.unit = started_unit
        self._start_frame = common.current_frame

    def get_position(self) -> Position:
        return Position(self.tile) + Position(self.type.tileWidth() * 16, self.type.tileHeight() * 16)

    def is_construction_started(self) -> bool:
        return self.unit is not None and self.unit.exists()

    def expected_frames_until_started(self) -> int:
        if self.unit is not None and self.unit.exists():
            return 0

        # This can be inaccurate if this isn't the next building in the builder's queue
        # TODO: Verify this doesn't cause problems
        builder = self.builder
        assert builder is not None
        worker_frames = path_finding.expected_travel_time(builder.last_position, self.get_position(), builder.type,
                                                          PathFindingOptions.UseNearestBWEMArea)

        return max(worker_frames, self.desired_start_frame - common.current_frame)

    def expected_frames_until_completion(self) -> int:
        if self._start_frame != -1:
            return unit_util.build_time(self.type) - (common.current_frame - self._start_frame)

        return unit_util.build_time(self.type) + self.expected_frames_until_started()

    def builder_ready(self) -> bool:
        if self.is_construction_started() or self.builder is None or not self.builder.exists():
            return False

        return self.builder.get_distance(self.get_position()) < 32

    def _no_go_box(self) -> tuple[TilePosition, TilePosition]:
        buffer = _no_go_area_buffer(self)
        return (self.tile - TilePosition(buffer, buffer),
                self.type.tileSize() + TilePosition(buffer * 2, buffer * 2))

    def add_no_go_area_when_needed(self) -> None:
        if self.no_go_area_added:
            return

        # Add the no-go area when the desired start frame is within 5 seconds of now
        if self.expected_frames_until_started() > 120:
            return

        no_go_areas.add_box(no_go_areas.Type.GROUND_NAVIGATIONAL, *self._no_go_box())
        self.no_go_area_added = True

    def remove_no_go_area(self) -> None:
        if not self.no_go_area_added:
            return

        no_go_areas.remove_box(no_go_areas.Type.GROUND_NAVIGATIONAL, *self._no_go_box())
        self.no_go_area_added = False

    def __str__(self) -> str:
        return f"{self.type} @ {self.tile}"

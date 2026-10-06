"""Port of Producer/ProductionGoals/UnitProductionGoal.h: a request to produce units or a building.

Stardust overloads the constructor on whether a build location is given; here the normal constructor takes the count,
producer limit, location and frame as keywords, and at() builds a goal for a building at a specific build location.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import UnitType
from stardust.builder.building_placement import BuildLocation

if TYPE_CHECKING:
    from stardust.producer.production_location import ProductionLocation
    from stardust.units.my_worker import MyWorker


class UnitProductionGoal:
    def __init__(self, requester: str, unit_type: UnitType, count: int = -1, producer_limit: int = -1,
                 location: ProductionLocation = None, frame: int = 0,
                 reserved_builder: MyWorker | None = None) -> None:
        self.requester = requester
        self._type = unit_type
        self._count = count
        self._producer_limit = producer_limit
        self._location = location
        self._reserved_builder = reserved_builder
        self._frame = frame

    @classmethod
    def at(cls, requester: str, unit_type: UnitType, location: BuildLocation,
           reserved_builder: MyWorker | None = None, frame: int = 0) -> UnitProductionGoal:
        """A building at a specific build location."""
        return cls(requester, unit_type, 1, 1, location, frame, reserved_builder)

    def unit_type(self) -> UnitType:
        return self._type

    def get_producer_limit(self) -> int:
        """Maximum cap of how many producers of the item we should create; -1 if we do not want to limit it."""
        return self._producer_limit

    def count_to_produce(self) -> int:
        """The number of items that should be produced; -1 if we want constant production."""
        return self._count

    def get_location(self) -> ProductionLocation:
        """Either a neighbourhood for the building placer or, if the item is a building, potentially a specific build
        location."""
        return self._location

    def get_reserved_builder(self) -> MyWorker | None:
        """For buildings with a fixed tile location, a builder we already reserved to build it. This is useful for
        plays that "own" a worker and want to build something with it."""
        return self._reserved_builder

    def get_frame(self) -> int:
        """The frame when the first item should be produced."""
        return self._frame

    def set_count_to_produce(self, new_count: int) -> None:
        """Used occasionally if something needs to reprioritize the queue."""
        self._count = new_count

    def __str__(self) -> str:
        if self._type.isBuilding():
            text = str(self._type)
            if isinstance(self._location, BuildLocation):
                text += f"@{self._location.location.tile}"
            else:
                text += "@UNK"
        else:
            text = f"{self._count}x{self._type}"
        if self._frame > 0:
            text += f"%{self._frame}"
        return text + f" ({self.requester})"

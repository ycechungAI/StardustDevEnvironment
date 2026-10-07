"""Port of Workers/MiningOptimizationV2/DataModel/PositionDeltaAndVelocity.h: a path node position, stored as an index
into the map's table of position deltas, plus the heading and velocity when needed to tell peers apart."""

from __future__ import annotations

from stardust.workers.mining_optimization_v2.data_model.position_and_velocity import PositionAndVelocity, i8, u16


class PositionDeltaAndVelocity:
    __slots__ = ("packed", "heading", "velocity_x", "velocity_y")

    def __init__(self, packed: int = 0, heading: int = 0, velocity_x: int = 0, velocity_y: int = 0) -> None:
        self.packed = packed  # Packed x and y deltas and whether this struct needs heading and velocity
        self.heading = heading
        self.velocity_x = velocity_x
        self.velocity_y = velocity_y

    def position_delta_index(self) -> int:
        """Index of the position delta into the position delta vector (stored in the upper 7 bits)."""
        return self.packed >> 1

    def requires_heading_and_velocity(self) -> bool:
        """Whether this position delta requires using the heading and velocity to differentiate it from its peers."""
        return (self.packed & 0b00000001) == 1

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, PositionDeltaAndVelocity):
            return NotImplemented

        # Packed data must match
        if self.packed != other.packed:
            return False

        # If heading and velocity don't matter, they are equal
        if not self.requires_heading_and_velocity():
            return True

        # Otherwise we need to compare the heading and velocity also
        return (self.heading, self.velocity_x, self.velocity_y) == (other.heading, other.velocity_x, other.velocity_y)

    __hash__ = None  # type: ignore[assignment]

    def matches(self, start: PositionAndVelocity, end: PositionAndVelocity,
                position_deltas: list[tuple[int, int]]) -> bool:
        """Checks if this delta matches the delta between two positions."""
        # Start by comparing the x and y deltas
        delta = position_deltas[self.position_delta_index()]
        if delta[0] != i8(end.x - start.x):
            return False
        if delta[1] != i8(end.y - start.y):
            return False

        # If we don't need to compare heading and velocity, there is a match
        if not self.requires_heading_and_velocity():
            return True

        # Compare the heading and velocities to the end position
        return (self.heading == end.heading and self.velocity_x == end.velocity_x
                and self.velocity_y == end.velocity_y)

    def add_to(self, other: PositionAndVelocity, position_deltas: list[tuple[int, int]]) -> PositionAndVelocity:
        """Gets the position that results from adding this delta to a given position."""
        delta = position_deltas[self.position_delta_index()]
        if self.requires_heading_and_velocity():
            return PositionAndVelocity(u16(other.x + delta[0]), u16(other.y + delta[1]), self.heading,
                                       self.velocity_x, self.velocity_y, False)
        return PositionAndVelocity(u16(other.x + delta[0]), u16(other.y + delta[1]), 0, 0, 0, True)

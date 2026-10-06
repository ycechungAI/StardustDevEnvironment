"""Port of Workers/MiningOptimizationV2/DataModel/PositionAndVelocity.h: a worker position with the heading and
velocity in full BW precision, used as the root of recorded paths."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bwapi import Position

if TYPE_CHECKING:
    import bwapi
    from stardust.units.my_worker import MyWorker


def u16(value: int) -> int:
    return value & 0xFFFF


def i8(value: int) -> int:
    return ((value + 0x80) & 0xFF) - 0x80


def i16(value: int) -> int:
    return ((value + 0x8000) & 0xFFFF) - 0x8000


class PositionAndVelocity:
    """x and y are pixel positions (uint16), heading is in 1/256ths of a circle (int8) and the velocities are in full BW
    precision cut down to 16 bits. If ignore_heading_and_velocity is set on either side, comparisons only use x and y."""

    __slots__ = ("x", "y", "heading", "velocity_x", "velocity_y", "ignore_heading_and_velocity")

    def __init__(self, x: int = 0, y: int = 0, heading: int = 0, velocity_x: int = 0, velocity_y: int = 0,
                 ignore_heading_and_velocity: bool = False) -> None:
        self.x = x
        self.y = y
        self.heading = heading
        self.velocity_x = velocity_x
        self.velocity_y = velocity_y
        self.ignore_heading_and_velocity = ignore_heading_and_velocity

    @staticmethod
    def from_exact_position(exact_position: bwapi.ExactPosition) -> PositionAndVelocity:
        pos = exact_position.pos()
        return PositionAndVelocity(u16(pos.x), u16(pos.y), exact_position.heading, i16(exact_position.velocityX),
                                   i16(exact_position.velocityY))

    @staticmethod
    def from_worker(worker: MyWorker) -> PositionAndVelocity:
        return PositionAndVelocity(u16(worker.last_position.x), u16(worker.last_position.y), i8(worker.bw_heading()),
                                   i16(worker.bw_velocity_x()), i16(worker.bw_velocity_y()))

    def copy(self) -> PositionAndVelocity:
        return PositionAndVelocity(self.x, self.y, self.heading, self.velocity_x, self.velocity_y,
                                   self.ignore_heading_and_velocity)

    def _key(self, other: PositionAndVelocity) -> tuple[int, ...]:
        if self.ignore_heading_and_velocity or other.ignore_heading_and_velocity:
            return self.x, self.y
        return self.x, self.y, self.heading, self.velocity_x, self.velocity_y

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, PositionAndVelocity):
            return NotImplemented
        return self._key(other) == other._key(self)

    def __lt__(self, other: PositionAndVelocity) -> bool:
        return self._key(other) < other._key(self)

    def __hash__(self) -> int:
        # (Stardust's hash also includes the heading and velocity, so positions that only compare equal because one
        # ignores them generally don't find each other in hashed containers there either.)
        return hash((self.x, self.y, self.heading, self.velocity_x, self.velocity_y))

    def to_position(self) -> Position:
        return Position(self.x, self.y)

    def __str__(self) -> str:
        if self.ignore_heading_and_velocity:
            return f"({self.x},{self.y})"
        return f"({self.x},{self.y},h={self.heading},dx={self.velocity_x},dy={self.velocity_y})"

    def __repr__(self) -> str:
        return f"PositionAndVelocity{self}"

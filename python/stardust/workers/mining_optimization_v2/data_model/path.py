"""Port of Workers/MiningOptimizationV2/DataModel/Path.h: the tree of recorded positions a worker may visit on its way
to a patch or depot, with the observed arrival data at the leaves."""

from __future__ import annotations

from typing import Protocol

import bwapi
from stardust.workers.mining_optimization_v2.data_model.cannon_placement import CannonPlacement
from stardust.workers.mining_optimization_v2.data_model.position_and_velocity import PositionAndVelocity
from stardust.workers.mining_optimization_v2.data_model.position_delta_and_velocity import PositionDeltaAndVelocity


class ArrivalData(Protocol):
    """GatherArrivalData or ReturnArrivalData (Stardust's ObservationType template parameter)."""

    def arrival_delay(self) -> int: ...

    def add_delay_after_action(self, delays_with_probabilities: dict[int, float], order_process_timer_at_arrival: int,
                               action_frame: int, base_probability: float) -> None: ...

    def __lt__(self, other: ArrivalData, /) -> bool: ...


class PathNode[T: ArrivalData]:
    """A node in a path. The path may branch because of a resend taking effect at this position or because of subpixel
    instability in the path. Vectors of (item, occurrence rate) pairs are sorted from highest occurrence rate to
    lowest."""

    __slots__ = ("pos", "arrival_data_when_final_node", "arrival_data_after_resend", "next_positions",
                 "next_positions_after_resend", "is_stable_resend_node")

    def __init__(self) -> None:
        # The position delta, which includes velocity and heading when needed
        self.pos = PositionDeltaAndVelocity()

        # The arrival observations on this node if it is the final node in the path, i.e. the node immediately before
        # arrival
        self.arrival_data_when_final_node: list[tuple[T, int]] = []

        # The arrival observations when a final resend takes effect here, meaning we don't track the rest of the path
        self.arrival_data_after_resend: list[tuple[T, int]] = []

        # All next positions seen from this position when the path has not been changed by a resend
        # Will be empty on the last node before arrival at the patch
        self.next_positions: list[tuple[PathNode[T], int]] = []

        # All next positions seen from this position after a resend takes effect at this node
        # Will be empty on any nodes where resends do not change the path or no additional resends are possible
        self.next_positions_after_resend: list[tuple[PathNode[T], int]] = []

        # Whether this is a stable resend node, where a resend taking effect does not change the path
        # Both stable nodes and nodes outside the exploration window are stored without data in the resend vectors, so
        # this is needed to differentiate the two cases.
        self.is_stable_resend_node = False

    def applicable_arrival_data(self, frame: int, previous_resend_frames: set[int]) -> list[tuple[T, int]]:
        """The appropriate arrival data depending on whether a resend is taking effect here or not. Note that the node
        is not guaranteed to have arrival data, so the caller should check this."""
        if (frame - bwapi.Broodwar.getLatencyFrames()) in previous_resend_frames:
            return self.arrival_data_after_resend
        return self.arrival_data_when_final_node

    def applicable_next_positions(self, frame: int, previous_resend_frames: set[int]) -> list[tuple[PathNode[T], int]]:
        """The appropriate next positions depending on whether a resend is taking effect here or not."""
        if self.is_stable_resend_node:
            return self.next_positions

        if (frame - bwapi.Broodwar.getLatencyFrames()) in previous_resend_frames:
            return self.next_positions_after_resend
        return self.next_positions


class Path[T: ArrivalData]:
    """The root of a path."""

    __slots__ = ("pos", "next_positions", "cannon_placement")

    def __init__(self, pos: PositionAndVelocity | None = None) -> None:
        # The position, including velocity and heading
        self.pos = pos if pos is not None else PositionAndVelocity()

        # All next positions seen from this node
        self.next_positions: list[tuple[PathNode[T], int]] = []

        # The cannon placement this path applies to
        # This is added here after deserialization to help with cache invalidation
        self.cannon_placement = CannonPlacement()

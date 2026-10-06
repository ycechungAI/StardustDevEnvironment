"""Port of Workers/MiningOptimizationV2/DataModel/GatherArrivalData.{h,cpp}: the arrival data tracked for gather paths.

- Arrival delay: the number of frames to arrival at the target
- Facing target: whether the worker is facing its target at arrival
- Collision: whether there was a collision when the worker leaves the target again
- Ten distance delta: the number of frames from the arrival frame where the worker is 10 distance from the patch
- Resend always arrives delta: the number of frames from the arrival frame where a resend will always reach the patch
  on time

For final resend nodes, the ten distance and resend always arrives deltas are stored together (as an index into the
map's table), as both are needed. For arrival nodes, only the resend always arrives delta is stored, as the 10-distance
data can be taken from the actual path. (Next path lengths are disabled in Stardust's configuration.)
"""

from __future__ import annotations

from stardust.util import order_process_timer


class GatherArrivalData:
    __slots__ = ("packed", "ten_distance_and_resend_always_arrives_index", "resend_always_arrives_delta")

    def __init__(self, packed: int = 0xFF, ten_distance_and_resend_always_arrives_index: int = 0,
                 resend_always_arrives_delta: int = 0xFF) -> None:
        self.packed = packed
        self.ten_distance_and_resend_always_arrives_index = ten_distance_and_resend_always_arrives_index
        self.resend_always_arrives_delta = resend_always_arrives_delta

    def arrival_delay(self) -> int:
        """The number of frames to arrival at the target (stored in the upper 6 bits)."""
        return self.packed >> 2

    def facing_target(self) -> bool:
        """Whether the worker is facing its target at arrival (the lowest bit is set if it isn't)."""
        return (self.packed & 0b00000001) == 0

    def collision(self) -> bool:
        """Whether there was a collision (the second-lowest bit is set if the worker collided)."""
        return (self.packed & 0b00000010) == 0b00000010

    def _key(self) -> tuple[int, int, int]:
        return self.packed, self.ten_distance_and_resend_always_arrives_index, self.resend_always_arrives_delta

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, GatherArrivalData):
            return NotImplemented
        return self._key() == other._key()

    def __lt__(self, other: GatherArrivalData) -> bool:
        return self._key() < other._key()

    def __hash__(self) -> int:
        return hash(self._key())

    def add_delay_after_action(self, delays_with_probabilities: dict[int, float], order_process_timer_at_arrival: int,
                               action_frame: int, base_probability: float) -> None:
        """Adds the delay after mining start to the given map.

        Possible delays for gather:
        - Not facing patch when transitioning to mine incurs an order process timer cycle of delay for the worker to turn
        - A collision after mining completion incurs an order process timer cycle of delay to resolve the collision
        - An order process timer reset during the transition to mining incurs a delay before the mining timer starts
          counting down
        The two first points can be shortened or lengthened if there is an order process timer reset during the delay,
        but this is such an extreme edge case that we don't consider it.
        """
        # The base delay just uses the collision and facing patch data
        # Not facing patch does not actually incur two rounds of delay, but it messes with our timings so much we
        # really want to avoid them
        base_delay = (9 if self.collision() else 0) + (18 if not self.facing_target() else 0)

        # For gather, the "action frame" is the frame that the worker transitions to MiningMinerals
        # There is an extra delay if the order timer resets on the frame after this, which is when the mining timer
        # starts counting down

        # If there is no order timer reset, just proceed normally
        if not order_process_timer.is_reset_frame(action_frame + 1):
            delays_with_probabilities[base_delay] = delays_with_probabilities.get(base_delay, 0.0) + base_probability
            return

        # Otherwise simulate between 0 and 7 extra frames of delay
        for i in range(8):
            delays_with_probabilities[base_delay + i] = (delays_with_probabilities.get(base_delay + i, 0.0)
                                                         + base_probability * 0.125)

    def __str__(self) -> str:
        text = str(self.arrival_delay())
        if not self.facing_target():
            text += "[!f]"
        # (As in Stardust, this marks the arrivals without a collision.)
        if not self.collision():
            text += "[c]"
        return text

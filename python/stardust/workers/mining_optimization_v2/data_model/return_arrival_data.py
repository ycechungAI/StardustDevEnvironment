"""Port of Workers/MiningOptimizationV2/DataModel/ReturnArrivalData.{h,cpp}: the arrival data tracked for return
paths.

- Arrival delay: the number of frames to arrival at the depot
- Collision: whether the worker collides with the depot after delivery when delivery does not happen on the first frame
- Exit speed: the exit speed from the depot when delivery happens on the first frame
- Facing target: whether the worker is facing the depot on arrival or needs to path to turn to the correct heading

The booleans are packed in with the arrival delay and exit speed to save on data.
"""

from __future__ import annotations

from stardust.util import order_process_timer


class ReturnArrivalData:
    __slots__ = ("packed_arrival_delay_and_collision", "packed_exit_speed_and_facing_depot")

    def __init__(self, packed_arrival_delay_and_collision: int = 0xFF,
                 packed_exit_speed_and_facing_depot: int = 0xFF) -> None:
        self.packed_arrival_delay_and_collision = packed_arrival_delay_and_collision
        self.packed_exit_speed_and_facing_depot = packed_exit_speed_and_facing_depot

    def arrival_delay(self) -> int:
        """The number of frames to arrival at the target (stored in the upper 7 bits)."""
        return self.packed_arrival_delay_and_collision >> 1

    def collision(self) -> bool:
        """Whether there is collision with delivery after arrival (lowest bit)."""
        return (self.packed_arrival_delay_and_collision & 0b00000001) == 0b00000001

    def exit_speed(self) -> int:
        """The exit speed of the worker from the depot back towards the patch when there was delivery at arrival."""
        return self.packed_exit_speed_and_facing_depot >> 1

    def facing_target(self) -> bool:
        """Whether the worker is facing the depot at arrival (lowest bit)."""
        return (self.packed_exit_speed_and_facing_depot & 0b00000001) == 0b00000001

    def _key(self) -> tuple[int, int]:
        return self.packed_arrival_delay_and_collision, self.packed_exit_speed_and_facing_depot

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, ReturnArrivalData):
            return NotImplemented
        return self._key() == other._key()

    def __lt__(self, other: ReturnArrivalData) -> bool:
        return self._key() < other._key()

    def __hash__(self) -> int:
        return hash(self._key())

    def add_delay_after_action(self, delays_with_probabilities: dict[int, float], order_process_timer_at_arrival: int,
                               action_frame: int, base_probability: float) -> None:
        """Adds the delay after the return to the given map. If the order process timer at arrival is 0, the delay is
        allowed to be negative if speed is kept; otherwise only collisions are considered."""
        delay = 0

        if not self.facing_target():
            delay = 18  # Not really, but we really really want to avoid these paths since they mess with our timings

        if order_process_timer_at_arrival == 0:
            # The numbers here have been experimentally derived
            speed = self.exit_speed()
            if speed == 0:
                # Collision
                delay += 9
            elif speed > 110:
                delay -= 5
            elif speed > 81:
                delay -= 4
            elif speed > 74:
                delay -= 3
            elif speed > 59:
                delay -= 2
        elif self.collision():
            delay += 9

        if order_process_timer.is_reset_frame(action_frame + 1):
            additional_delay_base_probability = base_probability / 8.0
            for additional_delay in range(8):
                key = delay + additional_delay
                delays_with_probabilities[key] = (delays_with_probabilities.get(key, 0.0)
                                                  + additional_delay_base_probability)
        else:
            delays_with_probabilities[delay] = delays_with_probabilities.get(delay, 0.0) + base_probability

    def __str__(self) -> str:
        text = f"{self.arrival_delay()}/{self.exit_speed()}"
        if self.collision():
            text += "[c]"
        if not self.facing_target():
            text += "[!fd]"
        return text

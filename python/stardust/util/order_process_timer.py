"""Port of Util/OrderProcessTimer.{h,cpp}: predicting BW's per-unit order process timer.

BW resets all order process timers every 150 frames, starting at frame 8. C++ overloads without a frame argument
use the current frame; here the frame argument defaults to None for that. Multisets are sorted lists.
"""

import bwapi
from stardust import common
from stardust.cpp import cmod

_FIRST_RESET_FRAME = 8
_RESET_FREQUENCY = 150
_ALL_RESET_VALUES = [0, 1, 2, 3, 4, 5, 6, 7]


def _frame(frame: int | None) -> int:
    return common.current_frame if frame is None else frame


def _bot_frame_to_game_frame(bot_frame: int) -> int:
    # Convert based on the current difference between the engine and bot frames to handle pauses.
    # Note that this might not be correct if a pause happens / happened between the current frame and the frame we
    # are converting.
    return bot_frame + (bwapi.Broodwar.getFrameCount() - common.current_frame)


def frames_to_previous_reset(frame: int | None = None) -> int:
    """Frames since the previous order process timer reset, or 0 if a reset happens on the frame."""
    return cmod(_bot_frame_to_game_frame(_frame(frame)) - _FIRST_RESET_FRAME, _RESET_FREQUENCY)


def frames_to_next_reset(frame: int | None = None) -> int:
    """Frames until the next order process timer reset, or 0 if a reset happens on the frame."""
    return cmod(_RESET_FREQUENCY - frames_to_previous_reset(frame), _RESET_FREQUENCY)


def next_reset_frame(frame: int | None = None) -> int:
    frame = _frame(frame)
    return frame + frames_to_next_reset(frame)


def previous_reset_frame(frame: int | None = None) -> int:
    frame = _frame(frame)
    return frame - frames_to_previous_reset(frame)


def is_reset_frame(frame: int | None = None) -> bool:
    return frames_to_previous_reset(frame) == 0


def unit_order_process_timer_at_delta(unit_order_process_timer: int, frame_delta: int, frame: int | None = None) -> int:
    """The order process timer a unit will have frame_delta frames from the given frame, or -1 if a reset makes it
    unpredictable."""
    frame = _frame(frame)
    if frame_delta == 0:
        return unit_order_process_timer
    if unit_order_process_timer == -1:
        return -1

    if frame_delta > 0:
        if frames_to_next_reset(frame + 1) < frame_delta:
            return -1
        result = unit_order_process_timer - frame_delta
        while result < 0:
            result += 9
        return result

    if frames_to_previous_reset(frame) < -frame_delta:
        return -1
    result = unit_order_process_timer - frame_delta
    while result > 8:
        result -= 9
    return result


def at_start_of_frame_at_delta(start_frame: int, possible_starting_values: list[int], gather_command_frames: set[int],
                               return_command_frames: set[int], frame_delta: int) -> list[int]:
    """The possible order process timer values frame_delta frames after start_frame, given the possible values at the
    start of start_frame and the frames on which gather / return commands were sent."""
    if frame_delta == 0:
        return sorted(possible_starting_values)

    end_frame = start_frame + frame_delta

    # Find the last command frame that takes effect within the window
    last_command_frame_takes_effect = -1
    last_command_is_gather = False
    latency = bwapi.Broodwar.getLatencyFrames()
    for command_frames, is_gather in ((gather_command_frames, True), (return_command_frames, False)):
        for command_frame in sorted(command_frames):
            takes_effect = command_frame + latency + 1  # Plus one as we are aligned to start of frame
            if last_command_frame_takes_effect < takes_effect <= end_frame:
                last_command_frame_takes_effect = takes_effect
                last_command_is_gather = is_gather

    # If there is a resend that takes effect, reset the order process timer values accordingly.
    # On gather the order process timer goes to 0 for two frames while the command is processed.
    # On return the order process timer goes to 0 for one frame while the command is processed.
    if (last_command_frame_takes_effect > start_frame
            or (last_command_frame_takes_effect == start_frame and last_command_is_gather)):
        if last_command_frame_takes_effect == end_frame:
            return [0]
        remaining = frame_delta - (last_command_frame_takes_effect + 1 - start_frame)
        if is_reset_frame(last_command_frame_takes_effect + 1):
            return at_start_of_frame_at_delta(last_command_frame_takes_effect + 1, _ALL_RESET_VALUES, set(), set(),
                                              remaining)
        return at_start_of_frame_at_delta(last_command_frame_takes_effect + 1, [0 if last_command_is_gather else 8],
                                          set(), set(), remaining)

    # If there is a reset frame within the window, the values will reset to 0-7 inclusive at the start of that frame.
    # We don't include the start frame since a reset there has already been taken into account in the initial options.
    to_next_reset = frames_to_next_reset(start_frame + 1) + 1
    if to_next_reset <= frame_delta:
        return at_start_of_frame_at_delta(start_frame + to_next_reset, _ALL_RESET_VALUES, set(), set(),
                                          frame_delta - to_next_reset)

    # Nothing has happened that would interfere with the normal cycle, so run it on all the values
    result = []
    for value in possible_starting_values:
        value -= frame_delta
        while value < 0:
            value += 9
        result.append(value)
    return sorted(result)


def at_start_of_next_frame(start_frame: int, at_end_of_start_frame: list[int]) -> list[int]:
    """Advances the possible values from the end of this frame to the start of the next frame."""
    if is_reset_frame(start_frame + 1):
        return list(_ALL_RESET_VALUES)
    return at_end_of_start_frame

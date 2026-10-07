"""Port of Units/MyWorker.h, Units/MyUnit/MyWorker.cpp and the gather/return orders from MyUnit_Orders.cpp: a worker,
tracking its order process timer (for mining optimization) and handling mineral walking.

Stardust's experimental worker attack micro is disabled upstream (attackUnit returns early), so only the live path is
ported. Multisets are sorted lists.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Orders, Position, Positions, UnitCommandTypes, WalkPosition
from stardust import common
from stardust.cpp import to_int
from stardust.instrumentation import log
from stardust.units.my_unit import MyUnit
from stardust.units.unit import Unit
from stardust.util import order_process_timer

if TYPE_CHECKING:
    from stardust.map.choke import Choke


def to_8b_speed(value: float) -> int:
    """Worker top speed is 5, so multiplying by 25 maps this to -125 to 125, fitting into an 8-bit integer."""
    return to_int(value * 25.0)


def to_8b_heading(value: float) -> int:
    """Headings are from 0 to 2pi, so multiplying by 40 maps this to 0 to 252, fitting into an 8-bit unsigned int."""
    return to_int(value * 40.0) & 0xFF


class MyWorker(MyUnit):
    __slots__ = (
        "carrying_resource", "last_carrying_resource_change", "last_started_mining",
        "last_transitioned_to_mining_order", "last_transitioned_to_wait_for_minerals_order",
        "possible_order_process_timer_values", "spawn_position", "horizontal_speed_8b", "vertical_speed_8b",
        "heading_8b", "_mineral_walking_patch", "_mineral_walking_target_area", "_mineral_walking_start_position",
        "_next_attack_predicted_at", "_previous_order",
    )

    def __init__(self, unit: bwapi.Unit) -> None:
        super().__init__(unit)
        self.carrying_resource = unit.isCarryingMinerals() or unit.isCarryingGas()
        self.last_carrying_resource_change = -1  # Frame when the unit last acquired or delivered a resource
        self.last_started_mining = -1  # Frame when the unit last started mining with the order timer counting down
        self.last_transitioned_to_mining_order = -1  # Frame when the unit last switched to MiningMinerals
        self.last_transitioned_to_wait_for_minerals_order = -1  # Frame when the unit last switched to WaitForMinerals
        # The possible order process timer values this worker might have on the current frame: one value if known,
        # otherwise an entry for each equally-possible value. A multiset because at game start the initial value may
        # be skewed by the enemy start location and race. Empty if we have no idea (units created by trigger in tests).
        self.possible_order_process_timer_values: list[int] = []
        # Where the worker spawned, if it spawned normally; invalid if it appeared because of a trigger (e.g. a test)
        self.spawn_position = Positions.Invalid
        self.horizontal_speed_8b = to_8b_speed(unit.getVelocityX())
        self.vertical_speed_8b = to_8b_speed(unit.getVelocityY())
        self.heading_8b = to_8b_heading(unit.getAngle())
        self._mineral_walking_patch: bwapi.Unit | None = None
        self._mineral_walking_target_area: bwem.Area | None = None
        self._mineral_walking_start_position = Positions.Invalid
        self._next_attack_predicted_at = -1
        self._previous_order = unit.getOrder()

    def update(self, unit: bwapi.Unit | None) -> None:
        if unit is None or not unit.exists():
            return

        frame = common.current_frame
        latency = bwapi.Broodwar.getLatencyFrames()

        if not self.completed and unit.isCompleted():
            self.spawn_position = unit.getPosition()

        super().update(unit)

        # We store an integer representation of the worker's velocity and heading, used for mining optimizations
        self.horizontal_speed_8b = to_8b_speed(unit.getVelocityX())
        self.vertical_speed_8b = to_8b_speed(unit.getVelocityY())
        self.heading_8b = to_8b_heading(unit.getAngle())

        # Set the order process timer for gathering workers
        is_reset_frame = order_process_timer.is_reset_frame()
        special_patch_lock_unknown_case = False
        order = unit.getOrder()
        carrying = unit.isCarryingMinerals() or unit.isCarryingGas()
        gather_frames = self.gather_command_frames
        return_frames = self.return_command_frames

        if self.carrying_resource != carrying:
            # We know the order process timer is 0 when the worker starts mining, finishes mining, and delivers
            self.carrying_resource = carrying
            self.last_carrying_resource_change = frame
            if not is_reset_frame:
                self.order_process_timer = 0
            if not carrying:
                # Clear return command frames, since we might have queued one up that hasn't taken effect yet
                return_frames.clear()
        elif order == Orders.MiningMinerals:
            # Record the transition to mining order
            if self._previous_order != Orders.MiningMinerals:
                # Clear gather command frames, since we might have queued one up that hasn't taken effect yet
                return_frames.clear()
                gather_frames.clear()
                self.last_transitioned_to_mining_order = frame
                if not is_reset_frame:
                    # There is one exception to this: if the worker has patch locked but hasn't rotated completely
                    # towards the patch yet, its order timer will go to 8 instead of 0. This happens exceptionally
                    # rarely and isn't trivial to detect, so we accept that the order timer is off by one in this case.
                    self.order_process_timer = 0

            # Record when mining actually starts
            if unit.getOrderTimer() == 75:
                self.last_started_mining = frame

                # First case here covers patch lock, where it overrides a reset
                if self.last_transitioned_to_mining_order == frame or not is_reset_frame:
                    self.order_process_timer = 8
        elif (((frame - latency) in gather_frames and not is_reset_frame)
              or (frame - latency - 1) in gather_frames
              or ((frame - latency) in return_frames and not is_reset_frame)):
            self.order_process_timer = 0
        elif (((frame - latency - 2) in gather_frames and not is_reset_frame)
              or (frame - latency - 1) in return_frames):
            self.order_process_timer = 8
        elif order == Orders.WaitForMinerals:
            if self._previous_order != Orders.WaitForMinerals:
                # On first frame the order process timer stays at 0
                self.last_transitioned_to_wait_for_minerals_order = frame
                if not order_process_timer.is_reset_frame():
                    self.order_process_timer = 0
            elif self.last_transitioned_to_wait_for_minerals_order == frame - 1:
                # Second frame indicates patch locking has just occurred, in which case the order process timer also
                # stays at 0 for one additional frame
                if not order_process_timer.is_reset_frame():
                    self.order_process_timer = 0
                else:
                    # If there is a reset frame on the second WaitForMinerals frame, the extra zero frame is deferred
                    # by the reset. Since we can't know when it will happen, we allow all 9 values.
                    special_patch_lock_unknown_case = True

        # Update the possible order process timer values; if the order process timer is known, set it directly
        if self.order_process_timer != -1:
            self.possible_order_process_timer_values = [self.order_process_timer]
        else:
            values = self.possible_order_process_timer_values

            # If this is a reset frame, start with the possible values after the reset (before orders are processed)
            if order_process_timer.is_reset_frame():
                values = [0, 1, 2, 3, 4, 5, 6, 7]

                # Exception for the special patch lock case described above, where we add on the final possible value
                if special_patch_lock_unknown_case:
                    values.append(8)

            # We can exclude the timer having been zero on this frame if we know the worker's order wasn't processed.
            # Currently only tracked for end of mining, since it's trivial to compute from the worker's current state.
            # (std::multiset::erase(0) removes every 0.)
            if 0 in values and order == Orders.MiningMinerals and unit.getOrderTimer() == 0:
                values = [v for v in values if v != 0]

            # Run the order process timer cycle on each value
            self.possible_order_process_timer_values = sorted(8 if v == 0 else v - 1 for v in values)

        self._previous_order = unit.getOrder()

    # Orders

    def gather(self, target: bwapi.Unit | None) -> bool:
        unit = self.bwapi_unit
        assert unit is not None
        if target is None or not target.exists():
            log.get(f"GATHER INVALID TARGET: {self}")
            return False
        if self._duplicate_order(f"Gather {target.getType()} @ {WalkPosition(target.getPosition())}"):
            return False

        # Unless it is a mineral field, don't gather on the same target again
        if not target.getType().isMineralField():
            current_command = unit.getLastCommand()
            if (current_command.getType() == UnitCommandTypes.Gather
                    and current_command.getTargetPosition() == target.getPosition()):
                return False

        result = unit.gather(target)
        if result:
            self.issued_order_this_frame = True
            self.gather_command_frames.add(common.current_frame)
        self._debug_order(f"Gather {target.getType()} @ {WalkPosition(target.getPosition())}")
        return result

    def return_cargo(self) -> bool:
        unit = self.bwapi_unit
        assert unit is not None
        if self._duplicate_order("Return cargo"):
            return False

        result = unit.returnCargo()
        if result:
            self.issued_order_this_frame = True
            self.return_command_frames.add(common.current_frame)
        self._debug_order("Return cargo")
        return result

    def attack_unit(self, target: Unit, units_and_targets: list[tuple[MyUnit, Unit | None]] | None = None,
                    cluster_attacking: bool = True, enemy_aoe_radius: int = 0) -> None:
        # Stardust disables its worker-specific attack logic ("Disable new code for now")
        super().attack_unit(target, units_and_targets, cluster_attacking, enemy_aoe_radius)

    # Movement

    def _reset_move_data(self) -> None:
        super()._reset_move_data()
        self._mineral_walking_patch = None
        self._mineral_walking_target_area = None
        self._mineral_walking_start_position = Positions.Invalid

    def mineral_walk(self, c: Choke | None) -> bool:
        from stardust.map.path_finding import path_finding

        if c is None and self._mineral_walking_patch is None:
            return False
        unit = self.bwapi_unit
        assert unit is not None
        bwem_map = bwem.Instance()

        # If we've passed a choke, we should consider initializing a new mineral walk
        if c is not None:
            if not c.requires_mineral_walk:
                return False

            next_waypoint = c.choke
            first_area, second_area = next_waypoint.GetAreas()

            # Determine which of the two areas accessible by the choke we are moving towards. We do this by looking at
            # the waypoint after the next one and seeing which area they share, or by looking at the area of the
            # target position if there are no more waypoints.
            if len(self.choke_path) == 1:
                self._mineral_walking_target_area = bwem_map.GetNearestArea(WalkPosition(self.target_position))
            else:
                self._mineral_walking_target_area = second_area
                if first_area in self.choke_path[1].GetAreas():
                    self._mineral_walking_target_area = first_area

            # Pull the mineral patch and start location to use for mineral walking. The patch may be None - on some
            # maps we need to use a visible mineral patch somewhere else on the map, which is handled below.
            towards_first = self._mineral_walking_target_area == first_area
            self._mineral_walking_patch = c.first_area_mineral_patch if towards_first else c.second_area_mineral_patch
            self._mineral_walking_start_position = (c.first_area_start_position if towards_first
                                                    else c.second_area_start_position)
            self.last_move_frame = 0

        # If we're close to the patch, or if the patch is None and we've moved beyond the choke, we're done
        patch = self._mineral_walking_patch
        if ((patch is not None and unit.getDistance(patch) < 32)
                or (patch is None
                    and bwem_map.GetArea(self.get_tile_position()) == self._mineral_walking_target_area
                    and self.get_distance(Position(self.choke_path[0].Center())) > 100)):
            self._mineral_walking_patch = None
            self._mineral_walking_target_area = None
            self._mineral_walking_start_position = Positions.Invalid

            # Remove the choke we just mineral walked and reset the grid
            if self.choke_path:
                self.choke_path.pop(0)
            self._reset_grid()

            # Move to the next waypoint
            self._move_to_next_waypoint()
            return True

        # Re-issue orders every second
        frame = common.current_frame
        if frame - self.last_move_frame < 24:
            return True

        # If the patch is None, click on any visible patch on the correct side of the choke
        if patch is None:
            for static_neutral in bwapi.Broodwar.getStaticNeutralUnits():
                if not static_neutral.getType().isMineralField():
                    continue
                if not static_neutral.exists() or not static_neutral.isVisible():
                    continue

                # The path to this mineral field should cross the choke we're mineral walking
                path, _ = path_finding.get_choke_point_path(self.last_position, static_neutral.getInitialPosition(),
                                                            self.type,
                                                            path_finding.PathFindingOptions.UseNearestBWEMArea)
                if self.choke_path and self.choke_path[0] in path:
                    self.right_click(static_neutral)
                    self.last_move_frame = frame
                    return True

            # We couldn't find any suitable visible mineral patch, warn and abort
            log.debug("Error: Unable to find mineral patch to use for mineral walking")
            self._reset_move_data()
            return True

        # If the patch is visible, click on it
        if patch.exists() and patch.isVisible():
            self.right_click(patch)
            self.last_move_frame = frame
            return True

        # If we have a start location defined, click on it
        if self._mineral_walking_start_position.isValid():
            self.move(self._mineral_walking_start_position)
            return True

        log.get("ERROR: Unable to find tile to mineral walk from")
        self._reset_move_data()
        return True

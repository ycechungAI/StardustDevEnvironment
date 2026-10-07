"""Port of Workers/MiningOptimizationV2/Solver/Solver.h, Solver_Common.cpp, Solver_Gather.cpp and Solver_Return.cpp:
the path solver.

The solver has the following outputs:
- The best actions to be taken for each possible branch in the path
- The expected results (arrival frame, mining/delivery frame, penalty for collision or not facing patch) with their
  probabilities
- Data on what frames can be used for resending close to the patch when optimizing for takeover and patch locking

This allows our mining optimization logic to call the solver, then follow the worker's path in the solver result,
issuing the desired actions and gradually becoming more confident of the result as any path branches are taken.

Constraints considered by the solver:
- Gather resends cannot be issued LF from each other (Unit_Busy)
- Gather resends cannot be issued LF+1 frames before an order process timer reset, as this usually puts the worker in a
  weird state (it will stay in the ResetCollision order for more than the usual single frame, unless its order process
  timer resets to 0)

The algorithm works in the following way:
- We start by recursively exploring the next node(s) on the no-resend path
- We then peek LF ahead to see if a resend is viable from the node
- If a resend is viable, we recursively explore as above, but with the resend
- We then weight and compare the results and pick the best one, updating our path branch structure accordingly
- We then return up the tree, until we have built the full decision tree with weighted results at each node

Stardust specializes the solver for gather and return paths; here is_gather selects the behaviour.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING

import bwapi
from bwapi import UnitTypes
from stardust import config
from stardust.instrumentation import log
from stardust.util import geo, order_process_timer
from stardust.workers.mining_optimization_v2.data_model.gather_arrival_data import GatherArrivalData
from stardust.workers.mining_optimization_v2.data_model.path import ArrivalData, Path, PathNode
from stardust.workers.mining_optimization_v2.solver.solver_result import SolverArrivalData, SolverResult

if TYPE_CHECKING:
    from stardust.units.resource import Resource
    from stardust.workers.mining_optimization_v2.data_model.map_data import MapData
    from stardust.workers.mining_optimization_v2.data_model.position_and_velocity import PositionAndVelocity

UINT8_MAX = 0xFF


@dataclass
class SolverResends:
    """The resend frames, with whether the final resend has been planned."""
    resend_frames: set[int] = field(default_factory=set)
    is_final: bool = False


def _add(probabilities: dict[int, float], key: int, value: float) -> None:
    probabilities[key] = probabilities.get(key, 0.0) + value


class Solver[T: ArrivalData]:
    def __init__(self, map_data: MapData, resource: Resource, start_position: PositionAndVelocity, path: Path[T],
                 start_frame: int, possible_worker_order_process_timer_values_at_start_frame: list[int],
                 is_gather: bool) -> None:
        # We keep a reference to the map data so we can interpret some of the data correctly
        self._map_data = map_data
        self._resource = resource

        # The start position of this solver execution
        self._start_position = start_position
        self._path = path

        # The start frame of this solver execution
        self._start_frame = start_frame

        # The worker's possible order process timer value at the end of the start frame
        # May be empty if the order process timer values are completely unknown
        self._possible_worker_order_process_timer_values_at_start_frame = sorted(
            possible_worker_order_process_timer_values_at_start_frame or [0, 1, 2, 3, 4, 5, 6, 7, 8])

        self._is_gather = is_gather

    def execute(self) -> SolverResult:
        # The possible order process timer frames we get as input to the solver are the values at the end of the start
        # frame
        # During the solver logic, we want to know the possible values just before the worker's orders are processed,
        # which may be different from the values at the end of the previous frame if there has been a resend or an
        # order process timer reset
        worker_order_process_timer = self._possible_worker_order_process_timer_values_at_start_frame
        if order_process_timer.is_reset_frame(self._start_frame + 1):
            worker_order_process_timer = [0, 1, 2, 3, 4, 5, 6, 7]

        result = self._process_next_nodes(self._start_position, self._path.next_positions, self._start_frame + 1, -1,
                                          SolverResends(), worker_order_process_timer)
        result.path_to_next_branch.appendleft(self._start_position)
        return result

    def _order_process_timer_in_future(self, start_frame: int, possible_starting_values: list[int],
                                       resend_frames: set[int], frames_in_future: int) -> list[int]:
        """The possible order process timer values (at the start of the given frame) frames_in_future from start."""
        if self._is_gather:
            return order_process_timer.at_start_of_frame_at_delta(start_frame, possible_starting_values,
                                                                  resend_frames, set(), frames_in_future)
        return order_process_timer.at_start_of_frame_at_delta(start_frame, possible_starting_values, set(),
                                                              resend_frames, frames_in_future)

    def _can_resend_on_frame(self, frame: int, previous_resend_frames: set[int]) -> bool:
        """Whether a resend can be sent on the given frame."""
        # For return, there are no frame timing limitations
        if not self._is_gather:
            return True

        latency = bwapi.Broodwar.getLatencyFrames()

        # Resends cannot take effect the frame before an order process timer reset, as this puts the worker in a weird
        # state
        # The reason for this is that gather commands take two frames to work out. On the first frame, the command
        # nullifies the order process timer reset. But on the second frame, the order process timer reset will take
        # effect and potentially delay the completion of the command processing.
        if order_process_timer.is_reset_frame(frame + latency + 2):
            return False

        # Resends cannot be sent LF from each other, as this gives a Unit_Busy error
        return (frame - latency) not in previous_resend_frames

    def _transition_frames_to_action(self) -> int:
        """For gather, there is one transition frame while the worker is in WaitForMinerals; delivery has no
        transition frame, it just delivers as soon as the order process timer is 0 after arrival."""
        return 1 if self._is_gather else 0

    def _process_next_nodes(self, pos: PositionAndVelocity, next_nodes: list[tuple[PathNode[T], int]],
                            next_frame: int, ten_distance_frame: int, previous_resends: SolverResends,
                            worker_order_process_timer: list[int]) -> SolverResult:
        """Recursively processes the given next nodes, returning the best solution for all paths below them."""
        map_data = self._map_data
        latency = bwapi.Broodwar.getLatencyFrames()

        def process_node(node: PathNode[T]) -> SolverResult:
            # Compute the position corresponding to this node and define the helper that adds it to a result
            here = node.pos.add_to(pos, map_data.position_deltas)

            def add_position_to(result: SolverResult) -> SolverResult:
                result.path_to_next_branch.appendleft(here)
                return result

            # Update the ten distance for this node
            # Basically we just find the first frame where the distance is 10 or less and pass it forward
            node_ten_distance_frame = ten_distance_frame
            if self._is_gather:
                if (node_ten_distance_frame == -1
                        and geo.edge_to_edge_distance(UnitTypes.Protoss_Probe, here.to_position(),
                                                      UnitTypes.Resource_Mineral_Field, self._resource.center) <= 10):
                    node_ten_distance_frame = next_frame

            # If we have reached the end of the path, create the new branch and populate it with the arrival data
            next_positions = node.applicable_next_positions(next_frame, previous_resends.resend_frames)
            if not next_positions:
                result = SolverResult()

                def add_arrival_data(arrival_data: T, probability: float) -> None:
                    # The arrival frame is given by the delay here
                    # For final resend nodes the delay will be positive
                    # For final non-resend nodes the delay will be 0
                    delay = arrival_data.arrival_delay()
                    arrival_frame = next_frame + delay

                    ten_distance = node_ten_distance_frame
                    resend_always_arrives = -1
                    if isinstance(arrival_data, GatherArrivalData):
                        # If this is a final resend node, take the ten distance frame from the arrival data
                        if delay != 0:
                            takeover_metadata = map_data.ten_distance_and_resend_always_arrives[
                                arrival_data.ten_distance_and_resend_always_arrives_index]

                            if ten_distance == -1:
                                if takeover_metadata[0] == (UINT8_MAX - 1):
                                    if config.LOGGING_ENABLED:
                                        log.get("ERROR: Takeover metadata indicates already at 10 distance, but this "
                                                "hasn't been captured in solver")
                                    ten_distance = arrival_frame
                                else:
                                    ten_distance = arrival_frame - takeover_metadata[0] + 1

                        # We always have the resend always arrives delta in the arrival data, but it is stored
                        # differently depending on whether this is a final resend node or an arrival node
                        resend_always_arrives_delta = arrival_data.resend_always_arrives_delta
                        if resend_always_arrives_delta == UINT8_MAX:
                            takeover_metadata = map_data.ten_distance_and_resend_always_arrives[
                                arrival_data.ten_distance_and_resend_always_arrives_index]
                            resend_always_arrives_delta = (takeover_metadata[1] - 1) & 0xFF

                        resend_always_arrives = max(arrival_frame - resend_always_arrives_delta + 1,
                                                    arrival_frame - 10)
                        if previous_resends.resend_frames:
                            resend_always_arrives = max(resend_always_arrives,
                                                        max(previous_resends.resend_frames) + latency + 1)

                    arrival_key = SolverArrivalData(arrival_frame, ten_distance, resend_always_arrives)
                    result.arrival_data_with_probabilities[arrival_key] = (
                        result.arrival_data_with_probabilities.get(arrival_key, 0.0) + probability)

                    # On gather there is an extra frame of delay between arrival and mining (the WaitForMinerals frame)
                    transition_frames = self._transition_frames_to_action()

                    # Compute the possible order process timer values at arrival, taking pending resends into account
                    possible_values_at_arrival = self._order_process_timer_in_future(
                        next_frame, worker_order_process_timer, previous_resends.resend_frames,
                        arrival_data.arrival_delay())

                    # If there was a recent resend, override the order process timer value to simulate the fact that a
                    # 0 value won't actually take effect here
                    if possible_values_at_arrival == [0]:
                        for i in range(transition_frames + 1):
                            if (arrival_frame - latency - i - 1) in previous_resends.resend_frames:
                                possible_values_at_arrival = [8 + i + transition_frames]

                    # Now use this data to compute when the action (mining start or resource delivery) will occur
                    # As the action will occur once the order process timer reaches 0, in the simple case we can just
                    # add the order process timer value to the arrival frame and get the action frame.
                    # However, the order process timer might reset after arrival. In this case, there will be 8
                    # additional possible order process timer values to consider.

                    # Get the number of frames from the arrival frame to the next order process timer reset
                    # We do not consider resets at the arrival frame itself, since this is already handled
                    reset_after_arrival = order_process_timer.frames_to_next_reset(arrival_frame + 1) + 1

                    # Loop through the possible values
                    # As the list is sorted, we know we will handle the values before the reset first
                    handled_values_before_reset = 0
                    value_count = len(possible_values_at_arrival)
                    for order_process_timer_value in possible_values_at_arrival:
                        action_delay = order_process_timer_value + transition_frames
                        if action_delay < reset_after_arrival:
                            probability_here = probability * (1.0 / value_count)
                            _add(result.action_frames_with_probabilities, arrival_frame + action_delay,
                                 probability_here)
                            arrival_data.add_delay_after_action(result.delays_with_probabilities,
                                                                order_process_timer_value,
                                                                arrival_frame + action_delay, probability_here)
                            handled_values_before_reset += 1
                            continue

                        # A reset has occurred before all values were considered
                        # We now have to consider the possible values 0 to 7 from the reset frame

                        # Their probability is the probability we made it to the reset frame times the probability of
                        # each value (1/8)
                        reset_value_probability = ((1.0 - (handled_values_before_reset / value_count)) / 8.0
                                                   * probability)

                        # The delay from the arrival frame will be the delta to the timer reset, plus the transition
                        # frame if it hasn't already happened, plus the value the order process timer resets to
                        action_delay = reset_after_arrival + (0 if action_delay == reset_after_arrival
                                                              else transition_frames)

                        # (As in Stardust, the loop carries on, so this is added for each remaining value.)
                        for reset_value in range(8):
                            _add(result.action_frames_with_probabilities, arrival_frame + action_delay + reset_value,
                                 reset_value_probability)
                            arrival_data.add_delay_after_action(result.delays_with_probabilities,
                                                                order_process_timer_value,
                                                                arrival_frame + action_delay + reset_value,
                                                                reset_value_probability)

                # Process the arrival data, weighting by probability if there are unstable results
                arrival_data_and_occurrence_rates = node.applicable_arrival_data(next_frame,
                                                                                 previous_resends.resend_frames)

                if config.LOGGING_ENABLED and not arrival_data_and_occurrence_rates:
                    log.get("ERROR: Empty arrival data at leaf node in path solver")

                for arrival_data, occurrence_rate in arrival_data_and_occurrence_rates:
                    add_arrival_data(arrival_data, map_data.occurrence_rate_to_probability(occurrence_rate))

                return add_position_to(result)

            # Compute the order process timer values at the start of the next frame
            next_worker_order_process_timer = self._order_process_timer_in_future(
                next_frame, worker_order_process_timer, previous_resends.resend_frames, 1)

            # Get the result from not resending here
            no_resend_result = self._process_next_nodes(here, next_positions, next_frame + 1, node_ten_distance_frame,
                                                        previous_resends, next_worker_order_process_timer)

            # If there can be a resend, try it
            resend_viable, resend_is_final = self._is_resend_viable_here(node, next_frame, previous_resends)
            if not resend_viable:
                return add_position_to(no_resend_result)

            # Add the resend frame
            resends_here = SolverResends(previous_resends.resend_frames | {next_frame}, resend_is_final)

            # Get the result
            resend_result = self._process_next_nodes(here, next_positions, next_frame + 1, node_ten_distance_frame,
                                                     resends_here, next_worker_order_process_timer)
            resend_result.resend_frames_on_this_branch.add(next_frame)
            resend_result.resend_frames_on_all_branches.add(next_frame)

            # Score the two results and return the best one
            def score_result(result: SolverResult) -> float:
                # Start with the action frame
                score = SolverResult.map_average(result.action_frames_with_probabilities)

                # Add the delays, but weight them ever so slightly more than the base score
                # The rationale is that if we have otherwise equal results, we'd prefer not to have the delay
                # TODO: Test and consider making this more sophisticated - we actually don't really care about
                #       collision delays, but facing patch and mining start delays keep the patch busy for longer,
                #       which can be good or bad depending on whether another worker is waiting to mine or not
                score += SolverResult.map_average(result.delays_with_probabilities) * 1.001

                return score

            if score_result(no_resend_result) <= score_result(resend_result):
                return add_position_to(no_resend_result)
            return add_position_to(resend_result)

        # If the path doesn't branch here, we can just return the result for the single node
        if len(next_nodes) == 1:
            return process_node(next_nodes[0][0])

        # The path branches, so we need to also branch the solve result
        branched = SolverResult()
        for node, occurrence_rate in next_nodes:
            node_result = process_node(node)

            # Adjust the probabilities by this node's probability
            node_probability = map_data.occurrence_rate_to_probability(occurrence_rate)

            for arrival_key, probability in sorted(node_result.arrival_data_with_probabilities.items()):
                branched.arrival_data_with_probabilities[arrival_key] = (
                    branched.arrival_data_with_probabilities.get(arrival_key, 0.0) + probability * node_probability)
            for frame, probability in sorted(node_result.action_frames_with_probabilities.items()):
                _add(branched.action_frames_with_probabilities, frame, probability * node_probability)
            for delay, probability in sorted(node_result.delays_with_probabilities.items()):
                _add(branched.delays_with_probabilities, delay, probability * node_probability)

            branched.resend_frames_on_all_branches.update(node_result.resend_frames_on_all_branches)

            branched.next_branches.append(node_result)

        return branched

    def _is_resend_viable_here(self, node: PathNode[T], frame: int, previous_resends: SolverResends
                               ) -> tuple[bool, bool]:
        """Whether a resend is viable from the given node on the given frame with the given previous resend frames.
        A resend is viable if it can be issued and all possible resend nodes are either stable or have resend data
        available. The second value is whether the resend must be the final resend, and is only relevant if the first
        value is True."""
        if previous_resends.is_final:
            return False, False

        # Reject immediately if a resend isn't possible on this frame
        if not self._can_resend_on_frame(frame, previous_resends.resend_frames):
            return False, False

        # We explore ahead LF frames and check if all observed nodes have resend data available
        frame_resend_takes_effect = frame + bwapi.Broodwar.getLatencyFrames()

        any_non_final_resends = False

        def check_next_nodes(next_nodes: list[tuple[PathNode[T], int]], next_frame: int) -> bool:
            nonlocal any_non_final_resends
            if not next_nodes:
                return False

            for next_node, _ in next_nodes:
                if next_frame == frame_resend_takes_effect:
                    if not next_node.arrival_data_after_resend and not next_node.next_positions_after_resend:
                        if not next_node.is_stable_resend_node:
                            return False
                    else:
                        any_non_final_resends = True
                else:
                    if not check_next_nodes(next_node.applicable_next_positions(next_frame,
                                                                                previous_resends.resend_frames),
                                            next_frame + 1):
                        return False

            return True

        viable = check_next_nodes(node.applicable_next_positions(frame, previous_resends.resend_frames), frame + 1)
        return viable, not any_non_final_resends

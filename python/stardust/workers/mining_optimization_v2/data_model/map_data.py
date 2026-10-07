"""Port of Workers/MiningOptimizationV2/DataModel/MapData.h: the mining optimization data for a map (recorded gather
and return paths per resource, and the initial worker split data)."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING

from stardust.cpp import cround
from stardust.instrumentation.log_formatting_util import format_vectorlike

if TYPE_CHECKING:
    from stardust.util.tile_position import TilePosition
    from stardust.workers.mining_optimization_v2.data_model.gather_arrival_data import GatherArrivalData
    from stardust.workers.mining_optimization_v2.data_model.position_and_velocity import PositionAndVelocity
    from stardust.workers.mining_optimization_v2.data_model.return_arrival_data import ReturnArrivalData
    from stardust.workers.mining_optimization_v2.data_model.serialized_path import SerializedPath

OCCURRENCE_SCALE = 127


@dataclass
class InitialSplitRotation:
    delay_frames: int = 0
    resend_frames: set[int] = field(default_factory=set)
    gather_arrival_frame: int = 0
    gather_action_frame: int = 0
    return_arrival_frame: int = 0
    return_action_frames_to_occurrences: dict[int, int] = field(default_factory=dict)

    def __str__(self) -> str:
        text = f"resends: {format_vectorlike(sorted(self.resend_frames))}"
        if self.delay_frames > 0:
            text += f"; delay {self.delay_frames}"
        text += f"; gather arrival: {self.gather_arrival_frame}"
        text += f"; gather action: {self.gather_action_frame}"
        text += f"; return arrival: {self.return_arrival_frame}"
        text += f"; return actions: {format_vectorlike(sorted(self.return_action_frames_to_occurrences))}"
        return text


@dataclass
class InitialSplitData:
    first_rotation: InitialSplitRotation = field(default_factory=InitialSplitRotation)
    first_rotation_delivery_to_second_rotation: dict[int, InitialSplitRotation] = field(default_factory=dict)

    def score(self) -> int:
        # First get the total occurrences
        total_occurrences = sum(self.first_rotation.return_action_frames_to_occurrences.values())

        # Now sum up the average delivery frames weighted by occurrences
        result = 0.0
        for first_delivery_frame, occurrences in sorted(self.first_rotation.return_action_frames_to_occurrences.items()):
            this_probability = occurrences / total_occurrences

            second_return_frames = \
                self.first_rotation_delivery_to_second_rotation[first_delivery_frame].return_action_frames_to_occurrences
            for second_return_frame in sorted(second_return_frames):
                result += (second_return_frame / len(second_return_frames)) * this_probability

        return cround(result * 10000.0)


def occurrence_rate_to_probability(occurrence_rate: int) -> float:
    return occurrence_rate / OCCURRENCE_SCALE


type InitialSplitDataByStartPosition = dict[PositionAndVelocity, dict[tuple[TilePosition, TilePosition],
                                                                      InitialSplitData]]


class MapData:
    def __init__(self) -> None:
        self.map_hash = ""

        # To save on bits, position deltas are stored as indices into this list
        # It is guaranteed to have a maximum size of 128, and the zero element (delta of 0,0) is always at index 0
        self.position_deltas: list[tuple[int, int]] = []

        # To save on bits, the ten distance and resend always arrives deltas are stored as indices into this list
        # Both could be packed as independent 4-bit numbers into one byte, but testing has shown there is a weak
        # correlation between the two values, allowing more data to be packed into one byte by treating them together
        self.ten_distance_and_resend_always_arrives: list[tuple[int, int]] = []

        # To save on bits, next path lengths are stored as an increment from the minimum path length for the map
        self.minimum_next_path_length = 0

        self.resource_to_serialized_gather_paths: dict[
            TilePosition, dict[PositionAndVelocity, SerializedPath[GatherArrivalData]]] = {}
        self.resource_to_serialized_return_paths: dict[
            TilePosition, dict[PositionAndVelocity, SerializedPath[ReturnArrivalData]]] = {}

        self.start_location_to_patch_pair_to_initial_split_data_zerg: InitialSplitDataByStartPosition = {}
        self.start_location_to_patch_pair_to_initial_split_data_not_zerg: InitialSplitDataByStartPosition = {}
        self.start_location_to_patch_pair_to_initial_split_data_unknown: InitialSplitDataByStartPosition = {}

        self.resource_to_average_single_worker_rotation_time: dict[TilePosition, int] = {}

    def clear(self, map_hash: str) -> None:
        # (As in Stardust, the ten distance table and the rotation times are not cleared here.)
        self.map_hash = map_hash
        self.position_deltas.clear()
        self.minimum_next_path_length = 0
        self.resource_to_serialized_gather_paths.clear()
        self.resource_to_serialized_return_paths.clear()
        self.start_location_to_patch_pair_to_initial_split_data_zerg.clear()
        self.start_location_to_patch_pair_to_initial_split_data_not_zerg.clear()
        self.start_location_to_patch_pair_to_initial_split_data_unknown.clear()

    @staticmethod
    def occurrence_rate_to_probability(occurrence_rate: int) -> float:
        return occurrence_rate_to_probability(occurrence_rate)

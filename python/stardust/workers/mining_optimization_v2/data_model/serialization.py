"""Port of Workers/MiningOptimizationV2/DataModel/Serialization.{h,cpp}: reads and writes the per-map mining
optimization data file (mining-optimization_<map hash>.bin.zstd), in the same format as Stardust: bitsery, compressed
with zstd.

The following techniques reduce the data file size:
- Positions are stored as deltas instead of absolute positions wherever possible
- The heading and velocity are only included on positions where they are needed to discriminate between peers
- The vectors and maps with occurrence rates are serialized such that they don't need a separate size item
- We know we never have resend data if we don't have non-resend data
- We use maximum zstd compression
"""

from __future__ import annotations

from collections.abc import Callable

import bwapi
from compression import zstd
from stardust.instrumentation import log
from stardust.util import file_tools
from stardust.util.tile_position import TilePosition
from stardust.workers.mining_optimization_v2.data_model.bitsery import BitseryError, Reader, Writer
from stardust.workers.mining_optimization_v2.data_model.map_data import InitialSplitData, \
    InitialSplitDataByStartPosition, InitialSplitRotation, MapData
from stardust.workers.mining_optimization_v2.data_model.path import ArrivalData
from stardust.workers.mining_optimization_v2.data_model.position_and_velocity import PositionAndVelocity
from stardust.workers.mining_optimization_v2.data_model.serialized_path import GATHER_CODEC, RETURN_CODEC, \
    ArrivalDataCodec, SerializedPath, read_position_and_velocity, read_tile_position, write_position_and_velocity, \
    write_tile_position

_game_parameters_initialized = False
_map_hash = ""


def _ensure_game_parameters_initialized() -> None:
    global _game_parameters_initialized, _map_hash
    if _game_parameters_initialized:
        return

    _map_hash = bwapi.Broodwar.mapHash()
    _game_parameters_initialized = True


def _get_filename(writing: bool) -> str:
    return file_tools.get_file_path(f"mining-optimization_{_map_hash}", "bin.zstd", writing)


def _read_paths[T: ArrivalData](reader: Reader, codec: ArrivalDataCodec[T]
                                ) -> dict[TilePosition, dict[PositionAndVelocity, SerializedPath[T]]]:
    result: dict[TilePosition, dict[PositionAndVelocity, SerializedPath[T]]] = {}
    for _ in range(reader.size()):
        tile = read_tile_position(reader)
        paths: dict[PositionAndVelocity, SerializedPath[T]] = {}
        for _ in range(reader.size()):
            serialized_path = SerializedPath(codec)
            serialized_path.read(reader)
            # (The root position is the key; like std::unordered_map::emplace_hint, duplicates are dropped)
            paths.setdefault(serialized_path.pos, serialized_path)
        result.setdefault(tile, paths)
    return result


def _write_paths[T: ArrivalData](writer: Writer,
                                 paths_by_tile: dict[TilePosition, dict[PositionAndVelocity, SerializedPath[T]]]
                                 ) -> None:
    writer.size(len(paths_by_tile))
    for tile, paths in paths_by_tile.items():
        write_tile_position(writer, tile)
        writer.size(len(paths))
        for serialized_path in paths.values():
            serialized_path.write(writer)


def _read_initial_split_rotation(reader: Reader) -> InitialSplitRotation:
    rotation = InitialSplitRotation()
    rotation.delay_frames = reader.u8()
    rotation.resend_frames = {reader.u16() for _ in range(reader.size())}
    rotation.gather_arrival_frame = reader.u16()
    rotation.gather_action_frame = reader.u16()
    rotation.return_arrival_frame = reader.u16()
    for _ in range(reader.size()):
        key = reader.u16()
        rotation.return_action_frames_to_occurrences.setdefault(key, reader.u8())
    return rotation


def _write_initial_split_rotation(writer: Writer, rotation: InitialSplitRotation) -> None:
    writer.u8(rotation.delay_frames)
    writer.size(len(rotation.resend_frames))
    for frame in sorted(rotation.resend_frames):
        writer.u16(frame)
    writer.u16(rotation.gather_arrival_frame)
    writer.u16(rotation.gather_action_frame)
    writer.u16(rotation.return_arrival_frame)
    writer.size(len(rotation.return_action_frames_to_occurrences))
    for frame, occurrences in sorted(rotation.return_action_frames_to_occurrences.items()):
        writer.u16(frame)
        writer.u8(occurrences)


def _read_initial_split_data(reader: Reader) -> InitialSplitDataByStartPosition:
    result: InitialSplitDataByStartPosition = {}
    for _ in range(reader.size()):
        start = read_position_and_velocity(reader)
        by_patch_pair: dict[tuple[TilePosition, TilePosition], InitialSplitData] = {}
        for _ in range(reader.size()):
            patch_pair = (read_tile_position(reader), read_tile_position(reader))
            data = InitialSplitData()
            data.first_rotation = _read_initial_split_rotation(reader)
            for _ in range(reader.size()):
                key = reader.u16()
                data.first_rotation_delivery_to_second_rotation.setdefault(key, _read_initial_split_rotation(reader))
            by_patch_pair.setdefault(patch_pair, data)
        result.setdefault(start, by_patch_pair)
    return result


def _write_initial_split_data(writer: Writer, initial_split_data: InitialSplitDataByStartPosition) -> None:
    writer.size(len(initial_split_data))
    for start, by_patch_pair in initial_split_data.items():
        write_position_and_velocity(writer, start)
        writer.size(len(by_patch_pair))
        for patch_pair, data in sorted(by_patch_pair.items()):
            write_tile_position(writer, patch_pair[0])
            write_tile_position(writer, patch_pair[1])
            _write_initial_split_rotation(writer, data.first_rotation)
            writer.size(len(data.first_rotation_delivery_to_second_rotation))
            for key, rotation in sorted(data.first_rotation_delivery_to_second_rotation.items()):
                writer.u16(key)
                _write_initial_split_rotation(writer, rotation)


def _read_pairs(reader: Reader, max_size: int, read_value: Callable[[], int]) -> list[tuple[int, int]]:
    return [(read_value(), read_value()) for _ in range(reader.size(max_size))]


def _deserialize(reader: Reader, data: MapData) -> None:
    data.position_deltas = _read_pairs(reader, 128, reader.i8)
    data.ten_distance_and_resend_always_arrives = _read_pairs(reader, 256, reader.u8)
    data.minimum_next_path_length = reader.u32()
    data.resource_to_serialized_gather_paths = _read_paths(reader, GATHER_CODEC)
    data.resource_to_serialized_return_paths = _read_paths(reader, RETURN_CODEC)
    data.start_location_to_patch_pair_to_initial_split_data_zerg = _read_initial_split_data(reader)
    data.start_location_to_patch_pair_to_initial_split_data_not_zerg = _read_initial_split_data(reader)
    data.start_location_to_patch_pair_to_initial_split_data_unknown = _read_initial_split_data(reader)

    data.resource_to_average_single_worker_rotation_time = {}
    for _ in range(reader.size()):
        tile = read_tile_position(reader)
        data.resource_to_average_single_worker_rotation_time.setdefault(tile, reader.u8())


def _serialize(writer: Writer, data: MapData) -> None:
    writer.size(len(data.position_deltas))
    for dx, dy in data.position_deltas:
        writer.i8(dx)
        writer.i8(dy)
    writer.size(len(data.ten_distance_and_resend_always_arrives))
    for ten_distance, resend_always_arrives in data.ten_distance_and_resend_always_arrives:
        writer.u8(ten_distance)
        writer.u8(resend_always_arrives)
    writer.u32(data.minimum_next_path_length)
    _write_paths(writer, data.resource_to_serialized_gather_paths)
    _write_paths(writer, data.resource_to_serialized_return_paths)
    _write_initial_split_data(writer, data.start_location_to_patch_pair_to_initial_split_data_zerg)
    _write_initial_split_data(writer, data.start_location_to_patch_pair_to_initial_split_data_not_zerg)
    _write_initial_split_data(writer, data.start_location_to_patch_pair_to_initial_split_data_unknown)

    writer.size(len(data.resource_to_average_single_worker_rotation_time))
    for tile, rotation_time in data.resource_to_average_single_worker_rotation_time.items():
        write_tile_position(writer, tile)
        writer.u8(rotation_time)


def set_game_parameters(map_hash: str) -> None:
    """Allows setting the game parameters if using the data outside a game (like when post-processing the data
    files)."""
    global _game_parameters_initialized, _map_hash
    _map_hash = map_hash
    _game_parameters_initialized = True


def read_map_data(data: MapData) -> None:
    _ensure_game_parameters_initialized()

    # Don't need to reload the data if the map hasn't changed
    if data.map_hash and data.map_hash == _map_hash:
        log.get("Using already-loaded mining optimization data")
        return

    data.clear(_map_hash)

    filename = _get_filename(False)
    if not filename:
        log.get("No saved mining optimization data available")
        return

    try:
        with open(filename, "rb") as file:
            compressed = file.read()
    except OSError:
        log.get(f"Could not open saved mining optimization data from {filename}")
        return

    try:
        _deserialize(Reader(zstd.decompress(compressed)), data)
    except (zstd.ZstdError, BitseryError) as error:
        log.get(f"ERROR: Could not read mining optimization data from {filename}: {error}")
        data.clear(_map_hash)
        return

    log.get(f"Read mining optimization data from {filename}")


def write_map_data(data: MapData) -> None:
    global _game_parameters_initialized, _map_hash
    _game_parameters_initialized = True
    _map_hash = data.map_hash

    filename = _get_filename(True)
    if not filename:
        log.get("ERROR: Could not generate filename for mining optimization data")
        return

    writer = Writer()
    _serialize(writer, data)
    try:
        with open(filename, "wb") as file:
            file.write(zstd.compress(bytes(writer.data), level=zstd.CompressionParameter.compression_level.bounds()[1]))
    except OSError:
        log.get(f"Could not open mining optimization data file for writing: {filename}")
        return

    log.get(f"Wrote mining optimization data to {filename}")

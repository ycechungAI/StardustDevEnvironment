"""Port of Workers/MiningOptimizationV2/DataModel/SerializedPath.{h,cpp}: a path root with its tree kept serialized
per cannon placement, deserialized on demand.

Stardust instantiates the class for GatherArrivalData and ReturnArrivalData; here the arrival data type is handled by
a codec (GATHER_CODEC or RETURN_CODEC).

Paths are serialized compactly: vectors of (item, occurrence rate) pairs have no size, but end when the occurrence
rates reach OCCURRENCE_SCALE or with a 0 rate. Final nodes (with no next positions) only store the arrival data;
other nodes store their next positions after a resend, and if there are none, the arrival data after a resend, where an
empty vector with the node's "stable resend node" flag set is written as a single 1.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Protocol

from stardust.units import units
from stardust.util.tile_position import TilePosition
from stardust.workers.mining_optimization_v2.data_model.bitsery import Reader, Writer
from stardust.workers.mining_optimization_v2.data_model.cannon_placement import CannonPlacement
from stardust.workers.mining_optimization_v2.data_model.gather_arrival_data import GatherArrivalData
from stardust.workers.mining_optimization_v2.data_model.path import ArrivalData, Path, PathNode
from stardust.workers.mining_optimization_v2.data_model.position_and_velocity import PositionAndVelocity
from stardust.workers.mining_optimization_v2.data_model.position_delta_and_velocity import PositionDeltaAndVelocity
from stardust.workers.mining_optimization_v2.data_model.return_arrival_data import ReturnArrivalData

OCCURRENCE_SCALE = 127


class ArrivalDataCodec[T: ArrivalData](Protocol):
    def read_final(self, reader: Reader) -> T: ...

    def read_resend(self, reader: Reader) -> T: ...

    def write_final(self, writer: Writer, item: T) -> None: ...

    def write_resend(self, writer: Writer, item: T) -> None: ...


class _GatherCodec:
    @staticmethod
    def read_final(reader: Reader) -> GatherArrivalData:
        data = GatherArrivalData()
        data.packed = reader.u8()
        data.resend_always_arrives_delta = reader.u8()
        return data

    @staticmethod
    def read_resend(reader: Reader) -> GatherArrivalData:
        data = GatherArrivalData()
        data.packed = reader.u8()
        data.ten_distance_and_resend_always_arrives_index = reader.u8()
        return data

    @staticmethod
    def write_final(writer: Writer, item: GatherArrivalData) -> None:
        writer.u8(item.packed)
        writer.u8(item.resend_always_arrives_delta)

    @staticmethod
    def write_resend(writer: Writer, item: GatherArrivalData) -> None:
        writer.u8(item.packed)
        writer.u8(item.ten_distance_and_resend_always_arrives_index)


class _ReturnCodec:
    @staticmethod
    def read_final(reader: Reader) -> ReturnArrivalData:
        return ReturnArrivalData(reader.u8(), reader.u8())

    read_resend = read_final

    @staticmethod
    def write_final(writer: Writer, item: ReturnArrivalData) -> None:
        writer.u8(item.packed_arrival_delay_and_collision)
        writer.u8(item.packed_exit_speed_and_facing_depot)

    write_resend = write_final


GATHER_CODEC: ArrivalDataCodec[GatherArrivalData] = _GatherCodec()
RETURN_CODEC: ArrivalDataCodec[ReturnArrivalData] = _ReturnCodec()


def read_position_and_velocity(reader: Reader) -> PositionAndVelocity:
    return PositionAndVelocity(reader.u16(), reader.u16(), reader.i8(), reader.i16(), reader.i16())


def write_position_and_velocity(writer: Writer, pos: PositionAndVelocity) -> None:
    writer.u16(pos.x)
    writer.u16(pos.y)
    writer.i8(pos.heading)
    writer.i16(pos.velocity_x)
    writer.i16(pos.velocity_y)


def read_tile_position(reader: Reader) -> TilePosition:
    return TilePosition(reader.u8(), reader.u8())


def write_tile_position(writer: Writer, tile: TilePosition) -> None:
    writer.u8(tile.x)
    writer.u8(tile.y)


def read_cannon_placement(reader: Reader) -> CannonPlacement:
    cannon_count = reader.u8()
    if cannon_count > 0:
        return CannonPlacement(cannon_count, read_tile_position(reader))
    return CannonPlacement(cannon_count)


def write_cannon_placement(writer: Writer, cannon_placement: CannonPlacement) -> None:
    writer.u8(cannon_placement.cannon_count)
    if cannon_placement.cannon_count > 0:
        write_tile_position(writer, cannon_placement.tile)


def _read_vector[U](reader: Reader, read_item: Callable[[], U],
                    set_packed_bool: Callable[[], None] | None = None) -> list[tuple[U, int]]:
    result: list[tuple[U, int]] = []
    total = 0
    while total < OCCURRENCE_SCALE:
        occurrence_rate = reader.u8()

        # A zero always indicates an empty vector
        if occurrence_rate == 0:
            return result

        # If there is a packed bool, a one in the first item indicates an empty vector with the packed bool set
        if set_packed_bool is not None and total == 0 and occurrence_rate == 1:
            set_packed_bool()
            return result

        result.append((read_item(), occurrence_rate))
        total = (total + occurrence_rate) & 0xFF
    return result


def _write_vector[U](writer: Writer, vector: list[tuple[U, int]], write_item: Callable[[U], None],
                     packed_bool: bool = False) -> None:
    # If the vector is empty, write zero or one depending on whether we want to pack a bool
    if not vector:
        writer.u8(1 if packed_bool else 0)
        return

    # Write the occurrences before the nodes
    for item, occurrence_rate in vector:
        writer.u8(occurrence_rate)
        write_item(item)


def _read_path[T: ArrivalData](reader: Reader, path: Path[T], codec: ArrivalDataCodec[T]) -> None:
    def read_node() -> PathNode[T]:
        node: PathNode[T] = PathNode()
        packed = reader.u8()
        if packed & 0b00000001:
            node.pos = PositionDeltaAndVelocity(packed, reader.i8(), reader.i16(), reader.i16())
        else:
            node.pos = PositionDeltaAndVelocity(packed)

        node.next_positions = _read_vector(reader, read_node)
        if not node.next_positions:
            node.arrival_data_when_final_node = _read_vector(reader, lambda: codec.read_final(reader))
        else:
            node.next_positions_after_resend = _read_vector(reader, read_node)
            if not node.next_positions_after_resend:
                def set_stable() -> None:
                    node.is_stable_resend_node = True

                node.arrival_data_after_resend = _read_vector(reader, lambda: codec.read_resend(reader), set_stable)
        return node

    path.next_positions = _read_vector(reader, read_node)


def _write_path[T: ArrivalData](writer: Writer, path: Path[T], codec: ArrivalDataCodec[T]) -> None:
    def write_node(node: PathNode[T]) -> None:
        writer.u8(node.pos.packed)
        if node.pos.requires_heading_and_velocity():
            writer.i8(node.pos.heading)
            writer.i16(node.pos.velocity_x)
            writer.i16(node.pos.velocity_y)

        _write_vector(writer, node.next_positions, write_node)
        if not node.next_positions:
            _write_vector(writer, node.arrival_data_when_final_node, lambda item: codec.write_final(writer, item))
        else:
            _write_vector(writer, node.next_positions_after_resend, write_node)
            if not node.next_positions_after_resend:
                _write_vector(writer, node.arrival_data_after_resend, lambda item: codec.write_resend(writer, item),
                              node.is_stable_resend_node)

    _write_vector(writer, path.next_positions, write_node)


class SerializedPath[T: ArrivalData]:
    __slots__ = ("pos", "codec", "data_by_cannon_placement")

    def __init__(self, codec: ArrivalDataCodec[T], pos: PositionAndVelocity | None = None) -> None:
        # The position, including velocity and heading
        self.pos = pos if pos is not None else PositionAndVelocity()
        self.codec = codec
        self.data_by_cannon_placement: list[tuple[CannonPlacement, bytes]] = []

    def active_cannon_placement(self) -> CannonPlacement | None:
        for cannon_placement, _ in self.data_by_cannon_placement:
            if (cannon_placement.cannon_count > 0
                    and units.my_building_at(cannon_placement.tile.to_bwapi()) is None):
                continue

            return cannon_placement

        return None

    def get(self, cannon_placement: CannonPlacement | None = None) -> Path[T]:
        if cannon_placement is None:
            cannon_placement = self.active_cannon_placement()
        if cannon_placement is None:
            return Path(self.pos)

        for placement, data in self.data_by_cannon_placement:
            if placement != cannon_placement:
                continue

            result: Path[T] = Path(self.pos)
            result.cannon_placement = placement
            _read_path(Reader(data), result, self.codec)
            return result

        # Shouldn't get here
        return Path(self.pos)

    @staticmethod
    def create(codec: ArrivalDataCodec[T], cannon_placement_to_path: dict[CannonPlacement, Path[T]]
               ) -> SerializedPath[T]:
        result = SerializedPath(codec)
        data_by_cannon_placement: list[tuple[CannonPlacement, bytes]] = []
        for cannon_placement in sorted(cannon_placement_to_path):
            path = cannon_placement_to_path[cannon_placement]
            result.pos = path.pos

            writer = Writer()
            _write_path(writer, path, codec)
            data_by_cannon_placement.append((cannon_placement, bytes(writer.data)))

        # (std::sort is not stable; equal cannon counts keep their order here)
        data_by_cannon_placement.sort(key=lambda item: item[0].cannon_count, reverse=True)

        # Remove any data vectors that are equivalent to a lower number of cannons
        index = 0
        while index < len(data_by_cannon_placement):
            placement, data = data_by_cannon_placement[index]
            are_equal = any(other_placement.cannon_count != placement.cannon_count and other_data == data
                            for other_placement, other_data in data_by_cannon_placement[index + 1:])
            if are_equal:
                del data_by_cannon_placement[index]
            else:
                index += 1

        result.data_by_cannon_placement = data_by_cannon_placement
        return result

    def read(self, reader: Reader) -> None:
        """Reads the serialized form (the position, then the data for each cannon placement)."""
        self.pos = read_position_and_velocity(reader)
        self.data_by_cannon_placement = []
        for _ in range(reader.size()):
            cannon_placement = read_cannon_placement(reader)
            self.data_by_cannon_placement.append((cannon_placement, reader.raw(reader.size())))

    def write(self, writer: Writer) -> None:
        write_position_and_velocity(writer, self.pos)
        writer.size(len(self.data_by_cannon_placement))
        for cannon_placement, data in self.data_by_cannon_placement:
            write_cannon_placement(writer, cannon_placement)
            writer.size(len(data))
            writer.raw(data)

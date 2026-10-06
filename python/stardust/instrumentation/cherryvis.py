"""Port of Instrumentation/CherryVis.{h,cpp}: replay annotations for the CherryVis viewer, in bwapi-data/write/cvis/.

C++ `CherryVis::log(unitId) << a << b` becomes `cherryvis.log(f"{a}{b}", unit_id)`.

Data files are JSON (zstd-compressed when verbose or partitioned by frame), written incrementally and indexed by
trace.json at game end.
"""

import enum
import json
import os
from collections.abc import Sequence
from enum import Enum
from typing import IO, Any

from compression import zstd

import bwapi
from stardust import config
from stardust.instrumentation import log as log_

_CVIS_DIR = "bwapi-data/write/cvis"
_FILE_EXTENSION = ".json.zstd" if config.INSTRUMENTATION_ENABLED_VERBOSE else ".json"
_PARTITIONED_FILE_EXTENSION = ".json.zstd"
_MAX_PART_SIZE = 26214400

_disabled = False


class DrawColor(enum.IntEnum):
    Black = 0
    Brown = 19
    Grey = 74
    Red = 111
    Green = 117
    Cyan = 128
    Yellow = 135
    Teal = 159
    Purple = 164
    Blue = 165
    Orange = -179
    White = 255


def _cvis_frame() -> int:
    return bwapi.Broodwar.getFrameCount()


def _dumps(value: Any) -> str:
    return json.dumps(value, separators=(",", ":"))


def _open_stream(path: str, compressed: bool) -> IO[str]:
    if compressed:
        return zstd.open(path, "wt")
    return open(path, "w")


class _DataFileType(Enum):
    ARRAY = 0
    ARRAY_PER_FRAME = 1
    OBJECT_PER_FRAME = 2


class _DataFile:
    def __init__(self, filename: str, file_type: _DataFileType, partitioned_object_size: int = 0,
                 frames_per_partition: int = 0) -> None:
        self.filename = filename
        self.type = file_type
        self.last_frame = -1
        self.partitioned_object_size = partitioned_object_size
        self.frames_per_partition = frames_per_partition
        self.current_partition = 0
        self.count = 0
        self.stream: IO[str] | None = None
        self.parts: list[tuple[int, str]] = []  # (first frame, filename)

    def is_partitioned(self) -> bool:
        return self.partitioned_object_size > 0 or self.frames_per_partition > 0

    def _partition_and_maybe_close_previous(self) -> int:
        partition = (_cvis_frame() // self.frames_per_partition) * self.frames_per_partition
        if partition != self.current_partition:
            self.close()
        return partition

    def index(self) -> dict[str, str]:
        return {str(first_frame): filename for first_frame, filename in self.parts}

    def write_entry(self, entry: Any) -> None:
        global _disabled
        try:
            if self.count * self.partitioned_object_size >= _MAX_PART_SIZE:
                self.close()

            if self.frames_per_partition > 0:
                self.current_partition = self._partition_and_maybe_close_previous()

            if self.count == 0:
                self._create_part()
            assert self.stream is not None
            if self.count > 0:
                frame = _cvis_frame()
                if self.type == _DataFileType.ARRAY:
                    self.stream.write(",")
                elif self.type == _DataFileType.ARRAY_PER_FRAME:
                    if self.last_frame == frame:
                        self.stream.write(",")
                    else:
                        self.stream.write(f'],"{frame}":[')
                        self.last_frame = frame
                else:
                    self.stream.write(f',"{frame}":')

            self.count += 1
            self.stream.write(_dumps(entry))
        except OSError as ex:
            log_.get(f"Exception caught in DataFile::writeEntry: {ex}")
            _disabled = True

    def close(self) -> None:
        global _disabled
        if self.count == 0 or self.stream is None:
            return
        try:
            if self.type == _DataFileType.ARRAY:
                self.stream.write("]")
            elif self.type == _DataFileType.ARRAY_PER_FRAME:
                self.stream.write("]}" if self.last_frame != -1 else "}")
            else:
                self.stream.write("}")
            self.stream.close()
            self.stream = None
            self.count = 0
        except OSError as ex:
            log_.get(f"Exception caught in DataFile::close: {ex}")
            _disabled = True

    def flush(self) -> None:
        if self.count == 0 or self.stream is None:
            return
        if self.frames_per_partition > 0:
            self._partition_and_maybe_close_previous()
            return
        self.stream.flush()

    def _create_part(self) -> None:
        global _disabled
        try:
            frame = _cvis_frame()
            start_frame = frame
            name = self.filename
            if self.partitioned_object_size > 0:
                name += f"_{frame}"
            if self.frames_per_partition > 0:
                start_frame = (frame // self.frames_per_partition) * self.frames_per_partition
                name += f"_{start_frame}"
                name += _PARTITIONED_FILE_EXTENSION
                self.stream = _open_stream(f"{_CVIS_DIR}/{name}", True)
            else:
                name += _FILE_EXTENSION
                self.stream = _open_stream(f"{_CVIS_DIR}/{name}", _FILE_EXTENSION.endswith(".zstd"))

            self.parts.append((start_frame, name))

            if self.type == _DataFileType.ARRAY:
                self.stream.write("[")
            elif self.type == _DataFileType.ARRAY_PER_FRAME:
                self.stream.write(f'{{"{frame}":[')
            else:
                self.stream.write(f'{{"{frame}":')
        except OSError as ex:
            log_.get(f"Exception caught in DataFile::createPart: {ex}")
            _disabled = True


_board_updates_file: _DataFile | None = None
_frame_board_updates: dict[str, str] = {}
_board_key_to_last_value: dict[str, str] = {}
_board_list_to_last_count: dict[str, int] = {}
_unit_ids: list[tuple[int, int]] = []
_frame_to_units_first_seen: dict[str, list[dict[str, int]]] = {}
_unit_id_to_frame_to_unit_update: dict[str, dict[str, dict[str, int]]] = {}
_unit_id_to_log_file: dict[int, _DataFile] = {}
_unit_id_to_draw_commands_file: dict[int, _DataFile] = {}
_label_to_data_file: dict[str, _DataFile] = {}
_heatmap_name_to_data_file: dict[str, _DataFile] = {}


def _enabled() -> bool:
    return config.CHERRYVIS_ENABLED and not _disabled


def _write_log(message: str, unit_id: int) -> None:
    log_file = _unit_id_to_log_file.get(unit_id)
    if log_file is None:
        name = "logs" if unit_id == -1 else f"logs_{unit_id}"
        log_file = _unit_id_to_log_file[unit_id] = _DataFile(name, _DataFileType.ARRAY)
    log_file.write_entry({"message": message, "frame": _cvis_frame()})


def _draw(command: dict[str, Any], unit_id: int) -> None:
    draw_file = _unit_id_to_draw_commands_file.get(unit_id)
    if draw_file is None:
        name = "drawCommands_all" if unit_id == -1 else f"drawCommands_{unit_id}"
        draw_file = _unit_id_to_draw_commands_file[unit_id] = _DataFile(
            name, _DataFileType.ARRAY_PER_FRAME, 0, 5000 if unit_id == -1 else 0)
    draw_file.write_entry(command)


def initialize() -> None:
    global _board_updates_file
    if not _enabled():
        return

    _board_updates_file = _DataFile("board_updates", _DataFileType.OBJECT_PER_FRAME)
    _frame_board_updates.clear()
    _board_key_to_last_value.clear()
    _board_list_to_last_count.clear()
    _unit_ids.clear()
    _frame_to_units_first_seen.clear()
    _unit_id_to_frame_to_unit_update.clear()
    _unit_id_to_log_file.clear()
    _unit_id_to_draw_commands_file.clear()
    _label_to_data_file.clear()
    _heatmap_name_to_data_file.clear()

    os.makedirs(_CVIS_DIR, exist_ok=True)

    # Mark all static neutrals as seen
    for unit in bwapi.Broodwar.getStaticNeutralUnits():
        unit_first_seen(unit)

    log("CherryVis initialized!")


def set_board_value(key: str, value: str) -> None:
    if not _enabled():
        return
    if _board_key_to_last_value.get(key) != value:
        _board_key_to_last_value[key] = value
        _frame_board_updates[key] = value


def set_board_list_value(key: str, values: Sequence[str]) -> None:
    if not _enabled():
        return
    limit = max(_board_list_to_last_count.get(key, 0), len(values))
    for i in range(1, limit + 1):
        set_board_value(f"{key}_{i:03d}", values[i - 1] if i <= len(values) else "")
    _board_list_to_last_count[key] = len(values)


def unit_first_seen(unit: bwapi.Unit) -> None:
    if not _enabled():
        return
    frame = _cvis_frame() or 1
    if config.IS_OPENBW:
        _unit_ids.append((unit.getID(), unit.getBWID()))
    position = unit.getPosition()
    _frame_to_units_first_seen.setdefault(str(frame), []).append(
        {"id": unit.getID(), "type": unit.getType().getID(), "x": position.x, "y": position.y})
    _unit_id_to_frame_to_unit_update.setdefault(str(unit.getID()), {})[str(frame)] = {"type": unit.getType().getID()}


def log(message: str, unit_id: int = -1) -> None:
    """Log a message, globally (-1) or against a unit id (CherryVis::log(unitId) << message)."""
    if not _enabled():
        return
    _write_log(message, unit_id)


def add_heatmap(key: str, data: Sequence[int], size_x: int, size_y: int) -> None:
    """Add a heatmap for this frame. data is row-major: data[x + y * size_x]."""
    if not _enabled():
        return
    count = len(data)
    maximum = max(0, *data) if count else 0
    minimum = min(data) if count else 0
    mean = sum(data) / count if count else 0.0
    std = (sum((value - mean) ** 2 for value in data) / count) ** 0.5 if count else 0.0
    game = bwapi.Broodwar
    frame_data = {
        "data": list(data),
        "dimension": [size_y, size_x],
        "scaling": [(game.mapHeight() * 32.0) / size_y, (game.mapWidth() * 32.0) / size_x],
        "top_left_pixel": [0, 0],
        "summary": {
            "hist": {"max": maximum, "min": minimum, "num_buckets": 1, "values": [0]},
            "max": maximum,
            "min": minimum,
            "mean": mean,
            "median": 0,
            "name": key,
            "shape": [size_y, size_x],
            "std": std,
        },
    }
    heatmap = _heatmap_name_to_data_file.get(key)
    if heatmap is None:
        heatmap = _heatmap_name_to_data_file[key] = _DataFile(
            f"heatmap_{key}", _DataFileType.OBJECT_PER_FRAME, size_x * size_y)
    heatmap.write_entry(frame_data)


def draw_line(x1: int, y1: int, x2: int, y2: int, color: DrawColor, unit_id: int = -1) -> None:
    if not _enabled():
        return
    _draw({"code": 20, "args": [x1, y1, x2, y2, int(color)], "str": "."}, unit_id)


def draw_circle(x: int, y: int, radius: int, color: DrawColor, unit_id: int = -1) -> None:
    if not _enabled():
        return
    _draw({"code": 23, "args": [x, y, radius, int(color)], "str": "."}, unit_id)


def draw_text(x: int, y: int, text: str, unit_id: int = -1) -> None:
    if not _enabled():
        return
    _draw({"code": 25, "args": [x, y], "str": text}, unit_id)


def write_frame_data(label: str, entry: Any, frames_per_partition: int = 0) -> None:
    """Write a JSON-serializable entry for this frame to the data file with the given label."""
    if not _enabled():
        return
    data_file = _label_to_data_file.get(label)
    if data_file is None:
        data_file = _label_to_data_file[label] = _DataFile(
            label, _DataFileType.OBJECT_PER_FRAME, 0, frames_per_partition)
    data_file.write_entry(entry)


def frame_end() -> None:
    if not _enabled() or _board_updates_file is None:
        return

    if _frame_board_updates:
        _board_updates_file.write_entry(dict(_frame_board_updates))
        _frame_board_updates.clear()

    # Flush data files every 500 frames
    if _cvis_frame() % 500 == 0:
        files: list[_DataFile] = [
            *_heatmap_name_to_data_file.values(),
            _board_updates_file,
            *_unit_id_to_draw_commands_file.values(),
            *_unit_id_to_log_file.values(),
            *_label_to_data_file.values(),
        ]
        for data_file in files:
            data_file.flush()


def game_end() -> None:
    if not _enabled() or _board_updates_file is None:
        return

    heatmaps = []
    for name, heatmap_file in _heatmap_name_to_data_file.items():
        heatmap_file.close()
        for first_frame, filename in heatmap_file.parts:
            heatmaps.append({"filename": filename, "first_frame": first_frame, "name": f"{name}_{first_frame}"})

    trace: dict[str, Any] = {
        "units_updates": _unit_id_to_frame_to_unit_update,
        "units_logs": {},
        "units_draw": {},
        "heatmaps": heatmaps,
    }

    _board_updates_file.close()
    trace["board_updates"] = _board_updates_file.parts[0][1] if _board_updates_file.parts else ""

    if config.IS_OPENBW:
        trace["unit_ids"] = _unit_ids
    trace["units_first_seen"] = _frame_to_units_first_seen

    trace["draw_commands"] = []
    units_draw: dict[str, str] = {}
    for unit_id, draw_file in sorted(_unit_id_to_draw_commands_file.items()):
        if unit_id == -1:
            trace["draw_commands"] = draw_file.index()
        else:
            units_draw[str(unit_id)] = draw_file.parts[0][1]
        draw_file.close()
    trace["units_draw"] = units_draw

    trace["logs"] = []
    units_logs: dict[str, str] = {}
    for unit_id, log_file in sorted(_unit_id_to_log_file.items()):
        if unit_id == -1:
            trace["logs"] = log_file.parts[0][1]
        else:
            units_logs[str(unit_id)] = log_file.parts[0][1]
        log_file.close()
    trace["units_logs"] = units_logs

    for label, data_file in sorted(_label_to_data_file.items()):
        trace[label] = data_file.index() if data_file.is_partitioned() else data_file.parts[0][1]
        data_file.close()

    try:
        with _open_stream(f"{_CVIS_DIR}/trace{_FILE_EXTENSION}", _FILE_EXTENSION.endswith(".zstd")) as trace_file:
            trace_file.write(_dumps(trace))
    except OSError as ex:
        log_.get(f"Exception caught writing trace file: {ex}")


def disable() -> None:
    global _disabled
    _disabled = True

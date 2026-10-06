"""Port of Instrumentation/Log.{h,cpp}: the game log, debug log and CSV files in bwapi-data/write/.

C++ `Log::Get() << a << b` becomes `log.get(f"{a}{b}")`. Debug and CSV output are only written when debug logging is
both compiled in (config.DEBUG_LOGGING_ENABLED) and switched on (set_debug); guard expensive formatting with
`if log.is_debug():`.
"""

import datetime
import os
from typing import TextIO

from stardust import common, config

_WRITE_DIR = "bwapi-data/write"

_output_to_console = False
_output_to_log_file = True
_start_time = datetime.datetime.now()
_log: TextIO | None = None
_debug_logging = False
_debug_log: TextIO | None = None
_csv_files: dict[str, TextIO] = {}
_log_files: list[str] = []


def _open(base: str, csv: bool = False) -> TextIO:
    filename = f"{_WRITE_DIR}/{base}_{_start_time:%Y%m%d_%H%M%S}{'.csv' if csv else '.txt'}"
    _log_files.append(filename)
    os.makedirs(_WRITE_DIR, exist_ok=True)
    # Line buffered: Stardust flushes every line so logs survive crashes
    return open(filename, "w", buffering=1)


def _prefix() -> str:
    seconds = common.current_frame // 24
    minutes, seconds = divmod(seconds, 60)
    return f"{common.current_frame}({minutes}:{seconds:02d}): "


def initialize() -> None:
    global _start_time, _log, _debug_logging, _debug_log
    _start_time = datetime.datetime.now()
    for file in [_log, _debug_log, *_csv_files.values()]:
        if file:
            try:
                file.close()
            except OSError:
                pass
    _log = None
    _debug_log = None
    _debug_logging = False
    _csv_files.clear()
    _log_files.clear()


def set_debug(debug: bool) -> None:
    global _debug_logging
    _debug_logging = debug


def set_output_to_console(output_to_console: bool) -> None:
    global _output_to_console
    _output_to_console = output_to_console


def set_output_to_log_file(output_to_log_file: bool) -> None:
    global _output_to_log_file
    _output_to_log_file = output_to_log_file


def get(message: str) -> None:
    """Write a line to the main log (Log::Get())."""
    global _log
    if not config.LOGGING_ENABLED:
        return
    if _log is None and _output_to_log_file:
        _log = _open("Stardust_log")
    if _log is None and not _output_to_console:
        return

    line = _prefix() + message
    if _output_to_console:
        print(line)
    if _log is not None:
        _log.write(line + "\n")


def is_debug() -> bool:
    """Whether debug() and csv() write anything."""
    return config.DEBUG_LOGGING_ENABLED and _debug_logging


def debug(message: str) -> None:
    """Write a line to the debug log (Log::Debug())."""
    global _debug_log
    if not is_debug():
        return
    if _debug_log is None and _output_to_log_file:
        _debug_log = _open("Stardust_debug")
    if _debug_log is not None:
        _debug_log.write(_prefix() + message + "\n")


def csv(name: str, *values: object) -> None:
    """Write a comma-separated row to bwapi-data/write/<name>_<timestamp>.csv (Log::Csv())."""
    if not is_debug():
        return
    file = _csv_files.get(name)
    if file is None:
        file = _csv_files[name] = _open(name, csv=True)
    file.write(",".join(str(value) for value in values) + "\n")


def log_files() -> list[str]:
    """Paths of all the log files written in this game."""
    return _log_files

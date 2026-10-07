"""Stardust instrumentation: file logging and CherryVis replay annotations.

Outside a game (offline tests) Log.write prints to stdout and the CherryVis functions do nothing.
"""

from typing import overload

from bwapi import Unit

class Log:
    @staticmethod
    def initialize() -> None: ...
    @staticmethod
    def setOutputToConsole(outputToConsole: bool) -> None: ...
    @staticmethod
    def write(message: str) -> None:
        """Append a line, prefixed with the frame number and game time, to bwapi-data/write/Stardust_log_*.txt."""
    @staticmethod
    def logFileName() -> str: ...

class CherryVis:
    @staticmethod
    def initialize() -> None: ...
    @staticmethod
    def setBoardValue(key: str, value: str) -> None: ...
    @staticmethod
    def setBoardListValue(key: str, values: list[str]) -> None: ...
    @staticmethod
    def unitFirstSeen(unit: Unit) -> None:
        """Register a unit so CherryVis can link it to the replay; call once per unit."""
    @overload
    @staticmethod
    def log(message: str, unitId: int = -1) -> None:
        """Log a message globally (-1) or against a unit id."""
    @overload
    @staticmethod
    def log(message: str, unit: Unit) -> None: ...
    @staticmethod
    def addHeatmap(key: str, data: list[int], sizeX: int, sizeY: int) -> None:
        """Add a heatmap for this frame; data is row-major, data[x + y * sizeX]."""
    @staticmethod
    def frameEnd(frame: int) -> None: ...
    @staticmethod
    def gameEnd() -> None: ...

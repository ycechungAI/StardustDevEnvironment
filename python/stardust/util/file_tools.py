"""Port of Util/FileTools.{h,cpp}: locating data files in bwapi-data."""

import os

_DATA_LOAD_PATHS = ("bwapi-data/read/", "bwapi-data/write/", "bwapi-data/AI/")
_DATA_WRITE_PATH = "bwapi-data/write/"


def get_file_path(label: str, file_type: str, writing: bool = False) -> str:
    """Path to the file with the given label: in the write folder if writing, otherwise the first of the read, write
    and AI folders that has it (or "" if none do)."""
    if writing:
        return f"{_DATA_WRITE_PATH}{label}.{file_type}"
    for path in _DATA_LOAD_PATHS:
        filename = f"{path}{label}.{file_type}"
        if os.path.exists(filename):
            return filename
    return ""

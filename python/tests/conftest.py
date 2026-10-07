"""pytest runs against the offline `bwapi` / `instrumentation` extension modules built by CMake."""

import os
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
BUILD_DIR = Path(os.environ.get("STARDUST_BUILD_DIR", REPO / "build"))
sys.path.insert(0, str(BUILD_DIR / "python"))

try:
    import bwapi  # noqa: F401
except ImportError as e:
    pytest.exit(
        f"Could not import the offline bwapi module from {BUILD_DIR / 'python'} ({e}).\n"
        "Build it with: cmake --build build --target bwapi_offline instrumentation_offline\n"
        "(or set STARDUST_BUILD_DIR to your CMake build directory)",
        returncode=1,
    )

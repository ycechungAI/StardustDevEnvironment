"""Port of Instrumentation/Timer.{h,cpp}: logs frames (or startup) that take too long, with the slowest checkpoints."""

import time

from stardust import config
from stardust.instrumentation import log

# Stardust's thresholds (ms) depend on the build type; Python is slower, so use the instrumented thresholds
if config.INSTRUMENTATION_ENABLED_VERBOSE:
    LOG_CUTOFF = 1000
    DEBUG_CUTOFF = 1000
elif config.INSTRUMENTATION_ENABLED:
    LOG_CUTOFF = 100
    DEBUG_CUTOFF = 100
else:
    LOG_CUTOFF = 55
    DEBUG_CUTOFF = 45

_overall_label = ""
_start_point = 0
_last_checkpoint = 0
_checkpoints: list[tuple[str, int]] = []  # (label, microseconds)


def start(label: str) -> None:
    global _overall_label, _start_point, _last_checkpoint
    _overall_label = label
    _checkpoints.clear()
    _start_point = _last_checkpoint = time.perf_counter_ns()


def checkpoint(label: str) -> None:
    global _last_checkpoint
    now = time.perf_counter_ns()
    _checkpoints.append((label, (now - _last_checkpoint) // 1000))
    _last_checkpoint = now


def stop(force_output: bool = False) -> None:
    overall = (time.perf_counter_ns() - _start_point) // 1_000_000
    if not force_output and overall <= DEBUG_CUTOFF:
        return

    msg = f"{_overall_label} took {overall}ms"
    slowest = [c for c in sorted(_checkpoints, key=lambda c: c[1], reverse=True) if c[1] >= DEBUG_CUTOFF // 5]
    if slowest:
        msg += ", longest: " + ", ".join(f"{label}: {us}us" for label, us in slowest)

    log.debug(msg)
    if force_output or overall > LOG_CUTOFF:
        log.get(msg)

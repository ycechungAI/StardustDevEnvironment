"""Runs game tests with an up-front time estimate and periodic progress lines.

Usage: python tools/run_games.py <gtest filter> [--games N] [--interval SECONDS] [--build DIR]

The test binary's own output goes to <build>/test/run_games.log. Progress comes from the bot's log
(bwapi-data/write/Stardust_log_*.txt), whose lines start with the frame number, and from the replays
written when each game ends.
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# From the first games of the Python port (Oct 2026): ~13.7k frames in ~5:15 of wall time.
TYPICAL_FRAMES = 13_700
TYPICAL_SECONDS_PER_GAME = 315
FRAME_LIMIT = 30_000  # BWTest's default frame limit
TIME_LIMIT = 600  # BWTest's default wall-time limit per game, in seconds


def fmt(seconds: float) -> str:
    seconds = max(0, int(seconds))
    return f"{seconds // 60}:{seconds % 60:02d}"


def game_time(frame: int) -> str:
    # Matches the bot log's mm:ss: 24 frames per game second
    return fmt(frame / 24)


def last_frame(log: Path) -> int:
    try:
        with log.open("rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 4096))
            lines = f.read().decode(errors="replace").splitlines()
    except OSError:
        return 0
    for line in reversed(lines):
        match = re.match(r"(\d+)\(", line)
        if match:
            return int(match.group(1))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("filter")
    parser.add_argument("--games", type=int, default=0,
                        help="number of games the filter runs (default: 20 for *RunTwenty, else 1)")
    parser.add_argument("--interval", type=float, default=30)
    parser.add_argument("--build", default="build",
                        help="build directory, e.g. build-ui for the one with the live game window")
    args = parser.parse_args()
    test_dir = ROOT / args.build / "test"
    games = args.games or (20 if "RunTwenty" in args.filter else 1)

    estimate = games * TYPICAL_SECONDS_PER_GAME
    worst = games * TIME_LIMIT
    print(f"START {args.filter}: {games} game(s), estimated {fmt(estimate)} "
          f"(at most {fmt(worst)} with the {TIME_LIMIT // 60}-minute limit per game)", flush=True)

    start = time.time()
    logs_dir = test_dir / "bwapi-data" / "write"
    replays_dir = test_dir / "replays"
    existing_logs = set(logs_dir.glob("Stardust_log_*.txt"))
    existing_replays = set(replays_dir.glob("*.rep"))

    output = (test_dir / "run_games.log").open("w")
    process = subprocess.Popen(["./tests", f"--gtest_filter={args.filter}"], cwd=test_dir,
                               stdout=output, stderr=subprocess.STDOUT)

    next_report = start + args.interval
    reported_replays: set[Path] = set()
    while process.poll() is None:
        time.sleep(1)

        for replay in sorted(set(replays_dir.glob("*.rep")) - existing_replays - reported_replays):
            reported_replays.add(replay)
            result = "won" if "_PASS" in replay.name else "lost/failed"
            print(f"GAME {len(reported_replays)}/{games} {result} at {fmt(time.time() - start)} elapsed "
                  f"({replay.name})", flush=True)

        if time.time() < next_report:
            continue
        next_report += args.interval

        elapsed = time.time() - start
        done = len(reported_replays)
        new_logs = sorted(set(logs_dir.glob("Stardust_log_*.txt")) - existing_logs, key=lambda p: p.stat().st_mtime)
        frame = last_frame(new_logs[-1]) if new_logs else 0

        # Time per finished game, else the typical one. The current game is assumed to run to the typical length at
        # the pace it has had since its first frame (startup excluded), capped by the per-game time limit.
        current_elapsed = _current_game_elapsed(new_logs, elapsed)
        per_game = (elapsed - current_elapsed) / done if done else TYPICAL_SECONDS_PER_GAME
        rate = frame / current_elapsed if frame and current_elapsed > 0 else 0.0
        if rate > 0:
            current_left = max(0.0, TYPICAL_FRAMES - frame) / rate
        else:
            current_left = max(0.0, per_game - current_elapsed)
        current_left = min(current_left, max(0.0, TIME_LIMIT - current_elapsed))
        left = current_left + per_game * max(0, games - done - 1)
        if frame > TYPICAL_FRAMES:
            note = f", longer than typical (frame limit {FRAME_LIMIT})"
        else:
            note = ""
        pace = f", {rate:.0f} frames/s" if rate > 0 else ""
        print(f"PROGRESS {fmt(elapsed)} elapsed | game {min(done + 1, games)}/{games} at frame {frame} "
              f"({game_time(frame)} game time{pace}) | ~{fmt(left)} left{note}", flush=True)

    output.close()
    elapsed = time.time() - start
    text = (test_dir / "run_games.log").read_text(errors="replace")
    python_errors = text.count("Python error")
    summary = re.search(r"\[  (PASSED|FAILED)  \].*", text)
    timing = re.findall(r"Python onFrame:.*", text)
    print(f"DONE in {fmt(elapsed)} (exit {process.returncode}): {summary.group(0) if summary else 'no gtest summary'}; "
          f"Python errors: {python_errors}", flush=True)
    for line in timing:
        print(f"  {line}", flush=True)
    return process.returncode


def _current_game_elapsed(new_logs: list[Path], elapsed: float) -> float:
    """Wall time since the current game's bot log was created."""
    if not new_logs:
        return elapsed
    return max(0.0, time.time() - new_logs[-1].stat().st_birthtime) if hasattr(new_logs[-1].stat(), "st_birthtime") \
        else elapsed


if __name__ == "__main__":
    sys.exit(main())

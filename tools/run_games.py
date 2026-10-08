"""Runs game tests with an up-front time estimate and periodic progress lines, several games at a time.

Usage: python tools/run_games.py <gtest filter> [--games N] [--parallel N] [--interval SECONDS] [--build DIR]
       python tools/run_games.py --opponent <bot> [--games N] ...   (plays Bots.Play against a bot from bots/)
       python tools/run_games.py --bot Stardust2025 --opponent <bot> ... (plays as that bot instead of the Python port)

Headless games are as fast as the bots' own computation allows, so more games per minute comes from running several
at once: by default up to 4 (--parallel), each in its own folder under <build>/test/parallel/<n>/ with its own
bwapi-data/write and OpenBW connection directory, sharing the maps, data files and replays folder. The window build
(--build build-ui) runs one game at a time.

Each worker's test output goes to its run_games.log (<build>/test/run_games.log when running one at a time).
Progress comes from the bot's log (bwapi-data/write/Stardust_log_*.txt), whose lines start with the frame number,
and from the replays written when each game ends.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path

import elo  # tools/elo.py

ROOT = Path(__file__).resolve().parent.parent

# Typical game length for the Python port; wall time per game varies with the opponent (about 1-3 minutes headless).
TYPICAL_FRAMES = 13_700
TYPICAL_SECONDS_PER_GAME = 150
FRAME_LIMIT = 30_000  # BWTest's default frame limit
TIME_LIMIT = 600  # BWTest's default wall-time limit per game, in seconds
DEFAULT_PARALLEL = 4


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


def created(path: Path) -> float:
    stat = path.stat()
    return getattr(stat, "st_birthtime", stat.st_mtime)


@dataclass
class Worker:
    directory: Path
    games: int
    process: subprocess.Popen[bytes] | None = None
    log_path: Path = field(default_factory=Path)
    existing_logs: set[Path] = field(default_factory=set)

    def current_game(self) -> tuple[int, float] | None:
        """(frame, seconds since it started) of the game this worker is playing, from its newest bot log."""
        logs = set((self.directory / "bwapi-data" / "write").glob("Stardust_log_*.txt")) - self.existing_logs
        if not logs:
            return None
        newest = max(logs, key=created)
        return last_frame(newest), max(0.0, time.time() - created(newest))


def prepare_worker_directory(test_dir: Path, index: int) -> Path:
    """A working folder for one parallel worker: shared inputs are linked, bwapi-data/write is its own."""
    directory = test_dir / "parallel" / str(index)
    (directory / "bwapi-data" / "write").mkdir(parents=True, exist_ok=True)
    links = {directory / "maps": test_dir / "maps",
             directory / "replays": test_dir / "replays",
             directory / "bwapi-data" / "AI": test_dir / "bwapi-data" / "AI",
             directory / "bwapi-data" / "read": test_dir / "bwapi-data" / "read"}
    links.update({directory / mpq.name: mpq for mpq in test_dir.glob("*.[mM][pP][qQ]")})
    (test_dir / "replays").mkdir(exist_ok=True)
    (test_dir / "bwapi-data" / "read").mkdir(parents=True, exist_ok=True)
    for link, target in links.items():
        if link.is_symlink() or link.exists():
            if link.resolve() == target.resolve():
                continue
            link.unlink() if link.is_symlink() or link.is_file() else shutil.rmtree(link)
        link.symlink_to(target)
    return directory


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("filter", nargs="?", help="gtest filter (default Bots.Play with --opponent)")
    parser.add_argument("--opponent", help="bot to play with Bots.Play (see bots/README.md)")
    parser.add_argument("--bot", help="bot to play as with --opponent, instead of the Python port "
                                      "(e.g. Stardust2025, the original C++ Stardust)")
    parser.add_argument("--games", type=int, default=0,
                        help="number of games (default: 20 for *RunTwenty, else 1)")
    parser.add_argument("--parallel", type=int, default=0,
                        help=f"games to run at once (default: up to {DEFAULT_PARALLEL}; 1 for the window build)")
    parser.add_argument("--interval", type=float, default=30)
    parser.add_argument("--build", default="build",
                        help="build directory, e.g. build-ui for the one with the live game window")
    args = parser.parse_args()
    test_dir = ROOT / args.build / "test"

    if args.bot and not args.opponent:
        parser.error("--bot needs --opponent")
    if args.opponent:
        args.filter = args.filter or "Bots.Play"
    if not args.filter:
        parser.error("give a gtest filter or --opponent")
    games = max(1, args.games or (20 if "RunTwenty" in args.filter else 1))

    # Tests that loop over many games internally can't be split between workers
    splittable = args.opponent is not None or "RunTwenty" not in args.filter
    window_build = Path(args.build).name.endswith("-ui")  # build-ui; a plain "in" would match "build" itself
    parallel = args.parallel or (1 if window_build else DEFAULT_PARALLEL)
    parallel = max(1, min(parallel, games if splittable else 1))

    estimate = -(-games // parallel) * TYPICAL_SECONDS_PER_GAME
    worst = -(-games // parallel) * TIME_LIMIT
    print(f"START {args.filter}: {games} game(s), {parallel} at a time, estimated {fmt(estimate)} "
          f"(at most {fmt(worst)} with the {TIME_LIMIT // 60}-minute limit per game)", flush=True)

    # Split the games between the workers
    workers: list[Worker] = []
    for index in range(parallel):
        share = games // parallel + (1 if index < games % parallel else 0)
        directory = test_dir if parallel == 1 else prepare_worker_directory(test_dir, index)
        workers.append(Worker(directory, share))

    start = time.time()
    replays_dir = test_dir / "replays"
    existing_replays = set(replays_dir.glob("*.rep"))
    socket_root = Path(tempfile.mkdtemp(prefix="ob", dir="/tmp")) if parallel > 1 else None
    for index, worker in enumerate(workers):
        env = dict(os.environ)
        command = [str(test_dir / "tests"), f"--gtest_filter={args.filter}"]
        if args.opponent:
            env["STARDUST_OPPONENT"] = args.opponent
            env["STARDUST_GAMES"] = str(worker.games)
            if args.bot:
                env["STARDUST_BOT"] = args.bot
        elif splittable and worker.games > 1:
            command.append(f"--gtest_repeat={worker.games}")
        if socket_root is not None:
            # OpenBW finds the other player through sockets in this directory (default /tmp/openbw, shared by all
            # games); each worker's two processes need their own, or games connect to each other.
            env["OPENBW_LOCAL_AUTO_DIRECTORY"] = str(socket_root / str(index))
        worker.existing_logs = set((worker.directory / "bwapi-data" / "write").glob("Stardust_log_*.txt"))
        worker.log_path = worker.directory / "run_games.log"
        with worker.log_path.open("w") as output:
            worker.process = subprocess.Popen(command, cwd=worker.directory, env=env, stdout=output,
                                              stderr=subprocess.STDOUT)

    next_report = start + args.interval
    reported_replays: set[Path] = set()
    results = {"won": 0, "lost": 0, "passed": 0, "failed": 0}
    while any(worker.process is not None and worker.process.poll() is None for worker in workers):
        time.sleep(1)

        for replay in sorted(set(replays_dir.glob("*.rep")) - existing_replays - reported_replays):
            # Replays saved mid-game with [r] in the game window aren't finished games
            if re.search(r"_frame\d+_", replay.name):
                existing_replays.add(replay)
                continue
            reported_replays.add(replay)
            if "_WON" in replay.name or "_LOST" in replay.name:
                result = "won" if "_WON" in replay.name else "lost"
            else:
                result = "passed" if "_PASS" in replay.name else "failed"
            results[result] += 1
            print(f"GAME {len(reported_replays)}/{games} {result} at {fmt(time.time() - start)} elapsed "
                  f"({replay.name})", flush=True)

            # The harness records the game in replays/results.csv just before saving its replay
            _, rated = elo.update(replays_dir)
            for game in rated:
                if game.row.get("replay") == replay.name:
                    print(f"  ELO {elo.game_line(game)}", flush=True)

        if time.time() < next_report:
            continue
        next_report += args.interval

        elapsed = time.time() - start
        done = len(reported_replays)
        running = [game for worker in workers
                   if worker.process is not None and worker.process.poll() is None
                   for game in [worker.current_game()] if game is not None]

        # Each running game is assumed to reach the typical length at its pace so far (capped by the time limit);
        # games not started yet take the average time of finished games (or the typical one), shared by the workers
        per_game = elapsed * parallel / done if done else TYPICAL_SECONDS_PER_GAME
        current_left = 0.0
        for frame, game_elapsed in running:
            rate = frame / game_elapsed if frame and game_elapsed > 0 else 0.0
            game_left = max(0.0, TYPICAL_FRAMES - frame) / rate if rate > 0 else max(0.0, per_game - game_elapsed)
            current_left = max(current_left, min(game_left, max(0.0, TIME_LIMIT - game_elapsed)))
        not_started = max(0, games - done - len(running))
        left = current_left + -(-not_started // parallel) * per_game

        frames = ", ".join(f"{frame} ({game_time(frame)})" for frame, _ in running) or "starting"
        long_game = any(frame > TYPICAL_FRAMES for frame, _ in running)
        note = f", a game is longer than typical (frame limit {FRAME_LIMIT})" if long_game else ""
        print(f"PROGRESS {fmt(elapsed)} elapsed | {done}/{games} done | playing at frame {frames} | "
              f"~{fmt(left)} left{note}", flush=True)

    elapsed = time.time() - start
    if socket_root is not None:
        shutil.rmtree(socket_root, ignore_errors=True)

    texts = [worker.log_path.read_text(errors="replace") for worker in workers]
    python_errors = sum(text.count("Python error") for text in texts)
    failed = [text for text in texts if re.search(r"\[  FAILED  \]", text)]
    tally = ", ".join(f"{count} {name}" for name, count in results.items() if count)
    exit_code = max((worker.process.returncode or 0) if worker.process else 1 for worker in workers)
    print(f"DONE in {fmt(elapsed)} (exit {exit_code}): {len(reported_replays)} game(s): {tally or 'no replays'}; "
          f"{len(failed)} worker(s) with failed tests; Python errors: {python_errors}", flush=True)
    for text in texts:
        for line in re.findall(r"STATS .*|Python onFrame:.*", text):
            print(f"  {line}", flush=True)
    records, _ = elo.update(replays_dir)
    if records:
        print("\n" + elo.leaderboard(records), flush=True)
    if parallel > 1:
        print(f"  logs: {', '.join(str(worker.log_path.relative_to(ROOT)) for worker in workers)}", flush=True)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())

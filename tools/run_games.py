"""Runs game tests several at a time, each in its own window unless --ui none is given, with a moving progress line,
a time estimate and a live stats page.

Usage: python tools/run_games.py <gtest filter> [--games N] [--ui none|1-6] [--build DIR]
       python tools/run_games.py --opponent <bot> [--games N] ...   (plays Bots.Play against a bot from bots/)
       python tools/run_games.py --bot Stardust2025 --opponent <bot> ... (plays as that bot instead of the Python port)

--ui N plays up to N games at once (1 to 6, default 4), each in its own 640x480 OpenBW window, tiled on the screen:
6 as 3 columns x 2 rows, 4 as 2 x 2, 2 side by side, and smaller on a screen too small for them. The games come from
the window build, build-ui (cmake -B build-ui -DOPENBW_ENABLE_UI=ON -DCMAKE_BUILD_TYPE=Release). --ui none plays them
headless from build, as tools/selfplay.py always does; --parallel N sets how many then (default 4). Either way fewer
games run at once when there isn't enough free memory for them all: 6, then 4, 2 or 1, both when starting and while
playing (the newest games are then stopped and played again later).

Each game is its own test process, in its own folder <build>/test/parallel/<slot>/ with its own bwapi-data/write and
OpenBW connection directory, sharing the maps, data files and replays folder; its output goes to run_games.log there.
The harness writes the game's numbers to live.json in that folder twice a second. The line at the bottom of the
terminal (a spinner, a bar and moving dots) and the live stats page (tools/live_stats.py: a tab per game and one with
them all, opened in a window of its own when the games have windows) show them as the games play. PROGRESS lines are printed
every --interval seconds as well, for output that isn't a terminal.
"""

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import elo  # tools/elo.py

ROOT = Path(__file__).resolve().parent.parent

# Typical game length for the Python port; wall time per game varies with the opponent (about 1-3 minutes headless).
TYPICAL_FRAMES = 13_700
TYPICAL_SECONDS_PER_GAME = 150
FRAME_LIMIT = 30_000  # BWTest's default frame limit
TIME_LIMIT = 600  # BWTest's default wall-time limit per game, in seconds
HANG_LIMIT = TIME_LIMIT + 300  # a game still running after this long is stopped (the harness should have ended it)

# Watching at a set speed (OPENBW_GAME_SPEED, milliseconds per frame; 42 is normal speed), Bots.Play allows 90 minutes
# of game time instead: 90 minutes at normal speed, 45 at x2
_speed = os.environ.get("OPENBW_GAME_SPEED", "")
MS_PER_FRAME = int(_speed) if _speed.isdigit() and int(_speed) > 0 else 0
if MS_PER_FRAME:
    FRAME_LIMIT = 90 * 60 * 1000 // 42
    TIME_LIMIT = 90 * 60 * MS_PER_FRAME // 42
    TYPICAL_SECONDS_PER_GAME = max(TYPICAL_SECONDS_PER_GAME, TYPICAL_FRAMES * MS_PER_FRAME // 1000)

MAX_AT_ONCE = 6
DEFAULT_AT_ONCE = 4  # games at once unless --ui or --parallel says otherwise: 6 is too many for a 10-core Mac
AT_ONCE_STEPS = (6, 4, 2, 1)  # games at once, stepping down when memory runs short
GRIDS = {1: (1, 1), 2: (2, 1), 3: (3, 1), 4: (2, 2), 5: (3, 2), 6: (3, 2)}  # games at once: window columns, rows
WINDOW_SIZE = "640x480"
GAME_MEMORY = 0.5e9  # memory allowed per game when choosing how many to start (one uses about 0.15-0.3 GB)
MEMORY_RESERVE = 2e9  # memory kept free for the rest of the computer
MEMORY_CHECK_SECONDS = 2
MEMORY_SETTLE_SECONDS = 10  # after stopping games, time for their memory to be freed before checking again
GAME_MEMORY_LIMIT = 4e9  # one game using more than this is a bot leaking memory: it is stopped, not played again


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


class Progress:
    """A status line at the bottom of the terminal, redrawn in place while games run so it never looks stuck: a
    spinner, a progress bar, counts and moving dots. Other lines print above it. Off when not a terminal."""

    SPINNER = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏"

    def __init__(self) -> None:
        self.line = ""
        self.tick = 0
        self.enabled = sys.stdout.isatty()

    def show(self, done: int, total: int, details: str) -> None:
        if not self.enabled:
            return
        self.tick += 1
        width = 24
        filled = round(width * done / total) if total else 0
        bar = "█" * filled + "░" * (width - filled)
        dots = "." * (self.tick // 2 % 4)
        text = f"{self.SPINNER[self.tick % len(self.SPINNER)]} [{bar}] {done}/{total} games · {details}{dots:<3}"
        self.line = text[:shutil.get_terminal_size((100, 20)).columns - 1]
        sys.stdout.write("\r\033[K" + self.line)
        sys.stdout.flush()

    def print(self, text: str) -> None:
        """A line above the status line."""
        if self.enabled and self.line:
            sys.stdout.write("\r\033[K")
        print(text, flush=True)
        if self.enabled and self.line:
            sys.stdout.write(self.line)
            sys.stdout.flush()

    def done(self) -> None:
        if self.enabled and self.line:
            sys.stdout.write("\r\033[K")
            sys.stdout.flush()
        self.line = ""


PROGRESS = Progress()
log = PROGRESS.print


def physical_memory() -> float:
    """Bytes of RAM in this computer (macOS or Linux), or 16 GB if it can't be told."""
    try:
        return float(subprocess.run(["sysctl", "-n", "hw.memsize"], capture_output=True, text=True,
                                    check=True).stdout)
    except (OSError, ValueError, subprocess.CalledProcessError):
        pass
    try:
        return float(os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES"))
    except (OSError, ValueError, AttributeError):
        return 16e9


def available_memory() -> float | None:
    """Bytes of memory free for more programs, counting memory the system can reclaim: macOS's memory status level
    (the percentage available) or Linux's MemAvailable. None if it can't be told."""
    if sys.platform == "darwin":
        try:
            level = float(subprocess.run(["sysctl", "-n", "kern.memorystatus_level"], capture_output=True, text=True,
                                         check=True).stdout)
            return level / 100 * physical_memory()
        except (OSError, ValueError, subprocess.CalledProcessError):
            return None
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemAvailable:"):
                return float(line.split()[1]) * 1024
    except (OSError, ValueError, IndexError):
        pass
    return None


def games_memory(pids: list[int]) -> dict[int, int]:
    """Resident bytes used by each of these processes with all its descendants: each game is the harness plus the
    opponent it forks."""
    try:
        listing = subprocess.run(["ps", "-A", "-o", "pid=,ppid=,rss="], capture_output=True, text=True,
                                 check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return {}
    children: dict[int, list[int]] = {}
    own: dict[int, int] = {}
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) != 3 or not all(f.isdigit() for f in fields):
            continue
        pid, ppid, kilobytes = map(int, fields)
        children.setdefault(ppid, []).append(pid)
        own[pid] = kilobytes * 1024
    totals = {}
    for root in pids:
        memory, todo, seen = 0, [root], set()
        while todo:
            pid = todo.pop()
            if pid in seen:
                continue
            seen.add(pid)
            memory += own.get(pid, 0)
            todo += children.get(pid, [])
        totals[root] = memory
    return totals


def step_down(at_once: int) -> int:
    return next((step for step in AT_ONCE_STEPS if step < at_once), 1)


def fit_in_memory(wanted: int) -> tuple[int, float | None]:
    """How many of the wanted games at once fit in the free memory (6, 4, 2 or 1 when not all of them do), and the
    free memory."""
    free = available_memory()
    if free is None:
        return wanted, None
    fits = int((free - MEMORY_RESERVE) // GAME_MEMORY)
    if fits >= wanted:
        return wanted, free
    return next((step for step in AT_ONCE_STEPS if step <= fits), 1), free


def has_window(build: Path) -> bool:
    """Whether this build shows its games in a window (configured with -DOPENBW_ENABLE_UI=ON)."""
    try:
        return "OPENBW_ENABLE_UI:BOOL=ON" in (build / "CMakeCache.txt").read_text(errors="replace")
    except OSError:
        return build.name.endswith("-ui")


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


def stop(process: subprocess.Popen[bytes]) -> None:
    """Kills a game: the harness and the opponent it forked, which share the harness's process group."""
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except OSError:
        process.kill()
    process.wait()


@dataclass
class Game:
    """One test process playing in a slot: one game, or all of a test that loops over several."""
    number: int  # which of the run's games it plays (from 1)
    slot: int
    directory: Path
    process: subprocess.Popen[bytes]
    started: float
    log_start: int  # where its output starts in the slot's run_games.log
    read_to: int  # how far its output has been read for finished games
    existing_logs: set[Path]

    def live(self) -> dict[str, Any] | None:
        """The numbers the harness writes to live.json as the game plays."""
        try:
            data = json.loads((self.directory / "live.json").read_text())
        except (OSError, ValueError):
            return None
        return data if isinstance(data, dict) else None

    def position(self) -> tuple[int, float] | None:
        """(frame, seconds playing) of the game: from live.json, or for older builds the Stardust bot's own log."""
        live = self.live()
        if live is not None:
            return int(live.get("frame", 0)), float(live.get("wallSeconds", time.time() - self.started))
        logs = set((self.directory / "bwapi-data" / "write").glob("Stardust_log_*.txt")) - self.existing_logs
        if not logs:
            return None
        newest = max(logs, key=created)
        return last_frame(newest), max(0.0, time.time() - created(newest))

    def output(self, start: int | None = None) -> str:
        """Its output from `start` (default: all of it)."""
        try:
            with (self.directory / "run_games.log").open("rb") as f:
                f.seek(self.log_start if start is None else start)
                return f.read().decode(errors="replace")
        except OSError:
            return ""

    def new_output(self) -> str:
        """Its whole lines written since the last call."""
        text = self.output(self.read_to)
        end = text.rfind("\n") + 1
        self.read_to += len(text[:end].encode(errors="replace"))
        return text[:end]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("filter", nargs="?", help="gtest filter (default Bots.Play with --opponent)")
    parser.add_argument("--opponent", help="bot to play with Bots.Play (see bots/README.md)")
    parser.add_argument("--bot", help="bot to play as with --opponent, instead of the Python port "
                                      "(e.g. Stardust2025, the original C++ Stardust)")
    parser.add_argument("--games", type=int, default=0,
                        help="number of games (default: 20 for *RunTwenty, else 1)")
    parser.add_argument("--ui", "--UI", default=str(DEFAULT_AT_ONCE), metavar="none|1-6",
                        help=f"games at once, each in its own window (default {DEFAULT_AT_ONCE}); none plays them "
                             "headless")
    parser.add_argument("--parallel", type=int, default=0,
                        help=f"games at once with --ui none (default {DEFAULT_AT_ONCE})")
    parser.add_argument("--window-size", default=WINDOW_SIZE,
                        help=f"each game window's size (default {WINDOW_SIZE}; smaller when the screen is)")
    parser.add_argument("--interval", type=float, default=30, help="seconds between PROGRESS lines (default 30)")
    parser.add_argument("--build", help="build directory (default build-ui with windows, build with --ui none)")
    parser.add_argument("--live", action="store_true",
                        help="open the live stats window (it opens by itself when games have windows)")
    parser.add_argument("--no-live", action="store_true", help="no live stats page")
    args = parser.parse_args()

    if args.bot and not args.opponent:
        parser.error("--bot needs --opponent")
    if args.opponent:
        args.filter = args.filter or "Bots.Play"
    if not args.filter:
        parser.error("give a gtest filter or --opponent")
    ui = args.ui.strip().lower()
    if ui in ("none", "off", "0"):
        windows, wanted = False, min(args.parallel or DEFAULT_AT_ONCE, MAX_AT_ONCE)
    elif ui.isdigit() and 1 <= int(ui) <= MAX_AT_ONCE:
        windows, wanted = True, min(args.parallel or int(ui), MAX_AT_ONCE)
    else:
        parser.error(f"--ui takes none or a number from 1 to {MAX_AT_ONCE}")
    if not re.fullmatch(r"\d+x\d+", args.window_size):
        parser.error("--window-size takes WIDTHxHEIGHT, e.g. 640x480")

    # The build: the window build unless the games are headless, or the one given
    if args.build:
        build = ROOT / args.build
    elif windows:
        build = next((ROOT / name for name in ("build-ui", "build")
                      if has_window(ROOT / name) and (ROOT / name / "test" / "tests").exists()), ROOT / "build")
    else:
        build = ROOT / "build"
    if windows and not has_window(build):
        print(f"NOTE {build.name} has no game window, so the games play headless (a window build: cmake -B build-ui "
              "-DOPENBW_ENABLE_UI=ON -DCMAKE_BUILD_TYPE=Release, then cmake --build build-ui --target tests)",
              flush=True)
        windows = False
    test_dir = build / "test"
    if not (test_dir / "tests").exists():
        parser.error(f"{test_dir / 'tests'} doesn't exist: build it first (cmake --build {build.name} --target tests)")

    games = max(1, args.games or (20 if "RunTwenty" in args.filter else 1))
    # Tests that loop over many games themselves can't be split into a process per game
    splittable = args.opponent is not None or "RunTwenty" not in args.filter
    jobs = list(range(1, games + 1)) if splittable else [1]
    wanted = max(1, min(wanted, len(jobs)))
    at_once, free_memory = fit_in_memory(wanted)
    columns, rows = GRIDS[at_once]

    estimate = -(-games // at_once) * TYPICAL_SECONDS_PER_GAME
    worst = -(-games // at_once) * TIME_LIMIT
    print(f"START {args.filter}{' vs ' + args.opponent if args.opponent else ''}: {games} game(s), {at_once} at a "
          f"time, {'each in its own window' if windows else 'headless'} ({build.name}), estimated {fmt(estimate)} "
          f"(at most {fmt(worst)} with the {TIME_LIMIT // 60}-minute limit per game)", flush=True)
    if at_once < wanted and free_memory is not None:
        print(f"MEMORY {free_memory / 1e9:.1f} GB free: {at_once} game(s) at once instead of {wanted}", flush=True)
    if windows:
        print(f"WINDOWS {columns} x {rows}, {args.window_size} each (smaller if the screen is)", flush=True)

    for slot in range(at_once):
        directory = prepare_worker_directory(test_dir, slot)
        (directory / "live.json").unlink(missing_ok=True)  # an earlier run's last game
        (directory / "events.jsonl").unlink(missing_ok=True)
    viewer = None
    if not args.no_live:
        try:
            from live_stats import Viewer  # tools/live_stats.py
            viewer = Viewer([test_dir / "parallel" / str(slot) / "live.json" for slot in range(at_once)])
            print(f"LIVE stats at {viewer.start()} (a tab per game)", flush=True)
            if windows or args.live:
                viewer.open()
        except OSError as error:
            print(f"NOTE no live stats page: {error}", flush=True)
            viewer = None

    replays_dir = test_dir / "replays"
    socket_root = Path(tempfile.mkdtemp(prefix="ob", dir="/tmp"))
    queue = list(jobs)
    running: dict[int, Game] = {}
    free = list(range(at_once))
    cap = at_once  # games at once: lowered for the rest of the run when memory runs short
    memory_limit = 0.75 * physical_memory()  # for all the games together
    used_slots: set[int] = set()
    results = {"won": 0, "lost": 0, "draw": 0, "passed": 0, "failed": 0, "no result": 0}
    done = 0  # games reported
    durations: list[float] = []  # wall seconds of each finished game
    outputs: list[str] = []  # each finished process's output, for the summary
    exit_code = 0
    start = time.time()
    next_report = start + args.interval
    next_memory_check = start + MEMORY_CHECK_SECONDS
    me = args.bot or "StardustPy"
    frame_limit = int(os.environ.get("STARDUST_TEST_FRAME_LIMIT") or FRAME_LIMIT)

    def launch(number: int) -> None:
        slot = min(free)
        free.remove(slot)
        directory = prepare_worker_directory(test_dir, slot)
        (directory / "live.json").unlink(missing_ok=True)
        (directory / "events.jsonl").unlink(missing_ok=True)
        env = dict(os.environ)
        # A game making no progress for this long is a hang: the harness saves what led up to it to replays/unfinished/
        env.setdefault("STARDUST_HANG_SECONDS", "120")
        # OpenBW finds the other player through sockets in this directory (default /tmp/openbw, shared by all games);
        # each game's two processes need their own, or games connect to each other
        env["OPENBW_LOCAL_AUTO_DIRECTORY"] = str(socket_root / str(slot))
        command = [str(test_dir / "tests"), f"--gtest_filter={args.filter}"]
        if args.opponent:
            env["STARDUST_OPPONENT"] = args.opponent
            env["STARDUST_GAMES"] = "1"
            if args.bot:
                env["STARDUST_BOT"] = args.bot
        if windows:
            env["OPENBW_WINDOW_GRID"] = f"{slot},{columns},{rows}"
            env["OPENBW_WINDOW_SIZE"] = args.window_size
            env["OPENBW_WINDOW_TITLE"] = (f"Game {number}/{games}: {me} vs {args.opponent}" if args.opponent
                                          else f"Game {number}/{games}: {args.filter}")
        else:
            env["OPENBW_ENABLE_UI"] = "0"
        mode = "ab" if slot in used_slots else "wb"
        used_slots.add(slot)
        with (directory / "run_games.log").open(mode) as output:
            output.write(f"===== game {number} of {games} =====\n".encode())
            output.flush()
            log_start = output.tell()
            process = subprocess.Popen(command, cwd=directory, env=env, stdout=output, stderr=subprocess.STDOUT,
                                       start_new_session=True)
        existing_logs = set((directory / "bwapi-data" / "write").glob("Stardust_log_*.txt"))
        running[slot] = Game(number, slot, directory, process, time.time(), log_start, log_start, existing_logs)

    def report(game: Game, replay: str | None, text: str) -> None:
        """A finished game: its result, numbers and rating change."""
        nonlocal done
        done += 1
        live = game.live() or {}
        over = live.get("state") == "over"
        if replay is None and over:
            # Builds from before the harness printed REPLAY: the replay with this game's seed, saved since it started
            replay = next((path.name for path in replays_dir.glob(f"*_{live.get('seed')}_*.rep")
                           if created(path) >= game.started - 1), None)
        if args.opponent and over and live.get("result") in ("WON", "LOST", "DRAW"):
            result = live["result"].lower()
        elif replay and ("_WON" in replay or "_LOST" in replay):
            result = "won" if "_WON" in replay else "lost"
        elif replay:
            result = "passed" if "_PASS" in replay else "failed"
        else:
            result = "no result"
        results[result] += 1
        frame = int(live.get("frame", 0)) if over else 0
        where = f", frame {frame} ({game_time(frame)})" if frame else ""
        log(f"GAME {done}/{games} {result} at {fmt(time.time() - start)} elapsed{where} "
            f"({replay or f'no replay; exit code {game.process.returncode}, see {game.directory / 'run_games.log'}'})")
        for line in re.findall(r"^(?:STATS |HUNG |Python onFrame:).*", text, re.M):
            log(f"  {line}")
        if replay:
            # The harness records the game in replays/results.csv just before saving its replay
            _, rated = elo.update(replays_dir)
            for rated_game in rated:
                if rated_game.row.get("replay") == replay:
                    log(f"  ELO {elo.game_line(rated_game)}")

    def finish(slot: int) -> None:
        """A process that has ended: report what it played and free its slot."""
        nonlocal exit_code
        game = running.pop(slot)
        free.append(slot)
        tail = game.new_output() + game.output(game.read_to)
        replays = re.findall(r"^REPLAY (\S+)", tail, re.M)
        for replay in replays:
            report(game, replay, tail)
        if splittable and not replays:
            report(game, None, tail)
        durations.append(time.time() - game.started)
        outputs.append(game.output())
        exit_code = max(exit_code, game.process.returncode or 0)

    def stopped(game: Game, why: str, again: bool) -> None:
        """Stops a game that is still playing, to play it again later or not at all."""
        stop(game.process)
        del running[game.slot]
        free.append(game.slot)
        if again:
            queue.insert(0, game.number)
        else:
            outputs.append(game.output())
        log(f"STOPPED game {game.number} (slot {game.slot + 1}): {why}"
            f"{'; it will be played again' if again else ''}")

    def on_signal(number: int, _frame: object) -> None:
        raise SystemExit(128 + number)  # runs the clean-up below, so no game is left running

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGHUP, on_signal)
    interrupted = False
    try:
        while queue or running:
            while queue and free and len(running) < cap:
                launch(queue.pop(0))
            time.sleep(0.5)
            now = time.time()

            for slot, game in list(running.items()):
                if game.process.poll() is not None:
                    finish(slot)
                    continue
                if not splittable:
                    # A test looping over games: report each one as its replay is saved
                    for replay in re.findall(r"^REPLAY (\S+)", game.new_output(), re.M):
                        report(game, replay, "")
                limit = HANG_LIMIT * (1 if splittable else games)
                if now - game.started > limit:
                    stopped(game, f"still running after {fmt(limit)}, so it has hung", again=False)

            if running and now >= next_memory_check:
                next_memory_check = now + MEMORY_CHECK_SECONDS
                usage = games_memory([game.process.pid for game in running.values()])
                for game in list(running.values()):
                    if usage.get(game.process.pid, 0) > GAME_MEMORY_LIMIT:
                        stopped(game, f"it used {usage[game.process.pid] / 1e9:.1f} GB, so a bot is leaking memory",
                                again=False)
                used = sum(usage.get(game.process.pid, 0) for game in running.values())
                available = available_memory()
                short = (f"only {available / 1e9:.1f} GB of memory free" if available is not None
                         and available < MEMORY_RESERVE else
                         f"the games use {used / 1e9:.1f} GB" if used > memory_limit else "")
                if short and cap > 1:
                    cap = step_down(cap)
                    log(f"MEMORY {short}: down to {cap} game(s) at once")
                    for game in sorted(running.values(), key=lambda g: g.started)[cap:]:  # the newest
                        stopped(game, "to free memory", again=True)
                    next_memory_check = now + MEMORY_SETTLE_SECONDS

            # Each running game is assumed to reach the typical length at its pace so far, or the frame limit once
            # past it (capped by the time limit); games not started yet take the average time of finished games (or
            # the typical one), shared by the slots
            positions = [(game, game.position()) for game in sorted(running.values(), key=lambda g: g.slot)]
            per_game = sum(durations) / len(durations) if durations else TYPICAL_SECONDS_PER_GAME
            lefts = []
            for game, position in positions:
                frame, seconds = position or (0, now - game.started)
                rate = frame / seconds if frame and seconds > 0 else 0.0
                target = min(TYPICAL_FRAMES, frame_limit) if frame < min(TYPICAL_FRAMES, frame_limit) else frame_limit
                game_left = max(0.0, target - frame) / rate if rate > 0 else max(0.0, per_game - seconds)
                lefts.append(min(game_left, max(0.0, TIME_LIMIT - seconds)))
            left = max(max(lefts, default=0.0), (sum(lefts) + len(queue) * per_game) / max(1, cap))
            elapsed = now - start
            times = " ".join(game_time(position[0]) if position else "…" for _, position in positions)
            PROGRESS.show(done, games, f"{fmt(elapsed)} elapsed · ~{fmt(left)} left · {len(running)} playing "
                                       f"(max {cap}){' at ' + times if times else ''}")
            if viewer is not None:
                viewer.set_run(total=games, done=done, playing=len(running), cap=cap, elapsed=elapsed, left=left,
                               results=dict(results), note="windows" if windows else "headless")

            if now >= next_report:
                next_report += args.interval
                frames = ", ".join(f"{position[0]} ({game_time(position[0])})" for _, position in positions
                                   if position) or "starting"
                long_game = any(position and position[0] > TYPICAL_FRAMES for _, position in positions)
                note = f", a game is longer than typical (frame limit {frame_limit})" if long_game else ""
                log(f"PROGRESS {fmt(elapsed)} elapsed | {done}/{games} done | {len(running)} playing (max {cap}) "
                    f"at frame {frames} | ~{fmt(left)} left{note}")
    except KeyboardInterrupt:
        interrupted = True
    finally:
        PROGRESS.done()
        for game in list(running.values()):
            stop(game.process)
            outputs.append(game.output())
        shutil.rmtree(socket_root, ignore_errors=True)
        if viewer is not None:
            viewer.set_run(total=games, done=done, playing=0, cap=cap, elapsed=time.time() - start, left=0,
                           results=results, note="stopped" if interrupted else "finished")
            time.sleep(1.5)  # the page's last look at the finished games
            viewer.stop()

    elapsed = time.time() - start
    python_errors = sum(text.count("Python error") for text in outputs)
    failed = sum(1 for text in outputs if re.search(r"\[  FAILED  \]", text))
    tally = ", ".join(f"{count} {name}" for name, count in results.items() if count)
    if interrupted:
        print(f"STOPPED with Ctrl-C after {fmt(elapsed)}: {done} of {games} game(s) played", flush=True)
        exit_code = 130
    print(f"DONE in {fmt(elapsed)} (exit {exit_code}): {done} game(s): {tally or 'no results'}; "
          f"{failed} with failed tests; Python errors: {python_errors}", flush=True)
    records, _ = elo.update(replays_dir)
    if records:
        print("\n" + elo.leaderboard(records), flush=True)
    print(f"  logs: {', '.join(os.path.relpath(test_dir / 'parallel' / str(slot) / 'run_games.log', ROOT) for slot in sorted(used_slots))}",
          flush=True)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())

"""Rates bots by tier: each tier in bots/tiers.json plays a round robin, headless and as fast as the bots allow.

Usage: python tools/round_robin.py [--tier "Tier 1"] [--games N] [--parallel N] [--build DIR]
       python tools/round_robin.py --table          (only print the ratings from the games played so far)

Every pairing in a tier plays N games (games_per_pairing in bots/tiers.json, by default). Results are appended to
<build>/test/round_robin/results.jsonl as each game ends, so a stopped run resumes where it left off and only plays
the games still missing.

Speed: games run without a window, several at once (by default one per two CPU cores: each game is two processes).
The harness prints each game's frame every 1000 frames; if a game plays slower than --min-fps (24 frames per second,
StarCraft's own speed on Fastest) or stops advancing for --stall seconds, the run warns, stops every game and quits
(exit code 3) rather than go on producing slow or unrepresentative games. Re-run to resume.

Ratings: in each tier, an Elo-scale Bradley-Terry fit of the tier's results (a draw counts half). The last-placed bot is
rated at the tier's floor, the others above it by their results, capped just below the next tier's floor, so every
bot of a higher tier is rated above every bot of a lower one. +/- is one standard deviation. A bot at the cap, or
scoring 85% or more, is marked "promote?"; one scoring 15% or less, "relegate?". Bots that are not built here are
listed at their tier's floor, as not played, as are those in a tier's "skip" list.
"""

import argparse
import itertools
import json
import math
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_games import fmt, prepare_worker_directory  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TIERS_FILE = ROOT / "bots" / "tiers.json"
PYTHON_PORT = "StardustPy"  # the Python port's name in Bots.Play; it plays when STARDUST_BOT is not set

ELO = 400 / math.log(10)  # Elo points per unit of log-odds
PRIOR_SD = 350.0  # keeps a bot that won or lost every game at a finite rating
TYPICAL_SECONDS_PER_GAME = 120


# ---------------------------------------------------------------------------------------------------------------------
# Ratings


@dataclass
class Record:
    games: int = 0
    won: int = 0
    lost: int = 0
    drawn: int = 0

    @property
    def score(self) -> float:
        return (self.won + self.drawn / 2) / self.games if self.games else 0.0


@dataclass
class Rating:
    bot: str
    rating: float
    deviation: float
    record: Record
    played: bool = True
    note: str = ""


def fit(bots: list[str], results: list[tuple[str, str, float]]) -> dict[str, tuple[float, float]]:
    """Elo-scale ratings, relative to the bots' mean, and their standard deviations, from (bot, opponent, score)
    results with score 1, 0.5 or 0: the most likely ratings under a Bradley-Terry model with a normal prior."""
    index = {bot: i for i, bot in enumerate(bots)}
    n = len(bots)
    wins = np.zeros((n, n))  # wins[i, j]: points i scored against j
    games = np.zeros((n, n))
    for bot, opponent, score in results:
        i, j = index[bot], index[opponent]
        wins[i, j] += score
        wins[j, i] += 1 - score
        games[i, j] += 1
        games[j, i] += 1

    prior = (PRIOR_SD / ELO) ** -2
    theta = np.zeros(n)  # natural log-odds units
    hessian = np.eye(n) * -prior
    for _ in range(100):
        p = 1 / (1 + np.exp(theta[None, :] - theta[:, None]))  # p[i, j]: chance i beats j
        gradient = (wins - games * p).sum(axis=1) - prior * theta
        weights = games * p * (1 - p)
        hessian = weights - np.diag(weights.sum(axis=1)) - prior * np.eye(n)
        step = np.linalg.solve(hessian, -gradient)
        theta += step
        if np.abs(step).max() < 1e-9:
            break
    covariance = np.linalg.inv(-hessian)
    theta -= theta.mean()
    return {bot: (float(theta[i] * ELO), float(math.sqrt(covariance[i, i]) * ELO)) for bot, i in index.items()}


def rate_tier(bots: list[str], played: set[str], results: list[tuple[str, str, float]], floor: int,
              ceiling: int | None, unplayed_note: dict[str, str]) -> list[Rating]:
    """The tier's table: played bots by rating, the last at the floor; then the bots that were not played."""
    records: dict[str, Record] = defaultdict(Record)
    for bot, opponent, score in results:
        for who, points in ((bot, score), (opponent, 1 - score)):
            record = records[who]
            record.games += 1
            if points == 1:
                record.won += 1
            elif points == 0:
                record.lost += 1
            else:
                record.drawn += 1

    rated = [bot for bot in bots if bot in played and records[bot].games]
    table: list[Rating] = []
    if rated:
        fitted = fit(rated, results)
        lowest = min(rating for rating, _ in fitted.values())
        for bot in rated:
            rating, deviation = fitted[bot]
            entry = Rating(bot, floor + rating - lowest, deviation, records[bot])
            notes = []
            if ceiling is not None and entry.rating >= ceiling:
                entry.rating = ceiling - 1
                notes.append("capped")
            if "capped" in notes or entry.record.score >= 0.85:
                notes.append("promote?")
            elif entry.record.score <= 0.15:
                notes.append("relegate?")
            entry.note = ", ".join(notes)
            table.append(entry)
        table.sort(key=lambda entry: -entry.rating)
    for bot in bots:
        if bot not in rated:
            table.append(Rating(bot, floor, 0.0, records[bot], played=False, note=unplayed_note.get(bot, "not played")))
    return table


def format_table(name: str, floor: int, table: list[Rating]) -> str:
    lines = [f"{name} (floor {floor})",
             f"{'#':>3}  {'Bot':<18} {'Rating':>6} {'+/-':>5} {'Games':>6} {'W':>4} {'L':>4} {'D':>4} {'Score':>7}  Note"]
    for rank, entry in enumerate(table, 1):
        r = entry.record
        if entry.played:
            lines.append(f"{rank:>3}  {entry.bot:<18} {entry.rating:>6.0f} {entry.deviation:>5.0f} {r.games:>6} "
                         f"{r.won:>4} {r.lost:>4} {r.drawn:>4} {r.score:>6.1%}  {entry.note}")
        else:
            lines.append(f"{'-':>3}  {entry.bot:<18} {entry.rating:>6.0f} {'':>5} {'':>6} {'':>4} {'':>4} {'':>4} "
                         f"{'':>7}  {entry.note}")
    return "\n".join(lines)


# ---------------------------------------------------------------------------------------------------------------------
# Running games


@dataclass
class Job:
    us: str
    opponent: str
    directory: Path
    process: subprocess.Popen[bytes]
    log: Path
    started: float
    samples: list[tuple[float, int]] = field(default_factory=list)  # (wall time, frame) at the last checks
    last_progress: float = 0.0
    log_size: int = 0


def load_tiers() -> dict[str, Any]:
    data: dict[str, Any] = json.loads(TIERS_FILE.read_text())
    return data


def registered_bots(tests: Path) -> set[str]:
    output = subprocess.run([str(tests), "--gtest_filter=Bots.List"], cwd=tests.parent, capture_output=True,
                            text=True, timeout=120).stdout
    return {match.group(1) for match in re.finditer(r"^  (\w+) \(", output, re.MULTILINE)} | {PYTHON_PORT}


def read_results(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def pair_key(a: str, b: str) -> tuple[str, str]:
    return (a, b) if a < b else (b, a)


def score_of(result: dict[str, str]) -> tuple[str, str, float]:
    return result["us"], result["opponent"], {"WON": 1.0, "DRAW": 0.5}.get(result["result"], 0.0)


def last_progress(log: Path, job: Job) -> tuple[int, float] | None:
    """The latest [progress] frame and seconds in the job's log, reading only what is new since the last call."""
    try:
        with log.open("rb") as f:
            f.seek(job.log_size)
            new = f.read().decode(errors="replace")
    except OSError:
        return None
    job.log_size += len(new.encode())
    matches = re.findall(r"\[progress\] frame=(\d+) seconds=([\d.]+)", new)
    if not matches:
        return None
    frame, seconds = matches[-1]
    return int(frame), float(seconds)


def stop_all(jobs: list[Job]) -> None:
    for job in jobs:
        if job.process.poll() is None:
            try:
                os.killpg(job.process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
    deadline = time.time() + 10
    for job in jobs:
        try:
            job.process.wait(timeout=max(0.1, deadline - time.time()))
        except subprocess.TimeoutExpired:
            os.killpg(job.process.pid, signal.SIGKILL)


def print_ratings(tiers: list[dict[str, Any]], available: set[str], results: list[dict[str, str]]) -> None:
    floors = [int(tier["floor"]) for tier in tiers]
    for position, tier in enumerate(tiers):
        bots = list(tier["bots"])
        ceiling = None
        higher = [f for f in floors if f > floors[position]]
        if higher:
            ceiling = min(higher)
        skip = set(tier.get("skip", []))
        in_tier = set(bots) - skip
        scores = [score_of(r) for r in results if r["us"] in in_tier and r["opponent"] in in_tier]
        notes = {bot: ("not built here" if bot not in available else "not played") for bot in bots}
        notes.update({bot: "skipped (rated elsewhere)" for bot in skip})
        if not tier.get("play", True):
            notes = {bot: "tier not played" for bot in bots}
        playable = (available - skip) if tier.get("play", True) else set()
        table = rate_tier(bots, playable, scores, floors[position], ceiling, notes)
        print()
        print(format_table(str(tier["name"]), floors[position], table))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tier", action="append", help="tier to play (default: every tier with play: true)")
    parser.add_argument("--games", type=int, help="games per pairing (default: games_per_pairing in bots/tiers.json)")
    parser.add_argument("--parallel", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("--build", default="build", help="headless build directory (default: build)")
    parser.add_argument("--min-fps", type=float, default=24.0,
                        help="stop if a game plays fewer frames per second than this (default 24: real time)")
    parser.add_argument("--stall", type=float, default=60.0, help="stop if a game does not advance for this long")
    parser.add_argument("--interval", type=float, default=30.0, help="seconds between progress reports and checks")
    parser.add_argument("--table", action="store_true", help="only print the ratings")
    args = parser.parse_args()

    test_dir = ROOT / args.build / "test"
    tests = test_dir / "tests"
    results_path = test_dir / "round_robin" / "results.jsonl"
    data = load_tiers()
    tiers: list[dict[str, Any]] = data["tiers"]
    games_per_pairing = args.games or int(data.get("games_per_pairing", 10))
    if not tests.exists():
        print(f"No test harness at {tests}: build it first (cmake --build {args.build} -j 5)", file=sys.stderr)
        return 1
    available = registered_bots(tests)

    if args.table:
        print_ratings(tiers, available, read_results(results_path))
        return 0

    # Every game still to play, pairings interleaved so that a stopped run leaves every pairing about as complete
    chosen = [tier for tier in tiers if (tier["name"] in args.tier if args.tier else tier.get("play", True))]
    played = Counter(pair_key(r["us"], r["opponent"]) for r in read_results(results_path))
    queues: list[list[tuple[str, str]]] = []
    for tier in chosen:
        skip = set(tier.get("skip", []))
        bots = [bot for bot in tier["bots"] if bot in available and bot not in skip]
        missing = [bot for bot in tier["bots"] if bot not in available and bot not in skip]
        if missing:
            print(f"{tier['name']}: not built here, so not played: {', '.join(missing)}")
        for a, b in itertools.combinations(bots, 2):
            # The Python port can only play as "us"; otherwise alternate which bot runs in the main process
            if b == PYTHON_PORT:
                a, b = b, a
            remaining = games_per_pairing - played[pair_key(a, b)]
            queues.append([(a, b) if n % 2 == 0 or a == PYTHON_PORT else (b, a) for n in range(max(0, remaining))])
    todo = [game for round_ in itertools.zip_longest(*queues) for game in round_ if game is not None]
    total = len(todo)
    if not total:
        print("Every pairing has played its games.")
        print_ratings(tiers, available, read_results(results_path))
        return 0

    parallel = max(1, min(args.parallel, total))
    estimate = -(-total // parallel) * TYPICAL_SECONDS_PER_GAME
    print(f"START round robin of {', '.join(str(t['name']) for t in chosen)}: {total} game(s) to play, "
          f"{parallel} at a time, headless; estimated {fmt(estimate)}. Stops with a warning if a game plays slower "
          f"than {args.min_fps:g} frames/s or stalls for {args.stall:g} s.", flush=True)

    results_path.parent.mkdir(parents=True, exist_ok=True)
    directories = [prepare_worker_directory(test_dir, index) for index in range(parallel)]
    socket_root = Path(tempfile.mkdtemp(prefix="ob", dir="/tmp"))
    free = list(range(parallel))
    running: dict[int, Job] = {}
    start = time.time()
    next_report = start + args.interval
    done = 0
    finished_seconds: list[float] = []
    slow_reason = ""

    def launch(slot: int, us: str, opponent: str) -> None:
        env = dict(os.environ)
        env.update({"STARDUST_OPPONENT": opponent, "STARDUST_GAMES": "1", "OPENBW_ENABLE_UI": "0",
                    "OPENBW_LOCAL_AUTO_DIRECTORY": str(socket_root / str(slot))})
        env.pop("STARDUST_BOT", None)
        if us != PYTHON_PORT:
            env["STARDUST_BOT"] = us
        log = directories[slot] / "round_robin.log"
        with log.open("w") as output:
            process = subprocess.Popen([str(tests), "--gtest_filter=Bots.Play"], cwd=directories[slot], env=env,
                                       stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        now = time.time()
        running[slot] = Job(us, opponent, directories[slot], process, log, now, [(now, 0)], now)

    try:
        while todo or running:
            while todo and free:
                us, opponent = todo.pop(0)
                launch(free.pop(0), us, opponent)
            time.sleep(1)
            now = time.time()

            for slot, job in list(running.items()):
                progress = last_progress(job.log, job)
                if progress:
                    job.samples.append((now, progress[0]))
                    job.last_progress = now
                if job.process.poll() is None:
                    # The harness reports every 1000 frames: a longer wait than 1000 frames take at the minimum speed
                    # means the game is slower than that. Before the first report, the game is starting up.
                    gap = now - job.last_progress
                    frame = job.samples[-1][1]
                    if frame == 0 and gap > args.stall:
                        slow_reason = f"{job.us} vs {job.opponent} has not started playing after {gap:.0f} s"
                    elif frame > 0 and gap > max(1000 / args.min_fps, 5.0):
                        slow_reason = (f"{job.us} vs {job.opponent} is playing slower than {args.min_fps:g} frames/s: "
                                       f"no progress for {gap:.0f} s after frame {frame}")
                    continue

                # Finished: record its result
                del running[slot]
                free.append(slot)
                text = job.log.read_text(errors="replace")
                match = re.search(r"\[result\] us=(\S+) opponent=(\S+) result=(\w+) map=(\S+) seed=(-?\d+)", text)
                if not match:
                    print(f"WARNING {job.us} vs {job.opponent} ended without a result (exit {job.process.returncode});"
                          f" see {job.log}. It will be played again on the next run.", flush=True)
                    continue
                result = {"us": match.group(1), "opponent": match.group(2), "result": match.group(3),
                          "map": match.group(4), "seed": match.group(5), "seconds": round(now - job.started, 1),
                          "frames": job.samples[-1][1]}
                with results_path.open("a") as out:
                    out.write(json.dumps(result) + "\n")
                done += 1
                finished_seconds.append(now - job.started)
                print(f"GAME {done}/{total} {result['us']} vs {result['opponent']}: {result['result']} on "
                      f"{result['map']} in {fmt(now - job.started)} ({fmt(now - start)} elapsed)", flush=True)

            if slow_reason:
                print(f"WARNING slowdown: {slow_reason}. Stopping every game; {done} game(s) were recorded. Check what "
                      f"else is using the CPU, or try fewer --parallel, then re-run to resume.", flush=True)
                stop_all(list(running.values()))
                return 3

            if now >= next_report:
                next_report += args.interval
                per_game = sum(finished_seconds) / len(finished_seconds) if finished_seconds else TYPICAL_SECONDS_PER_GAME
                left = -(-(len(todo) + len(running)) // parallel) * per_game
                playing = ", ".join(f"{job.us} vs {job.opponent} @{job.samples[-1][1]}" for job in running.values())
                print(f"PROGRESS {fmt(now - start)} elapsed | {done}/{total} done | {playing or 'starting'} | "
                      f"~{fmt(left)} left", flush=True)
    except KeyboardInterrupt:
        print("Interrupted: stopping every game. Re-run to resume.", flush=True)
        stop_all(list(running.values()))
        return 130
    finally:
        shutil.rmtree(socket_root, ignore_errors=True)

    print(f"DONE {done} game(s) in {fmt(time.time() - start)}", flush=True)
    print_ratings(tiers, available, read_results(results_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())

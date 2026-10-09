"""Improves ClaudeOpus55RL by self-play, in the way Leela Zero improves its network: propose a candidate, play it, and
promote it to "best" only if it beats the current best through a gate. Instead of a neural network, the candidate is a
set of parameters (bots/ClaudeOpus55RL reads them from a weights file at the start of each game): probe counts,
attack and retreat thresholds, and opening choices.

Usage: python tools/selfplay.py [--hours 12] [--games 20] [--gauntlet-games 2] [--parallel 6] [--build build]
                                [--approver COMMAND | --auto-approve]
       python tools/selfplay.py --status     (where training stands; plays nothing)

Each generation:
1. Mutate the best parameters: change one to three of them (numbers by a step that adapts to how often candidates
   pass, choices by switching), within the bounds below.
2. Self-play: the candidate (ClaudeOpus55RLCandidate) against the best (ClaudeOpus55RL), --games games, each
   playing half of them as "us". It needs a 55% score, as in Leela Zero.
3. Gauntlet, at the same time: the candidate against the 5 training bots, the middle of tools/ladder.py's Tier 2
   (PylonPuller, the three UAlbertaBots, Stone; BunkerBoxer, the weakest, is dropped). It must score at least as well
   as the best did, less 5%.
4. Approval: a candidate that passed is shown with its results, and promoted only once approved, at the terminal
   (y/n), by --approver (a command given the report's path; exit status 0 approves) or with --auto-approve.
5. Promotion: the Elo of the new best is the old one plus the self-play margin (400 log10(score / (1 - score))),
   so generation 0, ClaudeOpus55's own parameters, is 0 Elo.

The goal: the best wins every game against the 5 training bots, twice in a row (a confirming gauntlet is played when
the first is perfect), and then every --test-games game against the held-out test bot, ZZZKBot, the strongest of
Tier 2, which is never trained against. Then the first iteration of training has succeeded and the run stops. A failed
test is tried again with the next generation. It also stops after --hours.

Games run headless at full speed, --parallel at once (default 6, one per core), each as its own test harness
process. One slot always plays the old version (best) against the new one (candidate), game after game; the other
slots play the new version against the 5 training bots, at least --gauntlet-games each, and keep going round them
for as long as self-play lasts, so no core waits and the record against them grows.

Everything is saved as it happens, in bots/ClaudeOpus55RL/training/: state.json (the best parameters, generation,
Elo), history.jsonl (every candidate and its results) and selfplay.log. Stopping and re-running resumes; a generation
cut short is played again. Games are recorded by the test harness in <build>/test/replays/results.csv, which this reads.
"""

from __future__ import annotations

import argparse
import json
import math
import random
import os
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Protocol

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ladder import LADDER  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TRAINING_DIR = ROOT / "bots" / "ClaudeOpus55RL" / "training"
BEST = "ClaudeOpus55RL"
CANDIDATE = "ClaudeOpus55RLCandidate"
TIER2 = next(names for rung, names in LADDER if rung == "Tier 2")  # weakest first
# Trained against: the 5 in the middle. The weakest is dropped as too easy to teach anything; the strongest is held
# out, never trained against, to test the best once it beats the 5 in every game
TRAINING = TIER2[1:-1]
TEST = TIER2[-1]
DROPPED = TIER2[0]

GATE = 0.55  # the self-play score a candidate needs, as in Leela Zero
GAUNTLET_TOLERANCE = 0.05  # how much worse than the best a candidate may score against the training bots
SECONDS_PER_GAME = 90  # for estimates, before any game has been timed


@dataclass(frozen=True)
class Param:
    name: str
    default: float
    low: float
    high: float
    step: float
    integer: bool = True
    choices: int = 0  # a choice among 0..choices-1 instead of a number


PARAMS = [
    Param("probes_per_base", 24, 16, 30, 2),
    Param("max_probes", 60, 40, 75, 5),
    Param("one_base_probes", 22, 16, 26, 2),
    Param("first_attack_army", 12, 6, 24, 3),
    Param("later_attack_army", 16, 8, 30, 3),
    Param("retreat_below", 6, 2, 12, 2),
    Param("retreat_ratio", 1.25, 0.8, 2.0, 0.15, integer=False),
    Param("regroup_distance", 450, 250, 800, 75),
    Param("army_first_ratio", 2.0, 1.0, 3.5, 0.3, integer=False),
    Param("terran_opening", 0, 0, 1, 1, choices=2),
    Param("protoss_opening", 0, 0, 1, 1, choices=2),
    # Situational awareness
    Param("home_threat_radius", 900, 600, 1400, 100),
    Param("natural_threat_radius", 600, 400, 1000, 75),
    Param("rush_window", 9000, 6000, 13000, 750),
    Param("rush_min_units", 4, 2, 8, 1),
    Param("scout_supply", 9, 7, 14, 1),
    Param("scout_until", 6000, 4000, 9000, 500),
    # Build order and macro
    Param("army_for_natural_vs_protoss", 8, 4, 14, 2),
    Param("army_for_natural", 6, 3, 12, 2),
    Param("expand_strength_ratio", 0.8, 0.5, 1.3, 0.1, integer=False),
    Param("probes_per_base_before_next", 18, 12, 24, 2),
    Param("max_gateways", 12, 6, 16, 2),
    Param("gateways_per_base", 3, 2, 5, 1),
    Param("zealots_before_core_vs_terran", 3, 1, 6, 1),
    Param("zealots_before_core", 4, 2, 8, 1),
    # Unit reactions
    Param("uphill_penalty", 2.0, 1.2, 3.0, 0.2, integer=False),
]


def defaults() -> dict[str, float]:
    return {p.name: p.default for p in PARAMS}


def mutate(params: dict[str, float], scale: float, rng: random.Random) -> tuple[dict[str, float], list[str]]:
    """A candidate: one to three parameters changed. Returns it and the names changed."""
    candidate = dict(params)
    changed = rng.sample(PARAMS, rng.choice([1, 1, 2, 2, 3]))
    for p in changed:
        old = candidate.get(p.name, p.default)
        if p.choices:
            new = float(rng.choice([c for c in range(p.choices) if c != int(old)]))
        else:
            new = old
            for _ in range(20):  # a change that rounds or clamps back to the old value doesn't count
                new = min(p.high, max(p.low, old + rng.gauss(0, p.step * scale)))
                new = float(round(new)) if p.integer else round(new, 2)
                if new != old:
                    break
        candidate[p.name] = new
    return candidate, [p.name for p in changed if candidate[p.name] != params.get(p.name, p.default)]


def score(results: list[str]) -> float:
    """Wins count 1, draws half."""
    if not results:
        return 0.0
    return sum(1.0 if r == "WON" else 0.5 if r == "DRAW" else 0.0 for r in results) / len(results)


def beats_old_version(results: list[str]) -> bool:
    """The gate: at least 55%, and a standard error clear of 50%, so that luck alone rarely promotes a candidate
    that only changes something that doesn't matter (at 20 games, 13 wins; even candidates pass about 1 time in 8)."""
    points = score(results)
    return points >= GATE and points - math.sqrt(points * (1 - points) / max(1, len(results))) > 0.5


def elo_margin(points: float) -> float:
    points = min(0.99, max(0.01, points))
    return 400 * math.log10(points / (1 - points))


def gauntlet_score(gauntlet: dict[str, list[str]]) -> float:
    return score([r for results in gauntlet.values() for r in results])


def perfect(gauntlet: dict[str, list[str]], games: int, opponents: list[str] = TRAINING) -> bool:
    """Beat each of these opponents in every game, at least `games` games each."""
    return all(len(gauntlet.get(o, [])) >= games and all(r == "WON" for r in gauntlet[o]) for o in opponents)


# ---------------------------------------------------------------------------------------------------------------------


@dataclass
class State:
    generation: int = 0
    elo: float = 0.0
    scale: float = 1.0
    best: dict[str, float] | None = None
    best_gauntlet: dict[str, list[str]] | None = None
    candidates_tried: int = 0
    goal_reached: bool = False
    tested_generation: int = -1  # the last generation tested against the held-out bot

    @staticmethod
    def load(path: Path) -> State:
        if not path.exists():
            return State(best=defaults())
        data = json.loads(path.read_text())
        return State(**data)

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.__dict__, indent=2) + "\n")
        tmp.replace(path)


# One game: (us, opponent, lane). Self-play games ("self" lane) run one at a time, in their own slot; the others fill
# the remaining slots
Job = tuple[str, str, str]
# Plays the games, and while self-play games remain, more games from `fill` in the slots self-play doesn't use.
# Returns {(us, opponent): results for "us"} (WON, LOST or DRAW)
# `lost` (optional) is asked after each self-play game, with the candidate's self-play results so far and the number
# of self-play games still to finish; once it answers True the round is stopped, as nothing it plays can matter.
Lost = Callable[[list[str], int], bool]


class Runner(Protocol):
    def __call__(self, jobs: list[Job], fill: Callable[[], Job] | None,
                 lost: Lost | None = None) -> dict[tuple[str, str], list[str]]: ...


def candidate_view(played: dict[tuple[str, str], list[str]]) -> list[str]:
    """Self-play results from the candidate's side, whichever side it played as."""
    flipped = {"WON": "LOST", "LOST": "WON"}
    return played.get((CANDIDATE, BEST), []) + [flipped.get(r, r) for r in played.get((BEST, CANDIDATE), [])]


def cannot_pass(results: list[str], remaining: int) -> bool:
    """True when the candidate fails the self-play gate even if it wins every game still to play. The gate's score
    only rises with more wins, so this is exact: stopping then changes no decision."""
    return not beats_old_version(results + ["WON"] * remaining)


class Trainer:
    def __init__(self, runner: Runner, install: Callable[[dict[str, float], dict[str, float]], None],
                 approve: Callable[[dict[str, Any]], bool], log: Callable[[str], None], training: Path,
                 games: int, gauntlet_games: int, rng: random.Random, test_games: int = 4) -> None:
        self.runner = runner
        self.install = install
        self.approve = approve
        self.log = log
        self.training = training
        self.games = games
        self.gauntlet_games = gauntlet_games
        self.test_games = test_games
        self.rng = rng
        self.state = State.load(training / "state.json")
        # Candidates already tried against the current best: never played again
        self.tried: set[str] = set()
        history = training / "history.jsonl"
        for line in history.read_text().splitlines() if history.exists() else []:
            entry = json.loads(line)
            if entry.get("generation") == self.state.generation and "params" in entry:
                self.tried.add(json.dumps(entry["params"], sort_keys=True))

    def save(self) -> None:
        self.state.save(self.training / "state.json")
        best = self.state.best or defaults()
        (self.training / "best.json").write_text(json.dumps(best, indent=2) + "\n")

    def record(self, entry: dict[str, Any]) -> None:
        with (self.training / "history.jsonl").open("a") as f:
            f.write(json.dumps(entry) + "\n")

    def gauntlet_jobs(self, bot: str) -> list[Job]:
        return [(bot, opponent, "gauntlet") for _ in range(self.gauntlet_games) for opponent in TRAINING]

    def gauntlet_results(self, bot: str, played: dict[tuple[str, str], list[str]]) -> dict[str, list[str]]:
        results = {opponent: played.get((bot, opponent), []) for opponent in TRAINING}
        self.log(f"  {bot} vs the 5 training bots: " + ", ".join(f"{o} {r.count('WON')}/{len(r)}" for o, r in results.items()))
        return results

    def gauntlet(self, bot: str) -> dict[str, list[str]]:
        return self.gauntlet_results(bot, self.runner(self.gauntlet_jobs(bot), None))

    def ensure_best_gauntlet(self) -> None:
        """The best's own record against the training bots, the bar a candidate must reach."""
        if self.state.best_gauntlet is None:
            best = self.state.best or defaults()
            self.install(best, best)
            self.log(f"Generation {self.state.generation}: playing the best against the training bots for its baseline")
            self.state.best_gauntlet = self.gauntlet(BEST)
            self.save()

    def check_goal(self) -> bool:
        """The stop goal: every game won against the 5 training bots, twice in a row (a confirming gauntlet), then every
        test game against the held-out bot. A failed test waits for a new generation before it is tried again."""
        gauntlet = self.state.best_gauntlet or {}
        if self.state.tested_generation == self.state.generation or not perfect(gauntlet, self.gauntlet_games):
            return False
        best = self.state.best or defaults()
        self.install(best, best)
        self.log("The best won every game against the 5 training bots: playing a confirming gauntlet")
        confirm = self.gauntlet(BEST)
        self.record({"time": time.time(), "generation": self.state.generation, "confirming_gauntlet": confirm})
        self.state.best_gauntlet = {o: gauntlet.get(o, []) + confirm.get(o, []) for o in TRAINING}
        if not perfect(confirm, self.gauntlet_games):
            self.save()
            return False
        self.log(f"Confirmed. Testing against the held-out bot, {TEST}: {self.test_games} game(s)")
        test = self.runner([(BEST, TEST, "gauntlet")] * self.test_games, None).get((BEST, TEST), [])
        self.state.tested_generation = self.state.generation
        self.record({"time": time.time(), "generation": self.state.generation, "test": {TEST: test}})
        self.log(f"  {BEST} vs {TEST}: {test.count('WON')}/{len(test)} won")
        if len(test) >= self.test_games and all(r == "WON" for r in test):
            self.state.goal_reached = True
            self.save()
            return True
        self.log(f"  not every test game won; training continues, and the next generation is tested again")
        self.save()
        return False

    def generation(self) -> bool:
        """Tries one candidate. Returns True when the goal is reached."""
        self.ensure_best_gauntlet()
        if self.check_goal():
            return True
        best = self.state.best or defaults()
        for _ in range(100):
            candidate, changed = mutate(best, self.state.scale, self.rng)
            if json.dumps(candidate, sort_keys=True) not in self.tried:
                break
        self.tried.add(json.dumps(candidate, sort_keys=True))
        self.state.candidates_tried += 1
        trial = self.state.candidates_tried
        changes = ", ".join(f"{n} {best.get(n)} -> {candidate[n]}" for n in changed)
        self.log(f"Candidate {trial} (generation {self.state.generation} is best): {changes}")
        self.install(best, candidate)

        # One slot plays the old version against the new, taking turns at being "us"; the others play the new version
        # against the 5 training bots, round and round them for as long as self-play lasts
        jobs: list[Job] = [(CANDIDATE, BEST, "self") if n % 2 == 0 else (BEST, CANDIDATE, "self")
                           for n in range(self.games)]
        jobs += self.gauntlet_jobs(CANDIDATE)
        extra = iter(int(n) for n in range(10 ** 9))

        def fill() -> Job:
            return (CANDIDATE, TRAINING[next(extra) % len(TRAINING)], "gauntlet")

        played = self.runner(jobs, fill, cannot_pass)
        selfplay = candidate_view(played)
        selfplay_score = score(selfplay)
        self.log(f"  self-play against the old version: {selfplay.count('WON')}/{len(selfplay)} won, "
                 f"score {selfplay_score:.0%} (needs {GATE:.0%}, clear of 50% by a standard error)")
        gauntlet = self.gauntlet_results(CANDIDATE, played)
        bar = gauntlet_score(self.state.best_gauntlet or {}) - GAUNTLET_TOLERANCE
        self.log(f"  training bots: score {gauntlet_score(gauntlet):.0%} (needs {max(0.0, bar):.0%})")
        entry: dict[str, Any] = {"time": time.time(), "candidate": trial, "generation": self.state.generation,
                                 "params": candidate, "changed": changed, "selfplay": selfplay, "gauntlet": gauntlet}
        passed = beats_old_version(selfplay) and gauntlet_score(gauntlet) >= bar
        approved = False
        if passed:
            report = {"candidate": trial, "generation": self.state.generation + 1, "changed": changes,
                      "params": candidate, "selfplay_score": selfplay_score,
                      "elo": round(self.state.elo + elo_margin(selfplay_score), 1),
                      "tier2_score": gauntlet_score(entry["gauntlet"]),
                      "best_tier2_score": gauntlet_score(self.state.best_gauntlet or {}),
                      "tier2": {o: f"{r.count('WON')}/{len(r)}" for o, r in entry["gauntlet"].items()}}
            approved = self.approve(report)
            entry["approved"] = approved
        entry["promoted"] = passed and approved
        if passed and approved:
            self.state.generation += 1
            self.state.elo += elo_margin(selfplay_score)
            self.state.best = candidate
            self.state.best_gauntlet = entry["gauntlet"]
            self.state.scale = min(3.0, self.state.scale * 1.2)
            self.tried.clear()
            (self.training / "generations").mkdir(parents=True, exist_ok=True)
            (self.training / "generations" / f"{self.state.generation:03d}.json").write_text(
                json.dumps(candidate, indent=2) + "\n")
            self.log(f"  PROMOTED: generation {self.state.generation}, Elo {self.state.elo:+.0f} over ClaudeOpus55")
        else:
            self.state.scale = max(0.3, self.state.scale * 0.95)
            self.log("  rejected" + ("" if passed else " by the gate") + ("" if not passed or approved else " by you"))
        self.record(entry)
        self.save()
        return False


# ---------------------------------------------------------------------------------------------------------------------
# Running real games


HANG_SECONDS = 1800  # a game still running after this long is killed and reported as a hang


def make_issue_log(training: Path, build: str, log: Callable[[str], None]) -> Callable[[str, Job, Path | None], None]:
    """Bug finding: every crash, hang, lost result and draw goes to training/issues.log with the weights that played
    and a copy of the game's output, so a later session can find the cause in the bot and fix it."""
    weights_dir = ROOT / build / "test" / "bwapi-data" / "AI"

    def issue(kind: str, job: Job, output: Path | None) -> None:
        stamp = time.strftime("%Y%m%d-%H%M%S")
        kept = None
        if output is not None and output.exists():
            kept = training / "issues" / f"{stamp}-{job[0]}-vs-{job[1]}.log"
            kept.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(output, kept)
        weights_file = weights_dir / f"{job[0]}.json"
        weights = json.loads(weights_file.read_text()) if weights_file.exists() else None
        tail = kept.read_text(errors="replace").splitlines()[-30:] if kept else None
        entry = {"time": stamp, "issue": kind, "bot": job[0], "opponent": job[1], "lane": job[2],
                 "output": str(kept) if kept else None, "tail": tail, "weights": weights}
        training.mkdir(parents=True, exist_ok=True)
        with (training / "issues.log").open("a") as f:
            f.write(json.dumps(entry) + "\n")
        log(f"  issue: {kind}: {job[0]} vs {job[1]}" + (f" (output kept in {kept})" if kept else ""))

    return issue


PARALLEL_STEPS = (6, 4, 2, 1)  # games at once, stepping down when the games use too much memory
MEMORY_CHECK_SECONDS = 2
STALL_SECONDS = 120  # a game whose processes use no CPU for this long is stuck (a deadlock, or waiting forever)
SKIP_AFTER = 3  # a pairing that fails this many times in a row is skipped for the rest of the run
ERROR_MARKERS = ("Segmentation fault", "Assertion failed", "assertion failed", "terminate called", "Abort trap",
                 "Traceback (most recent call last)", "Unhandled exception", "AddressSanitizer", "std::bad_alloc")
DEFAULT_MEMORY_LIMIT = 6e9  # leaves the rest of the computer usable, even on an 8 GB machine
GAME_MEMORY_LIMIT = 1e9  # a single game using more than this is stopped and reported (--game-memory-gb)


class Progress:
    """A status line at the bottom of the terminal, redrawn in place while games run, so it never looks stuck:
    a spinner, moving dots, a progress bar and counts. Log lines print above it. Off when not a terminal."""

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

    def clear(self) -> None:
        if self.enabled and self.line:
            sys.stdout.write("\r\033[K")
            sys.stdout.flush()

    def restore(self) -> None:
        if self.enabled and self.line:
            sys.stdout.write(self.line)
            sys.stdout.flush()

    def done(self) -> None:
        self.clear()
        self.line = ""


PROGRESS = Progress()


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


def cpu_seconds(text: str) -> float:
    """ps's cumulative CPU time: [[dd-]hh:]mm:ss[.ss] (Linux and macOS write it differently)."""
    days, _, clock = text.rpartition("-")
    seconds = 0.0
    for part in clock.split(":"):
        seconds = seconds * 60 + float(part)
    return seconds + (int(days) * 86400 if days else 0)


def tree_usage(pids: list[int]) -> dict[int, tuple[int, float]]:
    """Memory (resident bytes) and CPU time (seconds) used by each of these processes with all its descendants:
    each game is the harness plus the opponent it forks. Uses ps, so it works on macOS and Linux without extra
    packages."""
    try:
        listing = subprocess.run(["ps", "-A", "-o", "pid=,ppid=,rss=,time="], capture_output=True, text=True,
                                 check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return {}
    children: dict[int, list[int]] = {}
    own: dict[int, tuple[int, float]] = {}
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) != 4 or not all(f.isdigit() for f in fields[:3]):
            continue
        try:
            cpu = cpu_seconds(fields[3])
        except ValueError:
            continue
        pid, ppid, kilobytes = map(int, fields[:3])
        children.setdefault(ppid, []).append(pid)
        own[pid] = (kilobytes * 1024, cpu)
    totals = {}
    for root in pids:
        memory, cpu, todo, seen = 0, 0.0, [root], set()
        while todo:
            pid = todo.pop()
            if pid in seen:
                continue
            seen.add(pid)
            memory += own.get(pid, (0, 0.0))[0]
            cpu += own.get(pid, (0, 0.0))[1]
            todo += children.get(pid, [])
        totals[root] = (memory, cpu)
    return totals


def clear_leftover_games(build: str, log: Callable[[str], None]) -> None:
    """Kills games still running from an earlier run that didn't get to stop them (a closed laptop, kill -9):
    they would hold memory and CPU the new run needs. The forked opponents share the harness's command line."""
    pattern = f"{ROOT / build / 'test' / 'tests'} --gtest_filter=Bots.Play"
    found = subprocess.run(["pgrep", "-f", pattern], capture_output=True, text=True).stdout.split()
    if found:
        subprocess.run(["pkill", "-9", "-f", pattern])
        log(f"Cleared {len(found)} game process(es) left over from an earlier run")


def stop(process: subprocess.Popen[bytes]) -> None:
    """Kills a game: the harness and the opponent it forked, which share the harness's process group."""
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except OSError:
        process.kill()


def step_down(cap: int) -> int:
    return next((step for step in PARALLEL_STEPS if step < cap), 1)


def make_runner(build: str, parallel: int, log: Callable[[str], None],
                issue: Callable[[str, Job, Path | None], None] = lambda kind, job, output: None,
                memory_limit: float = DEFAULT_MEMORY_LIMIT, game_memory_limit: float = GAME_MEMORY_LIMIT,
                usage: Callable[[list[int]], dict[int, tuple[int, float]]] = tree_usage) -> Runner:
    """Runs each game as its own headless test harness process, up to `parallel` at once, each in its own folder (as
    tools/run_games.py does), and reads the results the harness appends to replays/results.csv."""
    from run_games import prepare_worker_directory  # noqa: E402

    test_dir = ROOT / build / "test"
    results_file = test_dir / "replays" / "results.csv"
    cap = [parallel]  # games at once; lowered for the rest of the run when the games use more than memory_limit
    failures: dict[tuple[str, str], int] = {}  # failures in a row, per pairing
    skipped: set[tuple[str, str]] = set()  # pairings that kept failing, not played again this run

    def read_results(before: int) -> dict[tuple[str, str], list[str]]:
        results: dict[tuple[str, str], list[str]] = {}
        lines = results_file.read_text().splitlines() if results_file.exists() else []
        for line in lines[max(before, 1):]:
            fields = line.split(",")
            if len(fields) > 3:
                results.setdefault((fields[1], fields[2]), []).append(fields[3])
        return results

    def run(jobs: list[Job], fill: Callable[[], Job] | None,
            lost: Lost | None = None) -> dict[tuple[str, str], list[str]]:
        queue = list(jobs)
        wanted: dict[tuple[str, str], int] = {}
        for us, opponent, _ in jobs:
            wanted[(us, opponent)] = wanted.get((us, opponent), 0) + 1
        before = len(results_file.read_text().splitlines()) if results_file.exists() else 0
        socket_root = Path(tempfile.mkdtemp(prefix="ob", dir="/tmp"))
        running: dict[int, tuple[subprocess.Popen[bytes], float, Job]] = {}
        free = list(range(parallel))
        last_memory_check = time.time()
        extras: set[int] = set()  # slots playing a fill() game, which isn't replayed if it has to be stopped
        progress: dict[int, tuple[float, float]] = {}  # slot: (CPU seconds, when they last went up)
        round_started = time.time()
        ended = [0]  # games finished or stopped this round
        decided = False  # the candidate can no longer pass: the rest of the round is skipped
        for job in jobs:
            if (job[0], job[1]) in skipped:
                queue.remove(job)
                wanted[(job[0], job[1])] -= 1

        def failed(slot: int, kind: str) -> None:
            """A game went wrong: stop it if it is still going, log it, and move on."""
            process, _, job = running.pop(slot)
            if process.poll() is None:
                stop(process)
                process.wait()
            free.append(slot)
            extras.discard(slot)
            progress.pop(slot, None)
            ended[0] += 1
            issue(kind, job, test_dir / "parallel" / str(slot) / "selfplay.log")
            pair = (job[0], job[1])
            wanted[pair] -= 1  # reported already: not a missing result as well
            failures[pair] = failures.get(pair, 0) + 1
            if failures[pair] >= SKIP_AFTER and pair not in skipped:
                skipped.add(pair)
                issue(f"skipped: {SKIP_AFTER} failures in a row, so {pair[0]} vs {pair[1]} is not played again "
                      "until training restarts", job, None)
            if pair in skipped:
                for queued in [j for j in queue if (j[0], j[1]) == pair]:
                    queue.remove(queued)
                    wanted[pair] -= 1

        def start(job: Job) -> None:
            slot = free.pop(0)
            us, opponent, _ = job
            directory = prepare_worker_directory(test_dir, slot)
            env = dict(os.environ)
            env.update({"STARDUST_BOT": us, "STARDUST_OPPONENT": opponent, "STARDUST_GAMES": "1",
                        "OPENBW_ENABLE_UI": "0", "OPENBW_LOCAL_AUTO_DIRECTORY": str(socket_root / str(slot))})
            env.pop("OPENBW_GAME_SPEED", None)  # as fast as the bots allow
            with (directory / "selfplay.log").open("w") as output:
                process = subprocess.Popen([str(test_dir / "tests"), "--gtest_filter=Bots.Play"], cwd=directory,
                                           env=env, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
            running[slot] = (process, time.time(), job)

        try:
            while (queue or running) and not decided:
                self_running = any(job[2] == "self" for _, _, job in running.values())
                self_left = self_running or any(job[2] == "self" for job in queue)
                # The self-play slot first, then the others, keeping a slot for self-play while it has games left
                if free and not self_running:
                    for job in queue:
                        if job[2] == "self":
                            queue.remove(job)
                            start(job)
                            break
                others_allowed = cap[0] - (1 if self_left else 0)
                while free and sum(1 for _, _, job in running.values() if job[2] != "self") < others_allowed:
                    queued = next((job for job in queue if job[2] != "self"), None)
                    if queued is not None:
                        queue.remove(queued)
                        start(queued)
                    elif fill and self_left:
                        extra = next((job for job in (fill() for _ in range(10)) if (job[0], job[1]) not in skipped),
                                     None)
                        if extra is None:
                            break
                        wanted[(extra[0], extra[1])] = wanted.get((extra[0], extra[1]), 0) + 1
                        extras.add(free[0])
                        start(extra)
                    else:
                        break
                time.sleep(0.5)
                self_total = sum(1 for job in jobs if job[2] == "self")
                self_done = self_total - sum(1 for job in queue if job[2] == "self") - sum(
                    1 for _, _, job in running.values() if job[2] == "self")
                minutes, seconds = divmod(int(time.time() - round_started), 60)
                PROGRESS.show(ended[0], sum(wanted.values()),
                              (f"self-play {self_done}/{self_total} · " if self_total else "")
                              + f"{len(running)} playing (max {cap[0]}) · {minutes}m{seconds:02d}s")
                if running and time.time() - last_memory_check >= MEMORY_CHECK_SECONDS:
                    last_memory_check = time.time()
                    now = time.time()
                    measured = usage([process.pid for process, _, _ in running.values()])
                    per_game = {pid: memory for pid, (memory, _) in measured.items()}
                    for slot, (process, _, job) in list(running.items()):
                        if process.pid not in measured or process.poll() is not None:
                            continue
                        memory_used, cpu = measured[process.pid]
                        if memory_used > game_memory_limit:
                            failed(slot, f"memory: the game used {memory_used / 1e9:.1f} GB and was stopped")
                            continue
                        last_cpu, since = progress.get(slot, (-1.0, now))
                        if cpu > last_cpu + 0.5:
                            progress[slot] = (cpu, now)
                        elif now - since > STALL_SECONDS:
                            failed(slot, f"stuck: no CPU used for {STALL_SECONDS} s, so the game was stopped")
                    used = sum(per_game.get(process.pid, 0) for process, _, _ in running.values())
                    if used > memory_limit and cap[0] > 1:
                        cap[0] = step_down(cap[0])
                        log(f"  the games use {used / 1e9:.1f} GB, over {memory_limit / 1e9:g} GB: "
                            f"down to {cap[0]} at once")
                        # Stop the newest games beyond the new cap (never the self-play game) and play them again later
                        newest = sorted((item for item in running.items() if item[1][2][2] != "self"),
                                        key=lambda item: item[1][1], reverse=True)
                        for slot, (process, _, job) in newest[:max(0, len(running) - cap[0])]:
                            stop(process)
                            process.wait()
                            del running[slot]
                            free.append(slot)
                            progress.pop(slot, None)
                            if slot in extras:
                                wanted[(job[0], job[1])] -= 1
                            else:
                                queue.append(job)
                            log(f"  stopped {job[0]} vs {job[1]} to free memory")
                    elif used > memory_limit:
                        log(f"  the games use {used / 1e9:.1f} GB, over {memory_limit / 1e9:g} GB, already one at a time")
                self_before = sum(1 for _, _, job in running.values() if job[2] == "self")
                for slot, (process, started, job) in list(running.items()):
                    output = test_dir / "parallel" / str(slot) / "selfplay.log"
                    if process.poll() is None:
                        if time.time() - started > HANG_SECONDS:
                            failed(slot, f"hang: still running after {HANG_SECONDS} s, so the game was stopped")
                        continue
                    if process.returncode not in (0, 1):
                        failed(slot, f"crash: exit code {process.returncode} after {time.time() - started:.0f} s")
                        continue
                    del running[slot]
                    free.append(slot)
                    extras.discard(slot)
                    progress.pop(slot, None)
                    failures[(job[0], job[1])] = 0
                    ended[0] += 1
                    text = output.read_text(errors="replace") if output.exists() else ""
                    marker = next((line.strip() for line in text.splitlines()
                                   if any(m in line for m in ERROR_MARKERS)), None)
                    if marker:
                        issue(f"error in the output, though the game finished: {marker[:200]}", job, output)
                self_after = sum(1 for _, _, job in running.values() if job[2] == "self")
                if lost and self_after < self_before:
                    remaining = sum(1 for job in queue if job[2] == "self") + self_after
                    so_far = candidate_view(read_results(before))
                    if lost(so_far, remaining):
                        decided = True
                        log(f"  self-play {so_far.count('WON')}/{len(so_far)} won: it can't pass now even winning the "
                            f"{remaining} game(s) left, so the round stops here")
        finally:
            PROGRESS.done()
            for process, _, _ in running.values():
                stop(process)
            shutil.rmtree(socket_root, ignore_errors=True)

        results = read_results(before)
        lines = results_file.read_text().splitlines() if results_file.exists() else []
        for line in lines[max(before, 1):]:
            fields = line.split(",")
            if len(fields) > 3:
                if fields[3] == "DRAW":
                    issue(f"draw: the game hit the frame or time limit after {fields[4] if len(fields) > 4 else '?'} "
                          "frames", (fields[1], fields[2], ""), None)
        for (us, opponent), games in wanted.items():
            got = len(results.get((us, opponent), []))
            if got < games and not decided:
                issue(f"missing results: only {got} of {games} game(s) finished", (us, opponent, ""), None)
        return results

    return run


def summarize_issues(path: Path) -> str:
    """The issue log grouped by kind and pairing, most frequent first: where to start fixing."""
    if not path.exists():
        return "No issues logged."
    groups: dict[tuple[str, str, str], list[dict[str, Any]]] = {}
    for line in path.read_text().splitlines():
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            continue
        kind = str(entry.get("issue", "?")).split(":")[0]
        groups.setdefault((kind, entry.get("bot", "?"), entry.get("opponent", "?")), []).append(entry)
    lines = [f"{sum(len(g) for g in groups.values())} issue(s) in {path}:"]
    for (kind, bot, opponent), entries in sorted(groups.items(), key=lambda item: -len(item[1])):
        last = entries[-1]
        lines.append(f"  {len(entries):4d}  {kind:8s} {bot} vs {opponent}  (last {last.get('time')}"
                     + (f", output {last['output']}" if last.get("output") else "") + ")")
    return "\n".join(lines)


def make_install(build: str) -> Callable[[dict[str, float], dict[str, float]], None]:
    directory = ROOT / build / "test" / "bwapi-data" / "AI"

    def install(best: dict[str, float], candidate: dict[str, float]) -> None:
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "ClaudeOpus55RL-best.json").write_text(json.dumps(best, indent=2) + "\n")
        (directory / "ClaudeOpus55RL-candidate.json").write_text(json.dumps(candidate, indent=2) + "\n")

    return install


def make_approve(approver: str | None, auto: bool, training: Path,
                 log: Callable[[str], None]) -> Callable[[dict[str, Any]], bool]:
    def approve(report: dict[str, Any]) -> bool:
        path = training / "pending.json"
        path.write_text(json.dumps(report, indent=2) + "\n")
        summary = (f"Candidate {report['candidate']} passed: self-play {report['selfplay_score']:.0%} "
                   f"(Elo {report['elo']:+.0f}), Tier 2 {report['tier2_score']:.0%} "
                   f"(best {report['best_tier2_score']:.0%}). Changed: {report['changed']}")
        log(summary)
        if auto:
            log("  auto-approved")
            return True
        if approver:
            result = subprocess.run(shlex.split(approver) + [str(path)])
            log(f"  approver: {'approved' if result.returncode == 0 else 'rejected'}")
            return result.returncode == 0
        while True:
            answer = input(f"{summary}\nPromote it to generation {report['generation']}? [y/n] ").strip().lower()
            if answer in ("y", "yes", "n", "no"):
                return answer.startswith("y")

    return approve


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--hours", type=float, default=0, help="stop after this long (default: only at the goal)")
    parser.add_argument("--games", type=int, default=20, help="self-play games per candidate (default 20)")
    parser.add_argument("--gauntlet-games", type=int, default=2,
                        help="games at least against each training bot per candidate (default 2)")
    parser.add_argument("--test-games", type=int, default=4,
                        help=f"games against the held-out bot, {TEST}, all of which must be won (default 4)")
    parser.add_argument("--parallel", type=int, default=6, help="games at once, one per core (default 6)")
    parser.add_argument("--game-memory-gb", type=float, default=GAME_MEMORY_LIMIT / 1e9,
                        help="the most memory one game (harness and opponent) may use before it is stopped and "
                             "reported (default 1)")
    parser.add_argument("--memory-limit-gb", type=float, default=None,
                        help="the most memory the games may use together before stepping down to 4, 2, then 1 "
                             "game at once (default: 6 GB, or three quarters of the RAM if that is less)")
    parser.add_argument("--build", default="build", help="headless build directory (default: build)")
    parser.add_argument("--approver", help="command that approves a promotion (exit 0), given the report's path")
    parser.add_argument("--auto-approve", action="store_true", help="promote every candidate that passes the gate")
    parser.add_argument("--seed", type=int, help="random seed for the mutations")
    parser.add_argument("--status", action="store_true", help="show where training stands and exit")
    parser.add_argument("--issues", action="store_true", help="sum up training/issues.log and exit")
    args = parser.parse_args()

    TRAINING_DIR.mkdir(parents=True, exist_ok=True)
    log_file = TRAINING_DIR / "selfplay.log"

    def log(message: str) -> None:
        line = f"{time.strftime('%Y-%m-%d %H:%M:%S')} {message}"
        PROGRESS.clear()
        print(line, flush=True)
        PROGRESS.restore()
        with log_file.open("a") as f:
            f.write(line + "\n")

    state = State.load(TRAINING_DIR / "state.json")
    if args.issues:
        print(summarize_issues(TRAINING_DIR / "issues.log"))
        return 0
    if args.status:
        print(f"Generation {state.generation}, Elo {state.elo:+.0f} over ClaudeOpus55, {state.candidates_tried} "
              f"candidate(s) tried, step scale {state.scale:.2f}, goal {'reached' if state.goal_reached else 'not yet'}")
        if state.best_gauntlet:
            print(f"Best against the 5 training bots: {gauntlet_score(state.best_gauntlet):.0%}: " + ", ".join(
                f"{o} {r.count('WON')}/{len(r)}" for o, r in state.best_gauntlet.items()))
        print("Best parameters: " + json.dumps(state.best))
        return 0
    if state.goal_reached:
        print(f"The goal was reached already: the best beat the 5 training bots in every game twice, then {TEST}. "
              "Delete bots/ClaudeOpus55RL/training/state.json to start over.")
        return 0

    tests = ROOT / args.build / "test" / "tests"
    if not tests.exists():
        parser.error(f"no test harness at {tests}: build it first (cmake --build {args.build} -j)")
    listing = subprocess.run([str(tests), "--gtest_filter=Bots.List"], cwd=tests.parent, capture_output=True,
                             text=True).stdout
    missing = [bot for bot in [BEST, CANDIDATE, *TRAINING, TEST] if f"  {bot} (" not in listing]
    if missing:
        recipes = sorted({"UAlbertaBot" if name.startswith("UAlbertaBot") else name for name in missing
                          if name not in (BEST, CANDIDATE)})
        fetch = f"fetch the recipe bots ({' '.join(recipes)}) with tools/fetch_bot.py, then " if recipes else ""
        parser.error(f"not built: {', '.join(missing)}. To build them, {fetch}run: cmake -S . -B {args.build} && "
                     f"cmake --build {args.build} -j 4 --target tests")

    parallel = max(2, args.parallel)
    per_generation = args.games
    minutes = max(args.games, math.ceil(len(TRAINING) * args.gauntlet_games / (parallel - 1))) * SECONDS_PER_GAME / 60
    log(f"START self-play training of ClaudeOpus55RL: generation {state.generation}, Elo {state.elo:+.0f}; up to "
        f"{per_generation} self-play games per candidate, one at a time beside {parallel - 1} games against the training bots, roughly "
        f"{minutes:.0f} minutes each"
        + (f"; stopping after {args.hours:g} h" if args.hours else "; until the goal")
        + f". Goal: win every game against the {len(TRAINING)} training bots, then every test game against {TEST}.")

    approve = make_approve(args.approver, args.auto_approve, TRAINING_DIR, log)
    clear_leftover_games(args.build, log)
    # Games run in their own process groups, so closing the terminal doesn't reach them: stop them on the way out
    for hangup in (signal.SIGHUP, signal.SIGTERM):
        signal.signal(hangup, lambda number, frame: sys.exit(1))
    memory_limit = (args.memory_limit_gb * 1e9 if args.memory_limit_gb
                    else min(DEFAULT_MEMORY_LIMIT, 0.75 * physical_memory()))
    log(f"Memory: the games may use {memory_limit / 1e9:.1f} GB together, {args.game_memory_gb:g} GB each")
    runner = make_runner(args.build, parallel, log, make_issue_log(TRAINING_DIR, args.build, log), memory_limit,
                         args.game_memory_gb * 1e9)
    trainer = Trainer(runner, make_install(args.build), approve, log, TRAINING_DIR,
                      args.games, args.gauntlet_games, random.Random(args.seed), args.test_games)
    start = time.time()
    try:
        while not args.hours or time.time() - start < args.hours * 3600:
            if trainer.generation():
                log(f"GOAL REACHED at generation {trainer.state.generation} (Elo {trainer.state.elo:+.0f}): the best "
                    f"beat the 5 training bots in every game twice in a row, and the held-out {TEST} in every test "
                    f"game. The first iteration of RL succeeded.")
                return 0
            log(f"Elapsed {(time.time() - start) / 3600:.1f} h; generation {trainer.state.generation}, "
                f"Elo {trainer.state.elo:+.0f}, Tier 2 {gauntlet_score(trainer.state.best_gauntlet or {}):.0%}")
    except KeyboardInterrupt:
        log("Stopped; re-run to resume.")
        return 130
    log(f"Time is up after {args.hours:g} h at generation {trainer.state.generation}; re-run to continue.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

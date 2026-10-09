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
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

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
Runner = Callable[[list[Job], Callable[[], Job] | None], dict[tuple[str, str], list[str]]]


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
        candidate, changed = mutate(best, self.state.scale, self.rng)
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

        played = self.runner(jobs, fill)
        selfplay = played.get((CANDIDATE, BEST), [])
        selfplay += ["LOST" if r == "WON" else "WON" if r == "LOST" else r for r in played.get((BEST, CANDIDATE), [])]
        selfplay_score = score(selfplay)
        self.log(f"  self-play against the old version: {selfplay.count('WON')}/{len(selfplay)} won, "
                 f"score {selfplay_score:.0%} (needs {GATE:.0%})")
        gauntlet = self.gauntlet_results(CANDIDATE, played)
        bar = gauntlet_score(self.state.best_gauntlet or {}) - GAUNTLET_TOLERANCE
        self.log(f"  training bots: score {gauntlet_score(gauntlet):.0%} (needs {max(0.0, bar):.0%})")
        entry: dict[str, Any] = {"time": time.time(), "candidate": trial, "generation": self.state.generation,
                                 "params": candidate, "changed": changed, "selfplay": selfplay, "gauntlet": gauntlet}
        passed = selfplay_score >= GATE and gauntlet_score(gauntlet) >= bar
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


def make_runner(build: str, parallel: int, log: Callable[[str], None]) -> Runner:
    """Runs each game as its own headless test harness process, up to `parallel` at once, each in its own folder (as
    tools/run_games.py does), and reads the results the harness appends to replays/results.csv."""
    from run_games import prepare_worker_directory  # noqa: E402

    test_dir = ROOT / build / "test"
    results_file = test_dir / "replays" / "results.csv"

    def run(jobs: list[Job], fill: Callable[[], Job] | None) -> dict[tuple[str, str], list[str]]:
        queue = list(jobs)
        wanted: dict[tuple[str, str], int] = {}
        for us, opponent, _ in jobs:
            wanted[(us, opponent)] = wanted.get((us, opponent), 0) + 1
        before = len(results_file.read_text().splitlines()) if results_file.exists() else 0
        socket_root = Path(tempfile.mkdtemp(prefix="ob", dir="/tmp"))
        running: dict[int, tuple[subprocess.Popen[bytes], float, Job]] = {}
        free = list(range(parallel))

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
                                           env=env, stdout=output, stderr=subprocess.STDOUT)
            running[slot] = (process, time.time(), job)

        try:
            while queue or running:
                self_running = any(job[2] == "self" for _, _, job in running.values())
                self_left = self_running or any(job[2] == "self" for job in queue)
                # The self-play slot first, then the others, keeping a slot for self-play while it has games left
                if free and not self_running:
                    for job in queue:
                        if job[2] == "self":
                            queue.remove(job)
                            start(job)
                            break
                others_allowed = parallel - (1 if self_left else 0)
                while free and sum(1 for _, _, job in running.values() if job[2] != "self") < others_allowed:
                    queued = next((job for job in queue if job[2] != "self"), None)
                    if queued is not None:
                        queue.remove(queued)
                        start(queued)
                    elif fill and self_left:
                        extra = fill()
                        wanted[(extra[0], extra[1])] = wanted.get((extra[0], extra[1]), 0) + 1
                        start(extra)
                    else:
                        break
                time.sleep(0.5)
                for slot, (process, started, job) in list(running.items()):
                    if process.poll() is None:
                        continue
                    del running[slot]
                    free.append(slot)
                    if process.returncode not in (0, 1):
                        log(f"  {job[0]} vs {job[1]} exited {process.returncode} after {time.time() - started:.0f} s "
                            f"(see {test_dir / 'parallel' / str(slot) / 'selfplay.log'})")
        finally:
            for process, _, _ in running.values():
                process.kill()
            shutil.rmtree(socket_root, ignore_errors=True)

        results: dict[tuple[str, str], list[str]] = {}
        lines = results_file.read_text().splitlines() if results_file.exists() else []
        for line in lines[max(before, 1):]:
            fields = line.split(",")
            if len(fields) > 3:
                results.setdefault((fields[1], fields[2]), []).append(fields[3])
        for (us, opponent), games in wanted.items():
            got = len(results.get((us, opponent), []))
            if got < games:
                log(f"  only {got} of {games} game(s) of {us} vs {opponent} finished")
        return results

    return run


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
    parser.add_argument("--build", default="build", help="headless build directory (default: build)")
    parser.add_argument("--approver", help="command that approves a promotion (exit 0), given the report's path")
    parser.add_argument("--auto-approve", action="store_true", help="promote every candidate that passes the gate")
    parser.add_argument("--seed", type=int, help="random seed for the mutations")
    parser.add_argument("--status", action="store_true", help="show where training stands and exit")
    args = parser.parse_args()

    TRAINING_DIR.mkdir(parents=True, exist_ok=True)
    log_file = TRAINING_DIR / "selfplay.log"

    def log(message: str) -> None:
        line = f"{time.strftime('%Y-%m-%d %H:%M:%S')} {message}"
        print(line, flush=True)
        with log_file.open("a") as f:
            f.write(line + "\n")

    state = State.load(TRAINING_DIR / "state.json")
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
        parser.error(f"not built: {', '.join(missing)}. Fetch the recipe bots ({' '.join(recipes)}) with "
                     f"tools/fetch_bot.py, then re-run CMake and build {args.build}")

    parallel = max(2, args.parallel)
    per_generation = args.games
    minutes = max(args.games, math.ceil(len(TRAINING) * args.gauntlet_games / (parallel - 1))) * SECONDS_PER_GAME / 60
    log(f"START self-play training of ClaudeOpus55RL: generation {state.generation}, Elo {state.elo:+.0f}; up to "
        f"{per_generation} self-play games per candidate, one at a time beside {parallel - 1} games against the training bots, roughly "
        f"{minutes:.0f} minutes each"
        + (f"; stopping after {args.hours:g} h" if args.hours else "; until the goal")
        + ". Goal: beat all 7 Tier 2 bots in every game, twice in a row.")

    approve = make_approve(args.approver, args.auto_approve, TRAINING_DIR, log)
    trainer = Trainer(make_runner(args.build, parallel, log), make_install(args.build), approve, log, TRAINING_DIR,
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

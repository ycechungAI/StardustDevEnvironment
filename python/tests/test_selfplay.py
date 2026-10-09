"""tools/selfplay.py: mutations, the gate, Elo, the stop goal and resuming, with a fake game runner."""

import json
import time
import random
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import selfplay  # noqa: E402

# The fake bots' strength: how close the parameters are to these
IDEAL = {"first_attack_army": 18.0, "retreat_ratio": 1.7, "terran_opening": 1.0}


def strength(params: dict[str, float]) -> float:
    distance = (abs(params["first_attack_army"] - 18) / 6 + abs(params["retreat_ratio"] - 1.7) / 0.45
                + abs(params["terran_opening"] - 1))
    return 1.0 / (1.0 + distance)


class FakeGames:
    def __init__(self, seed: int) -> None:
        self.rng = random.Random(seed)
        self.weights: dict[str, dict[str, float]] = {}
        self.played = 0

    def install(self, best: dict[str, float], candidate: dict[str, float]) -> None:
        self.weights = {selfplay.BEST: best, selfplay.CANDIDATE: candidate}

    def run(self, jobs: list[tuple[str, str, str]], fill: Any, lost: Any = None) -> dict[tuple[str, str], list[str]]:
        if fill:
            jobs = jobs + [fill() for _ in range(5)]  # the slots beside self-play keep playing
        jobs = sorted(jobs, key=lambda job: job[2] != "self")  # self-play first, so it can stop the round early
        results: dict[tuple[str, str], list[str]] = {}
        self_left = sum(1 for job in jobs if job[2] == "self")
        for us, opponent, lane in jobs:
            if lane == "self":
                self_left -= 1
            elif lost and lost(selfplay.candidate_view(results), self_left):
                break
            self.played += 1
            mine = strength(self.weights[us])
            theirs = strength(self.weights[opponent]) if opponent in self.weights else 0.15
            p = mine ** 2 / (mine ** 2 + theirs ** 2)
            results.setdefault((us, opponent), []).append("WON" if self.rng.random() < p else "LOST")
        return results


def trainer(tmp_path: Path, games: FakeGames, approve: Any = lambda report: True) -> selfplay.Trainer:
    return selfplay.Trainer(games.run, games.install, approve, lambda message: None, tmp_path, games=20,
                            gauntlet_games=2, rng=random.Random(1))


def test_mutations_stay_in_bounds_and_change_something() -> None:
    rng = random.Random(0)
    best = selfplay.defaults()
    for _ in range(500):
        candidate, changed = selfplay.mutate(best, 2.0, rng)
        assert 1 <= len(changed) <= 3
        for p in selfplay.PARAMS:
            assert p.low <= candidate[p.name] <= p.high
            if p.integer:
                assert candidate[p.name] == round(candidate[p.name])
        assert all(candidate[name] != best[name] for name in changed)


def test_scores_and_elo() -> None:
    assert selfplay.score(["WON", "DRAW", "LOST", "WON"]) == 0.625
    assert selfplay.elo_margin(0.5) == 0
    assert round(selfplay.elo_margin(0.75)) == 191
    assert selfplay.elo_margin(1.0) < 1000
    assert not selfplay.beats_old_version(["WON"] * 12 + ["LOST"] * 8)
    assert selfplay.beats_old_version(["WON"] * 13 + ["LOST"] * 7)
    assert selfplay.beats_old_version(["WON"] * 3)


def test_the_gate_rejects_a_weaker_candidate(tmp_path: Path) -> None:
    games = FakeGames(0)
    t = trainer(tmp_path, games)
    t.state.best = dict(selfplay.defaults(), **IDEAL)  # nothing beats the ideal
    t.generation()
    entry = json.loads((tmp_path / "history.jsonl").read_text().splitlines()[-1])
    assert not entry["promoted"]
    assert t.state.generation == 0


def test_training_improves_reaches_the_goal_and_stops(tmp_path: Path) -> None:
    games = FakeGames(2)
    t = trainer(tmp_path, games)
    for _ in range(3000):  # only 3 of the parameters matter to the fake bots, so most candidates change nothing
        if t.generation():
            break
    assert t.state.goal_reached
    assert t.state.generation > 0 and t.state.elo > 0
    assert strength(t.state.best or {}) > strength(selfplay.defaults())
    assert selfplay.perfect(t.state.best_gauntlet or {}, 2)
    assert t.state.tested_generation == t.state.generation
    # Saved: a new trainer resumes from the same state
    resumed = trainer(tmp_path, games)
    assert resumed.state.generation == t.state.generation and resumed.state.goal_reached
    assert json.loads((tmp_path / "best.json").read_text()) == t.state.best
    assert (tmp_path / "generations" / f"{t.state.generation:03d}.json").exists()


def test_a_rejected_approval_keeps_the_best(tmp_path: Path) -> None:
    games = FakeGames(3)
    reports: list[dict[str, Any]] = []

    def refuse(report: dict[str, Any]) -> bool:
        reports.append(report)
        return False

    t = trainer(tmp_path, games, refuse)
    for _ in range(60):
        t.generation()
    assert reports, "some candidate should have passed the gate"
    assert t.state.generation == 0 and t.state.best == selfplay.defaults()
    assert {"selfplay_score", "tier2_score", "elo", "changed"} <= set(reports[0])


def test_five_bots_to_train_on_and_one_held_out_to_test() -> None:
    assert len(selfplay.TIER2) == 7
    assert len(selfplay.TRAINING) == 5
    assert selfplay.TEST == "ZZZKBot" and selfplay.DROPPED == "BunkerBoxer"
    assert selfplay.TEST not in selfplay.TRAINING and selfplay.DROPPED not in selfplay.TRAINING


def test_a_failed_test_waits_for_the_next_generation(tmp_path: Path) -> None:
    games = FakeGames(4)
    t = trainer(tmp_path, games)
    t.state.best = dict(selfplay.defaults(), **IDEAL)
    t.state.best_gauntlet = {o: ["WON", "WON"] for o in selfplay.TRAINING}
    games.install(t.state.best, t.state.best)
    real_run = games.run

    def lose_the_test(jobs: Any, fill: Any, lost: Any = None) -> Any:
        results = real_run(jobs, fill, lost)
        if (selfplay.BEST, selfplay.TEST) in results:
            results[(selfplay.BEST, selfplay.TEST)] = ["LOST"] * len(results[(selfplay.BEST, selfplay.TEST)])
        return results

    t.runner = lose_the_test
    assert not t.check_goal()
    assert t.state.tested_generation == t.state.generation
    played = games.played
    assert not t.check_goal()  # not tested again until a new generation
    assert games.played == played


def test_every_parameter_is_read_by_the_bot() -> None:
    source = (Path(__file__).resolve().parents[2] / "bots" / "ClaudeOpus55RL" / "ClaudeOpus55RL.cpp").read_text()
    for p in selfplay.PARAMS:
        assert f'"{p.name}"' in source, p.name
        assert p.low <= p.default <= p.high, p.name


def test_issues_are_logged_with_the_weights(tmp_path: Path) -> None:
    build = tmp_path / "build"
    weights = build / "test" / "bwapi-data" / "AI"
    weights.mkdir(parents=True)
    (weights / f"{selfplay.CANDIDATE}.json").write_text(json.dumps({"retreat_ratio": 1.4}))
    output = tmp_path / "game.log"
    output.write_text("Segmentation fault")
    selfplay.ROOT, root = tmp_path, selfplay.ROOT
    try:
        issue = selfplay.make_issue_log(tmp_path / "training", "build", lambda message: None)
        issue("crash: exit code 139", (selfplay.CANDIDATE, "Stone", "gauntlet"), output)
    finally:
        selfplay.ROOT = root
    entry = json.loads((tmp_path / "training" / "issues.log").read_text())
    assert entry["issue"].startswith("crash") and entry["opponent"] == "Stone"
    assert entry["weights"] == {"retreat_ratio": 1.4}
    assert Path(entry["output"]).read_text() == "Segmentation fault"


RECORD_A_WIN = 'echo "t,$STARDUST_BOT,$STARDUST_OPPONENT,WON,100,map" >> replays/results.csv'


def fake_harness(tmp_path: Path, script: str = f"sleep 3\n{RECORD_A_WIN}") -> None:
    """A test harness stand-in: by default each game takes 3 seconds, then records a win, as the real one does."""
    test = tmp_path / "build" / "test"
    for folder in ("maps", "replays", "bwapi-data/AI"):
        (test / folder).mkdir(parents=True, exist_ok=True)
    (test / "replays" / "results.csv").write_text("time,player,opponent,result,frames,map\n")
    harness = test / "tests"
    harness.write_text(f"#!/bin/sh\n{script}\n")
    harness.chmod(0o755)


def busy(memory: float) -> Any:
    """A usage reading: this much memory per game, and CPU time going up, as in a game being played."""
    start = time.time()
    return lambda pids: {pid: (int(memory), time.time() - start) for pid in pids}


def test_too_much_memory_steps_down_and_replays_the_stopped_games(tmp_path: Path, monkeypatch: Any) -> None:
    fake_harness(tmp_path)
    monkeypatch.setattr(selfplay, "ROOT", tmp_path)
    log: list[str] = []
    issues: list[str] = []
    runner = selfplay.make_runner("build", 6, log.append, lambda kind, job, output: issues.append(kind),
                                  memory_limit=12e9, profile=selfplay.MemoryProfile(None, fixed=4e9), usage=busy(3e9))
    jobs = [(selfplay.CANDIDATE, bot, "gauntlet") for bot in selfplay.TRAINING] + [(selfplay.CANDIDATE, "Stone", "gauntlet")]
    results = runner(jobs, None)
    assert any("down to 4 at once" in line for line in log)  # 6 games of 3 GB is over 12 GB; 4 isn't
    assert sum(len(r) for r in results.values()) == 6  # the 2 stopped games were played again
    assert not issues


def test_a_game_leaking_memory_is_stopped_and_reported(tmp_path: Path, monkeypatch: Any) -> None:
    fake_harness(tmp_path)
    monkeypatch.setattr(selfplay, "ROOT", tmp_path)
    issues: list[str] = []
    runner = selfplay.make_runner("build", 2, lambda message: None, lambda kind, job, output: issues.append(kind),
                                  usage=busy(2e9))
    runner([(selfplay.CANDIDATE, "Stone", "gauntlet")], None)
    assert any(kind.startswith("memory:") for kind in issues)


def test_a_stuck_game_is_stopped_and_reported(tmp_path: Path, monkeypatch: Any) -> None:
    fake_harness(tmp_path, f"sleep 30\n{RECORD_A_WIN}")
    monkeypatch.setattr(selfplay, "ROOT", tmp_path)
    monkeypatch.setattr(selfplay, "STALL_SECONDS", 1)
    issues: list[str] = []
    runner = selfplay.make_runner("build", 2, lambda message: None, lambda kind, job, output: issues.append(kind),
                                  usage=lambda pids: {pid: (int(1e8), 5.0) for pid in pids})  # no CPU used
    started = time.time()
    runner([(selfplay.CANDIDATE, "Stone", "gauntlet")], None)
    assert time.time() - started < 15
    assert any(kind.startswith("stuck:") for kind in issues)


def test_a_pairing_that_keeps_failing_is_skipped(tmp_path: Path, monkeypatch: Any) -> None:
    fake_harness(tmp_path, "exit 3")  # crashes at once
    monkeypatch.setattr(selfplay, "ROOT", tmp_path)
    issues: list[str] = []
    runner = selfplay.make_runner("build", 2, lambda message: None, lambda kind, job, output: issues.append(kind))
    runner([(selfplay.CANDIDATE, "Stone", "gauntlet")] * 8, None)
    # The other slot's game, already running when the pairing was skipped, may crash too
    assert selfplay.SKIP_AFTER <= sum(kind.startswith("crash:") for kind in issues) <= selfplay.SKIP_AFTER + 1
    assert sum(kind.startswith("skipped:") for kind in issues) == 1
    assert not any(kind.startswith("missing") for kind in issues)  # skipped games aren't waited for
    issues.clear()
    runner([(selfplay.CANDIDATE, "Stone", "gauntlet")], None)  # still skipped in the next round
    assert not issues


def test_errors_in_a_finished_games_output_are_reported(tmp_path: Path, monkeypatch: Any) -> None:
    fake_harness(tmp_path, f"echo 'Assertion failed: unit != nullptr'\n{RECORD_A_WIN}")
    monkeypatch.setattr(selfplay, "ROOT", tmp_path)
    issues: list[str] = []
    runner = selfplay.make_runner("build", 2, lambda message: None, lambda kind, job, output: issues.append(kind))
    results = runner([(selfplay.CANDIDATE, "Stone", "gauntlet")], None)
    assert results == {(selfplay.CANDIDATE, "Stone"): ["WON"]}
    assert issues and "Assertion failed" in issues[0]


def test_the_issue_summary_groups_by_kind_and_pairing(tmp_path: Path) -> None:
    log = tmp_path / "issues.log"
    assert selfplay.summarize_issues(log) == "No issues logged."
    entries = [{"time": "1", "issue": "crash: exit code 139", "bot": "A", "opponent": "Stone"},
               {"time": "2", "issue": "crash: exit code 6", "bot": "A", "opponent": "Stone"},
               {"time": "3", "issue": "stuck: no CPU", "bot": "A", "opponent": "ZZZKBot"}]
    log.write_text("".join(json.dumps(e) + "\n" for e in entries))
    summary = selfplay.summarize_issues(log).splitlines()
    assert summary[0].startswith("3 issue(s)")
    assert "2  crash" in summary[1] and "Stone" in summary[1]
    assert "stuck" in summary[2]


def test_cannot_pass_is_exact() -> None:
    # 20 games need 13 wins: 12 losses leave at most 8, 8 losses leave 12
    assert selfplay.cannot_pass(["LOST"] * 8, 12)
    assert not selfplay.cannot_pass(["LOST"] * 7, 13)
    for wins in range(21):
        for played in range(wins, 21):
            results = ["WON"] * wins + ["LOST"] * (played - wins)
            if selfplay.cannot_pass(results, 20 - played):  # then no ending of the remaining games passes
                assert not selfplay.beats_old_version(results + ["WON"] * (20 - played))


def test_a_candidate_that_cannot_pass_stops_the_round(tmp_path: Path, monkeypatch: Any) -> None:
    # The old version wins every game, whichever side it plays
    fake_harness(tmp_path, 'sleep 0.3\nif [ "$STARDUST_BOT" = ClaudeOpus55RL ]; then R=WON; else R=LOST; fi\n'
                           'echo "t,$STARDUST_BOT,$STARDUST_OPPONENT,$R,100,map" >> replays/results.csv')
    monkeypatch.setattr(selfplay, "ROOT", tmp_path)
    log: list[str] = []
    issues: list[str] = []
    runner = selfplay.make_runner("build", 3, log.append, lambda kind, job, output: issues.append(kind))
    jobs = [(selfplay.CANDIDATE, selfplay.BEST, "self") if n % 2 == 0 else (selfplay.BEST, selfplay.CANDIDATE, "self")
            for n in range(20)] + [(selfplay.CANDIDATE, "Stone", "gauntlet")] * 10
    played = runner(jobs, None, selfplay.cannot_pass)
    assert len(selfplay.candidate_view(played)) == 8  # 8 losses of 20: 13 wins are out of reach
    assert any("round stops here" in line for line in log)
    assert not issues  # the games stopped aren't reported as missing


def test_a_candidate_is_never_tried_twice_against_the_same_best(tmp_path: Path) -> None:
    games = FakeGames(5)
    t = trainer(tmp_path, games, approve=lambda report: False)
    for _ in range(40):
        t.generation()
    tried = [json.dumps(json.loads(line)["params"], sort_keys=True)
             for line in (tmp_path / "history.jsonl").read_text().splitlines()]
    assert len(tried) == len(set(tried))
    assert trainer(tmp_path, games).tried == set(tried)  # remembered after a restart


def test_the_memory_limit_comes_from_measured_games(tmp_path: Path) -> None:
    profile = selfplay.MemoryProfile(tmp_path / "memory.json")
    stone = (selfplay.CANDIDATE, "Stone", "gauntlet")
    assert profile.limit(stone) == selfplay.GAME_MEMORY_LIMIT  # not measured yet
    for peak in (300e6, 320e6, 310e6):
        profile.record(stone, peak)
    assert profile.limit(stone) == 3 * 310e6  # three times the typical game
    assert profile.limit((selfplay.CANDIDATE, selfplay.BEST, "self")) == selfplay.GAME_MEMORY_LIMIT
    assert selfplay.MemoryProfile(tmp_path / "memory.json").limit(stone) == 3 * 310e6  # kept across runs
    assert "Stone: typical 310 MB" in profile.summary()
    assert selfplay.MemoryProfile(None, fixed=2e9).limit(stone) == 2e9  # --game-memory-gb
    for peak in (900e6, 900e6, 900e6, 900e6):
        profile.record(stone, peak)
    assert profile.limit(stone) == 2e9  # never more than 2 GB


def test_an_unmeasured_opponent_is_allowed_1_then_1_5_then_2_gb(tmp_path: Path) -> None:
    profile = selfplay.MemoryProfile(tmp_path / "memory.json")
    uab = (selfplay.CANDIDATE, "UAlbertaBotZerg", "gauntlet")
    limits = [profile.limit(uab)]
    for _ in range(3):
        profile.stopped(uab)
        limits.append(profile.limit(uab))
    assert limits == [1e9, 1.5e9, 2e9, 2e9]
    assert selfplay.MemoryProfile(tmp_path / "memory.json").limit(uab) == 2e9  # kept across runs


def test_finished_games_are_measured(tmp_path: Path, monkeypatch: Any) -> None:
    fake_harness(tmp_path)
    monkeypatch.setattr(selfplay, "ROOT", tmp_path)
    profile = selfplay.MemoryProfile(None)
    runner = selfplay.make_runner("build", 2, lambda message: None, profile=profile, usage=busy(2e8))
    runner([(selfplay.CANDIDATE, "Stone", "gauntlet")], None)
    assert profile.peaks == {"Stone": [2e8]}


def test_self_play_uses_several_slots(tmp_path: Path, monkeypatch: Any) -> None:
    fake_harness(tmp_path, f"sleep 1\n{RECORD_A_WIN}")
    monkeypatch.setattr(selfplay, "ROOT", tmp_path)
    runner = selfplay.make_runner("build", 4, lambda message: None, self_slots=3)
    jobs = [(selfplay.CANDIDATE, selfplay.BEST, "self")] * 6 + [(selfplay.CANDIDATE, "Stone", "gauntlet")] * 2
    started = time.time()
    played = runner(jobs, None)
    assert len(played[(selfplay.CANDIDATE, selfplay.BEST)]) == 6 and len(played[(selfplay.CANDIDATE, "Stone")]) == 2
    assert time.time() - started < 4.5  # 6 self-play games 3 at a time beside the others; one at a time takes 6 s

"""tools/selfplay.py: mutations, the gate, Elo, the stop goal and resuming, with a fake game runner."""

import json
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

    def run(self, jobs: list[tuple[str, str, str]], fill: Any) -> dict[tuple[str, str], list[str]]:
        if fill:
            jobs = jobs + [fill() for _ in range(5)]  # the slots beside self-play keep playing
        results: dict[tuple[str, str], list[str]] = {}
        for us, opponent, _ in jobs:
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
    for _ in range(400):
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

    def lose_the_test(jobs: Any, fill: Any) -> Any:
        results = real_run(jobs, fill)
        if (selfplay.BEST, selfplay.TEST) in results:
            results[(selfplay.BEST, selfplay.TEST)] = ["LOST"] * len(results[(selfplay.BEST, selfplay.TEST)])
        return results

    t.runner = lose_the_test
    assert not t.check_goal()
    assert t.state.tested_generation == t.state.generation
    played = games.played
    assert not t.check_goal()  # not tested again until a new generation
    assert games.played == played

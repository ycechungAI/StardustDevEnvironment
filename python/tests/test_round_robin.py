"""The tiered ratings of tools/round_robin.py."""

import math
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import round_robin  # noqa: E402


def games(a: str, b: str, a_wins: int, b_wins: int, draws: int = 0) -> list[tuple[str, str, float]]:
    return [(a, b, 1.0)] * a_wins + [(a, b, 0.0)] * b_wins + [(a, b, 0.5)] * draws


def test_even_results_rate_bots_equally() -> None:
    fitted = round_robin.fit(["A", "B"], games("A", "B", 5, 5))
    assert fitted["A"][0] == pytest.approx(0, abs=1e-6)
    assert fitted["B"][0] == pytest.approx(0, abs=1e-6)


def test_ratings_follow_results_on_the_elo_scale() -> None:
    # Without the prior, 3 wins in 4 is log10(3) * 400 = 191 Elo apart; the prior pulls that in a little
    fitted = round_robin.fit(["A", "B"], games("A", "B", 300, 100))
    assert fitted["A"][0] - fitted["B"][0] == pytest.approx(400 * math.log10(3), abs=3)


def test_winning_every_game_still_gives_a_finite_rating() -> None:
    fitted = round_robin.fit(["A", "B"], games("A", "B", 10, 0))
    assert 0 < fitted["A"][0] - fitted["B"][0] < 1000
    assert fitted["A"][1] < round_robin.PRIOR_SD


def test_more_games_mean_smaller_deviations() -> None:
    few = round_robin.fit(["A", "B"], games("A", "B", 6, 4))
    many = round_robin.fit(["A", "B"], games("A", "B", 60, 40))
    assert many["A"][1] < few["A"][1]


def test_last_place_is_at_the_floor_and_the_rest_above() -> None:
    results = games("A", "B", 8, 2) + games("A", "C", 9, 1) + games("B", "C", 7, 3)
    table = round_robin.rate_tier(["A", "B", "C"], {"A", "B", "C"}, results, 1500, 2000, {})
    assert [entry.bot for entry in table] == ["A", "B", "C"]
    assert table[-1].rating == pytest.approx(1500)
    assert table[0].rating > table[1].rating > 1500
    assert table[0].record.won == 17 and table[0].record.games == 20


def test_a_tier_stays_below_the_next_floor() -> None:
    results = games("A", "B", 200, 0)
    table = round_robin.rate_tier(["A", "B"], {"A", "B"}, results, 1500, 1600, {})
    assert table[0].rating == 1599
    assert "capped" in table[0].note and "promote?" in table[0].note
    assert "relegate?" in table[1].note


def test_bots_not_played_are_listed_at_the_floor() -> None:
    table = round_robin.rate_tier(["A", "B", "Gone"], {"A", "B"}, games("A", "B", 5, 5), 2000, None,
                                  {"Gone": "not built here"})
    gone = table[-1]
    assert (gone.bot, gone.rating, gone.played, gone.note) == ("Gone", 2000, False, "not built here")

"""tools/watch.py's grid and pairings, and tools/game_feed.py's events and statistics."""

import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import game_feed  # noqa: E402
import watch  # noqa: E402


def test_pairings_follow_the_requested_order() -> None:
    assert watch.pairings(["A", "B", "C", "D"]) == [("A", "B"), ("A", "C"), ("A", "D"), ("B", "C"), ("B", "D"),
                                                    ("C", "D")]
    assert watch.pairings(["A", "B", "C"]) == [("A", "B"), ("A", "C"), ("B", "C")]


def test_the_python_port_always_plays_as_us() -> None:
    assert watch.pairings(["A", "StardustPy"]) == [("StardustPy", "A")]


def test_grid_shapes() -> None:
    assert [watch.grid_shape(n) for n in (1, 2, 3, 6)] == [(1, 1), (1, 2), (1, 3), (2, 3)]


def test_six_games_fit_the_screen_beside_the_details_window() -> None:
    games, details = watch.layout(6, 1512, 982, details=True)
    assert len(games) == 6
    right = max(g.x for g in games) + watch.GAME_WIDTH * games[0].scale
    height = watch.GAME_HEIGHT if games[0].hud else watch.GAME_HEIGHT - 146
    bottom = max(g.y for g in games) + height * games[0].scale
    assert right <= 1512 - watch.DETAILS_WIDTH + 1
    assert bottom <= 982 - watch.DOCK + 1
    assert games[3].y > games[0].y and games[3].x == games[0].x  # game 4 starts the second row
    assert details.x >= right - 1


def test_one_game_is_not_enlarged() -> None:
    games, _ = watch.layout(1, 3000, 2000, details=True)
    assert games[0].scale == 1.0 and games[0].hud


def test_small_windows_leave_out_the_hud() -> None:
    games, _ = watch.layout(6, 1512, 982, details=True)
    assert not games[0].hud


def test_speed() -> None:
    assert (watch.ms_per_frame(1), watch.ms_per_frame(2), watch.ms_per_frame(3), watch.ms_per_frame(0)) == (42, 21, 14, 0)


def snapshot(frame: int, **players: dict[str, Any]) -> dict[str, Any]:
    base = {"race": "Protoss", "color": 0, "local": False, "minerals": 0, "gas": 0, "gathered": [0, 0],
            "supply": [4, 9], "workers": 4, "units": {}, "production": [], "attacked": [], "born": [], "died": []}
    return {"frame": frame,
            "players": [dict(base, slot=slot, name=name, **fields)
                        for slot, (name, fields) in enumerate(players.items())]}


def test_events_and_statistics() -> None:
    model = game_feed.GameModel()
    model.apply(snapshot(0, Us={"local": True, "units": {"Nexus": [1, 0]}}, Them={}))
    events = model.apply(snapshot(24, Us={"local": True, "born": ["Gateway"], "units": {"Nexus": [1, 0], "Gateway": [0, 1]},
                                         "attacked": [{"name": "Nexus", "x": 1, "y": 2}]},
                                  Them={"died": ["Zergling", "Zergling"]}))
    texts = {(event.slot, event.text) for event in events}
    assert (0, "started Gateway") in texts
    assert (0, "Nexus under attack!") in texts
    assert (1, "lost 2 Zergling") in texts
    # Under attack again a second later: not repeated
    events = model.apply(snapshot(48, Us={"local": True, "units": {"Nexus": [1, 0], "Gateway": [1, 0]},
                                         "attacked": [{"name": "Nexus", "x": 1, "y": 2}]}, Them={}))
    texts = {(event.slot, event.text) for event in events}
    assert (0, "Gateway finished") in texts
    assert not any("under attack" in text for _, text in texts)

    assert model.stats[0].produced_buildings == 1
    assert model.stats[0].killed_units == 2
    assert model.stats[1].lost_units == 2

    model.apply({"end": {"result": "WON", "us": "Us", "opponent": "Them", "map": "M"}})
    assert model.result_for(0) == "VICTORY"
    assert model.result_for(1) == "DEFEAT"


def test_feed_reader_waits_for_complete_lines(tmp_path: Path) -> None:
    path = tmp_path / "status.jsonl"
    reader = game_feed.FeedReader(path)
    path.write_text('{"frame": 24, "players": []}\n{"frame": 4')
    reader.poll()
    assert reader.model.frame == 24
    with path.open("a") as f:
        f.write('8, "players": []}\n')
    reader.poll()
    assert reader.model.frame == 48

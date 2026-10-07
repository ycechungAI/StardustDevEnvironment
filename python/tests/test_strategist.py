"""Offline tests of strategist logic that doesn't need a running game."""

import random

import numpy as np
from bwapi import Position, TilePosition, TilePositions, UnitTypes
from stardust.cpp import INT_MAX, wrap_i32
from stardust.strategist.plays.scouting import early_game_worker_scout as scout_play
from stardust.strategist.strategy_engines.pv_p.pv_p import PvP
from stardust.strategist.strategy_engines.pv_t.pv_t import PvT
from stardust.strategist.strategy_engines.pv_u.pv_u import PvU
from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ
from stardust.util import geo
from tests.fakes import FakeGame, fake_game


def test_vectorized_distances_match_geo() -> None:
    rng = random.Random(1)
    dx = np.array([rng.randrange(0, 4000) for _ in range(500)], dtype=np.int64)
    dy = np.array([rng.randrange(0, 4000) for _ in range(500)], dtype=np.int64)
    approximate = scout_play._approximate_distances(dx, dy)
    for i in range(len(dx)):
        assert approximate[i] == geo.approximate_distance(int(dx[i]), 0, int(dy[i]), 0)

    center = Position(1000, 1200)
    px = np.array([rng.randrange(0, 4000) for _ in range(500)], dtype=np.int64)
    py = np.array([rng.randrange(0, 4000) for _ in range(500)], dtype=np.int64)
    edge = scout_play._edge_to_point_distances(UnitTypes.Protoss_Probe, center, px, py)
    for i in range(len(px)):
        assert edge[i] == geo.edge_to_point_distance(UnitTypes.Protoss_Probe, center, Position(int(px[i]),
                                                                                                int(py[i])))


def _literal_highest_priority_tile(scout_tiles: dict[int, set[TilePosition]], last_seen: list[int], width: int,
                                   position: Position) -> TilePosition:
    """EarlyGameWorkerScout::getHighestPriorityScoutTile as written in Stardust."""
    highest_priority_frame = INT_MAX
    highest_priority_dist = INT_MAX
    highest_priority = TilePositions.Invalid
    for priority in sorted(scout_tiles):
        for tile in sorted(scout_tiles[priority], key=lambda t: (t.x, t.y)):
            desired_frame = last_seen[tile.x + tile.y * width] + priority
            if desired_frame > highest_priority_frame:
                continue

            dist = geo.edge_to_point_distance(UnitTypes.Protoss_Probe, position, Position(tile) + Position(16, 16))
            if desired_frame < highest_priority_frame or dist < highest_priority_dist:
                highest_priority_frame = desired_frame
                highest_priority_dist = dist
                highest_priority = tile
    return highest_priority


def test_highest_priority_scout_tile_matches_stardust_scan() -> None:
    width = height = 64
    rng = random.Random(7)
    with fake_game(FakeGame(width, height)):
        for _ in range(50):
            tiles_by_priority: dict[int, set[TilePosition]] = {}
            for priority in rng.sample([120, 480, 600, 800, 960, 1200], 3):
                tiles_by_priority[priority] = {TilePosition(rng.randrange(width), rng.randrange(height))
                                               for _ in range(rng.randrange(1, 60))}

            # Few distinct values, so there are plenty of ties to break
            last_seen = [rng.choice([-1, 0, 100, 200]) for _ in range(width * height)]
            position = Position(rng.randrange(width * 32), rng.randrange(height * 32))

            groups = {priority: scout_play._TileGroup(tiles) for priority, tiles in tiles_by_priority.items()}
            assert (scout_play._highest_priority_tile(groups, np.array(last_seen, dtype=np.int64),
                                                      UnitTypes.Protoss_Probe, position)
                    == _literal_highest_priority_tile(tiles_by_priority, last_seen, width, position))


def test_strategy_names_match_stardust() -> None:
    # These are written to the opponent model, so must stay compatible with Stardust's files
    assert [s.value for s in PvP.OurStrategy] == [
        "ForgeExpandDT", "TwoGateDT", "EarlyGameDefense", "AntiZealotRush", "AntiDarkTemplarRush", "FastExpansion",
        "Defensive", "Normal", "3GateRobo", "DTExpand", "MidGame"]
    assert PvT.TerranStrategy.NormalOpening.value == "Normal"
    assert PvT.OurStrategy.NormalOpening.value == "Normal"
    assert [s.value for s in PvZ.OurStrategy] == [
        "EarlyGameDefense", "AntiAllIn", "AntiSunkenContain", "SairSpeedlot", "FFEDragoons", "FastExpansion",
        "Defensive", "Normal", "MidGame"]
    assert [s.value for s in PvU.OurStrategy] == ["TwoGateZealots", "ForgeFastExpand"]

    # Apart from the renamed ones, the names are the member names
    for enum in (PvP.ProtossStrategy, PvP.OurStrategy, PvT.TerranStrategy, PvT.OurStrategy, PvZ.ZergStrategy,
                 PvZ.OurStrategy):
        for member in enum:
            if member.name not in ("ThreeGateRobo", "NormalOpening"):
                assert member.value == member.name


def test_rushing_and_proxy_flags() -> None:
    engine = PvP()
    assert not engine.is_enemy_rushing()
    engine.enemy_strategy = PvP.ProtossStrategy.ProxyRush
    assert engine.is_enemy_rushing() and engine.is_enemy_proxy()

    zerg = PvZ()
    zerg.enemy_strategy = PvZ.ZergStrategy.ZerglingRush
    assert zerg.is_enemy_rushing() and not zerg.is_enemy_proxy()
    assert zerg.get_enemy_strategy() == "ZerglingRush"


def test_wrap_i32() -> None:
    assert wrap_i32(INT_MAX) == INT_MAX
    assert wrap_i32(INT_MAX + 1) == -(2 ** 31)
    assert wrap_i32(INT_MAX + 100) == -(2 ** 31) + 99
    assert wrap_i32(-5) == -5

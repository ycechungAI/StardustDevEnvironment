"""game_map's numpy creep check against a literal port of Stardust's loop."""

import random
from typing import Any

from bwapi import TilePosition
from stardust import common
from stardust.map import game_map
from tests.fakes import FakeGame, fake_game


class _FakeBase:
    def __init__(self, tile: TilePosition) -> None:
        self.tile = tile

    def get_tile_position(self) -> TilePosition:
        return self.tile


def _literal_check_creep(game: FakeGame, tile: TilePosition) -> bool:
    for x in (*range(-8, 0), *range(4, 12)):
        for y in (*range(-5, 0), *range(2, 8)):
            tile_x = tile.x + x
            tile_y = tile.y + y
            if tile_x < 0 or tile_x >= game.width or tile_y < 0 or tile_y >= game.height:
                continue
            if game.hasCreep(tile_x, tile_y):
                return True
    return False


def test_check_creep_matches_stardust(monkeypatch: Any) -> None:
    import stardust.opponent as opponent

    monkeypatch.setattr(opponent, "can_be_race", lambda race: True)
    monkeypatch.setattr(common, "current_frame", 0)
    rng = random.Random(5)
    game = FakeGame(64, 48)
    monkeypatch.setattr(game_map, "_map_width", game.width)
    monkeypatch.setattr(game_map, "_map_height", game.height)
    with fake_game(game):
        found = 0
        for frame in range(500):
            # A few creep tiles, often inside the depot's own rows or columns, which don't count
            game.creep = set()
            tile = TilePosition(rng.randrange(-4, game.width + 4), rng.randrange(-4, game.height + 4))
            for _ in range(rng.randrange(0, 3)):
                game.creep.add((tile.x + rng.randrange(-10, 14), tile.y + rng.randrange(-7, 10)))
            common.current_frame = frame
            expected = _literal_check_creep(game, tile)
            found += expected
            assert game_map._check_creep(_FakeBase(tile)) == expected  # type: ignore[arg-type]
        assert 50 < found < 450

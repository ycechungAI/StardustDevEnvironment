"""Offline tests of the block templates used for building placement."""

import math

import pytest

from bwapi import TilePosition, TilePositions
from tests.fakes import fake_game
from stardust.builder.blocks.block_12x8 import Block12x8
from stardust.builder.blocks.block_2x2 import Block2x2
from stardust.builder.blocks.start_normal_left import StartNormalLeft
from stardust.cpp import clog, clog10, fdiv
from stardust.map import game_map


@pytest.fixture
def all_walkable(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(game_map, "is_walkable", lambda x, y: True)


def tiles(locations: list) -> list[tuple[int, int]]:
    return [(location.tile.x, location.tile.y) for location in locations]


def test_block_placement_marks_tile_availability() -> None:
    with fake_game() as game:
        availability = [0] * (game.width * game.height)
        prototype = Block12x8(TilePositions.Invalid, TilePositions.Invalid)

        block = prototype.try_create(TilePosition(5, 5), availability)
        assert block is not None
        assert (block.top_left.x, block.top_left.y) == (5, 5)
        # The prototype's (invalid) top left decides the power pylon offset, as in Stardust
        assert (block.power_pylon.x, block.power_pylon.y) == (11, 8)
        assert availability[5 + 5 * game.width] == 4  # inside
        assert availability[4 + 5 * game.width] == 8  # border
        assert availability[16 + 12 * game.width] == 4
        assert availability[17 + 13 * game.width] == 8

        # Overlapping placements are rejected, and blocks can't touch the map edges they don't allow
        assert prototype.try_create(TilePosition(10, 10), availability) is None
        assert prototype.try_create(TilePosition(0, 30), availability) is None
        assert Block2x2(TilePositions.Invalid, TilePositions.Invalid).try_create(TilePosition(0, 30),
                                                                                   availability) is not None


def test_block_locations_follow_reservations(all_walkable: None) -> None:
    with fake_game():
        block = Block12x8(TilePosition(5, 5), TilePosition(11, 8))
        assert tiles(block.small)[0] == (11, 8)
        assert len(block.large) == 6

        # Reserving the first gateway removes the overlapping locations
        assert block.tiles_reserved(TilePosition(5, 5), TilePosition(4, 3))
        assert (5, 5) not in tiles(block.large)
        assert (6, 6) not in tiles(block.medium)
        assert not block.tiles_reserved(TilePosition(40, 40), TilePosition(2, 2))

        # Taking the first medium location to the left opens up the next one
        assert block.tiles_used(TilePosition(8, 8), TilePosition(3, 2))
        assert (5, 8) in tiles(block.medium)


def test_start_block() -> None:
    with fake_game() as game:
        availability = [0] * (game.width * game.height)
        nexus = TilePosition(30, 30)
        block = StartNormalLeft(TilePositions.Invalid, TilePositions.Invalid).try_create(nexus, availability)
        assert block is not None
        assert (block.top_left.x, block.top_left.y) == (22, 29)
        assert len(block.cannons) == 4
        assert StartNormalLeft(TilePositions.Invalid, TilePositions.Invalid).try_create(TilePosition(3, 3),
                                                                                         availability) is None


def test_ieee_float_helpers() -> None:
    assert fdiv(1.0, 0.0) == math.inf
    assert fdiv(-1.0, 0.0) == -math.inf
    assert math.isnan(fdiv(0.0, 0.0))
    assert clog(0.0) == -math.inf
    assert clog10(100.0) == 2.0
    assert fdiv(1.0, clog(1.0)) == math.inf

"""The numpy walkability searches in geo against literal ports of Stardust's loops."""

import math
import random

from bwapi import Position, Positions, UnitTypes, WalkPosition
from stardust.cpp import INT_MAX, cdiv
from stardust.util import geo
from tests.fakes import FakeGame, fake_game

_PI = 3.14159265358979323846

# Position.isValid in the offline bindings checks against the largest map size, so the fake map is that size
_SIZE = 256


def _game_with_blobs(rng: random.Random) -> FakeGame:
    game = FakeGame(_SIZE, _SIZE)
    for _ in range(400):
        cx, cy, r = rng.randrange(_SIZE * 4), rng.randrange(_SIZE * 4), rng.randrange(1, 12)
        for x in range(cx - r, cx + r + 1):
            for y in range(cy - r, cy + r + 1):
                game.unwalkable.add((x, y))
    return game


def _literal_find_closest(game: FakeGame, start: Position, search_radius: int,
                          further_from: Position = Positions.Invalid) -> Position:
    has_further_from = further_from != Positions.Invalid
    further_from_angle = math.atan2(start.y - further_from.y, start.x - further_from.x) if has_further_from else 0.0
    for radius, dx, dy in geo._radius_positions:
        if radius > search_radius:
            return Positions.Invalid
        here = Position(start.x + dx, start.y + dy)
        if here.isValid() and game.isWalkable(WalkPosition(here)):
            continue
        if has_further_from:
            angle = math.atan2(here.y - start.y, here.x - start.x)
            if abs(angle - further_from_angle) > _PI / 4.0:
                continue
        return here
    return Positions.Invalid


def _literal_find_closest_near(game: FakeGame, start: Position, close_to: Position, search_radius: int,
                               further_from: Position = Positions.Invalid) -> Position:
    best_pos = Positions.Invalid
    best_dist = INT_MAX
    has_further_from = further_from != Positions.Invalid
    further_from_angle = math.atan2(start.y - further_from.y, start.x - further_from.x) if has_further_from else 0.0
    for x in range(start.x - search_radius, start.x + search_radius + 1):
        for y in range(start.y - search_radius, start.y + search_radius + 1):
            current = Position(x, y)
            if current.isValid() and game.isWalkable(WalkPosition(current)):
                continue
            dist = current.getApproxDistance(close_to)
            if has_further_from:
                angle = math.atan2(current.y - start.y, current.x - start.x)
                if abs(angle - further_from_angle) > _PI / 2.0:
                    continue
            if dist < best_dist:
                best_pos = current
                best_dist = dist
    return best_pos


def _literal_walkable(game: FakeGame, center: Position) -> bool:
    unit_type = UnitTypes.Protoss_Dragoon
    for x in range(center.x - unit_type.dimensionLeft(), center.x + unit_type.dimensionRight() + 1):
        for y in range(center.y - unit_type.dimensionUp(), center.y + unit_type.dimensionDown() + 1):
            if not game.isWalkable(cdiv(x, 8), cdiv(y, 8)):
                return False
    return True


def _random_position(rng: random.Random) -> Position:
    # Includes positions near and past the map edges
    return Position(rng.randrange(-40, _SIZE * 32 + 40), rng.randrange(-40, _SIZE * 32 + 40))


def test_walkability_searches_match_stardust() -> None:
    rng = random.Random(3)
    with fake_game(_game_with_blobs(rng)) as game:
        geo.initialize()
        for _ in range(300):
            start = _random_position(rng)
            further_from = _random_position(rng) if rng.random() < 0.5 else Positions.Invalid

            radius = rng.choice([8, 32, 64, 128, 256])
            assert (geo.find_closest_unwalkable_position(start, radius, further_from)
                    == _literal_find_closest(game, start, radius, further_from))

            near_radius = rng.choice([4, 16, 32])
            close_to = start + Position(rng.randrange(-64, 65), rng.randrange(-64, 65))
            assert (geo.find_closest_unwalkable_position_near(start, close_to, near_radius, further_from)
                    == _literal_find_closest_near(game, start, close_to, near_radius, further_from))

            assert geo.walkable(UnitTypes.Protoss_Dragoon, start) == _literal_walkable(game, start)

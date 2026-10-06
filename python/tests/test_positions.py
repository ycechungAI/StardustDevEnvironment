import pickle

import pytest

from bwapi import Position, Positions, TilePosition, WalkPosition


def test_conversions_scale_coordinates():
    tile = TilePosition(2, 3)
    assert Position(tile) == Position(64, 96)
    assert WalkPosition(tile) == WalkPosition(8, 12)
    assert TilePosition(Position(100, 70)) == TilePosition(3, 2)


def test_arithmetic_uses_cpp_integer_semantics():
    assert Position(1, 2) + Position(3, 4) == Position(4, 6)
    assert Position(5, 5) - Position(1, 2) == Position(4, 3)
    assert Position(10, 10) * 2 == 2 * Position(10, 10) == Position(20, 20)
    assert Position(9, 9) / 2 == Position(4, 4)
    assert -Position(1, 2) == Position(-1, -2)


def test_distances():
    assert Position(0, 0).getDistance(Position(3, 4)) == 5.0
    assert Position(0, 0).getApproxDistance(Position(3, 4)) == 5
    assert Position(3, 4).getLength() == 5.0


def test_validity_and_truthiness():
    assert Position(10, 10)
    assert not Position(-1, 0)
    assert not Positions.None_
    assert Position(-5, 99999).makeValid() == Position(0, 8191)  # no game: assumes the largest map


def test_immutable_hashable_and_iterable():
    p = Position(1, 2)
    with pytest.raises(AttributeError):
        p.x = 5  # type: ignore[misc]
    assert len({Position(1, 2), Position(1, 2), Position(2, 1)}) == 2
    x, y = p
    assert (x, y) == (1, 2)
    assert pickle.loads(pickle.dumps(p)) == p


def test_str_matches_cpp_and_repr_is_constructor():
    assert str(WalkPosition(3, 4)) == "(3,4)"
    assert repr(TilePosition(3, 4)) == "TilePosition(3, 4)"

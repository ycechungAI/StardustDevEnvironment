"""Minimal stand-ins for BWAPI game objects, for testing bot logic without a running game."""

import bwapi
import numpy as np
from numpy.typing import NDArray


class FakePlayer:
    def __init__(self, name: str) -> None:
        self.name = name

    def getName(self) -> str:
        return self.name

    def __repr__(self) -> str:
        return f"<FakePlayer {self.name}>"


class FakeGame:
    def __init__(self, width: int = 64, height: int = 64) -> None:
        self.width = width
        self.height = height
        self.me = FakePlayer("me")
        self.them = FakePlayer("them")
        self.frame = 0
        # Walk-resolution walkability, as a set of unwalkable (x, y)
        self.unwalkable: set[tuple[int, int]] = set()
        # Tile-resolution creep, as a set of (x, y)
        self.creep: set[tuple[int, int]] = set()

    def mapWidth(self) -> int:
        return self.width

    def mapHeight(self) -> int:
        return self.height

    def self(self) -> FakePlayer:
        return self.me

    def enemy(self) -> FakePlayer:
        return self.them

    def getFrameCount(self) -> int:
        return self.frame

    def isWalkable(self, walk_x: int | bwapi.WalkPosition, walk_y: int | None = None) -> bool:
        if isinstance(walk_x, bwapi.WalkPosition):
            walk_x, walk_y = walk_x.x, walk_x.y
        assert walk_y is not None
        if walk_x < 0 or walk_y < 0 or walk_x >= self.width * 4 or walk_y >= self.height * 4:
            return False
        return (walk_x, walk_y) not in self.unwalkable

    def getWalkabilityGrid(self) -> NDArray[np.bool_]:
        grid = np.ones((self.width * 4, self.height * 4), dtype=np.bool_)
        for x, y in self.unwalkable:
            if 0 <= x < self.width * 4 and 0 <= y < self.height * 4:
                grid[x, y] = False
        return grid

    def hasCreep(self, tile_x: int, tile_y: int) -> bool:
        return (tile_x, tile_y) in self.creep

    def getCreepGrid(self) -> NDArray[np.bool_]:
        grid = np.zeros((self.width, self.height), dtype=np.bool_)
        for x, y in self.creep:
            if 0 <= x < self.width and 0 <= y < self.height:
                grid[x, y] = True
        return grid


class fake_game:
    """Context manager that installs a FakeGame as bwapi.Broodwar."""

    def __init__(self, game: FakeGame | None = None) -> None:
        self.game = game or FakeGame()

    def __enter__(self) -> FakeGame:
        self.previous = bwapi.Broodwar
        bwapi.Broodwar = self.game  # type: ignore[assignment]
        return self.game

    def __exit__(self, *exc: object) -> None:
        bwapi.Broodwar = self.previous

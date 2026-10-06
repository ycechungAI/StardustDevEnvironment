"""Minimal stand-ins for BWAPI game objects, for testing bot logic without a running game."""

import bwapi


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

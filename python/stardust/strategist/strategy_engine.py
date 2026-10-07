"""Port of Strategist/StrategyEngine.h: the base class of the matchup-specific strategy engines.

Stardust's StrategyEngine also declares static helper methods shared by the engines; they are implemented in
StrategyEngines/Common, which here are the modules of strategy_engines/common, called as module functions.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from stardust.strategist.play import MineralReservations, Play, ProductionGoals
    from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay


def get_play[T](plays: list[Play], play_type: type[T]) -> T | None:
    """The first play of the given type (or a subclass of it)."""
    for play in plays:
        if isinstance(play, play_type):
            return play
    return None


def get_main_army_play(plays: list[Play]) -> MainArmyPlay | None:
    """get_play for the (abstract) MainArmyPlay."""
    from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay

    for play in plays:
        if isinstance(play, MainArmyPlay):
            return play
    return None


def before_play_index(plays: list[Play], play_type: type) -> int:
    """The index of the first play of the given type, or the end of the list: inserting there puts a play before it."""
    for index, play in enumerate(plays):
        if isinstance(play, play_type):
            return index
    return len(plays)


class StrategyEngine(ABC):
    @abstractmethod
    def initialize(self, plays: list[Play], transitioning_from_random: bool, opening: str) -> None: ...

    @abstractmethod
    def update_plays(self, plays: list[Play]) -> None: ...

    @abstractmethod
    def update_production(self, plays: list[Play], prioritized_production_goals: ProductionGoals,
                          mineral_reservations: MineralReservations) -> None: ...

    def get_enemy_strategy(self) -> str:
        return "Unknown"

    def get_our_strategy(self) -> str:
        return "Unknown"

    def is_enemy_rushing(self) -> bool:
        return False

    def is_enemy_proxy(self) -> bool:
        return False

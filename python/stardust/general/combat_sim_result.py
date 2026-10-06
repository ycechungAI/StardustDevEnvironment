"""Port of General/CombatSimResult.{h,cpp}: the outcome of simulating a fight with FAP."""

from __future__ import annotations

from typing import TYPE_CHECKING

from stardust import common
from stardust.cpp import cdiv

if TYPE_CHECKING:
    from stardust.map.choke import Choke


class CombatSimResult:
    __slots__ = ("frame", "my_unit_count", "enemy_unit_count", "initial_mine", "initial_enemy", "final_mine",
                 "final_enemy", "enemy_has_undetected_units", "narrow_choke", "distance_factor", "aggression",
                 "closest_reinforcements", "reinforcement_percentage", "unit_log", "decision")

    def __init__(self, my_unit_count: int = 0, enemy_unit_count: int = 0, initial_mine: int = 0,
                 initial_enemy: int = 0, final_mine: int = 0, final_enemy: int = 0,
                 enemy_has_undetected_units: bool = False, narrow_choke: Choke | None = None,
                 unit_log: dict[str, list[int]] | None = None) -> None:
        self.frame = common.current_frame
        self.my_unit_count = my_unit_count
        self.enemy_unit_count = enemy_unit_count
        self.initial_mine = initial_mine
        self.initial_enemy = initial_enemy
        self.final_mine = final_mine
        self.final_enemy = final_enemy
        self.enemy_has_undetected_units = enemy_has_undetected_units
        self.narrow_choke = narrow_choke
        self.distance_factor = -1.0
        self.aggression = -1.0
        self.closest_reinforcements = -1.0
        self.reinforcement_percentage = -1.0
        self.unit_log = unit_log if unit_log is not None else {}

        # Set by the user of the combat sim result, indicates what decision was made after analysis
        self.decision = False

    def my_percent_lost(self) -> float:
        """What percentage of my army value was lost during the sim."""
        if self.initial_mine == 0:
            return 0.0
        return 1.0 - (self.final_mine / self.initial_mine)

    def enemy_percent_lost(self) -> float:
        """What percentage of the enemy's army value was lost during the sim."""
        if self.initial_enemy == 0:
            return 0.0
        return 1.0 - (self.final_enemy / self.initial_enemy)

    def value_gain(self) -> int:
        """How much relative value was gained by my army during the sim (i.e. if positive, the value the enemy lost
        more than mine)."""
        return self.final_mine - self.initial_mine - (self.final_enemy - self.initial_enemy)

    def percent_gain(self) -> float:
        """Similar to value_gain, but dealing in percentages (i.e. if positive, the enemy lost a higher percentage of
        their army than we lost of ours)."""
        return self.enemy_percent_lost() - self.my_percent_lost()

    def my_percentage_of_total(self) -> float:
        """What percentage of the total army value is ours."""
        if self.final_mine == 0 and self.final_enemy == 0:
            return 0.0
        return self.final_mine / (self.final_mine + self.final_enemy)

    def __iadd__(self, other: CombatSimResult) -> CombatSimResult:
        self.initial_mine += other.initial_mine
        self.initial_enemy += other.initial_enemy
        self.final_mine += other.final_mine
        self.final_enemy += other.final_enemy
        return self

    def __itruediv__(self, divisor: int) -> CombatSimResult:
        self.initial_mine = cdiv(self.initial_mine, divisor)
        self.initial_enemy = cdiv(self.initial_enemy, divisor)
        self.final_mine = cdiv(self.final_mine, divisor)
        self.final_enemy = cdiv(self.final_enemy, divisor)
        return self

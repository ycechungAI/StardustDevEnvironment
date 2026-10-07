"""Port of Strategist/Plays/MainArmy/MopUp.{h,cpp}: our main army hunts down the enemy's remaining buildings."""

from __future__ import annotations

from stardust.general import general
from stardust.general.squads.mop_up_squad import MopUpSquad
from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay


class MopUp(MainArmyPlay):
    def __init__(self) -> None:
        super().__init__("MopUp")
        self._squad = MopUpSquad()
        general.add_squad(self._squad)

    def is_defensive(self) -> bool:
        return False

    def get_squad(self) -> MopUpSquad:
        return self._squad

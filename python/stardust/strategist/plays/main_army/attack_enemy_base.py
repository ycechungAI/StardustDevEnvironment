"""Port of Strategist/Plays/MainArmy/AttackEnemyBase.{h,cpp}: our main army attacks an enemy base."""

from __future__ import annotations

from typing import TYPE_CHECKING

from stardust.general import general
from stardust.general.squads.attack_base_squad import AttackBaseSquad
from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay

if TYPE_CHECKING:
    from stardust.map.base import Base


class AttackEnemyBase(MainArmyPlay):
    def __init__(self, base: Base) -> None:
        super().__init__(f"Attack base @ {base.get_tile_position()}")
        self.base = base
        self._squad = AttackBaseSquad(base)
        general.add_squad(self._squad)

    def is_defensive(self) -> bool:
        return False

    def get_squad(self) -> AttackBaseSquad:
        return self._squad

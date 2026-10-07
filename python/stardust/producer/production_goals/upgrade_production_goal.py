"""Port of Producer/ProductionGoals/UpgradeProductionGoal.{h,cpp}: a request to research a tech or an upgrade level."""

from __future__ import annotations

import bwapi
from bwapi import UnitType, UnitTypes
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType


class UpgradeProductionGoal:
    def __init__(self, requester: str, upgrade_type: UpgradeOrTechType, level: int = 1, producer_limit: int = 1,
                 frame: int = 0) -> None:
        self.requester = requester
        self._type = upgrade_type
        self._level = level
        self._producer_limit = producer_limit
        self._frame = frame

    def upgrade_type(self) -> UpgradeOrTechType:
        return self._type

    def prerequisite_for_next_level(self) -> UnitType:
        if self._type.is_tech_type():
            return UnitTypes.None_
        upgrade = self._type.upgrade_type
        return upgrade.whatsRequired(bwapi.Broodwar.self().getUpgradeLevel(upgrade) + 1)

    def get_producer_limit(self) -> int:
        """Maximum cap of how many producers of the item we should create; -1 if we do not want to limit it."""
        return self._producer_limit

    def get_frame(self) -> int:
        """The frame when the upgrade should be started."""
        return self._frame

    def __str__(self) -> str:
        return f"{self._type}@{self._level} ({self.requester})"

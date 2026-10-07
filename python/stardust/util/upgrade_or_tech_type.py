"""Port of Util/UpgradeOrTechType.{h,cpp}: an upgrade or a tech, treated uniformly by production."""

from dataclasses import dataclass, field

import bwapi
from bwapi import TechType, TechTypes, UnitType, UnitTypes, UpgradeType, UpgradeTypes


@dataclass(frozen=True)
class UpgradeOrTechType:
    upgrade_type: UpgradeType = field(default=UpgradeTypes.None_)
    tech_type: TechType = field(default=TechTypes.None_)

    @staticmethod
    def of(value: "UpgradeType | TechType | UpgradeOrTechType") -> "UpgradeOrTechType":
        if isinstance(value, UpgradeOrTechType):
            return value
        if isinstance(value, TechType):
            return UpgradeOrTechType(tech_type=value)
        return UpgradeOrTechType(upgrade_type=value)

    def is_tech_type(self) -> bool:
        return self.tech_type != TechTypes.None_

    def _next_level(self) -> int:
        return bwapi.Broodwar.self().getUpgradeLevel(self.upgrade_type) + 1

    def mineral_price(self) -> int:
        if self.is_tech_type():
            return self.tech_type.mineralPrice()
        return self.upgrade_type.mineralPrice(self._next_level())

    def gas_price(self) -> int:
        if self.is_tech_type():
            return self.tech_type.gasPrice()
        return self.upgrade_type.gasPrice(self._next_level())

    def upgrade_or_research_time(self) -> int:
        if self.is_tech_type():
            return self.tech_type.researchTime()
        return self.upgrade_type.upgradeTime(self._next_level())

    def what_upgrades_or_researches(self) -> UnitType:
        if self.is_tech_type():
            return self.tech_type.whatResearches()
        return self.upgrade_type.whatUpgrades()

    def whats_required(self) -> UnitType:
        if self.is_tech_type():
            return UnitTypes.None_
        return self.upgrade_type.whatsRequired()

    def current_level(self) -> int:
        if self.is_tech_type():
            return 1 if bwapi.Broodwar.self().hasResearched(self.tech_type) else 0
        # Stardust returns whether the upgrade has any level (0 or 1), not the level itself
        return int(bwapi.Broodwar.self().getUpgradeLevel(self.upgrade_type) > 0)

    def max_level(self) -> int:
        if self.is_tech_type():
            return 1
        return self.upgrade_type.maxRepeats()

    def __str__(self) -> str:
        return str(self.tech_type) if self.is_tech_type() else str(self.upgrade_type)

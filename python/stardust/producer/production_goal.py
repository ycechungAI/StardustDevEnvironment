"""Port of Producer/ProductionGoal.{h,cpp}: a unit or upgrade production goal (std::variant in Stardust)."""

from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.producer.production_goals.upgrade_production_goal import UpgradeProductionGoal

type ProductionGoal = UnitProductionGoal | UpgradeProductionGoal

__all__ = ["ProductionGoal", "UnitProductionGoal", "UpgradeProductionGoal"]

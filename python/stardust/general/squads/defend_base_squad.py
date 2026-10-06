"""Port of General/Squads/DefendBaseSquad.{h,cpp}: defends one of our bases, using its workers when we have no
combat units there."""

from __future__ import annotations

from typing import TYPE_CHECKING

from stardust.general.squad import Squad
from stardust.general.squads.worker_defense_squad import WorkerDefenseSquad
from stardust.general.unit_cluster.unit_cluster import Activity

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster, UnitsAndTargets
    from stardust.map.base import Base
    from stardust.units.unit import Unit


class DefendBaseSquad(Squad):
    def __init__(self, base: Base) -> None:
        super().__init__(f"Defend base @ {base.get_tile_position()}")
        self.base = base
        self.enemy_units: set[Unit] = set()
        self._worker_defense_squad = WorkerDefenseSquad(base)
        self.target_position = base.get_position()

    def execute(self) -> None:
        self.enemy_units = {unit for unit in self.enemy_units if unit.exists()}

        super().execute()

        if not self.clusters:
            workers_and_targets = self._worker_defense_squad.select_targets(self.enemy_units)
            empty_units_and_targets: UnitsAndTargets = []
            self._worker_defense_squad.execute(workers_and_targets, empty_units_and_targets)

    def disband(self) -> None:
        self._worker_defense_squad.disband()

    def execute_cluster(self, cluster: UnitCluster) -> None:
        if not self.enemy_units:
            cluster.set_activity(Activity.Moving)
            cluster.move(self.target_position)
            return

        self.update_detection_needs(self.enemy_units)

        # Select targets
        units_and_targets = cluster.select_targets(self.enemy_units, self.target_position)

        # TODO: Run the combat sim and interpret the result

        cluster.set_activity(Activity.Attacking)
        cluster.attack(units_and_targets, self.target_position)

        if cluster.is_vanguard_cluster:
            workers_and_targets = self._worker_defense_squad.select_targets(self.enemy_units)
            self._worker_defense_squad.execute(workers_and_targets, units_and_targets)

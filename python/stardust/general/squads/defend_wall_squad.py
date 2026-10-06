"""Port of General/Squads/DefendWallSquad.{h,cpp}: defends our Forge/Gateway wall at the natural."""

from __future__ import annotations

from typing import TYPE_CHECKING

from stardust import common
from stardust.builder import building_placement
from stardust.general.squad import Squad
from stardust.general.unit_cluster.unit_cluster import Activity
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster
    from stardust.units.unit import Unit


def _combat_unit_seen_recently(unit: Unit) -> bool:
    frame = common.current_frame
    if not unit.type.isBuilding() and unit.last_seen < frame - 48:
        return False
    if not unit_util.is_combat_unit(unit.type) and unit.last_seen_attacking < frame - 120:
        return False
    if not unit.is_transport() and not unit_util.can_attack_ground(unit.type):
        return False
    return True


class DefendWallSquad(Squad):
    def __init__(self) -> None:
        super().__init__("Defend wall")
        self._wall = building_placement.get_forge_gateway_wall()
        if not self._wall.is_valid():
            log.get("ERROR: DefendWallSquad for invalid wall")

        self.target_position = self._wall.gap_center

    def execute_cluster(self, cluster: UnitCluster) -> None:
        wall = self._wall
        my_main = game_map.get_my_main()
        assert my_main is not None

        # Gather all enemy units in our main or natural area or close to the wall center
        enemy_units = units.enemy_in_radius(self.target_position, 240, _combat_unit_seen_recently)
        for area in game_map.get_my_main_areas():
            enemy_units |= units.enemy_in_area(area, _combat_unit_seen_recently)

        natural = game_map.map_specific_override().natural_for_wall_placement(my_main)
        if natural is None:
            natural = game_map.get_my_natural()
        if natural is not None:
            enemy_units |= units.enemy_in_area(natural.get_area(), _combat_unit_seen_recently)

        # Remove enemy units that are outside and not close to the wall
        enemy_units = {unit for unit in enemy_units
                       if not (unit.get_tile_position() in wall.tiles_outside_wall
                               and unit.get_tile_position() not in wall.tiles_outside_but_close_to_wall)}

        # If there are no enemy units, move to the wall center
        if not enemy_units:
            cluster.set_activity(Activity.Moving)

            # If any of our units are outside the wall, move to our main to give them room to get in
            unit_outside_wall = any(unit.get_tile_position() in wall.tiles_outside_wall for unit in cluster.units)
            cluster.move(my_main.get_position() if unit_outside_wall else self.target_position)
            return

        units_and_targets = cluster.select_targets(enemy_units, self.target_position)
        cluster.set_activity(Activity.Attacking)
        cluster.attack(units_and_targets, self.target_position)

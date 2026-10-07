"""Port of General/Squads/MopUpSquad.{h,cpp}: hunts down remaining enemy buildings when the enemy's bases are gone."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, Positions, TilePosition, TilePositions, WalkPosition
from stardust import common, config
from stardust.cpp import INT_MAX, USHRT_MAX
from stardust.general.squad import Squad
from stardust.general.unit_cluster.unit_cluster import Activity
from stardust.instrumentation import cherryvis
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.units import units

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster
    from stardust.map.base import Base

_DEBUG_SQUAD_TARGET = config.INSTRUMENTATION_ENABLED


def _my_main_position() -> Position:
    my_main = game_map.get_my_main()
    assert my_main is not None
    return my_main.get_position()


class MopUpSquad(Squad):
    def __init__(self) -> None:
        super().__init__("Mop Up")
        self.target_position = _my_main_position()

    def _debug(self, cluster: UnitCluster, message: str) -> None:
        if _DEBUG_SQUAD_TARGET:
            cherryvis.log(f"MopUp cluster {WalkPosition(cluster.center)}: {message}")

    def execute_cluster(self, cluster: UnitCluster) -> None:
        # If there are enemy units near the cluster, attack them
        # TODO: Refactor so we can use the same code as in AttackBaseSquad (combat sim, etc.)
        enemy_units = units.enemy_in_radius(cluster.vanguard.last_position, 750)
        if enemy_units:
            self.update_detection_needs(enemy_units)

            units_and_targets = cluster.select_targets(enemy_units, self.target_position)

            # If any of our units has a target, attack
            if any(target is not None for _, target in units_and_targets):
                if _DEBUG_SQUAD_TARGET:
                    self._debug(cluster, "attacking enemy units near cluster")
                    cherryvis.log(f"First unit: {next(iter(enemy_units))}")

                cluster.set_activity(Activity.Attacking)
                cluster.attack(units_and_targets, self.target_position)
                return

        # Search for the closest known enemy building to the cluster
        closest_dist = INT_MAX
        closest_position = Positions.Invalid
        for enemy_unit in units.all_enemy():
            if not enemy_unit.type.isBuilding():
                continue
            if not enemy_unit.last_position_valid or not enemy_unit.last_position.isValid():
                continue

            if enemy_unit.is_flying:
                dist = enemy_unit.last_position.getApproxDistance(cluster.vanguard.last_position)
            else:
                dist = path_finding.get_ground_distance(cluster.vanguard.last_position, enemy_unit.last_position)
            if dist != -1 and dist < closest_dist:
                closest_dist = dist
                closest_position = enemy_unit.last_position

        # If we found one, move towards it
        if closest_position.isValid():
            self._debug(cluster, f"attacking known building @ {WalkPosition(closest_position)}")

            # Move to the target, will switch to attack when the vanguard is close enough
            cluster.set_activity(Activity.Moving)

            base = game_map.base_near(closest_position)
            self.target_position = base.get_position() if base is not None else closest_position
            cluster.move(self.target_position)
            return

        # If we don't know the enemy's starting main, try to find it first
        if game_map.get_enemy_starting_main() is None:
            unscouted_starting_locations = game_map.unscouted_starting_locations()
            if not unscouted_starting_locations:
                self._debug(cluster, "don't know enemy starting base but have no unscouted starting locations")
            else:
                # (std::set of pointers in Stardust: use the starting location order)
                first_base = next(base for base in game_map.all_starting_locations()
                                  if base in unscouted_starting_locations)

                self._debug(cluster, f"moving to next unscouted starting location @ "
                                     f"{WalkPosition(first_base.get_position())}")

                cluster.set_activity(Activity.Moving)
                self.target_position = first_base.get_position()
                cluster.move(self.target_position)
                return

        # We don't know of any enemy buildings, so try to find one
        # TODO: Somehow handle terran floated buildings
        best_base_scouted_at = common.current_frame
        best_base: Base | None = None
        for base in game_map.get_untaken_expansions(bwapi.Broodwar.enemy()):
            # If the base has been scouted in the past few minutes, skip it
            if base.last_scouted > common.current_frame - 5000:
                continue

            if base.last_scouted < best_base_scouted_at:
                best_base_scouted_at = base.last_scouted
                best_base = base

        if best_base is not None:
            self._debug(cluster, f"moving to next base @ {WalkPosition(best_base.get_position())}")

            cluster.set_activity(Activity.Moving)
            self.target_position = best_base.get_position()
            cluster.move(self.target_position)
            return

        # Move towards the accessible tile that we have seen longest ago
        grid = path_finding.get_navigation_grid(_my_main_position(), True)
        if grid is not None:
            game = bwapi.Broodwar
            best_tile = TilePositions.Invalid
            best_frame = INT_MAX
            for y in range(game.mapHeight()):
                for x in range(game.mapWidth()):
                    # Consider frame seen in "buckets" of 1000
                    frame = game_map.last_seen(x, y) // 1000
                    if frame >= best_frame:
                        continue

                    tile = TilePosition(x, y)
                    if grid.cost_at(tile) == USHRT_MAX:
                        continue

                    # (Stardust also computes the ground distance here, but only uses it to break ties that can't
                    # happen, since the frame must be strictly lower.)
                    best_tile = tile
                    best_frame = frame

            if best_tile.isValid():
                self._debug(cluster, f"moving to best tile @ {WalkPosition(best_tile)}; frame seen: {best_frame}")

                cluster.set_activity(Activity.Moving)
                self.target_position = Position(best_tile) + Position(16, 16)
                cluster.move(self.target_position)
                return

        self._debug(cluster, "Nothing to do!")
        self.target_position = _my_main_position()

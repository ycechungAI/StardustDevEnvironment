"""Port of Strategist/Plays/Scouting/EarlyGameWorkerScout.{h,cpp}: finds the enemy main with one or two workers, then
scouts it, prioritizing the tiles around the depot, the natural and finally the rest of the main area.

Stardust regenerates the prioritized scout tiles every frame by scanning the whole map; the tile sets only depend on
static map data, so they are computed once per base here and the per-frame selection is vectorized with numpy, keeping
Stardust's scan order and tie-breaking.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

import bwapi
import bwem
import numpy as np
from bwapi import Position, Races, TilePosition, TilePositions, UnitType, UnitTypes, WalkPosition
from stardust import common, config
from stardust.builder import builder
from stardust.cpp import INT_MAX, USHRT_MAX, fdiv, to_int
from stardust.instrumentation import cherryvis
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.players import players
from stardust.strategist import opponent_economic_model
from stardust.strategist.play import Play, ProductionGoals, UnitCallback
from stardust.units import units
from stardust.units.my_worker import MyWorker
from stardust.util import boids, geo, unit_util
from stardust.workers import workers

if TYPE_CHECKING:
    from numpy.typing import NDArray
    from stardust.map.base import Base
    from stardust.map.choke import Choke

_OUTPUT_SCOUTTILE_HEATMAP = False

_GOAL_WEIGHT = 32
_THREAT_WEIGHT = 128.0

_MAIN_PRIORITY = 120


@dataclass
class ScoutLookingForEnemyBase:
    unit: MyWorker | None = None
    reserved: bool = False
    target_base: Base | None = None
    closest_distance_to_target_base: int = INT_MAX
    last_distance_to_target_base: int = INT_MAX
    last_forward_motion_frame: int = 0


class _TileGroup:
    """A std::set<TilePosition> of scout tiles: sorted by (x, y), with the tile indices and centers as arrays."""

    def __init__(self, tiles: set[TilePosition]) -> None:
        self.tiles = sorted(tiles, key=lambda tile: (tile.x, tile.y))
        map_width = bwapi.Broodwar.mapWidth()
        self.indices = np.array([tile.x + tile.y * map_width for tile in self.tiles], dtype=np.int64)
        self.center_x = np.array([tile.x * 32 + 16 for tile in self.tiles], dtype=np.int64)
        self.center_y = np.array([tile.y * 32 + 16 for tile in self.tiles], dtype=np.int64)

    def union(self, other: _TileGroup) -> _TileGroup:
        return _TileGroup(set(self.tiles) | set(other.tiles))


_approximate_distances = geo.approximate_distances


def _edge_to_point_distances(unit_type: UnitType, center: Position, px: NDArray[np.int64],
                             py: NDArray[np.int64]) -> NDArray[np.int64]:
    """geo.edge_to_point_distance from a unit to arrays of points."""
    left = center.x - unit_type.dimensionLeft()
    top = center.y - unit_type.dimensionUp()
    right = center.x + unit_type.dimensionRight()
    bottom = center.y + unit_type.dimensionDown()
    x_dist = np.maximum(np.maximum(left - px, px - right - 1), 0)
    y_dist = np.maximum(np.maximum(top - py, py - bottom - 1), 0)
    return _approximate_distances(x_dist, y_dist)


def _get_scout_after_building(building_type: UnitType, building_count: int,
                              existing_scout: MyWorker | None = None) -> MyWorker | None:
    # If a building of the required type already exists, this play is being added late
    # This happens vs. random when the play is re-initialized once the enemy race is known
    # In this case grab our furthest worker from the main base, as it is likely to have already been our scout
    if units.count_all(building_type) >= building_count:
        my_main = game_map.get_my_main()
        assert my_main is not None
        best_worker: MyWorker | None = None
        best_dist = 0
        for unit in units.all_mine_completed_of_type(UnitTypes.Protoss_Probe):
            assert isinstance(unit, MyWorker)
            if existing_scout is not None and existing_scout.id == unit.id:
                continue

            dist = path_finding.get_ground_distance(unit.last_position, my_main.get_position(),
                                                    UnitTypes.Protoss_Probe, PathFindingOptions.UseNearestBWEMArea)
            if dist > best_dist:
                best_dist = dist
                best_worker = unit

        return best_worker

    for pending_building in builder.all_pending_buildings():
        if pending_building.builder is not None and pending_building.type == building_type:
            return pending_building.builder

    return None


def _update_target_base(scout: ScoutLookingForEnemyBase, other_scout: ScoutLookingForEnemyBase) -> None:
    import stardust.strategist.strategist as strategist

    assert scout.unit is not None

    # Reserve the scout if it hasn't already been done
    if not scout.reserved:
        # Wait for the worker to be available for reassignment
        if not workers.is_available_for_reassignment(scout.unit, False, False):
            return

        # Reserve the worker
        workers.reserve_worker(scout.unit)
        scout.reserved = True

    # Jump out if this scout is already assigned to a valid base
    if scout.target_base is not None and scout.target_base is game_map.get_enemy_starting_main():
        return
    if scout.target_base is not None and scout.target_base.owner is None and scout.target_base.last_scouted == -1:
        return

    # Reset before picking a new base
    scout.target_base = None
    scout.closest_distance_to_target_base = INT_MAX
    scout.last_distance_to_target_base = INT_MAX
    scout.last_forward_motion_frame = common.current_frame

    # Pick the closest remaining base to the worker that isn't assigned to another scout
    # TODO: On four player maps with buildable centers, it may be a good idea to cross the center to uncover proxies
    best_base: Base | None = None
    best_travel_time = INT_MAX
    unscouted = game_map.unscouted_starting_locations()
    for base in game_map.all_starting_locations():
        if base not in unscouted:
            continue
        if other_scout.target_base is base:
            continue

        travel_time = path_finding.expected_travel_time(scout.unit.last_position, base.get_position(),
                                                        scout.unit.type, PathFindingOptions.UseNearestBWEMArea,
                                                        1.1, -1)
        if travel_time != -1 and travel_time < best_travel_time:
            best_travel_time = travel_time
            best_base = base

    # Move towards the base
    if best_base is not None:
        if config.DEBUG_UNIT_ORDERS:
            cherryvis.log(f"moveTo: Scout base @ {WalkPosition(best_base.get_position())}", scout.unit.id)
        scout.unit.move_to(best_base.get_position())
        scout.target_base = best_base
        if best_base is game_map.get_enemy_starting_main():
            strategist.set_worker_scout_status(strategist.WorkerScoutStatus.MovingToEnemyBase)
        else:
            strategist.set_worker_scout_status(strategist.WorkerScoutStatus.LookingForEnemyBase)


def _is_scout_blocked(scout: ScoutLookingForEnemyBase) -> bool:
    if scout.unit is None or not scout.reserved or scout.target_base is None:
        return False

    # Not blocked if we got into the target base once
    # TODO: May want to detect blocking after we've been out to check the natural at some point, but for now we just
    # handle initial blocking
    if scout.target_base.last_scouted != -1:
        return False

    # Get cost from here to the target base
    grid = path_finding.get_navigation_grid(scout.target_base.get_position())
    if grid is None:
        return False
    node = grid.node(scout.unit.last_position)
    cost = node.cost
    if cost == USHRT_MAX:
        # This means either that our scout is in an unexpected position or the enemy has done a wall-in
        # Check for the latter by checking if there is a path from the target base to our main
        my_main = game_map.get_my_main()
        assert my_main is not None
        main_grid = path_finding.get_navigation_grid(my_main.get_position())
        if main_grid is None:
            return False

        target_node = main_grid.node(scout.target_base.get_tile_position())
        if target_node.cost == USHRT_MAX:
            game_map.set_enemy_starting_main(scout.target_base)
            return True

        return False

    # Decreasing distance is fine
    # Increasing distance is fine if it jumps quite a bit - this generally means we've scouted a building that changes
    # the path
    if cost < scout.closest_distance_to_target_base or cost > (scout.last_distance_to_target_base + 100):
        scout.closest_distance_to_target_base = cost
        scout.last_distance_to_target_base = cost
        scout.last_forward_motion_frame = common.current_frame
        return False

    scout.last_distance_to_target_base = cost

    # Non-decreasing distance is fine if we are still far away from the enemy base
    if cost > 3000:
        return False

    # Consider us to be blocked if we haven't made forward progress in five seconds
    if (common.current_frame - scout.last_forward_motion_frame) > 120:
        game_map.set_enemy_starting_main(scout.target_base)
        return True

    return False


def _tile_valid(tile: TilePosition, neutral_elevation: int) -> bool:
    game = bwapi.Broodwar
    if not tile.isValid():
        return False
    if not game.isBuildable(tile):
        return False
    if game.getGroundHeight(tile) > neutral_elevation:
        return False

    return True


def _area_tiles(base: Base) -> set[TilePosition]:
    """All valid tiles in the base area at or below the base elevation."""
    bwem_map = bwem.Instance()
    area = base.get_area()
    base_elevation = bwapi.Broodwar.getGroundHeight(base.get_tile_position())
    result: set[TilePosition] = set()

    # Tiles in the area are within its bounding box
    top_left = area.TopLeft()
    bottom_right = area.BottomRight()
    for x in range(top_left.x, bottom_right.x + 1):
        for y in range(top_left.y, bottom_right.y + 1):
            here = TilePosition(x, y)
            if not _tile_valid(here, base_elevation):
                continue
            if bwem_map.GetArea(here) != area:
                continue

            result.add(here)
    return result


def _natural_tiles(natural: Base) -> set[TilePosition]:
    """Valid tiles close to the natural."""
    natural_elevation = bwapi.Broodwar.getGroundHeight(natural.get_tile_position())
    result: set[TilePosition] = set()
    for x in range(-3, 7):
        for y in range(-3, 6):
            here = natural.get_tile_position() + TilePosition(x, y)

            if (Position(here) + Position(16, 16)).getApproxDistance(natural.get_position()) > 160:
                continue

            if not _tile_valid(here, natural_elevation):
                continue

            result.add(here)
    return result


def _main_tiles(base: Base) -> set[TilePosition]:
    """Valid tiles close to the depot, except for the mineral line."""
    base_elevation = bwapi.Broodwar.getGroundHeight(base.get_tile_position())
    result: set[TilePosition] = set()
    for x in range(-6, 10):
        for y in range(-6, 9):
            here = base.get_tile_position() + TilePosition(x, y)

            if (Position(here) + Position(16, 16)).getApproxDistance(base.get_position()) > 250:
                continue

            if not _tile_valid(here, base_elevation):
                continue

            if base.is_in_mineral_line(here):
                continue

            result.add(here)
    return result


def _get_tile_to_monitor_choke_from() -> TilePosition:
    enemy_main_choke = game_map.get_enemy_main_choke()
    if enemy_main_choke is None:
        return TilePositions.Invalid

    # We measure potential tiles against the natural choke by default, or our main if a natural choke doesn't exist
    my_main = game_map.get_my_main()
    assert my_main is not None
    reference_position = my_main.get_position()
    choke_path, _ = path_finding.get_choke_point_path(reference_position, enemy_main_choke.center,
                                                      UnitTypes.Protoss_Dragoon,
                                                      PathFindingOptions.UseNearestBWEMArea)
    if choke_path:
        index = len(choke_path) - 1
        natural_choke = game_map.choke(choke_path[index])

        # Depending on the choke geography we might get the choke itself as the last part of the path
        # If so, advance to the next one
        if natural_choke is enemy_main_choke:
            index -= 1
            if index >= 0:
                natural_choke = game_map.choke(choke_path[index])

        if natural_choke is not None:
            reference_position = natural_choke.center

    # Use our natural's height as the reference height
    game = bwapi.Broodwar
    reference_height = -1
    enemy_starting_natural = game_map.get_enemy_starting_natural()
    if enemy_starting_natural is not None:
        reference_height = game.getGroundHeight(enemy_starting_natural.get_tile_position())

    probe_sight_range = UnitTypes.Protoss_Probe.sightRange()
    tile_sight_range = probe_sight_range // 32
    best_dist = INT_MAX
    best_tile = TilePositions.Invalid
    for y in range(-tile_sight_range + 1, tile_sight_range):
        for x in range(-tile_sight_range + 1, tile_sight_range):
            here = TilePosition(enemy_main_choke.center) + TilePosition(x, y)
            if not here.isValid():
                continue
            if not game_map.is_walkable_tile(here):
                continue

            if reference_height != -1 and game.getGroundHeight(here) != reference_height:
                continue

            here_center = Position(here) + Position(16, 16)
            choke_dist = enemy_main_choke.center.getApproxDistance(here_center)
            if choke_dist > probe_sight_range:
                continue

            dist = reference_position.getApproxDistance(here_center)
            if dist < best_dist:
                best_dist = dist
                best_tile = here

    return best_tile


def _highest_priority_tile(scout_tiles: dict[int, _TileGroup], last_seen: NDArray[np.int64], unit_type: UnitType,
                           position: Position) -> TilePosition:
    """The tile to scout next: Stardust scans the tiles in priority order, then (x, y) order, and takes the first tile
    with the lowest desired frame (last seen + priority), breaking ties by the lowest distance from the scout."""
    if not scout_tiles:
        return TilePositions.Invalid

    desired_parts = []
    groups = []
    for priority in sorted(scout_tiles):
        group = scout_tiles[priority]
        groups.append(group)
        desired_parts.append(last_seen[group.indices] + priority)
    desired = np.concatenate(desired_parts)
    center_x = np.concatenate([group.center_x for group in groups])
    center_y = np.concatenate([group.center_y for group in groups])

    candidates = np.flatnonzero(desired == desired.min())
    dists = _edge_to_point_distances(unit_type, position, center_x[candidates], center_y[candidates])
    best = int(candidates[int(np.argmin(dists))])

    for group in groups:
        if best < len(group.tiles):
            return group.tiles[best]
        best -= len(group.tiles)
    return TilePositions.Invalid


class EarlyGameWorkerScout(Play):
    def __init__(self) -> None:
        super().__init__("EarlyGameWorkerScout")
        self.scout = ScoutLookingForEnemyBase()
        self.second_scout = ScoutLookingForEnemyBase()

        # std::map<int, std::set<TilePosition>>: iterated in ascending priority
        self._scout_tiles: dict[int, _TileGroup] = {}
        self._scout_areas: set[bwem.Area | None] = set()
        self._fixed_position = TilePositions.Invalid
        self._hiding_until = 0

        # Caches of the static parts of the scout tile and hiding tile computations
        self._tile_groups: dict[tuple[str, Base], _TileGroup] = {}
        self._hide_candidates: tuple[Base, Choke | None, NDArray[np.int64], NDArray[np.int64],
                                     list[TilePosition]] | None = None
        self._last_scout_tiles_cvis: list[int] = []

    def update(self) -> None:
        enemy_starting_main = game_map.get_enemy_starting_main()
        if enemy_starting_main is not None and enemy_starting_main.last_scouted != -1:
            self._scout_enemy_base()
        else:
            self._find_enemy_base()

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        pass

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        import stardust.strategist.strategist as strategist

        def release_scout(s: ScoutLookingForEnemyBase) -> None:
            if s.unit is not None and s.unit.exists():
                cherryvis.log("Releasing from non-mining duties (scout disband)", s.unit.id)
                workers.release_worker(s.unit)

        release_scout(self.scout)
        release_scout(self.second_scout)

        if strategist.get_worker_scout_status() in (strategist.WorkerScoutStatus.EnemyBaseScouted,
                                                    strategist.WorkerScoutStatus.MonitoringEnemyChoke):
            strategist.set_worker_scout_status(strategist.WorkerScoutStatus.ScoutingCompleted)

    def hide_until(self, frame: int) -> None:
        """Instructs the scout to hide in a corner of the enemy main until the given frame."""
        if config.CHERRYVIS_ENABLED and frame != self._hiding_until:
            cherryvis.log(f"Hiding worker scout until frame {frame}")

        self._hiding_until = frame

    def monitor_enemy_choke(self) -> None:
        """Instructs the scout to take a position where it can keep an eye on units leaving the enemy main."""
        self._fixed_position = _get_tile_to_monitor_choke_from()

    def _find_enemy_base(self) -> None:
        import stardust.strategist.strategist as strategist

        scout = self.scout
        second_scout = self.second_scout

        # Handle initial state where we haven't reserved the worker scout yet
        if scout.unit is None and not self._select_scout():
            return
        assert scout.unit is not None

        if second_scout.unit is None:
            self._select_second_scout()

        # Mark the play completed if one of the scouts die
        if not scout.unit.exists() or (second_scout.unit is not None and not second_scout.unit.exists()):
            strategist.set_worker_scout_status(strategist.WorkerScoutStatus.ScoutingFailed)
            self.status.complete = True
            return

        # Update the target base
        _update_target_base(scout, second_scout)
        if second_scout.unit is not None:
            _update_target_base(second_scout, scout)

        # Handle case where no base can be scouted
        # This indicates an island map
        if (scout.reserved and scout.target_base is None
                and (not second_scout.reserved or second_scout.target_base is None)):
            strategist.set_worker_scout_status(strategist.WorkerScoutStatus.ScoutingFailed)

            self.status.complete = True
            return

        # Handle case where one scout has nothing left to do
        if scout.reserved and scout.target_base is None:
            # Second scout takes over for the first
            cherryvis.log("Releasing from non-mining duties (other scout takes over)", scout.unit.id)
            workers.release_worker(scout.unit)

            scout.unit = second_scout.unit
            scout.target_base = second_scout.target_base
            scout.closest_distance_to_target_base = second_scout.closest_distance_to_target_base
            scout.last_distance_to_target_base = second_scout.last_distance_to_target_base
            scout.last_forward_motion_frame = second_scout.last_forward_motion_frame

            second_scout.unit = None
            second_scout.reserved = False
            second_scout.target_base = None
        if second_scout.reserved and second_scout.target_base is None:
            # Second scout is no longer needed
            assert second_scout.unit is not None
            cherryvis.log("Releasing from non-mining duties (other scout takes over)", second_scout.unit.id)
            workers.release_worker(second_scout.unit)

            second_scout.unit = None
            second_scout.reserved = False
            second_scout.target_base = None

        # Detect if the enemy is blocking our scout from getting into the target base
        if _is_scout_blocked(scout) or _is_scout_blocked(second_scout):
            strategist.set_worker_scout_status(strategist.WorkerScoutStatus.ScoutingBlocked)

            self.status.complete = True
            return

    def _scout_enemy_base(self) -> None:
        import stardust.strategist.strategist as strategist

        scout = self.scout
        second_scout = self.second_scout
        enemy_starting_main = game_map.get_enemy_starting_main()
        assert enemy_starting_main is not None

        # If we still have two scouts, release one
        if second_scout.unit is not None:
            if not second_scout.reserved or not second_scout.unit.exists():
                second_scout.unit = None
            else:
                # Swap if the second scout found the base
                if (scout.unit is None or not scout.unit.exists()
                        or second_scout.unit.get_distance(enemy_starting_main.get_position())
                        < scout.unit.get_distance(enemy_starting_main.get_position())):
                    if scout.unit is not None and scout.unit.exists():
                        cherryvis.log("Releasing from non-mining duties (other scout takes over)", scout.unit.id)
                        workers.release_worker(scout.unit)

                    scout.unit = second_scout.unit
                    scout.closest_distance_to_target_base = second_scout.closest_distance_to_target_base
                    scout.last_distance_to_target_base = second_scout.last_distance_to_target_base
                    scout.last_forward_motion_frame = second_scout.last_forward_motion_frame

                    second_scout.unit = None
                    second_scout.reserved = False
                    second_scout.target_base = None
                else:
                    # Second scout is no longer needed
                    cherryvis.log("Releasing from non-mining duties (other scout takes over)", second_scout.unit.id)
                    workers.release_worker(second_scout.unit)

                    second_scout.unit = None
                    second_scout.reserved = False
                    second_scout.target_base = None

        # Mark the play completed if the scout dies
        scout_unit = scout.unit
        if scout_unit is None or not scout_unit.exists():
            if strategist.get_worker_scout_status() in (strategist.WorkerScoutStatus.EnemyBaseScouted,
                                                        strategist.WorkerScoutStatus.MonitoringEnemyChoke):
                strategist.set_worker_scout_status(strategist.WorkerScoutStatus.ScoutingCompleted)
            else:
                strategist.set_worker_scout_status(strategist.WorkerScoutStatus.ScoutingFailed)

            self.status.complete = True
            return

        scout.target_base = enemy_starting_main
        target_base = enemy_starting_main

        # Determine when the scout has seen most of the highest-priority tiles
        if (strategist.get_worker_scout_status() not in (strategist.WorkerScoutStatus.EnemyBaseScouted,
                                                         strategist.WorkerScoutStatus.MonitoringEnemyChoke)
                and self._scout_tiles):
            main_tiles = self._scout_tiles[min(self._scout_tiles)]
            last_seen = game_map.last_seen_grid()
            seen = int(np.count_nonzero(last_seen[main_tiles.indices] > 0))

            if fdiv(seen, len(main_tiles.tiles)) > 0.8:
                strategist.set_worker_scout_status(strategist.WorkerScoutStatus.EnemyBaseScouted)

        # Determine the next tile we want to scout or move to
        frame = common.current_frame
        if self._hiding_until > frame:
            tile = self._get_tile_to_hide_on()
        elif self._fixed_position.isValid():
            tile = self._fixed_position
            if scout_unit.get_distance(Position(tile) + Position(16, 16)) < 64:
                strategist.set_worker_scout_status(strategist.WorkerScoutStatus.MonitoringEnemyChoke)
            else:
                strategist.set_worker_scout_status(strategist.WorkerScoutStatus.EnemyBaseScouted)
        else:
            tile = self._get_highest_priority_scout_tile()
        if not tile.isValid():
            tile = TilePosition(target_base.get_position())

        # Compute threat avoidance boid
        threat_x = 0
        threat_y = 0
        has_threat = False
        has_ranged_threat = False
        for unit in units.all_enemy():
            if not unit.last_position_valid:
                continue
            if not unit_util.is_combat_unit(unit.type) and unit.last_seen_attacking < (frame - 120):
                continue
            if not unit_util.can_attack_ground(unit.type):
                continue
            if not unit.type.isBuilding() and unit.last_seen < (frame - 120):
                continue
            if not unit.completed:
                continue

            detection_limit = max(128, unit.ground_range() + 32)
            dist = scout_unit.get_distance(unit)
            if dist >= detection_limit:
                continue

            has_threat = True

            # If the enemy has a ranged unit inside our detection limit, skip threat avaoidance entirely
            # Rationale is that we can't get away anyway, so let's just get the scouting done that we can before dying
            if unit_util.is_ranged_unit(unit.type):
                has_ranged_threat = True
                break

            # Minimum force at detection limit, maximum force at detection limit - 32 (and closer)
            dist_factor = 1.0 - fdiv(max(0, dist - 32), detection_limit - 32)
            vector = Position(scout_unit.last_position.x - unit.last_position.x,
                              scout_unit.last_position.y - unit.last_position.y)
            scaled = geo.scale_vector(vector, to_int(dist_factor * _THREAT_WEIGHT))

            threat_x += scaled.x
            threat_y += scaled.y

        # If we have scouted the base at least once and the enemy has a ranged unit, complete the play
        if has_ranged_threat and strategist.get_worker_scout_status() in (
                strategist.WorkerScoutStatus.EnemyBaseScouted, strategist.WorkerScoutStatus.MonitoringEnemyChoke):
            self.status.complete = True
            return

        # Get the next waypoint
        bwem_map = bwem.Instance()

        # If we are outside the scout areas, use the navigation grid
        if bwem_map.GetNearestArea(WalkPosition(scout_unit.last_position)) not in self._scout_areas:
            # Move directly if there is no enemy threat or a ranged threat
            if not has_threat or has_ranged_threat:
                if config.DEBUG_UNIT_ORDERS:
                    cherryvis.log(f"Scout: out of scout areas, move directly to scout tile {WalkPosition(tile)}",
                                  scout_unit.id)
                scout_unit.move_to(Position(tile) + Position(16, 16))
                return

            navigation_grid = path_finding.get_navigation_grid(target_base.get_position())
            node = navigation_grid.node(scout_unit.get_tile_position()) if navigation_grid is not None else None
            node = node.next_node if node is not None else None
            node = node.next_node if node is not None else None
            if node is None:
                if config.DEBUG_UNIT_ORDERS:
                    cherryvis.log(f"Scout: out of scout areas and no valid navigation grid node, move directly to "
                                  f"scout tile {WalkPosition(tile)}", scout_unit.id)
                scout_unit.move_to(Position(tile) + Position(16, 16))
                return

            target_pos = node.center()
        else:
            # Plot a path, avoiding static defenses and the enemy mineral line
            # Also reject tiles outside the scout areas to limit the search space
            threat_tiles = players.grid(bwapi.Broodwar.enemy()).static_ground_threat_tiles()
            game = bwapi.Broodwar
            scout_areas = self._scout_areas

            def avoid_threat_tiles(t: TilePosition) -> bool:
                if not game_map.is_walkable_tile(t):
                    return False
                if bwem_map.GetNearestArea(t) not in scout_areas:
                    return False
                if target_base.is_in_mineral_line(t):
                    return False

                # (Stardust checks each of the tile's 16 walk tiles for static ground threat)
                return not threat_tiles[t.x, t.y]

            tile_height = game.getGroundHeight(tile)

            def close_enough_to_target(here: TilePosition) -> bool:
                return here.getApproxDistance(tile) < 6 and game.getGroundHeight(here) >= tile_height

            path = path_finding.search(scout_unit.get_tile_position(), tile, avoid_threat_tiles,
                                       close_enough_to_target)

            # Choose the appropriate target position
            if len(path) < 3:
                target_pos = Position(tile) + Position(16, 16)

                if config.DEBUG_UNIT_ORDERS:
                    cherryvis.log(f"Scout: target directly to scout tile {WalkPosition(target_pos)}", scout_unit.id)
            else:
                target_pos = Position(path[2]) + Position(16, 16)

                if config.DEBUG_UNIT_ORDERS:
                    cherryvis.log(f"Scout: target next path waypoint {WalkPosition(target_pos)}", scout_unit.id)

        # If there is a ranged threat or no threats, move directly
        if not has_threat or has_ranged_threat:
            if config.DEBUG_UNIT_ORDERS:
                if has_ranged_threat:
                    cherryvis.log(f"Scout: Ranged threat, moving directly to target {WalkPosition(target_pos)}",
                                  scout_unit.id)
                else:
                    cherryvis.log(f"Scout: No threats, moving directly to target {WalkPosition(target_pos)}",
                                  scout_unit.id)

            scout_unit.move_to(target_pos, True)
            return

        # Compute goal boid
        goal_x = 0
        goal_y = 0
        vector = Position(target_pos.x - scout_unit.last_position.x, target_pos.y - scout_unit.last_position.y)
        scaled = geo.scale_vector(vector, _GOAL_WEIGHT)
        if scaled != bwapi.Positions.Invalid:
            goal_x = scaled.x
            goal_y = scaled.y

        pos = boids.compute_position(scout_unit, [goal_x, threat_x], [goal_y, threat_y], 96,
                                     unit_util.halt_distance(scout_unit.type) + 16, True)

        if config.DEBUG_UNIT_BOIDS:
            last_position = scout_unit.last_position
            cherryvis.log(f"Scouting boids towards {WalkPosition(target_pos)}"
                          f": goal={WalkPosition(last_position + Position(goal_x, goal_y))}"
                          f"; threat={WalkPosition(last_position + Position(threat_x, threat_y))}"
                          f"; total={WalkPosition(last_position + Position(goal_x + threat_x, goal_y + threat_y))}"
                          f"; target={WalkPosition(pos)}", scout_unit.id)

        # Default to target pos if unit can't move in the desired direction
        if pos == bwapi.Positions.Invalid:
            pos = target_pos

        scout_unit.move_to(pos, True)

    def _select_scout(self) -> bool:
        import stardust.strategist.strategies as strategies
        from stardust.strategist.strategy_engines.pv_t.pv_t import PvT

        # Normally we scout after the first pylon
        # Exceptions are that we wait for the first gateway or forge if on a two-player map and enemy is not zerg,
        # and we wait for the nexus if we are doing a 12 nexus vs. Terran
        enemy_race = bwapi.Broodwar.enemy().getRace()
        if game_map.get_enemy_starting_main() is not None and enemy_race in (Races.Terran, Races.Protoss):
            self.scout.unit = _get_scout_after_building(UnitTypes.Protoss_Gateway, 1)
            if self.scout.unit is None:
                self.scout.unit = _get_scout_after_building(UnitTypes.Protoss_Forge, 1)
        elif strategies.is_our_strategy(PvT.OurStrategy.FastExpansion):
            self.scout.unit = _get_scout_after_building(UnitTypes.Protoss_Nexus, 2)
        else:
            self.scout.unit = _get_scout_after_building(UnitTypes.Protoss_Pylon, 1)

        return self.scout.unit is not None

    def _select_second_scout(self) -> None:
        # Only scout against Zerg or Random on a 4p map where we have 2 or more starting locations left to scout
        if game_map.get_enemy_starting_main() is not None:
            return
        if bwapi.Broodwar.enemy().getRace() in (Races.Terran, Races.Protoss):
            return
        if len(bwapi.Broodwar.getStartLocations()) < 4:
            return
        if len(game_map.unscouted_starting_locations()) < 2:
            return

        # Always wait until the first scout is on its way
        if not self.scout.reserved:
            return

        # Scout after the first gateway or forge
        self.second_scout.unit = _get_scout_after_building(UnitTypes.Protoss_Gateway, 1)
        if self.second_scout.unit is None:
            self.second_scout.unit = _get_scout_after_building(UnitTypes.Protoss_Forge, 1)

    def _tile_group(self, kind: str, base: Base) -> _TileGroup:
        key = (kind, base)
        group = self._tile_groups.get(key)
        if group is None:
            if kind == "area":
                group = _TileGroup(_area_tiles(base))
            elif kind == "natural":
                group = _TileGroup(_natural_tiles(base))
            else:
                group = _TileGroup(_main_tiles(base))
            self._tile_groups[key] = group
        return group

    def _generate_tile_scout_priorities(self) -> None:
        """Once we know the enemy main base, this method generates the map of prioritized tiles to scout.
        Top priority are mineral patches, geysers, and the area immediately around the resource depot
        Next priority is the natural location
        Lowest priority is all other tiles in the base area, unless the enemy is zerg (since they can only build on
        creep anyway)"""
        import stardust.strategist.strategist as strategist

        base = game_map.get_enemy_starting_main()
        if base is None:
            return

        scout_tiles = self._scout_tiles
        scout_tiles.clear()

        def add_tiles(priority: int, group: _TileGroup) -> None:
            if not group.tiles:
                return
            existing = scout_tiles.get(priority)
            scout_tiles[priority] = group if existing is None else existing.union(group)

        # Determine the priorities to use
        area_priority = 800
        natural_priority = 960

        enemy_race = bwapi.Broodwar.enemy().getRace()

        # For Zerg we don't need to scout around the base (as they can only build on creep), but want to scout the
        # natural more often
        if enemy_race == Races.Zerg:
            area_priority = 0
            natural_priority = 600

        strategy_engine = strategist.get_strategy_engine()
        assert strategy_engine is not None

        # If we suspect a proxy, scout the natural and outskirts more aggressively early on in case we missed
        # something
        if strategy_engine.is_enemy_proxy() and common.current_frame < 4000:
            natural_priority = 600
            area_priority = 480

        # If the enemy is rushing, give the main area and natural much lower priority
        # We mainly want to keep an eye on when they start transitioning out of the rush
        elif strategy_engine.is_enemy_rushing() and enemy_race != Races.Zerg:
            area_priority = 1200
            natural_priority = 1200

        # If the enemy is protoss, don't scout the natural if our economic model tells us they can't have taken an
        # extra nexus yet
        elif (opponent_economic_model.enabled()
              and opponent_economic_model.earliest_unit_production_frame(UnitTypes.Protoss_Nexus)
              > common.current_frame):
            natural_priority = 0

        # Start by assigning all tiles in the base area the lowest priority
        if area_priority:
            add_tiles(area_priority, self._tile_group("area", base))

        # Now add tiles close to the natural
        natural = game_map.get_enemy_starting_natural()
        if natural is not None and natural_priority:
            add_tiles(natural_priority, self._tile_group("natural", natural))

        # Now add the tiles close to the depot, except for the mineral line
        add_tiles(_MAIN_PRIORITY, self._tile_group("main", base))

        # Finally generate the areas covered by the scout
        self._scout_areas.add(base.get_area())
        if natural is not None:
            path, _ = path_finding.get_choke_point_path(base.get_position(), natural.get_position())
            for choke in path:
                first, second = choke.GetAreas()
                self._scout_areas.add(first)
                self._scout_areas.add(second)

        if _OUTPUT_SCOUTTILE_HEATMAP:
            map_width = bwapi.Broodwar.mapWidth()
            map_height = bwapi.Broodwar.mapHeight()
            scout_tiles_cvis = [0] * (map_width * map_height)
            for priority in sorted(scout_tiles, reverse=True):
                for index in scout_tiles[priority].indices:
                    scout_tiles_cvis[index] = priority
            if self._last_scout_tiles_cvis != scout_tiles_cvis:
                cherryvis.add_heatmap("WorkerScoutTiles", scout_tiles_cvis, map_width, map_height)
            self._last_scout_tiles_cvis = scout_tiles_cvis

    def _get_tile_to_hide_on(self) -> TilePosition:
        enemy_main = game_map.get_enemy_starting_main()
        if enemy_main is None:
            return TilePositions.Invalid

        # (Stardust also collects the tiles the enemy has vision on here, but doesn't use them.)

        # Now get the best tile in the main area
        # The candidate tiles and their scores only depend on static data; walkability is checked each time
        enemy_choke = game_map.get_enemy_main_choke()
        if (self._hide_candidates is None or self._hide_candidates[0] is not enemy_main
                or self._hide_candidates[1] is not enemy_choke):
            game = bwapi.Broodwar
            bwem_map = bwem.Instance()
            base_elevation = game.getGroundHeight(enemy_main.get_tile_position())
            map_width = game.mapWidth()
            candidate_tiles: list[TilePosition] = []
            candidate_indices: list[int] = []
            candidate_dists: list[int] = []

            # Tiles in the main area are within its bounding box; scan it in Stardust's (y, x) order
            top_left = enemy_main.get_area().TopLeft()
            bottom_right = enemy_main.get_area().BottomRight()
            for y in range(top_left.y, bottom_right.y + 1):
                for x in range(top_left.x, bottom_right.x + 1):
                    here = TilePosition(x, y)
                    if game.getGroundHeight(here) > base_elevation:
                        continue
                    if bwem_map.GetArea(here) != enemy_main.get_area():
                        continue

                    here_center = Position(here) + Position(16, 16)
                    dist = enemy_main.get_position().getApproxDistance(here_center)
                    if enemy_choke is not None:
                        dist += enemy_choke.center.getApproxDistance(here_center)

                    candidate_tiles.append(here)
                    candidate_indices.append(x + y * map_width)
                    candidate_dists.append(dist)
            self._hide_candidates = (enemy_main, enemy_choke, np.array(candidate_indices, dtype=np.int64),
                                     np.array(candidate_dists, dtype=np.int64), candidate_tiles)

        _, _, indices, dists, tiles = self._hide_candidates
        if not tiles:
            return TilePositions.Invalid
        walkable = np.frombuffer(game_map.walkability_grid(), dtype=np.uint8)[indices] != 0
        scores = np.where(walkable, dists, 0)

        # The first tile in scan order with the largest distance, if it is positive
        best = int(np.argmax(scores))
        if scores[best] <= 0:
            return TilePositions.Invalid
        return tiles[best]

    def _get_highest_priority_scout_tile(self) -> TilePosition:
        self._generate_tile_scout_priorities()

        scout_unit = self.scout.unit
        assert scout_unit is not None
        return _highest_priority_tile(self._scout_tiles, game_map.last_seen_grid(), scout_unit.type,
                                      scout_unit.last_position)

"""Port of Map/MapSpecificOverrides/Plasma.{h,cpp}: Plasma's chokes are blocked by neutral eggs and mineral walls."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Position, Positions, TilePosition, UnitType, UnitTypes, WalkPosition
from stardust import common, config
from stardust.cpp import INT_MAX
from stardust.instrumentation import cherryvis, log
from stardust.map.map_specific_override import MapSpecificOverride
from stardust.util import geo, unit_util

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster
    from stardust.map.choke import Choke
    from stardust.strategist.strategy_engine import StrategyEngine


def _tiles(*coordinates: tuple[int, int]) -> set[TilePosition]:
    return {TilePosition(x, y) for x, y in coordinates}


def _get_start_position(patch: bwapi.Unit | None, other_patch: bwapi.Unit | None) -> Position:
    from stardust.map import game_map

    assert patch is not None and other_patch is not None
    best_pos = Positions.Invalid
    best_dist = INT_MAX
    radius = UnitTypes.Protoss_Probe.sightRange() // 32
    for y in range(-radius, radius + 1):
        for x in range(-radius, radius + 1):
            tile = patch.getInitialTilePosition() + TilePosition(x, y)
            if not tile.isValid():
                continue
            if not game_map.is_walkable_tile(tile):
                continue

            pos = Position(tile) + Position(16, 16)
            dist = pos.getApproxDistance(other_patch.getInitialPosition())
            if dist < best_dist:
                best_pos = pos
                best_dist = dist

    return best_pos


class Plasma(MapSpecificOverride):
    def __init__(self) -> None:
        # std::map<Choke *, std::set<BWAPI::Unit>>; insertion order stands in for pointer order
        self._choke_to_blocking_eggs: dict[Choke, dict[bwapi.Unit, None]] = {}
        self._accessible_areas: list[bwem.Area] = []

    def has_mineral_walking(self) -> bool:
        return True

    def has_attack_clearable_chokes(self) -> bool:
        return True

    def can_use_bwem_path(self, unit_type: UnitType) -> bool:
        # On Plasma, BWEM doesn't mark the mineral walking chokes as blocked
        # So we can use BWEM pathing for workers but nothing else
        return unit_type.isWorker()

    def allow_diagonal_pathing_through(self, x: int, y: int) -> bool:
        # On Plasma we allow diagonal pathing through all of the narrow ramps
        return ((x == 13 and y == 26) or (x == 14 and y == 27)  # top-left main
                or (x == 25 and y == 119) or (x == 26 and y == 118)  # bottom-left main
                or (x == 81 and y == 75) or (x == 82 and y == 74)  # right main
                or (x == 59 and y == 35) or (x == 60 and y == 36)  # top-right expo
                or (x == 31 and y == 72) or (x == 32 and y == 71)  # left expo
                or (x == 55 and y == 91) or (x == 56 and y == 92))  # bottom-right expo

    def initialize_chokes(self, chokes: dict[bwem.ChokePoint, Choke]) -> None:
        bwem_map = bwem.Instance()
        for choke in chokes.values():
            center_tile = TilePosition(choke.center)
            if center_tile == TilePosition(70, 15):
                choke.center = Position(WalkPosition(289, 66))
                egg_positions = _tiles((70, 16), (71, 16), (72, 16), (73, 16), (74, 16), (70, 17), (71, 17), (72, 17),
                                       (73, 17), (74, 17))
            elif center_tile == TilePosition(11, 58) or center_tile == TilePosition(11, 69):
                choke.center = Position(WalkPosition(44, 258))
                egg_positions = _tiles((9, 62), (9, 63), (9, 64), (9, 65), (9, 66), (10, 62), (10, 63), (10, 64),
                                       (10, 65), (10, 66))
            elif center_tile == TilePosition(36, 28):
                egg_positions = _tiles((34, 30), (35, 30), (36, 30), (34, 29), (35, 29), (36, 29))
            elif center_tile == TilePosition(73, 111) or center_tile == TilePosition(70, 113):
                egg_positions = _tiles((70, 111), (71, 111), (72, 111), (73, 111), (74, 111), (70, 112), (71, 112),
                                       (72, 112), (73, 112), (74, 112))
            elif center_tile == TilePosition(37, 99) or center_tile == TilePosition(36, 99):
                egg_positions = _tiles((36, 98), (36, 99), (36, 100), (37, 98), (37, 99), (37, 100))
            elif center_tile == TilePosition(54, 64) or center_tile == TilePosition(53, 64):
                egg_positions = _tiles((52, 65), (53, 65), (54, 65), (52, 66), (53, 66), (54, 66))
            else:
                continue

            # Determine if the choke is blocked by eggs, and grab the close mineral patches
            closest_mineral_patch: bwapi.Unit | None = None
            second_closest_mineral_patch: bwapi.Unit | None = None
            closest_mineral_patch_dist = INT_MAX
            second_closest_mineral_patch_dist = INT_MAX
            for static_neutral in bwapi.Broodwar.getStaticNeutralUnits():
                if static_neutral.getType() == UnitTypes.Zerg_Egg:
                    initial_tile = static_neutral.getInitialTilePosition()
                    if initial_tile in egg_positions:
                        self._choke_to_blocking_eggs.setdefault(choke, {})[static_neutral] = None
                        egg_positions.discard(initial_tile)

                if (static_neutral.getType() == UnitTypes.Resource_Mineral_Field
                        and static_neutral.getResources() == 32):
                    dist = static_neutral.getDistance(choke.center)
                    if dist <= closest_mineral_patch_dist:
                        second_closest_mineral_patch_dist = closest_mineral_patch_dist
                        closest_mineral_patch_dist = dist
                        second_closest_mineral_patch = closest_mineral_patch
                        closest_mineral_patch = static_neutral
                    elif dist < second_closest_mineral_patch_dist:
                        second_closest_mineral_patch_dist = dist
                        second_closest_mineral_patch = static_neutral
            assert closest_mineral_patch is not None and second_closest_mineral_patch is not None

            choke.requires_mineral_walk = True

            closest_area = bwem_map.GetNearestArea(
                WalkPosition(closest_mineral_patch.getTilePosition()) + WalkPosition(4, 2))
            second_closest_area = bwem_map.GetNearestArea(
                WalkPosition(second_closest_mineral_patch.getTilePosition()) + WalkPosition(4, 2))
            first_area, second_area = choke.choke.GetAreas()
            if closest_area == second_area and second_closest_area == first_area:
                choke.second_area_mineral_patch = closest_mineral_patch
                choke.first_area_mineral_patch = second_closest_mineral_patch
            else:
                # Note: Two of the chokes don't have the mineral patches show up in expected areas because of
                # suboptimal BWEM choke placement, but luckily they both follow this pattern
                choke.first_area_mineral_patch = closest_mineral_patch
                choke.second_area_mineral_patch = second_closest_mineral_patch

            choke.first_area_start_position = _get_start_position(choke.first_area_mineral_patch,
                                                                  choke.second_area_mineral_patch)
            choke.second_area_start_position = _get_start_position(choke.second_area_mineral_patch,
                                                                   choke.first_area_mineral_patch)

        if len(self._choke_to_blocking_eggs) != 6:
            log.get(f"WARNING: Expected to find 6 blocked chokes, but found {len(self._choke_to_blocking_eggs)}")

    def on_unit_destroy(self, unit: bwapi.Unit) -> None:
        if unit.getType() != UnitTypes.Zerg_Egg or unit.getPlayer() != bwapi.Broodwar.neutral():
            return

        for choke, eggs in self._choke_to_blocking_eggs.items():
            eggs.pop(unit, None)

            if not eggs:
                log.get(f"Choke @ {TilePosition(choke.center)} unblocked")
                choke.requires_mineral_walk = False
                del self._choke_to_blocking_eggs[choke]
                return

    def cluster_move(self, cluster: UnitCluster, target_position: Position) -> bool:
        from stardust.map import game_map
        from stardust.map.path_finding import path_finding
        from stardust.players import players

        vanguard = cluster.vanguard
        if vanguard is None:
            return False

        # Check if the cluster needs to move through a blocked choke
        mineral_walk_choke: Choke | None = None

        # First check if the vanguard unit is close to one of the chokes
        for choke in self._choke_to_blocking_eggs:
            if vanguard.get_distance(choke.center) < 200:
                mineral_walk_choke = choke
                break

        # Next look for a blocked choke on the path between the cluster's vanguard unit and the target position
        if mineral_walk_choke is None:
            path, _ = path_finding.get_choke_point_path(vanguard.last_position, target_position,
                                                        UnitTypes.Protoss_Probe)
            for bwem_choke in path:
                path_choke = game_map.choke(bwem_choke)
                assert path_choke is not None
                if path_choke.requires_mineral_walk:
                    mineral_walk_choke = path_choke
                    break
            if mineral_walk_choke is None:
                return False

        eggs = self._choke_to_blocking_eggs.get(mineral_walk_choke)
        if eggs is None:
            return False

        game = bwapi.Broodwar
        grid = players.grid(game.self())

        # Attack with each unit
        for my_unit in cluster.units:
            # If the unit is stuck, unstick it
            if my_unit.unstick():
                continue

            # If the unit is not ready (i.e. is already in the middle of an attack), don't touch it
            if not my_unit.is_ready():
                continue

            # Attack the closest egg
            best_dist = INT_MAX
            best_egg: bwapi.Unit | None = None
            for egg in eggs:
                dist = geo.edge_to_edge_distance(my_unit.type, my_unit.last_position, egg.getType(),
                                                 egg.getInitialPosition())
                if dist < best_dist:
                    best_dist = dist
                    best_egg = egg
            if best_egg is None:
                continue
            egg_position = best_egg.getInitialPosition()

            if not unit_util.is_ranged_unit(my_unit.type):
                if best_egg.isVisible():
                    if config.DEBUG_UNIT_ORDERS:
                        cherryvis.log(f"Attacking closest egg @ {WalkPosition(egg_position)}", my_unit.id)
                    my_unit.attack(best_egg)
                else:
                    if config.DEBUG_UNIT_ORDERS:
                        cherryvis.log(f"Moving to closest egg @ {WalkPosition(egg_position)}", my_unit.id)
                    my_unit.move_to(egg_position, True)
                continue

            # Attack if ready
            weapon_range = my_unit.ground_range()
            if (best_egg.isVisible()
                    and best_dist <= weapon_range
                    and my_unit.cooldown_until < (common.current_frame + game.getRemainingLatencyFrames() + 2)):
                if config.DEBUG_UNIT_ORDERS:
                    cherryvis.log(f"Attacking closest egg @ {WalkPosition(egg_position)}", my_unit.id)
                my_unit.attack(best_egg)
                continue

            # Otherwise move towards the egg
            # For some reason just moving towards the egg position makes normal BW pathing bug out
            # So we look for the closest position between here and the egg that is walkable and not occupied by a
            # friendly unit
            egg_height = game.getGroundHeight(best_egg.getInitialTilePosition())
            dist = 64
            while dist < (best_dist + 16):
                vector = geo.scale_vector(my_unit.last_position - egg_position, dist)
                if vector != Positions.Invalid:
                    pos = egg_position + vector
                    tile = TilePosition(pos)
                    if (game.getGroundHeight(tile) == egg_height and game_map.is_walkable_tile(tile)
                            and grid.collision(pos) <= 0):
                        if config.DEBUG_UNIT_ORDERS:
                            cherryvis.log(f"Moving towards closest egg @ {WalkPosition(egg_position)}; closest "
                                          f"position {WalkPosition(pos)}", my_unit.id)

                        my_unit.move_to(pos, True)
                        break
                dist += 16
            if dist >= (best_dist + 16):
                if config.DEBUG_UNIT_ORDERS:
                    cherryvis.log(f"Moving to closest egg @ {WalkPosition(egg_position)}", my_unit.id)
                my_unit.move_to(egg_position, True)

        return True

    def modify_main_base_building_placement_areas(self, areas: set[bwem.Area]) -> None:
        from stardust.map import game_map
        from stardust.map.path_finding import path_finding

        # On Plasma there is very little room in the small main base platform, so use the entire accessible area
        if not self._accessible_areas:
            my_main = game_map.get_my_main()
            assert my_main is not None
            main_pos = my_main.get_position()
            for area in bwem.Instance().Areas():
                if path_finding.get_ground_distance(main_pos, Position(area.Top())) == -1:
                    continue

                self._accessible_areas.append(area)
        areas.update(self._accessible_areas)

    def create_strategy_engine(self) -> StrategyEngine | None:
        from stardust.strategist.strategy_engines.map_specific.plasma_strategy_engine import PlasmaStrategyEngine

        return PlasmaStrategyEngine()

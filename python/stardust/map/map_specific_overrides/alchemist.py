"""Port of Map/MapSpecificOverrides/Alchemist.{h,cpp}."""

from __future__ import annotations

from collections.abc import Iterable
from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Position, TilePosition, UnitTypes
from stardust.cpp import INT_MAX
from stardust.instrumentation import log
from stardust.map.map_specific_override import MapSpecificOverride

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.map.choke import Choke


class Alchemist(MapSpecificOverride):
    def enemy_starting_main_determined(self) -> None:
        from stardust.map import game_map
        from stardust.map.path_finding import path_finding
        from stardust.map.path_finding.path_finding import PathFindingOptions

        # On Alchemist there are two potential main chokes and naturals depending on which direction we want to go
        # So once we know where the enemy is, we pick them based on this logic:
        # - Our natural is the one further away from the enemy
        # - Our main choke is the one towards the enemy
        # - Enemy natural is the one closest to us
        # - Enemy main choke is the one closest to us
        my_main = game_map.get_my_main()
        enemy_main = game_map.get_enemy_starting_main()
        assert my_main is not None and enemy_main is not None

        # Start by finding the chokes
        choke_path, _ = path_finding.get_choke_point_path(my_main.get_position(), enemy_main.get_position(),
                                                          UnitTypes.Protoss_Dragoon,
                                                          PathFindingOptions.UseNearestBWEMArea)

        def first_ramp_or_choke(path: Iterable[bwem.ChokePoint]) -> Choke:
            path = list(path)
            for bwem_choke in path:
                c = game_map.choke(bwem_choke)
                assert c is not None
                if c.is_ramp:
                    return c

            c = game_map.choke(path[0])
            assert c is not None
            return c

        game_map.set_my_main_choke(first_ramp_or_choke(choke_path))
        game_map.set_enemy_main_choke(first_ramp_or_choke(reversed(choke_path)))

        # Now find the naturals
        def path_goes_through_choke(natural: Base, main: Base, choke: Choke | None) -> bool:
            path, _ = path_finding.get_choke_point_path(main.get_position(), natural.get_position(),
                                                        UnitTypes.Protoss_Dragoon,
                                                        PathFindingOptions.UseNearestBWEMArea)
            return any(game_map.choke(bwem_choke) is choke for bwem_choke in path)

        own_natural_dist = INT_MAX
        own_natural: Base | None = None
        enemy_natural_dist = INT_MAX
        enemy_natural: Base | None = None
        for base in game_map.all_bases():
            if base is my_main:
                continue
            if base is enemy_main:
                continue
            if base.gas == 0:
                continue

            if not path_goes_through_choke(base, my_main, game_map.get_my_main_choke()):
                dist = path_finding.get_ground_distance(my_main.get_position(), base.get_position(),
                                                        UnitTypes.Protoss_Probe, PathFindingOptions.UseNearestBWEMArea)
                if dist == -1 or dist > own_natural_dist:
                    continue

                own_natural_dist = dist
                own_natural = base

            if path_goes_through_choke(base, enemy_main, game_map.get_enemy_main_choke()):
                dist = path_finding.get_ground_distance(enemy_main.get_position(), base.get_position(),
                                                        UnitTypes.Protoss_Probe, PathFindingOptions.UseNearestBWEMArea)
                if dist == -1 or dist > enemy_natural_dist:
                    continue

                enemy_natural_dist = dist
                enemy_natural = base

        if own_natural is not None:
            game_map.set_my_natural(own_natural)
        else:
            log.get("WARNING: Could not compute our natural base")

        if enemy_natural is not None:
            game_map.set_enemy_starting_natural(enemy_natural)
        else:
            log.get("WARNING: Could not compute enemy natural base")

    def has_backdoor_natural(self) -> bool:
        return True

    def modify_main_base_building_placement_areas(self, areas: set[bwem.Area]) -> None:
        from stardust.map import game_map
        from stardust.map.path_finding import path_finding

        # The 3-oclock start position is split into two areas, so we need to handle it specially
        game = bwapi.Broodwar
        if game.self().getStartLocation() != TilePosition(117, 51):
            return

        # Find the area by looking at the first choke on a path
        my_main = game_map.get_my_main()
        assert my_main is not None
        path, _ = path_finding.get_choke_point_path(my_main.get_position(), Position(15, 15))
        if not path:
            return

        base_elevation = game.getGroundHeight(game.self().getStartLocation())

        def handle_area(area: bwem.Area) -> None:
            area_elevation = game.getGroundHeight(TilePosition(area.Top()))
            if area_elevation == base_elevation:
                areas.add(area)

        first, second = path[0].GetAreas()
        handle_area(first)
        handle_area(second)

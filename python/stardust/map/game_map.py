"""Port of Map/Map.{h,cpp} and Map/Map_Walkability.cpp: the `Map` namespace (named game_map to avoid shadowing the
`map` builtin).

Tracks bases and their ownership, chokes, starting locations, tile walkability (including buildings), distances to
unwalkable terrain, collision vectors and when each tile was last seen. Per-tile data is stored in flat arrays
indexed by x + y * map_width.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Position, Races, TilePosition, UnitType, UnitTypes, WalkPosition
from stardust import common, config
from stardust.cpp import INT_MAX
from stardust.instrumentation import cherryvis, log
from stardust.builder import building_placement
from stardust.map import no_go_areas
from stardust.map.base import Base
from stardust.map.choke import Choke
from stardust.map.map_specific_override import MapSpecificOverride
from stardust.map.starting_location import StartingLocation
from stardust.util import geo

if TYPE_CHECKING:
    from stardust.units.unit import Unit

_DIR_N, _DIR_S, _DIR_W, _DIR_E, _DIR_NW, _DIR_NE, _DIR_SW, _DIR_SE = range(8)


@dataclass(eq=False)
class _PlayerBases:
    starting_main: Base | None = None
    starting_natural: Base | None = None
    starting_main_choke: Choke | None = None
    starting_natural_choke: Choke | None = None
    main: Base | None = None
    all_owned: set[Base] = field(default_factory=set)
    probable_expansions: list[Base] = field(default_factory=list)
    island_expansions: list[Base] = field(default_factory=list)


_map_width = 0
_map_height = 0
_map_width_pixels = 0
_map_height_pixels = 0

_map_specific_override = MapSpecificOverride()
_bases: list[Base] = []
_starting_locations: list[StartingLocation] = []
_chokes: dict[bwem.ChokePoint, Choke] = {}
_min_choke_width = 0
_starting_base_areas: dict[Base | None, set[bwem.Area]] = {}
_areas_to_edge_positions: dict[bwem.Area, set[TilePosition]] = {}
_edge_positions_to_area: dict[TilePosition, bwem.Area] = {}

_in_own_mineral_line = bytearray()
_bordering_mineral_patch = bytearray()
_narrow_choke_tiles = bytearray()
_leaf_area_tiles = bytearray()
_island_tiles = bytearray()
_tile_last_seen: list[int] = []
_power: list[int] = []

_player_to_player_bases: dict[bwapi.Player, _PlayerBases] = {}

# Walkability
_tile_terrain_walkability = bytearray()
_tile_walkability = bytearray()
_tile_distance_to_unwalkable_directions: list[int] = []  # 8 entries per tile, indexed by tile index * 8 + direction
_tile_distance_to_unwalkable: list[int] = []
_tile_walkable_width: list[int] = []
_tile_collision_vector_x: list[int] = []
_tile_collision_vector_y: list[int] = []
_tile_walkability_updated = False

# Map hash -> (override module, class name); OpenBW hashes listed alongside the BWAPI ones where known
_MAP_SPECIFIC_OVERRIDES = {
    "83320e505f35c65324e93510ce2eafbaa71c9aa1": ("fortress", "Fortress"),
    "6f5295624a7e3887470f3f2e14727b1411321a67": ("plasma", "Plasma"),
    "8b3e8ed9ce9620a606319ba6a593ed5c894e51df": ("plasma", "Plasma"),
    "8000dc6116e405ab878c14bb0f0cde8efa4d640c": ("alchemist", "Alchemist"),
    "9e5770c62b523042e8af590c8dc267e6c12637fc": ("alchemist", "Alchemist"),
    "63a94b3a878c912f2fa5e31700491a60ac3f29d9": ("outsider", "Outsider"),
    "99324782b01af58f6b25aea13e2d62aa83564de0": ("outsider", "Outsider"),
    "4e24f217d2fe4dbfa6799bc57f74d8dc939d425b": ("destination", "Destination"),
    "e39c1c81740a97a733d227e238bd11df734eaf96": ("destination", "Destination"),
    "0a41f144c6134a2204f3d47d57cf2afcd8430841": ("match_point", "MatchPoint"),
    "7e14d53b944b1365973f2d8768c75358c6b28a8f": ("match_point", "MatchPoint"),
    "442e456721c94fd085ecd10230542960d57928d9": ("arcadia", "Arcadia"),
    "83cc5c3944a80915a68190d7b87714d8c0cf8a2f": ("arcadia", "Arcadia"),
    "bde51d09a2ad733db9e5492354798045db2ced3e": ("colosseum", "Colosseum"),
    "699c33ca1ff8f486d93b288371e57db49a0c403f": ("colosseum", "Colosseum"),
    "97944269ea55365d13c310f46c9337f5e873dc6c": ("crossing_field", "CrossingField"),
    "a8fff0bad1956dba03e234744f2e12f7941a8f8a": ("crossing_field", "CrossingField"),
    "2acb8e8cc2ec9b0911a73f2f29dcce424862dddd": ("gods_garden", "GodsGarden"),
    "0d592ab56bd5c9230444e48177f150e58fce0a91": ("gods_garden", "GodsGarden"),
    "2f69eaa1a73bb743934d55e7ea12186fe340e656": ("judgment_day", "JudgmentDay"),
    "a19d3ed890c2919c81a9aff55732d3f602a3323e": ("judgment_day", "JudgmentDay"),
    "444b805a88f971c6b2c5f8d2a467de3c1fb2f001": ("katrina", "Katrina"),
    "625b16a3a6c861bc6c2f91b709329edb7e8e28aa": ("katrina", "Katrina"),
    "5386ec02cc3ee913acc55181896c287ae9d5b5c6": ("roadkill", "Roadkill"),
    "2f55a45d8b9cb7ef86c2f688aeeb3c738406beb9": ("roadkill", "Roadkill"),
    "dbd844012e678b23ca8ef21b3b62008589a554b5": ("neo_sylphid", "NeoSylphid"),
    "1be2d1d778131323d3a0e7dd2301285c5a9887c9": ("neo_sylphid", "NeoSylphid"),
}


def _bases_of(player: bwapi.Player | None) -> _PlayerBases:
    """playerToPlayerBases[player], creating the entry like C++ std::map::operator[]."""
    if player is None:
        player = bwapi.Broodwar.self()
    bases = _player_to_player_bases.get(player)
    if bases is None:
        bases = _player_to_player_bases[player] = _PlayerBases()
    return bases


def _heatmap(name: str, values: bytearray | list[int]) -> None:
    if config.INSTRUMENTATION_ENABLED:
        cherryvis.add_heatmap(name, list(values), _map_width, _map_height)


# ---------------------------------------------------------------------------------------------------------------------
# Base and choke analysis


def _compute_main_choke(main: Base | None, natural: Base | None) -> Choke | None:
    from stardust.map.path_finding import path_finding

    if main is None or natural is None:
        return None

    # Main choke is defined as the last ramp traversed in the path from the main to the natural, or the first choke if
    # a ramp is not found
    path, _ = path_finding.get_choke_point_path(main.get_position(), natural.get_position(),
                                                UnitTypes.Protoss_Dragoon,
                                                path_finding.PathFindingOptions.UseNearestBWEMArea)
    if not path:
        return None

    for bwem_choke in reversed(path):
        c = choke(bwem_choke)
        if c is not None and c.is_ramp:
            return c
    return choke(path[0])


def _compute_natural_choke(main: Base | None, natural: Base | None, main_choke: Choke | None) -> Choke | None:
    if main is None or natural is None or main_choke is None:
        return None

    # Finds the choke out of the natural area that:
    # - is not the main choke
    # - is not blocked or very narrow
    # - goes to the area closest to the map center
    # Ties are broken by closest choke to our main
    natural_area = natural.get_area()
    map_center = bwem.Instance().Center()

    area_dist_best = INT_MAX
    choke_dist_best = INT_MAX
    best: bwem.ChokePoint | None = None
    for bwem_choke in natural_area.ChokePoints():
        if bwem_choke.Center() == main_choke.choke.Center():
            continue
        if bwem_choke.Blocked() or len(bwem_choke.Geometry()) <= 3:
            continue

        first, second = bwem_choke.GetAreas()
        area = second if first == natural_area else first
        if not area.Top().isValid():
            continue

        area_dist = Position(area.Top()).getApproxDistance(map_center)
        if area_dist > area_dist_best:
            continue

        choke_dist = Position(bwem_choke.Center()).getApproxDistance(main.get_position())
        if area_dist < area_dist_best or choke_dist < choke_dist_best:
            best = bwem_choke
            area_dist_best = area_dist
            choke_dist_best = choke_dist

    return choke(best) if best is not None else None


def _closest_base_distance(base: Base, other_bases: set[Base]) -> int:
    from stardust.map.path_finding import path_finding

    closest_distance = -1
    for other_base in other_bases:
        dist = path_finding.get_ground_distance(base.get_position(), other_base.get_position(), UnitTypes.Protoss_Probe,
                                                path_finding.PathFindingOptions.UseNearestBWEMArea)
        if dist >= 0 and (dist < closest_distance or closest_distance == -1):
            closest_distance = dist
    return closest_distance


def _get_natural_for_start_location(start_location: TilePosition) -> Base | None:
    from stardust.map.path_finding import path_finding

    start_position = Position(start_location) + Position(64, 48)
    options = path_finding.PathFindingOptions.UseNearestBWEMArea

    # Natural is the closest base that has gas, unless it is further from a start position than our main.
    # In this case the map either has a backdoor expansion or two exits from the main, both of which we handle via
    # map-specific overrides.
    best_natural: Base | None = None
    best_dist = INT_MAX
    for base in _bases:
        if base.get_tile_position() == start_location or base.gas == 0:
            continue
        dist = path_finding.get_ground_distance(start_position, base.get_position(), UnitTypes.Protoss_Probe, options)
        if dist == -1 or dist > best_dist:
            continue
        best_dist = dist
        best_natural = base
    if best_natural is None:
        return None

    # Verify it is closer than the main to all other start locations
    for other_start_location in bwapi.Broodwar.getStartLocations():
        if start_location == other_start_location:
            continue
        other_position = Position(other_start_location) + Position(64, 48)
        dist_from_main = path_finding.get_ground_distance(start_position, other_position, UnitTypes.Protoss_Probe,
                                                          options)
        dist_from_base = path_finding.get_ground_distance(best_natural.get_position(), other_position,
                                                          UnitTypes.Protoss_Probe, options)
        if dist_from_main != -1 and dist_from_base != -1 and dist_from_main < dist_from_base:
            return None

    return best_natural


def _compute_probable_expansions(player: bwapi.Player, player_bases: _PlayerBases) -> None:
    from stardust.map.path_finding import path_finding

    player_bases.probable_expansions.clear()
    player_bases.island_expansions.clear()

    # If we don't yet know the location of this player's starting base, we can't guess where they will expand
    starting_main = player_bases.starting_main
    if starting_main is None:
        return

    my_bases = set(get_my_bases(player))
    enemy_bases = get_enemy_bases(player)
    self_player = bwapi.Broodwar.self()

    scored_bases: list[tuple[int, Base, bool]] = []
    for base in _bases:
        # Skip bases that are already taken.
        # We consider any base owned by a different player than us to be taken.
        # We consider bases owned by us to be taken if the resource depot exists.
        if base.owner is not None and (base.owner != self_player or player != self_player
                                       or base.resource_depot is not None):
            continue

        # Want to be close to our own base
        distance_from_us = _closest_base_distance(base, my_bases)

        # If the player has no known bases, assume they expanded based on proximity to their starting main
        if distance_from_us == -1:
            distance_from_us = path_finding.get_ground_distance(base.get_position(), starting_main.get_position(),
                                                                UnitTypes.Protoss_Probe,
                                                                path_finding.PathFindingOptions.UseNearestBWEMArea)

        # Might be an island base
        island = distance_from_us == -1
        if island:
            distance_from_us = starting_main.get_position().getApproxDistance(base.get_position())

        # Want to be far from the enemy base
        distance_from_enemy = _closest_base_distance(base, enemy_bases)
        if distance_from_enemy == -1:
            distance_from_enemy = 10000

        # Initialize score based on distances, then increase it based on available resources
        score = (distance_from_enemy * 3) // 2 - distance_from_us
        score += base.minerals // 100
        score += base.gas // 50

        scored_bases.append((score, base, island))

    # Highest score first (C++ breaks ties by pointer address, which is arbitrary)
    scored_bases.sort(key=lambda scored: scored[0], reverse=True)
    for _, base, island in scored_bases:
        (player_bases.island_expansions if island else player_bases.probable_expansions).append(base)


def _set_base_owner(base: Base, owner: bwapi.Player | None, resource_depot: Unit | None = None) -> None:
    from stardust.map.path_finding import path_finding

    self_player = bwapi.Broodwar.self()

    def set_resource_depot() -> None:
        if resource_depot is None:
            return
        base.resource_depot = resource_depot
        if resource_depot.player == self_player and resource_depot.bwapi_unit is not None:
            resource_depot.bwapi_unit.setRallyPoint(base.mineral_line_center)

    if base.owner == owner:
        set_resource_depot()
        return

    # If the base previously had an owner, remove it from their list of owned bases
    if base.owner is not None:
        player_bases = _bases_of(base.owner)
        player_bases.all_owned.discard(base)

        # If this was the player's main base, pick the oldest of the player's known owned bases as their new main
        if base is player_bases.main:
            player_bases.main = None
            oldest_base = INT_MAX
            for other in player_bases.all_owned:
                if other.island:
                    continue
                if other.owned_since < oldest_base:
                    oldest_base = other.owned_since
                    player_bases.main = other

        # If this was our base, remove the mineral line tiles
        if base.owner == self_player:
            for tile in base.mineral_line_tiles:
                _in_own_mineral_line[tile.x + tile.y * _map_width] = 0
            path_finding.remove_blocking_tiles(base.mineral_line_tiles)

    def player_label(player: bwapi.Player | None) -> str:
        return player.getName() if player is not None else "(none)"

    message = (f"Changing base {base.get_tile_position()} owner from {player_label(base.owner)} "
               f"to {player_label(owner)}")
    cherryvis.log(message)
    log.get(message)

    base.owner = owner
    base.owned_since = common.current_frame
    base.resource_depot = None
    set_resource_depot()

    # If the base has a new owner, add it to their list of owned bases
    if owner is not None:
        owner_bases = _bases_of(owner)
        owner_bases.all_owned.add(base)

        # If this base is at a start position and we haven't registered a starting main base for the new owner, do so
        if owner_bases.starting_main is None and base.is_starting_base():
            owner_bases.starting_main = base
            owner_bases.main = base

            for starting_location in _starting_locations:
                if starting_location.main is not base:
                    continue
                owner_bases.starting_natural = starting_location.natural
                owner_bases.starting_main_choke = starting_location.main_choke
                owner_bases.starting_natural_choke = starting_location.natural_choke
                break

            if owner == bwapi.Broodwar.enemy():
                _map_specific_override.enemy_starting_main_determined()

        # If this player had a starting main, but doesn't currently have a main, set this base as the main
        if owner_bases.starting_main is not None and owner_bases.main is None and not base.island:
            owner_bases.main = base

        # If this is our base, add the mineral line tiles as blocking the navigation grids
        if owner == self_player:
            for tile in base.mineral_line_tiles:
                _in_own_mineral_line[tile.x + tile.y * _map_width] = 1
            path_finding.add_blocking_tiles(base.mineral_line_tiles)

    # Location of bases affects where all players might expand, so recompute the probable expansions for all players
    for player, player_bases in list(_player_to_player_bases.items()):
        _compute_probable_expansions(player, player_bases)


def _infer_base_ownership_from_unit_created(unit: Unit) -> None:
    """Given a newly-discovered building, determine whether it infers something about base ownership.

    - If we see a building within 10 tiles of the expected position of a resource depot, we consider the base to be
      owned by that player
    - We keep track of the resource depot itself when we see it, so if two players have buildings in the same base,
      the player owning the resource depot is considered to be the owner of the base
    - We infer unknown start positions if we see buildings nearby
    """
    from stardust.map.path_finding import path_finding

    game = bwapi.Broodwar

    # Only consider non-neutral non-flying buildings
    if unit.player == game.neutral() or not unit.type.isBuilding() or unit.is_flying:
        return

    # Only consider base ownership for self with depots
    if unit.player == game.self() and not unit.type.isResourceDepot():
        return

    # Check if there is a base near the building
    nearby_base = base_near(unit.last_position)
    if nearby_base is not None:
        # If this is an enemy building and the base is our natural, only change the ownership if the building overlaps
        # the depot position, is a resource depot, or is something that can shoot at ground units
        if (nearby_base is get_my_natural() and not unit.type.isResourceDepot() and not unit.can_attack_ground()
                and unit.player != game.self()):
            if not geo.overlaps(UnitTypes.Protoss_Nexus, nearby_base.get_position(), unit.type, unit.last_position):
                return

        # Is this unit a resource depot that is closer than the existing resource depot registered for this base?
        depot: Unit | None = None
        if unit.type.isResourceDepot():
            if nearby_base.resource_depot is not None:
                existing_dist = nearby_base.resource_depot.last_position.getApproxDistance(nearby_base.get_position())
                new_dist = unit.last_position.getApproxDistance(nearby_base.get_position())
                if new_dist < existing_dist:
                    depot = unit
            else:
                depot = unit

        # If the base was previously unowned, or this is a closer depot, change the ownership
        if nearby_base.owner is None or depot is not None:
            _set_base_owner(nearby_base, unit.player, depot)

    # If we haven't found the player's main base yet, determine if this building infers the start location
    player_bases = _bases_of(unit.player)
    if player_bases.starting_main is None:
        options = path_finding.PathFindingOptions.UseNearestBWEMArea
        for starting_location in _starting_locations:
            starting_location_base = starting_location.main
            if starting_location_base.owner is not None:
                continue

            dist = path_finding.get_ground_distance(unit.last_position, starting_location_base.get_position(),
                                                    UnitTypes.Protoss_Probe, options)
            if dist != -1 and dist < 1500:
                _set_base_owner(starting_location_base, unit.player)
                break

            natural = starting_location.natural
            if natural is not None:
                natural_dist = path_finding.get_ground_distance(unit.last_position, natural.get_position(),
                                                                UnitTypes.Protoss_Probe, options)
                if natural_dist != -1 and natural_dist < 640:
                    _set_base_owner(starting_location_base, unit.player)
                    break


def _check_creep(base: Base) -> bool:
    import stardust.opponent as opponent

    if not opponent.can_be_race(Races.Zerg):
        return False

    game = bwapi.Broodwar
    tile = base.get_tile_position()
    # The resource depot's own tiles (x 0..3, y 0..1) are skipped
    for x in (*range(-8, 0), *range(4, 12)):
        for y in (*range(-5, 0), *range(2, 8)):
            tile_x = tile.x + x
            tile_y = tile.y + y
            if tile_x < 0 or tile_x >= _map_width or tile_y < 0 or tile_y >= _map_height:
                continue
            if game.hasCreep(tile_x, tile_y):
                return True
    return False


def _validate_base_ownership(base: Base, recently_destroyed_building: Unit | None = None) -> None:
    from stardust.units import units

    if base.owner is None or base.last_scouted <= base.owned_since:
        return

    def is_nearby_building(unit: Unit) -> bool:
        if recently_destroyed_building is not None and unit.id == recently_destroyed_building.id:
            return False
        if not unit.type.isBuilding() or unit.type.isAddon() or not unit.last_position_valid:
            return False
        return base.get_position().getApproxDistance(unit.last_position) < 320

    if base.owner == bwapi.Broodwar.self():
        if any(is_nearby_building(unit) for unit in units.all_mine()):
            return
    else:
        # For the case where we are doing periodic re-evaluation, assume the base is still owned by the enemy if it
        # has creep
        if recently_destroyed_building is None and _check_creep(base):
            return
        if any(is_nearby_building(unit) for unit in units.all_enemy()):
            return

    _set_base_owner(base, None)


def _infer_base_ownership_from_unit_destroyed(unit: Unit) -> None:
    """If the lost unit is a building near a base owned by the unit's owner, change the base ownership if the player
    has no buildings left near the base."""
    if not unit.type.isBuilding():
        return

    nearby_base = base_near(unit.last_position)
    if nearby_base is None or nearby_base.owner is None or nearby_base.owner != unit.player:
        return

    # Clear dead resource depot
    if nearby_base.resource_depot is unit:
        nearby_base.resource_depot = None

    # Check if the destruction of the unit results in the base ownership changing
    _validate_base_ownership(nearby_base, unit)


def _compute_narrow_choke_tiles() -> None:
    global _narrow_choke_tiles
    _narrow_choke_tiles = bytearray(_map_width * _map_height)
    for c in _chokes.values():
        if not c.is_narrow_choke:
            continue
        for choke_tile in c.choke_tiles:
            _narrow_choke_tiles[choke_tile.x + choke_tile.y * _map_width] = 1
    _heatmap("NarrowChokeTiles", _narrow_choke_tiles)


def _compute_island_tiles() -> None:
    from stardust.map.path_finding import path_finding

    global _island_tiles
    bwem_map = bwem.Instance()
    start = Position(bwapi.Broodwar.self().getStartLocation())

    # Gather all "island" areas, which are areas not ground-connected to our start location
    island_areas = {area for area in bwem_map.Areas()
                    if path_finding.get_ground_distance(Position(area.Top()), start) == -1}
    _map_specific_override.add_island_areas(island_areas)

    _island_tiles = bytearray(_map_width * _map_height)
    for y in range(_map_height):
        for x in range(_map_width):
            tile_area = bwem_map.GetArea(TilePosition(x, y))
            if tile_area is not None and tile_area in island_areas:
                _island_tiles[x + y * _map_width] = 1
    _heatmap("IslandTiles", _island_tiles)


def _compute_leaf_area_tiles() -> None:
    global _leaf_area_tiles
    bwem_map = bwem.Instance()

    # Gather all "leaf" areas, which we define as areas that are only connected by narrow or blocked chokes.
    # We also always add our starting main areas as leaf areas.
    leaf_areas = set(_starting_base_areas.setdefault(get_my_main(), set()))

    def is_island_area(area: bwem.Area) -> bool:
        return is_on_island(TilePosition(area.Top()))

    for area in bwem_map.Areas():
        if is_island_area(area):
            continue
        is_leaf = True
        for bwem_choke in area.ChokePoints():
            c = choke(bwem_choke)
            if c is None:
                continue
            first, second = bwem_choke.GetAreas()
            if not c.is_narrow_choke and not is_island_area(first) and not is_island_area(second):
                is_leaf = False
                break
        if is_leaf:
            leaf_areas.add(area)

    _leaf_area_tiles = bytearray(_map_width * _map_height)
    for y in range(_map_height):
        for x in range(_map_width):
            tile_area = bwem_map.GetArea(TilePosition(x, y))
            if tile_area is not None and tile_area in leaf_areas:
                _leaf_area_tiles[x + y * _map_width] = 1
    _heatmap("LeafAreaTiles", _leaf_area_tiles)


def _dump_static_heatmaps() -> None:
    """Heatmaps for static map things like ground height."""
    if not config.INSTRUMENTATION_ENABLED:
        return
    game = bwapi.Broodwar
    bwem_map = bwem.Instance()
    w, h = _map_width, _map_height

    cherryvis.add_heatmap("GroundHeight", [game.getGroundHeight(x, y) for y in range(h) for x in range(w)], w, h)
    cherryvis.add_heatmap("Buildable", [int(game.isBuildable(x, y)) for y in range(h) for x in range(w)], w, h)
    cherryvis.add_heatmap(
        "Walkable",
        [2 if game.isBuildable(x // 4, y // 4) else int(game.isWalkable(x, y)) for y in range(h * 4)
         for x in range(w * 4)],
        w * 4, h * 4)
    cherryvis.add_heatmap(
        "Altitude", [bwem_map.GetMiniTile(WalkPosition(x, y)).Altitude() for y in range(h * 4) for x in range(w * 4)],
        w * 4, h * 4)
    cherryvis.add_heatmap(
        "EdgeTiles", [100 if TilePosition(x, y) in _edge_positions_to_area else 0 for y in range(h) for x in range(w)],
        w, h)

    # Mineral lines from all bases
    mineral_lines = [0] * (w * h)
    for base in _bases:
        rally_tile = base.worker_defense_rally_patch.tile if base.worker_defense_rally_patch is not None else None
        center_tile = TilePosition(base.mineral_line_center)
        for y in range(h):
            for x in range(w):
                here = TilePosition(x, y)
                if here == rally_tile or here == center_tile:
                    mineral_lines[x + y * w] = 10
                elif base.is_in_mineral_line(here):
                    mineral_lines[x + y * w] = -10
    cherryvis.add_heatmap("MineralLines", mineral_lines, w, h)

    # Bases and resources
    bases_cvis = [0] * (w * h)

    def set_base_tiles(tile: TilePosition, size: TilePosition, value: int) -> None:
        for y in range(tile.y, tile.y + size.y):
            for x in range(tile.x, tile.x + size.x):
                bases_cvis[x + y * w] = value

    value = 500
    for base in _bases:
        set_base_tiles(base.get_tile_position(), UnitTypes.Protoss_Nexus.tileSize(), value)
        for patch in base.mineral_patches():
            set_base_tiles(patch.tile, UnitTypes.Resource_Mineral_Field.tileSize(), value)
        for geyser in base.geysers_or_refineries():
            set_base_tiles(geyser.tile, UnitTypes.Resource_Vespene_Geyser.tileSize(), value)
        value += 20
    cherryvis.add_heatmap("Bases", bases_cvis, w, h)

    # Chokes and narrow chokes, at walk tile resolution
    chokes_cvis = [0] * (w * h * 16)
    narrow_chokes_cvis = [0] * (w * h * 16)

    def set_walk(data: list[int], pos: Position | WalkPosition | TilePosition, val: int) -> None:
        wp = pos if isinstance(pos, WalkPosition) else WalkPosition(pos)
        data[wp.x + wp.y * w * 4] = val

    for bwem_choke, c in _chokes.items():
        for pos in bwem_choke.Geometry():
            set_walk(chokes_cvis, pos, 1)
        set_walk(chokes_cvis, c.center, 30)
        if c.is_narrow_choke:
            for data in (chokes_cvis, narrow_chokes_cvis):
                set_walk(data, c.end1_exit, 15)
                set_walk(data, c.end2_exit, 15)
                set_walk(data, c.end1_center, 20)
                set_walk(data, c.end2_center, 20)
            set_walk(narrow_chokes_cvis, c.center, 30)
        if c.high_elevation_tile.isValid():
            set_walk(chokes_cvis, WalkPosition(c.high_elevation_tile) + WalkPosition(2, 2), 30)
    cherryvis.add_heatmap("Chokes", chokes_cvis, w * 4, h * 4)
    cherryvis.add_heatmap("NarrowChokes", narrow_chokes_cvis, w * 4, h * 4)


# ---------------------------------------------------------------------------------------------------------------------
# Walkability (Map_Walkability.cpp)


def _update_tile_distance_to_unwalkable(x: int, y: int) -> None:
    index = x + y * _map_width
    dirs = _tile_distance_to_unwalkable_directions
    dir_index = index << 3
    # Cap it at 20, we treat anything more than this as open terrain
    _tile_distance_to_unwalkable[index] = min(20, *dirs[dir_index:dir_index + 8])


def _update_tile_walkable_width(x: int, y: int) -> None:
    index = x + y * _map_width
    d = _tile_distance_to_unwalkable_directions
    i = index << 3
    _tile_walkable_width[index] = min(d[i + _DIR_N] + d[i + _DIR_S],
                                      d[i + _DIR_E] + d[i + _DIR_W],
                                      d[i + _DIR_NE] + d[i + _DIR_SW],
                                      d[i + _DIR_NW] + d[i + _DIR_SE])


def _initialize_distance_to_unwalkable() -> None:
    global _tile_distance_to_unwalkable_directions, _tile_distance_to_unwalkable, _tile_walkable_width
    w, h = _map_width, _map_height
    _tile_distance_to_unwalkable_directions = [0] * (w * h * 8)
    _tile_distance_to_unwalkable = [0] * (w * h)
    _tile_walkable_width = [0] * (w * h)
    dirs = _tile_distance_to_unwalkable_directions
    walkability = _tile_walkability

    def line(x: int, delta_x: int, y: int, delta_y: int, direction: int) -> None:
        current = 0
        while 0 <= x < w and 0 <= y < h:
            current = current + 1 if walkability[x + y * w] else 0
            dirs[((x + y * w) << 3) + direction] = current
            x += delta_x
            y += delta_y

    for x in range(w):
        line(x, 0, 0, 1, _DIR_N)  # South
        line(x, 0, h - 1, -1, _DIR_S)  # North
        line(x, 1, 0, 1, _DIR_NW)  # South-east
        line(x, -1, 0, 1, _DIR_NE)  # South-west
        line(x, 1, h - 1, -1, _DIR_SW)  # North-east
        line(x, -1, h - 1, -1, _DIR_SE)  # North-west

    for y in range(h):
        line(0, 1, y, 0, _DIR_W)  # East
        line(w - 1, -1, y, 0, _DIR_E)  # West

    for y in range(h - 1):
        line(0, 1, y, -1, _DIR_SW)  # North-east
        line(w - 1, -1, y, -1, _DIR_SE)  # North-west

    for y in range(1, h):
        line(0, 1, y, 1, _DIR_NW)  # South-east
        line(w - 1, -1, y, 1, _DIR_NE)  # South-west

    for y in range(h):
        for x in range(w):
            _update_tile_walkable_width(x, y)
            _update_tile_distance_to_unwalkable(x, y)


def _update_collision_vectors(tile_x: int, tile_y: int, walkable: bool) -> None:
    sign = 1 if walkable else -1
    for offset_x, offset_y, delta_x, delta_y in ((-1, -1, 10, 10), (-1, 0, 14, 0), (-1, 1, 10, -10), (0, 1, 0, -14),
                                                 (1, 1, -10, -10), (1, 0, -14, 0), (1, -1, -10, 10), (0, -1, 0, 14)):
        x = tile_x + offset_x
        y = tile_y + offset_y
        if x < 0 or x >= _map_width or y < 0 or y >= _map_height:
            continue
        _tile_collision_vector_x[x + y * _map_width] += sign * delta_x
        _tile_collision_vector_y[x + y * _map_width] += sign * delta_y


def _update_tile_walkability(tile: TilePosition, size: TilePosition, walkable: bool) -> bool:
    """Updates the tile walkability grid based on appearance or disappearance of a building, mineral field, etc."""
    w, h = _map_width, _map_height
    walkability = _tile_walkability
    dirs = _tile_distance_to_unwalkable_directions
    value = 1 if walkable else 0

    updated = False
    bottom_right_x = tile.x + size.x
    bottom_right_y = tile.y + size.y
    for y in range(tile.y, bottom_right_y):
        for x in range(tile.x, bottom_right_x):
            if walkability[x + y * w] != value:
                walkability[x + y * w] = value
                _update_collision_vectors(x, y, walkable)
                updated = True

    if not updated:
        return False

    # Update distance to unwalkable
    changed: set[tuple[int, int]] = set()
    if walkable:
        # We need to trace "inside out" from all of the outer tiles
        def line(x: int, delta_x: int, y: int, delta_y: int, direction: int) -> None:
            current = 0

            # Initialize the current value to the previous tile
            prev_x = x - delta_x
            prev_y = y - delta_y
            if 0 <= prev_x < w and 0 <= prev_y < h:
                current = dirs[((prev_x + prev_y * w) << 3) + direction]

            while 0 <= x < w and 0 <= y < h:
                if not walkability[x + y * w]:
                    return
                current += 1
                dirs[((x + y * w) << 3) + direction] = current
                changed.add((x, y))
                x += delta_x
                y += delta_y

        # Up from the bottom, down from the top
        for x in range(tile.x, bottom_right_x):
            line(x, 0, tile.y, 1, _DIR_N)  # South
            line(x, 0, bottom_right_y - 1, -1, _DIR_S)  # North
            line(x, 1, tile.y, 1, _DIR_NW)  # South-east
            line(x, -1, tile.y, 1, _DIR_NE)  # South-west
            line(x, 1, bottom_right_y - 1, -1, _DIR_SW)  # North-east
            line(x, -1, bottom_right_y - 1, -1, _DIR_SE)  # North-west

        # Left from the right, right from the left
        for y in range(tile.y, bottom_right_y):
            line(tile.x, 1, y, 0, _DIR_W)  # East
            line(bottom_right_x - 1, -1, y, 0, _DIR_E)  # West

        # Diagonal up from the sides
        for y in range(tile.y, bottom_right_y - 1):
            line(tile.x, 1, y, -1, _DIR_SW)  # North-east
            line(bottom_right_x - 1, -1, y, -1, _DIR_SE)  # North-west

        # Diagonal down from the sides
        for y in range(tile.y + 1, bottom_right_y):
            line(tile.x, 1, y, 1, _DIR_NW)  # South-east
            line(bottom_right_x - 1, -1, y, 1, _DIR_NE)  # South-west
    else:
        # We need to zero all of the now-unwalkable tiles, then trace outwards from the surrounding tiles
        for y in range(tile.y, bottom_right_y):
            first_index = tile.x + y * w
            for index in range(first_index, first_index + size.x):
                _tile_distance_to_unwalkable[index] = 0
                _tile_walkable_width[index] = 0
            first_dir_index = first_index << 3
            dirs[first_dir_index:first_dir_index + size.x * 8] = [0] * (size.x * 8)

        def line(x: int, delta_x: int, y: int, delta_y: int, direction: int) -> None:
            current = 0
            x += delta_x
            y += delta_y
            while 0 <= x < w and 0 <= y < h:
                if not walkability[x + y * w]:
                    return
                current += 1
                dirs[((x + y * w) << 3) + direction] = current
                changed.add((x, y))
                x += delta_x
                y += delta_y

        # Up from the top, down from the bottom
        for x in range(tile.x, bottom_right_x):
            line(x, 0, bottom_right_y - 1, 1, _DIR_N)  # South
            line(x, 0, tile.y, -1, _DIR_S)  # North
            line(x, 1, bottom_right_y - 1, 1, _DIR_NW)  # South-east
            line(x, -1, bottom_right_y - 1, 1, _DIR_NE)  # South-west
            line(x, 1, tile.y, -1, _DIR_SW)  # North-east
            line(x, -1, tile.y, -1, _DIR_SE)  # North-west

        # Left from the left, right from the right
        for y in range(tile.y, bottom_right_y):
            line(bottom_right_x - 1, 1, y, 0, _DIR_W)  # East
            line(tile.x, -1, y, 0, _DIR_E)  # West

        # Diagonal down from the sides
        for y in range(tile.y, bottom_right_y - 1):
            line(bottom_right_x - 1, 1, y, 1, _DIR_NW)  # South-east
            line(tile.x, -1, y, 1, _DIR_NE)  # South-west

        # Diagonal up from the sides
        for y in range(tile.y + 1, bottom_right_y):
            line(bottom_right_x - 1, 1, y, -1, _DIR_SW)  # North-east
            line(tile.x, -1, y, -1, _DIR_SE)  # North-west

    for x, y in changed:
        _update_tile_distance_to_unwalkable(x, y)
        _update_tile_walkable_width(x, y)

    return True


def initialize_walkability() -> None:
    global _tile_terrain_walkability, _tile_walkability, _tile_collision_vector_x, _tile_collision_vector_y
    global _tile_walkability_updated
    game = bwapi.Broodwar
    w, h = _map_width, _map_height

    _tile_collision_vector_x = [0] * (w * h)
    _tile_collision_vector_y = [0] * (w * h)
    _tile_walkability_updated = True
    _tile_terrain_walkability = bytearray(w * h)
    _tile_walkability = bytearray(w * h)

    # Start by checking the normal BWAPI walkability
    for tile_x in range(w):
        for tile_y in range(h):
            walkable = True
            for walk_x in range(4):
                for walk_y in range(4):
                    if not game.isWalkable((tile_x << 2) + walk_x, (tile_y << 2) + walk_y):
                        walkable = False
                        _update_collision_vectors(tile_x, tile_y, False)
                        break
                if not walkable:
                    break
            _tile_terrain_walkability[tile_x + tile_y * w] = int(walkable)
            _tile_walkability[tile_x + tile_y * w] = int(walkable)

    # For collision vectors, mark the edges of the map as unwalkable
    for tile_x in range(-1, w + 1):
        _update_collision_vectors(tile_x, -1, False)
        _update_collision_vectors(tile_x, h, False)
    for tile_y in range(h + 1):
        _update_collision_vectors(-1, tile_y, False)
        _update_collision_vectors(w, tile_y, False)

    _initialize_distance_to_unwalkable()

    # Add our start position
    _update_tile_walkability(game.self().getStartLocation(), UnitTypes.Protoss_Nexus.tileSize(), False)

    # Add static neutrals
    def handle_static_neutral(tile: TilePosition, size: TilePosition) -> None:
        # Update terrain walkability directly
        for y in range(tile.y, tile.y + size.y):
            for x in range(tile.x, tile.x + size.x):
                _tile_terrain_walkability[x + y * w] = 0

        # Update our main walkability through the update method so other related structures are updated
        _update_tile_walkability(tile, size, False)

    for neutral in game.getStaticNeutralUnits():
        neutral_type = neutral.getType()
        if neutral_type.isCritter() or neutral.isFlying():
            continue

        # Eggs might not be aligned to tiles, so add any tiles they overlap
        if neutral_type in (UnitTypes.Zerg_Lurker_Egg, UnitTypes.Zerg_Egg):
            bottom_right = neutral.getInitialPosition() + Position(neutral_type.width(), neutral_type.height())
            handle_static_neutral(neutral.getInitialTilePosition(),
                                  TilePosition(bottom_right) - neutral.getInitialTilePosition())
        else:
            handle_static_neutral(neutral.getInitialTilePosition(), neutral_type.tileSize())

    dump_walkability()

    # Terrain walkability is static, so dump it only at initialization
    _heatmap("TileTerrainWalkable", _tile_terrain_walkability)


def dump_walkability() -> None:
    """Writes the tile walkability grids to CherryVis when they have changed."""
    global _tile_walkability_updated
    if not config.INSTRUMENTATION_ENABLED or not _tile_walkability_updated:
        return
    _tile_walkability_updated = False
    _heatmap("TileWalkable", _tile_walkability)
    _heatmap("TileDistToUnwalkable", _tile_distance_to_unwalkable)
    _heatmap("TileWalkableWidth", _tile_walkable_width)
    _heatmap("TileCollisionVectorX", _tile_collision_vector_x)
    _heatmap("TileCollisionVectorY", _tile_collision_vector_y)


def _on_walkability_changed(unit_type: UnitType, tile: TilePosition, walkable: bool) -> None:
    from stardust.map.path_finding import path_finding

    global _tile_walkability_updated
    if _update_tile_walkability(tile, unit_type.tileSize(), walkable):
        if walkable:
            path_finding.remove_blocking_object(unit_type, tile)
        else:
            path_finding.add_blocking_object(unit_type, tile)
        _tile_walkability_updated = True


def on_unit_created_walkability(unit: Unit) -> None:
    # Units that affect tile walkability.
    # Skip on frame 0, since we handle static neutrals and our base explicitly.
    # Skip refineries, since creation of a refinery does not affect tile walkability (there was already a geyser).
    if (common.current_frame > 0 and unit.type.isBuilding() and not unit.is_flying
            and not unit.type.isRefinery()):
        _on_walkability_changed(unit.type, unit.get_tile_position(), False)
    # FIXME (upstream): Check tile walkability for mineral fields


def on_unit_discover(unit: bwapi.Unit) -> None:
    # Update tile walkability for discovered mineral fields
    # TODO (upstream): Is this even needed?
    if common.current_frame > 0 and unit.getType().isMineralField():
        _on_walkability_changed(unit.getType(), unit.getTilePosition(), False)


def on_unit_destroy_walkability(unit: Unit | bwapi.Unit) -> None:
    if isinstance(unit, bwapi.Unit):
        unit_type = unit.getType()
        # Units that affect tile walkability
        if (unit_type.isMineralField()
                or (unit_type.isBuilding() and not unit.isFlying() and not unit_type.isRefinery())
                or (unit_type == UnitTypes.Zerg_Egg and unit.getPlayer() == bwapi.Broodwar.neutral())):
            _on_walkability_changed(unit_type, unit.getTilePosition(), True)
        return

    # Skip refineries, since destruction of a refinery does not affect tile walkability (there will still be a geyser)
    if unit.type.isBuilding() and not unit.is_flying and not unit.type.isRefinery():
        _on_walkability_changed(unit.type, unit.get_tile_position(), True)


def on_building_lifted(unit_type: UnitType, tile: TilePosition) -> None:
    _on_walkability_changed(unit_type, tile, True)


def on_building_landed(unit_type: UnitType, tile: TilePosition) -> None:
    _on_walkability_changed(unit_type, tile, False)


def is_walkable(x: int, y: int) -> bool:
    """Walkable tiles: all walk positions are walkable according to BWAPI, and not occupied by a building."""
    return bool(_tile_walkability[x + y * _map_width])


def is_walkable_tile(pos: TilePosition) -> bool:
    return bool(_tile_walkability[pos.x + pos.y * _map_width])


def is_terrain_walkable(x: int, y: int) -> bool:
    return bool(_tile_terrain_walkability[x + y * _map_width])


def unwalkable_proximity(x: int, y: int) -> int:
    return _tile_distance_to_unwalkable[x + y * _map_width]


def walkable_width(x: int, y: int) -> int:
    return _tile_walkable_width[x + y * _map_width]


def collision_vector(x: int, y: int) -> Position:
    index = x + y * _map_width
    return Position(_tile_collision_vector_x[index], _tile_collision_vector_y[index])


# ---------------------------------------------------------------------------------------------------------------------
# Lifecycle


def initialize() -> None:
    from stardust.map.path_finding import path_finding

    global _map_width, _map_height, _map_width_pixels, _map_height_pixels, _map_specific_override, _min_choke_width
    global _in_own_mineral_line, _tile_last_seen, _power, _bordering_mineral_patch
    global _narrow_choke_tiles, _leaf_area_tiles, _island_tiles

    game = bwapi.Broodwar
    _map_width = game.mapWidth()
    _map_height = game.mapHeight()
    _map_width_pixels = _map_width * 32
    _map_height_pixels = _map_height * 32
    size = _map_width * _map_height

    _bases.clear()
    _starting_locations.clear()
    _chokes.clear()
    _min_choke_width = 0
    _starting_base_areas.clear()
    _areas_to_edge_positions.clear()
    _edge_positions_to_area.clear()
    _in_own_mineral_line = bytearray(size)
    _narrow_choke_tiles = bytearray()
    _leaf_area_tiles = bytearray()
    _island_tiles = bytearray()
    _tile_last_seen = [-1] * size
    _power = []
    _player_to_player_bases.clear()

    no_go_areas.initialize()

    # Initialize BWEM
    bwem.ResetInstance()
    bwem_map = bwem.Instance()
    bwem_map.Initialize()
    bwem_map.EnableAutomaticPathAnalysis()
    found_bases = bwem_map.FindBasesForStartingLocations()
    log.debug(f"Initialized BWEM: {int(found_bases)}")

    # Select map-specific overrides
    override = _MAP_SPECIFIC_OVERRIDES.get(game.mapHash())
    if override is not None:
        import importlib

        module_name, class_name = override
        log.get(f"Using map-specific override for {class_name}")
        module = importlib.import_module(f"stardust.map.map_specific_overrides.{module_name}")
        _map_specific_override = getattr(module, class_name)()
    else:
        _map_specific_override = MapSpecificOverride()

    _bordering_mineral_patch = bytearray(size)
    for neutral in game.getStaticNeutralUnits():
        if not neutral.getType().isMineralField():
            continue
        tile = neutral.getTilePosition()
        top = tile.y - 1
        bottom = tile.y + 1
        for x in range(tile.x - 1, tile.x + 3):
            if x < 0 or x >= _map_width:
                continue
            if top >= 0:
                _bordering_mineral_patch[x + top * _map_width] = 1
            if bottom < _map_height:
                _bordering_mineral_patch[x + bottom * _map_width] = 1
        left = tile.x - 1
        right = tile.x + 2
        if left >= 0:
            _bordering_mineral_patch[left + tile.y * _map_width] = 1
        if right < _map_width:
            _bordering_mineral_patch[right + tile.y * _map_width] = 1
    _heatmap("BorderingMineralPatch", _bordering_mineral_patch)

    initialize_walkability()

    # Analyze chokepoints
    for area in bwem_map.Areas():
        for bwem_choke in area.ChokePoints():
            if bwem_choke not in _chokes:
                _chokes[bwem_choke] = Choke(bwem_choke)
    _map_specific_override.initialize_chokes(_chokes)

    # Compute the minimum choke width
    _min_choke_width = min((c.width for c in _chokes.values()), default=INT_MAX)

    _compute_island_tiles()

    # Initialize bases
    for area in bwem_map.Areas():
        for bwem_base in area.Bases():
            _bases.append(Base.from_bwem_base(bwem_base.Location(), bwem_base))
    _map_specific_override.modify_bases(_bases)
    self_start = game.self().getStartLocation()
    for base in _bases:
        if not base.is_starting_base():
            continue
        natural = _get_natural_for_start_location(base.get_tile_position())
        main_choke = _compute_main_choke(base, natural)
        natural_choke = _compute_natural_choke(base, natural, main_choke)

        starting_location = _map_specific_override.modify_starting_location(
            StartingLocation(base, natural, main_choke, natural_choke))
        _starting_locations.append(starting_location)
        if base.get_tile_position() == self_start:
            _set_base_owner(base, game.self())
    log.debug(f"Found {len(_bases)} bases")

    my_main_choke = _bases_of(game.self()).starting_main_choke
    if my_main_choke is not None:
        my_main_choke.set_as_main_choke()

    # Compute areas for all starting location bases
    for starting_location in _starting_locations:
        # Initialize with the base area
        areas = _starting_base_areas.setdefault(starting_location.main, set())
        areas.add(starting_location.main.get_area())

        # Add any areas where the path to the natural goes through the main choke
        # TODO (upstream): Doesn't work for Alchemist 3 o'clock where we don't have a main choke or natural
        if starting_location.natural is None or starting_location.main_choke is None:
            continue
        if _map_specific_override.has_backdoor_natural():
            continue

        main_bwem_choke = starting_location.main_choke.choke
        for area in bwem_map.Areas():
            if is_on_island(TilePosition(area.Top())):
                continue
            path, _ = path_finding.get_choke_point_path(Position(area.Top()),
                                                        starting_location.natural.get_position(),
                                                        UnitTypes.Protoss_Dragoon,
                                                        path_finding.PathFindingOptions.UseNearestBWEMArea)
            if main_bwem_choke in path:
                areas.add(area)

    # Gather the edges of all areas on the map
    for y in range(_map_height * 4):
        for x in range(_map_width * 4):
            if not game.isWalkable(x, y):
                continue
            wp = WalkPosition(x, y)
            mini_tile = bwem_map.GetMiniTile(wp)
            altitude = mini_tile.Altitude()
            if 24 <= altitude < 32 and mini_tile.AreaId() > 0:
                edge_area = bwem_map.GetArea(mini_tile.AreaId())
                if edge_area is None or edge_area.MaxAltitude() < 128:
                    continue  # Exclude small / narrow areas
                tp = TilePosition(wp)
                _areas_to_edge_positions.setdefault(edge_area, set()).add(tp)
                _edge_positions_to_area[tp] = edge_area

    _compute_narrow_choke_tiles()
    _compute_leaf_area_tiles()
    _dump_static_heatmaps()

    update()


def on_unit_created(unit: Unit) -> None:
    # Whenever we see a new building, determine if it infers a change in base ownership
    _infer_base_ownership_from_unit_created(unit)
    on_unit_created_walkability(unit)


def on_unit_destroy(unit: Unit | bwapi.Unit) -> None:
    """Our tracked unit (Map::onUnitDestroy(Unit)) or a raw BWAPI unit (Map::onUnitDestroy(BWAPI::Unit))."""
    if isinstance(unit, bwapi.Unit):
        unit_type = unit.getType()
        if unit_type.isMineralField():
            bwem.Instance().OnMineralDestroyed(unit)
        elif unit_type.isSpecialBuilding():
            bwem.Instance().OnStaticBuildingDestroyed(unit)
        on_unit_destroy_walkability(unit)
        _map_specific_override.on_unit_destroy(unit)
        return

    # Whenever a building is lost, determine if it infers a change in base ownership
    _infer_base_ownership_from_unit_destroyed(unit)
    on_unit_destroy_walkability(unit)


def _last_seen_or_none(x: int, y: int) -> int | None:
    if 0 <= x < _map_width and 0 <= y < _map_height:
        return _tile_last_seen[x + y * _map_width]
    return None


def update() -> None:
    game = bwapi.Broodwar
    frame = common.current_frame
    w = _map_width

    # Update the last seen frame for all visible tiles.
    # This is fairly expensive and we don't need super high resolution updates, so we split it over two frames.
    if frame % 2 == 0:
        start_y, end_y = 0, _map_height >> 1
    else:
        start_y, end_y = _map_height >> 1, _map_height
    last_seen = _tile_last_seen
    for y in range(start_y, end_y):
        row = y * w
        for x in range(w):
            if game.isVisible(x, y):
                last_seen[x + row] = frame

    # Update bases, base scouting and resource depot
    for base in _bases:
        base.update()

        # If the resource depot is killed, reset it
        if base.resource_depot is not None and not base.resource_depot.exists():
            base.resource_depot = None

        # Owned bases were scouted when we last saw each of the tiles around where the resource depot should be.
        # Unowned bases were scouted when we last saw one of the center tiles of where the resource depot should be.
        # (Stardust doesn't bounds-check these tiles; tiles off the map are ignored here.)
        tile = base.get_tile_position()
        if base.owner is not None:
            border = [(-1, -1), (0, -1), (1, -1), (2, -1), (3, -1), (4, -1), (-1, 0), (4, 0), (-1, 1), (4, 1),
                      (-1, 2), (4, 2), (-1, 3), (0, 3), (1, 3), (2, 3), (3, 3), (4, 3)]
            values = [v for v in (_last_seen_or_none(tile.x + dx, tile.y + dy) for dx, dy in border) if v is not None]
            base.last_scouted = min(values) if values else -1
        else:
            values = [v for v in (_last_seen_or_none(tile.x + 1, tile.y + 1), _last_seen_or_none(tile.x + 2, tile.y + 1))
                      if v is not None]
            base.last_scouted = max(values) if values else -1

            # Check for creep
            if (base.owned_since == -1 or base.owned_since < frame - 2500) and _check_creep(base):
                _set_base_owner(base, game.enemy())

    # Infer single-enemy base ownership from which starting locations are scouted
    if len(game.enemies()) == 1 and _bases_of(game.enemy()).starting_main is None:
        unscouted = unscouted_starting_locations()
        if len(unscouted) == 1:
            _set_base_owner(next(iter(unscouted)), game.enemy())

    # Periodically check if base ownership is out-of-sync for any bases
    if frame % 24 == 17:
        for base in list(_bases):
            _validate_base_ownership(base)

    dump_walkability()
    no_go_areas.update()


# ---------------------------------------------------------------------------------------------------------------------
# Queries


def map_specific_override() -> MapSpecificOverride:
    return _map_specific_override


def all_bases() -> list[Base]:
    return _bases


def get_my_bases(player: bwapi.Player | None = None) -> set[Base]:
    return _bases_of(player).all_owned


def get_enemy_bases(player: bwapi.Player | None = None) -> set[Base]:
    if player is None:
        player = bwapi.Broodwar.self()
    return {base for base in _bases if base.owner is not None and base.owner.isEnemy(player)}


def get_untaken_expansions(player: bwapi.Player | None = None) -> list[Base]:
    return _bases_of(player).probable_expansions


def get_untaken_island_expansions(player: bwapi.Player | None = None) -> list[Base]:
    return _bases_of(player).island_expansions


def get_my_main() -> Base | None:
    return _bases_of(bwapi.Broodwar.self()).starting_main


def get_my_natural() -> Base | None:
    return _bases_of(bwapi.Broodwar.self()).starting_natural


def set_my_natural(base: Base) -> None:
    _bases_of(bwapi.Broodwar.self()).starting_natural = base
    log.get(f"Set my natural to {base.get_tile_position()}")


def get_enemy_starting_main() -> Base | None:
    return _bases_of(bwapi.Broodwar.enemy()).starting_main


def get_enemy_starting_natural() -> Base | None:
    return _bases_of(bwapi.Broodwar.enemy()).starting_natural


def get_enemy_main() -> Base | None:
    return _bases_of(bwapi.Broodwar.enemy()).main


def set_enemy_starting_main(base: Base) -> None:
    if base.owner is not None:
        return
    _set_base_owner(base, bwapi.Broodwar.enemy())


def set_enemy_starting_natural(base: Base) -> None:
    _bases_of(bwapi.Broodwar.enemy()).starting_natural = base
    log.get(f"Set enemy natural to {base.get_tile_position()}")


def get_hidden_base() -> Base | None:
    """A base with gas that is far away from both starting bases."""
    from stardust.map.path_finding import path_finding

    my_main = _bases_of(bwapi.Broodwar.self()).starting_main
    enemy_main = _bases_of(bwapi.Broodwar.enemy()).starting_main
    if enemy_main is None or my_main is None:
        return None

    options = path_finding.PathFindingOptions.UseNearestBWEMArea
    best: Base | None = None
    best_score = -1
    for base in _bases:
        if base.owner is not None or base.gas < 2000:
            continue

        our_dist = path_finding.get_ground_distance(base.get_position(), my_main.get_position(),
                                                    UnitTypes.Protoss_Probe, options)
        if our_dist == -1:
            continue

        enemy_dist = path_finding.get_ground_distance(base.get_position(), enemy_main.get_position(),
                                                      UnitTypes.Protoss_Probe, options)
        if enemy_dist == -1:
            enemy_dist = 10000

        # Pick the base with the longest total distance
        score = our_dist + enemy_dist
        if score > best_score:
            best_score = score
            best = base

    return best


def base_near(position: Position) -> Base | None:
    area = bwem.Instance().GetArea(WalkPosition(position))
    if area is None:
        return None

    closest_dist = INT_MAX
    result: Base | None = None
    for base in _bases:
        if base.get_area() != area:
            continue
        dist = base.get_position().getApproxDistance(position)
        if dist < 500 and dist < closest_dist:
            closest_dist = dist
            result = base
    return result


def all_starting_locations() -> list[Base]:
    return [starting_location.main for starting_location in _starting_locations]


def unscouted_starting_locations() -> set[Base]:
    return {sl.main for sl in _starting_locations if sl.main.last_scouted < 0}


def all_chokes() -> list[Choke]:
    return list(_chokes.values())


def choke_near(pos: Position, max_distance: int = 160) -> Choke | None:
    """The choke with the center nearest the given position, up to max_distance away."""
    best_dist = max_distance + 1
    best: Choke | None = None
    for c in _chokes.values():
        dist = c.center.getApproxDistance(pos)
        if dist < best_dist:
            best_dist = dist
            best = c
    return best


def choke(bwem_choke: bwem.ChokePoint | None) -> Choke | None:
    if bwem_choke is None:
        return None
    return _chokes.get(bwem_choke)


def get_my_main_choke() -> Choke | None:
    return _bases_of(bwapi.Broodwar.self()).starting_main_choke


def get_my_natural_choke() -> Choke | None:
    return _bases_of(bwapi.Broodwar.self()).starting_natural_choke


def set_my_main_choke(c: Choke) -> None:
    c.set_as_main_choke()
    _bases_of(bwapi.Broodwar.self()).starting_main_choke = c
    log.get(f"Set my main choke to {TilePosition(c.center)}")
    building_placement.on_main_choke_changed()


def get_enemy_main_choke() -> Choke | None:
    return _bases_of(bwapi.Broodwar.enemy()).starting_main_choke


def set_enemy_main_choke(c: Choke) -> None:
    _bases_of(bwapi.Broodwar.enemy()).starting_main_choke = c
    log.get(f"Set enemy main choke to {TilePosition(c.center)}")


def get_enemy_natural_choke() -> Choke | None:
    return _bases_of(bwapi.Broodwar.enemy()).starting_natural_choke


def min_choke_width() -> int:
    return _min_choke_width


def get_my_main_areas() -> set[bwem.Area]:
    return _starting_base_areas.setdefault(get_my_main(), set())


def get_starting_base_areas(base: Base | None) -> set[bwem.Area]:
    return _starting_base_areas.setdefault(base, set())


def get_starting_base_natural(base: Base) -> Base | None:
    for starting_location in _starting_locations:
        if starting_location.main is base:
            return starting_location.natural
    return None


def get_starting_base_chokes(base: Base) -> tuple[Choke | None, Choke | None]:
    for starting_location in _starting_locations:
        if starting_location.main is base:
            return starting_location.main_choke, starting_location.natural_choke
    return None, None


def get_areas_to_edge_positions() -> dict[bwem.Area, set[TilePosition]]:
    return _areas_to_edge_positions


def get_edge_positions_to_area() -> dict[TilePosition, bwem.Area]:
    return _edge_positions_to_area


def dump_power_heatmap() -> None:
    global _power
    if not config.INSTRUMENTATION_ENABLED:
        return
    game = bwapi.Broodwar
    new_power = [int(game.hasPower(x, y, UnitTypes.Protoss_Photon_Cannon))
                 for y in range(_map_height) for x in range(_map_width)]
    if new_power != _power:
        cherryvis.add_heatmap("Power", new_power, _map_width, _map_height)
        _power = new_power


def is_in_own_mineral_line(x: int, y: int) -> bool:
    return bool(_in_own_mineral_line[x + y * _map_width])


def is_in_own_mineral_line_tile(tile: TilePosition) -> bool:
    return bool(_in_own_mineral_line[tile.x + tile.y * _map_width])


def borders_mineral_patch(x: int, y: int) -> bool:
    return bool(_bordering_mineral_patch[x + y * _map_width])


def is_in_narrow_choke(pos: TilePosition) -> bool:
    return bool(_narrow_choke_tiles[pos.x + pos.y * _map_width])


def is_in_leaf_area(pos: TilePosition) -> bool:
    return bool(_leaf_area_tiles[pos.x + pos.y * _map_width])


def is_on_island(pos: TilePosition) -> bool:
    return bool(_island_tiles[pos.x + pos.y * _map_width])


def last_seen(x: int, y: int) -> int:
    return _tile_last_seen[x + y * _map_width]


def last_seen_tile(tile: TilePosition) -> int:
    return _tile_last_seen[tile.x + tile.y * _map_width]


def make_position_valid(x: int, y: int) -> tuple[int, int]:
    return max(0, min(x, _map_width_pixels - 1)), max(0, min(y, _map_height_pixels - 1))


def map_width() -> int:
    return _map_width


def map_height() -> int:
    return _map_height


def walkability_grid() -> bytearray:
    """The tile walkability array itself (x + y * map_width), for hot loops like navigation grids."""
    return _tile_walkability


def last_seen_grid() -> list[int]:
    """The tile last-seen frames themselves (x + y * map_width), for hot loops."""
    return _tile_last_seen


def own_mineral_line_grid() -> bytearray:
    """The in-own-mineral-line array itself (x + y * map_width), for hot loops like navigation grids."""
    return _in_own_mineral_line

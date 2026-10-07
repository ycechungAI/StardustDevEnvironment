"""Port of Map/NoGoAreas.{h,cpp}: tiles our units should avoid, either to keep them clear (e.g. building locations)
or because they are dangerous (nukes, storms, lurker spines, EMP)."""

from __future__ import annotations

from enum import Enum, IntEnum
from typing import TYPE_CHECKING

import bwapi
from bwapi import BulletTypes, Position, Positions, TilePosition, UnitTypes, WalkPosition
from stardust import common, config
from stardust.cpp import to_int
from stardust.instrumentation import cherryvis
from stardust.util import geo

if TYPE_CHECKING:
    from stardust.units.unit import Unit


class Type(IntEnum):
    GROUND_NAVIGATIONAL = 0  # We don't want our units to block something on the ground, like a building location
    DANGER = 1  # Danger at this area, like an impending nuke strike


class TypeFilter(Enum):
    ANY = 0
    ONLY_GROUND_NAVIGATIONAL = 1
    ONLY_DANGER = 2


class _Expiry:
    """When a no go area expires: at a frame, or when a bullet or unit no longer exists."""

    def __init__(self, area_type: Type, expiry: int | bwapi.Bullet | Unit) -> None:
        self.type = area_type
        self.frame = -1
        self.bullet: bwapi.Bullet | None = None
        self.unit: Unit | None = None
        if isinstance(expiry, int):
            self.frame = common.current_frame + expiry
        elif isinstance(expiry, bwapi.Bullet):
            self.bullet = expiry
        else:
            self.unit = expiry

    def is_expired(self) -> bool:
        if self.frame != -1:
            return common.current_frame >= self.frame
        if self.bullet is not None:
            return not self.bullet.exists()
        assert self.unit is not None
        return not self.unit.exists()


# Count of no go areas covering each tile, per type, indexed by x + y * mapWidth
_no_go_area_tiles: list[list[int]] = [[], []]
_no_go_area_tiles_updated = False
_no_go_areas_with_expiration: list[tuple[set[TilePosition], _Expiry]] = []
_tiles_in_radius_cache: dict[int, set[TilePosition]] = {}


def _dump_no_go_area_tiles() -> None:
    if not config.INSTRUMENTATION_ENABLED:
        return
    combined = [a + b for a, b in zip(_no_go_area_tiles[0], _no_go_area_tiles[1])]
    game = bwapi.Broodwar
    cherryvis.add_heatmap("NoGoAreas", combined, game.mapWidth(), game.mapHeight())


def _add(area_type: Type, tiles: set[TilePosition]) -> None:
    global _no_go_area_tiles_updated
    counts = _no_go_area_tiles[area_type]
    width = bwapi.Broodwar.mapWidth()
    for tile in tiles:
        counts[tile.x + tile.y * width] += 1
    _no_go_area_tiles_updated = True


def _remove(area_type: Type, tiles: set[TilePosition]) -> None:
    global _no_go_area_tiles_updated
    counts = _no_go_area_tiles[area_type]
    width = bwapi.Broodwar.mapWidth()
    for tile in tiles:
        counts[tile.x + tile.y * width] -= 1
    _no_go_area_tiles_updated = True


def _generate_circle(origin: Position, radius: int) -> set[TilePosition]:
    offsets = _tiles_in_radius_cache.get(radius)
    if not offsets:
        offsets = {TilePosition(Position(x, y))
                   for x in range(-radius, radius + 1)
                   for y in range(-radius, radius + 1)
                   if geo.approximate_distance(0, x, 0, y) <= radius}
        _tiles_in_radius_cache[radius] = offsets

    tile_origin = TilePosition(origin)
    return {here for here in (tile + tile_origin for tile in offsets) if here.isValid()}


def _generate_directed_box(origin: Position, target: Position, width: int) -> set[TilePosition]:
    length = origin.getApproxDistance(target)
    scaled_vector = geo.scale_vector(target - origin, 16)
    scaled_inverse = Position(scaled_vector.y, scaled_vector.x)

    result: set[TilePosition] = set()

    def insert_if_valid(tile: TilePosition) -> None:
        if tile.isValid():
            result.add(tile)

    current_lengthwise = origin
    for _ in range(length // 16 + 1):
        insert_if_valid(TilePosition(current_lengthwise))

        current_widthwise = current_lengthwise
        for _ in range(width // 16):
            current_widthwise = current_widthwise + scaled_inverse
            insert_if_valid(TilePosition(current_widthwise))

        current_widthwise = current_lengthwise
        for _ in range(width // 16):
            current_widthwise = current_widthwise - scaled_inverse
            insert_if_valid(TilePosition(current_widthwise))

        current_lengthwise = current_lengthwise + scaled_vector

    return result


def initialize() -> None:
    global _no_go_area_tiles_updated
    size = bwapi.Broodwar.mapWidth() * bwapi.Broodwar.mapHeight()
    for i in range(len(_no_go_area_tiles)):
        _no_go_area_tiles[i] = [0] * size
    _no_go_area_tiles_updated = True  # So we get an initial null state
    _no_go_areas_with_expiration.clear()
    update()


def update() -> None:
    still_active = []
    for tiles, expiry in _no_go_areas_with_expiration:
        if expiry.is_expired():
            _remove(expiry.type, tiles)
        else:
            still_active.append((tiles, expiry))
    _no_go_areas_with_expiration[:] = still_active

    for nuke_dot in bwapi.Broodwar.getNukeDots():
        if nuke_dot.isValid():
            add_circle(Type.DANGER, nuke_dot, 256, 1)


def write_instrumentation() -> None:
    global _no_go_area_tiles_updated
    if _no_go_area_tiles_updated:
        _dump_no_go_area_tiles()
        _no_go_area_tiles_updated = False


def _change_box(area_type: Type, top_left: TilePosition, size: TilePosition, delta: int) -> None:
    global _no_go_area_tiles_updated
    width = bwapi.Broodwar.mapWidth()
    counts = _no_go_area_tiles[area_type]
    bottom_right = top_left + size
    for y in range(top_left.y, bottom_right.y):
        if y < 0 or y >= width:  # (Stardust compares y against the map width)
            continue
        for x in range(top_left.x, bottom_right.x):
            if x < 0 or x >= width:
                continue
            counts[x + y * width] += delta
    _no_go_area_tiles_updated = True


def add_box(area_type: Type, top_left: TilePosition, size: TilePosition) -> None:
    """Adds a box-shaped no-go area that must be manually removed by calling remove_box."""
    _change_box(area_type, top_left, size, 1)


def remove_box(area_type: Type, top_left: TilePosition, size: TilePosition) -> None:
    _change_box(area_type, top_left, size, -1)


def add_circle(area_type: Type, origin: Position, radius: int, expiry: int | bwapi.Bullet | Unit) -> None:
    """Adds a circular no-go area that expires after a number of frames, or when a bullet or unit no longer exists."""
    tiles = _generate_circle(origin, radius)
    _add(area_type, tiles)
    _no_go_areas_with_expiration.append((tiles, _Expiry(area_type, expiry)))


def add_directed_box(area_type: Type, origin: Position, target: Position, width: int,
                     expiry: int | bwapi.Bullet) -> None:
    """Adds a box from origin to target with the given width, that expires after a number of frames or when a bullet
    no longer exists."""
    tiles = _generate_directed_box(origin, target, width)
    _add(area_type, tiles)
    _no_go_areas_with_expiration.append((tiles, _Expiry(area_type, expiry)))


def is_no_go(x: int, y: int, type_filter: TypeFilter = TypeFilter.ANY) -> bool:
    index = x + y * bwapi.Broodwar.mapWidth()
    if type_filter in (TypeFilter.ANY, TypeFilter.ONLY_DANGER) and _no_go_area_tiles[Type.DANGER][index] > 0:
        return True
    if (type_filter in (TypeFilter.ANY, TypeFilter.ONLY_GROUND_NAVIGATIONAL)
            and _no_go_area_tiles[Type.GROUND_NAVIGATIONAL][index] > 0):
        return True
    return False


def is_no_go_tile(pos: TilePosition, type_filter: TypeFilter = TypeFilter.ANY) -> bool:
    return is_no_go(pos.x, pos.y, type_filter)


def on_unit_create(unit: Unit) -> None:
    if unit.type == UnitTypes.Terran_Nuclear_Missile:
        cherryvis.log(f"Detected nuke targeting {WalkPosition(unit.last_position)}")
        add_circle(Type.DANGER, unit.last_position, 288 + 32, unit)


def on_bullet_create(bullet: bwapi.Bullet) -> None:
    bullet_type = bullet.getType()
    if bullet_type == BulletTypes.Psionic_Storm:
        cherryvis.log(f"Detected storm targeting {WalkPosition(bullet.getPosition())}")
        add_circle(Type.DANGER, bullet.getPosition(), 80 + 32, bullet)

    if bullet_type == BulletTypes.Subterranean_Spines:
        direction = geo.scale_vector(Position(to_int(bullet.getVelocityX()), to_int(bullet.getVelocityY())), 192)
        if direction != Positions.Invalid:
            add_directed_box(Type.DANGER, bullet.getPosition(), bullet.getPosition() + direction, 50, bullet)

    if bullet_type == BulletTypes.EMP_Missile:
        cherryvis.log(f"Detected EMP targeting {WalkPosition(bullet.getTargetPosition())}")
        add_circle(Type.DANGER, bullet.getTargetPosition(), 80 + 32, bullet)

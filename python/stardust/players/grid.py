"""Port of Players/Grid.{h,cpp}: a player's collision, threat, detection and stasis-range values at walk-tile
resolution, maintained incrementally as units are created, move, complete and die.

Each unit contributes to every walk tile within its range of its edges. Stardust iterates a cached set of offsets per
(unit type, range); here the offsets are a cached numpy mask applied with one slice operation.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import numpy as np

import bwapi
from bwapi import Position, UnitType, UnitTypes, WalkPosition, WeaponType, WeaponTypes
from stardust import common
from stardust.instrumentation import cherryvis
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.players.upgrade_tracker import UpgradeTracker

_STASIS_RANGE = 44

# (unit type, range) -> (offset of mask[0, 0] in walk tiles (x, y), mask[dx, dy])
_positions_in_range_cache: dict[tuple[UnitType, int], tuple[int, int, np.ndarray]] = {}


def _positions_in_range(unit_type: UnitType, range_: int) -> tuple[int, int, np.ndarray]:
    """Walk tiles (relative to the unit's walk tile) within range of the unit's edges, as a mask."""
    key = (unit_type, range_)
    cached = _positions_in_range_cache.get(key)
    if cached is not None:
        return cached

    left, right = unit_type.dimensionLeft(), unit_type.dimensionRight()
    up, down = unit_type.dimensionUp(), unit_type.dimensionDown()

    # Vectorized geo.edge_to_point_distance(unit_type, origin, (x, y)) <= range_ over all pixel offsets
    xs = np.arange(-left - range_, right + range_ + 1, dtype=np.int64)
    ys = np.arange(-up - range_, down + range_ + 1, dtype=np.int64)
    x, y = np.meshgrid(xs, ys, indexing="ij")
    zero = np.zeros_like(x)
    x_dist = np.maximum(np.maximum(-left - x, x - right - 1), zero)
    y_dist = np.maximum(np.maximum(-up - y, y - down - 1), zero)
    lo = np.minimum(x_dist, y_dist)
    hi = np.maximum(x_dist, y_dist)
    min_calc = (3 * lo) >> 3
    dist = np.where(lo <= (hi >> 2), hi, (min_calc >> 5) + min_calc + hi - (hi >> 4) - (hi >> 6))
    in_range = dist <= range_

    walk_x = x[in_range] >> 3
    walk_y = y[in_range] >> 3
    if walk_x.size:
        min_x, min_y = int(walk_x.min()), int(walk_y.min())
        mask = np.zeros((int(walk_x.max()) - min_x + 1, int(walk_y.max()) - min_y + 1), dtype=np.int64)
        mask[walk_x - min_x, walk_y - min_y] = 1
    else:
        min_x = min_y = 0
        mask = np.zeros((0, 0), dtype=np.int64)

    cached = (min_x, min_y, mask)
    _positions_in_range_cache[key] = cached
    return cached


class GridData:
    """Values indexed [walk x, walk y]."""

    def __init__(self) -> None:
        self.max_x = bwapi.Broodwar.mapWidth() * 4
        self.max_y = bwapi.Broodwar.mapHeight() * 4
        self.data = np.zeros((self.max_x, self.max_y), dtype=np.int64)
        self.frame_last_updated = -1
        self.frame_last_dumped = -1

    def at_position(self, pos: Position) -> int:
        return int(self.data[pos.x >> 3, pos.y >> 3])

    def at(self, walk_x: int, walk_y: int) -> int:
        return int(self.data[walk_x, walk_y])

    def add(self, unit_type: UnitType, range_: int, position: Position, delta: int) -> None:
        min_x, min_y, mask = _positions_in_range(unit_type, range_)
        if mask.size:
            x0 = (position.x >> 3) + min_x
            y0 = (position.y >> 3) + min_y
            x1 = x0 + mask.shape[0]
            y1 = y0 + mask.shape[1]

            # Clip to the grid
            cx0, cy0 = max(x0, 0), max(y0, 0)
            cx1, cy1 = min(x1, self.max_x), min(y1, self.max_y)
            if cx0 < cx1 and cy0 < cy1:
                self.data[cx0:cx1, cy0:cy1] += delta * mask[cx0 - x0:cx1 - x0, cy0 - y0:cy1 - y0]

        self.frame_last_updated = common.current_frame


class Grid:
    def __init__(self, upgrade_tracker: UpgradeTracker, player: bwapi.Player) -> None:
        self.upgrade_tracker = upgrade_tracker
        self.range_buffer = -16 if player == bwapi.Broodwar.self() else 48
        self._collision = GridData()
        self._ground_threat = GridData()
        self._static_ground_threat = GridData()
        self._air_threat = GridData()
        self._detection = GridData()
        self._stasis_range = GridData()

    @staticmethod
    def _weapon_unit_type(unit_type: UnitType) -> UnitType:
        """The type whose weapons a unit uses: bunkers use marines, carriers interceptors, reavers scarabs."""
        if unit_type == UnitTypes.Terran_Bunker:
            return UnitTypes.Terran_Marine
        if unit_type == UnitTypes.Protoss_Carrier:
            return UnitTypes.Protoss_Interceptor
        if unit_type == UnitTypes.Protoss_Reaver:
            return UnitTypes.Protoss_Scarab
        return unit_type

    def _threat_range(self, unit_type: UnitType, weapon_range: int) -> int:
        return weapon_range + self.range_buffer + (48 if unit_type == UnitTypes.Terran_Bunker else 0)

    @staticmethod
    def _bunker_multiplier(unit_type: UnitType) -> int:
        return 4 if unit_type == UnitTypes.Terran_Bunker else 1

    def _change_threats(self, unit_type: UnitType, position: Position, burrowed: bool, immobile: bool,
                        sign: int) -> None:
        """The threat and detection contributions of a completed unit, added (sign 1) or removed (sign -1)."""
        tracker = self.upgrade_tracker
        weapon_unit_type = self._weapon_unit_type(unit_type)
        ground_weapon = weapon_unit_type.groundWeapon()
        air_weapon = weapon_unit_type.airWeapon()

        if (ground_weapon != WeaponTypes.None_ and not immobile
                and ((burrowed and unit_type == UnitTypes.Zerg_Lurker)
                     or (not burrowed and unit_type != UnitTypes.Zerg_Lurker))):
            threat_range = self._threat_range(unit_type, tracker.weapon_range(ground_weapon))
            damage = (tracker.weapon_damage(ground_weapon) * weapon_unit_type.maxGroundHits()
                      * self._bunker_multiplier(unit_type))
            min_range_damage = tracker.weapon_damage(ground_weapon) * weapon_unit_type.maxGroundHits()
            has_min_range = ground_weapon.minRange() > 0
            min_range = ground_weapon.minRange() - self.range_buffer
            stationary = unit_util.is_stationary_attacker(unit_type)

            if sign > 0:
                self._ground_threat.add(unit_type, threat_range, position, damage)
                if stationary:
                    self._static_ground_threat.add(unit_type, threat_range, position, damage)

                # For sieged tanks, subtract the area close to the tank
                if has_min_range:
                    self._ground_threat.add(unit_type, min_range, position, -min_range_damage)
                    self._static_ground_threat.add(unit_type, min_range, position, -min_range_damage)
            else:
                # For sieged tanks, add the area close to the tank before removing the entire area.
                # We need to do it in this order to avoid triggering the negative values check.
                if has_min_range:
                    self._ground_threat.add(unit_type, min_range, position, min_range_damage)
                    self._static_ground_threat.add(unit_type, min_range, position, min_range_damage)

                self._ground_threat.add(unit_type, threat_range, position, -damage)
                if stationary:
                    self._static_ground_threat.add(unit_type, threat_range, position, -damage)

        if air_weapon != WeaponTypes.None_ and not immobile and not burrowed:
            self._air_threat.add(
                unit_type, self._threat_range(unit_type, tracker.weapon_range(air_weapon)), position,
                sign * tracker.weapon_damage(air_weapon) * weapon_unit_type.maxAirHits()
                * self._bunker_multiplier(unit_type))

        if unit_type.isDetector() and not immobile:
            if unit_type.isBuilding():
                self._detection.add(unit_type, 7 * 32 + self.range_buffer, position, sign)
            else:
                self._detection.add(unit_type, tracker.unit_sight_range(unit_type) + self.range_buffer, position,
                                    sign)

    def unit_created(self, unit_type: UnitType, position: Position, completed: bool, burrowed: bool,
                     immobile: bool) -> None:
        if not unit_type.isFlyer() and not burrowed:
            self._collision.add(unit_type, 0, position, 1)
        if not immobile and unit_type in (UnitTypes.Terran_Siege_Tank_Siege_Mode, UnitTypes.Terran_Siege_Tank_Tank_Mode):
            self._stasis_range.add(unit_type, _STASIS_RANGE, position, 1)
        if completed:
            self.unit_completed(unit_type, position, burrowed, immobile)

    def unit_completed(self, unit_type: UnitType, position: Position, burrowed: bool, immobile: bool) -> None:
        self._change_threats(unit_type, position, burrowed, immobile, 1)

    def unit_moved(self, unit_type: UnitType, position: Position, burrowed: bool, immobile: bool,
                   from_type: UnitType, from_position: Position, from_burrowed: bool, from_immobile: bool) -> None:
        if (unit_type == from_type and burrowed == from_burrowed and immobile == from_immobile
                and (position.x >> 3) == (from_position.x >> 3) and (position.y >> 3) == (from_position.y >> 3)):
            return
        self.unit_destroyed(from_type, from_position, True, from_burrowed, from_immobile)
        self.unit_created(unit_type, position, True, burrowed, immobile)

    def unit_destroyed(self, unit_type: UnitType, position: Position, completed: bool, burrowed: bool,
                       immobile: bool) -> None:
        if not unit_type.isFlyer() and not burrowed:
            self._collision.add(unit_type, 0, position, -1)
        if not immobile and unit_type in (UnitTypes.Terran_Siege_Tank_Siege_Mode, UnitTypes.Terran_Siege_Tank_Tank_Mode):
            self._stasis_range.add(unit_type, _STASIS_RANGE, position, -1)

        # If the unit was a building destroyed or cancelled before completion, only the collision grid changes
        if not completed:
            return

        self._change_threats(unit_type, position, burrowed, immobile, -1)

    def unit_weapon_damage_upgraded(self, unit_type: UnitType, position: Position, weapon: WeaponType,
                                    former_damage: int, new_damage: int) -> None:
        weapon_unit_type = self._weapon_unit_type(unit_type)
        threat_range = self._threat_range(unit_type, self.upgrade_tracker.weapon_range(weapon))
        multiplier = self._bunker_multiplier(unit_type)

        if weapon.targetsGround():
            delta = (new_damage - former_damage) * weapon_unit_type.maxGroundHits() * multiplier
            self._ground_threat.add(unit_type, threat_range, position, delta)
            if unit_util.is_stationary_attacker(unit_type):
                self._static_ground_threat.add(unit_type, threat_range, position, delta)

        if weapon.targetsAir():
            self._air_threat.add(unit_type, threat_range, position,
                                 (new_damage - former_damage) * weapon_unit_type.maxAirHits() * multiplier)

    def unit_weapon_range_upgraded(self, unit_type: UnitType, position: Position, weapon: WeaponType,
                                   former_range: int, new_range: int) -> None:
        # We don't need to worry about minimum range here, since tanks do not have range upgrades.
        # Also don't need to worry about carriers and reavers.
        damage = self.upgrade_tracker.weapon_damage(weapon)
        multiplier = self._bunker_multiplier(unit_type)
        former = self._threat_range(unit_type, former_range)
        new = self._threat_range(unit_type, new_range)

        if weapon.targetsGround():
            ground_damage = damage * unit_type.maxGroundHits() * multiplier
            self._ground_threat.add(unit_type, former, position, -ground_damage)
            self._ground_threat.add(unit_type, new, position, ground_damage)
            if unit_util.is_stationary_attacker(unit_type):
                self._static_ground_threat.add(unit_type, former, position, -ground_damage)
                self._static_ground_threat.add(unit_type, new, position, ground_damage)

        if weapon.targetsAir():
            air_damage = damage * unit_type.maxAirHits() * multiplier
            self._air_threat.add(unit_type, former, position, -air_damage)
            self._air_threat.add(unit_type, new, position, air_damage)

    def unit_sight_range_upgraded(self, unit_type: UnitType, position: Position, former_range: int,
                                  new_range: int) -> None:
        # Only mobile detectors are handled here
        if not unit_type.isDetector() or unit_type.isBuilding():
            return
        self._detection.add(unit_type, former_range + self.range_buffer, position, -1)
        self._detection.add(unit_type, new_range + self.range_buffer, position, 1)

    # Queries take a Position or WalkPosition; the *_at variants take walk tile coordinates

    @staticmethod
    def _get(data: GridData, pos: Position | WalkPosition) -> int:
        if isinstance(pos, WalkPosition):
            return int(data.data[pos.x, pos.y])
        return int(data.data[pos.x >> 3, pos.y >> 3])

    def collision(self, pos: Position | WalkPosition) -> int:
        return self._get(self._collision, pos)

    def collision_at(self, walk_x: int, walk_y: int) -> int:
        return int(self._collision.data[walk_x, walk_y])

    def ground_threat(self, pos: Position | WalkPosition) -> int:
        return self._get(self._ground_threat, pos)

    def ground_threat_at(self, walk_x: int, walk_y: int) -> int:
        return int(self._ground_threat.data[walk_x, walk_y])

    def static_ground_threat(self, pos: Position | WalkPosition) -> int:
        return self._get(self._static_ground_threat, pos)

    def static_ground_threat_at(self, walk_x: int, walk_y: int) -> int:
        return int(self._static_ground_threat.data[walk_x, walk_y])

    def air_threat(self, pos: Position | WalkPosition) -> int:
        return self._get(self._air_threat, pos)

    def air_threat_at(self, walk_x: int, walk_y: int) -> int:
        return int(self._air_threat.data[walk_x, walk_y])

    def detection(self, pos: Position | WalkPosition) -> int:
        return self._get(self._detection, pos)

    def detection_at(self, walk_x: int, walk_y: int) -> int:
        return int(self._detection.data[walk_x, walk_y])

    def stasis_range(self, pos: Position | WalkPosition) -> int:
        return self._get(self._stasis_range, pos)

    def stasis_range_at(self, walk_x: int, walk_y: int) -> int:
        return int(self._stasis_range.data[walk_x, walk_y])

    @staticmethod
    def _dump_heatmap_if_changed(heatmap_name: str, data: GridData) -> None:
        if data.frame_last_dumped >= data.frame_last_updated:
            return
        # Row-major (transposed from our [x, y] layout)
        cherryvis.add_heatmap(heatmap_name, data.data.T.ravel().tolist(), data.max_x, data.max_y)
        data.frame_last_dumped = common.current_frame

    def dump_collision_heatmap_if_changed(self, heatmap_name: str) -> None:
        self._dump_heatmap_if_changed(heatmap_name, self._collision)

    def dump_ground_threat_heatmap_if_changed(self, heatmap_name: str) -> None:
        self._dump_heatmap_if_changed(heatmap_name, self._ground_threat)

    def dump_static_ground_threat_heatmap_if_changed(self, heatmap_name: str) -> None:
        self._dump_heatmap_if_changed(heatmap_name, self._static_ground_threat)

    def dump_air_threat_heatmap_if_changed(self, heatmap_name: str) -> None:
        self._dump_heatmap_if_changed(heatmap_name, self._air_threat)

    def dump_detection_heatmap_if_changed(self, heatmap_name: str) -> None:
        self._dump_heatmap_if_changed(heatmap_name, self._detection)

    def dump_stasis_range_heatmap_if_changed(self, heatmap_name: str) -> None:
        self._dump_heatmap_if_changed(heatmap_name, self._stasis_range)

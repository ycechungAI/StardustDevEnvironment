import numpy as np

from bwapi import Position, Positions, UnitTypes, WeaponTypes
from stardust.players.grid import Grid, GridData
from stardust.players.upgrade_tracker import UpgradeTracker
from stardust.util import geo
from tests.fakes import fake_game


class FixedTracker(UpgradeTracker):
    """Base stats with no upgrades, without needing a BWAPI player."""

    def __init__(self) -> None:
        pass

    def weapon_damage(self, weapon):  # type: ignore[no-untyped-def]
        return weapon.damageAmount()

    def weapon_range(self, weapon):  # type: ignore[no-untyped-def]
        if weapon in (WeaponTypes.Pulse_Cannon, WeaponTypes.Scarab):
            return 256
        return weapon.maxRange()

    def unit_sight_range(self, unit_type):  # type: ignore[no-untyped-def]
        return unit_type.sightRange()


def reference_add(data: np.ndarray, unit_type, range_, position, delta):  # type: ignore[no-untyped-def]
    """Stardust's Grid::GridData::add, iterating the offset set."""
    offsets = set()
    for x in range(-unit_type.dimensionLeft() - range_, unit_type.dimensionRight() + range_ + 1):
        for y in range(-unit_type.dimensionUp() - range_, unit_type.dimensionDown() + range_ + 1):
            if geo.edge_to_point_distance(unit_type, Positions.Origin, Position(x, y)) <= range_:
                offsets.add((x >> 3, y >> 3))
    for dx, dy in offsets:
        x, y = (position.x >> 3) + dx, (position.y >> 3) + dy
        if 0 <= x < data.shape[0] and 0 <= y < data.shape[1]:
            data[x, y] += delta


def test_grid_data_add_matches_reference_including_map_edges():
    with fake_game():
        for unit_type, range_, pos in [(UnitTypes.Protoss_Dragoon, 192 + 48, Position(500, 600)),
                                       (UnitTypes.Zerg_Zergling, 15, Position(3, 7)),
                                       (UnitTypes.Terran_Siege_Tank_Siege_Mode, 384, Position(2040, 2040)),
                                       (UnitTypes.Protoss_Nexus, 0, Position(64, 48))]:
            data = GridData()
            data.add(unit_type, range_, pos, 7)
            expected = np.zeros_like(data.data)
            reference_add(expected, unit_type, range_, pos, 7)
            assert np.array_equal(data.data, expected), unit_type


def test_unit_lifecycle_leaves_grid_empty():
    with fake_game() as game:
        grid = Grid(FixedTracker(), game.enemy())
        pos, moved = Position(800, 800), Position(900, 820)

        grid.unit_created(UnitTypes.Terran_Siege_Tank_Siege_Mode, pos, True, False, False)
        # Ground threat is the tank's damage at range, but not inside its minimum range
        damage = WeaponTypes.Arclite_Shock_Cannon.damageAmount()
        assert grid.ground_threat(pos + Position(300, 0)) == damage
        assert grid.ground_threat(pos) == 0
        assert grid.static_ground_threat(pos + Position(300, 0)) == damage
        assert grid.collision(pos) == 1

        grid.unit_moved(UnitTypes.Terran_Siege_Tank_Siege_Mode, moved, False, False,
                        UnitTypes.Terran_Siege_Tank_Siege_Mode, pos, False, False)
        grid.unit_destroyed(UnitTypes.Terran_Siege_Tank_Siege_Mode, moved, True, False, False)

        for data in (grid._collision, grid._ground_threat, grid._static_ground_threat, grid._air_threat,
                     grid._detection, grid._stasis_range):
            assert not data.data.any()


def test_bunker_counts_as_four_marines_with_extra_range():
    with fake_game() as game:
        grid = Grid(FixedTracker(), game.enemy())
        pos = Position(800, 800)
        grid.unit_created(UnitTypes.Terran_Bunker, pos, True, False, False)
        marine_damage = WeaponTypes.Gauss_Rifle.damageAmount()
        assert grid.ground_threat(pos) == 4 * marine_damage
        assert grid.air_threat(pos) == 4 * marine_damage

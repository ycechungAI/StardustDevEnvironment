import itertools
import random

from bwapi import Position, TilePosition, UnitTypes, WeaponTypes
from stardust.cpp import cdiv, cmod, cround, f32, to_int
from stardust.util import geo, unit_util
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType
from bwapi import TechTypes, UpgradeTypes


def test_cpp_semantics():
    assert (cdiv(7, 2), cdiv(-7, 2), cdiv(7, -2), cdiv(-7, -2)) == (3, -3, -3, 3)
    assert (cmod(7, 3), cmod(-7, 3), cmod(7, -3)) == (1, -1, 1)
    assert (cround(0.5), cround(1.5), cround(2.5), cround(-0.5), cround(-2.5)) == (1, 2, 3, -1, -3)
    assert f32(0.1) != 0.1 and f32(0.5) == 0.5
    assert (to_int(-1.9), to_int(1.9), to_int(float("nan"))) == (-1, 1, 0)


def test_approximate_distance_matches_bwapi():
    rng = random.Random(1)
    for _ in range(2000):
        a = Position(rng.randint(0, 8000), rng.randint(0, 8000))
        b = Position(rng.randint(0, 8000), rng.randint(0, 8000))
        assert geo.approximate_distance(a.x, b.x, a.y, b.y) == a.getApproxDistance(b)


def test_edge_distances():
    zealot, dragoon = UnitTypes.Protoss_Zealot, UnitTypes.Protoss_Dragoon
    assert geo.edge_to_edge_distance(zealot, Position(100, 100), dragoon, Position(100, 100)) == 0
    assert geo.overlaps(zealot, Position(100, 100), dragoon, Position(110, 100))
    assert not geo.overlaps(zealot, Position(100, 100), dragoon, Position(200, 100))
    far = geo.edge_to_edge_distance(zealot, Position(0, 100), zealot, Position(100, 100))
    assert far == 100 - zealot.dimensionLeft() - zealot.dimensionRight() - 1


def test_scale_vector_and_directions():
    assert geo.scale_vector(Position(3, 4), 10) == Position(6, 8)
    assert geo.scale_vector(Position(0, 0), 10) == Position(32000, 32000) or not geo.scale_vector(Position(0, 0), 10)
    # BW directions: 0 is up, 64 right, -128 down, -64 left
    assert geo.bw_direction(Position(0, -10)) == 0
    assert geo.bw_direction(Position(10, 0)) == 64
    assert geo.bw_direction(Position(0, 10)) == -128
    assert geo.bw_direction(Position(-10, 0)) == -64
    assert geo.bw_angle_diff(127, -128) == 1
    assert geo.bw_angle_add(120, 20) == -116


def test_bw_movement_accelerates_and_turns():
    x, y, heading, speed = geo.bw_movement(0, 0, 0, 64, 40, 0, 100, 256)
    assert (x, y, heading, speed) == (0, 0, 40, 100)
    x, y, heading, speed = geo.bw_movement(x, y, heading, 64, 40, speed, 100, 256)
    assert heading == 64 and speed == 200 and x > 0


def test_spiral_visits_rings_in_order():
    spiral = geo.Spiral()
    seen = [(spiral.x, spiral.y)]
    for _ in range(8):
        spiral.next()
        seen.append((spiral.x, spiral.y))
    assert set(seen) == set(itertools.product((-1, 0, 1), repeat=2))
    assert spiral.radius == 2


def test_direction_from_building():
    tile, size = TilePosition(10, 10), TilePosition(2, 2)
    assert geo.direction_from_building(tile, size, Position(330, 300)) == geo.Direction.up
    assert geo.direction_from_building(tile, size, Position(300, 330)) == geo.Direction.left
    assert geo.direction_from_building(tile, size, Position(330, 330)) == geo.Direction.error
    assert geo.direction_from_building(tile, size, Position(300, 300)) == geo.Direction.upleft


def test_unit_util():
    assert unit_util.mineral_cost(UnitTypes.Zerg_Zergling) == 25  # two per egg
    assert unit_util.mineral_cost(UnitTypes.Zerg_Lurker) == 75 + 50  # larva (free) -> hydralisk -> lurker
    assert unit_util.gas_cost(UnitTypes.Zerg_Lurker) == 25 + 100
    assert unit_util.mineral_cost(UnitTypes.Zerg_Larva) == 0
    assert unit_util.build_time(UnitTypes.Protoss_Gateway) == UnitTypes.Protoss_Gateway.buildTime() + 71
    assert unit_util.get_ground_weapon(UnitTypes.Terran_Bunker) == WeaponTypes.Gauss_Rifle
    assert unit_util.is_combat_unit(UnitTypes.Protoss_Photon_Cannon)
    assert not unit_util.is_combat_unit(UnitTypes.Protoss_Probe)
    assert unit_util.powers(TilePosition(10, 10), TilePosition(12, 10), UnitTypes.Protoss_Gateway)
    assert not unit_util.powers(TilePosition(10, 10), TilePosition(30, 10), UnitTypes.Protoss_Gateway)
    assert unit_util.halt_distance(UnitTypes.Protoss_Dragoon) == 0
    assert unit_util.ground_weapon_angle(UnitTypes.Protoss_Zealot) == 16


def test_upgrade_or_tech_type():
    storm = UpgradeOrTechType.of(TechTypes.Psionic_Storm)
    weapons = UpgradeOrTechType.of(UpgradeTypes.Protoss_Ground_Weapons)
    assert storm.is_tech_type() and not weapons.is_tech_type()
    assert storm == UpgradeOrTechType(tech_type=TechTypes.Psionic_Storm)
    assert len({storm, weapons, UpgradeOrTechType.of(TechTypes.Psionic_Storm)}) == 2
    assert storm.max_level() == 1 and weapons.max_level() == 3
    assert str(storm) == "Psionic_Storm"

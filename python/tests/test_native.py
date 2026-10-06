import pytest

import bwapi
import bwem
import fap
from bwapi import Position, UnitTypes

ZEALOT = UnitTypes.Protoss_Zealot


@pytest.fixture(scope="module", autouse=True)
def unit_scores():
    size = max(int(t) for t in UnitTypes.allUnitTypes()) + 1
    base, scaled = [0] * size, [0] * size
    for t in UnitTypes.allUnitTypes():
        score = t.mineralPrice() + t.gasPrice() * 2
        base[int(t)] = score >> 2
        scaled[int(t)] = score - (score >> 2)
    fap.set_unit_scores(base, scaled)


def add_zealot(sim: fap.CombatSimulator, player1: bool, unit_id: int, x: int) -> None:
    sim.add_unit(
        player1=player1, unit_type=ZEALOT, position=Position(x, 500), target_position=Position(x, 500),
        health=160, shields=60, flying=False, speed=ZEALOT.topSpeed(), armor=1, ground_cooldown=22,
        ground_damage=8, ground_max_range=15, air_cooldown=0, air_damage=0, air_max_range=0, elevation=0,
        attacker_count=8, attack_cooldown_remaining=0, stimmed=False, undetected=False, id=unit_id, target=0,
        collision_value=6, collision_value_choke=6,
    )


def test_fap_more_zealots_win():
    sim = fap.CombatSimulator(128, 128)
    add_zealot(sim, True, 1, 400)
    add_zealot(sim, True, 2, 420)
    add_zealot(sim, False, 3, 600)
    initial1, initial2, final1, final2, iterations = sim.run(288, 1_000_000)
    assert (initial1, initial2) == (274, 137)
    assert final1 > final2
    assert iterations == 288

    sim.reset()
    add_zealot(sim, True, 1, 400)
    add_zealot(sim, False, 2, 600)
    add_zealot(sim, False, 3, 620)
    _, _, mirrored_final1, mirrored_final2, _ = sim.run(288, 1_000_000)
    assert mirrored_final2 > mirrored_final1


def test_fap_rejects_off_map_units():
    sim = fap.CombatSimulator(64, 64)
    with pytest.raises(ValueError, match="outside the map"):
        add_zealot(sim, True, 1, 64 * 32)


def test_bwem_needs_a_game():
    assert bwem.ChokePoint.end1 != bwem.ChokePoint.end2
    with pytest.raises(RuntimeError, match="during a game"):
        bwem.Instance().Initialize()


def test_events_are_bound():
    assert hasattr(bwapi.Game, "getEvents")
    assert bwapi.EventType.UnitDestroy != bwapi.EventType.UnitCreate

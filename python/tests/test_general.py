"""Offline tests of the General's combat sim bookkeeping."""

from collections import deque

from bwapi import UnitTypes
from stardust.general.combat_sim_result import CombatSimResult
from stardust.general.unit_cluster import combat_sim
from tests.fakes import fake_game


def test_unit_values() -> None:
    with fake_game():
        combat_sim.initialize()

    # Base the score on the cost, a quarter of it independent of health
    dragoon = 125 + 50 * 2
    assert combat_sim.unit_value(UnitTypes.Protoss_Dragoon) == dragoon
    assert combat_sim._base_score[UnitTypes.Protoss_Dragoon.getID()] == dragoon >> 2

    # Adjusted values
    assert combat_sim.unit_value(UnitTypes.Protoss_Photon_Cannon) == 150 + 100
    assert combat_sim.unit_value(UnitTypes.Terran_Bunker) == 100 + 4 * 50


def test_sim_result_percentages() -> None:
    result = CombatSimResult(3, 2, 1000, 800, 600, 200)
    assert abs(result.my_percent_lost() - 0.4) < 1e-9
    assert abs(result.enemy_percent_lost() - 0.75) < 1e-9
    assert result.value_gain() == 600 - 1000 - (200 - 800)
    assert abs(result.percent_gain() - 0.35) < 1e-9
    assert abs(result.my_percentage_of_total() - 0.75) < 1e-9

    result += CombatSimResult(0, 0, 10, 10, 10, 10)
    result /= 2
    assert (result.initial_mine, result.initial_enemy, result.final_mine, result.final_enemy) == (505, 405, 305, 105)


def test_consecutive_sim_results() -> None:
    results: deque[CombatSimResult] = deque()
    for decision in (True, True, False, True, True, True):
        result = CombatSimResult()
        result.decision = decision
        results.append(result)

    assert combat_sim.consecutive_sim_results(results, 10) == (3, 5, 1)
    assert combat_sim.consecutive_sim_results(results, 2) == (2, 2, 0)
    assert combat_sim.consecutive_sim_results(deque(), 10) == (0, 0, 0)

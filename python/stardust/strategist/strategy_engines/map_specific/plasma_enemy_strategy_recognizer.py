"""Port of Strategist/StrategyEngines/MapSpecific/PlasmaEnemyStrategyRecognizer.cpp:
PlasmaStrategyEngine::recognizeEnemyStrategy."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Races, WalkPosition
from stardust import common
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.strategist.strategy_engines.map_specific.plasma_strategy_engine import PlasmaStrategyEngine


def _is_proxy() -> bool:
    if common.current_frame >= 10000:
        return False

    # We expect enemies to bug out or do weird things on Plasma, so ignore their main base timings

    # Check if we have directly scouted an enemy building or ground combat unit in one of our main areas
    accessible_areas: set[bwem.Area] = set()
    game_map.map_specific_override().modify_main_base_building_placement_areas(accessible_areas)
    bwem_map = bwem.Instance()
    for enemy_unit in units.all_enemy():
        if enemy_unit.is_flying:
            continue
        if not enemy_unit.type.isBuilding() and not unit_util.is_combat_unit(enemy_unit.type):
            continue
        if enemy_unit.type.isRefinery():
            continue  # Don't count gas steals
        if enemy_unit.type.isResourceDepot():
            continue  # Don't count expansions

        area = bwem_map.GetArea(WalkPosition(enemy_unit.last_position))
        if area in accessible_areas:
            return True

    return False


def recognize_enemy_strategy(engine: PlasmaStrategyEngine) -> PlasmaStrategyEngine.EnemyStrategy:
    from stardust.strategist.strategy_engines.map_specific.plasma_strategy_engine import PlasmaStrategyEngine

    S = PlasmaStrategyEngine.EnemyStrategy
    strategy = engine.enemy_strategy
    for _ in range(10):
        if strategy == S.Unknown:
            # Assume zergs won't do a proxy, and that others would do a proxy before frame 6000
            if bwapi.Broodwar.enemy().getRace() == Races.Zerg or common.current_frame > 6000:
                strategy = S.Normal
                continue

            if _is_proxy():
                return S.ProxyRush
        elif strategy == S.ProxyRush:
            if not _is_proxy():
                strategy = S.Normal
                continue

        return strategy

    log.get(f"ERROR: Loop in strategy recognizer, ended on {strategy.value}")
    return strategy

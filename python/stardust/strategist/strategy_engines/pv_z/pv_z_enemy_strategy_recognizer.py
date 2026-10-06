"""Port of Strategist/StrategyEngines/PvZ/PvZEnemyStrategyRecognizer.cpp: PvZ::recognizeEnemyStrategy."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import TilePosition, UnitType, UnitTypes, WalkPosition
from stardust import common
from stardust.cpp import INT_MAX
from stardust.general.unit_cluster import combat_sim
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ


def _count_at_least(unit_type: UnitType, count: int) -> bool:
    return len(units.get_enemy_unit_timings(unit_type)) >= count


def _created_before_frame(unit_type: UnitType, frame: int, count: int = 1) -> bool:
    timings = units.get_enemy_unit_timings(unit_type)
    if len(timings) < count:
        return False

    return timings[count - 1][0] < frame


def _created_before_unit(first: UnitType, first_count: int, second: UnitType, second_count: int) -> bool:
    first_timings = units.get_enemy_unit_timings(first)
    if len(first_timings) < first_count:
        return False

    second_timings = units.get_enemy_unit_timings(second)
    return (len(second_timings) < second_count
            or first_timings[first_count - 1][0] <= second_timings[second_count - 1][0])


def _is_worker_rush() -> bool:
    if common.current_frame >= 6000:
        return False

    bwem_map = bwem.Instance()
    main_areas = game_map.get_my_main_areas()
    workers = 0
    for unit in units.all_enemy():
        if not unit.last_position_valid:
            continue
        if unit.type.isBuilding():
            continue

        if bwem_map.GetArea(WalkPosition(unit.last_position)) not in main_areas:
            continue

        # If there is a normal combat unit in our main, it isn't a worker rush
        if unit_util.is_combat_unit(unit.type) and unit.type.canAttack():
            return False

        if unit.type.isWorker():
            workers += 1

    return workers > 2


def _is_zergling_rush() -> bool:
    if common.current_frame >= 6000:
        return False

    return (_created_before_frame(UnitTypes.Zerg_Spawning_Pool, 1500)
            or _created_before_frame(UnitTypes.Zerg_Zergling, 2500))


def _is_zergling_all_in() -> bool:
    frame = common.current_frame

    # Suspect ling all-in if the enemy gets gas without a lair
    if units.has_enemy_built(UnitTypes.Zerg_Extractor) and not units.has_enemy_built(UnitTypes.Zerg_Lair):
        gas_timings = units.get_enemy_unit_timings(UnitTypes.Zerg_Extractor)
        gas_completed = gas_timings[0][0] + unit_util.build_time(UnitTypes.Zerg_Extractor)

        # Get the earliest frame we scouted one of the enemy's known hatcheries
        earliest_frame = INT_MAX
        for hatch in units.all_enemy_of_type(UnitTypes.Zerg_Hatchery):
            last_seen = game_map.last_seen_tile(TilePosition(hatch.last_position))
            earliest_frame = min(earliest_frame, last_seen)

        # Expect lair to have been started less than 1000 frames after gas completion
        if (earliest_frame - gas_completed) > 1000:
            return True

    # Expect a ling all-in if the enemy builds two in-base hatches on a low worker count
    if (frame < 5000
            and units.count_enemy(UnitTypes.Zerg_Spawning_Pool) > 0
            and units.count_enemy(UnitTypes.Zerg_Hatchery) > 1
            and units.count_enemy(UnitTypes.Zerg_Drone) < 15):
        enemy_natural = game_map.get_enemy_starting_natural()
        if enemy_natural is not None and enemy_natural.owner is None:
            return True

    if frame < 6000:
        return (_created_before_frame(UnitTypes.Zerg_Zergling, 4000, 9)
                or _created_before_frame(UnitTypes.Zerg_Zergling, 5000, 13))

    if frame < 8000:
        return (_created_before_frame(UnitTypes.Zerg_Zergling, 7000, 20)
                and units.count_enemy(UnitTypes.Zerg_Zergling) > 10)

    return (_created_before_frame(UnitTypes.Zerg_Zergling, 9000, 30)
            and units.count_enemy(UnitTypes.Zerg_Zergling) > 10)


def _is_hydra_bust() -> bool:
    frame = common.current_frame

    # Not a Hydra bust if there are more than 6 lings or no early hydra den
    if (_created_before_frame(UnitTypes.Zerg_Zergling, 7000, 7)
            or not _created_before_frame(UnitTypes.Zerg_Hydralisk_Den, 7000, 1)):
        return False

    # Early logic: determined purely on existance of early Hydra den
    if frame < 8000:
        return True

    # Middle logic: ling and hydra counts
    if frame < 10000:
        return (not _created_before_frame(UnitTypes.Zerg_Zergling, 10000, 10)
                and _created_before_frame(UnitTypes.Zerg_Hydralisk, 10000, 4))

    # Late logic: current hydra counts
    return units.count_enemy(UnitTypes.Zerg_Hydralisk) > 10


def _is_turtle() -> bool:
    return (_created_before_frame(UnitTypes.Zerg_Creep_Colony, 5000, 2)
            or _created_before_frame(UnitTypes.Zerg_Creep_Colony, 6000, 3)
            or _created_before_frame(UnitTypes.Zerg_Creep_Colony, 7000, 4)
            or _created_before_frame(UnitTypes.Zerg_Creep_Colony, 8000, 5))


def _is_sunken_contain(currently_sunken_contain: bool = False) -> bool:
    if not currently_sunken_contain and common.current_frame > 8000:
        return False
    if game_map.map_specific_override().has_backdoor_natural():
        return False

    # Sunken contain if the enemy owns our natural and has us outnumbered
    natural = game_map.get_my_natural()
    if natural is None:
        return False
    if natural.owner != bwapi.Broodwar.enemy():
        return False

    # Gather enemy combat value at the base
    enemy_value = 0
    for unit in units.enemy_at_base(natural):
        unit_type = unit.type
        complete = unit.completed

        if unit.type == UnitTypes.Zerg_Creep_Colony:
            unit_type = UnitTypes.Zerg_Sunken_Colony
            complete = False

        if not unit_util.is_combat_unit(unit_type):
            continue

        if complete:
            enemy_value += combat_sim.unit_value(unit_type)
        else:
            enemy_value += combat_sim.unit_value(unit_type) // 2

    # Gather our combat value
    our_value = 0
    for my_unit in units.all_mine():
        if not my_unit.completed:
            continue
        if not unit_util.is_combat_unit(my_unit.type):
            continue
        if my_unit.type == UnitTypes.Protoss_Photon_Cannon:
            dist = my_unit.get_distance(natural.get_position())
            if dist > 200:
                continue

        our_value += combat_sim.unit_value(my_unit)

    return enemy_value > our_value


_LAIR_TECH_TYPES = (
    UnitTypes.Zerg_Lair, UnitTypes.Zerg_Lurker_Egg, UnitTypes.Zerg_Lurker, UnitTypes.Zerg_Spire,
    UnitTypes.Zerg_Mutalisk, UnitTypes.Zerg_Devourer, UnitTypes.Zerg_Guardian, UnitTypes.Zerg_Ultralisk_Cavern,
    UnitTypes.Zerg_Ultralisk, UnitTypes.Zerg_Defiler_Mound, UnitTypes.Zerg_Defiler,
)


def _has_lair_tech() -> bool:
    return any(_count_at_least(unit_type, 1) for unit_type in _LAIR_TECH_TYPES)


def _is_muta_rush() -> bool:
    frame = common.current_frame

    # Assume early lair means mutas
    if frame < 8000 and _created_before_frame(UnitTypes.Zerg_Lair, 6000, 1):
        return True

    # Early spire or mutas
    return frame < 12000 and (_created_before_frame(UnitTypes.Zerg_Spire, 8000, 1)
                              or _created_before_frame(UnitTypes.Zerg_Mutalisk, 10000, 4))


def recognize_enemy_strategy(engine: PvZ) -> PvZ.ZergStrategy:
    import stardust.strategist.strategist as strategist
    from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ

    S = PvZ.ZergStrategy
    frame = common.current_frame
    strategy = engine.enemy_strategy
    for _ in range(10):
        # Each case either returns, moves to another strategy (continue) or keeps the current one (falls out)
        if strategy == S.Unknown:
            if _is_worker_rush():
                return S.WorkerRush
            if _is_sunken_contain():
                return S.SunkenContain
            if _is_zergling_rush():
                return S.ZerglingRush

            # Default to something reasonable if our scouting completely fails
            if frame > 4000:
                strategy = S.PoolBeforeHatchery
                continue

            # For pool or hatch first determination, wait until we have scouted the area around the base
            if strategist.get_worker_scout_status() == strategist.WorkerScoutStatus.EnemyBaseScouted:
                # Transition to pool-first or hatchery-first when we have the appropriate scouting information
                if _created_before_unit(UnitTypes.Zerg_Spawning_Pool, 1, UnitTypes.Zerg_Hatchery, 2):
                    strategy = S.PoolBeforeHatchery
                    continue
                if _created_before_unit(UnitTypes.Zerg_Hatchery, 2, UnitTypes.Zerg_Spawning_Pool, 1):
                    strategy = S.HatcheryBeforePool
                    continue
        elif strategy == S.WorkerRush:
            if not _is_worker_rush():
                strategy = S.Unknown
                continue
        elif strategy == S.ZerglingRush:
            if _is_worker_rush():
                return S.WorkerRush
            if _is_sunken_contain():
                return S.SunkenContain

            # Consider the rush to be over after 6000 frames
            # From there the PoolBeforeHatchery handler will potentially transition into ZerglingAllIn
            if frame >= 6000:
                strategy = S.PoolBeforeHatchery
                continue
        elif strategy == S.PoolBeforeHatchery:
            if _is_worker_rush():
                return S.WorkerRush
            if _is_sunken_contain():
                return S.SunkenContain

            # We might detect a rush late on large maps or if scouting is denied
            if _is_zergling_rush():
                return S.ZerglingRush

            if _is_zergling_all_in():
                return S.ZerglingAllIn

            if _is_hydra_bust():
                return S.HydraBust

            if _is_turtle():
                strategy = S.Turtle
                continue

            if _has_lair_tech():
                strategy = S.Lair
                continue
        elif strategy == S.HatcheryBeforePool:
            if _is_worker_rush():
                return S.WorkerRush
            if _is_sunken_contain():
                return S.SunkenContain
            if _is_zergling_all_in():
                return S.ZerglingAllIn
            if _is_hydra_bust():
                return S.HydraBust

            if _is_turtle():
                strategy = S.Turtle
                continue

            if _has_lair_tech():
                strategy = S.Lair
                continue
        elif strategy == S.ZerglingAllIn:
            if _is_sunken_contain():
                return S.SunkenContain

            if not _is_zergling_all_in():
                strategy = S.PoolBeforeHatchery
                continue

            if _has_lair_tech():
                strategy = S.Lair
                continue
        elif strategy == S.HydraBust:
            if _is_zergling_all_in():
                return S.ZerglingAllIn

            if not _is_hydra_bust():
                strategy = S.PoolBeforeHatchery
                continue
        elif strategy == S.Turtle:
            if _is_sunken_contain():
                return S.SunkenContain
            if _is_hydra_bust():
                return S.HydraBust

            if _has_lair_tech():
                strategy = S.Lair
                continue
        elif strategy == S.SunkenContain:
            if not _is_sunken_contain(True):
                strategy = S.PoolBeforeHatchery
                continue
        elif strategy == S.Lair:
            if _is_muta_rush():
                return S.MutaRush
        elif strategy == S.MutaRush:
            if not _is_muta_rush():
                return S.Lair

        return strategy

    log.get(f"ERROR: Loop in strategy recognizer, ended on {strategy.value}")
    return strategy

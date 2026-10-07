"""Port of Strategist/StrategyEngines/PvT/PvTEnemyStrategyRecognizer.cpp: PvT::recognizeEnemyStrategy."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import UnitType, UnitTypes, WalkPosition
from stardust import common
from stardust.cpp import fdiv
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.strategist.strategy_engines.pv_t.pv_t import PvT


def _count_at_least(unit_type: UnitType, count: int) -> bool:
    return len(units.get_enemy_unit_timings(unit_type)) >= count


def _none_created(unit_type: UnitType) -> bool:
    return not units.get_enemy_unit_timings(unit_type)


def _created_before_frame(unit_type: UnitType, frame: int, count: int = 1) -> bool:
    timings = units.get_enemy_unit_timings(unit_type)
    if len(timings) < count:
        return False

    return timings[count - 1][0] < frame


def _is_fast_expansion() -> bool:
    return _created_before_frame(UnitTypes.Terran_Command_Center, 7000, 2)


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


def _is_bunker_contain(currently_bunker_contain: bool = False) -> bool:
    if not currently_bunker_contain and common.current_frame > 8000:
        return False
    if game_map.map_specific_override().has_backdoor_natural():
        return False

    # Enemy must own our natural
    natural = game_map.get_my_natural()
    if natural is None:
        return False
    if natural.owner != bwapi.Broodwar.enemy():
        return False

    # Enemy must have a bunker
    return bool(units.all_enemy_of_type(UnitTypes.Terran_Bunker))


def _is_marine_rush() -> bool:
    frame = common.current_frame

    # In the early game, we consider the enemy to be doing a marine rush or all-in if we either see a lot of marines
    # or see two barracks with no factory or gas
    if frame < 6000:
        # Early rushes
        if (_created_before_frame(UnitTypes.Terran_Marine, 3500, 2)
                or _created_before_frame(UnitTypes.Terran_Barracks, 2300, 2)):
            return True

        # Later all-ins
        return (_created_before_frame(UnitTypes.Terran_Marine, 5000, 4)
                or (_none_created(UnitTypes.Terran_Refinery)
                    and _none_created(UnitTypes.Terran_Factory)
                    and _count_at_least(UnitTypes.Terran_Barracks, 2)))

    # Later on, we consider it to be a marine all-in purely based on the counts
    if frame < 8000:
        return (_created_before_frame(UnitTypes.Terran_Marine, 7000, 10)
                and units.count_enemy(UnitTypes.Terran_Marine) > 4
                and _none_created(UnitTypes.Terran_Siege_Tank_Tank_Mode)
                and _none_created(UnitTypes.Terran_Siege_Tank_Siege_Mode))

    return (_created_before_frame(UnitTypes.Terran_Marine, 9000, 16)
            and units.count_enemy(UnitTypes.Terran_Marine) > 4
            and _none_created(UnitTypes.Terran_Siege_Tank_Tank_Mode)
            and _none_created(UnitTypes.Terran_Siege_Tank_Siege_Mode))


def _is_proxy() -> bool:
    import stardust.strategist.strategist as strategist

    frame = common.current_frame
    if frame >= 6000:
        return False
    if units.count_enemy(UnitTypes.Terran_Refinery) > 0:
        return False

    # Otherwise check if we have directly scouted an enemy building in a proxy location
    enemy_main = game_map.get_enemy_starting_main()
    enemy_natural = game_map.get_enemy_starting_natural()
    for enemy_unit in units.all_enemy():
        # If we see an enemy marine in their main base, this indicates it not being a proxy
        if (enemy_unit.type == UnitTypes.Terran_Marine and enemy_main is not None
                and enemy_unit.get_distance(enemy_main.get_position()) < 600):
            return False

        if not enemy_unit.type.isBuilding():
            continue
        if enemy_unit.type.isRefinery():
            continue  # Don't count gas steals
        if enemy_unit.type.isResourceDepot():
            continue  # Don't count expansions

        # If the enemy main is unknown, this means the Map didn't consider this building to be part of that base and
        # it indicates a proxy
        if enemy_main is None:
            return True

        # Otherwise consider this a proxy if the building is not close to the enemy main or natural
        dist = path_finding.get_ground_distance(enemy_unit.last_position, enemy_main.get_position(),
                                                UnitTypes.Protoss_Probe, PathFindingOptions.UseNearestBWEMArea)
        if dist != -1 and dist < 1500:
            continue

        if enemy_natural is not None:
            natural_dist = path_finding.get_ground_distance(enemy_unit.last_position, enemy_natural.get_position(),
                                                            UnitTypes.Protoss_Probe,
                                                            PathFindingOptions.UseNearestBWEMArea)
            if natural_dist != -1 and natural_dist < 640:
                continue

        return True

    # If the enemy main has been scouted, determine if there is a proxy by looking at what they have built
    if strategist.has_worker_scout_completed_initial_base_scan():
        # Expect first barracks, refinery or command center by frame 2400
        if (frame > 2400
                and not _count_at_least(UnitTypes.Terran_Barracks, 1)
                and not _count_at_least(UnitTypes.Terran_Refinery, 1)
                and not _count_at_least(UnitTypes.Terran_Command_Center, 2)):
            return True

        return False

    return False


def _is_wall_in() -> bool:
    import stardust.strategist.strategist as strategist

    # The enemy is considered walled-in if our scout was unable to get into the base and there is no path from our
    # main to their base

    # TODO: This is too strict - the scout might get into the base before the wall-in is finished

    if strategist.get_worker_scout_status() != strategist.WorkerScoutStatus.ScoutingBlocked:
        return False
    if common.current_frame > 6000:
        return False

    enemy_main = game_map.get_enemy_starting_main()
    main_choke = game_map.get_my_main_choke()
    if enemy_main is None or main_choke is None:
        return False

    grid = path_finding.get_navigation_grid(enemy_main.get_position())
    if grid is None:
        return False

    return grid.node(main_choke.center).next_node is None


def _is_mid_game() -> bool:
    frame = common.current_frame

    # Never in the mid-game before frame 8000
    if frame < 8000:
        return False

    # We consider ourselves to be in the mid-game if the enemy has expanded

    # Scouted expansion
    if _count_at_least(UnitTypes.Terran_Command_Center, 2):
        return True

    # Inferred taken natural, usually by seeing a bunker
    enemy_natural = game_map.get_enemy_starting_natural()
    if enemy_natural is not None and enemy_natural.owner == bwapi.Broodwar.enemy():
        return True

    # We consider ourselves to be in the mid-game if the enemy has siege tech and a number of siege tanks
    return (units.has_enemy_built(UnitTypes.Terran_Siege_Tank_Siege_Mode)
            and (units.count_enemy(UnitTypes.Terran_Siege_Tank_Siege_Mode)
                 + units.count_enemy(UnitTypes.Terran_Siege_Tank_Tank_Mode)) > (2 if frame > 10000 else 4))


def _mid_game_strategy() -> PvT.TerranStrategy:
    from stardust.strategist.strategy_engines.pv_t.pv_t import PvT

    # Count the mech and bio units
    # Tanks are weighted twice as heavily as other mech
    # Medics are weighted three times as heavily as other bio
    mech = (units.count_enemy(UnitTypes.Terran_Siege_Tank_Siege_Mode) * 2
            + units.count_enemy(UnitTypes.Terran_Siege_Tank_Tank_Mode) * 2
            + units.count_enemy(UnitTypes.Terran_Vulture)
            + units.count_enemy(UnitTypes.Terran_Goliath))
    bio = (units.count_enemy(UnitTypes.Terran_Marine)
           + units.count_enemy(UnitTypes.Terran_Medic) * 3
           + units.count_enemy(UnitTypes.Terran_Firebat))

    if mech == 0 and bio == 0:
        return PvT.TerranStrategy.MidGameMech

    # Decide on the enemy strategy by the ratio of mech to bio
    ratio = fdiv(mech, mech + bio)
    if ratio < 0.333:
        return PvT.TerranStrategy.MidGameBio
    if ratio > 0.666:
        return PvT.TerranStrategy.MidGameMech
    return PvT.TerranStrategy.MidGameBioMech


def recognize_enemy_strategy(engine: PvT) -> PvT.TerranStrategy:
    import stardust.strategist.strategist as strategist
    from stardust.strategist.strategy_engines.pv_t.pv_t import PvT

    S = PvT.TerranStrategy
    frame = common.current_frame
    strategy = engine.enemy_strategy
    for _ in range(10):
        # Each case either returns, moves to another strategy (continue) or keeps the current one (falls out)
        if strategy == S.Unknown:
            if _is_worker_rush():
                return S.WorkerRush
            if _is_bunker_contain():
                return S.BunkerContain
            if _is_marine_rush():
                return S.MarineRush
            if _is_proxy():
                return S.ProxyRush
            if _is_wall_in():
                return S.WallIn
            if _is_fast_expansion():
                return S.FastExpansion

            # If the enemy blocks our scouting, set their strategy accordingly
            if strategist.get_worker_scout_status() == strategist.WorkerScoutStatus.ScoutingBlocked:
                strategy = S.BlockScouting
                continue

            # Two factory
            if _count_at_least(UnitTypes.Terran_Factory, 2):
                strategy = S.TwoFactory
                continue

            # Default to something reasonable if we don't detect anything else
            if frame > 4000 or strategist.has_worker_scout_completed_initial_base_scan():
                strategy = S.NormalOpening
                continue
        elif strategy == S.WorkerRush:
            if not _is_worker_rush():
                strategy = S.Unknown
                continue
        elif strategy == S.BunkerContain:
            if not _is_bunker_contain(True):
                strategy = S.Unknown
                continue
        elif strategy == S.ProxyRush:
            if _is_worker_rush():
                return S.WorkerRush
            if _is_bunker_contain():
                return S.BunkerContain

            # Handle a misdetected proxy, can happen if the enemy does a fast expand or builds further away from their
            # depot
            if frame < 6000 and not _is_proxy():
                strategy = S.Unknown
                continue

            # We assume the enemy has transitioned from the proxy when either:
            # - They have taken gas
            # - Our scout is dead and we are past frame 5000
            if (units.count_enemy(UnitTypes.Terran_Refinery) > 0
                    or (frame >= 6000 and strategist.is_worker_scout_complete())):
                strategy = S.MarinePressure
                continue

            # Also bail out of thinking it is a proxy rush if we have more dragoons than the enemy has marines
            if (frame >= 6000
                    and units.count_enemy(UnitTypes.Terran_Marine) < units.count_completed(UnitTypes.Protoss_Dragoon)):
                strategy = S.MarinePressure
                continue
        elif strategy == S.MarineRush:
            if _is_worker_rush():
                return S.WorkerRush
            if _is_bunker_contain():
                return S.BunkerContain

            if not _is_marine_rush():
                strategy = S.MarinePressure
                continue
        elif strategy == S.MarinePressure:
            if _is_bunker_contain():
                return S.BunkerContain

            if _is_marine_rush():
                strategy = S.MarineRush
                continue

            # Transition to midgame when appropriate
            if _is_mid_game():
                strategy = _mid_game_strategy()
        elif strategy == S.WallIn:
            if _is_worker_rush():
                return S.WorkerRush
            if _is_bunker_contain():
                return S.BunkerContain
            if _is_marine_rush():
                return S.MarineRush
            if _is_fast_expansion():
                return S.FastExpansion

            if _is_mid_game():
                strategy = _mid_game_strategy()
        elif strategy == S.BlockScouting:
            if _is_worker_rush():
                return S.WorkerRush
            if _is_bunker_contain():
                return S.BunkerContain
            if _is_proxy():
                return S.ProxyRush
            if _is_marine_rush():
                return S.MarineRush
            if _is_fast_expansion():
                return S.FastExpansion

            # If we haven't had any evidence of a rush for about 3 1/2 minutes, assume the enemy is opening normally
            if frame > 5000:
                strategy = S.NormalOpening
                continue

            if _is_mid_game():
                strategy = _mid_game_strategy()
        elif strategy in (S.TwoFactory, S.FastExpansion, S.NormalOpening):
            if _is_worker_rush():
                return S.WorkerRush
            if _is_bunker_contain():
                return S.BunkerContain
            if _is_proxy():
                return S.ProxyRush
            if _is_marine_rush():
                return S.MarineRush

            if _is_mid_game():
                strategy = _mid_game_strategy()
        elif strategy in (S.MidGameMech, S.MidGameBio, S.MidGameBioMech):
            strategy = _mid_game_strategy()

        return strategy

    log.get(f"ERROR: Loop in strategy recognizer, ended on {strategy.value}")
    return strategy

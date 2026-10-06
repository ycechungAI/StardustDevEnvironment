"""Port of Strategist/StrategyEngines/PvP/PvPEnemyStrategyRecognizer.cpp: PvP::recognizeEnemyStrategy."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwem
from bwapi import UnitType, UnitTypes, WalkPosition
from stardust import common
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.strategist import opponent_economic_model
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.strategist.strategy_engines.pv_p.pv_p import PvP


def _count_at_least(unit_type: UnitType, count: int) -> bool:
    return len(units.get_enemy_unit_timings(unit_type)) >= count


def _none_created(unit_type: UnitType) -> bool:
    return not units.get_enemy_unit_timings(unit_type)


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


def _just_discovered(unit_type: UnitType) -> bool:
    return any(timing[1] == common.current_frame for timing in units.get_enemy_unit_timings(unit_type))


def _is_fast_expansion() -> bool:
    return _created_before_frame(UnitTypes.Protoss_Nexus, 6000, 2)


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


def _is_zealot_rush() -> bool:
    if common.current_frame >= 6000:
        return False

    # We expect a zealot rush if we see an early zealot, early second zealot or early second gateway
    return (_created_before_frame(UnitTypes.Protoss_Zealot, 2700)
            or _created_before_frame(UnitTypes.Protoss_Zealot, 3300, 2)
            or _created_before_frame(UnitTypes.Protoss_Gateway, 2300, 2))


def _is_proxy() -> bool:
    import stardust.strategist.strategist as strategist

    frame = common.current_frame
    if _is_fast_expansion():
        return False
    if frame >= 5000:
        return False
    if units.count_enemy(UnitTypes.Protoss_Assimilator) > 0:
        return False

    # Check if we have directly scouted an enemy building in a proxy location
    enemy_main = game_map.get_enemy_starting_main()
    enemy_natural = game_map.get_enemy_starting_natural()
    for enemy_unit in units.all_enemy():
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
        # Expect first pylon by frame 1300
        if frame > 1300 and not _count_at_least(UnitTypes.Protoss_Pylon, 1):
            return True

        # Expect first gateway or forge by frame 2200
        # This will sometimes fail if the enemy does a fast expansion we don't see
        if (frame > 2200
                and not _count_at_least(UnitTypes.Protoss_Gateway, 1)
                and not _count_at_least(UnitTypes.Protoss_Forge, 1)):
            return True

        # If the enemy hasn't built a forge, expect a second pylon by frame 4000 if we still have a live scout
        if (frame > 4000
                and strategist.has_worker_scout_completed_initial_base_scan()
                and not strategist.is_worker_scout_complete()
                and not _count_at_least(UnitTypes.Protoss_Forge, 1)
                and not _count_at_least(UnitTypes.Protoss_Pylon, 2)):
            return True

        return False

    return False


def _is_zealot_all_in() -> bool:
    frame = common.current_frame

    # In the early game, we consider the enemy to be doing a zealot all-in if we either see a lot of zealots or see two
    # gateways with no core or gas
    if frame < 6000:
        return (_created_before_frame(UnitTypes.Protoss_Zealot, 5000, 4)
                or (_none_created(UnitTypes.Protoss_Assimilator)
                    and _none_created(UnitTypes.Protoss_Cybernetics_Core)
                    and _count_at_least(UnitTypes.Protoss_Gateway, 2)))

    # Later on, we consider it to be a zealot all-in purely based on the counts
    if frame < 8000:
        return (_created_before_frame(UnitTypes.Protoss_Zealot, 7000, 8)
                and units.count_enemy(UnitTypes.Protoss_Zealot) > 4)

    return (_created_before_frame(UnitTypes.Protoss_Zealot, 9000, 12)
            and units.count_enemy(UnitTypes.Protoss_Zealot) > 4)


def _is_dragoon_all_in() -> bool:
    # It's unlikely our worker scout survives long enough, but detect this if the enemy gets four gates before robo /
    # citadel
    if (common.current_frame < 10000
            and _created_before_unit(UnitTypes.Protoss_Gateway, 4, UnitTypes.Protoss_Robotics_Facility, 1)
            and _created_before_unit(UnitTypes.Protoss_Gateway, 4, UnitTypes.Protoss_Templar_Archives, 1)):
        return True

    # Check for four gates and no expansion from economic model
    if (opponent_economic_model.enabled()
            and not _created_before_frame(UnitTypes.Protoss_Nexus, common.current_frame, 2)
            and opponent_economic_model.minimum_producer_count(UnitTypes.Protoss_Gateway) >= 4
            and not opponent_economic_model.has_built(UnitTypes.Protoss_Robotics_Facility)
            and not opponent_economic_model.has_built(UnitTypes.Protoss_Templar_Archives)):
        return True

    return False


def _is_early_robo() -> bool:
    return _created_before_frame(UnitTypes.Protoss_Robotics_Facility, 7500)


def _is_dark_templar_rush() -> bool:
    return (_created_before_frame(UnitTypes.Protoss_Templar_Archives, 10000)
            or _created_before_frame(UnitTypes.Protoss_Dark_Templar, 12000))


def _is_turtle() -> bool:
    return (_created_before_frame(UnitTypes.Protoss_Photon_Cannon, 5000, 2)
            or _created_before_frame(UnitTypes.Protoss_Photon_Cannon, 6000, 3)
            or _created_before_frame(UnitTypes.Protoss_Photon_Cannon, 7000, 4)
            or _created_before_frame(UnitTypes.Protoss_Photon_Cannon, 8000, 5))


_MID_GAME_TYPES = (
    UnitTypes.Protoss_Nexus,  # (needs two)
    UnitTypes.Protoss_Templar_Archives, UnitTypes.Protoss_Dark_Templar, UnitTypes.Protoss_High_Templar,
    UnitTypes.Protoss_Dark_Archon, UnitTypes.Protoss_Archon, UnitTypes.Protoss_Robotics_Facility,
    UnitTypes.Protoss_Robotics_Support_Bay, UnitTypes.Protoss_Observatory, UnitTypes.Protoss_Shuttle,
    UnitTypes.Protoss_Reaver, UnitTypes.Protoss_Observer, UnitTypes.Protoss_Stargate, UnitTypes.Protoss_Fleet_Beacon,
    UnitTypes.Protoss_Arbiter_Tribunal, UnitTypes.Protoss_Scout, UnitTypes.Protoss_Corsair, UnitTypes.Protoss_Carrier,
    UnitTypes.Protoss_Arbiter,
)


def _is_mid_game() -> bool:
    # We consider ourselves to be in the mid-game after frame 14000 if the enemy has taken their natural or teched to
    # something beyond goons
    if common.current_frame < 14000:
        return False

    return any(_count_at_least(unit_type, 2 if unit_type == UnitTypes.Protoss_Nexus else 1)
               for unit_type in _MID_GAME_TYPES)


def recognize_enemy_strategy(engine: PvP) -> PvP.ProtossStrategy:
    import stardust.strategist.strategist as strategist
    from stardust.strategist.strategy_engines.pv_p.pv_p import PvP

    S = PvP.ProtossStrategy
    frame = common.current_frame
    strategy = engine.enemy_strategy
    for _ in range(10):
        # Each case either returns, moves to another strategy (continue) or keeps the current one (break)
        if strategy == S.Unknown:
            if _is_worker_rush():
                return S.WorkerRush
            if _is_zealot_rush():
                return S.ZealotRush
            if _is_proxy():
                return S.ProxyRush

            # If the enemy blocks our scouting, set their strategy accordingly
            if strategist.get_worker_scout_status() == strategist.WorkerScoutStatus.ScoutingBlocked:
                strategy = S.BlockScouting
                continue

            if _is_fast_expansion():
                return S.FastExpansion
            if _is_early_robo():
                return S.EarlyRobo

            # Early forge if we have seen either a forge or a cannon before transitioning to something else
            if _count_at_least(UnitTypes.Protoss_Forge, 1) or _count_at_least(UnitTypes.Protoss_Photon_Cannon, 1):
                strategy = S.EarlyForge
                continue

            # Transition to one- or two-gate when we have the appropriate scouting information
            if (_created_before_unit(UnitTypes.Protoss_Gateway, 2, UnitTypes.Protoss_Cybernetics_Core, 1)
                    and _created_before_unit(UnitTypes.Protoss_Gateway, 2, UnitTypes.Protoss_Assimilator, 1)):
                strategy = S.TwoGate
                continue

            # Check economic model if we scout the core first
            # It might think one-zealot until the assimilator is scouted, but that is corrected later
            if _created_before_unit(UnitTypes.Protoss_Cybernetics_Core, 1, UnitTypes.Protoss_Gateway, 2):
                if (opponent_economic_model.enabled()
                        and opponent_economic_model.worst_case_unit_count(UnitTypes.Protoss_Zealot)[1] == 0):
                    strategy = S.NoZealotCore
                else:
                    strategy = S.OneZealotCore
                continue

            # Assume one-zealot if we scout the assimilator first
            # Will be reconsidered when the core is scouted
            if _created_before_unit(UnitTypes.Protoss_Assimilator, 1, UnitTypes.Protoss_Gateway, 2):
                strategy = S.OneZealotCore
                continue

            # Default to something reasonable if our scouting completely fails
            if frame > 4000:
                strategy = S.OneZealotCore
                continue
        elif strategy == S.WorkerRush:
            if not _is_worker_rush():
                strategy = S.Unknown
                continue
        elif strategy == S.ProxyRush:
            if _is_worker_rush():
                return S.WorkerRush

            # Handle a misdetected proxy, can happen if the enemy does a fast expand or builds further away from their
            # nexus
            if frame < 5000 and not _is_proxy():
                strategy = S.Unknown
                continue

            # We assume the enemy has transitioned from the proxy when either:
            # - They have taken gas
            # - Our scout is dead and we are past frame 5000
            if (units.count_enemy(UnitTypes.Protoss_Assimilator) > 0
                    or (frame >= 5000 and strategist.is_worker_scout_complete())):
                strategy = S.TwoGate
                continue
        elif strategy == S.ZealotRush:
            if _is_worker_rush():
                return S.WorkerRush

            # Consider the rush to be over after 6000 frames
            # From there the TwoGate handler will potentially transition into ZealotAllIn
            if frame >= 6000:
                strategy = S.TwoGate
                continue
        elif strategy == S.EarlyForge:
            # The expected transition from here is into turtle or fast expansion, but detect others if the forge was a
            # fake-out
            if _is_worker_rush():
                return S.WorkerRush
            if _is_zealot_all_in():
                return S.ZealotAllIn
            if _is_dragoon_all_in():
                return S.DragoonAllIn
            if _is_proxy():
                return S.ProxyRush

            if _is_fast_expansion():
                strategy = S.FastExpansion
                continue

            if _is_early_robo():
                strategy = S.EarlyRobo
                continue

            if _is_turtle():
                strategy = S.Turtle
                continue

            if _is_mid_game():
                strategy = S.MidGame
                continue
        elif strategy == S.TwoGate:
            if _is_worker_rush():
                return S.WorkerRush

            # We might detect a zealot rush late on large maps or if scouting is denied
            if _is_zealot_rush():
                return S.ZealotRush

            if _is_dark_templar_rush():
                return S.DarkTemplarRush
            if _is_zealot_all_in():
                return S.ZealotAllIn
            if _is_dragoon_all_in():
                return S.DragoonAllIn

            if _is_early_robo():
                strategy = S.EarlyRobo
                continue

            if _is_turtle():
                strategy = S.Turtle
                continue

            if _is_mid_game():
                strategy = S.MidGame
                continue
        elif strategy in (S.NoZealotCore, S.OneZealotCore, S.FastExpansion):
            if _is_dark_templar_rush():
                return S.DarkTemplarRush
            if _is_zealot_all_in():
                return S.ZealotAllIn
            if _is_dragoon_all_in():
                return S.DragoonAllIn

            if _is_early_robo():
                strategy = S.EarlyRobo
                continue

            if _is_turtle():
                strategy = S.Turtle
                continue

            if _is_mid_game():
                strategy = S.MidGame
                continue

            # Reconsider detected one-zealot core if we have just discovered an enemy core or assimilator
            if engine.enemy_strategy == S.OneZealotCore and (
                    _just_discovered(UnitTypes.Protoss_Cybernetics_Core)
                    or _just_discovered(UnitTypes.Protoss_Assimilator)):
                if (opponent_economic_model.enabled()
                        and opponent_economic_model.worst_case_unit_count(UnitTypes.Protoss_Zealot)[1] == 0):
                    strategy = S.NoZealotCore
        elif strategy == S.BlockScouting:
            # An enemy that blocks our scouting could be doing anything, but we suspect some kind of rush or all-in
            if _is_worker_rush():
                return S.WorkerRush
            if _is_proxy():
                return S.ProxyRush  # If we see a proxy building somewhere
            if _is_zealot_rush():
                return S.ZealotRush
            if _is_dark_templar_rush():
                return S.DarkTemplarRush
            if _is_zealot_all_in():
                return S.ZealotAllIn
            if _is_dragoon_all_in():
                return S.DragoonAllIn

            if _is_mid_game():
                strategy = S.MidGame
                continue
        elif strategy == S.ZealotAllIn:
            # Enemy might transition from early zealot pressure into dark templar
            if _is_dark_templar_rush():
                return S.DarkTemplarRush

            if not _is_zealot_all_in():
                strategy = S.TwoGate
                continue

            if _is_mid_game():
                strategy = S.MidGame
                continue
        elif strategy == S.DragoonAllIn:
            if _is_dark_templar_rush():
                return S.DarkTemplarRush

            if not _is_dragoon_all_in() and _is_early_robo():
                strategy = S.EarlyRobo
                continue

            if _is_mid_game():
                strategy = S.MidGame
                continue
        elif strategy == S.Turtle:
            if _is_zealot_all_in():
                return S.ZealotAllIn
            if _is_dragoon_all_in():
                return S.DragoonAllIn
            if _is_proxy():
                return S.ProxyRush
            if _is_dark_templar_rush():
                return S.DarkTemplarRush

            if _is_early_robo():
                strategy = S.EarlyRobo
                continue

            if _is_mid_game():
                strategy = S.MidGame
                continue
        elif strategy == S.EarlyRobo:
            if _is_zealot_all_in():
                return S.ZealotAllIn
            if _is_dragoon_all_in():
                return S.DragoonAllIn
            if _is_dark_templar_rush():
                return S.DarkTemplarRush

            if _is_mid_game():
                strategy = S.MidGame
                continue
        elif strategy == S.DarkTemplarRush:
            if not _is_dark_templar_rush():
                strategy = S.OneZealotCore
                continue

            if _is_mid_game():
                strategy = S.MidGame
                continue

        return strategy

    log.get(f"ERROR: Loop in strategy recognizer, ended on {strategy.value}")
    return strategy

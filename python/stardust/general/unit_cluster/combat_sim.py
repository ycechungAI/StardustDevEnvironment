"""Port of General/UnitCluster/CombatSim.cpp and the CombatSim namespace from General/General.h: simulates fights with
FAP to decide whether a cluster should attack or regroup.

The simulation loop and scoring run in the native fap module; this module selects and describes the units.
The per-frame unit log of DEBUG_COMBATSIM_CVIS needs the sim state after every simulated frame, which is slow in
Python, so it is recorded only when DEBUG_COMBATSIM_CVIS_UNIT_LOG is set. The CSV and drawing debug output is omitted.
"""

from __future__ import annotations

from collections import deque
from typing import TYPE_CHECKING

import bwapi
import fap
from bwapi import Position, Positions, TechTypes, UnitType, UnitTypes, WalkPosition
from stardust import common, config
from stardust.general.combat_sim_result import CombatSimResult
from stardust.instrumentation import cherryvis, log
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.players import players
from stardust.units.enemy_bunker import EnemyBunker
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster, UnitsAndTargets
    from stardust.map.choke import Choke
    from stardust.units.my_observer import MyObserver
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit

_LIMIT_MICROSECONDS = 5000

# Cache of unit scores, indexed by unit type id
_base_score: list[int] = []
_scaled_score: list[int] = []

_max_iterations = 288

_simulator: fap.CombatSimulator | None = None
_choke_geometries: dict[int, fap.ChokeGeometry] = {}


def initialize() -> None:
    global _max_iterations, _simulator

    game = bwapi.Broodwar
    _simulator = fap.CombatSimulator(game.mapWidth(), game.mapHeight())
    _choke_geometries.clear()

    type_count = max(unit_type.getID() for unit_type in UnitTypes.allUnitTypes()) + 1
    _base_score[:] = [0] * type_count
    _scaled_score[:] = [0] * type_count
    for unit_type in UnitTypes.allUnitTypes():
        # Base the score on the cost
        score = unit_util.mineral_cost(unit_type) + unit_util.gas_cost(unit_type) * 2

        # Add cost of built / loaded units
        if unit_type == UnitTypes.Terran_Bunker:
            score += 4 * unit_util.mineral_cost(UnitTypes.Terran_Marine)
        elif unit_type == UnitTypes.Protoss_Carrier:
            score += 8 * unit_util.mineral_cost(UnitTypes.Protoss_Interceptor)

        # Adjust units whose combat value differs from their cost
        score += _SCORE_ADJUSTMENTS.get(unit_type, 0)

        _base_score[unit_type.getID()] = score >> 2
        _scaled_score[unit_type.getID()] = score - _base_score[unit_type.getID()]

    fap.set_unit_scores(_base_score, _scaled_score)
    _max_iterations = 288


_SCORE_ADJUSTMENTS = {
    UnitTypes.Zerg_Sunken_Colony: 125,
    UnitTypes.Zerg_Spore_Colony: 100,
    UnitTypes.Zerg_Creep_Colony: 50,
    UnitTypes.Protoss_Photon_Cannon: 100,
    UnitTypes.Terran_Missile_Turret: 150,
    UnitTypes.Protoss_Shuttle: 200,
    UnitTypes.Terran_Dropship: 200,
    UnitTypes.Zerg_Zergling: 15,
    UnitTypes.Terran_Vulture: 50,
    UnitTypes.Terran_Siege_Tank_Siege_Mode: 100,
    UnitTypes.Terran_Vulture_Spider_Mine: 100,
}


def unit_value(unit: Unit | UnitType) -> int:
    """The combat value of a unit (scaled by its remaining health) or of a unit type at full health."""
    if isinstance(unit, UnitType):
        return _base_score[unit.getID()] + _scaled_score[unit.getID()]

    type_id = unit.type.getID()
    return _base_score[type_id] + (_scaled_score[type_id] * (unit.last_health * 3 + unit.last_shields)) // (
        unit.type.maxHitPoints() * 3 + unit.type.maxShields())


def set_max_iterations(iterations: int) -> None:
    """Used from tests for sim evaluation."""
    global _max_iterations
    _max_iterations = iterations


def _is_sim_unit(unit: Unit) -> bool:
    """Whether a unit goes into the sim."""
    if not unit.completed:
        return False
    if unit.immobile:
        return False
    if unit.health <= 0:
        return False  # Upcoming attacks indicate this unit is about to die, so it probably won't get a shot off

    if unit.type == UnitTypes.Protoss_Interceptor:
        return False
    if unit.type == UnitTypes.Terran_Medic:
        return True
    if unit.type == UnitTypes.Zerg_Overlord:
        return True

    if config.USE_BUNKER_ATTACKER_LOGIC and isinstance(unit, EnemyBunker):
        return unit.loaded_marines > 0

    return unit.ground_damage() > 0 or unit.air_damage() > 0


def _get_cooldown(unit: Unit) -> int:
    # In FAP, units deal damage immediately on the frame their cooldown reaches 0
    # In the game, however, there is always a delay between the unit going on cooldown and damage being dealt
    # We therefore need to make sure we don't feed cooldown data to FAP that is out of sync with the damage being
    # dealt, otherwise we will see instability between frames where units have gone on cooldown but haven't dealt
    # damage yet

    # Compute the remaining cooldown
    cooldown = max(0, unit.cooldown_until - common.current_frame)

    # For our own units, we add their pending damage as upcoming damage on their targets, so it is already captured in
    # the targets' health and we can just use their actual cooldown
    if unit.player == bwapi.Broodwar.self():
        return cooldown

    # For opponent units, check if they went on cooldown more recently than their attack deals damage
    frame_delay = unit_util.delay_from_cooldown_to_bullet_or_damage(unit.type)
    if common.current_frame < unit.last_seen_attacking + frame_delay:
        return 0
    return cooldown


def _add_unit(simulator: fap.CombatSimulator, player1: bool, unit: Unit, vanguard: MyUnit, mobile_detection: bool,
              assume_stim: bool, target_position: Position = Positions.Invalid, target: int = 0) -> None:
    if unit.is_flying:
        collision_value = 0
        collision_value_choke = 0
    else:
        # In open terrain, collision values scale based on the unit range
        # Rationale: melee units have a smaller area to maneuver in, so they interfere with each other more
        unit_range = unit.ground_range()
        if unit_range > 128:
            collision_value = 3
        elif unit_range > 32:
            collision_value = 4
        else:
            collision_value = 6

        # In a choke, collision values depend on unit size
        # We allow no collision between full-size units and a stack of two smaller-size units
        collision_value_choke = 12 if unit.type.width() >= 32 or unit.type.height() >= 32 else 6

    stimmed = unit.stimmed_until > common.current_frame
    if not stimmed and assume_stim and unit.type in (UnitTypes.Terran_Marine, UnitTypes.Terran_Firebat):
        stimmed = True

    attacker_count = 8
    if isinstance(unit, EnemyBunker):
        attacker_count = unit.loaded_marines if config.USE_BUNKER_ATTACKER_LOGIC else 4

    player = unit.player
    sim_position = unit.sim_position
    try:
        simulator.add_unit(
            player1=player1,
            unit_type=unit.type,
            position=sim_position,
            target_position=sim_position if target_position == Positions.Invalid else target_position,
            health=unit.health,
            shields=unit.shields,
            flying=unit.is_flying,
            # For this section, Stardust has modified FAP to take the upgraded values instead of the upgrade levels
            speed=players.unit_top_speed(player, unit.type),
            armor=players.unit_armor(player, unit.type),
            ground_cooldown=players.unit_ground_cooldown(player, unit.type),
            ground_damage=unit.ground_damage(),
            ground_max_range=unit.ground_range(),
            air_cooldown=players.unit_air_cooldown(player, unit.type),
            air_damage=unit.air_damage(),
            air_max_range=unit.air_range(),
            elevation=bwapi.Broodwar.getGroundHeight(sim_position.x >> 5, sim_position.y >> 5),
            # TODO: Figure out if we need to do something with initializing attack cooldown for bunkers
            attacker_count=attacker_count,
            attack_cooldown_remaining=_get_cooldown(unit),
            stimmed=stimmed,
            undetected=unit.is_cliffed_tank(vanguard) or (unit.undetected and not mobile_detection),
            id=unit.id,
            target=target,
            collision_value=collision_value,
            collision_value_choke=collision_value_choke,
        )
    except ValueError as error:
        # FAP indexes its grids by position without bounds checks; the binding refuses positions off the map
        log.get(f"ERROR: Could not add {unit} to combat sim: {error}")


def _choke_geometry(choke: Choke) -> fap.ChokeGeometry:
    geometry = _choke_geometries.get(id(choke))
    if geometry is None:
        geometry = _choke_geometries[id(choke)] = fap.ChokeGeometry(
            choke.tile_side, choke.end1_center, choke.end2_center, choke.end1_exit, choke.end2_exit)
    return geometry


def _execute(target_position: Position, cluster: UnitCluster, units_and_targets: UnitsAndTargets,
             targets: set[Unit], detectors: set[MyObserver], attacking: bool,
             narrow_choke: Choke | None = None) -> CombatSimResult:
    simulator = _simulator
    assert simulator is not None
    simulator.reset(_choke_geometry(narrow_choke) if narrow_choke is not None else None)

    all_tier_one = True

    # Add our units with initial target
    my_count = 0
    for my_unit, unit_target in units_and_targets:
        if not _is_sim_unit(my_unit):
            continue

        target = unit_target.id if unit_target is not None else 0
        _add_unit(simulator, attacking, my_unit, cluster.vanguard, False, False, target_position, target)

        my_count += 1
        if my_unit.type != UnitTypes.Protoss_Zealot:
            all_tier_one = False

    # Determine if we have mobile detection with this cluster
    have_mobile_detection = any(cluster.vanguard.get_distance(detector) < 480 for detector in detectors)

    # Add enemy units

    # If we know the enemy has researched stim, assume enemy marines and firebats will stim if there is a medic with
    # them
    assume_stim = (players.has_researched(bwapi.Broodwar.enemy(), TechTypes.Stim_Packs)
                   and any(unit.type == UnitTypes.Terran_Medic for unit in targets))

    enemy_count = 0
    enemy_has_undetected_units = False
    for unit in targets:
        if not _is_sim_unit(unit):
            continue
        if unit.undetected and not have_mobile_detection:
            enemy_has_undetected_units = True

        # Only include workers if they have been seen attacking recently
        # TODO: Handle worker rushes
        if not unit.type.isWorker() or (common.current_frame - unit.last_seen_attacking) < 120:
            _add_unit(simulator, not attacking, unit, cluster.vanguard, have_mobile_detection, assume_stim)

            enemy_count += 1

            if unit.type not in (UnitTypes.Protoss_Zealot, UnitTypes.Zerg_Zergling, UnitTypes.Terran_Marine):
                all_tier_one = False

    iterations = _max_iterations
    if all_tier_one and not attacking:
        iterations //= 4

    unit_log: dict[str, list[int]] = {}
    if config.DEBUG_COMBATSIM_CVIS_UNIT_LOG:
        def update_unit_log() -> None:
            for player_units in simulator.state():
                for unit_id, _, x, y, _, _, cooldown, unit_target in player_units:
                    unit_log.setdefault(str(unit_id), []).extend((x, y, unit_target, cooldown))

        update_unit_log()
        initial1 = initial2 = final1 = final2 = 0
        done = 0
        while True:
            scores = simulator.run(1, _LIMIT_MICROSECONDS)
            if done == 0:
                initial1, initial2 = scores[0], scores[1]
            final1, final2 = scores[2], scores[3]
            update_unit_log()
            done += 1
            if done >= iterations:
                break
        simulated = done
    else:
        initial1, initial2, final1, final2, simulated = simulator.run(iterations, _LIMIT_MICROSECONDS)

    if simulated < iterations < _max_iterations:
        cherryvis.log(f"Sim aborted after {simulated}iterations")

    initial_mine, initial_enemy = (initial1, initial2) if attacking else (initial2, initial1)
    final_mine, final_enemy = (final1, final2) if attacking else (final2, final1)

    if config.DEBUG_COMBATSIM_LOG:
        debug = str(WalkPosition(cluster.center))
        if not attacking and narrow_choke is not None:
            debug += f" (defend {WalkPosition(narrow_choke.center)})"
        elif narrow_choke is not None:
            debug += f" (through {WalkPosition(narrow_choke.center)})"
        cherryvis.log(f"{debug}: {initial_mine},{initial_enemy}-{final_mine},{final_enemy}")

    return CombatSimResult(my_count, enemy_count, initial_mine, initial_enemy, final_mine, final_enemy,
                           enemy_has_undetected_units, narrow_choke, unit_log)


def run_combat_sim(cluster: UnitCluster, target_position: Position, units_and_targets: UnitsAndTargets,
                   targets: set[Unit], detectors: set[MyObserver], attacking: bool = True,
                   choke: Choke | None = None) -> CombatSimResult:
    if not units_and_targets or not targets:
        return CombatSimResult()

    # Check if the armies are separated by a narrow choke
    # We consider this to be the case if our cluster center is on one side of the choke and most of their units are on
    # the other side
    narrow_choke = choke
    if narrow_choke is None:
        # First check for the next narrow choke between our center and the target position
        narrow_choke = path_finding.separating_narrow_choke(cluster.center, target_position,
                                                            UnitTypes.Protoss_Dragoon,
                                                            PathFindingOptions.UseNeighbouringBWEMArea)

        # Next validate that most of the enemy units are behind that choke
        if narrow_choke is not None:
            count = 0
            total = 0
            for unit in targets:
                if not _is_sim_unit(unit):
                    continue
                if not unit.sim_position_valid:
                    continue

                total += 1

                if narrow_choke is path_finding.separating_narrow_choke(cluster.center, unit.sim_position,
                                                                        UnitTypes.Protoss_Dragoon,
                                                                        PathFindingOptions.UseNeighbouringBWEMArea):
                    count += 1
            if count < total - count:
                narrow_choke = None

    return _execute(target_position, cluster, units_and_targets, targets, detectors, attacking, narrow_choke)


def run_recorded_combat_sim(cluster: UnitCluster, sim_results: deque[CombatSimResult], target_position: Position,
                            units_and_targets: UnitsAndTargets, targets: set[Unit], detectors: set[MyObserver],
                            attacking: bool = True, choke: Choke | None = None) -> CombatSimResult:
    sim_result = run_combat_sim(cluster, target_position, units_and_targets, targets, detectors, attacking, choke)

    # Reset recent sim results if it hasn't been run on the last frame
    if sim_results and sim_results[-1].frame != common.current_frame - 1:
        sim_results.clear()

    # Cap the list at 72 items
    if len(sim_results) == 72:
        sim_results.popleft()

    sim_results.append(sim_result)
    return sim_result


def consecutive_sim_results(sim_results: deque[CombatSimResult], limit: int) -> tuple[int, int, int]:
    """The number of consecutive frames the sim has agreed on its current value, and the total number of attack and
    regroup frames within the window."""
    attack = 0
    regroup = 0

    if not sim_results:
        return 0, attack, regroup

    first_result = sim_results[-1].decision
    is_consecutive = True
    consecutive = 0
    count = 0
    for sim_result in reversed(sim_results):
        if count >= limit:
            break

        if is_consecutive:
            if sim_result.decision == first_result:
                consecutive += 1
            else:
                is_consecutive = False

        if sim_result.decision:
            attack += 1
        else:
            regroup += 1

        count += 1

    return consecutive, attack, regroup

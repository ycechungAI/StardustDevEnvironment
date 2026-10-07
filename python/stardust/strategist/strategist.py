"""Port of Strategist/Strategist.{h,cpp}.

Broadly, the Strategist (via a StrategyEngine) decides on a prioritized list of plays to run, each of which can order
units from the producer and influence the organization and behaviour of its managed units. The StrategyEngine also
orders production for our main army.

The strategic state measurements are constant in Stardust (their computation is commented out), as here.
"""

from __future__ import annotations

from enum import Enum
from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Races, TilePosition, UnitType, WalkPosition
from stardust import common, config
from stardust.general.unit_cluster.unit_cluster import Activity, SubActivity
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.strategist import opponent_economic_model
from stardust.strategist.play import Play, ProductionGoals
from stardust.strategist.plays.main_army.attack_enemy_base import AttackEnemyBase
from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay
from stardust.strategist.plays.main_army.mop_up import MopUp
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.producer.production_goal import ProductionGoal
    from stardust.strategist.strategy_engine import StrategyEngine
    from stardust.units.my_unit import MyUnit

_CVIS_BOARD_VALUES = config.INSTRUMENTATION_ENABLED


class WorkerScoutStatus(Enum):
    Unstarted = 0  # (initial state) We haven't started scouting yet
    LookingForEnemyBase = 1  # Our scout is trying to find the enemy base
    MovingToEnemyBase = 2  # Our scout knows which base is the enemy base, but hasn't reached it yet
    EnemyBaseScouted = 3  # Our scout has finished the first scout of the enemy base (enough to detect rushes)
    MonitoringEnemyChoke = 4  # Our scout is keeping an eye on what leaves the enemy main
    ScoutingBlocked = 5  # (final state) Our scout was prevented from entering the enemy base
    ScoutingFailed = 6  # (final state) Our scout was unable to scout the enemy base (couldn't find it or died quickly)
    ScoutingCompleted = 7  # (final state) Our scout scouted the enemy base and has now left or died


class StrategicState:
    """Measurements of the estimated overall strategic state of the game. Each value is from 0 to 1, where higher is
    better for us and lower is better for the opponent."""

    __slots__ = ("income", "available_resources", "army_count")

    def __init__(self) -> None:
        self.income = 0.5  # How many workers are gathering resources
        self.available_resources = 0.5  # How many minerals are available at owned bases
        self.army_count = 0.5  # Relative army size, taking into consideration expected production from the opponent


_engine: StrategyEngine | None = None
_unit_to_play: dict[MyUnit, Play] = {}
_plays: list[Play] = []
_production_goals: list[ProductionGoal] = []
_mineral_reservations: list[tuple[int, int]] = []
_enemy_contained = False
_enemy_contained_changed = 0
_worker_scout_status = WorkerScoutStatus.Unstarted
_strategic_state = StrategicState()


def _create_strategy_engine(transitioning_from_random: bool) -> None:
    from stardust.strategist.strategy_engines.pv_p.pv_p import PvP
    from stardust.strategist.strategy_engines.pv_t.pv_t import PvT
    from stardust.strategist.strategy_engines.pv_u.pv_u import PvU
    from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ

    global _engine

    engine = game_map.map_specific_override().create_strategy_engine()
    if engine is None:
        race = bwapi.Broodwar.enemy().getRace()
        if race == Races.Protoss:
            engine = PvP()
        elif race == Races.Terran:
            engine = PvT()
        elif race == Races.Zerg:
            engine = PvZ()
        else:
            engine = PvU()
    _engine = engine

    engine.initialize(_plays, transitioning_from_random, "")


class _ReassignableUnit:
    __slots__ = ("unit", "current_play", "distance")

    def __init__(self, unit: MyUnit, current_play: Play | None = None) -> None:
        self.unit = unit
        self.current_play = current_play
        self.distance = 0


def _update_unit_assignments() -> None:
    for play in _plays:
        play.assigned_incomplete_units.clear()

    # Gather a map of all reassignable units by type
    type_to_reassignable_units: dict[int, list[_ReassignableUnit]] = {}
    for unit in units.all_mine():
        if not unit.completed:
            continue
        if unit.type.isBuilding():
            continue
        if unit.type.isWorker():
            continue

        current_play = _unit_to_play.get(unit)
        if current_play is None:
            type_to_reassignable_units.setdefault(unit.type.getID(), []).append(_ReassignableUnit(unit))
        elif current_play.can_reassign_unit(unit):
            type_to_reassignable_units.setdefault(unit.type.getID(), []).append(_ReassignableUnit(unit, current_play))

    # Grab a copy of our incomplete units, in Stardust's std::map order (by type) and creation order
    type_to_incomplete_units: dict[UnitType, list[MyUnit]] = {
        unit_type: sorted(incomplete, key=lambda u: u.id)
        for unit_type, incomplete in sorted(units.all_mine_incomplete_by_type().items(),
                                            key=lambda entry: entry[0].getID())}

    # Process each play
    # They greedily take the reassignable units closest to where they want them
    play_receiving_unassigned_units: Play | None = None
    for play in _plays:
        if play.receives_unassigned_units():
            play_receiving_unassigned_units = play

        # Remove reassignable units already belonging to this play
        # This also makes them unavailable to later (lower-priority) plays
        # TODO: Need to figure out how to allow the scout squad to take a detector from the main army squad when it
        # doesn't need it
        for type_id, reassignable in type_to_reassignable_units.items():
            type_to_reassignable_units[type_id] = [r for r in reassignable if r.current_play is not play]

        for unit_requirement in play.status.unit_requirements:
            if unit_requirement.count < 1:
                continue

            reassignable_units = type_to_reassignable_units.setdefault(unit_requirement.type.getID(), [])

            # Score the available units by distance to the desired position
            # TODO: Include some measurement of whether it is safe for the unit to get to the position
            for reassignable_unit in reassignable_units:
                reassignable_unit.distance = (
                    reassignable_unit.unit.get_distance(unit_requirement.position) if reassignable_unit.unit.is_flying
                    else path_finding.get_ground_distance(reassignable_unit.unit.last_position,
                                                          unit_requirement.position, unit_requirement.type))

            # Pick the unit(s) with the lowest distance
            reassignable_units.sort(key=lambda r: r.distance)

            def reassign(check_grid_node: bool) -> None:
                index = 0
                while index < len(reassignable_units) and unit_requirement.count > 0:
                    reassignable_unit = reassignable_units[index]
                    if reassignable_unit.distance > unit_requirement.distance_limit:
                        break

                    current_play = reassignable_unit.current_play
                    current_squad = current_play.get_squad() if current_play is not None else None
                    if (not unit_requirement.allow_from_vanguard_cluster and current_squad is not None
                            and not current_squad.can_reassign_from_vanguard_cluster(reassignable_unit.unit)):
                        index += 1
                        continue
                    predicate = unit_requirement.grid_node_predicate
                    if (check_grid_node and predicate is not None
                            and not path_finding.check_grid_path(reassignable_unit.unit.get_tile_position(),
                                                                 TilePosition(unit_requirement.position), predicate)):
                        index += 1
                        continue

                    if current_play is not None:
                        current_play.remove_unit(reassignable_unit.unit)
                        cherryvis.log(f"Removed from play: {current_play.label}", reassignable_unit.unit.id)

                    _unit_to_play[reassignable_unit.unit] = play
                    play.add_unit(reassignable_unit.unit)
                    cherryvis.log(f"Added to play: {play.label}", reassignable_unit.unit.id)

                    unit_requirement.count -= 1

                    del reassignable_units[index]

            reassign(True)
            if unit_requirement.allow_failing_grid_node_predicate:
                reassign(False)

            # "Reserve" incomplete units if possible
            incomplete_units = type_to_incomplete_units.get(unit_requirement.type)
            while incomplete_units and unit_requirement.count > 0:
                incomplete_units[0].set_producer_rally_position(unit_requirement.position)

                unit_requirement.count -= 1
                del incomplete_units[0]
                play.assigned_incomplete_units[unit_requirement.type] = (
                    play.assigned_incomplete_units.get(unit_requirement.type, 0) + 1)

    # Add any unassigned units to the appropriate play
    if play_receiving_unassigned_units is not None:
        receiving = play_receiving_unassigned_units
        for reassignable in type_to_reassignable_units.values():
            for reassignable_unit in reassignable:
                if reassignable_unit.current_play is not None:
                    log.get(f"WARNING: Unit assigned to unknown play: {reassignable_unit.unit} in "
                            f"{reassignable_unit.current_play.label}")
                    _unit_to_play.pop(reassignable_unit.unit, None)

                # Skip units our main army plays don't know how to use
                unit = reassignable_unit.unit
                if unit.is_flying and not unit.type.isDetector() and unit.type != bwapi.UnitTypes.Protoss_Arbiter:
                    continue

                _unit_to_play[unit] = receiving
                receiving.add_unit(unit)
                cherryvis.log(f"Added to play: {receiving.label}", unit.id)

        receiving_squad = receiving.get_squad()
        if receiving_squad is not None:
            play_position = receiving_squad.get_target_position()
        else:
            my_main = game_map.get_my_main()
            assert my_main is not None
            play_position = my_main.get_position()
        for unit_type, incomplete_units in type_to_incomplete_units.items():
            if unit_type.isBuilding():
                continue
            if unit_type.isWorker():
                continue

            for incomplete_unit in incomplete_units:
                incomplete_unit.set_producer_rally_position(play_position)
                receiving.assigned_incomplete_units[unit_type] = receiving.assigned_incomplete_units.get(unit_type,
                                                                                                         0) + 1


def _enemy_is_contained() -> bool:
    # Only change our minds after at least 5 seconds
    if common.current_frame - _enemy_contained_changed < 120:
        return _enemy_contained

    # We consider the enemy to be contained if the following is true:
    # - The enemy has a known main base
    # - Our main army play is AttackEnemyBase
    # - The vanguard cluster in that play is either attacking or containing the enemy main or natural
    # - We have no knowledge of enemy combat units outside the enemy main and natural

    # If we don't know where the enemy main is, we don't have it contained
    enemy_main = game_map.get_enemy_main()
    if enemy_main is None:
        return False

    # We only consider the enemy contained if our main army play is AttackEnemyBase and that base is their main or
    # natural
    attack_main_play = next((play for play in _plays if isinstance(play, AttackEnemyBase)), None)
    if attack_main_play is None:
        return False
    if attack_main_play.base is not enemy_main and attack_main_play.base is not game_map.get_enemy_starting_natural():
        return False

    # Get the vanguard cluster with its distance to the enemy main
    vanguard_cluster, vanguard_dist = attack_main_play.get_squad().vanguard_cluster_and_distance()
    if vanguard_cluster is None:
        return False

    # Don't consider the enemy contained if our main army is retreating
    if vanguard_cluster.is_fleeing():
        return False

    # Now gather the areas considered to be part of the enemy main and natural
    enemy_main_areas: set[bwem.Area] = {enemy_main.get_area()}
    if game_map.get_enemy_starting_main() is enemy_main:
        enemy_natural = game_map.get_enemy_starting_natural()
        if enemy_natural is not None:
            enemy_main_areas.add(enemy_natural.get_area())
            choke_path, _ = path_finding.get_choke_point_path(enemy_main.get_position(), enemy_natural.get_position())
            for choke in choke_path:
                first, second = choke.GetAreas()
                enemy_main_areas.add(first)
                enemy_main_areas.add(second)

            # Update the vanguard cluster's distance if it is closer to the natural
            vanguard_distance_natural = path_finding.get_ground_distance(vanguard_cluster.vanguard.last_position,
                                                                         enemy_natural.get_position())
            if vanguard_distance_natural != -1 and vanguard_distance_natural < vanguard_dist:
                vanguard_dist = vanguard_distance_natural

    # The enemy is not contained if the vanguard unit is not in one of the identified areas and we aren't doing a
    # contain
    bwem_map = bwem.Instance()
    vanguard_area = bwem_map.GetNearestArea(WalkPosition(vanguard_cluster.vanguard.last_position))
    if vanguard_area not in enemy_main_areas and (
            vanguard_dist > 1000 or (vanguard_cluster.current_sub_activity != SubActivity.ContainChoke
                                     and vanguard_cluster.current_sub_activity != SubActivity.ContainStaticDefense)):
        return False

    # The enemy is not contained if it either has a combat unit or something capable of training units outside of the
    # identified areas
    for unit in units.all_enemy():
        # Any flying non-building indicates the enemy isn't contained
        if unit.is_flying:
            if unit.type.isBuilding():
                continue
            return False

        if not unit.last_position_valid:
            continue
        if unit.type.isWorker() and unit.last_seen_attacking < common.current_frame - 120:
            continue
        if not (unit.type.isBuilding() and unit.type.canProduce()) and not unit_util.can_attack_ground(unit.type):
            continue

        area = bwem_map.GetArea(WalkPosition(unit.last_position))
        if area is None:
            continue

        if area in enemy_main_areas:
            continue

        # Allow units close to our vanguard cluster, since these are units that might be contained but just on the
        # other side of the choke
        if unit.get_distance(vanguard_cluster.center) < 640:
            continue

        return False

    return True


def _write_instrumentation() -> None:
    if not _CVIS_BOARD_VALUES:
        return

    cherryvis.set_board_list_value("play", [play.label for play in _plays])
    cherryvis.set_board_list_value("prodgoal", [str(goal) for goal in _production_goals])
    cherryvis.set_board_list_value("mineralreservations",
                                   [f"{amount}:{frame}" for amount, frame in _mineral_reservations])

    if _engine is not None:
        cherryvis.set_board_value("strategy", _engine.get_our_strategy())
        cherryvis.set_board_value("strategyEnemy", _engine.get_enemy_strategy())

    cherryvis.set_board_value("enemyContained", "true" if _enemy_contained else "false")
    cherryvis.set_board_value("workerScoutStatus", _worker_scout_status.name)
    cherryvis.set_board_value("pressure", f"{pressure():.2g}")
    cherryvis.set_board_value("strategicState_income", f"{_strategic_state.income:.2g}")
    cherryvis.set_board_value("strategicState_resources", f"{_strategic_state.available_resources:.2g}")
    cherryvis.set_board_value("strategicState_army", f"{_strategic_state.army_count:.2g}")


def initialize() -> None:
    global _enemy_contained, _enemy_contained_changed, _worker_scout_status, _strategic_state

    opponent_economic_model.initialize()

    _unit_to_play.clear()
    _plays.clear()
    _production_goals.clear()
    _mineral_reservations.clear()
    _enemy_contained = False
    _enemy_contained_changed = 0
    _worker_scout_status = WorkerScoutStatus.Unstarted
    _strategic_state = StrategicState()

    _create_strategy_engine(False)


def update() -> None:
    import stardust.opponent as opponent
    from stardust.strategist.strategy_engines.pv_u.pv_u import PvU

    global _enemy_contained, _enemy_contained_changed

    opponent_economic_model.update()

    # Change the strategy engine when we discover the race of a random opponent
    if opponent.has_race_just_been_determined() and isinstance(_engine, PvU):
        _create_strategy_engine(True)

    if _enemy_contained != _enemy_is_contained():
        log.get(f"Enemy is {'no longer ' if _enemy_contained else ''}contained")
        _enemy_contained = not _enemy_contained
        _enemy_contained_changed = common.current_frame

    # Remove all dead or renegaded units from plays
    # This cascades down to squads and unit clusters
    for unit, play in list(_unit_to_play.items()):
        if unit.exists():
            continue
        play.remove_unit(unit)
        del _unit_to_play[unit]

    # Update the plays
    # They signal interesting changes to the Strategist through their PlayStatus object.
    for play in list(_plays):
        play.status.unit_requirements.clear()
        play.status.removed_units.clear()
        play.update()

    # Allow the strategy engine to change our plays
    engine = _engine
    assert engine is not None
    engine.update_plays(_plays)

    # Process the changes signalled by the PlayStatus objects
    index = 0
    while index < len(_plays):
        play = _plays[index]

        def remove_unit(unit: MyUnit, play: Play = play) -> None:
            cherryvis.log(f"Removed from play: {play.label}", unit.id)

            play.remove_unit(unit)
            _unit_to_play.pop(unit, None)

        # Update our unit map for units released from the play
        # We don't do this for the play receiving unassigned units to avoid instability
        if not play.receives_unassigned_units():
            for unit in play.status.removed_units:
                remove_unit(unit)

        # Handle play transition
        # This replaces the current play with a new one, moving all units
        transition_to = play.status.transition_to
        if transition_to is not None:
            def move_unit(unit: MyUnit, play: Play = play, transition_to: Play = transition_to) -> None:
                _unit_to_play[unit] = transition_to
                transition_to.add_unit(unit)
                cherryvis.log(f"Removed from play: {play.label}", unit.id)
                cherryvis.log(f"Added to play: {transition_to.label}", unit.id)

            play.disband(remove_unit, move_unit)

            cherryvis.log(f"Play transition: {play.label}->{transition_to.label}")

            _plays[index] = transition_to
            transition_to.post_transition()
            index += 1

        # Erase the play if it is marked complete
        elif play.status.complete:
            cherryvis.log(f"Play complete: {play.label}")
            play.disband(remove_unit, remove_unit)
            del _plays[index]
        else:
            index += 1

    _update_unit_assignments()

    # Ask all of the plays for their mineral reservations
    # We are not prioritizing them right now, as the expectation is that not many plays will need mineral
    # reservations, but this can be added later.
    _mineral_reservations.clear()
    for play in _plays:
        play.add_mineral_reservations(_mineral_reservations)

    # Ask all of the plays for their production goals
    prioritized_production_goals: ProductionGoals = {}
    for play in _plays:
        play.add_prioritized_production_goals(prioritized_production_goals)

    # Feed everything through the strategy engine
    engine.update_production(_plays, prioritized_production_goals, _mineral_reservations)

    # Flatten the production goals into a list ordered by priority
    _production_goals.clear()
    for priority in sorted(prioritized_production_goals):
        _production_goals.extend(prioritized_production_goals[priority])

    _write_instrumentation()


def current_production_goals() -> list[ProductionGoal]:
    return _production_goals


def current_mineral_reservations() -> list[tuple[int, int]]:
    return _mineral_reservations


def is_enemy_contained() -> bool:
    return _enemy_contained


def are_we_contained() -> bool:
    """We consider ourselves to be contained if our main army is in our base and wants to attack, but can't."""
    main_army_play = get_main_army_play()
    if main_army_play is None:
        return False

    if type(main_army_play) is not AttackEnemyBase:
        return False

    vanguard = main_army_play.get_squad().vanguard_cluster()
    if vanguard is None:
        return False
    if vanguard.current_activity != Activity.Regrouping:
        return False

    area = bwem.Instance().GetArea(WalkPosition(vanguard.vanguard.last_position))
    if area is None:
        return False

    return area in game_map.get_my_main_areas()


def pressure() -> float:
    """A measure between 0 and 1 of how much pressure we feel ourselves to be under, where 0 is no pressure and 1 is
    full pressure."""
    main_army_play = get_main_army_play()
    if main_army_play is None:
        return 1.0

    if main_army_play.is_defensive():
        return 1.0
    if type(main_army_play) is MopUp:
        return 0.0

    squad = main_army_play.get_squad()
    vanguard = squad.vanguard_cluster() if squad is not None else None
    if vanguard is None:
        return 1.0

    return 1.0 - vanguard.percentage_to_enemy_main


def get_strategic_state() -> StrategicState:
    return _strategic_state


def get_worker_scout_status() -> WorkerScoutStatus:
    return _worker_scout_status


def set_worker_scout_status(status: WorkerScoutStatus) -> None:
    global _worker_scout_status
    if _worker_scout_status != status:
        _worker_scout_status = status
        log.get(f"Worker scout status changed to: {_worker_scout_status.name}")


def is_worker_scout_complete() -> bool:
    return _worker_scout_status in (WorkerScoutStatus.ScoutingCompleted, WorkerScoutStatus.ScoutingFailed,
                                    WorkerScoutStatus.ScoutingBlocked)


def has_worker_scout_completed_initial_base_scan() -> bool:
    return _worker_scout_status in (WorkerScoutStatus.EnemyBaseScouted, WorkerScoutStatus.MonitoringEnemyChoke,
                                    WorkerScoutStatus.ScoutingCompleted)


# The following are used by tests to force specific behaviour

def set_opening(opening_plays: list[Play]) -> None:
    _plays[:] = opening_plays


def set_strategy_engine(strategy_engine: StrategyEngine, opening: str = "") -> None:
    global _engine
    _engine = strategy_engine
    strategy_engine.initialize(_plays, False, opening)


def get_strategy_engine() -> StrategyEngine | None:
    return _engine


def get_main_army_play() -> MainArmyPlay | None:
    return next((play for play in _plays if isinstance(play, MainArmyPlay)), None)

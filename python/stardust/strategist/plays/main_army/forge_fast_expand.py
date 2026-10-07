"""Port of Strategist/Plays/MainArmy/ForgeFastExpand.{h,cpp}.

Our FFE play handles both performing the build order, defending the wall, and transitioning into normal base defense if
the wall is at any point determined to be indefensible.

The play orders zealots it needs to defend the wall, but other army production is handled by the strategy engine.

The logic is implemented as a state machine, see the states defined in State.
"""

from __future__ import annotations

from enum import Enum, auto
from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Position, Races, TilePosition, UnitType, UnitTypes, WalkPosition
from stardust import common, config
from stardust.builder import builder, building_placement
from stardust.builder.block import Location
from stardust.builder.building_placement import BuildLocation
from stardust.cpp import INT_MAX, wrap_i32
from stardust.general import general
from stardust.general.squads.defend_wall_squad import DefendWallSquad
from stardust.general.squads.worker_defense_squad import WorkerDefenseSquad
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_BASEDEFENSE, PRIORITY_DEPOTS, PRIORITY_EMERGENCY, PRIORITY_MAINARMY, \
    PRIORITY_WORKERS, ProductionGoals, UnitCallback, add_goal
from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay
from stardust.units import units
from stardust.util import geo, unit_util

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitsAndTargets
    from stardust.map.base import Base
    from stardust.units.unit import Unit

_CVIS_LOG_STATE_CHANGES = config.INSTRUMENTATION_ENABLED


class State(Enum):
    STATE_UNINITIALIZED = auto()  # The state machine has not been run yet
    STATE_PYLON_PENDING = auto()  # Initial state where pylon is not yet built
    STATE_FORGE_PENDING = auto()  # Pylon is built, forge pending
    STATE_NEXUS_PENDING = auto()  # Forge is built, builds reactive cannons before building nexus
    STATE_GATEWAY_PENDING = auto()  # Nexus is built, builds reactive cannons before building gateway
    STATE_FINISHED = auto()  # All buildings are built, defends the wall until strategy engine is ready to transition
    STATE_ANTIFASTRUSHZERG = auto()  # Zerg enemy is doing a fast rush that causes us to abort the FFE and defend the
    # main instead
    STATE_ANTIFASTRUSH_GATEWAY_PENDING = auto()  # Non-zerg enemy is doing a fast rush and we are building the gateway
    # first
    STATE_ANTIFASTRUSH_NEXUS_PENDING = auto()  # Non-zerg enemy is doing a fast rush and we are building the nexus
    # after the gateway


def _my_main() -> Base:
    my_main = game_map.get_my_main()
    assert my_main is not None
    return my_main


def _my_natural() -> Base:
    my_natural = game_map.get_my_natural()
    assert my_natural is not None
    return my_natural


def _is_valid_building(unit: Unit) -> bool:
    if not unit.last_position_valid:
        return False
    if not unit.type.isBuilding():
        return False
    if unit.is_flying:
        return False
    return True


def _nexus_position_blocked() -> bool:
    nexus_position = _my_natural().get_tile_position()
    for unit in units.all_enemy():
        if not _is_valid_building(unit):
            continue

        if geo.overlaps_tiles(nexus_position, 4, 3, unit.get_tile_position(), unit.type.tileWidth(),
                              unit.type.tileHeight()):
            return True

    return False


def _proxy_behind_our_wall() -> bool:
    import stardust.strategist.strategies as strategies
    from stardust.strategist.strategy_engines.pv_p.pv_p import PvP
    from stardust.strategist.strategy_engines.pv_t.pv_t import PvT

    if (not strategies.is_enemy_strategy(PvP.ProtossStrategy.ProxyRush)
            and not strategies.is_enemy_strategy(PvT.TerranStrategy.ProxyRush)):
        return False

    main_and_natural_areas = set(game_map.get_my_main_areas())
    main_and_natural_areas.add(_my_natural().get_area())

    bwem_map = bwem.Instance()
    for unit in units.all_enemy():
        if not _is_valid_building(unit):
            continue

        if bwem_map.GetArea(WalkPosition(unit.last_position)) in main_and_natural_areas:
            log.get("Proxy detected behind our wall")
            return True

    return False


def _worst_case_enemy_arrival_frame() -> int:
    import stardust.strategist.strategist as strategist

    frame = common.current_frame
    strategy_engine = strategist.get_strategy_engine()
    assert strategy_engine is not None

    # For PvP or PvT proxy rushes assume arrival frame of 3700
    if strategy_engine.is_enemy_proxy():
        cherryvis.set_board_value("worstCaseEnemyArrival", "3700")
        return 3700

    # Set the types we are looking for, random assumes zerg as the worst-case situation (besides proxies)
    enemy_race = bwapi.Broodwar.enemy().getRace()
    if enemy_race == Races.Protoss:
        builder_type = UnitTypes.Protoss_Gateway
        unit_type = UnitTypes.Protoss_Zealot
    elif enemy_race == Races.Terran:
        builder_type = UnitTypes.Terran_Barracks
        unit_type = UnitTypes.Terran_Marine
    else:
        builder_type = UnitTypes.Zerg_Spawning_Pool
        unit_type = UnitTypes.Zerg_Zergling

    # Get start frame of the building needed to produce combat units

    # If we have scouted the enemy base, default to the last time we scouted it
    # If we haven't scouted the enemy base, default to 9 pool
    if strategist.has_worker_scout_completed_initial_base_scan():
        enemy_starting_main = game_map.get_enemy_starting_main()
        assert enemy_starting_main is not None
        builder_start_frame = enemy_starting_main.last_scouted
    else:
        builder_start_frame = 1600

    # Add observed information about the building construction
    builder_timings = units.get_enemy_unit_timings(builder_type)
    if builder_timings:
        start_frame, seen_frame = builder_timings[0]

        # We do not trust the start frame if we first saw the building was finished
        # In this case we assume 9 pool
        if start_frame <= (seen_frame - unit_util.build_time(builder_type)):
            builder_start_frame = 1600
        else:
            builder_start_frame = start_frame

    # Get travel time from the closest producer we have seen
    # If the enemy has not yet been scouted, we use the closest possible base position
    producers: list[tuple[Position, int]] = []

    producer_type = UnitTypes.Zerg_Hatchery
    if enemy_race == Races.Protoss:
        producer_type = UnitTypes.Protoss_Gateway
    if enemy_race == Races.Terran:
        producer_type = UnitTypes.Terran_Barracks
    for unit in units.all_enemy_of_type(producer_type):
        producers.append((unit.last_position,
                          0 if unit.completed else max(unit.estimated_completion_frame
                                                       - max(builder_start_frame + unit_util.build_time(builder_type),
                                                             frame),
                                                       0)))

    my_main = _my_main()
    enemy_starting_main = game_map.get_enemy_starting_main()
    for base in game_map.all_starting_locations():
        if base is my_main:
            continue
        if enemy_starting_main is not None:
            if base is not enemy_starting_main:
                continue
        elif base.last_scouted > -1:
            continue

        producers.append((base.get_position(), 0))

    wall = building_placement.get_forge_gateway_wall()
    lowest_travel_time = INT_MAX
    for pos, extra_frames in producers:
        # (An inaccessible producer with extra frames overflows to a negative time in Stardust.)
        frames = wrap_i32(extra_frames + path_finding.expected_travel_time(pos, wall.gap_center, unit_type,
                                                                           PathFindingOptions.Default, 1.1,
                                                                           INT_MAX))
        if frames < lowest_travel_time:
            lowest_travel_time = frames

    if lowest_travel_time == INT_MAX:
        log.get("ERROR: Unable to compute enemy unit travel time to wall")
        lowest_travel_time = 650  # short rush distance on Python

    # Put it all together to get the arrival time
    arrival_time = (builder_start_frame
                    + unit_util.build_time(builder_type)
                    + unit_util.build_time(unit_type)
                    + lowest_travel_time)

    cherryvis.set_board_value("worstCaseEnemyArrival", str(arrival_time))

    return arrival_time


def _add_building_to_goals(prioritized_production_goals: ProductionGoals, unit_type: UnitType, tile: TilePosition,
                           frame: int = 0, priority: int = PRIORITY_DEPOTS, frames_until_powered: int = 0) -> None:
    build_location = BuildLocation(Location(tile),
                                   building_placement.builder_frames(_my_main().get_position(), tile, unit_type),
                                   frames_until_powered, 0)
    add_goal(prioritized_production_goals, priority,
             UnitProductionGoal("ForgeFastExpand", unit_type, 1, 1, build_location, frame))


def _add_unit_to_goals(prioritized_production_goals: ProductionGoals, unit_type: UnitType,
                       priority: int = PRIORITY_DEPOTS) -> None:
    add_goal(prioritized_production_goals, priority, UnitProductionGoal("ForgeFastExpand", unit_type, 1, 1))


def _move_worker_production_to_lower_priority(prioritized_production_goals: ProductionGoals, after_workers: int,
                                              new_priority: int = PRIORITY_DEPOTS) -> None:
    import stardust.strategist.strategist as strategist

    # Jump out if there is no probe production in progress
    worker_goals = prioritized_production_goals.setdefault(PRIORITY_WORKERS, [])
    if not worker_goals:
        return
    probe_goals: list[UnitProductionGoal] = []
    for worker_goal in worker_goals:
        if not isinstance(worker_goal, UnitProductionGoal) or not worker_goal.unit_type().isWorker():
            return
        probe_goals.append(worker_goal)

    new_goals = prioritized_production_goals.setdefault(new_priority, [])

    # Get current number of probes without counting scout worker
    current_probes = units.count_all(UnitTypes.Protoss_Probe)
    if (strategist.get_worker_scout_status() != strategist.WorkerScoutStatus.Unstarted
            and not strategist.is_worker_scout_complete()):
        current_probes -= 1

    # Adjust probe production at high priority to the number requested
    probes = current_probes
    index = 0
    while index < len(probe_goals) and probes < after_workers:
        probe_goal = probe_goals[index]
        probes += probe_goal.count_to_produce()
        if probes > after_workers:
            probe_goal.set_count_to_produce(after_workers - (probes - probe_goal.count_to_produce()))

            new_goals.append(UnitProductionGoal(probe_goal.requester, UnitTypes.Protoss_Probe, probes - after_workers,
                                                probe_goal.get_producer_limit(), probe_goal.get_location()))

        index += 1

    if index < len(worker_goals):
        new_goals.extend(worker_goals[index:])
        del worker_goals[index:]


def _build_base_defense_cannons(base: Base | None, prioritized_production_goals: ProductionGoals, count: int,
                                frame: int, priority: int = PRIORITY_EMERGENCY) -> int:
    if base is None:
        return 0

    base_static_defense_locations = building_placement.base_static_defense_locations(base)
    if not base_static_defense_locations.is_valid():
        return 0

    current_cannons = 0
    cannon_locations: list[TilePosition] = []
    for cannon_location in base_static_defense_locations.worker_defense_cannons:
        if units.my_building_at(cannon_location) is not None:
            current_cannons += 1
        else:
            cannon_locations.append(cannon_location)

    if not cannon_locations:
        return 0
    if current_cannons >= count:
        return 0

    start_frame = frame - unit_util.build_time(UnitTypes.Protoss_Photon_Cannon)
    power_frame = builder.frames_until_completed(base_static_defense_locations.power_pylon,
                                                 unit_util.build_time(UnitTypes.Protoss_Pylon) + 240)

    pylon = units.my_building_at(base_static_defense_locations.power_pylon)
    if pylon is None:
        _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Pylon,
                               base_static_defense_locations.power_pylon,
                               start_frame - unit_util.build_time(UnitTypes.Protoss_Pylon), priority)

    if pylon is not None and not pylon.completed:
        start_frame = max(start_frame, pylon.completion_frame)

    queued = 0
    for cannon_location in cannon_locations:
        _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Photon_Cannon, cannon_location,
                               start_frame, priority, power_frame)
        queued += 1
        if (queued + current_cannons) >= count:
            break

    return power_frame


def _current_wall_cannons(prioritized_production_goals: ProductionGoals,
                          cannon_placements_available: list[TilePosition]) -> int:
    """Fills cannon_placements_available with the wall cannon placements not yet built or queued, returning how many
    are."""
    cannon_placements_available[:] = building_placement.get_forge_gateway_wall().cannons

    def placement_used(tile: TilePosition) -> bool:
        if units.my_building_at(tile) is not None:
            return True

        for goals in prioritized_production_goals.values():
            for goal in goals:
                if not isinstance(goal, UnitProductionGoal):
                    continue
                if goal.unit_type() != UnitTypes.Protoss_Photon_Cannon:
                    continue

                location = goal.get_location()
                if isinstance(location, BuildLocation) and location.location.tile == tile:
                    return True
        return False

    count = 0
    remaining: list[TilePosition] = []
    for tile in cannon_placements_available:
        if placement_used(tile):
            count += 1
        else:
            remaining.append(tile)
    cannon_placements_available[:] = remaining

    return count


def _build_wall_cannons(prioritized_production_goals: ProductionGoals,
                        cannon_placements_available: list[TilePosition], count: int, frame: int,
                        priority: int = PRIORITY_EMERGENCY) -> None:
    if count <= 0:
        return

    remaining = count
    while remaining > 0:
        if not cannon_placements_available:
            return

        _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Photon_Cannon,
                               cannon_placements_available[0], frame, priority)
        del cannon_placements_available[0]

        remaining -= 1


def _handle_muta_rush(prioritized_production_goals: ProductionGoals) -> None:
    import stardust.strategist.strategies as strategies
    from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ

    if not strategies.is_enemy_strategy(PvZ.ZergStrategy.MutaRush):
        return

    # Determine potential positions and frames where a muta will complete
    muta_positions_and_frames: list[tuple[Position, int]] = []

    # Case where we haven't seen them yet
    if units.count_enemy(UnitTypes.Zerg_Mutalisk) == 0:
        spire_start_frame = 0
        spire_timings = units.get_enemy_unit_timings(UnitTypes.Zerg_Spire)
        if spire_timings:
            start_frame, seen_frame = spire_timings[0]

            # We trust the start frame if we first saw it while it was incomplete
            if start_frame > (seen_frame - unit_util.build_time(UnitTypes.Zerg_Spire)):
                spire_start_frame = start_frame
        if spire_start_frame == 0:
            lair_timings = units.get_enemy_unit_timings(UnitTypes.Zerg_Lair)
            if lair_timings:
                start_frame, seen_frame = lair_timings[0]

                # We trust the start frame if we first saw it while it was incomplete
                if start_frame > (seen_frame - unit_util.build_time(UnitTypes.Zerg_Lair)):
                    spire_start_frame = start_frame + unit_util.build_time(UnitTypes.Zerg_Lair)

        # If we don't have data now, we didn't scout the spire or lair while they were incomplete
        # So default to a muta completing now
        if spire_start_frame == 0:
            muta_completion_frame = common.current_frame
        else:
            muta_completion_frame = (spire_start_frame + unit_util.build_time(UnitTypes.Zerg_Spire)
                                     + unit_util.build_time(UnitTypes.Zerg_Mutalisk))

        # Add one muta from each known enemy hatchery or lair
        for hatch in units.all_enemy_of_type(UnitTypes.Zerg_Hatchery):
            if hatch.completed:
                muta_positions_and_frames.append((hatch.last_position, muta_completion_frame))
        for lair in units.all_enemy_of_type(UnitTypes.Zerg_Lair):
            muta_positions_and_frames.append((lair.last_position, muta_completion_frame))
    else:
        # Add each muta's last position
        for muta in units.all_enemy_of_type(UnitTypes.Zerg_Mutalisk):
            muta_positions_and_frames.append((muta.last_position, muta.last_seen))

    # Now determine when the earliest muta could reach the main and natural
    def frame_for_position(target: Position) -> int:
        earliest_arrival_frame = INT_MAX
        for pos, frame in muta_positions_and_frames:
            arrival_frame = frame + path_finding.expected_travel_time(pos, target, UnitTypes.Zerg_Mutalisk)
            if arrival_frame < earliest_arrival_frame:
                earliest_arrival_frame = arrival_frame
        return earliest_arrival_frame

    my_main = _my_main()
    my_natural = _my_natural()
    _build_base_defense_cannons(my_main, prioritized_production_goals, 3, frame_for_position(my_main.get_position()))
    _build_base_defense_cannons(my_natural, prioritized_production_goals, 2,
                                frame_for_position(my_natural.get_position()))

    # Add a second cannon at the wall
    cannon_placements_available: list[TilePosition] = []
    current_cannons = _current_wall_cannons(prioritized_production_goals, cannon_placements_available)
    if current_cannons < 2:
        _build_wall_cannons(prioritized_production_goals, cannon_placements_available, 2 - current_cannons,
                            frame_for_position(my_natural.get_position()) - 100, PRIORITY_BASEDEFENSE)


def _handle_zergling_all_in(prioritized_production_goals: ProductionGoals) -> None:
    import stardust.strategist.strategies as strategies
    from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ

    if not strategies.is_enemy_strategy(PvZ.ZergStrategy.ZerglingAllIn):
        return

    available: list[TilePosition] = []
    current_cannons = _current_wall_cannons(prioritized_production_goals, available)
    if current_cannons < 2:
        _build_wall_cannons(prioritized_production_goals, available, 2 - current_cannons, 0)
    if current_cannons < 3:
        _build_wall_cannons(prioritized_production_goals, available, 1, 4000)
    if current_cannons < 4:
        _build_wall_cannons(prioritized_production_goals, available, 1, 5000)
    if current_cannons < 5:
        _build_wall_cannons(prioritized_production_goals, available, 1, 6000)


def _handle_hydra_bust(prioritized_production_goals: ProductionGoals) -> None:
    import stardust.strategist.strategies as strategies
    from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ

    if not strategies.is_enemy_strategy(PvZ.ZergStrategy.HydraBust):
        return

    available: list[TilePosition] = []
    current_cannons = _current_wall_cannons(prioritized_production_goals, available)
    if current_cannons < 2:
        _build_wall_cannons(prioritized_production_goals, available, 2 - current_cannons, 5000)
    if current_cannons < 3:
        _build_wall_cannons(prioritized_production_goals, available, 1, 6000)
    if current_cannons < 4:
        _build_wall_cannons(prioritized_production_goals, available, 1, 7000)
    if current_cannons < 5:
        _build_wall_cannons(prioritized_production_goals, available, 1, 8000)
    if current_cannons < 6:
        _build_wall_cannons(prioritized_production_goals, available, 1, 9000)


def _handle_non_zerg_rush(prioritized_production_goals: ProductionGoals) -> None:
    import stardust.strategist.strategies as strategies
    from stardust.strategist.strategy_engines.pv_p.pv_p import PvP
    from stardust.strategist.strategy_engines.pv_t.pv_t import PvT

    base_frame = 4000 - unit_util.build_time(UnitTypes.Protoss_Photon_Cannon)
    if (strategies.is_enemy_strategy(PvP.ProtossStrategy.ProxyRush)
            or strategies.is_enemy_strategy(PvT.TerranStrategy.ProxyRush)):
        base_frame -= 500
    elif strategies.is_enemy_strategy(PvP.ProtossStrategy.ZealotAllIn):
        base_frame += 500
    elif (not strategies.is_enemy_strategy(PvP.ProtossStrategy.ZealotRush)
          and not strategies.is_enemy_strategy(PvT.TerranStrategy.MarineRush)):
        return

    available: list[TilePosition] = []
    current_cannons = _current_wall_cannons(prioritized_production_goals, available)
    if current_cannons < 2:
        _build_wall_cannons(prioritized_production_goals, available, 2 - current_cannons, base_frame)
    if current_cannons < 3:
        _build_wall_cannons(prioritized_production_goals, available, 1, base_frame + 1000)
    if current_cannons < 4:
        _build_wall_cannons(prioritized_production_goals, available, 1, base_frame + 2000)

    if units.count_completed(UnitTypes.Protoss_Cybernetics_Core) == 0:
        zealots = units.count_all(UnitTypes.Protoss_Zealot)
        desired_zealots = min(1, units.count_enemy(UnitTypes.Protoss_Zealot) // 2)
        if zealots < desired_zealots:
            # Put priority of zealot slightly lower than cannons if we don't have any yet
            zealot_priority = PRIORITY_MAINARMY
            if zealots == 0:
                zealot_priority = PRIORITY_EMERGENCY + 1
            add_goal(prioritized_production_goals, zealot_priority,
                     UnitProductionGoal("ForgeFastExpand", UnitTypes.Protoss_Zealot, 1, 1))


class ForgeFastExpand(MainArmyPlay):
    def __init__(self) -> None:
        super().__init__("ForgeFastExpand")
        self._current_state = State.STATE_UNINITIALIZED
        self._squad = DefendWallSquad()
        self._main_base_worker_defense_squad = WorkerDefenseSquad(_my_main())

        general.add_squad(self._squad)

    def is_defensive(self) -> bool:
        return True

    def get_squad(self) -> DefendWallSquad:
        return self._squad

    def update(self) -> None:
        super().update()

        # Perform worker defense in main base
        enemy_units = set(units.enemy_at_base(_my_main()))
        workers_and_targets = self._main_base_worker_defense_squad.select_targets(enemy_units)
        empty_units_and_targets: UnitsAndTargets = []
        self._main_base_worker_defense_squad.execute(workers_and_targets, empty_units_and_targets)

        self._update_state()

    def post_transition(self) -> None:
        self._update_state()

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        """For all of the early states, this generates the current build order based on the information we have on
        the enemy. When this is called, we expect the SaturateBases play to have added its probe goal, but nothing
        else to be added yet."""
        import stardust.strategist.strategies as strategies
        from stardust.strategist.plays.main_army.defend_my_main import DefendMyMain
        from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ

        wall = building_placement.get_forge_gateway_wall()
        my_main = _my_main()
        my_natural = _my_natural()
        enemy_race = bwapi.Broodwar.enemy().getRace()

        # Common timing-based logic needed by multiple states
        enemy_arrival_frame = _worst_case_enemy_arrival_frame()
        cannon_frame = enemy_arrival_frame - unit_util.build_time(UnitTypes.Protoss_Photon_Cannon)
        cannon_placements_available: list[TilePosition] = []
        current_cannons = _current_wall_cannons(prioritized_production_goals, cannon_placements_available)

        desired_cannons = 2

        nexus_blocked = _nexus_position_blocked()
        if nexus_blocked:
            cannon_frame = common.current_frame

        state = self._current_state
        if state == State.STATE_UNINITIALIZED:
            return
        elif state in (State.STATE_PYLON_PENDING, State.STATE_FORGE_PENDING):
            if state == State.STATE_PYLON_PENDING:
                _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Pylon, wall.pylon)
            _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Forge, wall.forge)
            _build_wall_cannons(prioritized_production_goals, cannon_placements_available, desired_cannons,
                                cannon_frame, PRIORITY_DEPOTS)
            _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Nexus,
                                   my_natural.get_tile_position())
            _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Gateway, wall.gateway)
            _add_unit_to_goals(prioritized_production_goals, UnitTypes.Protoss_Zealot)
        elif state == State.STATE_NEXUS_PENDING:
            _build_wall_cannons(prioritized_production_goals, cannon_placements_available,
                                desired_cannons - current_cannons, cannon_frame)
            _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Nexus,
                                   my_natural.get_tile_position())
            _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Gateway, wall.gateway)
            _add_unit_to_goals(prioritized_production_goals, UnitTypes.Protoss_Zealot)
            _move_worker_production_to_lower_priority(prioritized_production_goals, 14)
        elif state == State.STATE_GATEWAY_PENDING:
            _build_wall_cannons(prioritized_production_goals, cannon_placements_available,
                                desired_cannons - current_cannons, cannon_frame)
            _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Gateway, wall.gateway)
            _add_unit_to_goals(prioritized_production_goals, UnitTypes.Protoss_Zealot)
            _move_worker_production_to_lower_priority(prioritized_production_goals, 14)
        elif state == State.STATE_FINISHED:
            if (units.count_all(UnitTypes.Protoss_Zealot) == 0
                    and not strategies.is_enemy_strategy(PvZ.ZergStrategy.MutaRush)):
                _add_unit_to_goals(prioritized_production_goals, UnitTypes.Protoss_Zealot, PRIORITY_EMERGENCY)
            _build_wall_cannons(prioritized_production_goals, cannon_placements_available,
                                desired_cannons - current_cannons, cannon_frame)

            # Handle additional cannons based on enemy strategy
            _handle_muta_rush(prioritized_production_goals)
            _handle_zergling_all_in(prioritized_production_goals)
            _handle_hydra_bust(prioritized_production_goals)
            _handle_non_zerg_rush(prioritized_production_goals)
        elif state == State.STATE_ANTIFASTRUSHZERG:
            # Basic idea is to build a pylon and cannons in the main, then add a gateway
            defense_locations = building_placement.base_static_defense_locations(my_main)
            if not defense_locations.power_pylon.isValid() or not defense_locations.worker_defense_cannons:
                self.status.transition_to = DefendMyMain()
            else:
                power_frame = _build_base_defense_cannons(my_main, prioritized_production_goals, 2, 0)

                if (defense_locations.start_block_cannon.isValid()
                        and units.my_building_at(defense_locations.start_block_cannon) is None):
                    _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Photon_Cannon,
                                           defense_locations.start_block_cannon, 0, PRIORITY_EMERGENCY, power_frame)

                add_goal(prioritized_production_goals, PRIORITY_EMERGENCY,
                         UnitProductionGoal(self.label, UnitTypes.Protoss_Zealot, 1, 1))
        elif state == State.STATE_ANTIFASTRUSH_GATEWAY_PENDING:
            _build_wall_cannons(prioritized_production_goals, cannon_placements_available,
                                desired_cannons - current_cannons, cannon_frame)
            _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Gateway, wall.gateway, 0,
                                   PRIORITY_EMERGENCY)
            if enemy_race == Races.Protoss:
                _add_unit_to_goals(prioritized_production_goals, UnitTypes.Protoss_Zealot, PRIORITY_EMERGENCY)
            _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Nexus,
                                   my_natural.get_tile_position())
            _move_worker_production_to_lower_priority(prioritized_production_goals, 16)
        elif state == State.STATE_ANTIFASTRUSH_NEXUS_PENDING:
            _build_wall_cannons(prioritized_production_goals, cannon_placements_available,
                                desired_cannons - current_cannons, cannon_frame)
            if enemy_race == Races.Protoss and units.count_all(UnitTypes.Protoss_Zealot) == 0:
                _add_unit_to_goals(prioritized_production_goals, UnitTypes.Protoss_Zealot, PRIORITY_EMERGENCY)
            _handle_muta_rush(prioritized_production_goals)
            _handle_zergling_all_in(prioritized_production_goals)
            _handle_hydra_bust(prioritized_production_goals)
            _handle_non_zerg_rush(prioritized_production_goals)
            _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Nexus,
                                   my_natural.get_tile_position())
            _move_worker_production_to_lower_priority(prioritized_production_goals, 16)

        # If the enemy did a gas steal, build two cannons in our main to kill it
        if state in (State.STATE_FINISHED, State.STATE_ANTIFASTRUSH_NEXUS_PENDING):
            for gas_steal in units.all_enemy_of_type(enemy_race.getRefinery()):
                if not my_main.has_geyser_or_refinery_at(gas_steal.get_tile_position()):
                    continue

                # Find the best cannon location
                defense_locations = building_placement.base_static_defense_locations(my_main)
                if not defense_locations.is_valid():
                    break

                pylon = units.my_building_at(defense_locations.power_pylon)
                if pylon is None:
                    _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Pylon,
                                           defense_locations.power_pylon, 0, PRIORITY_BASEDEFENSE)

                count = 0
                for cannon_location in defense_locations.worker_defense_cannons:
                    if count >= 2:
                        break

                    dist = geo.edge_to_edge_distance(UnitTypes.Protoss_Photon_Cannon,
                                                     Position(cannon_location) + Position(32, 32),
                                                     gas_steal.type, gas_steal.last_position)
                    if dist > (UnitTypes.Protoss_Photon_Cannon.groundWeapon().maxRange() - 16):
                        continue

                    if units.my_building_at(cannon_location) is None:
                        _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Photon_Cannon,
                                               cannon_location, 0, PRIORITY_BASEDEFENSE)
                    count += 1

                break

        # If the enemy has blocked our natural nexus position, build one of the natural defense cannons
        if nexus_blocked:
            natural_defense_positions = building_placement.base_static_defense_locations(my_natural)
            if natural_defense_positions.is_valid():
                if units.my_building_at(natural_defense_positions.power_pylon) is None:
                    _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Pylon,
                                           natural_defense_positions.power_pylon, 0, PRIORITY_EMERGENCY)
                elif (units.my_building_at(natural_defense_positions.worker_defense_cannons[0]) is None
                      and units.count_all(UnitTypes.Protoss_Forge) > 0):
                    _add_building_to_goals(prioritized_production_goals, UnitTypes.Protoss_Photon_Cannon,
                                           natural_defense_positions.worker_defense_cannons[0], 0, PRIORITY_EMERGENCY)

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        super().disband(removed_unit_callback, movable_unit_callback)
        self._main_base_worker_defense_squad.disband()

    def _update_state(self) -> None:
        import stardust.strategist.strategies as strategies
        import stardust.strategist.strategist as strategist
        from stardust.strategist.plays.main_army.defend_my_main import DefendMyMain
        from stardust.strategist.strategy_engines.pv_p.pv_p import PvP

        wall = building_placement.get_forge_gateway_wall()
        me = bwapi.Broodwar.self()

        def enemy_rushing() -> bool:
            strategy_engine = strategist.get_strategy_engine()
            assert strategy_engine is not None
            return (strategy_engine.is_enemy_rushing()
                    or strategies.is_enemy_strategy(PvP.ProtossStrategy.ZealotAllIn))

        # Update the current state
        previous_state = self._current_state
        counter = 0
        while True:
            last_state = self._current_state
            state = self._current_state
            if state in (State.STATE_UNINITIALIZED, State.STATE_PYLON_PENDING):
                if units.my_building_at(wall.pylon) is not None:
                    self._current_state = State.STATE_FORGE_PENDING
                else:
                    self._current_state = State.STATE_PYLON_PENDING
            elif state == State.STATE_FORGE_PENDING:
                if units.my_building_at(wall.forge) is not None:
                    self._current_state = State.STATE_NEXUS_PENDING
            elif state == State.STATE_NEXUS_PENDING:
                natural = _my_natural()
                if natural.resource_depot is not None and natural.owner == me:
                    self._current_state = State.STATE_GATEWAY_PENDING
                elif enemy_rushing():
                    builder.cancel_base(natural)

                    if bwapi.Broodwar.enemy().getRace() == Races.Zerg or _proxy_behind_our_wall():
                        self._current_state = State.STATE_ANTIFASTRUSHZERG
                    else:
                        self._current_state = State.STATE_ANTIFASTRUSH_GATEWAY_PENDING
                elif _nexus_position_blocked():
                    log.get("Nexus position blocked; building gateway first")
                    self._current_state = State.STATE_ANTIFASTRUSH_GATEWAY_PENDING
            elif state == State.STATE_GATEWAY_PENDING:
                natural = _my_natural()
                if enemy_rushing():
                    builder.cancel_base(natural)

                    if bwapi.Broodwar.enemy().getRace() == Races.Zerg:
                        builder.cancel(wall.gateway)
                        self._current_state = State.STATE_ANTIFASTRUSHZERG
                    else:
                        self._current_state = State.STATE_ANTIFASTRUSH_GATEWAY_PENDING

                if units.my_building_at(wall.gateway) is not None:
                    self._current_state = State.STATE_FINISHED
            elif state == State.STATE_FINISHED:
                # Final state
                pass
            elif state == State.STATE_ANTIFASTRUSHZERG:
                # Transition when we've completed the gateway in our main
                if units.count_completed(UnitTypes.Protoss_Gateway) > 0:
                    self.status.transition_to = DefendMyMain()
            elif state == State.STATE_ANTIFASTRUSH_GATEWAY_PENDING:
                if units.my_building_at(wall.gateway) is not None:
                    self._current_state = State.STATE_ANTIFASTRUSH_NEXUS_PENDING
            elif state == State.STATE_ANTIFASTRUSH_NEXUS_PENDING:
                natural = _my_natural()
                if natural.resource_depot is not None and natural.owner == me:
                    self._current_state = State.STATE_FINISHED

            if last_state == self._current_state:
                break
            counter += 1
            if counter > 20:
                log.get(f"ERROR: ForgeFastExpand play unstable state, waffling between {last_state.name} and "
                        f"{self._current_state.name}")
                break

        if _CVIS_LOG_STATE_CHANGES and self._current_state != previous_state:
            cherryvis.log(f"ForgeFastExpand: State transition from {previous_state.name} to "
                          f"{self._current_state.name}")

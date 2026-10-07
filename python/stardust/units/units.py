"""Port of Units/Units.{h,cpp}: the `Units` namespace, which tracks all of our units, all known enemy units (including
those in the fog), and all resources.

C++ functions that fill an output set (`Units::mine(set, predicate)`, `Units::enemyInRadius(set, ...)`) return a new set
here. The per-unit status dumps (DEBUG_*_STATUS, verbose only in Stardust) are omitted.
"""

from __future__ import annotations

from collections.abc import Callable

import bwapi
import bwem
from bwapi import Orders, Position, Races, TechType, TechTypes, TilePosition, UnitType, UnitTypes, UpgradeType, \
    WalkPosition
from stardust import common, config
from stardust.cpp import INT_MAX
from stardust.builder import building_placement
from stardust.instrumentation import cherryvis, log
from stardust.map.base import Base
from stardust.units.enemy_bunker import EnemyBunker
from stardust.units.my_unit import MyUnit
from stardust.units.resource import Resource
from stardust.units.unit import Unit
from stardust.util import geo, order_process_timer, unit_util
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType
from stardust.workers import workers

_log_units_created_and_lost = True

_tile_to_resource: dict[TilePosition, Resource] = {}

_my_units: set[MyUnit] = set()
_enemy_units: set[Unit] = set()

_unit_id_to_my_unit: dict[int, MyUnit] = {}
_unit_id_to_enemy_unit: dict[int, Unit] = {}

_my_completed_units_by_type: dict[UnitType, set[MyUnit]] = {}
_my_incomplete_units_by_type: dict[UnitType, set[MyUnit]] = {}

_enemy_units_by_type: dict[UnitType, set[Unit]] = {}
_enemy_unit_timings: dict[UnitType, list[tuple[int, int]]] = {}  # (start frame, frame seen) per type

_enemy_units_to_base: dict[Unit, Base] = {}
_bases_to_enemy_units: dict[Base, set[Unit]] = {}
_enemy_units_not_at_enemy_base: set[Unit] = set()

_upgrades_in_progress: set[UpgradeType] = set()
_research_in_progress: set[TechType] = set()


def _by_type(collection: dict[UnitType, set[MyUnit]], unit_type: UnitType) -> set[MyUnit]:
    result = collection.get(unit_type)
    if result is None:
        result = collection[unit_type] = set()
    return result


def _update_resource(bwapi_unit: bwapi.Unit, refinery: Unit | None = None) -> None:
    tile = bwapi_unit.getTilePosition()
    resource = _tile_to_resource.get(tile)
    if resource is None:
        log.get(f"ERROR: Resource not found for {bwapi_unit.getType()} @ {tile}")
        resource = _tile_to_resource[tile] = Resource(bwapi_unit)

    if refinery is not None:
        resource.refinery = refinery
        # Break out if the refinery is owned by another player, as we aren't supposed to have access to resource counts
        if refinery.player != bwapi.Broodwar.self():
            return
    else:
        resource.refinery = None

    resource.current_amount = bwapi_unit.getResources()
    resource.last_seen_frame = common.current_frame


def _resource_destroyed(tile: TilePosition) -> None:
    resource = _tile_to_resource.pop(tile, None)
    if resource is None:
        return
    if config.INSTRUMENTATION_ENABLED:
        cherryvis.log("Destroyed", resource.id)
    resource.destroyed = True
    resource.current_amount = 0
    workers.on_mineral_patch_destroyed(resource)


def _track_research(bwapi_unit: bwapi.Unit) -> None:
    from stardust.players import players

    game = bwapi.Broodwar
    enemy = game.enemy()

    def researched(tech_type: TechType) -> None:
        players.set_has_researched(enemy, tech_type)

    if bwapi_unit.getPlayer() == game.self():
        # Terran
        if bwapi_unit.isLockedDown():
            researched(TechTypes.Lockdown)
        if bwapi_unit.isIrradiated():
            researched(TechTypes.Irradiate)
        if bwapi_unit.isBlind():
            researched(TechTypes.Optical_Flare)

        # Zerg
        if bwapi_unit.isUnderDarkSwarm():
            researched(TechTypes.Dark_Swarm)
        if bwapi_unit.isPlagued():
            researched(TechTypes.Plague)
        if bwapi_unit.isEnsnared():
            researched(TechTypes.Ensnare)
        if bwapi_unit.isParasited():
            researched(TechTypes.Parasite)
        return

    unit_type = bwapi_unit.getType()

    # Terran
    if bwapi_unit.isStimmed():
        researched(TechTypes.Stim_Packs)
    if unit_type == UnitTypes.Terran_Vulture_Spider_Mine:
        researched(TechTypes.Spider_Mines)
    if unit_type == UnitTypes.Spell_Scanner_Sweep or (bwapi_unit.getPlayer() == enemy
                                                      and unit_type == UnitTypes.Terran_Comsat_Station):
        researched(TechTypes.Scanner_Sweep)
    if bwapi_unit.isSieged():
        researched(TechTypes.Tank_Siege_Mode)
    if bwapi_unit.isDefenseMatrixed():
        researched(TechTypes.Defensive_Matrix)
    if unit_type == UnitTypes.Terran_Wraith and bwapi_unit.isCloaked():
        researched(TechTypes.Cloaking_Field)
    if unit_type == UnitTypes.Terran_Ghost and bwapi_unit.isCloaked():
        researched(TechTypes.Personnel_Cloaking)
    if unit_type in (UnitTypes.Terran_Nuclear_Silo, UnitTypes.Terran_Nuclear_Missile):
        researched(TechTypes.Nuclear_Strike)

    # Zerg
    if unit_type != UnitTypes.Zerg_Lurker and (bwapi_unit.isBurrowed()
                                                or bwapi_unit.getOrder() in (Orders.Burrowing, Orders.Unburrowing)):
        researched(TechTypes.Burrowing)
    if unit_type == UnitTypes.Zerg_Broodling:
        researched(TechTypes.Spawn_Broodlings)
    if unit_type == UnitTypes.Spell_Dark_Swarm:
        researched(TechTypes.Dark_Swarm)
    if unit_type in (UnitTypes.Zerg_Lurker_Egg, UnitTypes.Zerg_Lurker):
        researched(TechTypes.Lurker_Aspect)

    # Protoss
    if bwapi_unit.getOrder() == Orders.CastRecall:
        researched(TechTypes.Recall)
    if bwapi_unit.getOrder() == Orders.CastStasisField:
        researched(TechTypes.Stasis_Field)
    if unit_type == UnitTypes.Spell_Disruption_Web:
        researched(TechTypes.Disruption_Web)


def _unit_created(unit: Unit) -> None:
    from stardust.map import game_map, no_go_areas

    game_map.on_unit_created(unit)
    building_placement.on_unit_create(unit)
    no_go_areas.on_unit_create(unit)

    if _log_units_created_and_lost:
        if unit.player == bwapi.Broodwar.self():
            log.get(f"Unit created: {unit}")
        if unit.player == bwapi.Broodwar.enemy() and unit.type.isBuilding():
            log.get(f"Enemy discovered: {unit}")


def _unit_destroyed(unit: Unit, morphed: bool = False) -> None:
    from stardust.map import game_map
    from stardust.players import players
    if (unit.last_position_valid and not unit.being_manufactured_or_carried
            and (not unit.type.isBuilding() or not unit.is_flying)):
        players.grid(unit.player).unit_destroyed(unit.type, unit.last_position, unit.completed, unit.burrowed,
                                                 unit.immobile)

    game_map.on_unit_destroy(unit)
    workers.on_unit_destroy(unit)
    building_placement.on_unit_destroy(unit)

    unit.bwapi_unit = None  # Signals to all holding a reference that this unit is dead

    if not morphed:
        cherryvis.log("Destroyed", unit.id)


def _my_unit_destroyed(unit: MyUnit) -> None:
    if _log_units_created_and_lost:
        log.get(f"Unit lost: {unit}")

    _unit_destroyed(unit)

    _by_type(_my_completed_units_by_type, unit.type).discard(unit)
    _by_type(_my_incomplete_units_by_type, unit.type).discard(unit)
    _my_units.discard(unit)
    _unit_id_to_my_unit.pop(unit.id, None)


def _enemy_unit_destroyed(unit: Unit, morphed: bool = False) -> None:
    import stardust.strategist.opponent_economic_model as opponent_economic_model

    if unit.type.isBuilding() and not morphed and _log_units_created_and_lost:
        log.get(f"Enemy destroyed: {unit}")

    _unit_destroyed(unit, morphed)

    _enemy_units.discard(unit)
    _unit_id_to_enemy_unit.pop(unit.id, None)
    _enemy_units_by_type.setdefault(unit.type, set()).discard(unit)
    base = _enemy_units_to_base.pop(unit, None)
    if base is not None:
        if config.INSTRUMENTATION_ENABLED:
            cherryvis.log(f"Removed from base @ {WalkPosition(base.get_position())}", unit.id)
        _bases_to_enemy_units.setdefault(base, set()).discard(unit)
    _enemy_units_not_at_enemy_base.discard(unit)

    opponent_economic_model.opponent_unit_destroyed(unit.type, unit.id)


def _track_enemy_unit_timings(unit: Unit, include_morphs: bool) -> None:
    import stardust.opponent as opponent
    from stardust.map import game_map
    from stardust.map.path_finding import path_finding
    import stardust.strategist.opponent_economic_model as opponent_economic_model
    import stardust.strategist.strategist as strategist

    frame = common.current_frame

    # Don't track eggs or larva
    if unit.type in (UnitTypes.Zerg_Larva, UnitTypes.Zerg_Egg):
        return

    # Don't track a gas steal
    my_main = game_map.get_my_main()
    if unit.type.isRefinery() and my_main is not None and unit.get_distance(my_main.get_position()) < 300:
        return

    # If this is the opponent's initial depot, update the element we already added
    timings = _enemy_unit_timings.setdefault(unit.type, [])
    enemy_starting_main = game_map.get_enemy_starting_main()
    if (unit.type.isResourceDepot() and timings and enemy_starting_main is not None
            and unit.get_tile_position() == enemy_starting_main.get_tile_position()):
        timings[0] = (timings[0][0], frame)
        return

    # TODO (upstream): Do we need to handle the initial units?

    options = path_finding.PathFindingOptions.UseNearestBWEMArea

    # Estimate the frame the unit completed
    if unit.type.isBuilding():
        completion_frame = frame if unit.completed else unit.estimated_completion_frame
    else:
        def get_closest_producer() -> Unit | None:
            closest_producer: Unit | None = None
            closest_dist = INT_MAX

            def handle_possible_producer(producer: Unit) -> None:
                nonlocal closest_producer, closest_dist
                if not producer.exists() or not producer.last_position_valid:
                    return
                if unit.is_flying:
                    dist = producer.last_position.getApproxDistance(unit.last_position)
                else:
                    dist = path_finding.get_ground_distance(producer.last_position, unit.last_position, unit.type,
                                                            options)
                if dist < closest_dist:
                    closest_dist = dist
                    closest_producer = producer

            # Assume all Zerg units come from a larva (at a hatchery / lair / hive)
            if unit.player.getRace() == Races.Zerg:
                for producer in list(_enemy_units):
                    if producer.type in (UnitTypes.Zerg_Hatchery, UnitTypes.Zerg_Lair, UnitTypes.Zerg_Hive):
                        handle_possible_producer(producer)
            else:
                producer_type = unit.type.whatBuilds()[0]
                if not producer_type.isBuilding():
                    producer_type = producer_type.whatBuilds()[0]
                for producer in list(_enemy_units_by_type.get(producer_type, ())):
                    if not producer.is_flying:
                        handle_possible_producer(producer)

            return closest_producer

        # Try to guess how many frames the unit has moved since it was built
        def get_frames_moved() -> int:
            if unit.type.topSpeed() < 0.001:
                return 0

            # Assume proxied units need to travel about 5 seconds
            engine = strategist.get_strategy_engine()
            assert engine is not None
            if engine.is_enemy_proxy() and frame < 8000:
                return 120

            closest_producer = get_closest_producer()
            if closest_producer is not None:
                return path_finding.expected_travel_time(unit.last_position, closest_producer.last_position,
                                                         unit.type, options, 1.1)

            # We don't know the producer, so assume it came from the closest possible enemy main
            enemy_main = game_map.get_enemy_main() or game_map.get_enemy_starting_main()
            if enemy_main is not None:
                return path_finding.expected_travel_time(unit.last_position, enemy_main.get_position(), unit.type,
                                                         options, 1.1)

            # Default to the closest unscouted starting position
            frames_moved = INT_MAX
            for base in game_map.unscouted_starting_locations():
                frames_moved = min(frames_moved, path_finding.expected_travel_time(
                    unit.last_position, base.get_position(), unit.type, options, 1.1))

            # Guard against bugs in scouting
            return 0 if frames_moved == INT_MAX else frames_moved

        completion_frame = frame - get_frames_moved()

    start_frame = completion_frame - unit_util.build_time(unit.type)
    timings.append((start_frame, frame))

    opponent_economic_model.opponent_unit_created(unit.type, unit.id, start_frame, not unit.completed)

    if len(timings) == 1:
        log.get(f"First enemy of type discovered: {unit}")

        # Track some common tech rush units in our opponent model
        if unit.type == UnitTypes.Protoss_Dark_Templar:
            opponent.set_game_value("firstDarkTemplarCompleted", completion_frame)
        elif unit.type == UnitTypes.Zerg_Mutalisk:
            opponent.set_game_value("firstMutaliskCompleted", completion_frame)
        elif unit.type == UnitTypes.Zerg_Lurker:
            # Get frames it would take this lurker to get to our main choke
            main_choke = game_map.get_my_main_choke()
            if main_choke is not None:
                frames = path_finding.expected_travel_time(
                    unit.last_position, main_choke.center, UnitTypes.Zerg_Lurker,
                    path_finding.PathFindingOptions.UseNeighbouringBWEMArea, 1.1, 1000)
                opponent.set_game_value("firstLurkerAtOurMain", completion_frame + frames)

    if unit.type.isBuilding() and include_morphs:
        morphs_from_type, _ = unit_util.morphs_from(unit.type)
        if morphs_from_type not in (UnitTypes.None_, UnitTypes.Zerg_Drone):
            _enemy_unit_timings.setdefault(morphs_from_type, []).append(
                (start_frame - unit_util.build_time(morphs_from_type), frame))


def _assign_enemy_units_to_bases() -> None:
    from stardust.map import game_map
    from stardust.map.path_finding import path_finding

    frame = common.current_frame
    game = bwapi.Broodwar

    def combat_unit(unit: Unit) -> bool:
        return ((unit_util.is_combat_unit(unit.type) or unit.last_seen_attacking >= frame - 120)
                and (unit.is_transport() or unit_util.can_attack_ground(unit.type)))

    def get_base(unit: Unit) -> Base | None:
        if not unit.last_position_valid:
            return None

        closest: Base | None = None
        closest_dist = 1000
        is_combat = combat_unit(unit)
        for base in game_map.all_bases():
            # Only consider combat units and units that block untaken bases
            if not is_combat and (base.owner is not None or not geo.overlaps_tiles(
                    base.get_tile_position(), 4, 3, unit.get_tile_position(), 2, 2)):
                continue

            # For our bases, ignore units we haven't seen for a while
            if base.owner == game.self() and not unit.type.isBuilding() and unit.last_seen < frame - 240:
                continue

            if unit.is_flying:
                dist = unit.last_position.getApproxDistance(base.get_position())
            else:
                dist = path_finding.get_ground_distance(unit.last_position, base.get_position(), unit.type)
            if dist == -1 or dist > closest_dist:
                continue

            # If the distance is above 500, ignore this unit if the base is unowned or it is moving away from the base
            if dist > 500:
                if base.owner is None:
                    continue
                predicted_position = unit.predict_position(1)
                if (predicted_position.getApproxDistance(base.get_position())
                        > unit.last_position.getApproxDistance(base.get_position())):
                    continue

            closest = base
            closest_dist = dist

        return closest

    for unit in list(_enemy_units):
        base = get_base(unit)

        # Remove from current base if it is different from the new one
        current = _enemy_units_to_base.get(unit)
        if current is not None:
            if current is base:
                continue
            if config.INSTRUMENTATION_ENABLED:
                cherryvis.log(f"Removed from base @ {WalkPosition(current.get_position())}", unit.id)
            _bases_to_enemy_units.setdefault(current, set()).discard(unit)
            del _enemy_units_to_base[unit]

        # Add to the new base if required
        if base is not None:
            if config.INSTRUMENTATION_ENABLED:
                cherryvis.log(f"Added to base @ {WalkPosition(base.get_position())}", unit.id)
            _bases_to_enemy_units.setdefault(base, set()).add(unit)
            _enemy_units_to_base[unit] = base

        if combat_unit(unit) and (base is None or base.owner != game.enemy()):
            _enemy_units_not_at_enemy_base.add(unit)
        else:
            _enemy_units_not_at_enemy_base.discard(unit)


def _create_my_unit(bwapi_unit: bwapi.Unit) -> MyUnit:
    from stardust.units.my_cannon import MyCannon
    from stardust.units.my_carrier import MyCarrier
    from stardust.units.my_corsair import MyCorsair
    from stardust.units.my_dragoon import MyDragoon
    from stardust.units.my_observer import MyObserver
    from stardust.units.my_worker import MyWorker

    unit_type = bwapi_unit.getType()
    unit: MyUnit
    if unit_type.isWorker():
        unit = MyWorker(bwapi_unit)
    elif unit_type == UnitTypes.Protoss_Dragoon:
        unit = MyDragoon(bwapi_unit)
    elif unit_type == UnitTypes.Protoss_Observer:
        unit = MyObserver(bwapi_unit)
    elif unit_type == UnitTypes.Protoss_Carrier:
        unit = MyCarrier(bwapi_unit)
    elif unit_type == UnitTypes.Protoss_Corsair:
        unit = MyCorsair(bwapi_unit)
    elif unit_type == UnitTypes.Protoss_Photon_Cannon:
        unit = MyCannon(bwapi_unit)
    else:
        unit = MyUnit(bwapi_unit)
    unit.created()
    return unit


def initialize() -> None:
    import stardust.opponent as opponent

    _tile_to_resource.clear()
    _my_units.clear()
    _enemy_units.clear()
    _unit_id_to_my_unit.clear()
    _unit_id_to_enemy_unit.clear()
    _my_completed_units_by_type.clear()
    _my_incomplete_units_by_type.clear()
    _enemy_units_by_type.clear()
    _enemy_unit_timings.clear()
    _enemy_units_to_base.clear()
    _bases_to_enemy_units.clear()
    _enemy_units_not_at_enemy_base.clear()
    _upgrades_in_progress.clear()
    _research_in_progress.clear()

    # Add a placeholder for the enemy depot to the timings
    if opponent.is_unknown_race():
        # For a random opponent, we don't know what type of depot they have, so just add one of each.
        # The matchup-specific strategy engines will never query for offrace types anyway.
        for depot in (UnitTypes.Protoss_Nexus, UnitTypes.Terran_Command_Center, UnitTypes.Zerg_Hatchery):
            _enemy_unit_timings.setdefault(depot, []).append((0, INT_MAX))
    else:
        depot = bwapi.Broodwar.enemy().getRace().getResourceDepot()
        _enemy_unit_timings.setdefault(depot, []).append((0, INT_MAX))

    # Initialize resources
    for bwapi_unit in bwapi.Broodwar.getNeutralUnits():
        if bwapi_unit.getType().isMineralField() or bwapi_unit.getType() == UnitTypes.Resource_Vespene_Geyser:
            _tile_to_resource.setdefault(bwapi_unit.getTilePosition(), Resource(bwapi_unit))


def update() -> None:
    from stardust.map import game_map
    from stardust.players import players
    from stardust.units.my_worker import MyWorker

    game = bwapi.Broodwar
    frame = common.current_frame
    _upgrades_in_progress.clear()
    _research_in_progress.clear()

    # Start by updating the order timers. Unit update may reset these later in the frame if something has been
    # observed that reveals the order timer. Order timers reset to unknown values every 150 frames starting on frame 8.
    reset = order_process_timer.is_reset_frame()
    for timed_unit in [*_my_units, *_enemy_units]:
        if reset:
            timed_unit.order_process_timer = -1
        elif timed_unit.order_process_timer > 0:
            timed_unit.order_process_timer -= 1
        elif timed_unit.order_process_timer == 0:
            timed_unit.order_process_timer = 8

    def ignore_unit(bwapi_unit: bwapi.Unit) -> bool:
        unit_type = bwapi_unit.getType()
        return unit_type in (UnitTypes.Protoss_Interceptor, UnitTypes.Protoss_Scarab) or unit_type.isSpell()

    # Update our units. We always have vision of our own units, so we don't have to handle units in fog.
    for bwapi_unit in game.self().getUnits():
        if bwapi_unit is None or not bwapi_unit.exists():
            continue

        _track_research(bwapi_unit)

        if ignore_unit(bwapi_unit):
            continue

        # If we just mind controlled an enemy unit, consider the enemy unit destroyed
        enemy_unit = _unit_id_to_enemy_unit.get(bwapi_unit.getID())
        if enemy_unit is not None:
            _enemy_unit_destroyed(enemy_unit)

        # Create or update
        unit = _unit_id_to_my_unit.get(bwapi_unit.getID())
        if unit is None or not unit.exists():
            unit = _create_my_unit(bwapi_unit)
            _my_units.add(unit)
            _unit_id_to_my_unit[unit.id] = unit

            _unit_created(unit)

            _by_type(_my_completed_units_by_type if unit.completed else _my_incomplete_units_by_type,
                     unit.type).add(unit)
        else:
            if (unit.type == UnitTypes.Protoss_Assimilator and not unit.completed and bwapi_unit.isCompleted()):
                for base in game_map.get_my_bases():
                    if base.has_geyser_or_refinery_at(unit.get_tile_position()):
                        if base.resource_depot is None:
                            log.get(f"ERROR: Assimilator @ {unit.get_tile_position()} completed without nexus")
                        elif not base.resource_depot.completed:
                            log.get(f"ERROR: Assimilator @ {unit.get_tile_position()} completed before nexus")

            if not unit.completed and bwapi_unit.isCompleted():
                _by_type(_my_completed_units_by_type, unit.type).add(unit)
                _by_type(_my_incomplete_units_by_type, unit.type).discard(unit)

            unit.update(bwapi_unit)

        if bwapi_unit.getType().isRefinery():
            _update_resource(bwapi_unit, unit)

        if bwapi_unit.isUpgrading():
            _upgrades_in_progress.add(bwapi_unit.getUpgrade())
        elif bwapi_unit.isResearching():
            _research_in_progress.add(bwapi_unit.getTech())

        build_unit = bwapi_unit.getBuildUnit()
        if build_unit is not None:
            producing = _unit_id_to_my_unit.get(build_unit.getID())
            if producing is not None:
                producing.producer = bwapi_unit

    # If this is the first frame, set the order process index for our initial units.
    # They are always initialized in this order: depot, leftmost worker to rightmost worker.
    # Some maps use tricks to move the starting workers. If we have a map-specific override, we use this to get the
    # expected locations, otherwise we set the order process indices to the same value since we don't know which
    # comes first. We also set the spawn position on the workers.
    if frame == 0:
        expected_worker_positions = game_map.map_specific_override().starting_worker_positions(
            game.self().getStartLocation())
        count_found = 0
        for unit in _my_units:
            if unit.type.isResourceDepot():
                unit.order_process_index = 0
            if not isinstance(unit, MyWorker):
                continue
            unit.spawn_position = unit.last_position
            if unit.last_position in expected_worker_positions:
                unit.order_process_index = expected_worker_positions.index(unit.last_position) + 1
                count_found += 1

        if count_found != 4:
            log.get("WARNING: Could not determine order process index of all starting workers")
            for unit in _my_units:
                if unit.type.isWorker():
                    unit.order_process_index = 1

    # Update visible enemy units
    for bwapi_unit in game.enemy().getUnits():
        _track_research(bwapi_unit)

        if ignore_unit(bwapi_unit) or not bwapi_unit.isVisible():
            continue

        # If the enemy just mind controlled one of our units, consider our unit destroyed
        my_unit = _unit_id_to_my_unit.get(bwapi_unit.getID())
        if my_unit is not None:
            _my_unit_destroyed(my_unit)

        # Create or update
        existing = _unit_id_to_enemy_unit.get(bwapi_unit.getID())
        morphed = False
        if existing is not None:
            # If the type is still the same, update and continue
            if existing.type == bwapi_unit.getType():
                existing.update(bwapi_unit)
                if existing.type.isRefinery():
                    _update_resource(bwapi_unit, existing)
                continue

            # The unit has morphed - for simplicity consider the old one as destroyed and the new one created
            morphed = True
            _enemy_unit_destroyed(existing, True)

        enemy: Unit = EnemyBunker(bwapi_unit) if bwapi_unit.getType() == UnitTypes.Terran_Bunker else Unit(bwapi_unit)
        enemy.created()
        _enemy_units.add(enemy)
        _unit_id_to_enemy_unit[enemy.id] = enemy

        _unit_created(enemy)

        if enemy.type.isRefinery():
            _update_resource(bwapi_unit, enemy)

        _enemy_units_by_type.setdefault(enemy.type, set()).add(enemy)
        _track_enemy_unit_timings(enemy, not morphed)

    # Update enemy units in the fog
    destroyed_enemy_units: list[Unit] = []
    for enemy in list(_enemy_units):
        if enemy.last_seen == frame:
            continue

        enemy.update_unit_in_fog()

        # If a building that can't be lifted has disappeared from its last position, treat it as destroyed
        if not enemy.last_position_valid and enemy.type.isBuilding() and not enemy.type.isFlyingBuilding():
            destroyed_enemy_units.append(enemy)
    for enemy in destroyed_enemy_units:
        _enemy_unit_destroyed(enemy)

    # Build a set of build tiles for mineral fields that should be visible
    visible_mineral_field_tiles: set[TilePosition] = set()
    for tile, mineral_field in _tile_to_resource.items():
        if not mineral_field.is_minerals:
            continue
        if game.isVisible(tile) and game.isVisible(tile + TilePosition(1, 0)):
            if mineral_field.tile_seen_last_frame:
                visible_mineral_field_tiles.add(tile)
            mineral_field.tile_seen_last_frame = True
        else:
            mineral_field.tile_seen_last_frame = False

    # Update visible neutral units to detect addons that have gone neutral or refineries that have become geysers
    destroyed_enemy_units.clear()
    for bwapi_unit in game.neutral().getUnits():
        if not bwapi_unit.isVisible() or not bwapi_unit.exists():
            continue

        # Update resource counts
        if bwapi_unit.getType().isMineralField() or bwapi_unit.getType() == UnitTypes.Resource_Vespene_Geyser:
            _update_resource(bwapi_unit)
            visible_mineral_field_tiles.discard(bwapi_unit.getTilePosition())

        neutral_unit = _unit_id_to_enemy_unit.get(bwapi_unit.getID())
        if neutral_unit is None:
            continue

        # Refineries are treated as destroyed
        if neutral_unit.type.isRefinery():
            destroyed_enemy_units.append(neutral_unit)
            continue

        # All others are treated as destroyed for the grid but otherwise not
        if neutral_unit.type.isBuilding():
            log.get(f"Enemy switched to neutral: {neutral_unit}")

        if neutral_unit.last_position_valid and not neutral_unit.being_manufactured_or_carried:
            players.grid(neutral_unit.player).unit_destroyed(neutral_unit.type, neutral_unit.last_position,
                                                             neutral_unit.completed, neutral_unit.burrowed,
                                                             neutral_unit.immobile)

        neutral_unit.bwapi_unit = None  # Signals to all holding a reference that this unit is dead

        _enemy_units_by_type.setdefault(neutral_unit.type, set()).discard(neutral_unit)
        _enemy_units.discard(neutral_unit)
        del _unit_id_to_enemy_unit[bwapi_unit.getID()]
    for enemy in destroyed_enemy_units:
        _enemy_unit_destroyed(enemy)

    # Mark mineral fields destroyed if we should be able to see them but they aren't there
    for mineral_field_tile in sorted(visible_mineral_field_tiles):
        _resource_destroyed(mineral_field_tile)

    _assign_enemy_units_to_bases()

    # Occasionally check for any inconsistencies in the enemy unit collections
    if frame % 48 == 0:
        _check_enemy_unit_collections()

    if config.INSTRUMENTATION_ENABLED:
        values = []
        for unit_type in sorted(_enemy_unit_timings):
            timings = _enemy_unit_timings[unit_type]
            if timings:
                values.append(f"{unit_type}: " + ", ".join(f"{start}:{seen}" for start, seen in timings))
        cherryvis.set_board_list_value("enemyUnitTimings", values)


def _check_enemy_unit_collections() -> None:
    enemy_player = bwapi.Broodwar.enemy()

    def check_unit(unit: Unit, label: str) -> bool:
        error = None
        if not unit.exists():
            error = "does not exist!"
        elif unit.player != enemy_player:
            error = "not owned by enemy!"
        elif unit.bwapi_unit is not None and unit.bwapi_unit.isVisible() and unit.bwapi_unit.getPlayer() != enemy_player:
            error = "not owned by enemy (bwapiUnit)!"
        if error is None:
            return True
        message = f"ERROR: {unit} in {label} {error}"
        log.get(message)
        cherryvis.log(message, unit.id)
        return False

    for unit in [u for u in _enemy_units if not check_unit(u, "enemyUnits")]:
        _enemy_units.discard(unit)
    for unit_id, unit in [(i, u) for i, u in _unit_id_to_enemy_unit.items() if not check_unit(u, "unitIdToEnemyUnit")]:
        del _unit_id_to_enemy_unit[unit_id]
    for units_of_type in _enemy_units_by_type.values():
        for unit in [u for u in units_of_type if not check_unit(u, "enemyUnitsByType")]:
            units_of_type.discard(unit)
    for unit in [u for u in _enemy_units_to_base if not check_unit(u, "enemyUnitsToBase")]:
        del _enemy_units_to_base[unit]
    for units_at_base in _bases_to_enemy_units.values():
        for unit in [u for u in units_at_base if not check_unit(u, "basesAndEnemyUnits")]:
            units_at_base.discard(unit)
    for unit in [u for u in _enemy_units_not_at_enemy_base if not check_unit(u, "enemyUnitsNotAtEnemyBase")]:
        _enemy_units_not_at_enemy_base.discard(unit)


def issue_orders() -> None:
    for unit in list(_my_units):
        unit.issue_move_orders()


def on_unit_destroy(bwapi_unit: bwapi.Unit) -> None:
    my_unit = _unit_id_to_my_unit.get(bwapi_unit.getID())
    if my_unit is not None:
        _my_unit_destroyed(my_unit)

    enemy_unit = _unit_id_to_enemy_unit.get(bwapi_unit.getID())
    if enemy_unit is not None:
        _enemy_unit_destroyed(enemy_unit)


def on_bullet_create(bullet: bwapi.Bullet) -> None:
    from stardust import bullets

    target = get(bullet.getTarget())
    if target is None:
        return

    source = get(bullet.getSource())
    if source is not None:
        source.last_target = target

    # If this bullet is a ranged bullet that deals damage after a delay, track it on the unit it is moving towards
    fixed_delay = bullets.fixed_damage_delay(bullet.getType())
    if fixed_delay is not None or bullets.deals_damage_after_delay(bullet.getType()):
        target.add_upcoming_attack_from_bullet(source, bullet, fixed_delay)


def get(unit: bwapi.Unit | None) -> Unit | None:
    if unit is None:
        return None
    unit_id = unit.getID()
    return _unit_id_to_my_unit.get(unit_id) or _unit_id_to_enemy_unit.get(unit_id)


def mine(unit: bwapi.Unit) -> MyUnit | None:
    return _unit_id_to_my_unit.get(unit.getID())


def my_building_at(tile: TilePosition) -> MyUnit | None:
    for unit in _my_units:
        if unit.type.isBuilding() and unit.get_tile_position() == tile:
            return unit
    return None


def all_mine() -> set[MyUnit]:
    return _my_units


def all_mine_completed_of_type(unit_type: UnitType) -> set[MyUnit]:
    return _by_type(_my_completed_units_by_type, unit_type)


def all_mine_incomplete_of_type(unit_type: UnitType) -> set[MyUnit]:
    return _by_type(_my_incomplete_units_by_type, unit_type)


def all_mine_incomplete_by_type() -> dict[UnitType, set[MyUnit]]:
    return _my_incomplete_units_by_type


def all_enemy() -> set[Unit]:
    return _enemy_units


def all_enemy_of_type(unit_type: UnitType) -> set[Unit]:
    return _enemy_units_by_type.setdefault(unit_type, set())


def mine_matching(predicate: Callable[[MyUnit], bool] | None = None) -> set[MyUnit]:
    return {unit for unit in _my_units if predicate is None or predicate(unit)}


def enemy_matching(predicate: Callable[[Unit], bool] | None = None) -> set[Unit]:
    return {unit for unit in _enemy_units if predicate is None or predicate(unit)}


def enemy_in_radius(position: Position, radius: int, predicate: Callable[[Unit], bool] | None = None) -> set[Unit]:
    return {unit for unit in _enemy_units
            if (predicate is None or predicate(unit))
            and unit.sim_position_valid and unit.sim_position.getApproxDistance(position) <= radius}


def enemy_in_area(area: bwem.Area | None, predicate: Callable[[Unit], bool] | None = None) -> set[Unit]:
    bwem_map = bwem.Instance()
    return {unit for unit in _enemy_units
            if (predicate is None or predicate(unit))
            and unit.last_position_valid and bwem_map.GetArea(WalkPosition(unit.last_position)) == area}


def enemy_at_base(base: Base) -> set[Unit]:
    return _bases_to_enemy_units.setdefault(base, set())


def enemy_combat_units_not_at_an_enemy_base() -> set[Unit]:
    return _enemy_units_not_at_enemy_base


def count_all(unit_type: UnitType) -> int:
    return count_completed(unit_type) + count_incomplete(unit_type)


def count_completed(unit_type: UnitType) -> int:
    return len(_my_completed_units_by_type.get(unit_type, ()))


def count_incomplete(unit_type: UnitType) -> int:
    return len(_my_incomplete_units_by_type.get(unit_type, ()))


def count_incomplete_by_type() -> dict[UnitType, int]:
    return {unit_type: len(units) for unit_type, units in sorted(_my_incomplete_units_by_type.items())}


def count_enemy(unit_type: UnitType) -> int:
    return len(_enemy_units_by_type.get(unit_type, ()))


def get_enemy_unit_timings(unit_type: UnitType) -> list[tuple[int, int]]:
    return _enemy_unit_timings.setdefault(unit_type, [])


def has_enemy_built(unit_type: UnitType) -> bool:
    return bool(_enemy_unit_timings.get(unit_type))


def resource_at(tile: TilePosition) -> Resource | None:
    return _tile_to_resource.get(tile)


def my_completed_refineries() -> list[Resource]:
    return [resource for _, resource in sorted(_tile_to_resource.items()) if resource.has_my_completed_refinery()]


def is_being_upgraded_or_researched(upgrade_or_tech: UpgradeOrTechType) -> bool:
    if upgrade_or_tech.is_tech_type():
        return upgrade_or_tech.tech_type in _research_in_progress
    return upgrade_or_tech.upgrade_type in _upgrades_in_progress


def set_log_units_created_and_lost(new_value: bool) -> None:
    global _log_units_created_and_lost
    _log_units_created_and_lost = new_value

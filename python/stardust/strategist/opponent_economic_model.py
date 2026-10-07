"""Port of Strategist/OpponentEconomicModel.{h,cpp}: simple economic modelling of the opponent to figure out what we
need to protect against. It currently only works for Protoss opponents on one base.

The model simulates the opponent's income over the first MODEL_FRAME_LIMIT frames, spends it on the units we have
observed (and the prerequisites they imply), pulls them as early as resources allow, and infers the minimum number of
production facilities the opponent must have.

Porting notes:
- Unit sets ordered by (creation frame, completion frame, pointer) are lists sorted by (creation frame, completion
  frame, creation order).
- The resource timelines are numpy arrays; scans whose state doesn't change mid-scan are vectorized, the others are
  plain loops, as in Stardust.
- The verbose board values and build order output are omitted.
"""

from __future__ import annotations

import heapq
from typing import TYPE_CHECKING

import numpy as np

import bwapi
from bwapi import Races, TechType, UnitType, UnitTypes, UpgradeType, UpgradeTypes
from stardust import common
from stardust.cpp import to_int
from stardust.instrumentation import log
from stardust.map import game_map
from stardust.units import units
from stardust.util import unit_util
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType

if TYPE_CHECKING:
    Array = np.ndarray

MODEL_FRAME_LIMIT = 20000

_MINERALS_PER_WORKER_FRAME = 0.0465
_GAS_PER_WORKER_FRAME = 0.071

# Assume a builder misses one mining run until frame 5500
_BUILDER_LOSS = 8
_BUILDER_LOSS_UNTIL = 5500

# We simulate the early income using an assumed normal build
_PYLON_STARTED = 1115
_PYLON_FINISHED = 1625
_SCOUT_SENT = 1850
_SCOUT_DIED = 4500

# When guessing at gas timings, use this frame as the earliest frame
_EARLIEST_GAS = 2500

# The number of frames we simulate it takes a worker to transfer from minerals to gas
_TRANSFER_TO_GAS_FRAMES = 24

_unit_sequence = 0


class _EcoModelUnit:
    """An enemy unit in our economic model."""

    __slots__ = ("type", "id", "creation_frame", "completion_frame", "death_frame", "creation_frame_known",
                 "has_produced", "can_produce_at", "prerequisites_available_frame", "shifted_creation_frame",
                 "supply_required", "mineral_price", "gas_price", "seq")

    def __init__(self, unit_type: UnitType, unit_id: int, creation_frame: int,
                 creation_frame_known: bool = False) -> None:
        global _unit_sequence

        # These fields are from observations
        self.type = unit_type
        self.id = unit_id
        self.creation_frame = creation_frame
        self.completion_frame = creation_frame + unit_util.build_time(unit_type)
        self.death_frame = MODEL_FRAME_LIMIT + 1
        self.creation_frame_known = creation_frame_known  # If true, we know exactly when the unit was created

        # These fields are transient and used while simulating production
        self.has_produced = False
        self.can_produce_at = self.completion_frame
        self.prerequisites_available_frame = creation_frame
        self.shifted_creation_frame = creation_frame

        # These are cached for performance reasons
        self.supply_required = unit_type.supplyRequired()
        self.mineral_price = unit_type.mineralPrice()
        self.gas_price = unit_type.gasPrice()

        _unit_sequence += 1
        self.seq = _unit_sequence  # stands in for the pointer comparison breaking ties in the set ordering


def _unit_key(unit: _EcoModelUnit) -> tuple[int, int, int]:
    return unit.creation_frame, unit.completion_frame, unit.seq


def _sorted_units(*unit_lists: list[_EcoModelUnit]) -> list[_EcoModelUnit]:
    """A multiset of units: all of the given units in (creation, completion, creation order) order."""
    return sorted((unit for unit_list in unit_lists for unit in unit_list), key=_unit_key)


def _empty_array() -> Array:
    return np.zeros(MODEL_FRAME_LIMIT, dtype=np.int64)


_is_enabled = False
_worker_limit = 0
_changed_this_frame = False

_current_upgrade_levels: dict[UpgradeType, int] = {}

_observed_units: list[_EcoModelUnit] = []
_implied_units: list[_EcoModelUnit] = []
_pylon_frames: list[int] = []

_observed_units_by_id: dict[int, _EcoModelUnit] = {}
_observed_units_by_type: dict[UnitType, list[_EcoModelUnit]] = {}
_producers_by_type: dict[UnitType, list[_EcoModelUnit]] = {}

_first_of_type_created: dict[UnitType, int] = {}

_research: list[tuple[UpgradeOrTechType, int]] = []

# These are the main arrays representing our observed state
_minerals = _empty_array()
_gas = _empty_array()
_supply_available = _empty_array()

# These are alternate resource arrays for the early game when we haven't seen the enemy take gas yet
_gas_taken_simulated = False
_minerals_with_taken_gas = _empty_array()
_gas_with_taken_gas = _empty_array()
_supply_available_with_taken_gas = _empty_array()

# Caches for the expensive lookup methods
_worst_case_unit_count_cache: dict[tuple[UnitType, int], tuple[int, int]] = {}
_earliest_unit_production_frame_cache: dict[UnitType, int] = {}


def _ignore_unit(unit_type: UnitType) -> bool:
    # We currently ignore workers and supply providers, just assuming they are built as needed
    return unit_type.isWorker() or unit_type.supplyProvided() > 0


def _simulate_income(minerals: Array, gas: Array, supply_available: Array, override_gas_timing: int = -1) -> None:
    """Assumptions:
    - Workers are built constantly
    - Pylon is built at 8 to support worker production
    - Scout is sent at 10 supply
    - Three workers are moved to gas when an assimilator finishes
    - Enemy stops building workers once they have saturated their resources

    This simulation does not take later pylon building into account after the one at 8 supply, so it will initialize
    supply_available to negative numbers past a certain point, which will be accounted for later.

    We start mineral collection at frame 25 to account for the initial delay in getting the economy started."""
    # At frame 0 we have 4 mineral workers, no gas workers, and are building a worker that started after LF
    mineral_workers = 4
    gas_workers = 0
    remaining_worker_build_time = 300 + bwapi.Broodwar.getLatencyFrames()

    # Find the frames where a refinery finished
    refinery_frames: list[int] = []
    if override_gas_timing > 0:
        refinery_frames.append(override_gas_timing + unit_util.build_time(UnitTypes.Protoss_Assimilator))
    else:
        for unit in _sorted_units(_observed_units):
            if unit.type.isRefinery():
                refinery_frames.append(unit.completion_frame)
    refinery_frames.append(MODEL_FRAME_LIMIT + 1)
    refinery_index = 0

    move_to_gas_frames = [MODEL_FRAME_LIMIT + 1]

    minerals_list = [0] * MODEL_FRAME_LIMIT
    gas_list = [0] * MODEL_FRAME_LIMIT
    supply_list = [0] * MODEL_FRAME_LIMIT
    worker_limit = _worker_limit

    mineral_counter = 0.0
    gas_counter = 0.0
    supply_counter = 8
    for f in range(MODEL_FRAME_LIMIT):
        # If a refinery finished at this frame, move workers from minerals to gas
        if f == refinery_frames[refinery_index]:
            mineral_workers -= 3
            heapq.heappush(move_to_gas_frames, f + _TRANSFER_TO_GAS_FRAMES)
            refinery_index += 1
        if f == move_to_gas_frames[0]:
            gas_workers += 3
            heapq.heappop(move_to_gas_frames)

        # Completed worker goes to minerals
        if remaining_worker_build_time == 0:
            mineral_workers += 1

        # Build a worker
        pending_refineries = len(refinery_frames) - refinery_index - 1
        if (remaining_worker_build_time <= 0 and mineral_counter >= 50
                and (mineral_workers + gas_workers + (len(move_to_gas_frames) - 1) * 3 + pending_refineries * 3)
                < worker_limit
                and (f >= _PYLON_FINISHED or supply_counter >= 2)):
            mineral_counter -= 50
            remaining_worker_build_time = 300
            supply_counter -= 2
        remaining_worker_build_time -= 1

        # Build a pylon at frame 1115
        if f == _PYLON_STARTED:
            mineral_counter -= 100 + _BUILDER_LOSS
        if f == _PYLON_FINISHED:
            supply_counter += 16

        # Send scout at frame 1850
        if f == _SCOUT_SENT:
            mineral_workers -= 1

        # Scout dies at frame 4500
        if f == _SCOUT_DIED:
            supply_counter += 2

        # Set the resources at this frame
        # Simulate a short delay to get going at the start
        if f > 25:
            mineral_counter += mineral_workers * _MINERALS_PER_WORKER_FRAME
            gas_counter += gas_workers * _GAS_PER_WORKER_FRAME
        minerals_list[f] = int(mineral_counter)
        gas_list[f] = int(gas_counter)
        supply_list[f] = supply_counter

    minerals[:] = minerals_list
    gas[:] = gas_list
    supply_available[:] = supply_list


def _spend_resource(resource: Array, amount: int, from_frame: int, to_frame: int = MODEL_FRAME_LIMIT) -> None:
    if amount == 0:
        return
    start = max(0, from_frame)
    if start < to_frame:
        resource[start:to_frame] -= amount


def _scan_back(minerals: Array, gas: Array, mineral_price: int, gas_price: int, high: int, low: int) -> int:
    """`for (f = high; f >= low; f--) if (minerals[f] < mp || gas[f] < gp) break; return f + 1;`"""
    high = min(high, MODEL_FRAME_LIMIT - 1)
    if high < low:
        return high + 1
    start = max(0, low)
    failing = np.flatnonzero((minerals[start:high + 1] < mineral_price) | (gas[start:high + 1] < gas_price))
    if failing.size:
        return start + int(failing[-1]) + 1
    return low


def _scan_back_with_supply(minerals: Array, gas: Array, supply_available: Array, mineral_price: int, gas_price: int,
                           supply_required: int, high: int, low: int) -> tuple[int, int, int]:
    """Scans back from high to low until resources run out, returning the frame after the break and the first
    (highest) and last (lowest) frames scanned where the supply was short (0 if none)."""
    f = _scan_back(minerals, gas, mineral_price, gas_price, high, low)
    high = min(high, MODEL_FRAME_LIMIT - 1)
    start = max(f, 0)
    if start > high:
        return f, 0, 0
    blocked = np.flatnonzero(supply_available[start:high + 1] < supply_required)
    if not blocked.size:
        return f, 0, 0
    return f, start + int(blocked[-1]), start + int(blocked[0])


def _simulate_taking_gas_if_needed(unit_type: UnitType) -> tuple[Array, Array, Array]:
    global _gas_taken_simulated

    # Use normal arrays if the type doesn't require gas or we've seen when the enemy took gas
    if UnitTypes.Protoss_Assimilator in _observed_units_by_type or unit_type.gasPrice() < 2:
        return _minerals, _gas, _supply_available

    # If we've already simulated gas being taken, return the current data
    if _gas_taken_simulated:
        return _minerals_with_taken_gas, _gas_with_taken_gas, _supply_available_with_taken_gas
    _gas_taken_simulated = True

    # Use our scouting information to determine when the opponent could have taken their gas at the earliest
    geyser_last_scouted = _EARLIEST_GAS
    base = game_map.get_enemy_starting_main()
    if base is not None:
        for geyser_resource in base.geysers_or_refineries():
            geyser_last_scouted = max(geyser_last_scouted,
                                      game_map.last_seen(geyser_resource.tile.x, geyser_resource.tile.y))

    # Simulate the income with the different gas timing
    _simulate_income(_minerals_with_taken_gas, _gas_with_taken_gas, _supply_available_with_taken_gas,
                     geyser_last_scouted)
    return _minerals_with_taken_gas, _gas_with_taken_gas, _supply_available_with_taken_gas


def _add_prerequisite(prerequisites: list[tuple[int, UnitType]], unit_type: UnitType, frame: int) -> int:
    if unit_type == UnitTypes.None_:
        return 0

    for unit in _sorted_units(_observed_units_by_type.get(unit_type, [])):
        if frame < 0 or frame < unit.death_frame:
            return unit.completion_frame

    start_frame = frame - unit_util.build_time(unit_type)
    prerequisites.append((start_frame, unit_type))

    return _get_prerequisites(prerequisites, unit_type, start_frame)


def _get_prerequisites(prerequisites: list[tuple[int, UnitType]], unit_type: UnitType, frame: int = 0) -> int:
    if unit_type == UnitTypes.None_:
        return 0

    earliest_frame = 0
    for required_type in unit_type.requiredUnits():
        if not required_type.isBuilding():
            continue
        if required_type.isResourceDepot():
            continue

        earliest_frame = max(earliest_frame, _add_prerequisite(prerequisites, required_type, frame))

    # For units, always include what builds them in case it isn't a part of the dependency tree
    if not unit_type.isBuilding():
        earliest_frame = max(earliest_frame, _add_prerequisite(prerequisites, unit_type.whatBuilds()[0], frame))

    return earliest_frame


def _remove_duplicates(prerequisites: list[tuple[int, UnitType]]) -> None:
    # Start by sorting to get them in the right order, then remove duplicates
    prerequisites.sort(key=lambda entry: (entry[0], entry[1].getID()))
    seen_types: set[UnitType] = set()
    result = []
    for frame, unit_type in prerequisites:
        if unit_type in seen_types:
            continue
        seen_types.add(unit_type)
        result.append((frame, unit_type))
    prerequisites[:] = result


def enabled(frame: int = 0) -> bool:
    """Whether the model is currently enabled. It currently only works for Protoss opponents on one base, so will
    switch to False when this is no longer the case."""
    return _is_enabled and frame < MODEL_FRAME_LIMIT


def initialize() -> None:
    global _is_enabled, _worker_limit, _changed_this_frame

    if bwapi.Broodwar.enemy().getRace() in (Races.Terran, Races.Zerg):
        log.get("Disabling opponent economic model, as opponent is not Protoss")
        _is_enabled = False
        return

    my_main = game_map.get_my_main()
    assert my_main is not None
    _is_enabled = True
    _worker_limit = my_main.mineral_patch_count() * 2 + my_main.geyser_count() * 3
    _changed_this_frame = True
    _observed_units_by_id.clear()
    _observed_units.clear()
    _observed_units_by_type.clear()
    _first_of_type_created.clear()
    _research.clear()

    _current_upgrade_levels.clear()
    for upgrade_type in sorted(UpgradeTypes.allUpgradeTypes(), key=lambda t: t.getID()):
        if upgrade_type.getRace() != Races.Protoss:
            continue
        _current_upgrade_levels[upgrade_type] = 0


def _disable(message: str) -> None:
    global _is_enabled
    log.get(message)
    _is_enabled = False


def update() -> None:
    global _changed_this_frame, _gas_taken_simulated

    if not _is_enabled:
        return

    frame = common.current_frame
    if frame >= MODEL_FRAME_LIMIT - 5000:
        _disable("Disabling opponent economic model, reached frame limit")
        return
    enemy = bwapi.Broodwar.enemy()
    if enemy.getRace() in (Races.Terran, Races.Zerg):
        _disable("Disabling opponent economic model, as opponent is not Protoss")
        return
    if sum(1 for nexus in units.all_enemy_of_type(UnitTypes.Protoss_Nexus) if nexus.completed) > 1:
        _disable("Disabling opponent economic model, as opponent has a completed natural nexus")
        return

    # Detect upgrades
    for upgrade_type, current_level in _current_upgrade_levels.items():
        level = enemy.getUpgradeLevel(upgrade_type)
        if level > current_level:
            log.get(f"Enemy has upgraded {upgrade_type} to {level}")
            _current_upgrade_levels[upgrade_type] = level
            _research.append((UpgradeOrTechType.of(upgrade_type), frame - upgrade_type.upgradeTime(level)))
            _changed_this_frame = True

    if not _changed_this_frame:
        return
    _changed_this_frame = False
    _gas_taken_simulated = False
    _worst_case_unit_count_cache.clear()
    _earliest_unit_production_frame_cache.clear()

    _simulate()


def _simulate() -> None:
    minerals = _minerals
    gas = _gas
    supply_available = _supply_available

    # Compute units we haven't seen that are prerequisites to what we have seen
    _implied_units.clear()
    prerequisites: list[tuple[int, UnitType]] = []
    for unit_type, frame_first_created in sorted(_first_of_type_created.items(), key=lambda e: e[0].getID()):
        _get_prerequisites(prerequisites, unit_type, frame_first_created)
    for upgrade_or_tech_type, research_frame in _research:
        _add_prerequisite(prerequisites, upgrade_or_tech_type.whats_required(), research_frame)
        _add_prerequisite(prerequisites, upgrade_or_tech_type.what_upgrades_or_researches(), research_frame)
    _remove_duplicates(prerequisites)
    for prerequisite_frame, unit_type in prerequisites:
        if prerequisite_frame < 0:
            _disable(f"ERROR: Prerequisite {unit_type} would need to have been built at frame {prerequisite_frame}; "
                     f"assuming this is a test and disabling econ model")
            return

        _implied_units.append(_EcoModelUnit(unit_type, 0, prerequisite_frame))

    all_units = _sorted_units(_observed_units, _implied_units)

    def insert_unit(unit_list: list[_EcoModelUnit], unit: _EcoModelUnit) -> None:
        unit_list.append(unit)
        unit_list.sort(key=_unit_key)

    # If we haven't seen an assimilator, check if we know when the enemy must have gotten it at the latest
    override_gas_timing = -1
    if UnitTypes.Protoss_Assimilator not in _observed_units_by_type:
        gas_frames: list[tuple[int, int]] = []
        for unit in all_units:
            if unit.gas_price > 5:
                gas_frames.append((unit.creation_frame, unit.gas_price))
        for upgrade_or_tech_type, research_frame in _research:
            if upgrade_or_tech_type.gas_price() > 5:
                gas_frames.append((research_frame, upgrade_or_tech_type.gas_price()))
        gas_frames.sort()

        cumulative_gas_needed = 0
        for gas_frame, gas_spent in gas_frames:
            cumulative_gas_needed += gas_spent
            f = gas_frame - to_int(cumulative_gas_needed / (_GAS_PER_WORKER_FRAME * 3))
            if override_gas_timing == -1 or f < override_gas_timing:
                override_gas_timing = f

        if override_gas_timing != -1:
            override_gas_timing -= unit_util.build_time(UnitTypes.Protoss_Assimilator) - _TRANSFER_TO_GAS_FRAMES

            assimilator = _EcoModelUnit(UnitTypes.Protoss_Assimilator, 0, override_gas_timing)
            _implied_units.append(assimilator)
            insert_unit(all_units, assimilator)

    _simulate_income(minerals, gas, supply_available, override_gas_timing)

    # Simulate resource spend for all units
    def spend(unit: _EcoModelUnit) -> None:
        builder_loss = _BUILDER_LOSS if unit.type.isBuilding() and unit.creation_frame < _BUILDER_LOSS_UNTIL else 0
        _spend_resource(minerals, unit.mineral_price + builder_loss, unit.creation_frame)
        _spend_resource(gas, unit.gas_price, unit.creation_frame)
        _spend_resource(supply_available, unit.supply_required, unit.creation_frame)
        if unit.death_frame < MODEL_FRAME_LIMIT:
            _spend_resource(supply_available, -unit.supply_required, unit.death_frame)

            # Refund cancelled buildings
            if unit.type.isBuilding() and unit.completion_frame > unit.death_frame:
                _spend_resource(minerals, int((unit.mineral_price * -3) / 4), unit.death_frame)
                _spend_resource(gas, int((unit.gas_price * -3) / 4), unit.death_frame)

    for unit in all_units:
        spend(unit)

    # Simulate resource spend for upgrades
    # Only considers level 1, which is probably fine given the constraints of the model
    for upgrade_or_tech_type, research_frame in _research:
        _spend_resource(minerals, upgrade_or_tech_type.mineral_price(), research_frame)
        _spend_resource(gas, upgrade_or_tech_type.gas_price(), research_frame)

    # Insert supply wherever needed
    # Keep track of the frames, as we will potentially manipulate this later when simulating production
    pylon_build_time = unit_util.build_time(UnitTypes.Protoss_Pylon)
    pylon_frames = _pylon_frames
    pylon_frames[:] = [_PYLON_STARTED]
    for f in range(_PYLON_FINISHED, MODEL_FRAME_LIMIT):
        if supply_available[f] < 0:
            start_frame = f - pylon_build_time
            _spend_resource(minerals, 100 + (_BUILDER_LOSS if start_frame < _BUILDER_LOSS_UNTIL else 0), start_frame)
            _spend_resource(supply_available, -16, f)
            pylon_frames.append(start_frame)

    # Now simulate production

    # Do an initial scan to initialize the data structures we will use for the search
    # Since the prerequisite availability frame is the same for all units of the same type, keep a cache of what we
    # have computed. Prefill with gateway and zealot since they are given by our assumed early opening.
    prerequisites_available_by_type: dict[UnitType, int] = {
        UnitTypes.Protoss_Gateway: _PYLON_FINISHED,
        UnitTypes.Protoss_Zealot: _PYLON_FINISHED + unit_util.build_time(UnitTypes.Protoss_Gateway),
    }
    _producers_by_type.clear()
    units_by_producer_type: dict[UnitType, list[_EcoModelUnit]] = {}

    for unit in all_units:
        # Determine when this unit type can be started
        if unit.type not in prerequisites_available_by_type:
            if unit.creation_frame_known:
                prerequisites_available_by_type[unit.type] = unit.creation_frame
            else:
                prerequisites_done = 0
                for required_type in unit.type.requiredUnits():
                    if not required_type.isBuilding():
                        continue
                    prerequisites_done = max(prerequisites_done,
                                             prerequisites_available_by_type.setdefault(required_type, 0)
                                             + unit_util.build_time(required_type))

                # Check when we actually had the resources
                prerequisites_available_by_type[unit.type] = _scan_back(
                    minerals, gas, unit.mineral_price, unit.gas_price, unit.creation_frame - 1, prerequisites_done)

        # No longer need to consider tech buildings
        if unit.type.isBuilding() and not unit.type.canProduce():
            continue

        # Add to an appropriate set
        unit.prerequisites_available_frame = prerequisites_available_by_type[unit.type]
        if unit.type.canProduce():
            _producers_by_type.setdefault(unit.type, []).append(unit)
        else:
            units_by_producer_type.setdefault(unit.type.whatBuilds()[0], []).append(unit)

    # Now search for a solution for each producer type giving the fewest number of producers
    # This is a tricky problem because we usually don't know the exact creation frames of what we have observed
    # For now we are assuming that units were created in the order that we observed them
    # This might cause issues though, as we might see a zealot after a dragoon that was produced before the core was
    # finished. Another option could be to track "holes" in producer production that we can fill with later units.
    for producer_type in sorted(units_by_producer_type, key=lambda t: t.getID()):
        producer_units = sorted(units_by_producer_type[producer_type], key=_unit_key)

        # Back up our state so we can roll back whenever we need to add a new implied producer
        mineral_backup = minerals.copy()
        gas_backup = gas.copy()
        supply_backup = supply_available.copy()
        pylon_frames_backup = list(pylon_frames)

        producer_build_time = unit_util.build_time(producer_type)

        while True:
            producers = _producers_by_type.setdefault(producer_type, [])

            # Guard against an endless loop
            if len(producers) > 30:
                _disable("ERROR: Endless loop in opponent economic model; disabling economic model")
                return

            for producer in producers:
                producer.has_produced = False
                producer.can_produce_at = producer.completion_frame
                producer.shifted_creation_frame = producer.creation_frame
            producers.sort(key=_unit_key)
            if not producers:
                break  # Shouldn't happen, since each unit needs its prerequisites

            restart = False
            for unit in producer_units:
                if _simulate_unit_production(unit, producers, producer_build_time, minerals, gas, supply_available,
                                             pylon_frames):
                    continue

                # We have no producer for this unit
                # Add a new one as early as possible

                # Start by restoring our backed-up state
                minerals[:] = mineral_backup
                gas[:] = gas_backup
                supply_available[:] = supply_backup
                pylon_frames[:] = pylon_frames_backup

                # Now find the first frame
                mineral_price = producer_type.mineralPrice() + (
                    _BUILDER_LOSS if unit.creation_frame < _BUILDER_LOSS_UNTIL else 0)
                gas_price = producer_type.gasPrice()
                f = unit.creation_frame
                lowest = prerequisites_available_by_type.setdefault(producer_type, 0)
                while f >= lowest:
                    if minerals[f] < mineral_price:
                        break
                    if gas[f] < gas_price:
                        break
                    if f == _BUILDER_LOSS_UNTIL:
                        mineral_price += _BUILDER_LOSS
                    f -= 1
                f += 1
                if f > unit.creation_frame - producer_build_time:
                    # The simulation couldn't figure it out, which can mean that we've guessed wrong about creation
                    # frames (because of a proxy, for example), or the build is just really weird. Let's just bail
                    # out now to avoid overestimating the enemy's production facilities.
                    log.get("WARNING: Couldn't resolve producers of observed units; aborting economic simulation")
                    break

                producer = _EcoModelUnit(producer_type, 0, f)
                _implied_units.append(producer)
                insert_unit(all_units, producer)
                producers.append(producer)
                spend(producer)
                restart = True
                break

            # All units were handled, or we found a situation we couldn't resolve, so break the loop
            if not restart:
                break


def _simulate_unit_production(unit: _EcoModelUnit, producers: list[_EcoModelUnit], producer_build_time: int,
                              minerals: Array, gas: Array, supply_available: Array, pylon_frames: list[int]) -> bool:
    """Shifts the unit as early as resources, supply and a producer allow. Returns False if no producer could produce
    it."""
    pylon_build_time = unit_util.build_time(UnitTypes.Protoss_Pylon)

    # Reset shifted creation frame as it might have been changed in a previous iteration
    unit.shifted_creation_frame = unit.creation_frame

    def shift_earlier(to_frame: int) -> None:
        if to_frame >= unit.shifted_creation_frame:
            return

        _spend_resource(minerals, unit.mineral_price, to_frame, unit.shifted_creation_frame)
        _spend_resource(gas, unit.gas_price, to_frame, unit.shifted_creation_frame)
        _spend_resource(supply_available, unit.supply_required, to_frame, unit.shifted_creation_frame)

        unit.shifted_creation_frame = to_frame

    # Shift earlier until hitting missing resources or supply
    f, first_supply_block_frame, last_supply_block_frame = _scan_back_with_supply(
        minerals, gas, supply_available, unit.mineral_price, unit.gas_price, unit.supply_required,
        unit.creation_frame - 1, unit.prerequisites_available_frame)

    # Shift as far as we could
    shift_earlier(max(f, first_supply_block_frame + 1))

    # Try to resolve a supply block
    if first_supply_block_frame != 0:
        # Find the earliest pylon that finishes on or after the unit's creation frame
        pylon_index = next((i for i, pylon_frame in enumerate(pylon_frames)
                            if pylon_frame + pylon_build_time >= unit.shifted_creation_frame), len(pylon_frames))
        if pylon_index < len(pylon_frames):
            # Pull both the unit and the pylon back as far as possible
            earliest_pylon_frame = max(unit.prerequisites_available_frame, last_supply_block_frame) - pylon_build_time
            pylon_cost = 100
            if earliest_pylon_frame < _BUILDER_LOSS_UNTIL:
                pylon_cost += _BUILDER_LOSS

            now_minerals = 0
            now_gas = 0
            offset_minerals = pylon_cost
            f = pylon_frames[pylon_index] + pylon_build_time
            while f >= unit.prerequisites_available_frame:
                # When we cross the creation frame (or start there), begin considering the unit itself
                if f == unit.shifted_creation_frame:
                    now_minerals = unit.mineral_price + pylon_cost
                    now_gas = unit.gas_price

                # (Stardust compares the gas needed against minerals here.)
                if minerals[f] < now_minerals:
                    break
                if minerals[f] < now_gas:
                    break
                if minerals[f - pylon_build_time] < offset_minerals:
                    break

                # When we reach the beginning of the supply block, we no longer need to pull the pylon
                if f == last_supply_block_frame:
                    offset_minerals = 0
                if f == earliest_pylon_frame:
                    now_minerals -= pylon_cost
                f -= 1
            f += 1

            # Shift the pylon
            if f < unit.shifted_creation_frame:
                new_start_frame = max(f - pylon_build_time, earliest_pylon_frame)
                _spend_resource(minerals, pylon_cost, new_start_frame, pylon_frames[pylon_index])
                _spend_resource(supply_available, -16, new_start_frame + pylon_build_time,
                                pylon_frames[pylon_index] + pylon_build_time)
                pylon_frames[pylon_index] = new_start_frame
                pylon_frames.sort()

            shift_earlier(f)
        else:
            # There wasn't a pylon to shift, so try creating one instead
            earliest_pylon_frame = unit.prerequisites_available_frame - pylon_build_time
            mineral_cost = 100
            if earliest_pylon_frame < _BUILDER_LOSS_UNTIL:
                mineral_cost += _BUILDER_LOSS
            gas_cost = 0
            f = MODEL_FRAME_LIMIT - 1
            while f >= earliest_pylon_frame:
                if minerals[f] < mineral_cost:
                    break
                if minerals[f] < gas_cost:
                    break

                if f == unit.prerequisites_available_frame:
                    mineral_cost -= unit.mineral_price
                    gas_cost -= unit.gas_price
                if f == unit.shifted_creation_frame:
                    mineral_cost += unit.mineral_price
                    gas_cost += unit.gas_price
                f -= 1
            f += 1
            if f < unit.shifted_creation_frame - pylon_build_time:
                pylon_frames.append(f)
                pylon_frames.sort()
                _spend_resource(minerals, 100 + (_BUILDER_LOSS if f < _BUILDER_LOSS_UNTIL else 0), f)
                _spend_resource(supply_available, -16, f)

                shift_earlier(f + pylon_build_time)

    # Find the earliest producer that can produce this unit
    # For producers that haven't simulated producing anything yet, check if they can be moved earlier than one that has
    earliest_producer: _EcoModelUnit | None = None
    earliest_producer_frame = MODEL_FRAME_LIMIT + 1
    attempted_producer_shift = False
    for producer in producers:
        # If this producer can be moved earlier, try it
        # Only do this for the first one though, as others are redundant
        if not producer.has_produced and not producer.creation_frame_known:
            if attempted_producer_shift:
                continue
            attempted_producer_shift = True

            # Find an earlier frame with enough resources
            earliest_frame = max(unit.shifted_creation_frame - producer_build_time,
                                 producer.prerequisites_available_frame)
            mineral_price = producer.type.mineralPrice() + (
                _BUILDER_LOSS if earliest_frame < _BUILDER_LOSS_UNTIL else 0)
            gas_price = producer.type.gasPrice()
            producer.can_produce_at = _scan_back(minerals, gas, mineral_price, gas_price, producer.creation_frame - 1,
                                                 earliest_frame) + producer_build_time

        if producer.can_produce_at > unit.creation_frame:
            continue
        if producer.can_produce_at > earliest_producer_frame:
            continue

        earliest_producer_frame = producer.can_produce_at
        earliest_producer = producer

    if earliest_producer is None:
        return False

    # Shift the producer earlier if needed
    if (not earliest_producer.has_produced and not earliest_producer.creation_frame_known
            and earliest_producer.can_produce_at < earliest_producer.completion_frame):
        earliest_producer.shifted_creation_frame = earliest_producer.can_produce_at - producer_build_time
        mineral_price = earliest_producer.type.mineralPrice() + (
            _BUILDER_LOSS if earliest_producer.shifted_creation_frame < _BUILDER_LOSS_UNTIL else 0)
        _spend_resource(minerals, mineral_price, earliest_producer.shifted_creation_frame,
                        earliest_producer.creation_frame)
        _spend_resource(gas, earliest_producer.type.gasPrice(), earliest_producer.shifted_creation_frame,
                        earliest_producer.creation_frame)

    production_frame = max(earliest_producer.can_produce_at, unit.shifted_creation_frame)

    # Shift the unit later if the producer isn't available early enough
    if production_frame > unit.shifted_creation_frame:
        _spend_resource(minerals, -unit.mineral_price, unit.shifted_creation_frame, production_frame)
        _spend_resource(gas, -unit.gas_price, unit.shifted_creation_frame, production_frame)
        _spend_resource(supply_available, -unit.supply_required, unit.shifted_creation_frame, production_frame)
        unit.shifted_creation_frame = production_frame

    earliest_producer.can_produce_at = production_frame + unit_util.build_time(unit.type)
    earliest_producer.has_produced = True
    return True


def opponent_unit_created(unit_type: UnitType, unit_id: int, estimated_creation_frame: int,
                          creation_frame_known: bool = False) -> None:
    global _changed_this_frame

    if not _is_enabled:
        return
    if _ignore_unit(unit_type):
        return

    _changed_this_frame = True

    observed_unit = _EcoModelUnit(unit_type, unit_id, estimated_creation_frame, creation_frame_known)
    _observed_units_by_id[unit_id] = observed_unit
    _observed_units.append(observed_unit)
    _observed_units_by_type.setdefault(unit_type, []).append(observed_unit)
    _first_of_type_created[unit_type] = min(_first_of_type_created.get(unit_type, estimated_creation_frame),
                                            estimated_creation_frame)


def opponent_unit_destroyed(unit_type: UnitType, unit_id: int, frame_destroyed: int = -1) -> None:
    global _changed_this_frame

    if not _is_enabled:
        return
    if _ignore_unit(unit_type):
        return

    _changed_this_frame = True

    frame = common.current_frame if frame_destroyed == -1 else frame_destroyed

    observed_unit = _observed_units_by_id.get(unit_id)
    if observed_unit is not None:
        observed_unit.death_frame = frame
        return

    # We don't expect to see a unit die that hasn't already been observed, but handle it gracefully
    log.get(f"Non-observed unit died: {unit_type}#{unit_id}")
    observed_unit = _EcoModelUnit(unit_type, unit_id, frame - unit_util.build_time(unit_type))
    observed_unit.death_frame = frame
    _observed_units_by_id[unit_id] = observed_unit
    _observed_units.append(observed_unit)


def opponent_researched(tech_type: TechType, frame_started: int = -1) -> None:
    global _changed_this_frame

    if not _is_enabled:
        return

    _changed_this_frame = True

    start_frame = common.current_frame - tech_type.researchTime() if frame_started == -1 else frame_started
    _research.append((UpgradeOrTechType.of(tech_type), start_frame))


def opponent_upgraded(upgrade_type: UpgradeType, level: int, frame_started: int) -> None:
    global _changed_this_frame

    if not _is_enabled:
        return

    _changed_this_frame = True

    _current_upgrade_levels[upgrade_type] = level
    _research.append((UpgradeOrTechType.of(upgrade_type), frame_started))


def worst_case_unit_count(unit_type: UnitType, frame: int = -1) -> tuple[int, int]:
    """The worst-case number of the given unit the opponent can have at the given frame (the current frame if not
    specified): the number of units the opponent currently has, and the number they could have in total if they spent
    all resources on that unit type."""
    global _is_enabled

    if not _is_enabled:
        log.get("ERROR: Trying to use opponent economic model when it is not enabled")
        return 0, 0

    cached = _worst_case_unit_count_cache.get((unit_type, frame))
    if cached is not None:
        return cached
    cache_key = (unit_type, frame)

    if frame == -1:
        frame = common.current_frame

    # Our simulation has already pulled all observed units as early as possible and simulated producers we know must
    # have been there. So this just needs to first fill in additional production from the producers, then simulate if
    # the enemy could have built more producers.

    # Start by counting how many of the unit type are alive at the given frame
    current_count = sum(1 for unit in _observed_units
                        if unit.type == unit_type and unit.shifted_creation_frame <= frame and unit.death_frame > frame)

    def create_result(additional_unit_count: int) -> tuple[int, int]:
        result = (current_count, current_count + additional_unit_count)
        _worst_case_unit_count_cache[cache_key] = result
        return result

    # Use our other method to figure out when the first additional unit of this type could be produced
    # This takes prerequisites into account
    # Jump out now if no further units can be produced before the given frame
    earliest_frame = earliest_unit_production_frame(unit_type)
    if earliest_frame > frame:
        return create_result(0)

    additional_unit_count = 0

    producer_type = unit_type.whatBuilds()[0]
    build_time = unit_util.build_time(unit_type)
    producer_build_time = unit_util.build_time(producer_type)
    pylon_build_time = unit_util.build_time(UnitTypes.Protoss_Pylon)

    # Take a copy of the resource arrays, since we might be changing them
    pylon_frames = list(_pylon_frames)
    resources = _simulate_taking_gas_if_needed(unit_type)
    minerals = resources[0].copy()
    gas = resources[1].copy()
    supply_available = resources[2].copy()

    supply_required = unit_type.supplyRequired()
    mineral_price = unit_type.mineralPrice()
    gas_price = unit_type.gasPrice()

    def can_produce_unit_at() -> int:
        """When the unit can next be produced; shifts pylons earlier if needed."""
        resources_available_frame, first_supply_block_frame, last_supply_block_frame = _scan_back_with_supply(
            minerals, gas, supply_available, mineral_price, gas_price, supply_required, MODEL_FRAME_LIMIT - 1,
            earliest_frame)

        if first_supply_block_frame == 0:
            return resources_available_frame

        # Try to resolve a supply block

        # Find the earliest pylon that finishes after the supply block frame
        pylon_index = next((i for i, pylon_frame in enumerate(pylon_frames)
                            if pylon_frame + pylon_build_time > first_supply_block_frame), len(pylon_frames))
        if pylon_index < len(pylon_frames):
            # Pull the pylon back as far as possible, but ensuring we also have resources for the unit
            earliest_pylon_frame = max(resources_available_frame, last_supply_block_frame) - pylon_build_time
            pylon_cost = 100
            if earliest_pylon_frame < _BUILDER_LOSS_UNTIL:
                pylon_cost += _BUILDER_LOSS

            now_minerals = 0
            now_gas = 0
            offset_minerals = pylon_cost
            f = pylon_frames[pylon_index] + pylon_build_time
            while f >= resources_available_frame:
                # When we cross the supply block frame (or start there), begin considering the unit itself
                if f == first_supply_block_frame:
                    now_minerals = mineral_price + pylon_cost
                    now_gas = gas_price

                # (Stardust compares the gas needed against minerals here.)
                if minerals[f] < now_minerals:
                    break
                if minerals[f] < now_gas:
                    break
                if minerals[f - pylon_build_time] < offset_minerals:
                    break

                # When we reach the beginning of the supply block, we no longer need to pull the pylon
                if f == last_supply_block_frame:
                    offset_minerals = 0
                if f == earliest_pylon_frame:
                    now_minerals -= pylon_cost
                f -= 1
            f += 1

            # Shift the pylon
            if f < first_supply_block_frame:
                new_start_frame = max(f - pylon_build_time, earliest_pylon_frame)
                _spend_resource(minerals, pylon_cost, new_start_frame, pylon_frames[pylon_index])
                _spend_resource(supply_available, -16, new_start_frame + pylon_build_time,
                                pylon_frames[pylon_index] + pylon_build_time)
                pylon_frames[pylon_index] = new_start_frame
                pylon_frames.sort()

            return f

        # There wasn't a pylon to shift, so try creating one instead
        earliest_pylon_frame = resources_available_frame - pylon_build_time
        mineral_cost = 100 + mineral_price
        if earliest_pylon_frame < _BUILDER_LOSS_UNTIL:
            mineral_cost += _BUILDER_LOSS
        gas_cost = gas_price
        f = MODEL_FRAME_LIMIT - 1
        while f >= earliest_pylon_frame:
            if minerals[f] < mineral_cost:
                break
            if minerals[f] < gas_cost:
                break

            if f == resources_available_frame:
                mineral_cost -= mineral_price
                gas_cost -= gas_price
            f -= 1
        f += 1
        if f < first_supply_block_frame - pylon_build_time:
            pylon_frames.append(f)
            pylon_frames.sort()
            _spend_resource(minerals, 100 + (_BUILDER_LOSS if f < _BUILDER_LOSS_UNTIL else 0), f)
            _spend_resource(supply_available, -16, f)
        return f + pylon_build_time

    # Frames a producer can produce at
    producer_frames: list[int] = []
    for producer in _producers_by_type.get(producer_type, []):
        if producer.can_produce_at > frame:
            continue
        heapq.heappush(producer_frames, producer.can_produce_at)

    while True:
        # Guard against an endless loop
        if additional_unit_count > 100:
            log.get("ERROR: Endless loop in worstCaseUnitCount; disabling economic model")
            _is_enabled = False
            return current_count, current_count

        earliest_frame = can_produce_unit_at()
        if earliest_frame > frame:
            break

        # Try to get an existing producer that can produce it
        if producer_frames:
            production_frame = max(earliest_frame, heapq.heappop(producer_frames))

            _spend_resource(minerals, mineral_price, production_frame)
            _spend_resource(gas, gas_price, production_frame)
            _spend_resource(supply_available, supply_required, production_frame)

            additional_unit_count += 1

            production_frame += build_time
            if production_frame < frame:
                heapq.heappush(producer_frames, production_frame)
            continue

        # Try to create a new producer that can produce it
        mineral_cost = mineral_price + producer_type.mineralPrice()
        gas_cost = gas_price + producer_type.gasPrice()
        f = MODEL_FRAME_LIMIT - 1
        while f >= earliest_frame - producer_build_time:
            if f == _BUILDER_LOSS_UNTIL:
                mineral_cost += _BUILDER_LOSS

            if minerals[f] < mineral_cost:
                break
            if gas[f] < gas_cost:
                break

            if f == earliest_frame:
                mineral_cost -= mineral_price
                gas_cost -= gas_price
            f -= 1
        f += 1

        production_frame = f + producer_build_time
        if production_frame > frame:
            break

        _spend_resource(minerals, producer_type.mineralPrice(), f)
        _spend_resource(gas, producer_type.gasPrice(), f)

        _spend_resource(minerals, mineral_price, production_frame)
        _spend_resource(gas, gas_price, production_frame)
        _spend_resource(supply_available, supply_required, production_frame)

        additional_unit_count += 1

        production_frame += build_time
        if production_frame < frame:
            heapq.heappush(producer_frames, production_frame)

    return create_result(additional_unit_count)


def minimum_producer_count(producer_type: UnitType) -> int:
    """The minimum number of production facilities the enemy currently has of the given type."""
    if not _is_enabled:
        log.get("ERROR: Trying to use opponent economic model when it is not enabled")
        return 0
    return len(_producers_by_type.get(producer_type, ()))


def has_built(unit_type: UnitType) -> bool:
    """Whether the enemy has built at least one of the given type of unit."""
    if not _is_enabled:
        log.get("ERROR: Trying to use opponent economic model when it is not enabled")
        return False

    if unit_type in _observed_units_by_type:
        return True
    return any(unit.type == unit_type for unit in _implied_units)


def earliest_unit_production_frame(unit_type: UnitType) -> int:
    """The earliest frame the enemy could start building the given unit type."""
    if not _is_enabled:
        log.get("ERROR: Trying to use opponent economic model when it is not enabled")
        return 0

    cached = _earliest_unit_production_frame_cache.get(unit_type)
    if cached is not None:
        return cached

    def create_result(result_frame: int) -> int:
        _earliest_unit_production_frame_cache[unit_type] = result_frame
        return result_frame

    minerals, gas, _ = _simulate_taking_gas_if_needed(unit_type)

    # Get a list of the buildings we need at relative frame offset from building the given unit type
    prerequisites: list[tuple[int, UnitType]] = []
    start_frame = _get_prerequisites(prerequisites, unit_type)
    _remove_duplicates(prerequisites)

    # Now figure out the earliest frames we could produce everything
    mineral_price = unit_type.mineralPrice()
    gas_price = unit_type.gasPrice()

    # Simple case: no prerequisites
    if not prerequisites:
        # Find the first frame scanning backwards where we don't have enough of the resource, then advance one
        return create_result(_scan_back(minerals, gas, mineral_price, gas_price, MODEL_FRAME_LIMIT - 1, start_frame))

    # Get the total resource cost of the needed buildings
    total_mineral_cost = sum(prerequisite_type.mineralPrice() for _, prerequisite_type in prerequisites)
    total_gas_cost = sum(prerequisite_type.gasPrice() for _, prerequisite_type in prerequisites)

    # Compute the frame stops and how much of the resource we need at each one
    frame_stops: list[tuple[int, int, int]] = [(0, total_mineral_cost + mineral_price, total_gas_cost + gas_price)]
    for frame_offset, prerequisite_type in reversed(prerequisites):
        frame_stops.append((frame_offset, total_mineral_cost, total_gas_cost))
        total_mineral_cost -= prerequisite_type.mineralPrice()
        total_gas_cost -= prerequisite_type.gasPrice()

    # Find the frame where we can meet resource requirements at all frame stops
    high = MODEL_FRAME_LIMIT - 1
    if high < start_frame:
        return create_result(high + 1)
    frames = np.arange(start_frame, high + 1, dtype=np.int64)
    failing = np.zeros(len(frames), dtype=bool)
    for frame_offset, mineral_cost, gas_cost in frame_stops:
        stop_frames = frames - frame_offset
        in_window = stop_frames < MODEL_FRAME_LIMIT
        clipped = np.clip(stop_frames, 0, MODEL_FRAME_LIMIT - 1)
        failing |= (stop_frames < 0) | (in_window & ((minerals[clipped] < mineral_cost) | (gas[clipped] < gas_cost)))
    indexes = np.flatnonzero(failing)
    if indexes.size:
        return create_result(int(frames[indexes[-1]]) + 1)
    return create_result(start_frame)


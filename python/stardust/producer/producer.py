"""Port of Producer/Producer.{h,cpp}: schedules production for the strategist's prioritized goals.

The overall objective is, for each production goal, to determine what should be produced next and do so whenever this
does not delay a higher-priority goal. The producer simulates minerals, gas and supply over a prediction window
(2000-4500 frames) and commits items to the earliest frames where their resources, prerequisites, producers, build
locations and supply are available.

Porting notes:
- The resource timelines are numpy arrays, and the frame scans over them are vectorized; each keeps the semantics of
  the corresponding C++ loop (which frame wins when scanning forwards or backwards). Frames outside the window, which
  Stardust would index out of bounds, are clamped.
- ProductionItemSet (a std::multiset ordered by start frame, completion frame and pointer) is a list kept in order of
  (start frame, completion frame, creation order) at insertion. Like the tree, items whose frames are changed in
  place keep their position.
- Producer limits are compared against unsigned sizes in Stardust, so a limit of -1 means no limit.
- The verbose build queue CherryVis output (OUTPUT_BUILD_QUEUE / DEBUG_WRITE_SUBGOALS) is omitted.
"""

from __future__ import annotations

import bisect
import heapq
from typing import TYPE_CHECKING

import numpy as np

import bwapi
from bwapi import TilePositions, UnitCommandTypes, UnitType, UnitTypes
from stardust import common
from stardust.builder import builder, building_placement
from stardust.builder.block import Location
from stardust.builder.building_placement import BuildLocation, BuildLocations, BuildLocationSet, Neighbourhood
from stardust.cpp import INT_MAX, to_int
from stardust.instrumentation import log
from stardust.map.base import Base
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.producer.production_goals.upgrade_production_goal import UpgradeProductionGoal
from stardust.units import units
from stardust.util import unit_util
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType
from stardust.workers import workers
from stardust.workers.worker_gather_optimizer import GAS_PER_WORKER_FRAME, MINERALS_PER_GAS_UNIT, \
    MINERALS_PER_WORKER_FRAME

if TYPE_CHECKING:
    from stardust.builder.building import Building
    from stardust.producer.production_location import ProductionLocation
    from stardust.units.my_unit import MyUnit
    from stardust.units.my_worker import MyWorker

type Type = UnitType | UpgradeOrTechType
type Array = np.ndarray

_predict_frames = 0
_minerals: Array = np.zeros(0, dtype=np.int64)
_gas: Array = np.zeros(0, dtype=np.int64)
_supply: Array = np.zeros(0, dtype=np.int64)
_total_supply: Array = np.zeros(0, dtype=np.int64)
_frames_with_reassignable_mineral_worker: list[int] = []  # a min-heap (std::multiset<int>)
_reassigned_mineral_workers_at_start_frame = 0
_build_locations: BuildLocations = []
_available_geysers: BuildLocationSet = []

_INVALID_BUILD_LOCATION = BuildLocation(Location(TilePositions.Invalid), 0, 0, 0)

_item_sequence = 0


def _build_time(item_type: Type) -> int:
    if isinstance(item_type, UpgradeOrTechType):
        return item_type.upgrade_or_research_time()
    return unit_util.build_time(item_type)


def _at_producer_limit(producer_count: int, producer_limit: int) -> bool:
    """`producers.size() >= producerLimit` with the int converted to size_t, so -1 is no limit."""
    return 0 <= producer_limit <= producer_count


class _ProductionItem:
    __slots__ = ("type", "location", "start_frame", "completion_frame", "producer", "is_prerequisite",
                 "queued_building", "build_location", "reserved_builder", "estimated_worker_movement_time", "seq")

    def __init__(self, item_type: Type, start_frame: int, location: ProductionLocation = None,
                 producer: _Producer | None = None, is_prerequisite: bool = False) -> None:
        global _item_sequence
        self.type = item_type  # Can be a unit or upgrade
        # Can be empty, a neighbourhood, or, optionally for buildings, a specific build location
        self.location = location
        self.start_frame = start_frame
        self.completion_frame = start_frame + _build_time(item_type)

        # Fields applicable to non-buildings
        self.producer = producer

        # Fields applicable to buildings
        self.is_prerequisite = is_prerequisite
        self.queued_building: Building | None = None
        self.build_location = _INVALID_BUILD_LOCATION
        self.reserved_builder: MyWorker | None = None
        self.estimated_worker_movement_time = 0

        _item_sequence += 1
        self.seq = _item_sequence  # stands in for the pointer comparison breaking ties in the set ordering

    @classmethod
    def for_pending_building(cls, queued_building: Building) -> _ProductionItem:
        item = cls(queued_building.type, queued_building.expected_frames_until_started())
        item.completion_frame = queued_building.expected_frames_until_completion()
        item.queued_building = queued_building
        item.build_location = BuildLocation(Location(queued_building.tile), 0, 0, 0)
        return item

    def mineral_price(self) -> int:
        price = (self.type.mineral_price() if isinstance(self.type, UpgradeOrTechType)
                 else self.type.mineralPrice())

        # Buildings require a worker to be taken off minerals for a while, so we estimate the impact
        # In some cases this will be too pessimistic, as we can re-use the same worker for multiple jobs
        return price + to_int(2.0 * self.estimated_worker_movement_time * MINERALS_PER_WORKER_FRAME)

    def gas_price(self) -> int:
        return self.type.gas_price() if isinstance(self.type, UpgradeOrTechType) else self.type.gasPrice()

    def supply_provided(self) -> int:
        return 0 if isinstance(self.type, UpgradeOrTechType) else self.type.supplyProvided()

    def supply_required(self) -> int:
        return 0 if isinstance(self.type, UpgradeOrTechType) else self.type.supplyRequired()

    def is_type(self, unit_type: UnitType) -> bool:
        return isinstance(self.type, UnitType) and self.type == unit_type


def _item_key(item: _ProductionItem) -> tuple[int, int, int]:
    return item.start_frame, item.completion_frame, item.seq


class _ItemSet:
    """The std::multiset of production items: see the module docstring."""

    __slots__ = ("items",)

    def __init__(self) -> None:
        self.items: list[_ProductionItem] = []

    def insert(self, item: _ProductionItem) -> int:
        """Inserts the item and returns its index."""
        index = bisect.bisect_right(self.items, _item_key(item), key=_item_key)
        self.items.insert(index, item)
        return index

    def remove(self, item: _ProductionItem) -> int:
        """Removes the item and returns the index it had."""
        for index, other in enumerate(self.items):
            if other is item:
                del self.items[index]
                return index
        return -1

    def clear(self) -> None:
        self.items.clear()

    def __len__(self) -> int:
        return len(self.items)

    def __bool__(self) -> bool:
        return bool(self.items)

    def __getitem__(self, index: int) -> _ProductionItem:
        return self.items[index]


class _Producer:
    __slots__ = ("available_from", "queued", "existing", "items")

    def __init__(self, available_from: int, queued: _ProductionItem | None = None,
                 existing: MyUnit | None = None) -> None:
        self.available_from = available_from
        self.queued = queued
        self.existing = existing
        self.items = _ItemSet()


_existing_producers: dict[MyUnit, _Producer] = {}
_committed_items = _ItemSet()


def _my_race() -> bwapi.Race:
    return bwapi.Broodwar.self().getRace()


def _last_command_is_recent(producer: MyUnit) -> bool:
    return (common.current_frame - producer.last_command_frame - 1) <= bwapi.Broodwar.getLatencyFrames()


def _bw(unit: MyUnit) -> bwapi.Unit:
    bwapi_unit = unit.bwapi_unit
    assert bwapi_unit is not None
    return bwapi_unit


def _get_remaining_build_time(producer: MyUnit) -> int:
    unit = _bw(producer)
    last_command = unit.getLastCommand()
    if last_command.getType() == UnitCommandTypes.Train and _last_command_is_recent(producer):
        return _build_time(last_command.getUnitType())

    if not unit.isTraining():
        return -1

    return unit.getRemainingTrainTime()


def _get_remaining_upgrade_time(producer: MyUnit) -> int:
    unit = _bw(producer)
    last_command = unit.getLastCommand()
    if last_command.getType() == UnitCommandTypes.Upgrade and _last_command_is_recent(producer):
        return _build_time(UpgradeOrTechType.of(last_command.getUpgradeType()))

    if not unit.isUpgrading():
        return -1

    return unit.getRemainingUpgradeTime()


def _get_remaining_research_time(producer: MyUnit) -> int:
    unit = _bw(producer)
    last_command = unit.getLastCommand()
    if last_command.getType() == UnitCommandTypes.Research and _last_command_is_recent(producer):
        return _build_time(UpgradeOrTechType.of(last_command.getTechType()))

    if not unit.isResearching():
        return -1

    return unit.getRemainingResearchTime()


# ---------------------------------------------------------------------------------------------------------------------
# Resource timelines


def _update_resource_collection(resource: Array, from_frame: int, worker_change: int, base_rate: float) -> None:
    delta = worker_change * base_rate
    start = max(0, from_frame + 1)
    if start >= _predict_frames:
        return
    frames = np.arange(start, _predict_frames, dtype=np.int64)
    resource[start:] += (delta * (frames - from_frame)).astype(np.int64)


def _spend_resource(resource: Array, amount: int, from_frame: int, to_frame: int | None = None) -> None:
    if to_frame is None:
        to_frame = _predict_frames
    start = max(0, from_frame)
    if start < to_frame:
        resource[start:to_frame] -= amount


def _move_resource_spend(resource: Array, amount: int, from_frame: int, delta: int) -> None:
    # If the delta is positive, add back the resource into the future, otherwise subtract it into the past
    if delta > 0:
        _spend_resource(resource, -amount, from_frame, from_frame + delta)
    else:
        _spend_resource(resource, amount, from_frame + delta, from_frame)


def _add_provided_supply(amount: int, from_frame: int, to_frame: int | None = None) -> None:
    if to_frame is None:
        to_frame = _predict_frames

    # Start by adding the supply to the total
    _spend_resource(_total_supply, -amount, from_frame, to_frame)

    # If the final supply does not exceed the max, we can just add the provided supply
    # We only need to check the last frame, as total supply only increases over time
    if _total_supply[min(to_frame, _predict_frames) - 1] <= 400:
        _spend_resource(_supply, -amount, from_frame, to_frame)
        return

    # Add provided supply up to max supply, short-circuiting if at any point we were already maxed before this
    # provider was added
    start = max(0, from_frame)
    end = min(to_frame, _predict_frames)
    if start >= end:
        return
    totals = _total_supply[start:end]
    maxed = np.flatnonzero(totals >= 400 + amount)
    stop = int(maxed[0]) if maxed.size else end - start
    _supply[start:start + stop] += 400 + amount - totals[:stop]


def _move_provided_supply(amount: int, from_frame: int, delta: int) -> None:
    # Moving back is the same as adding supply in the changed frames
    if delta < 0:
        _add_provided_supply(amount, from_frame + delta, from_frame)
        return

    # Moving forward requires us to adjust whenever we capped for max supply earlier

    # Start by removing the total supply
    _spend_resource(_total_supply, amount, from_frame, from_frame + delta)

    # Now loop backwards through the available supply
    for f in range(min(from_frame + delta, _predict_frames) - 1, from_frame - 1, -1):
        # If we were not maxed here, the rest of the frames were not capped and can be handled directly
        if _total_supply[f] <= 400 - amount:
            _spend_resource(_supply, amount, from_frame, f + 1)
            return

        # If we were already maxed here without this provider, move to the next frame
        if _total_supply[f] >= 400:
            continue

        # Remove the supply, respecting the cap
        _supply[f] -= 400 - _total_supply[f]


def _scan_back(resource: Array, needed: int | Array, high: int, low: int) -> int:
    """`for (f = high; f >= low; f--) if (resource[f] < needed) break; return f + 1;`

    needed is a constant or an array of the amount needed at each frame in low..high."""
    high = min(high, _predict_frames - 1)
    if high < low:
        return high + 1
    start = max(0, low)
    below = np.flatnonzero(resource[start:high + 1] < needed)
    if below.size:
        return start + int(below[-1]) + 1
    return low


def _initialize_resources() -> None:
    global _predict_frames, _minerals, _gas, _supply, _total_supply, _reassigned_mineral_workers_at_start_frame
    global _build_locations, _available_geysers

    game = bwapi.Broodwar
    me = game.self()

    # Decide on the length of the prediction window
    # In the early game we use a 4500-frame window, which shrinks to 2000 later on
    _predict_frames = max(2000, min(4500, 12000 - common.current_frame))
    frames = np.arange(_predict_frames, dtype=np.int64)

    # Fill mineral and gas with current rates
    mineral_rate = workers.mineral_workers() * MINERALS_PER_WORKER_FRAME
    gas_workers = workers.gas_workers()
    gas_rate = (gas_workers[0] * GAS_PER_WORKER_FRAME) + (gas_workers[1] * GAS_PER_WORKER_FRAME / 4.0)
    _minerals = me.minerals() + (mineral_rate * frames).astype(np.int64)
    _gas = me.gas() + (gas_rate * frames).astype(np.int64)

    # Fill supply with available and provided supply
    # For provided we use the total of our completed supply providers as we want to store values over the supply limit
    _supply = np.full(_predict_frames, me.supplyTotal() - me.supplyUsed(), dtype=np.int64)
    _total_supply = np.full(_predict_frames,
                            units.count_completed(UnitTypes.Protoss_Nexus) * UnitTypes.Protoss_Nexus.supplyProvided()
                            + units.count_completed(UnitTypes.Protoss_Pylon) * UnitTypes.Protoss_Pylon.supplyProvided(),
                            dtype=np.int64)

    # Assume workers being built will go to minerals
    for unit in units.all_mine():
        if not unit.completed:
            continue
        if not unit.type.isResourceDepot():
            continue

        remaining_train_time = _get_remaining_build_time(unit)
        if remaining_train_time >= 0:
            _update_resource_collection(_minerals, remaining_train_time + 1, 1, MINERALS_PER_WORKER_FRAME)

    # Initialize workers available for transfer
    _frames_with_reassignable_mineral_worker.clear()
    _reassigned_mineral_workers_at_start_frame = 0
    for _ in range(workers.reassignable_mineral_workers()):
        heapq.heappush(_frames_with_reassignable_mineral_worker, 0)

    # Adjust for pending buildings
    # This also adds all pending buildings to the committed item set
    # TODO: Should be able to change our minds and cancel pending buildings that are no longer needed
    refinery_type = _my_race().getRefinery()
    for pending_building in builder.all_pending_buildings():
        # "Commit" the item and spend resources if the building isn't started
        item = _ProductionItem.for_pending_building(pending_building)
        _committed_items.insert(item)
        if not pending_building.is_construction_started():
            _spend_resource(_minerals, pending_building.type.mineralPrice(), item.start_frame)
            _spend_resource(_gas, pending_building.type.gasPrice(), item.start_frame)

        # Supply providers
        if pending_building.type.supplyProvided() > 0:
            _add_provided_supply(pending_building.type.supplyProvided(), item.completion_frame)

        # Refineries are assumed to allow three workers to transfer on completion
        # In certain cases this might not be possible, but for simplicity we assume there will always be three workers
        # available
        if pending_building.type == refinery_type:
            for _ in range(3):
                heapq.heappush(_frames_with_reassignable_mineral_worker, item.completion_frame)

    # Handle mineral reservations
    import stardust.strategist.strategist as strategist
    for amount, desired_frame in strategist.current_mineral_reservations():
        # Scan backwards to find the earliest frame the item can be produced after the desired frame
        frame = _scan_back(_minerals, amount, _predict_frames - 1, desired_frame)

        # Abort now if we never have enough minerals
        if frame >= _predict_frames:
            continue

        # Spend the minerals at the detected frame
        _spend_resource(_minerals, amount, frame)

    _build_locations = [[list(locations) for locations in neighbourhood]
                        for neighbourhood in building_placement.get_build_locations()]
    _available_geysers = list(building_placement.available_geysers())
    _existing_producers.clear()


# ---------------------------------------------------------------------------------------------------------------------
# Prerequisites


def _add_building_if_incomplete(items: _ItemSet, unit_type: UnitType, location: ProductionLocation,
                                producer_type: UnitType, base_frame: int = 0, is_prerequisite: bool = False) -> None:
    if unit_type.isResourceDepot():
        return
    if units.count_completed(unit_type) > 0:
        return

    # If we are already building one, add an item as a placeholder for how much longer it will take to complete it
    # This will ensure any later prerequisites are scheduled correctly
    # The placeholder item will be removed when resolving duplicates
    pending_buildings = builder.pending_buildings_of_type(unit_type)
    if pending_buildings:
        time_to_completion = min(pending_building.expected_frames_until_completion()
                                 for pending_building in pending_buildings)
        items.insert(_ProductionItem(unit_type, base_frame - time_to_completion, None, None, is_prerequisite))
        return

    # If this item is the producer of our target unit, make sure it is in the correct location
    # Otherwise it doesn't matter
    item_location = location if unit_type == producer_type else None

    start_frame = base_frame - unit_util.build_time(unit_type)
    items.insert(_ProductionItem(unit_type, start_frame, item_location, None, is_prerequisite))
    _add_missing_prerequisites(items, unit_type, location, producer_type, start_frame)


def _add_missing_prerequisites(items: _ItemSet, unit_type: UnitType, location: ProductionLocation,
                               producer_type: UnitType, start_frame: int = 0) -> None:
    """For the specified unit type, recursively checks what prerequisites are missing and adds them to the set. The
    frames are relative to the time when the first unit of the desired type can be produced."""
    for required_type, _ in unit_type.requiredUnits().items():
        # Don't include workers or resource depots
        if required_type.isWorker():
            continue
        if required_type.isResourceDepot():
            continue

        _add_building_if_incomplete(items, required_type, location, producer_type, start_frame, True)


def _can_produce_from_item(item_type: Type, location: ProductionLocation, producer_item: _ProductionItem) -> bool:
    """For buildings, whether this building can produce an item at the given location."""
    # Handle case where we want to produce a worker from a specific base
    if isinstance(location, Base):
        if not isinstance(item_type, UnitType) or not item_type.isWorker():
            return True
        return producer_item.build_location.location.tile == location.get_tile_position()

    # Handle case where we want to produce from a specific neighbourhood
    if isinstance(location, Neighbourhood):
        return building_placement.is_in_neighbourhood(producer_item.build_location.location.tile, location)

    return True


def _can_produce_from_unit(item_type: Type, location: ProductionLocation, producer: MyUnit) -> bool:
    """For buildings, whether this building can produce an item at the given location."""
    # Handle case where we want to produce a worker from a specific base
    if isinstance(location, Base):
        if not isinstance(item_type, UnitType) or not item_type.isWorker():
            return True
        return producer is location.resource_depot

    # Handle case where we want to produce from a specific neighbourhood
    if isinstance(location, Neighbourhood):
        return building_placement.is_in_neighbourhood(producer.build_tile, location)

    return True


def _shift_one(items: _ItemSet, item: _ProductionItem, delta: int) -> None:
    """Shifts one item in the given set by the given amount."""
    if delta == 0:
        return

    # Adjust resources

    if item.supply_provided() > 0:
        _move_provided_supply(item.supply_provided(), min(_predict_frames - 1, item.completion_frame), delta)

    if item.gas_price() > 0:
        _move_resource_spend(_gas, item.gas_price(), item.start_frame, delta)

    _move_resource_spend(_minerals, item.mineral_price(), item.start_frame, delta)

    # We assume a refinery causes three workers to move from minerals to gas
    if item.is_type(_my_race().getRefinery()):
        _update_resource_collection(_minerals, item.completion_frame, 3, MINERALS_PER_WORKER_FRAME)
        _update_resource_collection(_gas, item.completion_frame, -3, GAS_PER_WORKER_FRAME)
        _update_resource_collection(_minerals, item.completion_frame + delta, -3, MINERALS_PER_WORKER_FRAME)
        _update_resource_collection(_gas, item.completion_frame + delta, 3, GAS_PER_WORKER_FRAME)

    # We keep the same item, but reinsert it to ensure the set is correctly ordered
    items.remove(item)
    item.start_frame += delta
    item.completion_frame += delta
    items.insert(item)


def _shift_all(items: _ItemSet, start_index: int, delta: int) -> None:
    """Shifts the item at the index and all later items by the given number of frames."""
    if delta < 1 or start_index >= len(items):
        return

    for item in items.items[start_index:]:
        item.start_frame += delta
        item.completion_frame += delta


def _resolve_duplicates(goal_items: _ItemSet) -> int:
    """Removes duplicate items from this goal's set and resolves conflicts with already-committed items. Returns the
    max frame an item in goal_items will finish."""
    max_frame = 0
    seen: set[UnitType] = set()
    index = 0
    while index < len(goal_items):
        item = goal_items[index]

        unit_type = item.type
        if not isinstance(unit_type, UnitType):
            # (Stardust's `continue` here skips the increment, revisiting the same upgrade forever; upgrades are never
            # prerequisites, so it does not happen.)
            index += 1
            continue

        # There's already an item of this type in this goal's queue
        if unit_type in seen:
            del goal_items.items[index]
            continue

        # Check if there is a matching item in the committed item set
        handled = False
        for committed_item in _committed_items.items:
            if not committed_item.is_type(unit_type):
                continue

            # We found a match

            # Compute the difference in completion times
            delta = committed_item.completion_frame - item.completion_frame

            # Remove the item
            del goal_items.items[index]

            # If the committed item completes later, shift all the later items in this goal's queue
            # The rationale for this is that the committed item set is already optimized to produce everything as
            # early as possible, so we know we can't move it earlier
            _shift_all(goal_items, index, delta)

            max_frame = max(max_frame, committed_item.completion_frame)
            handled = True
            break

        # We didn't find a match, so just register this type and continue
        if not handled:
            max_frame = max(max_frame, item.completion_frame)
            seen.add(unit_type)
            index += 1

    return max_frame


# ---------------------------------------------------------------------------------------------------------------------
# Build locations


def _choose_pylon_build_location(pylon: _ProductionItem, tentative: bool = False, required_width: int = 0) -> bool:
    if pylon.build_location.location.tile.isValid():
        return True

    neighbourhood = pylon.location if isinstance(pylon.location, Neighbourhood) else Neighbourhood.ALL_MY_BASES

    pylon_locations = _build_locations[neighbourhood][2]
    if not pylon_locations:
        return False

    # All else being equal, try to keep two of each location type powered
    desired_medium = max(0, 2 - len(_build_locations[neighbourhood][3]))
    desired_large = max(0, 2 - len(_build_locations[neighbourhood][4]))

    # Find the first pylon that meets the requirements

    # Score the locations based on distance and what they will power
    best_score = INT_MAX
    best = -1
    for index, pylon_location in enumerate(pylon_locations):
        # If we have a hard requirement, don't consider anything that doesn't satisfy it
        if ((required_width == 3 and not pylon_location.powers_medium)
                or (required_width == 4 and not pylon_location.powers_large)):
            continue

        score = (max(0, desired_medium - len(pylon_location.powers_medium))
                 + max(0, desired_large - len(pylon_location.powers_large)))
        if score < best_score:
            best_score = score
            best = index

            if best_score == 0:
                break

    # If we found a match, use it
    if best != -1:
        pylon.estimated_worker_movement_time = pylon_locations[best].builder_frames
        if not tentative:
            pylon.build_location = pylon_locations.pop(best)
        return True

    # We couldn't satisfy our hard requirement, so just return the first one
    pylon.estimated_worker_movement_time = pylon_locations[0].builder_frames
    if not tentative:
        pylon.build_location = pylon_locations.pop(0)
    return False


def _reserve_build_positions(items: _ItemSet, commit: bool) -> bool:
    pylon_type = UnitTypes.Protoss_Pylon
    pylon_build_time = unit_util.build_time(pylon_type)

    index = -1
    while True:
        index += 1
        if index >= len(items):
            break
        item = items[index]

        if item.queued_building is not None:
            continue
        if item.build_location.location.tile.isValid():
            continue

        # Choose a tentative build location for pylons that don't already have one
        # It may be made concrete later to account for psi requirements
        if item.is_type(pylon_type):
            _choose_pylon_build_location(item, True)
            continue

        unit_type = item.type
        if not isinstance(unit_type, UnitType):
            continue
        if not unit_type.isBuilding():
            continue
        if not unit_type.requiresPsi():
            continue

        neighbourhood = item.location if isinstance(item.location, Neighbourhood) else Neighbourhood.ALL_MY_BASES
        locations = _build_locations[neighbourhood][unit_type.tileWidth()]

        def first_usable_location() -> int:
            for location_index, location in enumerate(locations):
                if unit_type != UnitTypes.Protoss_Robotics_Facility or location.location.has_exit:
                    return location_index
            return len(locations)

        # Get the frame when the next available build location will be powered
        location_index = first_usable_location()
        available_at = locations[location_index].frames_until_powered if location_index < len(locations) else INT_MAX

        # If there is an available location now, just take it
        if available_at <= item.start_frame:
            # If we are building a Stargate, peek ahead to see if there is a location with lower builder frames
            # Here the "exit" is not really a concern
            if unit_type == UnitTypes.Protoss_Stargate:
                for alternate_index in range(1, len(locations)):
                    alternate = locations[alternate_index]
                    if alternate.frames_until_powered > available_at:
                        break
                    if alternate.builder_frames < locations[location_index].builder_frames:
                        location_index = alternate_index
                        available_at = alternate.frames_until_powered

            item.estimated_worker_movement_time = locations[location_index].builder_frames
            if commit:
                item.build_location = locations.pop(location_index)
            continue

        # Find the earliest committed pylon that has no assigned build location
        pylon: _ProductionItem | None = None
        for committed_item in _committed_items.items:
            if not committed_item.is_type(pylon_type):
                continue
            if committed_item.queued_building is not None:
                continue
            if committed_item.build_location.location.tile.isValid():
                continue

            pylon = committed_item
            break

        if pylon is not None:
            # If the found pylon will complete later than we need it, try to move it earlier
            if pylon.completion_frame > item.start_frame:
                # Try to shift the pylon so that it completes right when we need it
                desired_start_frame = max(0, item.start_frame - pylon_build_time)

                # Determine how far we can move the pylon back and still have minerals
                mineral_frame = _scan_back(_minerals, pylon_type.mineralPrice(), pylon.start_frame - 1,
                                           desired_start_frame)

                # If the new completion frame is worse than the current best, don't bother
                if mineral_frame + pylon_build_time >= available_at:
                    pylon = None

                # Otherwise, make the relevant adjustments and use this pylon
                else:
                    available_at = pylon.completion_frame + (mineral_frame - pylon.start_frame)
                    if commit:
                        _shift_one(_committed_items, pylon, mineral_frame - pylon.start_frame)
        else:
            # Try to queue a new pylon unless we can't beat our current availability frame
            # Because items queued in the builder have the estimated worker travel time included, we make sure to add a
            # buffer so we don't keep queueing pylons thinking they can complete faster
            # TODO: Would be even better to use actual builder times for the new pylon
            if available_at >= (pylon_build_time + 250):
                # Calculate the desired start frame
                desired_start_frame = max(0, item.start_frame - pylon_build_time)

                # Find the frame after the desired start frame where we have minerals
                mineral_frame = _scan_back(_minerals, pylon_type.mineralPrice(), _predict_frames - 1,
                                           desired_start_frame)

                # Add the pylon if it is better than the current best
                if mineral_frame + pylon_build_time < available_at:
                    pylon = _ProductionItem(pylon_type, mineral_frame, item.location)
                    if items.insert(pylon) <= index:
                        index += 1
                    available_at = pylon.completion_frame

        if not commit:
            if pylon is not None and not _choose_pylon_build_location(pylon, True, unit_type.tileWidth()):
                return False
            if available_at == INT_MAX:
                return False
            _shift_all(items, index, available_at - item.start_frame)
            continue

        # If we are creating or moving a pylon, pick a location and add the new build locations
        if pylon is not None:
            _choose_pylon_build_location(pylon, False, unit_type.tileWidth())

            # TODO: The powered after times are wrong here, as the pylon may get moved later
            # Should perhaps look for available build locations within the committed pylon set instead
            # Might make sense to move the build location stuff after minerals, etc. too
            for size, powered in ((3, pylon.build_location.powers_medium), (4, pylon.build_location.powers_large)):
                if not powered:
                    continue
                for powered_build_location in powered:
                    _build_locations[neighbourhood][size].append(BuildLocation(
                        powered_build_location.location,
                        powered_build_location.builder_frames,
                        pylon.completion_frame,
                        powered_build_location.distance_to_exit))
                _build_locations[neighbourhood][size].sort(key=building_placement.build_location_key)

        location_index = first_usable_location()

        # TODO: Cancel everything if there are no build locations
        if location_index == len(locations):
            return False

        # Take the best build location
        item.estimated_worker_movement_time = locations[location_index].builder_frames
        item.build_location = locations.pop(location_index)

        # If it is first powered later, shift the remaining items in the queue
        _shift_all(items, index, item.build_location.frames_until_powered - item.start_frame)

    return True


def _available_at(item_type: Type, start_frame: int, producer: _Producer) -> int:
    """The frame at which a producer is available to build something."""
    build = _build_time(item_type)
    queued = producer.items.items
    queued_index = 0

    frame = max(start_frame, producer.available_from)
    while frame < _predict_frames:
        # If the producer already has queued items, make sure this item can be inserted
        # Otherwise advance the frame to when the item is completed
        if queued_index < len(queued) and frame >= (queued[queued_index].start_frame - build):
            frame = max(frame, queued[queued_index].completion_frame)
            queued_index += 1
            continue

        # The producer is available at this frame
        return frame

    return _predict_frames


def _normalize_start_frame(items: _ItemSet) -> None:
    """When prerequisites are involved, the start frames will initially be negative. This shifts all of the frames in
    the set forward so they start at 0."""
    if not items:
        return
    offset = -items[0].start_frame
    for item in items.items:
        item.start_frame += offset
        item.completion_frame += offset


# ---------------------------------------------------------------------------------------------------------------------
# Resource blocks


def _include_gas_cost_in_minerals(prerequisite_items: _ItemSet) -> bool:
    """Whether we haven't taken gas yet and the prerequisites include a cybernetics core, in which case the minerals
    lost by shifting workers to mine gas count towards the mineral cost."""
    if not _available_geysers or units.count_all(UnitTypes.Protoss_Assimilator) != 0:
        return False
    return any(prerequisite.is_prerequisite and prerequisite.is_type(UnitTypes.Protoss_Cybernetics_Core)
               for prerequisite in prerequisite_items.items)


def _first_failing_frame(resource: Array, stops: list[tuple[int, int]], low: int, high: int,
                         last: bool) -> int | None:
    """For f in low..high, f fails if, for any (offset, needed) stop, frame f - offset is before the window or has
    less than needed (frames after the window are skipped). Returns the last (or first) failing f, or None."""
    if high < low:
        return None
    frames = np.arange(low, high + 1, dtype=np.int64)
    failing = np.zeros(len(frames), dtype=bool)
    for offset, needed in stops:
        frame = frames - offset
        in_window = frame < _predict_frames
        clipped = np.clip(frame, 0, _predict_frames - 1)
        failing |= (frame < 0) | (in_window & (resource[clipped] < needed))
    indexes = np.flatnonzero(failing)
    if not indexes.size:
        return None
    return low + int(indexes[-1] if last else indexes[0])


def _frame_when_resources_met(item: _ProductionItem, prerequisite_items: _ItemSet, is_minerals: bool) -> int:
    """The frame where we have enough of a resource for the item and its prerequisites."""
    resource = _minerals if is_minerals else _gas

    def cost(production_item: _ProductionItem) -> int:
        return production_item.mineral_price() if is_minerals else production_item.gas_price()

    item_cost = cost(item)

    # Simple case: no prerequisites
    if not prerequisite_items:
        # Find the first frame scanning backwards where we don't have enough of the resource, then advance one
        return _scan_back(resource, item_cost, _predict_frames - 1, item.start_frame)

    # If we haven't taken gas yet and the prerequisite items list includes a cybernetics core, we need to include the
    # mineral cost of the gas (i.e. minerals lost by shifting workers to mine gas)
    include_gas_cost_in_minerals = is_minerals and _include_gas_cost_in_minerals(prerequisite_items)

    def extra_cost(prerequisite: _ProductionItem) -> int:
        if not include_gas_cost_in_minerals:
            return 0
        extra = to_int(MINERALS_PER_GAS_UNIT * prerequisite.gas_price())

        # Include the price of the assimilator with the cybernetics core
        if prerequisite.is_prerequisite and prerequisite.is_type(UnitTypes.Protoss_Cybernetics_Core):
            extra += UnitTypes.Protoss_Assimilator.mineralPrice()
        return extra

    # Get the total resource cost of the prerequisites
    prerequisite_cost = sum(cost(prerequisite) + extra_cost(prerequisite) for prerequisite in prerequisite_items.items)

    # Precompute the frame stops and how much of the resource we need at each one
    frame_stops_and_resource_needed = [(0, prerequisite_cost + item_cost)]
    for prerequisite in reversed(prerequisite_items.items):
        prerequisite_resource_cost = cost(prerequisite)
        if prerequisite_resource_cost > 0:
            frame_stops_and_resource_needed.append((item.start_frame - prerequisite.start_frame, prerequisite_cost))
            prerequisite_cost -= prerequisite_resource_cost + extra_cost(prerequisite)

    # Scan backwards, breaking whenever we don't have enough at any of the stops
    if item.start_frame > _predict_frames - 1:
        return _predict_frames
    failing = _first_failing_frame(resource, frame_stops_and_resource_needed, item.start_frame, _predict_frames - 1,
                                   last=True)
    return item.start_frame if failing is None else failing + 1


def _shift_for_minerals(item: _ProductionItem, prerequisite_items: _ItemSet) -> bool:
    mineral_cost = item.mineral_price()

    # Find the frame where we have enough minerals from that point forward
    f = _frame_when_resources_met(item, prerequisite_items, True)

    # Workers are a special case, as they start producing income after they are completed
    # So for them we just need enough minerals to cover the period until they have recovered their investment
    if f > item.start_frame and item.is_type(UnitTypes.Protoss_Probe):
        f = _worker_payback_frame(item.start_frame, mineral_cost, _build_time(item.type))

    # If we can't ever produce this item, return false
    if f == _predict_frames:
        return False

    # If we can't produce the item immediately, shift the start frame for this and all remaining items
    # Since we are preserving the relative order, we don't need to reinsert anything into the set
    delta = f - item.start_frame
    if delta > 0:
        item.start_frame += delta
        item.completion_frame += delta
        _shift_all(prerequisite_items, 0, delta)
    return True


def _worker_payback_frame(start_frame: int, mineral_cost: int, time_to_build: int) -> int:
    """For a worker, the last frame from which we can't cover its cost until it has mined it back. Stardust's loop:

        f = startFrame;
        for (i = startFrame; i < PREDICT_FRAMES; i++) {
            required = mineralCost - (miningTime = i - f - timeToBuild) > 0 ? (int)(miningTime * rate) : 0;
            if (required <= 0) break;
            if (minerals[i] < required) f = i;
        }

    This does the same with numpy. Two things keep it fast: once the worker has mined for payback_time frames the
    loop always ends, so only frames up to f + timeToBuild + payback_time are looked at; and until f + timeToBuild
    the requirement is the full cost, so a run of frames short of it, each within timeToBuild of the last, moves f
    to the end of the run in one step."""
    f = start_frame
    if mineral_cost <= 0:
        return f

    # The first mining time at which the worker has paid for itself (int(m * rate) is nondecreasing in m)
    payback_time = max(1, int(mineral_cost / MINERALS_PER_WORKER_FRAME) - 1)
    while int(payback_time * MINERALS_PER_WORKER_FRAME) >= mineral_cost and payback_time > 1:
        payback_time -= 1
    while int(payback_time * MINERALS_PER_WORKER_FRAME) < mineral_cost:
        payback_time += 1

    i = start_frame
    while i < _predict_frames:
        end = min(_predict_frames, f + time_to_build + payback_time)
        if i >= end:
            break
        lo = max(0, i)
        mining_time = np.arange(lo - f - time_to_build, end - f - time_to_build, dtype=np.int64)
        required = mineral_cost - np.where(mining_time > 0,
                                           (mining_time * MINERALS_PER_WORKER_FRAME).astype(np.int64), 0)
        short = np.flatnonzero(_minerals[lo:end] < required)
        if not short.size:
            break
        f = lo + int(short[0])

        # Follow the run of frames short of the full cost that are each within timeToBuild of the previous one
        after = np.flatnonzero(_minerals[f + 1:] < mineral_cost)
        if after.size:
            gaps = np.diff(after, prepend=-1)
            breaks = np.flatnonzero(gaps > time_to_build)
            run_end = int(breaks[0]) - 1 if breaks.size else after.size - 1
            if run_end >= 0:
                f = f + 1 + int(after[run_end])
        i = f + 1
    return f


def _mineral_block_frame(reassign_frame: int) -> int:
    """`for (f = P - 1; f > reassignFrame; f--) if (minerals[f] < (int)((f - reassignFrame) * rate)) break;`"""
    if reassign_frame >= _predict_frames - 1:
        return _predict_frames - 1
    start = max(0, reassign_frame + 1)
    frames = np.arange(start, _predict_frames, dtype=np.int64)
    minerals_needed = ((frames - reassign_frame) * MINERALS_PER_WORKER_FRAME).astype(np.int64)
    below = np.flatnonzero(_minerals[start:] < minerals_needed)
    return start + int(below[-1]) if below.size else reassign_frame


def _gas_deficit(gas_cost: int, start_frame: int) -> tuple[int, int]:
    """Scanning backwards from the end of the window to the start frame, the lowest frame where we need to start
    collecting with 3 more workers to cover the gas deficit, and that deficit; (P, 0) if there is no deficit."""
    gas_deficit_frame = _predict_frames
    gas_deficit = 0
    start = max(0, start_frame)
    if start < _predict_frames:
        deficits = gas_cost - _gas[start:]
        positive = np.flatnonzero(deficits > 0)
        if positive.size:
            # Approximation avoiding floating-point division
            deficit_frames = (start + positive) - ((deficits[positive] * 75) >> 4)
            lowest = int(deficit_frames.min())
            if lowest < gas_deficit_frame:
                # Scanning backwards, the first (latest) frame reaching the lowest deficit frame wins
                chosen = int(np.flatnonzero(deficit_frames == lowest)[-1])
                gas_deficit_frame = lowest
                gas_deficit = int(deficits[positive[chosen]])
    return gas_deficit_frame, gas_deficit


def _refinery_start_frame(desired_start_frame: int, end_frame: int, price: int, build_time: int,
                          existing_start_frame: int | None) -> int:
    """Stardust's scan for the frame a refinery can start at, from the desired frame: one past the last frame f
    before end_frame that fails either check, where actual is that value so far:

        if (minerals[f] < price) { actual = f + 1; continue; }
        needed = (int)(3.0 * (f - actual) * MINERALS_PER_WORKER_FRAME) + extra;
        if (minerals[f + buildTime] < needed) { actual = f + 1; continue; }

    extra is the price when adding a refinery (existing_start_frame None), and when moving one, the price only if
    the refinery would complete before its existing start frame.

    This does the same with numpy. Right after a failure, the frame is checked with nothing extra mined (f == actual),
    so a run of frames failing that is skipped in one step; then the next failure is found with the mined amount
    growing from there."""
    actual = desired_start_frame
    if desired_start_frame >= end_frame:
        return actual

    frames = np.arange(desired_start_frame, end_frame, dtype=np.int64)
    start_ok = _minerals[desired_start_frame:end_frame] >= price
    at_completion = _minerals[desired_start_frame + build_time:end_frame + build_time]
    if existing_start_frame is None:
        extra = np.full(len(frames), price, dtype=np.int64)
    else:
        extra = np.where(frames + build_time < existing_start_frame, price, 0)
    passes_fresh = start_ok & (at_completion >= extra)

    while actual < end_frame:
        # Frames failing right after a failure (or at the desired frame)
        i = actual - desired_start_frame
        passing = np.flatnonzero(passes_fresh[i:])
        if not passing.size:
            return end_frame
        actual += int(passing[0])

        # The next failure after that, with the mined amount growing from actual
        i = actual - desired_start_frame + 1
        mined = (3.0 * np.arange(1, end_frame - actual, dtype=np.int64) * MINERALS_PER_WORKER_FRAME).astype(np.int64)
        failing = np.flatnonzero(~start_ok[i:] | (at_completion[i:] < extra[i:] + mined))
        if not failing.size:
            return actual
        actual += int(failing[0]) + 2
    return actual


def _gas_deficit_with_prerequisites(gas_cost: int, prerequisite_cost: int, start_frame: int,
                                    reversed_prerequisites: list[tuple[int, int]]) -> tuple[int, int]:
    """Stardust's backwards scan for the gas deficit of an item and its prerequisites, given as (start frame, gas
    price) in reverse order. Returns (P, 0) if there is no deficit. Stardust's loop:

        gasNeeded = prerequisiteCost + gasCost;
        for (f = P - 1; f >= 0; f--) {
            deficit = gasNeeded - gas[f];
            if (deficit > 0 && f - ((deficit * 75) >> 4) < gasDeficitFrame) {
                gasDeficitFrame = f - ((deficit * 75) >> 4); gasDeficit = deficit;
            }
            if (f == item.startFrame) gasNeeded -= gasCost;
            while (next prerequisite's startFrame == f) gasNeeded -= its gas price;
            if (gasNeeded == 0) break;
        }

    This does the same with numpy: it works out at which frames the amounts come off, and so how much is needed at
    each frame the loop looks at, then picks the lowest deficit frame (the first found, i.e. latest f, on ties)."""
    p = _predict_frames
    if p <= 0:
        return p, 0

    # The amount that comes off at each frame, after that frame's deficit check
    decrements = np.zeros(p, dtype=np.int64)
    if 0 <= start_frame < p:
        decrements[start_frame] += gas_cost

    # Prerequisites come off in order as f counts down: each one at its start frame if the loop gets there, which it
    # doesn't if that's above the previous one's frame (or off the window), and then neither do the rest
    reachable = p - 1
    for prerequisite_start, prerequisite_gas in reversed_prerequisites:
        if not 0 <= prerequisite_start <= reachable:
            break
        decrements[prerequisite_start] += prerequisite_gas
        reachable = prerequisite_start

    # needed[f]: gas needed at f's check, i.e. the total less everything that came off at frames above f
    came_off_above = np.concatenate((np.cumsum(decrements[::-1])[::-1][1:], [0]))
    needed = (prerequisite_cost + gas_cost) - came_off_above

    # The loop stops after the (highest) frame where nothing is needed any more
    done = np.flatnonzero(needed - decrements == 0)
    lowest_frame = int(done[-1]) if done.size else 0

    deficits = needed[lowest_frame:] - _gas[lowest_frame:]
    positive = np.flatnonzero(deficits > 0)
    if not positive.size:
        return p, 0
    deficit_frames = (lowest_frame + positive) - ((deficits[positive] * 75) >> 4)  # Approximation avoiding division
    lowest = int(deficit_frames.min())
    chosen = int(np.flatnonzero(deficit_frames == lowest)[-1])
    return lowest, int(deficits[positive[chosen]])


def _supply_block_frames(start_frame: int, supply_required: int) -> list[int] | None:
    """The first frame of each run of frames from the start frame where we lack the supply, or None if any of them
    is at max supply."""
    start = max(0, start_frame)
    if start >= _predict_frames:
        return []
    blocked = _supply[start:] < supply_required
    if np.any(blocked & (_total_supply[start:] >= 400)):
        return None
    block_starts = np.flatnonzero(blocked & ~np.concatenate(([False], blocked[:-1])))
    return [start + int(frame) for frame in block_starts]


def _push_reassignable_workers(frame: int) -> None:
    for _ in range(3):
        heapq.heappush(_frames_with_reassignable_mineral_worker, frame)


def _shift_for_gas(item: _ProductionItem, prerequisite_items: _ItemSet, commit: bool) -> bool:
    global _reassigned_mineral_workers_at_start_frame

    # Before resolving a gas block, check if we have a cybernetics core as a prerequisite
    # If so, insert an assimilator immediately at the same time unless we already have one
    if _available_geysers and units.count_all(UnitTypes.Protoss_Assimilator) == 0:
        for prerequisite_item in prerequisite_items.items:
            if prerequisite_item.is_prerequisite and prerequisite_item.is_type(UnitTypes.Protoss_Cybernetics_Core):
                # Spend minerals
                _spend_resource(_minerals, UnitTypes.Protoss_Assimilator.mineralPrice(), prerequisite_item.start_frame)

                # Update mineral and gas collection
                _push_reassignable_workers(prerequisite_item.start_frame
                                           + unit_util.build_time(UnitTypes.Protoss_Assimilator))

                # Commit
                geyser = _ProductionItem(UnitTypes.Protoss_Assimilator, prerequisite_item.start_frame)
                _committed_items.insert(geyser)
                geyser.build_location = _available_geysers.pop(0)

                # There will be only one
                break

    # Get the total gas cost of the prerequisites
    prerequisite_cost = sum(prerequisite.gas_price() for prerequisite in prerequisite_items.items)

    # Jump out now if the item doesn't require gas
    if item.gas_price() == 0 and prerequisite_cost == 0:
        return True

    # Precompute the frame stops and how much gas we need at each one
    prerequisite_cost_remaining = prerequisite_cost
    frame_stops_and_gas_needed = [(0, prerequisite_cost_remaining + item.gas_price())]
    for prerequisite in reversed(prerequisite_items.items):
        gas_cost = prerequisite.gas_price()
        if gas_cost > 0:
            frame_stops_and_gas_needed.append((item.start_frame - prerequisite.start_frame,
                                               prerequisite_cost_remaining))
            prerequisite_cost_remaining -= gas_cost

    # While we have workers we can assign to gas, repeatedly assign one of them to resolve any gas blocks
    while _frames_with_reassignable_mineral_worker:
        # Find the earliest gas block
        failing = _first_failing_frame(_gas, frame_stops_and_gas_needed, item.start_frame, _predict_frames - 1,
                                       last=False)
        gas_block_frame = _predict_frames if failing is None else failing + 1

        # If there is no gas block, return immediately
        if gas_block_frame == _predict_frames:
            return True

        # Get the first frame where we have a worker we can reassign
        reassign_frame = _frames_with_reassignable_mineral_worker[0]

        # Find the first frame where we have enough minerals to reassign it
        mineral_block_frame = _mineral_block_frame(reassign_frame)

        # If we never have enough minerals, abort here, as we can't produce the item
        if mineral_block_frame == _predict_frames - 1:
            return False

        # If this is too late to help resolve the gas block, shift the item
        delta = mineral_block_frame - gas_block_frame
        if delta > 0:
            item.start_frame += delta
            item.completion_frame += delta
            _shift_all(prerequisite_items, 0, delta)

        # Move the worker from minerals to gas
        # Note that this does not take refineries into account that require 4 workers - we will overestimate gas
        # collection slightly
        _update_resource_collection(_minerals, mineral_block_frame, -1, MINERALS_PER_WORKER_FRAME)
        _update_resource_collection(_gas, mineral_block_frame, 1, GAS_PER_WORKER_FRAME)

        # If it is frame 0, record this
        if mineral_block_frame == 0:
            _reassigned_mineral_workers_at_start_frame += 1

        # Remove the reassignable worker
        heapq.heappop(_frames_with_reassignable_mineral_worker)

    # Determine the frame where we need 3 additional gas workers to resolve a gas deficit
    gas_cost = item.gas_price()
    gas_deficit = 0
    gas_deficit_frame = _predict_frames

    # Simple case: no prerequisites, or the prerequisites require no gas
    if prerequisite_cost == 0:
        if gas_cost == 0:
            return True
        gas_deficit_frame, gas_deficit = _gas_deficit(gas_cost, item.start_frame)
    else:
        # There are prerequisites that require gas
        # Get the gas deficit if we produce the item and prerequisites on schedule
        gas_deficit_frame, gas_deficit = _gas_deficit_with_prerequisites(
            gas_cost, prerequisite_cost, item.start_frame,
            [(prerequisite.start_frame, prerequisite.gas_price())
             for prerequisite in reversed(prerequisite_items.items)])

    # If we have enough gas now, just return
    if gas_deficit == 0 or gas_deficit_frame == _predict_frames:
        return True

    # We are gas blocked, so there are three options:
    # - Shift one or more queued refineries earlier
    # - Add a refinery
    # - Shift the start frame of this item and the prerequisites to where we have gas
    # TODO: Also adjust number of gas workers
    refinery_type = _my_race().getRefinery()
    refinery_build_time = unit_util.build_time(refinery_type)

    # Compute how many frames of 3 workers collecting gas is needed to resolve the deficit
    gas_frames_needed = to_int(gas_deficit / (GAS_PER_WORKER_FRAME * 3))

    # Look for refineries we can move earlier
    refinery_index = 0
    while gas_frames_needed > 0 and refinery_index < len(_committed_items):
        refinery = _committed_items[refinery_index]
        if not refinery.is_type(refinery_type) or refinery.queued_building is not None:
            refinery_index += 1
            continue

        # What frame do we want to move this refinery back to?
        # Will either be gas_frames_needed frames before the current start frame (if it is already completing before
        # the block), or the build time of a refinery before the frame where we have the gas deficit
        desired_start_frame = max(0, min(refinery.start_frame - gas_frames_needed,
                                         gas_deficit_frame - refinery_build_time))

        # Find the actual frame we can move it to
        # We check two things:
        # - We have enough minerals to start the refinery at its new start frame
        # - We have enough minerals after the refinery completes and workers are reassigned
        actual_start_frame = _refinery_start_frame(
            desired_start_frame, min(refinery.start_frame, _predict_frames - refinery_build_time),
            refinery_type.mineralPrice(), refinery_build_time, refinery.start_frame)

        # If we could move it back, make the relevant adjustment
        delta = refinery.start_frame - actual_start_frame
        if delta > 0:
            completion_frame = actual_start_frame + refinery_build_time

            # Reduce the needed gas frames if the move helped
            if refinery.completion_frame < (gas_deficit_frame + gas_frames_needed):
                gas_frames_needed -= delta
            elif completion_frame < (gas_deficit_frame + gas_frames_needed):
                gas_frames_needed -= gas_deficit_frame + gas_frames_needed - completion_frame

            if commit:
                _shift_one(_committed_items, refinery, -delta)

                # Restart the iteration as the iterator may have been invalidated
                refinery_index = 0
                continue

        refinery_index += 1

    # If we've resolved the gas block, return
    if gas_frames_needed <= 0:
        return True

    # Add a refinery if possible
    if _available_geysers:
        # Ideal timing makes enough gas available to build the current item
        desired_start_frame = max(0, gas_deficit_frame - refinery_build_time)

        # Find the actual frame we can start it at
        # We check two things:
        # - We have enough minerals to start the refinery
        # - We have enough minerals after the refinery completes and workers are reassigned
        actual_start_frame = _refinery_start_frame(desired_start_frame, _predict_frames - refinery_build_time,
                                                   refinery_type.mineralPrice(), refinery_build_time, None)

        # Queue it if it was possible to build
        if actual_start_frame < _predict_frames:
            completion_frame = actual_start_frame + refinery_build_time

            if commit:
                # Spend minerals
                _spend_resource(_minerals, refinery_type.mineralPrice(), actual_start_frame)

                # Update mineral and gas collection
                _update_resource_collection(_minerals, completion_frame, -3, MINERALS_PER_WORKER_FRAME)
                _update_resource_collection(_gas, completion_frame, 3, GAS_PER_WORKER_FRAME)

                # Commit
                geyser = _ProductionItem(refinery_type, actual_start_frame)
                _committed_items.insert(geyser)
                geyser.build_location = _available_geysers.pop(0)

            # Reduce the needed gas frames as appropriate
            if completion_frame < (gas_deficit_frame + gas_frames_needed):
                gas_frames_needed -= gas_deficit_frame + gas_frames_needed - completion_frame

    # If we've resolved the gas block, return
    if gas_frames_needed <= 0:
        return True

    # Find the frame where we have enough gas
    f = _frame_when_resources_met(item, prerequisite_items, False)
    if f == _predict_frames:
        return False

    delta = f - item.start_frame
    if delta > 0:
        item.start_frame += delta
        item.completion_frame += delta
        _shift_all(prerequisite_items, 0, delta)

    return True


def _shift_for_supply(item: _ProductionItem, prerequisite_items: _ItemSet, commit: bool) -> bool:
    supply_required = item.supply_required()
    if supply_required == 0:
        return True

    pylon_type = UnitTypes.Protoss_Pylon
    pylon_build_time = unit_util.build_time(pylon_type)

    # Gather the frames where we will create a supply block by producing this item
    supply_block_frames = _supply_block_frames(item.start_frame, supply_required)

    # Break out if we are at max supply - this is a supply block we cannot fix
    if supply_block_frames is None:
        return False

    # If there are no blocks, return now
    if not supply_block_frames:
        return True

    def needed_with_item(base_cost: int, low: int, high: int) -> Array:
        """The minerals needed at each frame from low to high when scanning backwards: base_cost plus the item's price,
        which is no longer needed below the item's start frame once the scan has passed it."""
        frames = np.arange(max(0, low), max(max(0, low), min(high, _predict_frames - 1) + 1), dtype=np.int64)
        without_item = (frames < item.start_frame) & (item.start_frame <= high)
        return np.where(without_item, base_cost, base_cost + item.mineral_price())

    # Try to resolve each supply block
    for f in supply_block_frames:
        # Look for an existing pylon active at this time
        # We are currently ignoring nexuses as they are not primarily built for supply, and we want our producer to
        # queue pylons while a nexus is building when it is appropriate
        # TODO This probably introduces some issues when a nexus is about to complete
        supply_provider: _ProductionItem | None = None
        supply_provider_queued = False
        for potential_supply_provider in _committed_items.items:
            # Ignore anything that isn't a pylon or completes before the block
            if not potential_supply_provider.is_type(pylon_type):
                continue
            if potential_supply_provider.completion_frame <= f:
                continue

            # If a pylon is already being built, push the item until the completion frame
            # Rationale is that we can't move it earlier and can't build a new one faster
            if potential_supply_provider.queued_building is not None:
                if potential_supply_provider.completion_frame >= _predict_frames:
                    return False

                delta = potential_supply_provider.completion_frame - item.start_frame
                item.start_frame += delta
                item.completion_frame += delta
                _shift_all(prerequisite_items, 0, delta)

                supply_provider_queued = True
                break

            supply_provider = potential_supply_provider
            break
        if supply_provider_queued:
            continue

        # If there is one, attempt to move it earlier
        if supply_provider is not None:
            # Try to shift the supply provider so that it completes at the time of the block
            desired_start_frame = max(0, f - _build_time(supply_provider.type))

            # Determine how far we can move the supply provider back and still have minerals
            high = supply_provider.start_frame - 1
            mineral_frame = _scan_back(_minerals,
                                       needed_with_item(supply_provider.mineral_price(), desired_start_frame, high),
                                       high, desired_start_frame)

            pylon_delta = mineral_frame - supply_provider.start_frame

            # If the supply provider couldn't be moved back to resolve the block completely, shift the item
            if mineral_frame > desired_start_frame:
                item_delta = supply_provider.completion_frame + pylon_delta - item.start_frame
                item.start_frame += item_delta
                item.completion_frame += item_delta

                # Break out if the start frame goes outside the window
                if item.start_frame >= _predict_frames:
                    return False

                _shift_all(prerequisite_items, 0, item_delta)

            # If we could move the pylon, make the relevant adjustments
            if pylon_delta < 0 and commit:
                _shift_one(_committed_items, supply_provider, pylon_delta)

            # Continue to the next supply block
            continue

        # There was no active supply provider to move
        # Let's see when we have the resources to queue a new one

        # Find the frame when we have enough minerals to produce the pylon
        desired_start_frame = max(0, f - pylon_build_time)
        mineral_frame = _scan_back(_minerals,
                                   needed_with_item(pylon_type.mineralPrice(), desired_start_frame,
                                                    _predict_frames - 1),
                                   _predict_frames - 1, desired_start_frame)

        # If the pylon can't complete within our prediction window, don't produce the item
        completion_frame = mineral_frame + pylon_build_time
        if completion_frame >= _predict_frames:
            return False

        # (Stardust has a disabled experiment here for delaying the previous item to resolve the block earlier.)

        # Queue the supply provider
        if commit:
            _committed_items.insert(_ProductionItem(pylon_type, mineral_frame))
            _add_provided_supply(pylon_type.supplyProvided(), completion_frame)
            _spend_resource(_minerals, pylon_type.mineralPrice(), mineral_frame)

        # If the block could not be resolved completely, shift the item to start when the supply provider completes
        if mineral_frame > desired_start_frame:
            delta = completion_frame - item.start_frame
            item.start_frame += delta
            item.completion_frame += delta
            _shift_all(prerequisite_items, 0, delta)

        # Queueing a pylon always resolves all later supply blocks
        return True

    return True


def _commit_item(item: _ProductionItem) -> None:
    # Spend the resources
    _spend_resource(_minerals, item.mineral_price(), item.start_frame)
    if item.gas_price() > 0:
        _spend_resource(_gas, item.gas_price(), item.start_frame)
    if item.supply_required() > 0:
        _spend_resource(_supply, item.supply_required(), item.start_frame)

    # If the item is a worker, assume it will go on minerals when complete
    if item.is_type(_my_race().getWorker()):
        _update_resource_collection(_minerals, item.completion_frame, 1, MINERALS_PER_WORKER_FRAME)

    # If the item is a refinery, make three workers available to it when it finishes
    if item.is_type(_my_race().getRefinery()):
        _push_reassignable_workers(item.completion_frame)

    # If the item provides supply, add it
    if item.supply_provided() > 0:
        _add_provided_supply(item.supply_provided(), item.start_frame)

    # Commit
    _committed_items.insert(item)


def _resolve_resource_blocks(item: _ProductionItem, prerequisite_items: _ItemSet, commit: bool) -> bool:
    if not _shift_for_minerals(item, prerequisite_items):
        return False
    if not _shift_for_gas(item, prerequisite_items, commit):
        return False
    if not _shift_for_supply(item, prerequisite_items, commit):
        return False

    # Guard against endless loops if there's a logic bug in one of the shift methods
    if item.start_frame >= _predict_frames:
        return False

    # Shift if the producer is busy at the start frame
    # This can happen if the producer was able to insert this item earlier, but we didn't have resources for it at the
    # time
    if item.producer is not None:
        delta = _available_at(item.type, item.start_frame, item.producer) - item.start_frame
        if delta > 0:
            item.start_frame += delta
            item.completion_frame += delta
            _shift_all(prerequisite_items, 0, delta)

            # Also set the availability of the producer later so it won't be considered for inserting the next item
            item.producer.available_from = item.completion_frame

    if not commit:
        return True

    # If there are prerequisites, commit them now
    for prerequisite_item in list(prerequisite_items.items):
        _commit_item(prerequisite_item)

    # Commit this item
    _commit_item(item)
    return True


def _pull_refineries() -> None:
    """Pulls refineries earlier if there are minerals available to do so."""
    refinery_type = _my_race().getRefinery()
    restart = True
    while restart:
        restart = False
        for item in _committed_items.items:
            if not item.is_type(refinery_type):
                continue
            if item.queued_building is not None:
                continue
            if item.completion_frame >= _predict_frames:
                continue

            # Look up the lowest number of minerals we have after the refinery is completed
            lowest_minerals = int(_minerals[max(0, item.completion_frame):].min())

            if lowest_minerals <= 0:
                continue

            # Use this to deduce how many frames of 3 workers mining we can afford to lose, with a safety buffer
            available_frames = to_int(lowest_minerals / (3.0 * MINERALS_PER_WORKER_FRAME * 0.9))

            # Find a frame earlier than the current start frame within this constraint where we have enough minerals
            start_frame = _scan_back(_minerals, refinery_type.mineralPrice(), item.start_frame - 1,
                                     max(0, item.start_frame - available_frames))
            start_frame = min(start_frame, item.start_frame)

            # Update the item if needed
            if start_frame < item.start_frame:
                _shift_one(_committed_items, item, start_frame - item.start_frame)

                # Restart the iteration as we may have changed the ordering
                restart = True
                break


def _pull_supply_providers() -> None:
    """Pulls supply providers earlier if there are minerals available to do so."""
    for item in _committed_items.items:
        if not item.is_type(UnitTypes.Protoss_Pylon) and not item.is_type(UnitTypes.Protoss_Nexus):
            continue
        if item.queued_building is not None:
            continue
        if item.completion_frame >= _predict_frames:
            continue

        # Pull nexuses as far as possible, pylons up to 48 frames
        limit = max(0, item.start_frame - 48) if item.is_type(UnitTypes.Protoss_Pylon) else 0

        item.start_frame = _scan_back(_minerals, item.mineral_price(), item.start_frame - 1, limit)

        # This runs after everything has been scheduled, so we don't need to bother with updating minerals, completion
        # frame, etc.


# ---------------------------------------------------------------------------------------------------------------------
# Goals


def _handle_goal(item_type: Type, location: ProductionLocation, count_to_produce: int, producer_limit: int,
                 prerequisite: UnitType, reserved_builder: MyWorker | None, frame: int) -> None:
    to_produce = count_to_produce

    unit_type = item_type if isinstance(item_type, UnitType) else None
    upgrade_or_tech_type = item_type if isinstance(item_type, UpgradeOrTechType) else None
    build_location = location if isinstance(location, BuildLocation) else None

    # Do some preprocessing of buildings to remove them if a matching building already exists in the committed items
    # This happens if we are requesting a building that has been queued by the builder
    if unit_type is not None and unit_type.isBuilding():
        # Verify we have a build location, which is required for buildings
        if build_location is None:
            log.get("ERROR: Trying to produce building without specifying a location")
            return

        # Scan for a matching building in the committed item set
        for other_item in _committed_items.items:
            # Compare unit types
            if not other_item.is_type(unit_type):
                continue

            # If the tile positions are the same, skip this item
            if build_location.location.tile == other_item.build_location.location.tile:
                return

            # If the other item is a pylon that does not already have a location, use this one instead
            # This case happens if we've queued a pylon for supply earlier and now get a request for a specific pylon
            # location - we may as well just build one
            # An exception is if the pylon is being queued for a much later frame, in which case we don't want to
            # interrupt our normal pylon placement decisions
            if (unit_type == UnitTypes.Protoss_Pylon and not other_item.build_location.location.tile.isValid()
                    and (frame - other_item.start_frame) < 2000):
                other_item.estimated_worker_movement_time = build_location.builder_frames
                other_item.build_location = build_location
                other_item.reserved_builder = reserved_builder
                return

    if unit_type is not None:
        producer_type = unit_type.whatBuilds()[0]
    else:
        assert upgrade_or_tech_type is not None
        producer_type = upgrade_or_tech_type.what_upgrades_or_researches()

    prerequisite_items = _ItemSet()

    # Step 1: Ensure we have all buildings we need to build this type

    # Add missing prerequisites for the type
    if unit_type is not None:
        _add_missing_prerequisites(prerequisite_items, unit_type, location, producer_type)
    if prerequisite != UnitTypes.None_:
        _add_building_if_incomplete(prerequisite_items, prerequisite, None, producer_type, 0, True)

    # Unless this item is a building, ensure we have something that can produce it
    if unit_type is None or not unit_type.isBuilding():
        _add_building_if_incomplete(prerequisite_items, producer_type, location, producer_type)

    # Shift the buildings so the frames start at 0
    _normalize_start_frame(prerequisite_items)

    # Resolve duplicates, keeping the earliest one of each type
    # Also record when the last prerequisite will finish
    prerequisites_available = _resolve_duplicates(prerequisite_items)

    # Reserve provisional positions for all buildings to get the earliest availability
    if not _reserve_build_positions(prerequisite_items, False):
        # There is no way to produce the prerequisites within the current prediction horizon
        return

    # If the last goal item has been pushed back, update the prerequisites_available frame
    if prerequisite_items:
        prerequisites_available = max(prerequisites_available, prerequisite_items[-1].completion_frame)

    # Push the start frame if we want to build the item later
    earliest_start_frame = max(prerequisites_available, frame - common.current_frame)

    # Push the start frame if the build location for a building is not yet powered
    if unit_type is not None and unit_type.isBuilding():
        assert build_location is not None
        earliest_start_frame = max(earliest_start_frame, build_location.frames_until_powered)

    # Shift the prerequisite items if needed
    if prerequisite_items and earliest_start_frame > prerequisites_available:
        delta = earliest_start_frame - prerequisites_available
        for prerequisite_item in prerequisite_items.items:
            prerequisite_item.start_frame += delta
            prerequisite_item.completion_frame += delta

    # Commit the build positions for the prerequisites
    _reserve_build_positions(prerequisite_items, True)

    # Buildings can now be handled immediately - we only ever produce one at a time and require a specific location
    if unit_type is not None and unit_type.isBuilding():
        assert build_location is not None
        building_item = _ProductionItem(item_type, earliest_start_frame, location)
        building_item.estimated_worker_movement_time = build_location.builder_frames
        building_item.build_location = build_location
        building_item.reserved_builder = reserved_builder
        _resolve_resource_blocks(building_item, prerequisite_items, True)
        return

    # Step 2: Collect producers
    producers: list[_Producer] = []

    # Planned producers
    for item in prerequisite_items.items:
        if item.is_type(producer_type) and _can_produce_from_item(item_type, location, item):
            producers.append(_Producer(max(earliest_start_frame, item.completion_frame), queued=item))

    # Committed producers
    for item in _committed_items.items:
        if not item.is_type(producer_type) or not _can_produce_from_item(item_type, location, item):
            continue

        queued_unit = item.queued_building.unit if item.queued_building is not None else None
        if queued_unit is not None:
            existing_producer = _existing_producers.get(queued_unit)
            if existing_producer is not None:
                existing_producer.available_from = max(earliest_start_frame, item.completion_frame)
                producers.append(existing_producer)
                continue

            producer = _Producer(max(earliest_start_frame, item.completion_frame), queued=item)
            producers.append(producer)
            _existing_producers[queued_unit] = producer
        else:
            producers.append(_Producer(max(earliest_start_frame, item.completion_frame), queued=item))

    # Completed producers
    game = bwapi.Broodwar
    for unit in units.all_mine():
        if not unit.exists() or not unit.completed:
            continue
        if unit.type != producer_type:
            continue
        if not _can_produce_from_unit(item_type, location, unit):
            continue

        # If we have sent the train command to a producer, but the unit isn't created yet, then reduce the number we
        # need to build. This is not perfect, as it will reduce the count of all production goals targeting this unit
        # type, but it's probably rare that we have multiple goals with limited numbers of the same unit type anyway.
        last_command = _bw(unit).getLastCommand()
        if (unit_type is not None and to_produce != -1 and last_command.getType() == UnitCommandTypes.Train
                and (common.current_frame - unit.last_command_frame - 1) < game.getLatencyFrames()
                and last_command.getUnitType() == unit_type):
            to_produce -= 1
            if to_produce == 0:
                return

        remaining_train_time = -1
        if unit_type is not None:
            remaining_train_time = _get_remaining_build_time(unit)
        elif upgrade_or_tech_type is not None:
            remaining_train_time = (_get_remaining_research_time(unit) if upgrade_or_tech_type.is_tech_type()
                                    else _get_remaining_upgrade_time(unit))

        # If this producer is already created when handling a previous production goal, reference the existing object
        existing_producer = _existing_producers.get(unit)
        if existing_producer is not None:
            existing_producer.available_from = max(earliest_start_frame, remaining_train_time + 1)
            producers.append(existing_producer)
            continue

        producer = _Producer(max(earliest_start_frame, remaining_train_time + 1), existing=unit)
        producers.append(producer)
        _existing_producers[unit] = producer

    # If we at this point have no producers, it means we have buildings that can produce the item, but not in the
    # location we need. So add a new producer at the correct location to the prerequisite items.
    # It can also mean we are trying to produce workers and have no resource depots left.
    if not producers:
        if producer_type.isResourceDepot():
            return

        start_frame = max(0, earliest_start_frame - unit_util.build_time(producer_type))
        producer_item = _ProductionItem(producer_type, start_frame, location)
        prerequisite_items.insert(producer_item)

        _reserve_build_positions(prerequisite_items, True)

        producers.append(_Producer(max(earliest_start_frame, producer_item.completion_frame), queued=producer_item))

    # Step 3: Repeatedly commit a unit from the earliest producer available, or a new one if applicable, until we have
    #         built enough or don't have resources for more
    committed_something = False
    while to_produce == -1 or to_produce > 0:
        # Find the earliest available producer
        best_producer: _Producer | None = None
        best_frame = _predict_frames
        for producer in producers:
            available = _available_at(item_type, earliest_start_frame, producer)
            if available < best_frame:
                best_frame = available
                best_producer = producer

        # Attempt to create an item from the best producer
        best_producer_item: _ProductionItem | None = None
        if best_producer is not None:
            # In some situations we want to commit immediately:
            # - There are prerequisite items to produce
            #   - Usually means this is the first item of the type, and a producer is in the prerequisite items set
            # - We are producing an unlimited number of units
            #   - We want to saturate production from existing producers before adding new ones
            # - We are already at our producer limit
            commit_immediately = (bool(prerequisite_items) or _at_producer_limit(len(producers), producer_limit)
                                  or to_produce == -1)

            # Create the item and resolve resource blocks
            best_producer_item = _ProductionItem(item_type, best_frame, location, best_producer)
            result = _resolve_resource_blocks(best_producer_item, prerequisite_items, commit_immediately)

            # If the item was created and committed, continue the loop now
            if commit_immediately and result:
                committed_something = True

                if to_produce > 0:
                    to_produce -= 1
                best_producer.items.insert(best_producer_item)

                # If we have prerequisites, we may need to adjust the earliest start frame, as they may have shifted
                if prerequisite_items:
                    for prerequisite_item in prerequisite_items.items:
                        earliest_start_frame = max(earliest_start_frame, prerequisite_item.completion_frame)

                    # Clear the prerequisites, as they have now been committed
                    prerequisite_items.clear()

                continue

            # If we wanted to commit the item but couldn't, continue only if we are producing an unlimited number of
            # units. In the other two cases we can't produce the unit.
            if commit_immediately and (prerequisite_items or _at_producer_limit(len(producers), producer_limit)):
                break

            # If the item could not be created, clear it
            if not result:
                best_producer_item = None
        elif _at_producer_limit(len(producers), producer_limit):
            break

        # Create a new producer
        new_producer_prerequisites = _ItemSet()
        producer_item = _ProductionItem(producer_type,
                                        max(0, earliest_start_frame - unit_util.build_time(producer_type)),
                                        location)
        new_producer_prerequisites.insert(producer_item)
        new_producer = _Producer(producer_item.completion_frame, queued=producer_item)

        # Run a provisional build position check to determine if we need to insert a pylon to get a valid build
        # location. If no build location is available at all, we don't consider the new producer.
        # If a pylon is needed, this may shift the completion time of the producer and therefore the item.
        new_producer_item: _ProductionItem | None = None
        if _reserve_build_positions(new_producer_prerequisites, False):
            # Try to provisionally insert the item
            new_producer_item = _ProductionItem(item_type, producer_item.completion_frame, location, new_producer)
            if not _resolve_resource_blocks(new_producer_item, new_producer_prerequisites, False):
                new_producer_item = None

        # If we couldn't produce an item, break now
        if best_producer_item is None and new_producer_item is None:
            break

        # Commit the one that could be produced earliest
        if best_producer_item is not None and (new_producer_item is None
                                               or best_producer_item.start_frame <= new_producer_item.start_frame):
            assert best_producer is not None
            if not _resolve_resource_blocks(best_producer_item, prerequisite_items, True):
                break
            best_producer.items.insert(best_producer_item)
        else:
            assert new_producer_item is not None
            _reserve_build_positions(new_producer_prerequisites, True)
            if not _resolve_resource_blocks(new_producer_item, new_producer_prerequisites, True):
                break
            producers.append(new_producer)
            new_producer.items.insert(new_producer_item)

        committed_something = True

        if to_produce > 0:
            to_produce -= 1

    # If we have prerequisites but didn't produce anything, try to produce the first prerequisite instead
    # This situation comes up when we don't predict to be able to produce the unit within our prediction window
    # So producing the first prerequisite allows us to keep things moving until the item can be produced at a later
    # frame
    if prerequisite_items and not committed_something:
        _resolve_resource_blocks(prerequisite_items[0], _ItemSet(), True)


def _queue_maxed_gateways(has_unlimited_gateway_goal: bool) -> None:
    """When we are maxed, ensure we have enough gateways to quickly replace losses."""
    if not has_unlimited_gateway_goal or _total_supply[0] < 400 or _supply[0] > 4:
        return

    gateway_type = UnitTypes.Protoss_Gateway
    large_locations = _build_locations[Neighbourhood.ALL_MY_BASES][gateway_type.tileWidth()]

    # Count the gateways we already have
    gateways = units.count_completed(gateway_type) + len(builder.pending_buildings_of_type(gateway_type))

    # Build up to 25 gateways, but keep 2 build locations reserved for other types of buildings
    # We might have a need to build air units or tech later on in the game
    # (Both operands are size_t in Stardust, so a negative difference wraps around to a huge value.)
    def as_size_t(value: int) -> int:
        return value % 2**64

    desired_gateways = min(as_size_t(25 - gateways), as_size_t(len(large_locations) - 2))

    # Scale up to 25 gateways as our bank allows
    # At each step in the loop, we find the frame where we have enough money to produce a dragoon from every gateway
    # The rationale is that we don't need more gateways if we can't produce from all of them anyway
    gateway = gateways + 1
    while gateway <= as_size_t(gateways + desired_gateways):
        # If there are fewer than 2 large build locations available, add a pylon
        pylon_needed = len(large_locations) < 2

        required_minerals = (gateway * UnitTypes.Protoss_Dragoon.mineralPrice() + gateway_type.mineralPrice()
                             + (UnitTypes.Protoss_Pylon.mineralPrice() if pylon_needed else 0))
        required_gas = gateway * UnitTypes.Protoss_Dragoon.gasPrice() + gateway_type.gasPrice()

        # Find the frame where it makes sense to build this gateway
        # (Stardust compares the required gas against minerals here.)
        start_frame = _scan_back(_minerals, max(required_minerals, required_gas), _predict_frames - 1, 0)
        if start_frame >= _predict_frames:
            break

        # If needed, commit a pylon that powers large building locations
        if pylon_needed:
            pylon = _ProductionItem(UnitTypes.Protoss_Pylon, start_frame, Neighbourhood.ALL_MY_BASES)
            if not _choose_pylon_build_location(pylon, False, 4):
                break
            _committed_items.insert(pylon)
            start_frame = pylon.completion_frame
            if start_frame >= _predict_frames:
                break

        # Commit the gateway
        item = _ProductionItem(gateway_type, start_frame, Neighbourhood.ALL_MY_BASES)
        item_in_set = _ItemSet()
        item_in_set.insert(item)
        _reserve_build_positions(item_in_set, True)
        if not item.build_location.location.tile.isValid():
            break  # Shouldn't happen, but guard against it anyway
        _committed_items.insert(item)

        gateway += 1


def _issue_build_commands() -> None:
    game = bwapi.Broodwar
    remaining_latency_frames = game.getRemainingLatencyFrames()
    first_unbuilt_item = True
    for item in list(_committed_items.items):
        item_type = item.type
        producer_unit = item.producer.existing if item.producer is not None else None

        # Units
        if isinstance(item_type, UnitType):
            # Produce units desired at frame 0
            if producer_unit is not None and item.start_frame <= remaining_latency_frames:
                if producer_unit.train(item_type):
                    log.debug(f"Sent command to train {item_type} from {producer_unit.type} @ "
                              f"{producer_unit.get_tile_position()}")

            # Pylons may not already have a build location
            if item.is_type(UnitTypes.Protoss_Pylon):
                _choose_pylon_build_location(item)

            # Queue buildings when we expect the builder will arrive at the desired start frame or later
            if (item_type.isBuilding() and item.queued_building is None
                    and (item.start_frame - item.build_location.builder_frames) <= remaining_latency_frames):
                # TODO: Should be resolved earlier
                if not item.build_location.location.tile.isValid():
                    log.get(f"ERROR: No build location for {item_type}")
                    continue

                # Special case: never try to build a prerequisite cybernetics core before the assimilator has been
                # queued. This may happen because a worker needs to travel further to the core build location.
                # It really confuses our resource scheduling and would usually result in pushing back the assimilator.
                if (item.is_prerequisite and item.is_type(UnitTypes.Protoss_Cybernetics_Core)
                        and units.count_all(UnitTypes.Protoss_Assimilator) == 0
                        and not builder.pending_buildings_of_type(UnitTypes.Protoss_Assimilator)):
                    continue

                arrival_frame = common.current_frame
                building_builder = item.reserved_builder
                if building_builder is None:
                    building_builder, arrival_frame = builder.get_builder_unit(item.build_location.location.tile,
                                                                               item_type)
                if building_builder is not None and (arrival_frame - common.current_frame) >= item.start_frame:
                    builder.build(item_type, item.build_location.location.tile, building_builder,
                                  common.current_frame + item.start_frame)

            # If the first unbuilt item in the queue is a building, clear the desired start frame if the worker is
            # ready to build it. At this point we have made sure we had time to produce more important things while
            # the worker was in transit, so there's no reason for it to wait until the original scheduled time.
            queued_building = item.queued_building
            if (first_unbuilt_item and queued_building is not None and queued_building.desired_start_frame > 0
                    and queued_building.builder_ready()):
                queued_building.desired_start_frame = 0

            # Update the flag
            if queued_building is None or not queued_building.is_construction_started():
                first_unbuilt_item = False

        # Upgrades
        else:
            # Upgrade if start frame is now
            if producer_unit is not None and item.start_frame <= remaining_latency_frames:
                if item_type.is_tech_type():
                    if producer_unit.research(item_type.tech_type):
                        log.get(f"Started research: {item_type.tech_type}")
                elif producer_unit.upgrade(item_type.upgrade_type):
                    log.get(f"Started upgrade: {item_type.upgrade_type} "
                            f"({game.self().getUpgradeLevel(item_type.upgrade_type) + 1})")


def update() -> None:
    import stardust.strategist.strategist as strategist

    _committed_items.clear()

    _initialize_resources()

    # While running through the goals, keep track of whether we have an unlimited production goal that needs gas
    # In emergency situations we might cancel all production of units that need gas, in which case we want to transfer
    # workers off of gas. Transferring workers onto gas is handled in the producer logic.
    has_unlimited_gas_goal = False

    # Also track whether we have a goal that produces an unlimited number of units from a gateway
    has_unlimited_gateway_goal = False

    available_gas = bwapi.Broodwar.self().gas()
    for goal in strategist.current_production_goals():
        if isinstance(goal, UnitProductionGoal):
            if goal.count_to_produce() == -1 and goal.unit_type().gasPrice() > 0:
                has_unlimited_gas_goal = True
                available_gas = -1

            if (goal.count_to_produce() == -1
                    and goal.unit_type().whatBuilds()[0] == UnitTypes.Protoss_Gateway):
                has_unlimited_gateway_goal = True

            _handle_goal(goal.unit_type(), goal.get_location(), goal.count_to_produce(), goal.get_producer_limit(),
                         UnitTypes.None_, goal.get_reserved_builder(), goal.get_frame())
        elif isinstance(goal, UpgradeProductionGoal):
            _handle_goal(goal.upgrade_type(), None, 1, goal.get_producer_limit(), goal.prerequisite_for_next_level(),
                         None, goal.get_frame())
        else:
            log.get("ERROR: Unknown variant type for ProductionGoal")

    if not has_unlimited_gas_goal:
        for item in _committed_items.items:
            available_gas -= item.gas_price()

    # Set gas collection appropriately
    if _reassigned_mineral_workers_at_start_frame > 0:
        workers.set_desired_gas_worker_delta(_reassigned_mineral_workers_at_start_frame)
    elif available_gas >= 0:
        workers.set_desired_gas_worker_delta(-workers.reassignable_gas_workers())

    if has_unlimited_gas_goal:
        _pull_refineries()

    # Pylons are often built a bit too late, since we don't accurately simulate mineral collection and the build worker
    # can be delayed. So pull pylons a bit earlier whenever we have the resources for it.
    # Also pulls nexuses since we always want them build as early as possible.
    _pull_supply_providers()

    _queue_maxed_gateways(has_unlimited_gateway_goal)

    # Issue build commands
    _issue_build_commands()

"""Port of Strategist/Plays/Macro/TakeExpansion.{h,cpp}: takes an expansion, clearing enemy units at it and building a
cannon to reveal a blocking burrowed or cloaked unit."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Orders, Position, Races, TilePosition, UnitTypes
from stardust.builder import builder, building_placement
from stardust.builder.block import Location
from stardust.builder.building_placement import BuildLocation
from stardust.general import general
from stardust.general.squads.attack_base_squad import AttackBaseSquad
from stardust.general.unit_cluster import combat_sim
from stardust.instrumentation import log
from stardust.map.path_finding import path_finding
from stardust.players import players
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.strategist.play import PRIORITY_DEPOTS, PRIORITY_NORMAL, Play, PlayUnitRequirement, ProductionGoals, \
    UnitCallback, add_goal
from stardust.units import units
from stardust.util import geo, unit_util
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.units.my_worker import MyWorker
    from stardust.units.unit import Unit


class TakeExpansion(Play):
    def __init__(self, base: Base, enemy_value: int, label: str | None = None) -> None:
        super().__init__(label if label is not None else f"Take expansion @ {base.get_tile_position()}")
        self.base = base
        self.enemy_value = enemy_value
        self.depot_position = base.get_tile_position()
        self.builder: MyWorker | None = None
        self.build_cannon = False
        self.squad: AttackBaseSquad | None = None

    def get_squad(self) -> AttackBaseSquad | None:
        return self.squad

    def update(self) -> None:
        base = self.base

        # If our builder is dead, release it
        if self.builder is not None and not self.builder.exists():
            self.builder = None

        # Treat the play as complete when the nexus is finished
        nexus = units.my_building_at(self.depot_position)
        if nexus is not None:
            if nexus.completed:
                self.status.complete = True
            return

        # Cancel the play if the base becomes owned by someone else in the meantime
        if base.owner == bwapi.Broodwar.enemy():
            self.status.complete = True
            return

        # Update the enemy unit value and look for a blocking unit
        self.enemy_value = 0
        blocker: Unit | None = None
        need_detection = False
        for unit in units.enemy_at_base(base):
            # Count enemies that can currently attack
            if ((unit.is_transport() or unit_util.can_attack_ground(unit.type))
                    and (not unit.burrowed or unit.type == UnitTypes.Zerg_Lurker)):
                self.enemy_value += combat_sim.unit_value(unit)
                need_detection = need_detection or unit.needs_detection()

            if unit.is_flying:
                continue
            if geo.overlaps_tiles(self.depot_position, 4, 3, unit.get_tile_position(), 2, 2):
                blocker = unit

                # If the unit is unburrowing, assume it is what triggered the base to be blocked
                if (base.blocked_by_enemy and blocker.bwapi_unit is not None
                        and blocker.bwapi_unit.getOrder() == Orders.Unburrowing):
                    log.get(f"Base @ {base.get_tile_position()} is probably no longer blocked by an enemy burrowed "
                            f"unit")
                    base.blocked_by_enemy = False

        def update_attack_squad() -> None:
            # Disband the squad if we don't need it any more
            if self.enemy_value == 0:
                if self.squad is not None:
                    self.status.removed_units = self.squad.get_units()
                    general.remove_squad(self.squad)
                    self.squad = None
                return

            # Ensure we have the squad
            if self.squad is None:
                self.squad = AttackBaseSquad(base, "Take Expansion")
                general.add_squad(self.squad)
            squad = self.squad

            # Check if we need detection
            detectors = squad.get_detectors()
            if not need_detection and detectors:
                self.status.removed_units.extend(detectors)
            elif need_detection and not detectors:
                self.status.unit_requirements.append(
                    PlayUnitRequirement(1, UnitTypes.Protoss_Observer, squad.get_target_position()))

                # Release the squad units until we get the detector
                self.status.removed_units = squad.get_units()
                return

            # Reserve enough units to attack the base
            our_value = sum(combat_sim.unit_value(unit) for unit in squad.get_units())

            requested_units = 0
            dragoon_value = combat_sim.unit_value(UnitTypes.Protoss_Dragoon)
            while our_value < self.enemy_value * 2:
                requested_units += 1
                our_value += dragoon_value

            # Ensure we have at least two units
            requested_units = max(requested_units, 2 - len(squad.get_units()))

            # TODO: Request zealot or dragoon when we have that capability
            if requested_units > 0:
                self.status.unit_requirements.append(
                    PlayUnitRequirement(requested_units, UnitTypes.Protoss_Dragoon, base.get_position(),
                                        allow_from_vanguard_cluster=False))

        # If there are enemy combat units, ensure we have an attack squad and cancel anything the builder is doing
        if self.enemy_value > 0:
            update_attack_squad()
            builder.cancel_base(base)
            if self.builder is not None:
                workers.release_worker(self.builder)
                self.builder = None
            return

        # If there is a burrowed Zerg unit, also get an attack squad but allow the builder to continue its work
        if blocker is None and base.blocked_by_enemy and bwapi.Broodwar.enemy().getRace() == Races.Zerg:
            # Assume it is a zergling
            self.enemy_value = combat_sim.unit_value(UnitTypes.Zerg_Zergling)

        update_attack_squad()

        # Get a builder if we don't have one
        if self.builder is None:
            self.builder, _ = builder.get_builder_unit(self.depot_position, UnitTypes.Protoss_Nexus)
            if self.builder is None:
                return
        build_worker = self.builder

        # If the enemy base is blocked by a hidden enemy unit, build a cannon to clear it
        self.build_cannon = False
        if (blocker is not None or base.blocked_by_enemy
                or units.count_enemy(UnitTypes.Protoss_Dark_Templar) > 0):
            self.build_cannon = True

            # Ensure the builder stays reserved between buildings
            workers.reserve_worker(build_worker)

            # Attack the blocker if we have it and the worker doesn't have anything else to do
            if blocker is not None:
                if not builder.has_pending_building(build_worker) and not blocker.undetected:
                    build_worker.attack_unit(blocker)
                return

            # If we have detection on all of the depot's tiles, clear the base being blocked by enemy
            detection_grid = players.grid(bwapi.Broodwar.self())
            all_detected = all(
                detection_grid.detection(Position(TilePosition(x, y)) + Position(16, 16)) != 0
                for y in range(self.depot_position.y, self.depot_position.y + 3)
                for x in range(self.depot_position.x, self.depot_position.x + 4))

            if not all_detected:
                return

            if base.blocked_by_enemy:
                log.get(f"Base @ {base.get_tile_position()} is no longer blocked by an enemy unit")
                base.blocked_by_enemy = False

        if not builder.is_pending_here(self.depot_position):
            builder.build(UnitTypes.Protoss_Nexus, self.depot_position, build_worker)

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        if self.build_cannon:
            # Cancel the play if we don't have a cannon location
            base_static_defense_locations = building_placement.base_static_defense_locations(self.base)
            if not base_static_defense_locations.is_valid():
                self.status.complete = True
                return

            cannon_location = base_static_defense_locations.worker_defense_cannons[0]
            power_pylon = base_static_defense_locations.power_pylon
            if units.my_building_at(cannon_location) is None and builder.pending_here(cannon_location) is None:
                # Build the pylon if it is not already pending
                if units.my_building_at(power_pylon) is None and builder.pending_here(power_pylon) is None:
                    build_location = BuildLocation(Location(power_pylon), 0, 0, 0)
                    add_goal(prioritized_production_goals, PRIORITY_DEPOTS,
                             UnitProductionGoal.at(self.label, UnitTypes.Protoss_Pylon, build_location, self.builder))

                build_location = BuildLocation(
                    Location(cannon_location), 0,
                    builder.frames_until_completed(power_pylon, unit_util.build_time(UnitTypes.Protoss_Pylon) + 240),
                    0)
                add_goal(prioritized_production_goals, PRIORITY_DEPOTS,
                         UnitProductionGoal.at(self.label, UnitTypes.Protoss_Photon_Cannon, build_location,
                                               self.builder))

        # Build an observer if we need one
        for unit_requirement in self.status.unit_requirements:
            if unit_requirement.type != UnitTypes.Protoss_Observer:
                continue
            if unit_requirement.count < 1:
                continue

            add_goal(prioritized_production_goals, PRIORITY_NORMAL,
                     UnitProductionGoal(self.label, unit_requirement.type, unit_requirement.count, 1))

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        builder.cancel_base(self.base)

        if self.builder is not None and not builder.has_pending_building(self.builder):
            workers.release_worker(self.builder)

        super().disband(removed_unit_callback, movable_unit_callback)

    def cancellable(self) -> bool:
        # Don't allow to be cancelled if the builder is very close to the base
        # This prevents instability in wanting to take the expansion after the probe has already been sent most of the
        # way
        if self.builder is not None:
            dist = path_finding.get_ground_distance(self.builder.last_position, self.base.get_position(),
                                                    self.builder.type)
            if dist != -1 and dist < 500:
                return False

        return units.my_building_at(self.depot_position) is None

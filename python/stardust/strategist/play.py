"""Port of Strategist/Play.{h,cpp}: the base class of plays.

A play is a unit of strategy: it signals the Strategist which units it needs and whether it is complete or should
transition to another play, manages its units (usually through a squad), and orders production.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import TYPE_CHECKING

from bwapi import Position, UnitType, UnitTypes
from stardust.cpp import INT_MAX

if TYPE_CHECKING:
    from stardust.general.squad import Squad
    from stardust.map.path_finding.navigation_grid import GridNode
    from stardust.producer.production_goal import ProductionGoal
    from stardust.units.my_unit import MyUnit

PRIORITY_EMERGENCY = 10
PRIORITY_WORKERS = 20
PRIORITY_DEPOTS = 30
PRIORITY_BASEDEFENSE = 40  # Generally early-game defenders
PRIORITY_SPECIALTEAMS = 50  # Units we need for some specific purpose
PRIORITY_MAINARMYBASEPRODUCTION = 60  # What we consider to be the minimum allowable production for our main army
PRIORITY_HIGHPRIORITYUPGRADES = 70  # e.g. basic ground upgrades that we don't want to get pushed behind other stuff
PRIORITY_NORMAL = 80
PRIORITY_MAINARMY = 90
PRIORITY_LOWEST = 100

type ProductionGoals = dict[int, list[ProductionGoal]]
type MineralReservations = list[tuple[int, int]]
type UnitCallback = Callable[[MyUnit], None]


def add_goal(prioritized_production_goals: ProductionGoals, priority: int, goal: ProductionGoal) -> None:
    """prioritizedProductionGoals[priority].emplace_back(goal)."""
    prioritized_production_goals.setdefault(priority, []).append(goal)


class PlayUnitRequirement:
    __slots__ = ("count", "type", "position", "distance_limit", "allow_from_vanguard_cluster",
                 "allow_failing_grid_node_predicate", "grid_node_predicate")

    def __init__(self, count: int, unit_type: UnitType, position: Position, distance_limit: int = INT_MAX,
                 allow_from_vanguard_cluster: bool = True,
                 grid_node_predicate: Callable[[GridNode], bool] | None = None,
                 allow_failing_grid_node_predicate: bool = False) -> None:
        self.count = count
        self.type = unit_type
        self.position = position
        self.distance_limit = distance_limit
        self.allow_from_vanguard_cluster = allow_from_vanguard_cluster
        self.allow_failing_grid_node_predicate = allow_failing_grid_node_predicate
        self.grid_node_predicate = grid_node_predicate


class PlayStatus:
    __slots__ = ("complete", "unit_requirements", "removed_units", "transition_to")

    def __init__(self) -> None:
        self.complete = False
        self.unit_requirements: list[PlayUnitRequirement] = []
        self.removed_units: list[MyUnit] = []
        self.transition_to: Play | None = None


class Play:
    def __init__(self, label: str) -> None:
        self.label = label
        self.status = PlayStatus()
        self.assigned_incomplete_units: dict[UnitType, int] = {}

    def receives_unassigned_units(self) -> bool:
        """Whether this play should receive any unassigned combat units."""
        return False

    def can_reassign_unit(self, unit: MyUnit) -> bool:
        return True

    def get_squad(self) -> Squad | None:
        """The play's squad, if it has one."""
        return None

    def add_unit(self, unit: MyUnit) -> None:
        """Add a unit to the play. By default, this adds it to the play's squad, if it has one."""
        squad = self.get_squad()
        if squad is not None:
            squad.add_unit(unit)

    def remove_unit(self, unit: MyUnit) -> None:
        """Remove a unit from the play. By default, this removes it from the play's squad, if it has one."""
        squad = self.get_squad()
        if squad is not None:
            squad.remove_unit(unit)

    def update(self) -> None:
        """Runs at the start of the Strategist's frame and updates the play status."""

    def post_transition(self) -> None:
        """Runs just after the play has transitioned from another play."""

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        """Runs at the end of the Strategist's frame and allows the play to order units or upgrades from the
        producer."""

    def add_mineral_reservations(self, mineral_reservations: MineralReservations) -> None:
        """Runs at the end of the Strategist's frame and allows the play to reserve minerals at a specific future
        frame."""

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        """Called when a play is being disbanded (either removed completely or transitioned to a different play).
        It is the play's responsibility to call either removed_unit_callback or movable_unit_callback for all units
        that have been assigned to it via add_unit (and not removed earlier through status.removed_units)."""
        from stardust.general import general

        # By default, all units in the squad except cannons are considered movable
        squad = self.get_squad()
        if squad is not None:
            for unit in squad.get_units():
                if unit.type == UnitTypes.Protoss_Photon_Cannon:
                    removed_unit_callback(unit)
                else:
                    movable_unit_callback(unit)

            general.remove_squad(squad)

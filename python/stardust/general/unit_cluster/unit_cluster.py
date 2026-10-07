"""Port of General/UnitCluster.h and General/UnitCluster/UnitCluster.cpp: a group of units in a squad that move and
fight together.

The methods implemented in Stardust's other UnitCluster/*.cpp files delegate to the modules of this package.
removeUnit takes the unit rather than a set iterator.
"""

from __future__ import annotations

import math
from collections import deque
from enum import Enum
from typing import TYPE_CHECKING, Any

from bwapi import Position, UnitType, UnitTypes, WalkPosition
from stardust import common, config
from stardust.cpp import INT_MAX, cdiv
from stardust.instrumentation import cherryvis
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions

if TYPE_CHECKING:
    from stardust.general.combat_sim_result import CombatSimResult
    from stardust.map.choke import Choke
    from stardust.units.my_observer import MyObserver
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit

_DEBUG_CLUSTER_MEMBERSHIP = config.INSTRUMENTATION_ENABLED

type UnitsAndTargets = list[tuple[MyUnit, Unit | None]]


class Activity(Enum):
    Moving = 0
    Attacking = 1
    Regrouping = 2


class SubActivity(Enum):
    None_ = 0
    ContainStaticDefense = 1
    ContainChoke = 2
    StandGround = 3
    Flee = 4
    AttackBlockingArmy = 5


def _activity_name(activity: Activity | SubActivity) -> str:
    return "None" if activity is SubActivity.None_ else activity.name


class UnitCluster:
    def __init__(self, unit: MyUnit) -> None:
        self.center = unit.last_position
        self.vanguard = unit
        self.units: set[MyUnit] = {unit}

        self.vanguard_dist_to_target = -1
        self.vanguard_dist_to_main = 0
        self.percentage_to_enemy_main = 0.0

        self.ball_radius = 16
        self.line_radius = 16

        self.enemy_aoe_radius = 0

        self.current_activity = Activity.Moving
        self.current_sub_activity = SubActivity.None_
        self.last_activity_change = 0

        self.recent_sim_results: deque[CombatSimResult] = deque()
        self.recent_regroup_sim_results: deque[CombatSimResult] = deque()

        self.is_vanguard_cluster = False

        self.area = unit.type.width() * unit.type.height()

        if _DEBUG_CLUSTER_MEMBERSHIP:
            cherryvis.log(f"Added to new cluster @ {WalkPosition(self.center)}", unit.id)

    def absorb_cluster(self, other: UnitCluster, target_position: Position) -> None:
        self.units.update(other.units)
        self.area += other.area
        self.ball_radius = int(math.sqrt(self.area / math.pi))
        self.line_radius = 16 * len(self.units)

        # Recompute the center
        if not self.units:
            return  # should never happen, but guard against divide-by-zero
        sum_x = sum(unit.last_position.x for unit in self.units)
        sum_y = sum(unit.last_position.y for unit in self.units)
        self.center = Position(sum_x // len(self.units), sum_y // len(self.units))

    def add_unit(self, unit: MyUnit) -> None:
        if unit in self.units:
            return

        self.units.add(unit)

        count = len(self.units)
        self.center = Position(cdiv(self.center.x * (count - 1) + unit.last_position.x, count),
                               cdiv(self.center.y * (count - 1) + unit.last_position.y, count))

        self.area += unit.type.width() * unit.type.height()

        if _DEBUG_CLUSTER_MEMBERSHIP:
            cherryvis.log(f"Added to cluster @ {WalkPosition(self.center)}", unit.id)

    def remove_unit(self, unit: MyUnit, target_position: Position) -> None:
        self.units.discard(unit)
        self.area -= unit.type.width() * unit.type.height()

        # Guard against divide-by-zero, but this shouldn't happen as we don't remove the last unit from a cluster in
        # this way
        if not self.units:
            return

        if self.vanguard is unit:
            self.update_positions(target_position)
        else:
            count = len(self.units)
            self.center = Position(cdiv(self.center.x * (count + 1) - unit.last_position.x, count),
                                   cdiv(self.center.y * (count + 1) - unit.last_position.y, count))

    def has_unit_type(self, unit_type: UnitType) -> bool:
        return any(unit.type == unit_type for unit in self.units)

    def update_positions(self, target_position: Position) -> None:
        sum_x = 0
        sum_y = 0

        # Start by pruning dead units and recomputing unit distances and the cluster center position
        min_ground_distance = INT_MAX
        min_air_distance = INT_MAX
        for unit in list(self.units):
            if not unit.exists():
                self.area -= unit.type.width() * unit.type.height()
                self.units.discard(unit)
                continue

            sum_x += unit.last_position.x
            sum_y += unit.last_position.y

            if unit.is_flying:
                unit.dist_to_target_position = unit.last_position.getApproxDistance(target_position)
                if unit.dist_to_target_position < min_air_distance:
                    min_air_distance = unit.dist_to_target_position
                continue

            unit.dist_to_target_position = path_finding.get_ground_distance(
                target_position, unit.last_position, unit.type, PathFindingOptions.UseNeighbouringBWEMArea)
            if unit.dist_to_target_position != -1 and unit.dist_to_target_position < min_ground_distance:
                min_ground_distance = unit.dist_to_target_position

        self.ball_radius = int(math.sqrt(self.area / math.pi)) if self.area > 0 else 0
        self.line_radius = 16 * len(self.units)

        if not self.units:
            return

        self.center = Position(sum_x // len(self.units), sum_y // len(self.units))

        # Now determine which unit is our vanguard unit
        # This is generally the unit closest to the target position, but if more than one unit is at approximately the
        # same distance, we take the one closest to the cluster center
        vanguard: MyUnit | None = None
        min_center_dist = INT_MAX
        for unit in self.units:
            if unit.type == UnitTypes.Protoss_Photon_Cannon:
                continue
            if unit.dist_to_target_position == -1:
                continue
            if unit.is_flying != (min_ground_distance == INT_MAX):
                continue
            if unit.dist_to_target_position - (min_air_distance if unit.is_flying else min_ground_distance) > 32:
                continue

            center_dist = unit.last_position.getApproxDistance(self.center)
            if center_dist < min_center_dist:
                vanguard = unit
                min_center_dist = center_dist

        # If the vanguard isn't set, it means we have no units with a valid distance to the goal
        # So fall back to air distances for all units
        if vanguard is None:
            min_air_distance = INT_MAX
            for unit in self.units:
                if unit.type == UnitTypes.Protoss_Photon_Cannon:
                    continue
                unit.dist_to_target_position = unit.last_position.getApproxDistance(target_position)
                if unit.dist_to_target_position < min_air_distance:
                    min_air_distance = unit.dist_to_target_position
                    vanguard = unit

        if vanguard is None:
            vanguard = next(iter(self.units))

        self.vanguard = vanguard
        self.vanguard_dist_to_target = vanguard.dist_to_target_position

        def vanguard_dist_to(pos: Position) -> int:
            if vanguard.is_flying:
                return vanguard.last_position.getApproxDistance(pos)

            dist = path_finding.get_ground_distance(pos, vanguard.last_position, vanguard.type,
                                                    PathFindingOptions.UseNeighbouringBWEMArea)
            if dist != -1:
                return dist

            return vanguard.last_position.getApproxDistance(pos)

        my_main = game_map.get_my_main()
        assert my_main is not None
        self.vanguard_dist_to_main = vanguard_dist_to(my_main.get_position())

        enemy_main = game_map.get_enemy_main()
        vanguard_dist_to_enemy_main = vanguard_dist_to(enemy_main.get_position() if enemy_main is not None
                                                       else target_position)
        if vanguard_dist_to_enemy_main > 0 or self.vanguard_dist_to_main > 0:
            self.percentage_to_enemy_main = (self.vanguard_dist_to_main
                                             / (self.vanguard_dist_to_main + vanguard_dist_to_enemy_main))
        else:
            self.percentage_to_enemy_main = 0.5

    def set_activity(self, new_activity: Activity, new_sub_activity: SubActivity = SubActivity.None_) -> None:
        if self.current_activity == new_activity:
            return

        if config.DEBUG_COMBATSIM:
            cherryvis.log(f"{WalkPosition(self.center)}: Changed activity from "
                          f"{_activity_name(self.current_activity)} to {_activity_name(new_activity)}")

        self.current_activity = new_activity
        self.current_sub_activity = new_sub_activity
        self.last_activity_change = common.current_frame

    def set_sub_activity(self, new_sub_activity: SubActivity) -> None:
        if self.current_sub_activity == new_sub_activity:
            return

        if config.DEBUG_COMBATSIM:
            cherryvis.log(f"{WalkPosition(self.center)}: Changed sub-activity from "
                          f"{_activity_name(self.current_sub_activity)} to {_activity_name(new_sub_activity)}")

        self.current_sub_activity = new_sub_activity

    def get_current_activity(self) -> str:
        return _activity_name(self.current_activity)

    def get_current_sub_activity(self) -> str:
        return _activity_name(self.current_sub_activity)

    def is_fleeing(self) -> bool:
        return (self.current_activity == Activity.Regrouping
                and self.current_sub_activity in (SubActivity.Flee, SubActivity.AttackBlockingArmy))

    # -----------------------------------------------------------------------------------------------------------------
    # Implemented in the other modules of this package

    def move(self, target_position: Position) -> None:
        from stardust.general.unit_cluster.tactics import move
        move.move(self, target_position)

    def regroup(self, units_and_targets: UnitsAndTargets, enemy_units: set[Unit], detectors: set[MyObserver],
                sim_result: CombatSimResult, target_position: Position, has_valid_target: bool) -> None:
        from stardust.general.unit_cluster.tactics import regroup
        regroup.regroup(self, units_and_targets, enemy_units, detectors, sim_result, target_position, has_valid_target)

    def select_targets(self, target_units: set[Unit], target_position: Position,
                       static_position: bool = False) -> UnitsAndTargets:
        from stardust.general.unit_cluster import targeting
        return targeting.select_targets(self, target_units, target_position, static_position)

    def attack(self, units_and_targets: UnitsAndTargets, target_position: Position) -> None:
        from stardust.general.unit_cluster.tactics import attack
        attack.attack(self, units_and_targets, target_position)

    def contain_static(self, enemy_units: set[Unit], target_position: Position) -> None:
        from stardust.general.unit_cluster.tactics import contain_static
        contain_static.contain_static(self, enemy_units, target_position)

    def hold_choke(self, choke: Choke, defend_end: Position, units_and_targets: UnitsAndTargets) -> None:
        from stardust.general.unit_cluster.tactics import hold_choke
        hold_choke.hold_choke(self, choke, defend_end, units_and_targets)

    def stand_ground(self, enemy_units: set[Unit], target_position: Position) -> None:
        from stardust.general.unit_cluster.tactics import stand_ground
        stand_ground.stand_ground(self, enemy_units, target_position)

    def flee(self, enemy_units: set[Unit]) -> None:
        from stardust.general.unit_cluster.tactics import flee
        flee.flee(self, enemy_units)

    def move_as_ball(self, target_position: Position, ball_units: set[MyUnit]) -> bool:
        from stardust.general.unit_cluster.formations import ball
        return ball.move_as_ball(self, target_position, ball_units)

    def form_arc(self, pivot: Position, desired_distance: int) -> bool:
        from stardust.general.unit_cluster.formations import arc
        return arc.form_arc(self, pivot, desired_distance)

    def run_combat_sim(self, target_position: Position, units_and_targets: UnitsAndTargets, targets: set[Unit],
                       detectors: set[MyObserver], attacking: bool = True,
                       choke: Choke | None = None) -> CombatSimResult:
        from stardust.general.unit_cluster import combat_sim
        return combat_sim.run_combat_sim(self, target_position, units_and_targets, targets, detectors, attacking,
                                         choke)

    def run_recorded_combat_sim(self, sim_results: deque[CombatSimResult], target_position: Position,
                                units_and_targets: UnitsAndTargets, targets: set[Unit], detectors: set[MyObserver],
                                attacking: bool = True, choke: Choke | None = None) -> CombatSimResult:
        """runCombatSim overload that records the result in the given recent results."""
        from stardust.general.unit_cluster import combat_sim
        return combat_sim.run_recorded_combat_sim(self, sim_results, target_position, units_and_targets, targets,
                                                  detectors, attacking, choke)

    @staticmethod
    def consecutive_sim_results(sim_results: deque[CombatSimResult], limit: int) -> tuple[int, int, int]:
        """The number of consecutive frames the sim has agreed on its current value, and the total number of attack
        and regroup frames within the window."""
        from stardust.general.unit_cluster import combat_sim
        return combat_sim.consecutive_sim_results(sim_results, limit)

    # -----------------------------------------------------------------------------------------------------------------
    # Instrumentation

    def add_instrumentation(self, cluster_array: list[Any]) -> None:
        def sim_result_to_json(sim_result: CombatSimResult) -> dict[str, Any]:
            result: dict[str, Any] = {
                "decision": sim_result.decision,
                "myUnitCount": sim_result.my_unit_count,
                "enemyUnitCount": sim_result.enemy_unit_count,
                "initialMine": sim_result.initial_mine,
                "initialEnemy": sim_result.initial_enemy,
                "finalMine": sim_result.final_mine,
                "finalEnemy": sim_result.final_enemy,
                "enemyHasUndetectedUnits": sim_result.enemy_has_undetected_units,
                "distanceFactor": sim_result.distance_factor,
                "aggression": sim_result.aggression,
                "closestReinforcements": sim_result.closest_reinforcements,
                "reinforcementPercentage": sim_result.reinforcement_percentage,
            }
            if sim_result.narrow_choke is not None:
                result["choke_x"] = sim_result.narrow_choke.center.x
                result["choke_y"] = sim_result.narrow_choke.center.y
            else:
                result["choke_x"] = None
                result["choke_y"] = None
            if sim_result.unit_log:
                result["unitLog"] = sim_result.unit_log
            return result

        sim_result: dict[str, Any] | None = None
        regroup_sim_result: dict[str, Any] | None = None
        if self.recent_sim_results and self.recent_sim_results[-1].frame == common.current_frame:
            sim_result = sim_result_to_json(self.recent_sim_results[-1])
        if self.recent_regroup_sim_results and self.recent_regroup_sim_results[-1].frame == common.current_frame:
            regroup_sim_result = sim_result_to_json(self.recent_regroup_sim_results[-1])

        cluster_array.append({
            "center_x": self.center.x,
            "center_y": self.center.y,
            "unit_count": len(self.units),
            "is_vanguard": self.is_vanguard_cluster,
            "percent_distance_to_target": self.percentage_to_enemy_main,
            "activity": _activity_name(self.current_activity),
            "subactivity": _activity_name(self.current_sub_activity),
            "sim_result": sim_result,
            "regroup_sim_result": regroup_sim_result,
        })

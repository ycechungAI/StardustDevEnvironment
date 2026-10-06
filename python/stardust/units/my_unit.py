"""Port of Units/MyUnit.h and Units/MyUnit/{MyUnit,MyUnit_Orders,MyUnit_Move}.cpp: one of our units.

Movement is done in two phases: callers queue a move with move_to, then the most recent move command is issued at the
end of the frame (issue_move_orders). This lets high-priority movement like evading storms take priority.

There are two modes of movement: moving towards a goal position, where MyUnit follows a navigation grid or chokepoint
path and handles mineral walking; and direct moves (direct=True) towards an intermediate position, used by finer
micro like flocking where the caller does the work. In both modes MyUnit detects stuck units and unsticks them.
"""

from __future__ import annotations

from collections import deque
from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Orders, Position, Positions, TechType, TilePosition, UnitCommand, UnitCommandTypes, UnitType, \
    UpgradeType, WalkPosition
from stardust import common, config
from stardust.cpp import INT_MAX
from stardust.instrumentation import cherryvis, log
from stardust.map.path_finding.navigation_grid import GridNode, NavigationGrid
from stardust.units.unit import Unit
from stardust.util import geo, unit_util

if TYPE_CHECKING:
    from stardust.map.choke import Choke


def _is_visible(unit: bwapi.Unit) -> bool:
    """BWAPI's isVisible is intended for knowing if an enemy unit is in the fog. We also want to know if a unit is
    loaded or harvesting gas, as these remove the unit from the game's visible units list."""
    if not unit.getType().isBuilding() and not unit.isCompleted():
        return False
    if not unit.isVisible() or unit.isLoaded():
        return False
    if unit.getOrder() == Orders.HarvestGas and unit.getOrderTimer() > 0:
        return False
    return True


def _next_node(current_node: GridNode | None) -> GridNode | None:
    """We prefer to go 5 tiles ahead, but accept an earlier tile if a later one is invalid."""
    if current_node is None or current_node.next_node is None:
        return None
    node = current_node
    for _ in range(5):
        next_node = node.next_node
        if next_node is None:
            return node
        node = next_node
    return node


class _MoveCommand:
    __slots__ = ("target_position", "direct")

    def __init__(self, target_position: Position, direct: bool) -> None:
        self.target_position = target_position
        self.direct = direct


class MyUnit(Unit):
    __slots__ = (
        "order_process_index", "producer", "energy", "last_cast_frame", "completion_frame",
        "issued_order_this_frame", "recent_commands", "gather_command_frames", "return_command_frames",
        "move_command", "target_position", "currently_moving_towards", "choke_path", "grid", "grid_node",
        "currently_navigating_using_grid", "last_move_frame", "unstick_until", "_simulated_positions",
        "_simulated_heading", "_simulated_positions_updated", "_visible",
    )

    def __init__(self, unit: bwapi.Unit) -> None:
        super().__init__(unit)
        frame = common.current_frame
        latency = bwapi.Broodwar.getLatencyFrames()
        visible = _is_visible(unit)

        # When this unit's orders are processed relative to our other units. Unit orders are processed in reverse
        # order of when they were added to the visible units list; units move up in the order processing list when
        # they appear again after being hidden (see usages of hide_unit in bwgame.h).
        self.order_process_index = 10 + frame if visible else -1
        self.producer: bwapi.Unit | None = None
        self.energy = unit.getEnergy()  # Estimated energy of the unit
        self.last_cast_frame = -1  # Last frame the unit cast some kind of energy-using spell
        self.completion_frame = frame if unit.isCompleted() else -1  # Actual completion frame, -1 until complete

        self.issued_order_this_frame = False
        self.recent_commands: deque[UnitCommand] = deque([unit.getLastCommand() for _ in range(latency + 1)])
        self.gather_command_frames: set[int] = set()
        self.return_command_frames: set[int] = set()

        # The move command to be issued this frame
        self.move_command: _MoveCommand | None = None
        # The final target position of the current move. If invalid, the unit is not performing a move.
        self.target_position = Positions.Invalid
        # The grid node or choke currently being moved towards (for direct moves, the target position)
        self.currently_moving_towards = Positions.Invalid
        # The remaining choke points between the unit and its target
        self.choke_path: list[bwem.ChokePoint] = []
        # The navigation grid used for the current move; may not reach all the way to target_position (e.g. a worker
        # that needs to mineral walk navigates to the mineral walk choke first)
        self.grid: NavigationGrid | None = None
        self.grid_node: GridNode | None = None  # The current grid node occupied by the unit in the above grid
        self.currently_navigating_using_grid = False
        self.last_move_frame = 0  # The last frame where we sent a move command to the unit
        self.unstick_until = -1  # If we sent a command to unstick the unit, when to send normal commands again

        # Simulated positions / headings up to latency frames ahead, assuming no collisions
        self._simulated_positions = [unit.getPosition()] * (latency + 1)
        self._simulated_heading = [0] * (latency + 1)
        self._simulated_positions_updated = False
        self._visible = visible

    def __str__(self) -> str:
        return f"{self.type}:{self.id}@{WalkPosition(self.get_tile_position())}"

    # -----------------------------------------------------------------------------------------------------------------
    # Updates

    def update(self, unit: bwapi.Unit | None) -> None:
        from stardust.units import units

        if unit is None or not unit.exists():
            return

        frame = common.current_frame
        game = bwapi.Broodwar

        # Update the order process index if the unit's visibility has changed
        if _is_visible(unit) != self._visible:
            if self._visible:
                self._visible = False
                self.order_process_index = -1
            else:
                self._visible = True
                self.order_process_index = 10 + frame
                self.order_process_timer = 0

        # Update command positions
        self.recent_commands.popleft()
        command = unit.getLastCommand()
        if command.target is not None:
            # UnitCommand::assignTarget
            target_position = command.target.getPosition().makeValid()
            command.x = target_position.x
            command.y = target_position.y
        self.recent_commands.append(command)
        self._simulated_positions_updated = False

        if unit.isCompleted() and self.completion_frame == -1:
            self.producer = None
            self.completion_frame = frame

        if self.energy - unit.getEnergy() > 45:
            self.last_cast_frame = frame
            log.get(f"Unit cast spell: {self}")
        self.energy = unit.getEnergy()

        # If this unit has just gone on cooldown, add an upcoming attack on its target
        last_command = unit.getLastCommand()
        if last_command.getType() == UnitCommandTypes.Attack_Unit or unit.getOrder() == Orders.AttackUnit:
            cooldown = max(unit.getGroundWeaponCooldown(), unit.getAirWeaponCooldown())
            if cooldown > 0 and cooldown > (self.cooldown_until - frame + 1):
                target = units.get(last_command.getTarget() if last_command.getType() == UnitCommandTypes.Attack_Unit
                                   else unit.getOrderTarget())
                if target is not None:
                    target.add_upcoming_attack(self)

        super().update(unit)

        self.issued_order_this_frame = False
        self.move_command = None

        # Guard against buildings having a deep training queue
        if (config.LOGGING_ENABLED and game.self().supplyUsed() < 380
                and last_command.getType() not in (UnitCommandTypes.Cancel_Train, UnitCommandTypes.Cancel_Train_Slot)
                and len(unit.getTrainingQueue()) > 1
                and self.last_command_frame < frame - game.getLatencyFrames()):
            log.get(f"WARNING: Training queue for {unit.getType()} @ {unit.getTilePosition()} is too deep! "
                    "Cancelling later units.")
            log.get("Queue: " + ",".join(str(t) for t in unit.getTrainingQueue()))
            unit.cancelTrain(1)

        # Cancel dying incomplete buildings
        if (self.type.isBuilding() and not self.completed and unit.isUnderAttack() and unit.canCancelConstruction()
                and (self.last_shields + self.last_health) < 20):
            log.get(f"Cancelling dying {self.type} @ {self.get_tile_position()}")
            unit.cancelConstruction()

    def is_being_manufactured_or_carried(self) -> bool:
        unit = self.bwapi_unit
        if unit is None:
            return False

        # For Protoss, anything incomplete that isn't a building is currently being manufactured.
        # Will need to change if we ever support building Zerg units.
        if not unit.isCompleted() and not unit.getType().isBuilding():
            return True

        # Otherwise look at whether it is loaded
        return unit.isLoaded()

    def attack_unit(self, target: Unit, units_and_targets: list[tuple[MyUnit, Unit | None]] | None = None,
                    cluster_attacking: bool = True, enemy_aoe_radius: int = 0) -> None:
        game = bwapi.Broodwar
        frame = common.current_frame
        assert target.bwapi_unit is not None and self.bwapi_unit is not None

        # If the enemy is a long way away, move to it
        dist = self.get_distance(target)
        if dist > 320 or not target.bwapi_unit.isVisible():
            self.move_to(target.sim_position)
            return

        remaining_latency = game.getRemainingLatencyFrames()
        my_predicted_position = self.predict_position(remaining_latency)
        target_predicted_position = target.predict_position(remaining_latency)
        predicted_dist = geo.edge_to_edge_distance(self.type, my_predicted_position, target.type,
                                                   target_predicted_position)
        range_to_target = self.range(target)

        # Predicted to be in range or target is stationary: attack
        if predicted_dist <= range_to_target or target_predicted_position == target.sim_position:
            # Re-send the attack command when we are coming into range
            force_attack_command = False
            if (dist > range_to_target and predicted_dist <= range_to_target
                    and self.cooldown_until < frame + remaining_latency
                    and self.bwapi_unit.getLastCommand().type == UnitCommandTypes.Attack_Unit):
                # If we recently sent the attack command, check our predicted distance when it will kick in.
                # If we still expect to be in range, we don't need to do anything.
                last_command_takes_effect = self.last_command_frame + game.getLatencyFrames() - frame
                if last_command_takes_effect >= 0:
                    if last_command_takes_effect == 0:
                        dist_at_attack_frame = dist
                    else:
                        dist_at_attack_frame = geo.edge_to_edge_distance(
                            self.type, self.predict_position(last_command_takes_effect),
                            target.type, target.predict_position(last_command_takes_effect))
                    if dist_at_attack_frame <= range_to_target:
                        return

                # Otherwise force the attack command
                force_attack_command = True

            self.attack(target.bwapi_unit, force_attack_command)
            return

        # Plot an intercept course
        intercept_position = self.intercept(target)
        if not intercept_position.isValid():
            intercept_position = target.predict_position(game.getLatencyFrames() + 2)
        self.move_to(intercept_position)

    def is_ready(self) -> bool:
        # When we have a large army, only micro units every other frame to avoid having commands dropped
        if bwapi.Broodwar.self().supplyUsed() > 250 and (common.current_frame % 2) != (self.id % 2):
            return False
        return not self.immobile

    def unstick(self) -> bool:
        unit = self.bwapi_unit

        # Unit is dead
        if unit is None:
            return True

        frame = common.current_frame

        # If we recently sent a command meant to unstick the unit, give it a bit of time to kick in
        if self.unstick_until > frame:
            return True

        # If the unit is listed as stuck, send a stop command unless we have done so recently
        if unit.isStuck() and self.unstick_until < frame - 10:
            self.stop()
            self.unstick_until = frame + bwapi.Broodwar.getRemainingLatencyFrames()
            return True

        return False

    def get_unstick_until(self) -> int:
        return self.unstick_until

    def get_last_move_frame(self) -> int:
        return self.last_move_frame

    def moving_to(self) -> Position:
        return self.target_position

    def simulate_position(self, frames: int) -> Position:
        if not self._simulated_positions_updated:
            self._update_simulated_positions()
        return self._simulated_positions[frames - 1]

    def simulate_heading(self, frames: int) -> int:
        if not self._simulated_positions_updated:
            self._update_simulated_positions()
        return self._simulated_heading[frames - 1]

    def _update_simulated_positions(self) -> None:
        from stardust.players import players

        self._simulated_positions_updated = True
        assert self.bwapi_unit is not None

        x = self.last_position.x
        bw_x = x << 8
        y = self.last_position.y
        bw_y = y << 8
        heading = self.bw_heading()

        speed = self.bw_speed()
        top_speed = players.unit_top_speed(self.player, self.type)
        bw_top_speed = players.unit_bw_top_speed(self.player, self.type)
        speed = min(speed, bw_top_speed)
        acceleration = unit_util.acceleration(self.type, top_speed)
        if not self.bwapi_unit.isAccelerating():
            acceleration = -acceleration

        turn_rate = self.type.turnRadius()
        for i, command in enumerate(self.recent_commands):
            # Compute desired heading to move target at this frame
            command_target = command.getTargetPosition()
            if command.type == UnitCommandTypes.Move and command_target.isValid():
                desired_heading = geo.bw_direction(Position(command_target.x - x, command_target.y - y))
            else:
                desired_heading = heading

            # Simulate movement
            bw_x, bw_y, heading, speed = geo.bw_movement(bw_x, bw_y, heading, desired_heading, turn_rate, speed,
                                                         acceleration, bw_top_speed)

            # Assign simulated position and heading
            x = bw_x >> 8
            y = bw_y >> 8
            self._simulated_positions[i] = Position(x, y)
            self._simulated_heading[i] = heading

    # -----------------------------------------------------------------------------------------------------------------
    # Orders (MyUnit_Orders.cpp)

    def _duplicate_order(self, description: str) -> bool:
        if self.issued_order_this_frame:
            log.get(f"DUPLICATE ORDER: {self}: {description}")
            return True
        return False

    def _debug_order(self, description: str) -> None:
        if config.DEBUG_UNIT_ORDERS:
            cherryvis.log(f"Order: {description}", self.id)

    def move(self, position: Position, force: bool = False) -> None:
        unit = self.bwapi_unit
        assert unit is not None
        if self._duplicate_order(f"Move to {WalkPosition(position)}{' (forced)' if force else ''}"):
            return

        def skip_move_command() -> bool:
            if force or unit.isStuck():
                return False
            last_command = unit.getLastCommand()
            if last_command.getType() != UnitCommandTypes.Move:
                return False
            if last_command.getTargetPosition().getApproxDistance(position) > 3:
                return False

            # Don't resend orders to similar positions too quickly
            if self.last_command_frame > common.current_frame - bwapi.Broodwar.getLatencyFrames() - 3:
                return False

            # Don't resend order if the unit is moving
            return self.last_command_frame < self.frame_last_moved

        if skip_move_command():
            return

        self.issued_order_this_frame = unit.move(position)
        if self.issued_order_this_frame:
            self.last_move_frame = common.current_frame
        self._debug_order(f"Move to {WalkPosition(position)}{' (forced)' if force else ''}")

    def attack(self, target: bwapi.Unit | None, force: bool = False) -> None:
        unit = self.bwapi_unit
        assert unit is not None
        if target is None or not target.exists():
            log.get(f"ATTACK INVALID TARGET: {self}")
            return
        if self._duplicate_order(f"Attack {target.getType()} @ {WalkPosition(target.getPosition())}"):
            return

        current_command = unit.getLastCommand()
        if (not force and not unit.isStuck() and current_command.getType() == UnitCommandTypes.Attack_Unit
                and current_command.getTarget() == target):
            return

        self.issued_order_this_frame = unit.attack(target)
        self._debug_order(f"Attack {target.getType()} @ {WalkPosition(target.getPosition())}")

    def attack_move(self, position: Position) -> None:
        unit = self.bwapi_unit
        assert unit is not None
        if self._duplicate_order(f"Attack move to {WalkPosition(position)}"):
            return

        current_command = unit.getLastCommand()
        if (not unit.isStuck() and current_command.getType() == UnitCommandTypes.Attack_Move
                and current_command.getTargetPosition() == position):
            return

        self.issued_order_this_frame = unit.attack(position)
        self._debug_order(f"Attack move to {WalkPosition(position)}")

    def right_click(self, target: bwapi.Unit | None) -> None:
        unit = self.bwapi_unit
        assert unit is not None
        if target is None or not target.exists():
            log.get(f"RIGHT-CLICK INVALID TARGET: {self}")
            return
        if self._duplicate_order(f"Right-click {target.getType()} @ {WalkPosition(target.getPosition())}"):
            return

        # Unless it is a mineral field, don't click on the same target again
        target_type = target.getType()
        if not target_type.isMineralField():
            current_command = unit.getLastCommand()
            if (current_command.getType() == UnitCommandTypes.Right_Click_Unit
                    and current_command.getTargetPosition() == target.getPosition()):
                return

        if unit.rightClick(target):
            self.issued_order_this_frame = True
            if target_type.isMineralField():
                self.gather_command_frames.add(common.current_frame)
            elif target_type.isResourceDepot():
                self.return_command_frames.add(common.current_frame)
        self._debug_order(f"Right-click {target_type} @ {WalkPosition(target.getPosition())}")

    def build(self, unit_type: UnitType, tile: TilePosition) -> bool:
        unit = self.bwapi_unit
        assert unit is not None
        if not tile.isValid():
            log.get(f"BUILD INVALID TILE: {self}")
            return False
        if self._duplicate_order(f"Build {unit_type} @ {WalkPosition(tile)}"):
            return False
        self.issued_order_this_frame = unit.build(unit_type, tile)
        self._debug_order(f"Build {unit_type} @ {WalkPosition(tile)}")
        return self.issued_order_this_frame

    def train(self, unit_type: UnitType) -> bool:
        unit = self.bwapi_unit
        assert unit is not None
        if self._duplicate_order(f"Train {unit_type}"):
            return False
        self.issued_order_this_frame = unit.train(unit_type)
        self._debug_order(f"Train {unit_type}")
        return self.issued_order_this_frame

    def upgrade(self, upgrade_type: UpgradeType) -> bool:
        unit = self.bwapi_unit
        assert unit is not None
        if self._duplicate_order(f"Upgrade {upgrade_type}"):
            return False
        self.issued_order_this_frame = unit.upgrade(upgrade_type)
        self._debug_order(f"Upgrade {upgrade_type}")
        return self.issued_order_this_frame

    def research(self, tech_type: TechType) -> bool:
        unit = self.bwapi_unit
        assert unit is not None
        if self._duplicate_order(f"Research {tech_type}"):
            return False
        self.issued_order_this_frame = unit.research(tech_type)
        self._debug_order(f"Research {tech_type}")
        return self.issued_order_this_frame

    def stop(self) -> None:
        unit = self.bwapi_unit
        assert unit is not None
        if self._duplicate_order("Stop"):
            return
        self.issued_order_this_frame = unit.stop()
        self._debug_order("Stop")

    def cancel_construction(self) -> None:
        unit = self.bwapi_unit
        assert unit is not None
        if self._duplicate_order("Cancel Construction"):
            return
        self.issued_order_this_frame = unit.cancelConstruction()
        self._debug_order("Cancel Construction")

    def load(self, cargo: bwapi.Unit) -> None:
        unit = self.bwapi_unit
        assert unit is not None
        if self._duplicate_order(f"Load {cargo.getType()} @ {cargo.getTilePosition()}"):
            return

        # Don't re-issue the same command
        current_command = unit.getLastCommand()
        if current_command.getType() == UnitCommandTypes.Load and current_command.getTarget() == cargo:
            return

        self.issued_order_this_frame = unit.load(cargo)
        self._debug_order(f"Load {cargo.getType()} @ {cargo.getTilePosition()}")

    def unload_all(self, pos: Position) -> None:
        unit = self.bwapi_unit
        assert unit is not None
        if self._duplicate_order("Unload All"):
            return
        self.issued_order_this_frame = unit.unloadAll(pos)
        self._debug_order(f"Unload All {WalkPosition(pos)}")

    def unload(self, cargo: bwapi.Unit) -> None:
        unit = self.bwapi_unit
        assert unit is not None
        if self._duplicate_order(f"Unload {cargo.getType()} @ {cargo.getTilePosition()}"):
            return
        self.issued_order_this_frame = unit.unload(cargo)
        self._debug_order(f"Unload {cargo.getType()} @ {cargo.getTilePosition()}")

    def set_producer_rally_position(self, pos: Position) -> None:
        producer = self.producer
        if producer is None:
            return
        current = producer.getRallyPosition()
        if current.isValid() and pos.getApproxDistance(current) < 32:
            return
        producer.setRallyPoint(pos)
        if config.DEBUG_UNIT_ORDERS:
            cherryvis.log(f"Order: Set rally position to {WalkPosition(pos)}", producer.getID())

    # -----------------------------------------------------------------------------------------------------------------
    # Movement (MyUnit_Move.cpp)

    def move_to(self, position: Position, direct: bool = False) -> None:
        """Queue a move; the last one queued this frame is issued in issue_move_orders."""
        if not position.isValid():
            log.get(f"ERROR: MOVE TO INVALID POSITION: {self} - {position}")
            cherryvis.log(f"ERROR: MOVE TO INVALID POSITION: {self} - {position}", self.id)
            return
        self.move_command = _MoveCommand(position, direct)

    def issue_move_orders(self) -> None:
        if self.issued_order_this_frame:
            return
        unit = self.bwapi_unit
        if unit is None:
            return

        # Clear the move command if this unit is loaded
        if unit.isLoaded():
            self._reset_move_data()
            return

        # Process a new move command
        if self.move_command is not None and self.move_command.target_position != self.target_position:
            self._initiate_move()
            return

        # Jump out now if the unit is not currently doing a move
        if not self.target_position.isValid():
            return

        # Unstick the unit if it is stuck. There are two forms of sticking: general stuck units that can't do
        # anything, and units that are probably stuck on terrain or a building.
        if self.unstick() or self._unstick_move_unit():
            return

        # If the unit has just been unstuck, reissue the command to move towards our current target position
        if self.unstick_until == common.current_frame:
            self.move(self.currently_moving_towards)
            return

        self._update_move_waypoints()

    def _initiate_move(self) -> None:
        from stardust.map.path_finding import path_finding

        assert self.move_command is not None and self.bwapi_unit is not None
        self._reset_move_data()
        self.target_position = self.move_command.target_position

        # No special pathing is required if the movement mode is direct or the unit is flying
        if self.move_command.direct or self.bwapi_unit.isFlying():
            self._move_to_next_waypoint()
            return

        # Get the choke path
        path, _ = path_finding.get_choke_point_path(self.bwapi_unit.getPosition(), self.target_position,
                                                    self.bwapi_unit.getType(),
                                                    path_finding.PathFindingOptions.UseNearestBWEMArea)
        self.choke_path.extend(path)

        # Attempt to get an appropriate navigation grid
        self._reset_grid()

        if config.DEBUG_UNIT_ORDERS:
            message = f"Order: Initiating move to {WalkPosition(self.target_position)}"
            if self.grid is not None:
                message += f"; grid target {self.grid.goal}"
            if self.grid_node is not None:
                message += f"; initial grid node {self.grid_node}"
            message += "; choke path " + ", ".join(str(c.Center()) for c in self.choke_path)
            cherryvis.log(message, self.id)

        self._move_to_next_waypoint()

    def _reset_move_data(self) -> None:
        self.target_position = Positions.Invalid
        self.currently_moving_towards = Positions.Invalid
        self.grid = None
        self.choke_path.clear()
        self.grid_node = None
        self.currently_navigating_using_grid = False

    def _has_reached_next_choke(self) -> bool:
        from stardust.map import game_map

        if not self.choke_path:
            return False
        assert self.bwapi_unit is not None

        # Wait until the unit is close enough to the current target
        if self.bwapi_unit.getDistance(self.currently_moving_towards) > 100:
            return False

        # If the current target is a narrow ramp, wait until we can see the high elevation tile.
        # We want to make sure we go up the ramp far enough to see anything potentially blocking the ramp.
        c = game_map.choke(self.choke_path[0])
        if c is not None and c.is_narrow_choke and c.is_ramp and not bwapi.Broodwar.isVisible(c.high_elevation_tile):
            return False

        return True

    def _move_to_next_waypoint(self) -> None:
        from stardust.map import game_map

        assert self.bwapi_unit is not None
        bwem_map = bwem.Instance()

        # Current grid node is close to the target (approx. 3 tiles away)
        if self.grid_node is not None and self.grid_node.cost <= 90:
            self.grid = None
            self.grid_node = None

            # Short-circuit if the unit is in the target area. This means we are close to the destination and just
            # need to do a simple move from here. State will be reset after latency frames to avoid resetting the
            # order later.
            unit_area = bwem_map.GetNearestArea(WalkPosition(self.bwapi_unit.getPosition()))
            target_area = bwem_map.GetArea(WalkPosition(self.target_position))
            if target_area is None or target_area == unit_area:
                self.choke_path.clear()
                self.currently_moving_towards = self.target_position
                self.move(self.currently_moving_towards)
                return

            # The unit is not in the target area, so we were using the grid to navigate to a choke.
            # Pop the choke path until it fits the current position and fall through to choke-based navigation.
            if self.currently_navigating_using_grid:
                self._update_choke_path(unit_area)
                self.currently_navigating_using_grid = False

        # Navigate using the current grid node, if we have it
        if self.grid_node is not None:
            next_node = _next_node(self.grid_node)
            if next_node is not None:
                self.currently_navigating_using_grid = True
                self.currently_moving_towards = next_node.center()
                self.move(self.currently_moving_towards)
                return

            # We have a valid grid, but we're not in a connected grid node. Ensure the choke path is updated and fall
            # through; we will pick up the grid path when we can.
            if self.currently_navigating_using_grid:
                self._update_choke_path(bwem_map.GetNearestArea(WalkPosition(self.bwapi_unit.getPosition())))
                self.currently_navigating_using_grid = False

        # If there is no choke path, and we couldn't navigate using the grid, just move to the position
        if not self.choke_path:
            self.currently_moving_towards = self.target_position
            self.move(self.currently_moving_towards)
            return

        next_waypoint = self.choke_path[0]
        c = game_map.choke(next_waypoint)
        assert c is not None

        # Check if the next waypoint needs to be mineral walked
        if self.mineral_walk(c):
            return

        # Determine the position on the choke to move towards; default to the center
        self.currently_moving_towards = c.center

        if c.is_narrow_choke and c.is_ramp:
            # If it is a narrow ramp, move towards the point with highest elevation. We do this to make sure we
            # explore the higher elevation part of the ramp before bugging out if it is blocked.
            self.currently_moving_towards = Position(c.high_elevation_tile) + Position(16, 16)
        else:
            # Get the next position after this waypoint
            next_position = self.target_position
            if len(self.choke_path) > 1:
                next_position = Position(self.choke_path[1].Center()) + Position(4, 4)

            # Move to the part of the choke closest to the next position
            best_dist = self.currently_moving_towards.getApproxDistance(next_position)
            for walk_position in next_waypoint.Geometry():
                if not game_map.is_walkable_tile(TilePosition(walk_position)):
                    continue
                pos = Position(walk_position) + Position(4, 4)
                dist = pos.getApproxDistance(next_position)
                if dist < best_dist:
                    best_dist = dist
                    self.currently_moving_towards = pos

        # Check if we have arrived at the waypoint. This might happen if the move is being initiated while we are very
        # close to the choke.
        if self._has_reached_next_choke():
            self.choke_path.pop(0)
            self._move_to_next_waypoint()
            return

        self.move(self.currently_moving_towards)

    def _update_move_waypoints(self) -> None:
        if self.mineral_walk(None):
            return
        assert self.bwapi_unit is not None

        current_command = self.bwapi_unit.getLastCommand()

        # If this unit has just finished a mineral walk, resend the move command until it works
        command_target = current_command.getTarget()
        if (current_command.getType() == UnitCommandTypes.Right_Click_Unit and command_target is not None
                and command_target.getType().isMineralField()):
            self.move(self.currently_moving_towards)
            return

        # Check if the unit has been ordered to do something else and clear our move data
        if (current_command.getType() != UnitCommandTypes.Move
                or current_command.getTargetPosition().getApproxDistance(self.currently_moving_towards) > 3):
            if config.DEBUG_UNIT_ORDERS:
                cherryvis.log("Order: Aborting move as command has changed", self.id)
            self._reset_move_data()
            return

        # We have a grid we can use for navigation
        if self.grid is not None and self.grid_node is not None:
            self.grid.update()

            # If we are no longer in the same node, update it and move to the next waypoint
            if self.tile_position_x != self.grid_node.x or self.tile_position_y != self.grid_node.y:
                self.grid_node = self.grid.node(self.get_tile_position())
                self._move_to_next_waypoint()
                return

            # Check if the waypoint we are currently using is still valid
            next_node = _next_node(self.grid_node)
            if next_node is not None:
                # React if a grid update has changed the desired waypoint
                if self.currently_moving_towards != next_node.center():
                    self._move_to_next_waypoint()
                return

            # In all other cases fall through - we do not have a valid grid node so we navigate using choke points

        if self._has_reached_next_choke():
            self.choke_path.pop(0)
            self._move_to_next_waypoint()
            return

        # For a direct move, resend the move command frequently to avoid units doing weird stuff because of collisions
        if self.last_move_frame < common.current_frame - bwapi.Broodwar.getLatencyFrames() - 12:
            self.move(self.currently_moving_towards, True)

    def _reset_grid(self) -> None:
        from stardust.map import game_map
        from stardust.map.path_finding import path_finding

        assert self.bwapi_unit is not None

        # If there is a choke in the BWEM path requiring mineral walking, try to get a navigation grid to that choke
        mineral_walking_choke: Choke | None = None
        if self.bwapi_unit.getType().isWorker() and game_map.map_specific_override().has_mineral_walking():
            for chokepoint in self.choke_path:
                c = game_map.choke(chokepoint)
                if c is not None and c.requires_mineral_walk:
                    mineral_walking_choke = c
                    break

        if mineral_walking_choke is not None:
            self.grid = path_finding.get_navigation_grid(mineral_walking_choke.center)
        else:
            # We have ruled out mineral walking, so try to get a grid to the target
            self.grid = path_finding.get_navigation_grid(self.target_position)

            # If that failed, try to get a grid to the furthest choke we can
            if self.grid is None:
                for bwem_choke in reversed(self.choke_path):
                    c = game_map.choke(bwem_choke)
                    assert c is not None
                    self.grid = path_finding.get_navigation_grid(TilePosition(c.center))

                    # Don't use a grid if the current node is invalid or if the goal is very close
                    if self.grid is not None:
                        node = self.grid.node(self.get_tile_position())
                        if node.next_node is None or node.cost < 90:
                            self.grid = None

                    if self.grid is not None:
                        break

        # If we have a grid, get the first grid node
        if self.grid is not None:
            self.grid_node = self.grid.node(self.bwapi_unit.getPosition())
            self.currently_navigating_using_grid = True

    def _update_choke_path(self, unit_area: bwem.Area | None) -> None:
        from stardust.map import game_map

        while self.choke_path:
            next_choke = self.choke_path[0]

            # If the choke does not link the area we are currently in, pop it and continue
            first, second = next_choke.GetAreas()
            if first != unit_area and second != unit_area:
                self.choke_path.pop(0)
                continue

            # If the choke is the only one left, break now
            if len(self.choke_path) == 1:
                break

            # If the next choke also includes the area we are currently in, then it should be next
            second_first, second_second = self.choke_path[1].GetAreas()
            if second_first == unit_area or second_second == unit_area:
                self.choke_path.pop(0)

            break

        if not self.choke_path:
            return

        # Finally pop the choke if the unit is close enough to it that we want to use the next choke.
        # Exceptions: choke requires mineral walk or choke is a ramp and we can't see the high elevation tile yet.
        c = game_map.choke(self.choke_path[0])
        assert c is not None and self.bwapi_unit is not None
        if (c.requires_mineral_walk or self.bwapi_unit.getDistance(c.center) > 100
                or (c.is_narrow_choke and c.is_ramp and not bwapi.Broodwar.isVisible(c.high_elevation_tile))):
            return

        self.choke_path.pop(0)

    def _unstick_move_unit(self) -> bool:
        from stardust.map import game_map

        unit = self.bwapi_unit
        assert unit is not None
        frame = common.current_frame
        game = bwapi.Broodwar

        # First validate that the last move command was issued more than 6+LF frames ago
        if frame - self.last_move_frame < game.getLatencyFrames() + 6:
            return False

        # Now validate that the last move command matches the current move target
        current_command = unit.getLastCommand()
        command_target = current_command.getTargetPosition()
        if (current_command.getType() != UnitCommandTypes.Move or not command_target.isValid()
                or command_target != self.currently_moving_towards):
            return False

        # Don't consider the unit stuck if it is within 32 pixels of the target (it may have arrived)
        if unit.getDistance(self.currently_moving_towards) < 32:
            return False

        # Don't consider the unit stuck if it is moving and the order is not Guard or PlayerGuard
        if (unit.isMoving() and (abs(unit.getVelocityX()) > 0.001 or abs(unit.getVelocityY()) > 0.001)
                and unit.getOrder() not in (Orders.Guard, Orders.PlayerGuard)):
            return False

        # If we haven't moved for the past 48 frames, assume previous attempts to unstick the unit have failed and try
        # to reset completely
        if self.frame_last_moved < frame - 48:
            self.stop()
            self.unstick_until = frame + game.getRemainingLatencyFrames()
            return True

        # We are stuck. If we are close to unwalkable terrain, move along it to get us moving again.
        if game_map.unwalkable_proximity(self.tile_position_x, self.tile_position_y) < 2:
            # Scores the distance from a neighbouring tile to the target position.
            # Prefers tiles that are farther away from unwalkable terrain.
            best = Positions.Invalid
            best_dist = INT_MAX

            def score_tile(tile: TilePosition) -> None:
                nonlocal best, best_dist
                if not tile.isValid() or not game_map.is_walkable_tile(tile):
                    return
                position = Position(tile) + Position(16, 16)
                dist = command_target.getApproxDistance(position)
                if game_map.unwalkable_proximity(tile.x, tile.y) > 1:
                    dist //= 2
                if dist < best_dist:
                    best_dist = dist
                    best = position

            current_tile = self.get_tile_position()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (-1, 1), (1, -1)):
                score_tile(current_tile + TilePosition(dx, dy))
            if not best.isValid():
                for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -2)):
                    score_tile(current_tile + TilePosition(dx, dy))

            if best.isValid():
                self.move(best)
                self.unstick_until = frame + game.getRemainingLatencyFrames() + 4
                return True

        # Reissue the move command
        self.move(self.currently_moving_towards, True)
        self.unstick_until = frame + game.getRemainingLatencyFrames()
        return True

    def mineral_walk(self, c: Choke | None) -> bool:
        """Overridden by workers, which can walk through mineral fields."""
        return False

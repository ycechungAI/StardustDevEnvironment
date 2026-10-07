"""Port of Units/MyCorsair.h and Units/MyUnit/MyCorsair.cpp: corsair micro that manages acceleration and attack angle,
and kites scourge."""

from __future__ import annotations

import bwapi
from bwapi import Positions, UnitCommandTypes, UnitTypes, WalkPosition
from stardust import common, config
from stardust.instrumentation import cherryvis
from stardust.units.my_unit import MyUnit
from stardust.units.unit import Unit
from stardust.util import geo, unit_util

_ATTACK_FRAMES = 8


class MyCorsair(MyUnit):
    __slots__ = ()

    def is_ready(self) -> bool:
        if not super().is_ready():
            return False

        # Not ready if we are in our attack animation
        if (self.last_seen_attacking > 0 and self.last_seen_attacking + _ATTACK_FRAMES
                > common.current_frame + bwapi.Broodwar.getRemainingLatencyFrames()):
            return False

        return True

    def attack_unit(self, target: Unit, units_and_targets: list[tuple[MyUnit, Unit | None]] | None = None,
                    cluster_attacking: bool = True, enemy_aoe_radius: int = 0) -> None:
        from stardust.map import game_map
        from stardust.players import players

        if units_and_targets is None:
            units_and_targets = []
        assert self.bwapi_unit is not None and target.bwapi_unit is not None
        game = bwapi.Broodwar
        frame = common.current_frame
        remaining_latency = game.getRemainingLatencyFrames()

        def debug(message: str) -> None:
            if config.DEBUG_UNIT_ORDERS:
                cherryvis.log(message, self.id)

        # If the enemy is a long way away, move to it
        dist = self.get_distance(target)
        if dist > 320 or not target.bwapi_unit.isVisible():
            debug(f"Attack: Moving to target @ {WalkPosition(target.sim_position)}")
            self.move_to(target.sim_position)
            return

        def validate_attack(frames_in_future: int) -> bool:
            my_pos = self.last_position if frames_in_future == 0 else self.simulate_position(frames_in_future)
            target_pos = target.last_position if frames_in_future == 0 else target.predict_position(frames_in_future)

            command_dist = geo.edge_to_edge_distance(self.type, my_pos, target.type, target_pos)
            if command_dist > self.air_range():
                return False

            my_heading = self.bw_heading() if frames_in_future == 0 else self.simulate_heading(frames_in_future)
            angle_diff = geo.bw_angle_diff(geo.bw_direction(target_pos - my_pos), my_heading)
            return angle_diff <= self.type.turnRadius()

        # If we have sent an attack order recently, check if we still expect the attack to take place.
        # We don't need to consider when we are in our attack animation, as that is handled by is_ready.
        if (self.bwapi_unit.getLastCommand().getType() == UnitCommandTypes.Attack_Unit
                and self.last_command_frame > frame - game.getLatencyFrames() - 1):
            last_command_takes_effect = self.last_command_frame + game.getLatencyFrames() - frame
            if validate_attack(last_command_takes_effect):
                debug("Attack: Attack is pending and valid; don't touch")
                return
            debug("Attack: Attack is not expected to succeed, fall through to attack logic")

        # Determine whether to move or attack. We move in the following situations:
        # - We want to kite the target
        # - We are on cooldown
        # - We have just finished our attack animation and need to issue a move command to either accelerate or
        #   decelerate during the next attack
        # - We want to attack but are not in range or do not have the correct attack angle

        def target_moving_away() -> bool:
            return (target.predict_position(1).getApproxDistance(self.last_position)
                    > target.last_position.getApproxDistance(self.last_position))

        # Start by determining if we should kite. We only kite scourge that might be trying to attack us.
        def should_kite() -> bool:
            # Only kite scourge that are moving towards us
            if target.type != UnitTypes.Zerg_Scourge or target_moving_away():
                return False

            # Don't kite if another friendly unit is closer to it
            for other_unit, _ in units_and_targets:
                if other_unit.id != self.id and other_unit.get_distance(target, target.sim_position) < dist:
                    return False

            # Kite if we are too close
            if dist < 32:
                return True

            # Kite if we aren't near our top speed
            return self.bw_speed() * 10 < players.unit_bw_top_speed(self.player, self.type) * 9

        if should_kite():
            move_target = self.last_position + (self.last_position - target.last_position)
            accelerate = True
            decelerate = True
            debug(f"Moving to kite @ {WalkPosition(move_target)}")
        else:
            # Whether the first speed is higher than the second, given some buffer
            def faster(first: int, second: int, buffer: int) -> bool:
                return first + buffer > second

            target_top_speed = players.unit_bw_top_speed(target.player, target.type)

            # We want to accelerate if the enemy is moving away from us and is fast
            accelerate = (faster(target_top_speed, self.bw_speed(), 10) and target_moving_away()
                          and (faster(target.bw_speed(), self.bw_speed(), 250) or dist > 96))

            # We want to decelerate if the enemy is slower than us and we are in range
            decelerate = faster(self.bw_speed(), target_top_speed, -100) and self.is_in_our_weapon_range(target)

            cherryvis.log(f"Target top speed: {target_top_speed}"
                          f"; my top speed: {players.unit_bw_top_speed(self.player, self.type)}"
                          f"; my speed: {self.bw_speed()}"
                          f"; target moving away: {int(target_moving_away())}"
                          f"; target speed: {target.bw_speed()}"
                          f"; dist: {dist}"
                          f"; accelerate: {int(accelerate)}"
                          f"; decelerate: {int(decelerate)}"
                          f"; accelerating: {int(self.bwapi_unit.isAccelerating())}", self.id)

            # If this is the frame that our attack animation ended, move if we need to change speed
            if ((accelerate or (decelerate and self.bwapi_unit.isAccelerating()))
                    and self.last_seen_attacking + _ATTACK_FRAMES == frame + remaining_latency):
                debug("Attack: Moving for one frame before continuing attack")
            elif decelerate or (self.cooldown_until - frame <= remaining_latency
                                and validate_attack(remaining_latency)):
                debug("Sending attack command")
                self.attack(target.bwapi_unit, True)
                return

            move_target = target.predict_position(remaining_latency)
            debug(f"Attack: Moving to target @ {WalkPosition(move_target)}")

        if accelerate:
            # Scale to ensure we move further than our halt distance
            vector = geo.scale_vector(move_target - self.last_position, unit_util.halt_distance(self.type) + 192)
            if vector != Positions.Invalid:
                move_target = self.last_position + vector

        if decelerate:
            # Scale depending on where we expect to be
            my_pos = (self.simulate_position(remaining_latency) if self.frame_last_moved == frame
                      else self.last_position)
            target_pos = target.predict_position(remaining_latency)
            vector = geo.scale_vector(target_pos - my_pos, 8)
            if vector != Positions.Invalid:
                potential_move_target = my_pos + vector
                if potential_move_target.isValid():
                    move_target = potential_move_target

        if move_target.isValid():
            self.move(move_target)
        else:
            debug("Attack: No valid move target; moving to main")
            my_main = game_map.get_my_main()
            assert my_main is not None
            self.move(my_main.get_position())

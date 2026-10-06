"""Port of Units/MyDragoon.h and Units/MyUnit/MyDragoon.cpp: dragoons kite, range down bunkers and work around the
dragoon attack-cancel stuck bug."""

from __future__ import annotations

import bwapi
from bwapi import Position, Positions, UnitCommandTypes, UnitTypes
from stardust import common, config
from stardust.cpp import cdiv, to_int
from stardust.instrumentation import cherryvis
from stardust.units.my_unit import MyUnit
from stardust.units.unit import Unit
from stardust.util import boids, geo

_DRAGOON_ATTACK_FRAMES = 6

# Boid parameters for kiting
# TODO (upstream): These parameters need to be tuned
_TARGET_WEIGHT = 64.0
_SEPARATION_DETECTION_LIMIT_FACTOR = 1.5
_SEPARATION_WEIGHT = 96.0


class MyDragoon(MyUnit):
    __slots__ = ("last_attack_started_at", "next_attack_predicted_at", "potentially_stuck_since")

    def __init__(self, unit: bwapi.Unit) -> None:
        super().__init__(unit)
        self.last_attack_started_at = 0
        self.next_attack_predicted_at = 0
        self.potentially_stuck_since = 0  # Frame the unit might have been stuck since, or 0 if it isn't stuck

    def get_last_attack_started_at(self) -> int:
        return self.last_attack_started_at

    def get_next_attack_predicted_at(self) -> int:
        return self.next_attack_predicted_at

    def get_potentially_stuck_since(self) -> int:
        return self.potentially_stuck_since

    def update(self, unit: bwapi.Unit | None) -> None:
        if unit is None or not unit.exists():
            return

        frame = common.current_frame
        game = bwapi.Broodwar

        # Set last_attack_started_at on the frame where our cooldown starts
        cooldown = max(unit.getGroundWeaponCooldown(), unit.getAirWeaponCooldown())
        if frame + cooldown - self.cooldown_until > 1:
            self.last_attack_started_at = frame
            self.potentially_stuck_since = frame + _DRAGOON_ATTACK_FRAMES

        # Clear potentially_stuck_since if the unit has moved this frame or has stopped
        if self.potentially_stuck_since <= frame and (not unit.isMoving() or unit.getPosition() != self.last_position):
            self.potentially_stuck_since = 0

        super().update(unit)

        # If we're not currently in an attack, predict the frame when the next attack will start
        if frame - self.last_attack_started_at > _DRAGOON_ATTACK_FRAMES - game.getRemainingLatencyFrames():
            self.next_attack_predicted_at = 0

            last_command = unit.getLastCommand()
            target = last_command.getTarget()
            if (last_command.getType() == UnitCommandTypes.Attack_Unit and target is not None and target.exists()
                    and target.isVisible() and target.getPosition().isValid()):
                self.next_attack_predicted_at = max(frame + 1, max(self.last_command_frame + game.getLatencyFrames(),
                                                                   self.cooldown_until))

    def unstick(self) -> bool:
        if super().unstick():
            return True

        # This checks for the case of cancelled attacks sticking a dragoon
        frame = common.current_frame
        game = bwapi.Broodwar
        assert self.bwapi_unit is not None
        if (self.bwapi_unit.isMoving() and self.potentially_stuck_since > 0
                and self.potentially_stuck_since < frame - game.getLatencyFrames() - 10):
            self.stop()
            self.unstick_until = frame + game.getRemainingLatencyFrames()
            self.potentially_stuck_since = 0
            return True

        return False

    def is_ready(self) -> bool:
        from stardust.units import units

        if not super().is_ready():
            return False

        frame = common.current_frame
        remaining_latency = bwapi.Broodwar.getRemainingLatencyFrames()

        # If the last attack has just started, give the dragoon some frames to complete it
        if frame - self.last_attack_started_at + remaining_latency <= _DRAGOON_ATTACK_FRAMES:
            return False

        # If the next attack is predicted to happen within remaining latency frames, we may want to leave it alone
        frames_to_next_attack = self.next_attack_predicted_at - frame
        if 0 < frames_to_next_attack <= remaining_latency:
            assert self.bwapi_unit is not None
            target = self.bwapi_unit.getLastCommand().getTarget()

            # Allow switching targets if the current target no longer exists
            if target is None or not target.exists() or not target.isVisible() or not target.getPosition().isValid():
                return True

            # Also allow switching targets if we don't have a unit entry for it
            target_unit = units.get(target)
            if target_unit is None:
                return True

            # Otherwise only allow switching targets if the current one is expected to be too far away to attack
            my_predicted_position = self.predict_position(frames_to_next_attack)
            target_predicted_position = target_unit.predict_position(frames_to_next_attack)
            predicted_distance = geo.edge_to_edge_distance(self.type, my_predicted_position, target_unit.type,
                                                           target_predicted_position)
            return predicted_distance > self.range(target_unit)

        return True

    def attack_unit(self, target: Unit, units_and_targets: list[tuple[MyUnit, Unit | None]] | None = None,
                    cluster_attacking: bool = True, enemy_aoe_radius: int = 0) -> None:
        from stardust.map import game_map, no_go_areas

        if units_and_targets is None:
            units_and_targets = []
        assert self.bwapi_unit is not None
        remaining_latency = bwapi.Broodwar.getRemainingLatencyFrames()

        cooldown = (self.bwapi_unit.getAirWeaponCooldown() if target.is_flying
                    else self.bwapi_unit.getGroundWeaponCooldown())

        my_range = self.range(target)
        target_range = target.ground_range()
        if target.type == UnitTypes.Terran_Vulture_Spider_Mine:
            target_range = 96
        ranging_bunker = target.type == UnitTypes.Terran_Bunker and my_range > target_range

        def debug(message: str) -> None:
            if config.DEBUG_UNIT_ORDERS:
                cherryvis.log(message, self.id)

        # If we are not on cooldown, defer to normal unit attack unless we are ranging a bunker
        if not ranging_bunker and cooldown <= remaining_latency + 2:
            # Special case, if we are in a no-go area, move out of it
            if no_go_areas.is_no_go(self.tile_position_x, self.tile_position_y):
                debug("Attack: Moving to avoid no-go area")
                self.move_to(boids.avoid_no_go_area(self))
                return

            super().attack_unit(target, units_and_targets, cluster_attacking, enemy_aoe_radius)
            return

        current_distance_to_target = self.get_distance(target, target.sim_position)

        # Handle ranging a bunker and not on cooldown
        if ranging_bunker and cooldown <= remaining_latency + 2:
            # What we do depends on where we are and where we're going
            my_predicted_position = self.predict_position(remaining_latency)
            predicted_distance = geo.edge_to_edge_distance(self.type, my_predicted_position, target.type,
                                                           target.sim_position)

            # Well out of range: attack
            if predicted_distance > my_range:
                super().attack_unit(target, units_and_targets, cluster_attacking, enemy_aoe_radius)
                return

            # Expect momentum to carry us into range: stop
            if current_distance_to_target > my_range >= predicted_distance:
                self.stop()
                return

            # In range, out of range of the bunker, bunker is visible: attack
            if (target_range < current_distance_to_target <= my_range
                    and predicted_distance >= current_distance_to_target and target.last_position_visible):
                super().attack_unit(target, units_and_targets, cluster_attacking, enemy_aoe_radius)
                return

            # Otherwise fall through to move boids

        predicted_target_position = target.predict_position(remaining_latency + 2)
        predicted_distance_to_target = self.get_distance(target, predicted_target_position)

        # Move towards our target if the cluster is attacking and any of the following is true:
        # - The target is a sieged tank
        # - The target cannot attack us
        # - We are inside a narrow choke and aren't ranging down a bunker
        # TODO (upstream): Perhaps allow kiting in chokes if this unit doesn't block others
        if cluster_attacking and (target.type == UnitTypes.Terran_Siege_Tank_Siege_Mode
                                  or not self.can_be_attacked_by(target)
                                  or (game_map.is_in_narrow_choke(self.get_tile_position()) and not ranging_bunker)):
            # Just short-circuit and move towards the target
            # TODO (upstream): Consider other threats
            debug(f"Skipping kiting and moving towards {target.type}")
            self.move_to(target.sim_position, True)
            return

        # Compute our preferred distance to the target
        if current_distance_to_target < predicted_distance_to_target:
            # For targets moving away from us, desire to be closer so we don't kite out of range and never catch them
            desired_distance = min(my_range - 48, target_range + 16)
        elif ranging_bunker:
            # Move closer, then back up a bit to the desired range
            desired_distance = (my_range - 12) if current_distance_to_target > my_range else my_range
        elif my_range >= target_range:
            # The target is stationary or moving towards us, so kite it if we can
            cooldown_distance = to_int((cooldown - remaining_latency - 2) * self.type.topSpeed())
            desired_distance = min(my_range,
                                   my_range + cdiv(cooldown_distance - (predicted_distance_to_target - my_range), 2))

            # If the target's range is much lower than ours, keep a bit closer.
            # Exception for SCVs since they might be repairing something dangerous.
            if target.type != UnitTypes.Terran_SCV and target_range <= my_range - 64:
                desired_distance -= 32

            if config.DEBUG_UNIT_BOIDS:
                cherryvis.log(f"Kiting: cdwn={cooldown}; dist={predicted_distance_to_target}; range={my_range}; "
                              f"des={desired_distance}", self.id)
        else:
            # All others: desire to be at our range
            desired_distance = my_range

        # Target boid: tries to get us to our desired distance from the target
        target_x = predicted_target_position.x - self.last_position.x
        target_y = predicted_target_position.y - self.last_position.y
        if target_x != 0 or target_y != 0:
            target_scale = (max(-_TARGET_WEIGHT, min(_TARGET_WEIGHT, float(predicted_distance_to_target - desired_distance)))
                            / geo.approximate_distance(target_x, 0, target_y, 0))
            target_x = to_int(target_x * target_scale)
            target_y = to_int(target_y * target_scale)

        # Separation boid: don't block friendly units that are not in range of their targets.
        # Skipped for SCVs as they might be repairing something we don't want to get closer to.
        separation_x = 0
        separation_y = 0
        if target.type != UnitTypes.Terran_SCV:
            for other_unit, other_target in units_and_targets:
                if other_unit.id == self.id or other_target is None:
                    continue

                if ranging_bunker and current_distance_to_target <= my_range:
                    # If we are ranging a bunker and are currently in range, skip separation except from units that
                    # are caught too close to the bunker
                    if (other_target.type != UnitTypes.Terran_Bunker
                            or not other_unit.is_in_enemy_weapon_range(other_target)):
                        continue
                elif other_unit.is_in_our_weapon_range(other_target):
                    # Otherwise skip units in range of their targets
                    continue

                separation_x, separation_y = boids.add_separation_factor(
                    self, other_unit, _SEPARATION_DETECTION_LIMIT_FACTOR, _SEPARATION_WEIGHT, separation_x,
                    separation_y)

        pos = boids.compute_position(self, [target_x, separation_x], [target_y, separation_y], 0, 16, True)

        if config.DEBUG_UNIT_BOIDS:
            cherryvis.log(
                f"Kiting boids; target={(self.last_position + Position(target_x, target_y))}; "
                f"separation={(self.last_position + Position(separation_x, separation_y))}; target={pos}", self.id)

        # If the unit can't move in the desired direction, attack the target instead
        if pos == Positions.Invalid:
            debug("Attack boid invalid; attacking")
            super().attack_unit(target, units_and_targets, cluster_attacking, enemy_aoe_radius)
        else:
            debug(f"Attack boids: Moving to {pos}")
            self.move_to(pos, True)

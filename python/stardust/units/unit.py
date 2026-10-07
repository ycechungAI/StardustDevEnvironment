"""Port of Units/Unit.h, Units/Unit/Unit_Update.cpp and Units/Unit/Unit_Info.cpp: our model of any unit (ours or the
enemy's), including where enemy units probably are while in the fog of war and the damage they are about to take.

C++ `UnitImpl` held by `shared_ptr` (typedef `Unit`) is the `Unit` class here; the raw BWAPI unit is `bwapi_unit`.
"""

from __future__ import annotations

import math
from typing import TYPE_CHECKING

import bwapi
from bwapi import Orders, Position, Positions, Races, TechTypes, TilePosition, TilePositions, UnitTypes, \
    UpgradeTypes, WalkPosition, WeaponType
from stardust import common
from stardust.cpp import INT_MAX, cround, to_int
from stardust.instrumentation import cherryvis
from stardust.util import geo, unit_util

if TYPE_CHECKING:
    from stardust.units.my_unit import MyUnit


class UpcomingAttack:
    __slots__ = ("attacker", "bullet", "bullet_id", "expiry_frame", "damage", "unknown_attacker")

    def __init__(self, attacker: Unit | None, damage: int, bullet: bwapi.Bullet | None = None,
                 frames: int | None = None) -> None:
        """An attack from a bullet (expires when the bullet hits) or one expiring after a number of frames."""
        self.attacker = attacker
        self.bullet = bullet
        self.bullet_id = bullet.getID() if bullet is not None else -1  # Bullets are re-used, so keep the original id
        # The frame at which this upcoming attack "expires" (i.e. results in a bullet or deals damage)
        self.expiry_frame = INT_MAX if frames is None else common.current_frame + frames
        self.damage = damage
        # Stardust's constructor moves `attacker` into the member before testing the (now empty) parameter, so this is
        # always true and attacks are only cleared by expiry or their bullet hitting. Preserved.
        self.unknown_attacker = True


def _is_undetected(unit: bwapi.Unit) -> bool:
    return (unit.isBurrowed() or unit.isCloaked() or unit.getType().hasPermanentCloak()) and not unit.isDetected()


def _compute_build_tile(unit: bwapi.Unit) -> TilePosition:
    if unit.getType().isBuilding() and not unit.isFlying():
        return unit.getTilePosition()
    return TilePositions.Invalid


class Unit:
    __slots__ = (
        "bwapi_unit", "player", "tile_position_x", "tile_position_y", "build_tile", "dist_to_target_position",
        "last_command_frame", "last_seen", "last_seen_attacking", "type", "id", "last_position",
        "last_position_valid", "last_position_visible", "last_angle", "being_manufactured_or_carried",
        "frame_last_moved", "offset_to_vanguard_unit", "sim_position", "sim_position_valid", "predicted_positions",
        "predicted_positions_updated", "_bw_heading", "_bw_heading_updated", "_bw_velocity_x",
        "_bw_velocity_x_updated", "_bw_velocity_y", "_bw_velocity_y_updated", "_bw_speed", "_bw_speed_updated",
        "last_health", "last_shields", "health", "shields", "last_heal_frame", "last_attacked_frame", "completed",
        "estimated_completion_frame", "is_flying", "cooldown_until", "stimmed_until", "undetected", "immobile",
        "burrowed", "last_burrowing", "upcoming_attacks", "order_process_timer", "last_target",
        "__weakref__",
    )

    def __init__(self, unit: bwapi.Unit) -> None:
        frame = common.current_frame
        position = unit.getPosition()

        self.bwapi_unit: bwapi.Unit | None = unit  # None once the unit no longer exists
        self.player = unit.getPlayer()  # Player owning the unit
        self.tile_position_x = position.x >> 5
        self.tile_position_y = position.y >> 5
        self.build_tile = _compute_build_tile(unit)  # For landed buildings, the top-left tile
        self.dist_to_target_position = -1  # Transient field used in targeting / combat sim / combat micro
        self.last_command_frame = -1  # Frame of the last command to this unit
        self.last_seen = frame  # Frame the unit was last updated
        self.last_seen_attacking = -1  # Frame when the unit was last seen making an attack
        self.type = unit.getType()
        self.id = unit.getID()
        self.last_position = position  # Position of the unit when last seen
        self.last_position_valid = True  # Whether we haven't since seen the position empty
        self.last_position_visible = True  # Whether the last position was visible on the previous frame
        self.last_angle = unit.getAngle()
        self.being_manufactured_or_carried = False
        self.frame_last_moved = frame  # Last frame on which the unit changed position
        # For units in the fog, the offset to our vanguard unit they had when they disappeared
        self.offset_to_vanguard_unit = 0
        self.sim_position = position  # The position to use for this unit in combat simulation / targeting / etc.
        self.sim_position_valid = True
        # Predicted positions for the next latency + 2 frames, assuming the unit keeps moving in its current direction
        self.predicted_positions: list[Position] = []
        self.predicted_positions_updated = False
        self._bw_heading = 0
        self._bw_heading_updated = False
        self._bw_velocity_x = 0
        self._bw_velocity_x_updated = False
        self._bw_velocity_y = 0
        self._bw_velocity_y_updated = False
        self._bw_speed = 0
        self._bw_speed_updated = False
        self.last_health = unit.getHitPoints()  # Health when last seen, adjusted for upcoming attacks
        self.last_shields = unit.getShields()
        self.health = unit.getHitPoints()  # Estimated health, adjusted for upcoming attacks
        self.shields = unit.getShields()
        self.last_heal_frame = -1
        self.last_attacked_frame = -1
        self.completed = unit.isCompleted()
        self.estimated_completion_frame = -1  # If not completed, the frame when we expect the unit to complete
        self.is_flying = unit.isFlying()
        # The frame when the unit can use its weapon again
        self.cooldown_until = frame + max(unit.getGroundWeaponCooldown(), unit.getAirWeaponCooldown())
        self.stimmed_until = frame + unit.getStimTimer()
        self.undetected = _is_undetected(unit)  # Whether the unit is currently cloaked and undetected
        self.immobile = unit.isStasised() or unit.isLockedDown()  # Immobilized by stasis or lockdown
        self.burrowed = unit.isBurrowed()
        self.last_burrowing = frame if unit.getOrder() == Orders.Burrowing else 0  # Frame last seen burrowing
        self.upcoming_attacks: list[UpcomingAttack] = []  # Attacks on this unit that are expected soon
        # The expected current value of the unit's order process timer, or -1 if we don't know
        self.order_process_timer = -1
        self.last_target: Unit | None = None

    def created(self) -> None:
        from stardust.players import players

        self.predicted_positions = [self.last_position] * (bwapi.Broodwar.getLatencyFrames() + 2)
        self.being_manufactured_or_carried = self.is_being_manufactured_or_carried()
        assert self.bwapi_unit is not None
        self.update(self.bwapi_unit)

        if not self.being_manufactured_or_carried:
            players.grid(self.player).unit_created(self.type, self.last_position, self.completed, self.burrowed,
                                                   self.immobile)
            cherryvis.unit_first_seen(self.bwapi_unit)

    # -----------------------------------------------------------------------------------------------------------------
    # Updates (Unit_Update.cpp)

    def update(self, unit: bwapi.Unit | None) -> None:
        if unit is None or not unit.exists():
            return

        game = bwapi.Broodwar
        frame = common.current_frame
        self.dist_to_target_position = -1

        # Convert last command frame to our frame counter
        if unit.getLastCommandFrame() >= game.getFrameCount() - 1:
            self.last_command_frame = frame - (game.getFrameCount() - unit.getLastCommandFrame())

        self._update_grid(unit)

        self.player = unit.getPlayer()
        self.type = unit.getType()
        self.last_seen = frame

        position = unit.getPosition()
        if self.last_position != position:
            self.frame_last_moved = frame

        self.tile_position_x = position.x >> 5
        self.tile_position_y = position.y >> 5
        self.last_position = self.sim_position = position
        self.last_angle = unit.getAngle()
        self.offset_to_vanguard_unit = 0
        self.last_position_valid = self.sim_position_valid = True
        self.last_position_visible = True
        self.being_manufactured_or_carried = self.is_being_manufactured_or_carried()

        self.predicted_positions = [position] * len(self.predicted_positions)
        self.predicted_positions_updated = False

        self._bw_heading_updated = False
        self._bw_velocity_x_updated = False
        self._bw_velocity_y_updated = False
        self._bw_speed_updated = False

        hit_points = unit.getHitPoints()
        unit_shields = unit.getShields()
        is_terran = self.player.getRace() == Races.Terran
        if is_terran and unit.isCompleted() and hit_points > self.last_health:
            self.last_heal_frame = frame

        # Currently ignoring Terran buildings since they can burn
        if unit_shields < self.last_shields or (hit_points < self.last_health
                                                and (not is_terran or not self.type.isBuilding())):
            self.last_attacked_frame = frame

        # Cloaked units show up with 0 hit points and shields, so default to max and otherwise don't touch them
        self.undetected = _is_undetected(unit)
        if not self.undetected:
            self.last_health = self.health = hit_points
            self.last_shields = self.shields = unit_shields
        elif self.last_health == 0:
            self.last_health = self.health = self.type.maxHitPoints()
            self.last_shields = self.shields = self.type.maxShields()

        self.immobile = unit.isStasised() or unit.isLockedDown()
        self.burrowed = unit.isBurrowed()
        self.last_burrowing = frame if unit.getOrder() == Orders.Burrowing else 0

        self.completed = unit.isCompleted()
        self._update_estimated_completion_frame(unit)

        # TODO (upstream): Track lifted buildings
        self.is_flying = unit.isFlying()

        if unit.isVisible():
            cooldown = max(unit.getGroundWeaponCooldown(), unit.getAirWeaponCooldown())
            if cooldown > 0:
                if cooldown > (self.cooldown_until - frame + 1):
                    self.last_seen_attacking = frame
                    self.order_process_timer = 0
                elif (self.order_process_timer == 0 and self.last_target is not None and self.last_target.exists()
                      and self.last_target.last_position_valid
                      and self.get_distance(self.last_target) < self.range(self.last_target)):
                    self.order_process_timer = cooldown
            self.cooldown_until = frame + cooldown
            self.stimmed_until = frame + unit.getStimTimer()

        # Process upcoming attacks
        upcoming_damage = 0
        remaining = []
        for attack in self.upcoming_attacks:
            # Remove attacks when they expire
            if frame >= attack.expiry_frame:
                continue

            # Remove bullets when they have hit
            if attack.bullet is not None:
                bullet = attack.bullet
                if (not bullet.exists() or bullet.getID() != attack.bullet_id
                        or bullet.getPosition().getApproxDistance(bullet.getTargetPosition()) == 0):
                    continue

            # Clear if the attacker is dead, no longer visible, or out of range
            elif not attack.unknown_attacker and (
                    attack.attacker is None or not attack.attacker.exists()
                    or attack.attacker.bwapi_unit is None or not attack.attacker.bwapi_unit.isVisible()
                    or not self.is_in_enemy_weapon_range(attack.attacker)):
                continue

            upcoming_damage += attack.damage
            remaining.append(attack)
        self.upcoming_attacks = remaining

        # Simulate half damage if the unit is being healed
        if self.is_being_healed():
            upcoming_damage //= 2

        if upcoming_damage > 0 and self.shields > 0:
            shield_damage = min(self.shields, upcoming_damage)
            upcoming_damage -= shield_damage
            self.shields -= shield_damage
        if upcoming_damage > 0:
            self.health = max(0, self.health - upcoming_damage)
            if self.health <= 0:
                cherryvis.log("DOOMED!", self.id)

    def _update_estimated_completion_frame(self, unit: bwapi.Unit) -> None:
        unit_type = unit.getType()
        if not unit_type.isBuilding() or unit.isCompleted():
            self.estimated_completion_frame = -1
            return

        # Don't need to update more than once
        if self.estimated_completion_frame != -1:
            return

        frame = common.current_frame

        # For our own units, this is called on the frame the building started, so we can use the build time directly
        if unit.getPlayer() == bwapi.Broodwar.self():
            self.estimated_completion_frame = frame + unit_util.build_time(unit_type)
            return

        # For enemy units we need to estimate the start frame based on the current health.
        # We assume that the building hasn't taken damage.
        # Buildings start at 10% health and then linearly grow to 100% health, then the animation plays.

        # If the building is already at max HP, it is in its animation so we can't determine exactly when it completes
        if unit.getHitPoints() == unit_type.maxHitPoints():
            self.estimated_completion_frame = frame
            return

        # Estimate how far into the build the unit is
        initial_hit_points = unit_type.maxHitPoints() // 10
        progress = (unit.getHitPoints() - initial_hit_points) / (unit_type.maxHitPoints() - initial_hit_points)
        progress_frame = to_int(progress * unit_type.buildTime())
        self.estimated_completion_frame = frame - progress_frame + unit_util.build_time(unit_type)

    def update_unit_in_fog(self) -> None:
        from stardust.players import players

        frame = common.current_frame
        game = bwapi.Broodwar
        position_visible = game.isVisible(self.tile_position_x, self.tile_position_y)

        # Detect burrowed units we have observed burrowing
        if self.last_burrowing == frame - 1 and not self.burrowed and position_visible:
            players.grid(self.player).unit_moved(self.type, self.last_position, True, self.immobile,
                                                 self.type, self.last_position, False, self.immobile)
            self.burrowed = True

        # Update position simulation
        sim_position_succeeded = self._update_sim_position()

        # Units in fog do not have movement predicted beyond the sim position
        self.predicted_positions = [self.sim_position] * len(self.predicted_positions)
        self.predicted_positions_updated = True

        # If we've already detected that this unit has moved from its last known location, skip it
        if not self.last_position_valid:
            return

        if sim_position_succeeded:
            # If the unit has a valid simulated position, treat its last position as invalid
            self.last_position_valid = False

        elif position_visible and self.last_position_visible and self.last_seen < frame - 1:
            # If the last position has been visible for two consecutive frames, the unit is gone.
            # Units that we know have been burrowed at this position might still be there.
            if self.burrowed:
                # Assume the unit is still burrowed here unless we have detection on the position or the unit was
                # doomed before it burrowed
                grid = players.grid(game.self())
                if self.health > 0 and grid.detection(self.last_position) == 0:
                    return

            self.last_position_valid = False
            # TODO (upstream): Track lifted buildings

        elif self.type == UnitTypes.Terran_Siege_Tank_Siege_Mode and self.last_seen < frame - 240:
            # If the unit is a sieged tank, assume it is gone from its last position if we haven't seen it in 10
            # seconds and have a unit that it would otherwise fire upon.
            # TODO (upstream): Could also look for a unit inside the tank's weapon range that the enemy can see
            sight_range = UnitTypes.Terran_Siege_Tank_Siege_Mode.sightRange()
            for unit in game.self().getUnits():
                if not unit.getType().isBuilding() and not unit.isCompleted():
                    continue
                if unit.isLoaded() or unit.isFlying() or unit.isStasised():
                    continue
                if geo.edge_to_edge_distance(unit.getType(), unit.getPosition(),
                                             UnitTypes.Terran_Siege_Tank_Siege_Mode, self.last_position) <= sight_range:
                    self.last_position_valid = False
                    break

        # For the grid, treat units with invalid last position as destroyed
        if not self.last_position_valid:
            # Ignore flying buildings that have moved in the fog
            if not self.type.isBuilding() or not self.is_flying:
                players.grid(self.player).unit_destroyed(self.type, self.last_position, self.completed,
                                                         self.burrowed, self.immobile)

        # Mark buildings complete in the grid if we expect them to have completed in the fog
        if (self.last_position_valid and not self.completed and self.estimated_completion_frame != -1
                and self.estimated_completion_frame <= frame):
            self.completed = True
            self.estimated_completion_frame = -1
            players.grid(self.player).unit_completed(self.type, self.last_position, self.burrowed, self.immobile)

        self.last_position_visible = position_visible

    def add_upcoming_attack_from_bullet(self, attacker: Unit | None, bullet: bwapi.Bullet,
                                        fixed_frame_delay: int | None) -> None:
        from stardust import bullets

        # Bullets always remove any existing upcoming attack from this attacker
        if attacker is not None:
            self.upcoming_attacks = [a for a in self.upcoming_attacks if a.attacker is not attacker]

        damage = bullets.upcoming_damage(bullet)
        if damage > 0:
            if fixed_frame_delay is not None:
                self.upcoming_attacks.append(UpcomingAttack(attacker, damage, frames=fixed_frame_delay))
            else:
                self.upcoming_attacks.append(UpcomingAttack(attacker, damage, bullet=bullet))

    def add_upcoming_attack(self, attacker: Unit) -> None:
        from stardust.players import players

        # TODO (upstream): Carrier and reaver when we have them (if they even make sense)

        # Get the delay between the attacking unit type going on cooldown to when its bullet is created or damage dealt
        delay = unit_util.delay_from_cooldown_to_bullet_or_damage(attacker.type)
        if delay == 0:
            return

        damage = players.attack_damage(attacker.player, attacker.type, self.player, self.type)

        # Zealot is a special case because it hits twice
        if attacker.type == UnitTypes.Protoss_Zealot:
            damage_per_hit = damage // 2
            self.upcoming_attacks.append(UpcomingAttack(attacker, damage_per_hit, frames=delay))
            self.upcoming_attacks.append(UpcomingAttack(attacker, damage_per_hit, frames=delay + 2))
        else:
            self.upcoming_attacks.append(UpcomingAttack(attacker, damage, frames=delay))

    def _update_grid(self, unit: bwapi.Unit) -> None:
        from stardust.map import game_map
        from stardust.players import players

        grid = players.grid(unit.getPlayer())
        new_type = unit.getType()
        new_position = unit.getPosition()
        new_completed = unit.isCompleted()
        new_burrowed = unit.isBurrowed()
        new_immobile = unit.isStasised() or unit.isLockedDown()

        # Units that have renegaded
        if unit.getPlayer() != self.player:
            players.grid(self.player).unit_destroyed(self.type, self.last_position, self.completed, self.burrowed,
                                                     self.immobile)
            grid.unit_created(new_type, new_position, new_completed, new_burrowed, new_immobile)
            return

        # Units that have morphed
        if self.type != new_type:
            grid.unit_destroyed(self.type, self.last_position, self.completed, self.burrowed, self.immobile)
            grid.unit_created(new_type, new_position, new_completed, new_burrowed, new_immobile)
            return

        # Units that have changed manufactured or carried
        now_being_manufactured_or_carried = self.is_being_manufactured_or_carried()
        if self.being_manufactured_or_carried != now_being_manufactured_or_carried:
            # If no longer being manufactured or carried, treat as a new unit
            if self.being_manufactured_or_carried:
                grid.unit_created(new_type, new_position, new_completed, new_burrowed, new_immobile)

                # If this is the incomplete to complete transition, register in CherryVis
                if not self.completed and new_completed and self.bwapi_unit is not None:
                    cherryvis.unit_first_seen(self.bwapi_unit)
                return

            # Otherwise treat it as destroyed until it shows up again
            grid.unit_destroyed(self.type, self.last_position, self.completed, self.burrowed, self.immobile)
            return
        if now_being_manufactured_or_carried:
            return  # Nothing to update for units that are still being manufactured or carried

        # Units that have completed; fall through as the unit may have moved
        if not self.completed and new_completed:
            grid.unit_completed(self.type, self.last_position, self.burrowed, self.immobile)

        # Units that have "incompleted". This either means we assumed something completed in the fog that didn't, or a
        # morph is happening (e.g. hydralisk to lurker egg). In either case we just recreate the unit in the grid.
        if self.completed and not new_completed:
            grid.unit_destroyed(self.type, self.last_position, True, self.burrowed, self.immobile)
            grid.unit_created(new_type, new_position, False, new_burrowed, new_immobile)
            return

        # Units that moved while in the fog and have now reappeared
        if not self.last_position_valid:
            grid.unit_created(new_type, new_position, new_completed, new_burrowed, new_immobile)
            return

        # Units that have taken off. We can treat them as destroyed, as nothing that can take off has an attack.
        if not self.is_flying and unit.isFlying():
            grid.unit_destroyed(self.type, self.last_position, self.completed, self.burrowed, self.immobile)

            # Also affects navigation grids, so tell the map when a building has lifted
            if self.type.isBuilding():
                top_left = self.last_position - (Position(self.type.tileSize()) / 2)
                game_map.on_building_lifted(self.type, TilePosition(top_left))
            return

        # Units that have landed
        if self.is_flying and not unit.isFlying():
            grid.unit_created(new_type, new_position, new_completed, new_burrowed, new_immobile)

            # Also affects navigation grids, so tell the map the unit has landed
            if self.type.isBuilding():
                game_map.on_building_landed(self.type, unit.getTilePosition())
            return

        # At this point we can ignore any flying buildings
        if self.is_flying and self.type.isBuilding():
            return

        # Units that have moved or changed burrow or immobile state
        if self.last_position != new_position or self.burrowed != new_burrowed or self.immobile != new_immobile:
            grid.unit_moved(new_type, new_position, new_burrowed, new_immobile,
                            self.type, self.last_position, self.burrowed, self.immobile)

        # TODO (upstream): Workers in a refinery

    def _update_predicted_positions(self) -> None:
        from stardust.map import game_map
        from stardust.players import players

        if self.predicted_positions_updated:
            return
        self.predicted_positions_updated = True

        # Return if we can't predict the movement
        unit = self.bwapi_unit
        if unit is None or not unit.exists() or not unit.isVisible():
            return

        # Return if the unit isn't moving
        speed = self.bw_speed()
        if speed == 0:
            return

        # Determine the acceleration to use during the prediction
        top_speed = players.unit_top_speed(self.player, self.type)
        bw_top_speed = players.unit_bw_top_speed(self.player, self.type)
        speed = min(speed, bw_top_speed)
        if not unit.isAccelerating() or speed >= bw_top_speed:
            acceleration = 0
        else:
            acceleration = unit_util.acceleration(self.type, top_speed)

        # Simulate the positions
        x = self.last_position.x << 8
        y = self.last_position.y << 8
        heading = self.bw_heading()
        for i in range(len(self.predicted_positions)):
            x, y, heading, speed = geo.bw_movement(x, y, heading, heading, 0, speed, acceleration, bw_top_speed)
            self.predicted_positions[i] = Position(*game_map.make_position_valid(x >> 8, y >> 8))

    def _update_sim_position(self) -> bool:
        """For units in the fog: records the offset to our attack squad's vanguard unit when the unit enters the fog,
        then uses it to predict where the unit is regardless of whether we or the enemy are fleeing."""
        from stardust.general import general
        from stardust.map import game_map
        from stardust.map.path_finding import path_finding
        from stardust.map.path_finding.navigation_grid import NavigationGrid

        frame = common.current_frame
        game = bwapi.Broodwar
        vanguard: MyUnit | None = None
        target_position = Positions.Invalid

        def get_attack_enemy_main_vanguard() -> bool:
            nonlocal vanguard, target_position
            squad = general.get_attack_base_squad(game_map.get_enemy_starting_natural())
            if squad is None:
                squad = general.get_attack_base_squad(game_map.get_enemy_main())
            if squad is None:
                return False
            vanguard_cluster = squad.vanguard_cluster()
            if vanguard_cluster is None:
                return False
            vanguard = vanguard_cluster.vanguard
            if vanguard is None:
                return False
            target_position = squad.get_target_position()
            return True

        self.sim_position = self.last_position
        self.sim_position_valid = self.last_position_valid

        # If the unit just entered the fog, record the offset
        if self.last_seen == frame - 1:
            # First reject units that are not interesting (i.e. are not mobile ground combat units)
            if (self.is_flying or self.burrowed or self.type.isBuilding()
                    or self.type == UnitTypes.Terran_Siege_Tank_Siege_Mode or not unit_util.is_combat_unit(self.type)):
                return False

            # Now attempt to get the vanguard unit of the vanguard attack squad cluster
            if not get_attack_enemy_main_vanguard():
                return False
            assert vanguard is not None

            # Skip units too far away
            vanguard_dist = vanguard.last_position.getApproxDistance(self.last_position)
            if vanguard_dist > 640:
                return False

            self.offset_to_vanguard_unit = vanguard_dist
            return True

        # If we have an offset and a vanguard unit, predict the position
        if self.offset_to_vanguard_unit == 0 or not get_attack_enemy_main_vanguard():
            return False
        assert vanguard is not None

        # Criteria for target position: must not be visible, and must have a navigation grid
        def navigation_grid_at(pos: Position) -> NavigationGrid | None:
            if game.isVisible(TilePosition(pos)):
                return None
            return path_finding.get_navigation_grid(pos)

        grid = navigation_grid_at(target_position)
        if grid is None:
            base = game_map.get_enemy_main()
            if base is None:
                return False
            grid = navigation_grid_at(base.get_position())
            if grid is None:
                return False

        # Find the node where the path enters the fog
        node = grid.node(vanguard.last_position)
        next_node = node.next_node
        while next_node is not None and game.isVisible(node.x, node.y):
            node = next_node
            next_node = node.next_node

        # Detect if the search failed
        if next_node is None:
            return False

        # Set the position here to start with
        self.sim_position = node.center()
        self.sim_position_valid = True

        # Try to scale the position further away where appropriate
        vector = Position(TilePosition(node.x - vanguard.tile_position_x, node.y - vanguard.tile_position_y))
        if geo.approximate_distance(vector.x, 0, vector.y, 0) < self.offset_to_vanguard_unit:
            scaled_vector = geo.scale_vector(vector, self.offset_to_vanguard_unit)
            if scaled_vector != Positions.Invalid:
                pos = Position(*game_map.make_position_valid(*(vanguard.last_position + scaled_vector)))
                if game_map.is_walkable_tile(TilePosition(pos)):
                    self.sim_position = pos

        return self.sim_position_valid

    # -----------------------------------------------------------------------------------------------------------------
    # Information (Unit_Info.cpp)

    def get_tile_position(self) -> TilePosition:
        if self.build_tile.isValid():
            return self.build_tile
        return TilePosition(self.tile_position_x, self.tile_position_y)

    def exists(self) -> bool:
        return self.bwapi_unit is not None

    def bw_heading(self) -> int:
        if not self._bw_heading_updated:
            self._bw_heading = geo.bw_heading(self.last_angle)
            self._bw_heading_updated = True
        return self._bw_heading

    def bw_velocity_x(self) -> int:
        if not self._bw_velocity_x_updated:
            # BWAPI divides the raw value (32 bit with 8 bit fractional) by 256; reverse that to get the raw value
            assert self.bwapi_unit is not None
            self._bw_velocity_x = cround(self.bwapi_unit.getVelocityX() * 256.0)
            self._bw_velocity_x_updated = True
        return self._bw_velocity_x

    def bw_velocity_y(self) -> int:
        if not self._bw_velocity_y_updated:
            assert self.bwapi_unit is not None
            self._bw_velocity_y = cround(self.bwapi_unit.getVelocityY() * 256.0)
            self._bw_velocity_y_updated = True
        return self._bw_velocity_y

    def bw_speed(self) -> int:
        if not self._bw_speed_updated:
            assert self.bwapi_unit is not None
            speed = math.hypot(self.bwapi_unit.getVelocityX(), self.bwapi_unit.getVelocityY())
            self._bw_speed = to_int(speed * 256.0)
            self._bw_speed_updated = True
        return self._bw_speed

    def is_training(self) -> bool:
        if self.bwapi_unit is None or not self.completed:
            return False
        if self.bwapi_unit.isTraining():
            return True
        return (self.bwapi_unit.getLastCommand().getType() == bwapi.UnitCommandTypes.Train
                and (common.current_frame - self.last_command_frame - 1) <= bwapi.Broodwar.getLatencyFrames())

    def is_being_manufactured_or_carried(self) -> bool:
        return False

    def is_being_healed(self) -> bool:
        return common.current_frame < self.last_heal_frame + 24

    def is_being_attacked(self) -> bool:
        return bool(self.upcoming_attacks) or common.current_frame < self.last_attacked_frame + 48

    def is_attackable(self) -> bool:
        return self.bwapi_unit is not None and not self.undetected and not self.bwapi_unit.isStasised()

    def is_cliffed_tank(self, attacker: Unit | None) -> bool:
        from stardust.map.path_finding import path_finding

        if attacker is None or self.type != UnitTypes.Terran_Siege_Tank_Siege_Mode:
            return False

        # For now let's assume the tank is not directly attackable if a narrow choke divides it from the cluster
        return path_finding.separating_narrow_choke(self.last_position, attacker.last_position, attacker.type,
                                                    path_finding.PathFindingOptions.UseNeighbouringBWEMArea) is not None

    def can_attack(self, target: Unit) -> bool:
        return target.is_attackable() and (self.can_attack_air() if target.is_flying else self.can_attack_ground())

    def can_be_attacked_by(self, attacker: Unit) -> bool:
        return self.is_attackable() and (attacker.can_attack_air() if self.is_flying else attacker.can_attack_ground())

    def can_attack_ground(self) -> bool:
        return not self.immobile and unit_util.can_attack_ground(self.type)

    def can_attack_air(self) -> bool:
        return not self.immobile and unit_util.can_attack_air(self.type)

    def is_static_ground_defense(self) -> bool:
        if not unit_util.is_stationary_attacker(self.type) or not unit_util.can_attack_ground(self.type):
            return False
        return not (self.type == UnitTypes.Zerg_Lurker and not self.burrowed)

    def is_transport(self) -> bool:
        from stardust.players import players

        return (self.type in (UnitTypes.Protoss_Shuttle, UnitTypes.Terran_Dropship)
                or (self.type == UnitTypes.Zerg_Overlord
                    and players.upgrade_level(self.player, UpgradeTypes.Ventral_Sacs) > 0))

    def needs_detection(self) -> bool:
        from stardust.players import players

        if self.type in (UnitTypes.Zerg_Lurker, UnitTypes.Zerg_Lurker_Egg) or self.type.hasPermanentCloak():
            return True
        if self.type.isCloakable() and players.has_researched(self.player, self.type.cloakingTech()):
            return True
        return self.type.isBurrowable() and players.has_researched(self.player, TechTypes.Burrowing)

    def ground_range(self) -> int:
        from stardust.players import players

        if self.type in (UnitTypes.Protoss_Carrier, UnitTypes.Protoss_Reaver):
            return 256
        if self.type == UnitTypes.Terran_Bunker:
            return players.weapon_range(self.player, UnitTypes.Terran_Marine.groundWeapon()) + 48
        return players.weapon_range(self.player, self.type.groundWeapon())

    def air_range(self) -> int:
        from stardust.players import players

        if self.type == UnitTypes.Protoss_Carrier:
            return 256
        if self.type == UnitTypes.Terran_Bunker:
            return players.weapon_range(self.player, UnitTypes.Terran_Marine.airWeapon()) + 48
        return players.weapon_range(self.player, self.type.airWeapon())

    def range(self, target: Unit) -> int:
        return self.air_range() if target.is_flying else self.ground_range()

    def ground_damage(self) -> int:
        from stardust.players import players

        if self.type != UnitTypes.Terran_Vulture_Spider_Mine and (
                (self.burrowed and self.type != UnitTypes.Zerg_Lurker)
                or (not self.burrowed and self.type == UnitTypes.Zerg_Lurker)):
            return 0
        return players.weapon_damage(self.player, self.ground_weapon()) * min(1, unit_util.max_ground_hits(self.type))

    def air_damage(self) -> int:
        from stardust.players import players

        return players.weapon_damage(self.player, self.air_weapon()) * min(1, unit_util.max_air_hits(self.type))

    def ground_weapon(self) -> WeaponType:
        return unit_util.get_ground_weapon(self.type)

    def air_weapon(self) -> WeaponType:
        return unit_util.get_air_weapon(self.type)

    def get_weapon(self, target: Unit) -> WeaponType:
        return self.air_weapon() if target.is_flying else self.ground_weapon()

    def is_in_our_weapon_range(self, target: Unit, predicted_target_position: Position = Positions.Invalid,
                               buffer: int = 0) -> bool:
        weapon_range = self.air_range() if target.is_flying else self.ground_range()
        return self.get_distance(target, predicted_target_position) <= weapon_range + buffer

    def is_in_enemy_weapon_range(self, attacker: Unit, predicted_attacker_position: Position = Positions.Invalid,
                                 buffer: int = 0) -> bool:
        weapon_range = attacker.air_range() if self.is_flying else attacker.ground_range()
        return self.get_distance(attacker, predicted_attacker_position) <= weapon_range + buffer

    def get_distance(self, other: Unit | Position, predicted_other_position: Position = Positions.Invalid) -> int:
        """Edge-to-edge distance to another unit (optionally at a predicted position), or edge-to-point to a
        position."""
        if isinstance(other, Position):
            return geo.edge_to_point_distance(self.type, self.last_position, other)
        other_position = predicted_other_position if predicted_other_position.isValid() else other.last_position
        return geo.edge_to_edge_distance(self.type, self.last_position, other.type, other_position)

    def predict_position(self, frames: int) -> Position:
        self._update_predicted_positions()
        return self.predicted_positions[frames - 1]

    def intercept(self, target: Unit) -> Position:
        """The intercept point of this unit targeting another one, assuming this unit goes at full speed and the
        target remains on its current trajectory and speed; invalid if the target cannot be intercepted.

        Uses the formula derived here:
        http://jaran.de/goodbits/2011/07/17/calculating-an-intercept-course-to-a-target-with-constant-direction-and-velocity-in-a-2-dimensional-plane/
        """
        target_unit = target.bwapi_unit
        if (target_unit is None or not target_unit.exists() or not target_unit.isVisible()
                or target.type.topSpeed() < 0.001):
            return target.last_position

        speed = self.type.topSpeed()
        velocity_x = target_unit.getVelocityX()
        velocity_y = target_unit.getVelocityY()
        diff_x = float(target.last_position.x - self.last_position.x)
        diff_y = float(target.last_position.y - self.last_position.y)
        dist_squared = diff_x * diff_x + diff_y * diff_y

        diff_speed = velocity_x * velocity_x + velocity_y * velocity_y - speed * speed
        diff_dist = diff_x * velocity_x + diff_y * velocity_y

        if diff_speed < 0.0001:
            if diff_dist == 0:
                # C++ divides by zero: -inf when the units are apart (rejected below), NaN when they coincide
                if dist_squared > 0:
                    return Positions.Invalid
                return target.last_position
            t = -dist_squared / (2 * diff_dist)
        else:
            dist_speed_ratio = -diff_dist / diff_speed
            d = dist_speed_ratio * dist_speed_ratio - dist_squared / diff_speed
            if d < 0:
                return Positions.Invalid
            r = math.sqrt(d)
            t = max(dist_speed_ratio + r, dist_speed_ratio - r)

        if t < 0 or t > 5000:
            return Positions.Invalid

        return Position(target.last_position.x + to_int(t * velocity_x),
                        target.last_position.y + to_int(t * velocity_y))

    def __str__(self) -> str:
        text = f"{self.type}:{self.id}@{WalkPosition(self.last_position)}"
        if self.sim_position != self.last_position:
            text += f"->{WalkPosition(self.sim_position)}"
        return text

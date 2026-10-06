"""Port of General/UnitCluster/Targeting.cpp: chooses a target for each unit in a cluster.

The verbose DEBUG_TARGETING log and DRAW_TARGETING lines are omitted.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import DamageTypes, Orders, Position, UnitCommandTypes, UnitSizeTypes, UnitTypes, WalkPosition
from stardust import common
from stardust.cpp import INT_MAX, cdiv, to_int
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.map.path_finding.path_finding import PathFindingOptions
from stardust.players import players
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster, UnitsAndTargets
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit


def _bw(unit: Unit) -> bwapi.Unit:
    bwapi_unit = unit.bwapi_unit
    assert bwapi_unit is not None
    return bwapi_unit


def _target_priority(target: Unit) -> int:
    """This is similar to the target prioritization in Locutus that originally came from Steamhammer/UAlbertaBot."""
    target_type = target.type
    target_unit = _bw(target)

    def not_close_to_target_position() -> bool:
        return target.dist_to_target_position > 1500

    if (target_type in (UnitTypes.Zerg_Infested_Terran, UnitTypes.Protoss_High_Templar, UnitTypes.Protoss_Reaver)
            or (target_type == UnitTypes.Terran_Vulture_Spider_Mine and not target.burrowed)
            or (target_type == UnitTypes.Protoss_Observer
                and units.count_completed(UnitTypes.Protoss_Dark_Templar) > 0)):
        return 15

    if target_type in (UnitTypes.Protoss_Arbiter, UnitTypes.Terran_Siege_Tank_Siege_Mode):
        return 14

    if target_type in (UnitTypes.Terran_Siege_Tank_Tank_Mode, UnitTypes.Terran_Dropship, UnitTypes.Terran_Medic,
                       UnitTypes.Protoss_Shuttle, UnitTypes.Terran_Science_Vessel, UnitTypes.Zerg_Scourge,
                       UnitTypes.Protoss_Observer, UnitTypes.Zerg_Nydus_Canal):
        return 13

    # Proxies, except missile turrets
    if (target_type.isBuilding() and not target.is_flying and not_close_to_target_position()
            and target_type != UnitTypes.Terran_Missile_Turret):
        if target.can_attack_ground() or target.can_attack_air():
            return 12
        return 10

    if target_type == UnitTypes.Terran_Bunker:
        return 11

    if target_type.isWorker():
        if (target_unit.isConstructing() or target_unit.isRepairing()) and not_close_to_target_position():
            return 15

        # Blocking a narrow choke makes you critical.
        if game_map.is_in_narrow_choke(target.get_tile_position()):
            return 14

        # Repairing
        order_target = target_unit.getOrderTarget()
        if target_unit.isRepairing() and order_target is not None:
            # Something that can shoot
            if order_target.getType().groundWeapon() != bwapi.WeaponTypes.None_:
                return 14

            # A bunker: only target the workers if we can't outrange the bunker
            if (order_target.getType() == UnitTypes.Terran_Bunker
                    and players.weapon_range(target.player, UnitTypes.Terran_Marine.groundWeapon()) > 128):
                return 13

        # Workers that have attacked in the last four seconds
        if (common.current_frame - target.last_seen_attacking) < 96:
            return 11

        if target_unit.isConstructing():
            return 10

        return 9

    if target.can_attack_ground() or target.can_attack_air():
        return 11

    if target_type.isSpellcaster():
        return 10

    if target_type.isResourceDepot():
        return 7

    if target_type in (UnitTypes.Protoss_Pylon, UnitTypes.Zerg_Spawning_Pool, UnitTypes.Terran_Factory,
                       UnitTypes.Terran_Armory):
        return 5

    if target_type.isAddon():
        return 1

    if not target.completed or (target_type.requiresPsi() and not target_unit.isPowered()):
        return 2

    if target_type.gasPrice() > 0:
        return 4

    if target_type.mineralPrice() > 0:
        return 3

    return 1


def _is_in_our_main_or_natural(unit: Unit) -> bool:
    if not unit.last_position_valid:
        return False

    area = bwem.Instance().GetArea(WalkPosition(unit.last_position))
    main_and_natural_areas = set(game_map.get_my_main_areas())
    natural = game_map.get_my_natural()
    if natural is not None:
        main_and_natural_areas.add(natural.get_area())

    return area in main_and_natural_areas


class _Target:
    __slots__ = ("unit", "priority", "health_including_shields", "attacker_count", "cliffed_tank",
                 "in_my_main_or_natural")

    def __init__(self, unit: Unit, vanguard: MyUnit) -> None:
        self.unit = unit
        self.priority = _target_priority(unit)  # Base priority computed by _target_priority
        self.health_including_shields = unit.health + unit.shields  # Reduced by bullets and earlier attackers
        self.attacker_count = 0  # How many attackers have this target in their close targets
        self.cliffed_tank = unit.is_cliffed_tank(vanguard)
        self.in_my_main_or_natural = _is_in_our_main_or_natural(unit)

    def deal_damage(self, attacker: MyUnit) -> None:
        unit = self.unit
        damage = players.attack_damage(attacker.player, attacker.type, unit.player, unit.type)

        # For low-ground to high-ground attacks, simulate half damage
        game = bwapi.Broodwar
        if (not attacker.is_flying and unit_util.is_ranged_unit(attacker.type)
                and game.getGroundHeight(attacker.get_tile_position()) < game.getGroundHeight(unit.get_tile_position())):
            damage = cdiv(damage, 2)

        # Simulate half damage if the unit is being healed
        if unit.is_being_healed():
            damage = cdiv(damage, 2)

        self.health_including_shields -= damage


class _Attacker:
    __slots__ = ("unit", "targets", "frames_to_attack", "close_targets")

    def __init__(self, unit: MyUnit) -> None:
        self.unit = unit
        self.targets: list[_Target] = []  # All of the targets available to this attacker
        self.frames_to_attack = INT_MAX  # The number of frames before this attacker can attack something
        self.close_targets: list[_Target] = []  # All targets that can be attacked at frames_to_attack


def _is_target_reachable_enemy_base(target_position: Position, vanguard: MyUnit | None) -> bool:
    # First check if the target is an enemy base
    target_base = game_map.base_near(target_position)
    if target_base is None:
        return False
    if target_base.owner != bwapi.Broodwar.enemy():
        return False
    if target_base.last_scouted != -1 and target_base.resource_depot is None:
        return False

    if vanguard is None:
        return True

    grid = path_finding.get_navigation_grid(target_base.get_position())
    if grid is None:
        return True

    return grid.node(vanguard.last_position).next_node is not None


def select_targets(cluster: UnitCluster, target_units: set[Unit], target_position: Position,
                   static_position: bool = False) -> UnitsAndTargets:
    game = bwapi.Broodwar
    frame = common.current_frame
    latency_frames = game.getLatencyFrames()
    remaining_latency_frames = game.getRemainingLatencyFrames()
    vanguard = cluster.vanguard

    # Perform a scan to remove target units that are at much different distances from our target position than the
    # vanguard unit. This indicates we have picked up enemy units that are in a different region separated from us by
    # a cliff.
    if not vanguard.is_flying:
        for unit in list(target_units):
            if unit.is_flying:
                continue
            unit.dist_to_target_position = path_finding.get_ground_distance(
                unit.sim_position, target_position, unit.type, PathFindingOptions.UseNeighbouringBWEMArea)

            if (unit.dist_to_target_position != -1
                    and abs(unit.dist_to_target_position - cluster.vanguard_dist_to_target) > 700
                    and not vanguard.is_in_enemy_weapon_range(unit)):
                target_units.discard(unit)

    result: UnitsAndTargets = []

    # For our targeting we want to know if we are attacking a reachable enemy base
    # Criteria:
    # - Not in static position mode
    # - Target is near a base
    # - The base is owned by the enemy
    # - The base has a resource depot or hasn't been scouted yet
    # - The base has a navigation grid path from our main choke (i.e. hasn't been walled-off)
    target_is_reachable_enemy_base = not static_position and _is_target_reachable_enemy_base(target_position,
                                                                                             vanguard)

    # Create the target objects
    # This also updates the enemy AOE radius
    targets: list[_Target] = []
    cluster.enemy_aoe_radius = 0
    for target_unit in target_units:
        if target_unit.exists():
            targets.append(_Target(target_unit, vanguard))
            cluster.enemy_aoe_radius = max(cluster.enemy_aoe_radius, target_unit.ground_weapon().outerSplashRadius())

    def get_current_target(unit: MyUnit) -> _Target | None:
        last_command = _bw(unit).getLastCommand()
        if last_command.getType() != UnitCommandTypes.Attack_Unit:
            return None

        current_target_unit = units.get(last_command.getTarget())
        if current_target_unit is None:
            return None

        for target in targets:
            if target.unit is current_target_unit:
                return target

        return None

    # Perform a pre-scan to get valid targets and the frame at which we can attack them for each unit
    attackers: list[_Attacker] = []
    for unit in cluster.units:
        # If the unit isn't ready, lock it to its current target and skip targeting completely
        if not unit.is_ready():
            current_target = get_current_target(unit)

            # If the unit isn't on its cooldown yet, simulate the attack on the target for use in later targeting
            # If the unit is on its cooldown, there will be an upcoming attack already registered on the target
            # This ensures consistency between our targeting and combat sim (avoiding double-counting of damage)
            if current_target is not None and (unit.cooldown_until - frame) <= (latency_frames + 2):
                current_target.deal_damage(unit)

            result.append((unit, current_target.unit if current_target is not None else None))
            continue

        is_ranged = unit_util.is_ranged_unit(unit.type)
        is_anti_air = unit.type == UnitTypes.Protoss_Corsair
        distance_to_target_position = unit.get_distance(target_position)

        # Start by doing a pass to gather the types of targets we have and filter those we don't want to consider
        has_non_building = False
        filtered_targets: list[tuple[_Target, int]] = []
        for target in targets:
            target_unit = target.unit
            target_bwapi_unit = _bw(target_unit)
            if (target_unit.type in (UnitTypes.Zerg_Larva, UnitTypes.Zerg_Egg)
                    or target_unit.undetected
                    or target_unit.health <= 0
                    or not unit.can_attack(target_unit)):
                continue

            # Ranged cannot hit targets under dark swarm
            if ((is_ranged or unit.type.isWorker()) and unit.type != UnitTypes.Protoss_Reaver
                    and target_bwapi_unit.isUnderDarkSwarm()):
                continue

            # Melee cannot hit targets under disruption web and don't want to attack targets under storm
            if not is_ranged and (target_bwapi_unit.isUnderDisruptionWeb() or target_bwapi_unit.isUnderStorm()):
                continue

            # Cannons can only attack what they can see and are in range of
            if unit.type == UnitTypes.Protoss_Photon_Cannon:
                if not target_bwapi_unit.isVisible():
                    continue

                predicted_range = unit.get_distance(target_unit, target_unit.predict_position(latency_frames))
                if predicted_range > (unit.air_range() if target_unit.is_flying else unit.ground_range()):
                    continue

            target_range = unit.get_distance(target_unit, target_unit.sim_position)
            dist_to_range = max(0, target_range - (unit.air_range() if target_unit.is_flying else unit.ground_range()))

            # In static position mode, units only attack what they are in range of
            if static_position and dist_to_range > 0:
                continue

            # Cliffed tanks can only be attacked by units in range with vision
            if target.cliffed_tank and (dist_to_range > 0 or not target_bwapi_unit.isVisible()):
                continue

            # The next checks ignore certain units if we are not close to our target position
            # The idea is to avoid getting sidetracked chasing single units
            # We ignore them if the target is part of a larger army, since we will actually want to engage it
            if distance_to_target_position > 500 and not is_anti_air and not target.in_my_main_or_natural:
                from stardust.general import general

                army = general.army_for_enemy_unit(target_unit)
                if army is None or len(army.units) < 3:
                    # Skip targets that are out of range and moving away from us
                    if dist_to_range > 0:
                        if not target_bwapi_unit.isVisible():
                            continue

                        predicted_target_position = target_unit.predict_position(1)
                        if (predicted_target_position.isValid()
                                and unit.get_distance(target_unit, predicted_target_position) > target_range):
                            continue

                    # Skip targets that are further away from the target position and are either:
                    # - Out of range
                    # - In our range, but we aren't in their range, and we are on cooldown
                    if (unit.is_flying == target_unit.is_flying and unit.dist_to_target_position != -1
                            and target_unit.dist_to_target_position != -1
                            and unit.dist_to_target_position < target_unit.dist_to_target_position
                            and (dist_to_range > 0
                                 or (unit.cooldown_until > frame and not unit.is_in_enemy_weapon_range(target_unit)))):
                        continue

            # This is a suitable target
            filtered_targets.append((target, dist_to_range))

            if target.priority > 7:
                has_non_building = True

        attacker = _Attacker(unit)
        attackers.append(attacker)
        for target, dist_to_range in filtered_targets:
            # If we are targeting an enemy base, ignore outlying buildings (except static defense) unless we have a
            # higher-priority target. Rationale: When we have a non-building target, we want to consider buildings
            # since they might be blocking us from attacking them.
            if (target.priority < 7 and (not has_non_building or target.unit.is_flying)
                    and target_is_reachable_enemy_base and distance_to_target_position > 200 and not is_anti_air):
                continue

            attacker.targets.append(target)

            frames_to_attack = unit.cooldown_until - frame
            if unit.type != UnitTypes.Protoss_Photon_Cannon:
                frames_to_attack = max(frames_to_attack,
                                       to_int(dist_to_range / unit.type.topSpeed()) + remaining_latency_frames + 2)

            if frames_to_attack < attacker.frames_to_attack:
                attacker.frames_to_attack = frames_to_attack
                attacker.close_targets = [target]
            elif frames_to_attack == attacker.frames_to_attack:
                attacker.close_targets.append(target)

        # Increment the close target count for all of our close targets
        for close_target in attacker.close_targets:
            close_target.attacker_count += 1

    # Sort the attackers: lowest frames to attack, then lowest number of close targets, then lowest number of targets,
    # then unit ID (guaranteed to be unequal)
    attackers.sort(key=lambda a: (a.frames_to_attack, len(a.close_targets), len(a.targets), a.unit.id))

    # Now assign each unit a target, skipping any that are simulated to already be dead
    for attacker in attackers:
        unit = attacker.unit

        best_target: _Target | None = None
        best_score = -999999
        best_attacker_count = 0
        best_dist = INT_MAX
        best_visible = False

        is_ranged = unit_util.is_ranged_unit(unit.type)
        cooldown_move_frames = max(0, unit.cooldown_until - frame - remaining_latency_frames - 2)
        top_speed = unit.type.topSpeed()

        distance_to_target_position = unit.get_distance(target_position)
        for potential_target in attacker.targets:
            if potential_target.health_including_shields <= 0:
                continue

            target_unit = potential_target.unit
            target_bwapi_unit = _bw(target_unit)

            # If we have a visible target, ignore non-visible
            if best_visible and not target_bwapi_unit.isVisible():
                continue

            # Initialize the score as a formula of the target priority and how far outside our attack range it is
            # Each priority step is equivalent to 2 tiles
            # If the unit is on cooldown, we assume it can move towards the target before attacking
            target_dist = unit.get_distance(target_unit, target_unit.sim_position)
            weapon_range = unit.air_range() if target_unit.is_flying else unit.ground_range()
            score = (2 * 32 * potential_target.priority
                     - max(0, target_dist - to_int(cooldown_move_frames * top_speed) - weapon_range))

            # Now adjust the score according to some rules

            # Give a bonus to units that are already in range
            # Melee units get an extra bonus, as they have a more difficult time getting around blocking things
            # Increase the bonus if we have a lot of close targets, since that means we might have more difficulty
            # reaching a different target. Increase the bonus if we are off cooldown.
            if target_dist <= weapon_range:
                score += 64 if is_ranged else 160
                score += 16 * max(0, len(attacker.close_targets) - 1)
                if cooldown_move_frames == 0:
                    score += 64

            # Give a bonus to injured targets
            # This is what provides some focus fire behaviour, as we simulate previous attackers' hits
            health_percentage = (potential_target.health_including_shields
                                 / (target_unit.type.maxHitPoints() + target_unit.type.maxShields()))
            score += to_int(160.0 * (1.0 - health_percentage))

            # Penalize ranged units fighting uphill
            if is_ranged and (game.getGroundHeight(unit.tile_position_x, unit.tile_position_y)
                              < game.getGroundHeight(target_unit.tile_position_x, target_unit.tile_position_y)):
                score -= 2 * 32

            # Avoid defensive matrix
            if target_bwapi_unit.isDefenseMatrixed():
                score -= 4 * 32

            # Give a bonus for enemies that are closer to our target position (usually the enemy base)
            if target_unit.get_distance(target_position) < distance_to_target_position:
                score += 2 * 32

            # Give bonus to units under dark swarm
            # Ranged units skip these targets earlier
            if target_bwapi_unit.isUnderDarkSwarm():
                score += 4 * 32

            # Give a bonus to units that can attack us
            if target_unit.can_attack(unit):
                score += 3 * 32

            # Give a bonus to non-moving or braking targets, and a penalty to units that are faster than us
            if not target_bwapi_unit.isMoving():
                if (target_bwapi_unit.isSieged() or target_bwapi_unit.getOrder() == Orders.Sieging
                        or target_bwapi_unit.getOrder() == Orders.Unsieging):
                    score += 48
                else:
                    score += 24
            elif target_bwapi_unit.isBraking():
                score += 16
            elif target_unit.type.topSpeed() >= top_speed:
                score -= 4 * 32

            # Take the damage type into account
            damage = unit.get_weapon(target_unit).damageType()
            if damage == DamageTypes.Explosive:
                if target_unit.type.size() == UnitSizeTypes.Large:
                    score += 32
            elif damage == DamageTypes.Concussive:
                if target_unit.type.size() == UnitSizeTypes.Small:
                    score += 32
                elif target_unit.type.size() == UnitSizeTypes.Large:
                    score -= 32

            # Give a big bonus to SCVs repairing a bunker that we can attack without coming into range of the bunker
            order_target = target_bwapi_unit.getOrderTarget()
            if (target_bwapi_unit.isRepairing() and order_target is not None
                    and order_target.getType() == UnitTypes.Terran_Bunker
                    and unit.get_distance(target_unit, target_unit.sim_position) <= weapon_range):
                bunker = units.get(order_target)
                if bunker is not None and not unit.is_in_enemy_weapon_range(bunker):
                    score += 256

            # See if this is the best target
            # Criteria:
            # - Score is higher
            # - Attackers is higher
            # - Distance is lower
            visible = target_bwapi_unit.isVisible()
            if ((not best_visible and visible)
                    or score > best_score
                    or (score == best_score and potential_target.attacker_count > best_attacker_count)
                    or (score == best_score and potential_target.attacker_count == best_attacker_count
                        and target_dist < best_dist)):
                best_score = score
                best_attacker_count = potential_target.attacker_count
                best_dist = target_dist
                best_target = potential_target
                best_visible = visible

        # For carriers, avoid frequently switching targets
        if unit.type == UnitTypes.Protoss_Carrier:
            current_target = get_current_target(unit)
            if (current_target is not None and unit.get_distance(current_target.unit) < 11 * 32
                    and unit.last_command_frame > (frame - 96)):
                best_target = current_target

        if best_target is not None:
            # Only simulate dealt damage if the unit is already in our weapon range
            # Otherwise especially melee units will be simulated very badly
            if unit.is_in_our_weapon_range(best_target.unit):
                best_target.deal_damage(unit)
            result.append((attacker.unit, best_target.unit))
        else:
            result.append((attacker.unit, None))

    return result

"""Port of Bullets.{h,cpp}: watches bullets to predict upcoming damage, infer enemy research and upgrades, and
estimate how many marines are in enemy bunkers."""

from __future__ import annotations

from dataclasses import dataclass, field

import bwapi
from bwapi import BulletType, BulletTypes, Position, Race, Races, TechType, TechTypes, UnitType, UnitTypes, \
    WalkPosition, WeaponTypes
from stardust import common, config
from stardust.cpp import INT_MAX
from stardust.instrumentation import cherryvis, log

# Bullet types that deal damage after a delay, where the bullet travels to its target, mapped to the unit types that
# fire them and their initial bullet remove timer. We use these for damage from bullets that come out of the fog; the
# remove timer differentiates which unit type fired the bullet where several can (not Dragoon vs. Arbiter though,
# since they have the same delay). Sorted by unit type id, as C++ std::set iterates them.
_DELAYED_DAMAGE_BULLET_UNITS: dict[BulletType, list[tuple[UnitType, int]]] = {
    bullet_type: sorted(units, key=lambda u: (int(u[0]), u[1])) for bullet_type, units in {
        BulletTypes.Burst_Lasers: [(UnitTypes.Terran_Wraith, 254)],
        BulletTypes.Gemini_Missiles: [(UnitTypes.Terran_Wraith, 254)],
        BulletTypes.Fragmentation_Grenade: [(UnitTypes.Terran_Vulture, 254)],
        BulletTypes.Longbolt_Missile: [(UnitTypes.Terran_Missile_Turret, 254), (UnitTypes.Terran_Goliath, 246)],
        BulletTypes.ATS_ATA_Laser_Battery: [(UnitTypes.Terran_Battlecruiser, 254)],
        BulletTypes.Yamato_Gun: [(UnitTypes.Terran_Battlecruiser, 254)],
        BulletTypes.Halo_Rockets: [(UnitTypes.Terran_Valkyrie, 254)],
        BulletTypes.Anti_Matter_Missile: [(UnitTypes.Protoss_Scout, 29)],
        BulletTypes.Phase_Disruptor: [(UnitTypes.Protoss_Dragoon, 254), (UnitTypes.Protoss_Arbiter, 254)],
        BulletTypes.STA_STS_Cannon_Overlay: [(UnitTypes.Protoss_Photon_Cannon, 254)],
        BulletTypes.Pulse_Cannon: [(UnitTypes.Protoss_Interceptor, 254)],
        BulletTypes.Glave_Wurm: [(UnitTypes.Zerg_Mutalisk, 60)],
        BulletTypes.Seeker_Spores: [(UnitTypes.Zerg_Spore_Colony, 59)],
        BulletTypes.Subterranean_Spines: [(UnitTypes.Zerg_Lurker, 254)],
        BulletTypes.Acid_Spore: [(UnitTypes.Zerg_Guardian, 254)],
        BulletTypes.Corrosive_Acid_Shot: [(UnitTypes.Zerg_Devourer, 254)],
    }.items()
}

# Bullet types without travel time that deal damage after a fixed frame delay
_FIXED_DELAYED_DAMAGE_BULLETS: dict[BulletType, tuple[UnitType, int]] = {
    BulletTypes.Arclite_Shock_Cannon_Hit: (UnitTypes.Terran_Siege_Tank_Siege_Mode, 1),
    BulletTypes.Invisible: (UnitTypes.Terran_Firebat, 5),
    BulletTypes.Psionic_Shockwave_Hit: (UnitTypes.Protoss_Archon, 3),
    BulletTypes.Sunken_Colony_Tentacle: (UnitTypes.Zerg_Sunken_Colony, 5),
}

# Bullet types that infer tech: (tech, race that uses it, whether it is usually used on own units)
_BULLET_TECH_TYPES: dict[BulletType, tuple[TechType, Race, bool]] = {
    BulletTypes.Psionic_Storm: (TechTypes.Psionic_Storm, Races.Protoss, False),
    BulletTypes.EMP_Missile: (TechTypes.EMP_Shockwave, Races.Terran, False),
    BulletTypes.Yamato_Gun: (TechTypes.Yamato_Gun, Races.Terran, False),
    BulletTypes.Optical_Flare_Grenade: (TechTypes.Optical_Flare, Races.Terran, False),
    BulletTypes.Plague_Cloud: (TechTypes.Plague, Races.Zerg, False),
    BulletTypes.Consume: (TechTypes.Consume, Races.Zerg, True),
    BulletTypes.Ensnare: (TechTypes.Ensnare, Races.Zerg, False),
}

_seen_bullet_frames: dict[int, int] = {}
_bullets_seen_at_extended_marine_range = 0


@dataclass
class _BunkerBullet:
    id: int
    first_seen_frame: int
    position: Position


# Most recent first
_bunker_bullets: list[_BunkerBullet] = []


def _track_research(bullet: bwapi.Bullet) -> None:
    from stardust.players import players

    # Reference the data for this bullet type, or return if it isn't a bullet that infers tech research
    tech = _BULLET_TECH_TYPES.get(bullet.getType())
    if tech is None:
        return
    tech_type, race, used_on_own_units = tech
    game = bwapi.Broodwar

    # Determine the player. This may be None if the source of the bullet has died.
    player = bullet.getPlayer()

    # If the tech is used by a race other than ours, we can infer that the enemy used it
    if player is None and race != game.self().getRace():
        player = game.enemy()

    # If the bullet has a target unit, we can infer the player based on who the tech is expected to be used on
    target = bullet.getTarget()
    if player is None and target is not None and target.getPlayer() is not None:
        if target.getPlayer() == game.self():
            player = game.self() if used_on_own_units else game.enemy()
        elif target.getPlayer() == game.enemy():
            player = game.enemy() if used_on_own_units else game.self()

    if player is not None:
        players.set_has_researched(player, tech_type)


def _check_bunker_range(bullet: bwapi.Bullet) -> None:
    global _bullets_seen_at_extended_marine_range
    from stardust.players import players

    game = bwapi.Broodwar

    # Bail out if we already know the enemy has the upgrade
    if players.weapon_range(game.enemy(), WeaponTypes.Gauss_Rifle) == 160:
        return

    # If the bullet has a source, it definitely isn't from a bunker. The bullet may still be from a marine if it has
    # died in the meantime, but since we analyze on the second frame, this is unlikely to happen.
    if bullet.getSource() is not None:
        return

    # Ignore bullets where the target has died in the meantime
    target = bullet.getTarget()
    if target is None:
        return

    # Get the closest visible bunker to the bullet
    best_dist = INT_MAX
    bunker: bwapi.Unit | None = None
    for unit in game.enemy().getUnits():
        if not unit.exists() or not unit.isVisible() or not unit.isCompleted():
            continue
        if unit.getType() != UnitTypes.Terran_Bunker:
            continue
        dist = unit.getDistance(bullet.getPosition())
        if dist < best_dist:
            best_dist = dist
            bunker = unit
    if bunker is None:
        return

    # The bullet seems to always be located 7 pixels "inside" the target, so use this to compute distance between
    # bunker and target
    best_dist -= 7

    # Now use this to determine if the marines have the range upgrade. We get some false positives, so use a
    # relatively conservative distance range and make sure we have seen a few volleys.
    if 190 <= best_dist <= 192:
        _bullets_seen_at_extended_marine_range += 1
        if _bullets_seen_at_extended_marine_range > 4:
            cherryvis.log(f"Detected ranged marines in bunker @ {WalkPosition(bunker.getTilePosition())}; target @ "
                          f"{WalkPosition(target.getTilePosition())}; dist={best_dist}")
            log.get(f"Detected ranged marines in bunker @ {bunker.getTilePosition()}; target @ "
                    f"{target.getTilePosition()}; dist={best_dist}")
            players.set_weapon_range(game.enemy(), WeaponTypes.Gauss_Rifle, 160)


def initialize() -> None:
    global _bullets_seen_at_extended_marine_range
    _seen_bullet_frames.clear()
    _bullets_seen_at_extended_marine_range = 0
    _bunker_bullets.clear()


def update() -> None:
    from stardust.map import no_go_areas
    from stardust.units import units

    frame = common.current_frame
    for bullet in bwapi.Broodwar.getBullets():
        # Ignore invalid bullets
        if (not bullet.exists() or not bullet.isVisible()
                or (bullet.getSource() is None and bullet.getTarget() is None)):
            continue

        # Track enemy research
        _track_research(bullet)

        # Call on_bullet_create on the first frame the bullet is seen
        seen_frame = _seen_bullet_frames.get(bullet.getID())
        if seen_frame is None:
            units.on_bullet_create(bullet)
            no_go_areas.on_bullet_create(bullet)
            _seen_bullet_frames[bullet.getID()] = frame
            continue

        # For marine rifle hits, check if we can deduce whether or not the enemy has the range upgrade for shots from
        # a bunker. We check on the frame after we first see the bullet, since this is when hidden units appear.
        if bullet.getType() == BulletTypes.Gauss_Rifle_Hit and seen_frame == frame - 1:
            _check_bunker_range(bullet)
            if config.USE_BUNKER_ATTACKER_LOGIC and bullet.getSource() is None:
                _bunker_bullets.insert(0, _BunkerBullet(bullet.getID(), frame - 1, bullet.getPosition()))


def deals_damage_after_delay(bullet_type: BulletType) -> bool:
    return bullet_type in _DELAYED_DAMAGE_BULLET_UNITS


def fixed_damage_delay(bullet_type: BulletType) -> int | None:
    info = _FIXED_DELAYED_DAMAGE_BULLETS.get(bullet_type)
    return info[1] if info is not None else None


def upcoming_damage(bullet: bwapi.Bullet) -> int:
    from stardust.players import players

    game = bwapi.Broodwar

    # Require target
    target = bullet.getTarget()
    if target is None or target.getPlayer() is None:
        return 0

    # Validate target position for tracking bullets
    tracking_bullet_info = _DELAYED_DAMAGE_BULLET_UNITS.get(bullet.getType())
    if tracking_bullet_info is not None and bullet.getTargetPosition() != target.getPosition():
        return 0

    # Get the player owning the bullet
    source = bullet.getSource()
    attacking_player = source.getPlayer() if source is not None else bullet.getPlayer()
    if attacking_player is None:
        attacking_player = game.enemy() if target.getPlayer() == game.self() else game.self()

    # Set weapon override for Yamato
    weapon_override = WeaponTypes.Yamato_Gun if bullet.getType() == BulletTypes.Yamato_Gun else WeaponTypes.None_

    # Figure out what unit type fired the bullet
    source_unit_type = source.getType() if source is not None else UnitTypes.None_

    # Ranged bullet that deals damage after a delay
    if source_unit_type == UnitTypes.None_ and tracking_bullet_info is not None:
        for unit_type, initial_remove_timer in tracking_bullet_info:
            if source is not None and unit_type != source.getType():
                continue
            if bullet.getRemoveTimer() != initial_remove_timer:
                continue
            source_unit_type = unit_type  # (no break: the last match wins, as in Stardust)

        # If we didn't see the bullet on its first frame and couldn't get an exact match, assume the first possibility
        if source_unit_type == UnitTypes.None_:
            source_unit_type = tracking_bullet_info[0][0]

    # Non-ranged bullet that deals damage after a delay
    if source_unit_type == UnitTypes.None_:
        bullet_info = _FIXED_DELAYED_DAMAGE_BULLETS.get(bullet.getType())
        if bullet_info is not None:
            source_unit_type = bullet_info[0]

    # C++ `if (!sourceUnitType)` tests the type id, which is only 0 for Terran_Marine
    if int(source_unit_type) == 0:
        return 0
    return players.attack_damage(attacking_player, source_unit_type, target.getPlayer(), target.getType(),
                                 weapon_override)


@dataclass
class _MatchedBunkerBullet:
    id: int
    first_seen_frame: int
    next_round_same_marine_bullet_id: int = -1  # Bullet in the next round matched to the same marine


@dataclass(eq=False)
class _BunkerBulletMatches:
    bunker: object  # EnemyBunker
    first_round_bullets: list[_MatchedBunkerBullet] = field(default_factory=list)
    second_round_bullets: list[_MatchedBunkerBullet] = field(default_factory=list)
    has_third_round: bool = False

    def add(self, bunker_bullet: _BunkerBullet) -> bool:
        """Marks the bunker bullet as fired from this bunker. Returns False if the bullet is expired (too old to
        consider for the first round, or would be placed in a third round)."""
        from stardust.units.enemy_bunker import EnemyBunker

        bunker = self.bunker
        assert isinstance(bunker, EnemyBunker)

        # This check is a bit difficult to do perfectly, since the bullet gets put about 7 pixels inside the target,
        # and knowing whether the enemy has the marine range upgrade is similarly difficult to perfectly detect. Since
        # false negatives (not matching a bullet to any bunker) are much worse than false positives (thinking a shot
        # could come from multiple bunkers), we use a loose distance check assuming the enemy has range, plus half a
        # tile of buffer.
        if bunker.get_distance(bunker_bullet.position) > 216:
            return False

        # This is the first bullet assigned to this bunker
        if not self.first_round_bullets:
            # If the bullet is much older than the maximum time between shots, this bunker has stopped firing
            if bunker_bullet.first_seen_frame < common.current_frame - 20:
                return False
            self.first_round_bullets.append(_MatchedBunkerBullet(bunker_bullet.id, bunker_bullet.first_seen_frame))
            return True

        # This is not the first bullet assigned to this bunker, so figure out which "round" it fits into
        def is_in_next_round(previous_round: list[_MatchedBunkerBullet], skip: int = 0) -> bool:
            if skip >= len(previous_round):
                return False
            if previous_round[skip].first_seen_frame - bunker_bullet.first_seen_frame >= 12:
                previous_round[skip].next_round_same_marine_bullet_id = bunker_bullet.id
                return True
            return False

        # First check if the bullet could be in the third round, in which case we are done
        if is_in_next_round(self.second_round_bullets):
            self.has_third_round = True
            return False

        # If this bullet doesn't fit into the second round, potentially shift a bullet previously detected to align
        # everything correctly
        while not is_in_next_round(self.first_round_bullets, len(self.second_round_bullets)):
            # Add to the first round if there is nothing in the second round
            if not self.second_round_bullets:
                self.first_round_bullets.append(_MatchedBunkerBullet(bunker_bullet.id, bunker_bullet.first_seen_frame))
                return True

            # We previously put a bullet in the second round that now prevents a bullet from being matched, so it
            # should have been in the first round. Clear its related bullet and move it.
            moved = self.second_round_bullets.pop(0)
            for bullet in self.first_round_bullets:
                if bullet.next_round_same_marine_bullet_id == moved.id:
                    bullet.next_round_same_marine_bullet_id = -1
            self.first_round_bullets.append(moved)

        # The bullet goes in the second round
        self.second_round_bullets.append(_MatchedBunkerBullet(bunker_bullet.id, bunker_bullet.first_seen_frame))
        return True

    def set_marine_count(self, marine_count: int) -> None:
        from stardust.units.enemy_bunker import EnemyBunker

        bunker = self.bunker
        assert isinstance(bunker, EnemyBunker)

        # Cap at 4 in case we've misdetected something (e.g. stimmed marines)
        marine_count = min(marine_count, 4)
        if bunker.loaded_marines != marine_count:
            cherryvis.log(f"Updated loaded marines from {bunker.loaded_marines} to {marine_count}", bunker.id)
        bunker.loaded_marines = marine_count


def update_bunkers() -> None:
    """Updates how many marines we think are in each of the enemy's bunkers based on observed bullets.

    - Match each observed bunker-fired bullet with a bunker. If multiple bunkers are in range, track it on each and
      deduplicate later. If a bullet could have been fired by the same marine as an earlier one, track them as two
      separate "rounds". Stop at a third round, or at a first round beyond a frame threshold.
    - With two full rounds from a bunker, use the max shots per round as the marine count.
    - With only one round, use the number of observed shots unless it is lower than the current count (marines fire
      at different times because of their order process timers).
    - For bunkers with no matched bullets but previously containing marines, check if one of our units has been in
      range for a while, meaning it has probably been emptied.

    Marines are assumed not stimmed; a stimmed marine just looks like two, which reflects its combat value. The
    cooldown of a non-stimmed marine is randomized in [14,17], but bullets from the same marine can be as little as 12
    frames apart.
    """
    from stardust.units import units
    from stardust.units.enemy_bunker import EnemyBunker

    if not config.USE_BUNKER_ATTACKER_LOGIC:
        return

    frame = common.current_frame

    # Initialize the bunker data structures
    bunkers = [_BunkerBulletMatches(unit) for unit in units.all_enemy_of_type(UnitTypes.Terran_Bunker)
               if unit.completed and isinstance(unit, EnemyBunker)]

    # Go backwards through the bullets and assign them to bunkers, tracking bullets assigned to multiple bunkers
    bullets_with_multiple_bunkers: list[tuple[int, list[_BunkerBulletMatches]]] = []
    kept = []
    for bunker_bullet in _bunker_bullets:
        bunkers_matched = [bunker for bunker in bunkers if bunker.add(bunker_bullet)]
        if bunkers_matched:
            if len(bunkers_matched) > 1:
                bullets_with_multiple_bunkers.append((bunker_bullet.id, bunkers_matched))
        elif bunker_bullet.first_seen_frame < frame - 51:
            continue  # Clear bullets after 3 times max marine cooldown
        kept.append(bunker_bullet)
    _bunker_bullets[:] = kept

    # Deduplicate bullets assigned to multiple bunkers
    for bullet_id, bunker_matches in bullets_with_multiple_bunkers:
        # (bunker match, in second round, next round same marine bullet id, bunker bullet count)
        find_results: list[tuple[_BunkerBulletMatches, bool, int, int]] = []

        def remove_from_bunker(result: tuple[_BunkerBulletMatches, bool, int, int]) -> None:
            match, in_second_round, next_round_id, _ = result

            def erase(remove_id: int, bullets: list[_MatchedBunkerBullet]) -> None:
                for i, bullet in enumerate(bullets):
                    if bullet.id == remove_id:
                        del bullets[i]
                        return

            erase(bullet_id, match.second_round_bullets if in_second_round else match.first_round_bullets)
            if next_round_id != -1:
                erase(next_round_id, match.second_round_bullets)

        # First pass: find the bullet in each bunker
        any_have_next_round_same_marine_bullet_id = False
        for match in bunker_matches:
            first_round = next((b for b in match.first_round_bullets if b.id == bullet_id), None)
            if first_round is not None:
                any_have_next_round_same_marine_bullet_id = (any_have_next_round_same_marine_bullet_id
                                                             or first_round.next_round_same_marine_bullet_id != -1)
                find_results.append((match, False, first_round.next_round_same_marine_bullet_id,
                                     len(match.first_round_bullets)))
                continue
            if any(b.id == bullet_id for b in match.second_round_bullets):
                find_results.append((match, True, -1, len(match.second_round_bullets)))
            # Otherwise a matched first round bullet was removed in an earlier iteration, so skip this bunker

        if not find_results:
            continue

        # Second pass: if the bullet is matched to a second round bullet in some bunker, remove it from any bunkers
        # where this isn't the case. Keep track of whether any kept bunkers have the bullet in the second round.
        any_in_second_round = False
        remaining = []
        for result in find_results:
            if any_have_next_round_same_marine_bullet_id and result[2] == -1:
                remove_from_bunker(result)
            else:
                any_in_second_round = any_in_second_round or result[1]
                remaining.append(result)
        find_results = remaining

        # Third pass: if the bullet is in the second round in some bunker, remove it from any bunkers where it is in
        # the first round. Keep track of which kept bunker has the least bullets.
        min_count = 2**32 - 1
        remaining = []
        for result in find_results:
            if any_in_second_round and not result[1]:
                remove_from_bunker(result)
            else:
                min_count = min(min_count, result[3])
                remaining.append(result)
        find_results = remaining

        # Fourth pass: remove the bullet from any bunkers that have a higher count than the minimum
        remaining = []
        for result in find_results:
            if result[3] > min_count:
                remove_from_bunker(result)
            else:
                remaining.append(result)
        find_results = remaining

        # Guard against removing it from every bunker
        if not find_results:
            log.get("ERROR: Logic error in bunker bullet detection, have removed the bullet from all bunkers")
            continue

        # Final pass: keep the bullet only in the first bunker
        for result in find_results[1:]:
            remove_from_bunker(result)

    # Update the marine counts on the bunkers
    for match in bunkers:
        bunker = match.bunker
        assert isinstance(bunker, EnemyBunker)
        if not match.first_round_bullets:
            # If one of our units has been in range of the bunker for more than 15 frames, assume it is now empty
            if bunker.my_unit_in_range and bunker.frame_my_unit_in_range_last_changed < frame - 15:
                match.set_marine_count(0)
        elif not match.second_round_bullets:
            # We are seeing the first shots from the bunker, so bump up the count if needed but otherwise keep it
            match.set_marine_count(max(bunker.loaded_marines, len(match.first_round_bullets)))
        else:
            # We have two rounds of shots, so take whichever round has the most shots. As differences in possible
            # cooldowns can cause misdetections, we don't reduce an existing count if we haven't seen a third round
            # yet, or if we have been in range of this bunker for quite a while.
            if match.has_third_round and (
                    (not bunker.my_unit_in_range and bunker.frame_my_unit_in_range_last_changed > frame - 15)
                    or (bunker.my_unit_in_range and bunker.frame_my_unit_in_range_last_changed > frame - 51)):
                match.set_marine_count(max(len(match.first_round_bullets), len(match.second_round_bullets)))
            else:
                match.set_marine_count(max(bunker.loaded_marines, len(match.first_round_bullets),
                                           len(match.second_round_bullets)))

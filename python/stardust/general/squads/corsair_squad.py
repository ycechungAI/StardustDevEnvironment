"""Port of General/Squads/CorsairSquad.{h,cpp}: controls our corsairs, defending our bases from air units, hunting
enemy air units and scouting.

The verbose target selection log (CVIS_LOG_TARGET_SELECTION) and DEBUG_COMBATSIM messages are omitted.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import Position, Positions, TilePosition, UnitTypes, WalkPosition
from stardust import common, config
from stardust.cpp import INT_MAX
from stardust.general.squad import Squad
from stardust.general.unit_cluster.unit_cluster import Activity, UnitCluster
from stardust.instrumentation import cherryvis
from stardust.map import game_map
from stardust.map.path_finding import path_finding
from stardust.players import players
from stardust.units import units
from stardust.util import geo, unit_util
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.general.combat_sim_result import CombatSimResult
    from stardust.map.base import Base
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit

_CVIS_BOARD_VALUE = config.INSTRUMENTATION_ENABLED


def _my_main_position() -> Position:
    my_main = game_map.get_my_main()
    assert my_main is not None
    return my_main.get_position()


def _enemy_or_my_main_position() -> Position:
    enemy_main = game_map.get_enemy_main()
    return enemy_main.get_position() if enemy_main is not None else _my_main_position()


def _scaled_position(current_position: Position, vector: Position, length: int) -> Position:
    scaled_vector = geo.scale_vector(vector, length)
    if scaled_vector == Positions.Invalid:
        return Positions.Invalid
    return current_position + scaled_vector


def _corsair_move(corsair: MyUnit, target: Position) -> None:
    halt_distance = unit_util.halt_distance(UnitTypes.Protoss_Corsair)

    # Scale to always move at least our halt distance
    move_target = _scaled_position(corsair.last_position, target - corsair.last_position, halt_distance)
    if not move_target.isValid():
        corsair.move_to(target)
        return

    # Check for threats
    grid = players.grid(bwapi.Broodwar.enemy())
    if grid.air_threat(move_target) == 0:
        corsair.move_to(move_target)
        return

    # There is a threat - find a default position to move to if we don't find a better option
    move_target = _scaled_position(corsair.last_position, corsair.last_position - target, halt_distance)
    if not move_target.isValid():
        move_target = _my_main_position()

    # Search for a safe path, stopping if it gets us 10 tiles closer
    target_tile = TilePosition(target)
    current_dist = corsair.get_tile_position().getApproxDistance(target_tile)

    def avoid_threat_tiles(tile: TilePosition) -> bool:
        return grid.air_threat_at((tile.x << 2) + 2, (tile.y << 2) + 2) == 0

    def close_enough_to_end(tile: TilePosition) -> bool:
        return (current_dist - tile.getApproxDistance(target_tile)) > 10

    path = path_finding.search(corsair.get_tile_position(), target_tile, avoid_threat_tiles, close_enough_to_end, 2)
    if path:
        move_target = Position(path[0]) + Position(16, 16)

        scaled = _scaled_position(corsair.last_position, move_target - corsair.last_position, halt_distance)
        if scaled.isValid():
            move_target = scaled
        elif len(path) > 2:
            move_target = Position(path[2]) + Position(16, 16)
        elif len(path) > 1:
            move_target = Position(path[1]) + Position(16, 16)

    corsair.move_to(move_target)


def _cluster_move(cluster: UnitCluster, target: Position) -> None:
    cluster.set_activity(Activity.Moving)

    for unit in cluster.units:
        _corsair_move(unit, target)


def _main_army_vanguard() -> UnitCluster | None:
    import stardust.strategist.strategist as strategist

    main_army_play = strategist.get_main_army_play()
    if main_army_play is None:
        return None

    return main_army_play.get_squad().vanguard_cluster()


def _regroup_target(cluster: UnitCluster, default_position: Position) -> Position:
    positions: list[Position] = []

    # Add the main army's vanguard cluster if it has at least one dragoon
    vanguard = _main_army_vanguard()
    if vanguard is not None and vanguard.has_unit_type(UnitTypes.Protoss_Dragoon):
        positions.append(vanguard.center)

    # Add powered cannons
    for cannon in units.all_mine_completed_of_type(UnitTypes.Protoss_Photon_Cannon):
        if cannon.bwapi_unit is None or not cannon.bwapi_unit.isPowered():
            continue
        positions.append(cannon.last_position)

    # Return the closest position
    best = default_position
    best_dist = INT_MAX
    for pos in positions:
        dist = cluster.center.getApproxDistance(pos)
        if dist < best_dist:
            best_dist = dist
            best = pos
    return best


def _should_attack(cluster: UnitCluster, sim_result: CombatSimResult, aggression: float = 1.0) -> bool:
    def attack() -> bool:
        # Always attack if we don't lose anything
        if sim_result.my_percent_lost() <= 0.001:
            return True

        # Attack in cases where we think we will kill 50% more value than we lose
        if (aggression > 0.99 and sim_result.value_gain() > (sim_result.initial_mine - sim_result.final_mine) // 2
                and (sim_result.percent_gain() > -0.05 or sim_result.my_percentage_of_total() > 0.9)):
            return True

        # Attack if we expect to end the fight with a sufficiently larger army and aren't losing an unacceptable
        # percentage of it
        percent_of_total = sim_result.my_percentage_of_total() - (0.9 - 0.35 * aggression)
        if percent_of_total > 0.0 and sim_result.percent_gain() > (-0.05 * aggression - percent_of_total):
            return True

        # Attack if the percentage gain, adjusted for aggression and distance factor, is acceptable
        # A percentage gain here means the enemy loses a larger percentage of their army than we do
        return sim_result.percent_gain() > (0.2 / aggression)

    result = attack()

    sim_result.distance_factor = 1.0
    sim_result.aggression = aggression

    return result


def _should_start_attack(cluster: UnitCluster, sim_result: CombatSimResult) -> bool:
    sim_result.decision = _should_attack(cluster, sim_result)
    return sim_result.decision


def _should_continue_attack(cluster: UnitCluster, sim_result: CombatSimResult) -> bool:
    sim_result.decision = _should_attack(cluster, sim_result, 1.2)
    if sim_result.decision:
        return True

    # TODO: Would probably be a good idea to run a retreat sim to see what the consequences of retreating are

    # If this is the first run of the combat sim for this fight, always abort immediately
    if len(cluster.recent_sim_results) < 2:
        return False

    previous_sim_result = cluster.recent_sim_results[-2]

    # If the enemy army strength has increased significantly, abort the attack immediately
    if sim_result.initial_enemy > int(previous_sim_result.initial_enemy * 1.2):
        return False

    consecutive_retreat_frames, attack_frames, regroup_frames = UnitCluster.consecutive_sim_results(
        cluster.recent_sim_results, 48)

    # Continue if the sim hasn't been stable for 6 frames
    if consecutive_retreat_frames < 6:
        return True

    # Continue if the sim has recommended attacking more than regrouping; otherwise abort
    return attack_frames > regroup_frames


def _should_stop_regrouping(cluster: UnitCluster, sim_result: CombatSimResult) -> bool:
    sim_result.decision = _should_attack(cluster, sim_result, 0.8)
    if not sim_result.decision:
        return False

    consecutive_attack_frames, attack_frames, regroup_frames = UnitCluster.consecutive_sim_results(
        cluster.recent_sim_results, 72)

    # Continue if the sim hasn't been stable for 12 frames
    if consecutive_attack_frames < 12:
        return False

    # Continue if the sim has recommended regrouping more than attacking
    if regroup_frames > attack_frames:
        return False

    # Continue if our number of units has increased in the past 72 frames.
    # This gives our reinforcements time to link up with the rest of the cluster before engaging.
    for count, recent in enumerate(reversed(cluster.recent_sim_results)):
        if count >= 72:
            break
        if sim_result.my_unit_count > recent.my_unit_count:
            return False

    # Start the attack
    return True


def _decide(cluster: UnitCluster, sim_result: CombatSimResult) -> bool:
    if cluster.current_activity == Activity.Moving:
        return _should_start_attack(cluster, sim_result)
    if cluster.current_activity == Activity.Attacking:
        return _should_continue_attack(cluster, sim_result)
    return _should_stop_regrouping(cluster, sim_result)


class CorsairSquad(Squad):
    def __init__(self) -> None:
        super().__init__("Corsairs")
        self.target_position = _enemy_or_my_main_position()

    def _set_board_value(self, value: str) -> None:
        if _CVIS_BOARD_VALUE:
            cherryvis.set_board_value("corsairs", value)

    def execute(self) -> None:
        import stardust.strategist.strategist as strategist

        if not self.clusters:
            return

        # Helper to set the target position and recompute the clusters so we mark the correct vanguard cluster
        def ensure_target_position(position: Position) -> None:
            if self.target_position.getApproxDistance(position) < 32:
                return
            self.target_position = position
            self.update_clusters()

        # Select the strategy to use for our corsairs based on the targets available
        # Priority is:
        # - Scout enemy main and natural if it is early-game and we haven't scouted it recently
        # - Combat units threatening our bases
        # - Other combat units
        # - Non-combat units not covered by anti-air
        # - Other non-combat units
        # - Scout enemy bases

        remaining_clusters = list(self.clusters)

        # Check if we need to do an early-game scout
        if (strategist.is_worker_scout_complete() and common.current_frame < 11000
                and not units.has_enemy_built(UnitTypes.Zerg_Scourge)
                and not units.has_enemy_built(UnitTypes.Zerg_Hydralisk)
                and not units.has_enemy_built(UnitTypes.Zerg_Mutalisk)):
            base_to_scout: Base | None = None

            enemy_main = game_map.get_enemy_starting_main()
            enemy_natural = game_map.get_enemy_starting_natural()
            if enemy_main is not None and enemy_main.last_scouted < 7500:
                base_to_scout = enemy_main
            elif enemy_natural is not None and enemy_natural.last_scouted < 7500:
                base_to_scout = enemy_natural

            if base_to_scout is not None:
                # Scout with the closest cluster
                best_dist = INT_MAX
                best_cluster: UnitCluster | None = None
                for cluster in remaining_clusters:
                    dist = cluster.center.getApproxDistance(base_to_scout.get_position())
                    if dist < best_dist:
                        best_dist = dist
                        best_cluster = cluster
                if best_cluster is not None:
                    _cluster_move(best_cluster, base_to_scout.get_position())
                    remaining_clusters.remove(best_cluster)

        if not remaining_clusters:
            enemy_starting_main = game_map.get_enemy_starting_main()
            assert enemy_starting_main is not None
            ensure_target_position(enemy_starting_main.get_position())
            self._set_board_value("early-game-scout")
            return

        # Now scan for targets
        grid = players.grid(bwapi.Broodwar.enemy())
        threatened_bases: dict[Base, set[Unit]] = {}
        combat_targets: set[Unit] = set()
        vulnerable_non_combat_targets: set[Unit] = set()
        non_combat_targets: list[tuple[int, Unit]] = []
        for unit in units.all_enemy():
            if not unit.is_flying:
                continue
            if not unit.is_attackable():
                continue

            # Ignore targets that haven't been seen recently
            # 1 minute threshold for overlords with last position valid, 30 seconds for overlords with last position
            # invalid, 5 seconds for all other targets
            last_seen_threshold = 120
            if unit.type == UnitTypes.Zerg_Overlord:
                last_seen_threshold = 1440 if unit.last_position_valid else 720
            if unit.last_seen < common.current_frame - last_seen_threshold:
                continue

            # Check if it is threatening one of our bases
            if unit.can_attack_ground():
                matched = False
                for base in game_map.get_my_bases():
                    if unit in units.enemy_at_base(base):
                        threatened_bases.setdefault(base, set()).add(unit)
                        matched = True
                        break
                if matched:
                    continue

                combat_targets.add(unit)
                continue

            # Combat target
            if unit.can_attack_air():
                combat_targets.add(unit)
                continue

            # Non-combat target
            threat = grid.air_threat(unit.last_position)
            if threat == 0:
                vulnerable_non_combat_targets.add(unit)
            else:
                non_combat_targets.append((threat, unit))

        # (std::set of (threat, unit pointer) in Stardust: the least-defended first, ties by unit id here)
        non_combat_targets.sort(key=lambda entry: (entry[0], entry[1].id))

        # The least-scouted enemy base, evaluated lazily
        least_scouted_computed = False
        least_scouted: Base | None = None

        def least_scouted_enemy_base() -> Base | None:
            nonlocal least_scouted_computed, least_scouted
            if least_scouted_computed:
                return least_scouted

            least_scouted_computed = True

            last_scouted = INT_MAX

            def visit(base: Base, limit: int = 0) -> None:
                nonlocal last_scouted, least_scouted

                # Ignore bases that are covered by anti-air
                if grid.air_threat(base.get_position()) > 0:
                    return

                if base.last_scouted < last_scouted and base.last_scouted <= common.current_frame - limit:
                    last_scouted = base.last_scouted
                    least_scouted = base

            for base in game_map.get_enemy_bases():
                visit(base)
            untaken = game_map.get_untaken_expansions(bwapi.Broodwar.enemy())
            if untaken:
                visit(untaken[0], 1440)

            return least_scouted

        # Pick the targets
        targets: set[Unit] = set()
        if threatened_bases:
            # Pick the base that has the most workers
            best_workers = 0
            best_base: Base | None = None
            for base in threatened_bases:
                worker_count = workers.get_base_worker_count(base)
                if worker_count > best_workers:
                    best_workers = worker_count
                    best_base = base
            if best_base is None:
                best_base = next(iter(threatened_bases))

            targets = set(threatened_bases[best_base])
            ensure_target_position(best_base.get_position())

            self._set_board_value(f"defend-{best_base.get_tile_position()}")

            for cluster in remaining_clusters:
                self._cluster_defend(cluster, best_base, targets)
            return

        elif combat_targets:
            targets = combat_targets
            ensure_target_position(_enemy_or_my_main_position())
            self._set_board_value("attack-combat")

        elif vulnerable_non_combat_targets:
            targets = vulnerable_non_combat_targets
            ensure_target_position(_enemy_or_my_main_position())
            self._set_board_value("attack-vulnerable")

        elif (scout_base := least_scouted_enemy_base()) is not None:
            pos = scout_base.get_position()
            ensure_target_position(pos)
            self._set_board_value(f"scout-{scout_base.get_tile_position()}")

            # (Stardust then also falls through to attack with no targets, which moves the clusters again.)
            for cluster in remaining_clusters:
                _cluster_move(cluster, pos)

        elif non_combat_targets:
            target = non_combat_targets[0][1]
            targets.add(target)
            ensure_target_position(target.last_position)
            self._set_board_value(f"attack-noncombat-{target.get_tile_position()}")

        else:
            # Move to an appropriate location:
            # - least-scouted enemy base
            # - main army vanguard cluster
            # - our main
            wait_base = least_scouted_enemy_base()
            if wait_base is not None:
                pos = wait_base.get_position()
            else:
                vanguard = _main_army_vanguard()
                pos = vanguard.center if vanguard is not None else _my_main_position()

            ensure_target_position(pos)
            self._set_board_value(f"wait-{WalkPosition(pos)}")

            for cluster in remaining_clusters:
                _cluster_move(cluster, pos)
            return

        for cluster in remaining_clusters:
            self._cluster_attack(cluster, targets)

    def _attack_or_move(self, cluster: UnitCluster, units_and_targets: list[tuple[MyUnit, Unit | None]]) -> None:
        cluster.set_activity(Activity.Attacking)

        for my_unit, target in units_and_targets:
            if not my_unit.is_ready():
                continue

            if target is not None:
                if my_unit.get_distance(target) > 320:
                    _corsair_move(my_unit, target.last_position)
                else:
                    my_unit.attack_unit(target, units_and_targets)
            else:
                _corsair_move(my_unit, self.target_position)

    def _regroup(self, cluster: UnitCluster) -> None:
        # Corsair move logic takes care of avoiding threats
        cluster.set_activity(Activity.Regrouping)
        pos = _regroup_target(cluster, self.target_position)
        for unit in cluster.units:
            _corsair_move(unit, pos)

    def _cluster_attack(self, cluster: UnitCluster, targets: set[Unit]) -> None:
        # Select targets
        units_and_targets = cluster.select_targets(targets, cluster.center)

        # If none of our units has a target, move to the target position
        if not any(target is not None for _, target in units_and_targets):
            _cluster_move(cluster, self.target_position)
            return

        # Add nearby ground units that can shoot at us
        def ground_threat(unit: Unit) -> bool:
            if unit.is_flying:
                return False
            if unit.immobile:
                return False
            return unit.can_attack_air()

        radius = 640 + cluster.vanguard.get_distance(cluster.center)
        enemy_units = set(targets) | units.enemy_in_radius(cluster.center, radius, ground_threat)

        # Run combat sim
        sim_result = cluster.run_recorded_combat_sim(cluster.recent_sim_results, self.target_position,
                                                     units_and_targets, enemy_units, self.detectors)

        # Determine if we should attack
        if _decide(cluster, sim_result):
            self._attack_or_move(cluster, units_and_targets)
            return

        self._regroup(cluster)

    def _cluster_defend(self, cluster: UnitCluster, base: Base, targets: set[Unit]) -> None:
        # If far away from the base, move towards it
        if cluster.center.getApproxDistance(base.get_position()) > 1000:
            _cluster_move(cluster, base.get_position())
            return

        # Select targets
        units_and_targets = cluster.select_targets(targets, cluster.center)

        # If none of our units has a target, move to the target position
        if not any(target is not None for _, target in units_and_targets):
            _cluster_move(cluster, self.target_position)
            return

        # If the cluster is covered by static defense, always attack
        grid = players.grid(bwapi.Broodwar.self())
        if grid.static_ground_threat(cluster.center) > 0:
            attack = True
        else:
            # Run combat sim
            radius = 640 + cluster.vanguard.get_distance(cluster.center)
            enemy_units = set(targets) | units.enemy_in_radius(cluster.center, radius)
            sim_result = cluster.run_recorded_combat_sim(cluster.recent_sim_results, self.target_position,
                                                         units_and_targets, enemy_units, self.detectors)
            attack = _decide(cluster, sim_result)

        if attack:
            self._attack_or_move(cluster, units_and_targets)
            return

        self._regroup(cluster)

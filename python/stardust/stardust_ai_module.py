"""Port of StardustAIModule.{h,cpp}: the bot itself, running each of the modules in turn every frame.

The C++ host (PythonAIModule) calls the BWAPI callbacks by their BWAPI names (onStart, onFrame, ...), so those methods
keep their camelCase names. Like Stardust, events are handled from Broodwar.getEvents() in onFrame so they can be
timed, and the other callbacks do nothing.

WorkerMiningInstrumentation (Stardust's mining research instrumentation) is not ported yet, so its calls are omitted.
"""

from __future__ import annotations

import cProfile
import gc
import os
import time
from collections.abc import Callable

import bwapi
from bwapi import EventType, Position, TilePosition, UnitTypes
from stardust import bullets, common, config, opponent
from stardust.builder import builder, building_placement
from stardust.general import general
from stardust.general.unit_cluster import combat_sim
from stardust.instrumentation import cherryvis, log, timer
from stardust.map import game_map, no_go_areas
from stardust.map.path_finding import path_finding
from stardust.players import players
from stardust.producer import producer
from stardust.strategist import strategist
from stardust.units import units
from stardust.util import geo
from stardust.workers import worker_gather_optimizer, workers

# While instrumenting we have a lower global frame limit to ensure we get data if the game locks up
if config.INSTRUMENTATION_ENABLED_VERBOSE:
    FRAME_LIMIT: int | None = 25000
elif config.INSTRUMENTATION_ENABLED:
    FRAME_LIMIT = 40000
else:
    FRAME_LIMIT = None

# Heatmaps are quite large, so we don't always want to write them every frame
# These configure what frequency to dump them, or 0 to disable them (Stardust only dumps them in verbose builds)
COLLISION_HEATMAP_FREQUENCY_ENEMY = 0
GROUND_THREAT_HEATMAP_FREQUENCY_ENEMY = 0
GROUND_THREAT_STATIC_HEATMAP_FREQUENCY_ENEMY = 0
AIR_THREAT_HEATMAP_FREQUENCY_ENEMY = 0
DETECTION_HEATMAP_FREQUENCY_ENEMY = 0
STASIS_RANGE_HEATMAP_FREQUENCY_ENEMY = 0

COLLISION_HEATMAP_FREQUENCY_MINE = 0
GROUND_THREAT_HEATMAP_FREQUENCY_MINE = 0
GROUND_THREAT_STATIC_HEATMAP_FREQUENCY_MINE = 0
AIR_THREAT_HEATMAP_FREQUENCY_MINE = 0
DETECTION_HEATMAP_FREQUENCY_MINE = 0

POWER_HEATMAP_FREQUENCY = 0  # Only tracked for self


def _handle_unit_discover(unit: bwapi.Unit) -> None:
    game_map.on_unit_discover(unit)


def _handle_unit_destroy(unit: bwapi.Unit) -> None:
    game_map.on_unit_destroy(unit)
    units.on_unit_destroy(unit)


def _dump_heatmaps() -> None:
    frame = common.current_frame
    game = bwapi.Broodwar

    def due(frequency: int) -> bool:
        return frequency > 0 and frame % frequency == 0

    if due(COLLISION_HEATMAP_FREQUENCY_ENEMY):
        players.grid(game.enemy()).dump_collision_heatmap_if_changed("CollisionEnemy")
    if due(COLLISION_HEATMAP_FREQUENCY_MINE):
        players.grid(game.self()).dump_collision_heatmap_if_changed("CollisionMine")
    if due(GROUND_THREAT_HEATMAP_FREQUENCY_ENEMY):
        players.grid(game.enemy()).dump_ground_threat_heatmap_if_changed("GroundThreatEnemy")
    if due(GROUND_THREAT_HEATMAP_FREQUENCY_MINE):
        players.grid(game.self()).dump_ground_threat_heatmap_if_changed("GroundThreatMine")
    if due(GROUND_THREAT_STATIC_HEATMAP_FREQUENCY_ENEMY):
        players.grid(game.enemy()).dump_static_ground_threat_heatmap_if_changed("GroundThreatStaticEnemy")
    if due(GROUND_THREAT_STATIC_HEATMAP_FREQUENCY_MINE):
        players.grid(game.self()).dump_static_ground_threat_heatmap_if_changed("GroundThreatStaticMine")
    if due(AIR_THREAT_HEATMAP_FREQUENCY_ENEMY):
        players.grid(game.enemy()).dump_air_threat_heatmap_if_changed("AirThreatEnemy")
    if due(AIR_THREAT_HEATMAP_FREQUENCY_MINE):
        players.grid(game.self()).dump_air_threat_heatmap_if_changed("AirThreatMine")
    if due(DETECTION_HEATMAP_FREQUENCY_ENEMY):
        players.grid(game.enemy()).dump_detection_heatmap_if_changed("DetectionEnemy")
    if due(DETECTION_HEATMAP_FREQUENCY_MINE):
        players.grid(game.self()).dump_detection_heatmap_if_changed("DetectionMine")
    if due(STASIS_RANGE_HEATMAP_FREQUENCY_ENEMY):
        players.grid(game.enemy()).dump_stasis_range_heatmap_if_changed("StasisRange")
    if due(POWER_HEATMAP_FREQUENCY):
        game_map.dump_power_heatmap()


_gc_callback: Callable[[str, dict[str, int]], None] | None = None


def _log_slow_garbage_collections(threshold_ms: float) -> None:
    global _gc_callback
    if _gc_callback is not None and _gc_callback in gc.callbacks:
        gc.callbacks.remove(_gc_callback)
    started = 0.0

    def callback(phase: str, info: dict[str, int]) -> None:
        nonlocal started
        if phase == "start":
            started = time.perf_counter()
            return
        elapsed_ms = (time.perf_counter() - started) * 1000
        if elapsed_ms >= threshold_ms:
            log.get(f"Garbage collection of generation {info['generation']} took {elapsed_ms:.1f}ms "
                    f"({info['collected']} collected, {len(gc.get_objects())} objects tracked)")

    _gc_callback = callback
    gc.callbacks.append(callback)


class StardustAIModule:
    def __init__(self) -> None:
        # Used in our test infrastructure
        self.frame_skip = 0
        self.test_on_start: Callable[[], None] | None = None
        self.test_on_frame: Callable[[], None] | None = None
        self.test_on_end: Callable[[bool], None] | None = None

        # Whether to surrender when losing is inevitable
        self.enable_surrender = config.VS_HUMAN

        self.enable_frame_limit = True

        self._game_finished = False

        # Not in Stardust: STARDUST_PROFILE_FRAMES=<file> writes a cProfile of all onFrame calls there (read it with
        # pstats). It's written every 1000 frames and at the end of the game.
        self._frame_profile_path = os.environ.get("STARDUST_PROFILE_FRAMES")
        self._frame_profile = cProfile.Profile() if self._frame_profile_path else None

    def onStart(self) -> None:
        # Not in Stardust: STARDUST_PROFILE_STARTUP=<file> writes a cProfile of startup there (read it with pstats)
        profile_path = os.environ.get("STARDUST_PROFILE_STARTUP")
        if profile_path:
            with cProfile.Profile() as profile:
                self._on_start()
            profile.dump_stats(profile_path)
        else:
            self._on_start()

    def _on_start(self) -> None:
        # Not in Stardust: STARDUST_LOG_GC=<ms> logs Python garbage collections that take at least that long
        gc_threshold = os.environ.get("STARDUST_LOG_GC")
        if gc_threshold:
            _log_slow_garbage_collections(float(gc_threshold))

        self._game_finished = False
        common.current_frame = 0

        # Initialize globals that just need to make sure their global data is reset
        log.initialize()
        builder.initialize()
        opponent.initialize()
        general.initialize()
        workers.initialize()
        bullets.initialize()
        players.initialize()
        geo.initialize()
        path_finding.clear_grids()
        path_finding.initialize_search()

        log.set_debug(True)
        cherryvis.initialize()

        timer.start("Startup")

        units.initialize()
        timer.checkpoint("Units::initialize")

        game_map.initialize()
        timer.checkpoint("Map::initialize")

        path_finding.initialize_grids()
        timer.checkpoint("PathFinding::initialize")

        building_placement.initialize()
        timer.checkpoint("BuildingPlacement::initialize")

        combat_sim.initialize()
        timer.checkpoint("CombatSim::initialize")

        worker_gather_optimizer.initialize()
        timer.checkpoint("MiningOptimization::initialize")

        strategist.initialize()
        timer.checkpoint("Strategist::initialize")

        timer.stop(True)

        game = bwapi.Broodwar
        log.get(f"Initialized game against {opponent.get_name()} on {game.mapFileName()} ({game.mapHash()})")
        my_main = game_map.get_my_main()
        assert my_main is not None
        log.get(f"My starting position: {my_main.get_tile_position()}")
        my_natural = game_map.get_my_natural()
        if my_natural is not None:
            log.get(f"My natural position: {my_natural.get_tile_position()}")
        else:
            log.get("No natural position available")
        my_main_choke = game_map.get_my_main_choke()
        if my_main_choke is not None:
            log.get(f"My main choke: {TilePosition(my_main_choke.center)}")
        else:
            log.get("No main choke available")

        if self.test_on_start is not None:
            self.test_on_start()

    def onEnd(self, isWinner: bool) -> None:
        if self.test_on_end is not None:
            self.test_on_end(isWinner)

        opponent.game_end(isWinner)
        worker_gather_optimizer.game_end()
        cherryvis.game_end()

        if self._frame_profile is not None and self._frame_profile_path:
            self._frame_profile.dump_stats(self._frame_profile_path)

    def onFrame(self) -> None:
        if self._frame_profile is None or not self._frame_profile_path:
            self._on_frame()
            return

        self._frame_profile.enable()
        try:
            self._on_frame()
        finally:
            self._frame_profile.disable()
        if common.current_frame % 1000 == 0:
            self._frame_profile.dump_stats(self._frame_profile_path)

    def _on_frame(self) -> None:
        if common.current_frame < self.frame_skip:
            common.current_frame += 1
            return
        if self._game_finished:
            return

        game = bwapi.Broodwar
        if game.isPaused():
            return
        if game.isReplay():
            return

        if FRAME_LIMIT is not None and self.enable_frame_limit and common.current_frame > FRAME_LIMIT:
            log.get("Frame limit reached; leaving game")
            self._game_finished = True
            game.leaveGame()
            return

        timer.start("Frame")

        # Before doing anything else, check if the opponent has left or been eliminated
        events = game.getEvents()
        for event in events:
            if (event.getType() == EventType.PlayerLeft and event.getPlayer() == game.enemy()
                    and common.current_frame > 100):
                log.get("Opponent has left the game")
                self._game_finished = True
                return

        # First priority is to update unit-related things, as most of our other stuff relies on our unit abstraction
        # being updated

        # We start with bullets though as they interact directly with units
        bullets.update()
        timer.checkpoint("Bullets::update")

        units.update()
        timer.checkpoint("Units::update")

        bullets.update_bunkers()
        timer.checkpoint("Bullets::updateBunkers")

        # We handle events explicitly instead of through the event handlers so we can time them
        for event in events:
            event_type = event.getType()
            if event_type == EventType.UnitDiscover:
                unit = event.getUnit()
                assert unit is not None
                _handle_unit_discover(unit)
            elif event_type == EventType.UnitDestroy:
                unit = event.getUnit()
                assert unit is not None
                _handle_unit_destroy(unit)
            elif event_type == EventType.ReceiveText:
                log.get(f"Received text: {event.getText()}")
        timer.checkpoint("Events")

        # Update general information things
        opponent.update()
        timer.checkpoint("Opponent::update")

        players.update()
        timer.checkpoint("Players::update")

        game_map.update()
        timer.checkpoint("Map::update")

        general.update_clusters()
        timer.checkpoint("General::updateClusters")

        building_placement.update()
        timer.checkpoint("BuildingPlacement::update")

        builder.update()
        timer.checkpoint("Builder::update")

        workers.update_assignments()
        timer.checkpoint("Workers::updateAssignments")

        # Strategist is what makes all of the big decisions
        strategist.update()
        timer.checkpoint("Strategist::update")

        # Hook our test infrastructure in here in case we want to have our units do different stuff
        if self.test_on_frame is not None:
            self.test_on_frame()

        # Update stuff that issues orders
        general.issue_orders()
        timer.checkpoint("General::issueOrders")

        producer.update()
        timer.checkpoint("Producer::update")

        builder.issue_orders()
        timer.checkpoint("Builder::issueOrders")

        # Called after the above to allow workers to be reassigned for combat, scouting, and building first
        workers.issue_orders()
        timer.checkpoint("Workers::issueOrders")

        # Must be last, as this is what executes move orders queued earlier
        units.issue_orders()
        timer.checkpoint("Units::issueOrders")

        # Updates the mining optimization data
        worker_gather_optimizer.update()
        timer.checkpoint("MiningOptimization::update")

        # Surrender logic
        if self.enable_surrender:
            self._surrender_if_lost()

        # Instrumentation
        no_go_areas.write_instrumentation()
        general.write_instrumentation()

        _dump_heatmaps()
        cherryvis.frame_end()
        timer.checkpoint("Instrumentation")

        timer.stop()

        common.current_frame += 1

    def _surrender_if_lost(self) -> None:
        # Surrender if the following is true:
        # - We have no workers left and no money / depot to create a new one
        # - We have no mobile combat units
        # - There is at least one enemy combat unit in our main base
        game = bwapi.Broodwar
        if not (units.count_all(UnitTypes.Protoss_Probe) == 0
                and (units.count_all(UnitTypes.Protoss_Nexus) == 0 or game.self().minerals() < 50)):
            return

        for unit in units.all_mine():
            if not unit.can_attack_ground():
                continue
            if not unit.type.canMove():
                continue
            return  # We have a mobile combat unit

        enemy_in_base = False
        for base in game_map.get_my_bases():
            if enemy_in_base:
                break
            for enemy_unit in units.enemy_at_base(base):
                if not enemy_unit.can_attack_ground():
                    continue
                if enemy_unit.type.isWorker():
                    continue
                enemy_in_base = True
                break

        if enemy_in_base:
            log.get("Surrendering")
            game.sendText("gg")
            self._game_finished = True
            game.leaveGame()

    # The remaining callbacks are not used: events are handled in onFrame

    def onSendText(self, text: str) -> None:
        pass

    def onReceiveText(self, player: bwapi.Player, text: str) -> None:
        pass

    def onPlayerLeft(self, player: bwapi.Player) -> None:
        pass

    def onNukeDetect(self, target: Position) -> None:
        pass

    def onUnitDiscover(self, unit: bwapi.Unit) -> None:
        pass

    def onUnitEvade(self, unit: bwapi.Unit) -> None:
        pass

    def onUnitShow(self, unit: bwapi.Unit) -> None:
        pass

    def onUnitHide(self, unit: bwapi.Unit) -> None:
        pass

    def onUnitCreate(self, unit: bwapi.Unit) -> None:
        pass

    def onUnitDestroy(self, unit: bwapi.Unit) -> None:
        pass

    def onUnitMorph(self, unit: bwapi.Unit) -> None:
        pass

    def onUnitRenegade(self, unit: bwapi.Unit) -> None:
        pass

    def onSaveGame(self, gameName: str) -> None:
        pass

    def onUnitComplete(self, unit: bwapi.Unit) -> None:
        pass

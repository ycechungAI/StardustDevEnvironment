"""A demo bot, selected with STARDUST_BOT=stardust.bot:StardustBot (Stardust itself is stardust_ai_module).

Each BWAPI callback (onStart, onFrame, onUnitCreate, ...) is a method of the same name; callbacks the bot doesn't
define are skipped by the host.

This is a direct port of the dev environment's C++ DemoAIModule (itself based on BWAPI's ExampleAIModule): workers
mine, depots train workers, and supply is built when blocked.
"""

import math

import bwapi
from bwapi import Colors, Errors, Position, Text, Unit, WalkPosition
from instrumentation import CherryVis, Log


class StardustBot:
    def __init__(self) -> None:
        # Units registered with CherryVis (it must see each unit once to link it to the replay)
        self._cherryvis_registered: set[int] = set()
        self._last_supply_check_frame = 0

    def onStart(self) -> None:
        game = bwapi.Broodwar
        Log.initialize()
        CherryVis.initialize()

        enemy = game.enemy()
        Log.write(f"Started game on {game.mapName()} against {enemy.getName() if enemy else 'nobody'}")

        # Demo of a CherryVis heatmap: buildable tiles
        width, height = game.mapWidth(), game.mapHeight()
        buildable = [int(game.isBuildable(x, y)) for y in range(height) for x in range(width)]
        CherryVis.addHeatmap("Buildable", buildable, width, height)

    def onEnd(self, isWinner: bool) -> None:
        # Writes all of the CherryVis data files
        CherryVis.gameEnd()

    def onFrame(self) -> None:
        game = bwapi.Broodwar
        me = game.self()
        if game.isReplay() or game.isPaused() or me is None:
            return

        frame = game.getFrameCount()
        for unit in me.getUnits():
            if not unit.exists():
                continue

            if unit.isCompleted() and unit.getID() not in self._cherryvis_registered:
                CherryVis.unitFirstSeen(unit)
                self._cherryvis_registered.add(unit.getID())

            CherryVis.log(self._describe(unit, frame), unit.getID())

            # Skip units that can't act right now
            if unit.isLockedDown() or unit.isMaelstrommed() or unit.isStasised():
                continue
            if unit.isLoaded() or not unit.isPowered() or unit.isStuck():
                continue
            if not unit.isCompleted() or unit.isConstructing():
                continue

            if unit.getType().isWorker():
                self._handle_worker(unit)
            elif unit.getType().isResourceDepot():
                self._handle_depot(unit)

        CherryVis.frameEnd(frame)

    def _handle_worker(self, worker: Unit) -> None:
        if not worker.isIdle():
            return

        if worker.isCarryingGas() or worker.isCarryingMinerals():
            worker.returnCargo()
        elif not worker.getPowerUp():  # A worker carrying a powerup (e.g. a flag) can't harvest
            resource = worker.getClosestUnit(lambda u: u.getType().isMineralField() or u.getType().isRefinery())
            if resource and not worker.gather(resource):
                bwapi.Broodwar.printf(str(bwapi.Broodwar.getLastError()))

    def _handle_depot(self, depot: Unit) -> None:
        game = bwapi.Broodwar
        if not depot.isIdle() or depot.train(depot.getType().getRace().getWorker()):
            return

        # Training failed: show the error at the depot for a few frames
        position = depot.getPosition()
        error = game.getLastError()
        game.registerEvent(lambda g: g.drawTextMap(position, Text.White + str(error)), None, game.getLatencyFrames())

        supply_type = depot.getType().getRace().getSupplyProvider()
        me = depot.getPlayer()
        if (error != Errors.Insufficient_Supply
                or self._last_supply_check_frame + 400 >= game.getFrameCount()
                or me.incompleteUnitCount(supply_type) > 0):
            return
        self._last_supply_check_frame = game.getFrameCount()

        builder_type = supply_type.whatBuilds()[0]
        builder = depot.getClosestUnit(
            lambda u: u.getType() == builder_type and (u.isIdle() or u.isGatheringMinerals()) and u.getPlayer() == me)
        if not builder:
            return

        if not supply_type.isBuilding():
            builder.train(supply_type)  # Overlords
            return

        location = game.getBuildLocation(supply_type, builder.getTilePosition())
        if not location:
            return
        game.registerEvent(
            lambda g: g.drawBoxMap(Position(location), Position(location + supply_type.tileSize()), Colors.Blue),
            None,
            supply_type.buildTime() + 100)
        builder.build(supply_type, location)

    @staticmethod
    def _describe(unit: Unit, frame: int) -> str:
        """Per-frame CherryVis log line: last command, current order, movement."""
        command = unit.getLastCommand()
        text = f"cmd={command.getType()};f={frame - unit.getLastCommandFrame()}"
        target = command.getTarget()
        if target:
            text += (f";tgt={target.getType()}#{target.getID()}@{WalkPosition(target.getPosition())}"
                     f";d={target.getDistance(unit)}")
        elif command.getTargetPosition():
            text += f";tgt={WalkPosition(command.getTargetPosition())}"

        text += f"\nord={unit.getOrder()};t={unit.getOrderTimer()}"
        order_target = unit.getOrderTarget()
        if order_target:
            text += (f";tgt={order_target.getType()}#{order_target.getID()}@{WalkPosition(order_target.getPosition())}"
                     f";d={order_target.getDistance(unit)}")
        elif unit.getOrderTargetPosition():
            text += f";tgt={WalkPosition(unit.getOrderTargetPosition())}"

        text += "\n"
        top_speed = unit.getType().topSpeed()
        if top_speed > 0.001:
            speed = math.hypot(unit.getVelocityX(), unit.getVelocityY())
            text += f"spd={int(100.0 * speed / top_speed)};mvng={int(unit.isMoving())};stk={int(unit.isStuck())}"
        return text

    def onSendText(self, text: str) -> None:
        bwapi.Broodwar.sendText(text)

    def onReceiveText(self, player: bwapi.Player, text: str) -> None:
        bwapi.Broodwar.printf(f'{player.getName()} said "{text}"')

    def onPlayerLeft(self, player: bwapi.Player) -> None:
        bwapi.Broodwar.sendText(f"Goodbye {player.getName()}!")

    def onNukeDetect(self, target: Position) -> None:
        if target:
            bwapi.Broodwar.printf(f"Nuclear Launch Detected at {target}")
        else:
            bwapi.Broodwar.sendText("Where's the nuke?")

    def onUnitCreate(self, unit: Unit) -> None:
        if unit.getPlayer() == bwapi.Broodwar.self():
            Log.write(f"Unit created: {unit.getType()}")

    def onUnitDestroy(self, unit: Unit) -> None:
        if unit.getPlayer() == bwapi.Broodwar.self():
            Log.write(f"Unit lost: {unit.getType()}")

    def onUnitMorph(self, unit: Unit) -> None:
        game = bwapi.Broodwar
        # In replays, print the build order of the structures
        if game.isReplay() and unit.getType().isBuilding() and not unit.getPlayer().isNeutral():
            minutes, seconds = divmod(game.getFrameCount() // 24, 60)
            game.sendText(f"{minutes:02d}:{seconds:02d}: {unit.getPlayer().getName()} morphs a {unit.getType()}")

    def onSaveGame(self, gameName: str) -> None:
        bwapi.Broodwar.printf(f'The game was saved to "{gameName}"')

"""Port of Builder/ForgeGatewayWall.h: a Forge/Gateway wall at the natural."""

from __future__ import annotations

import bwapi
from bwapi import Position, Positions, TilePosition, TilePositions
from stardust.cpp import INT_MAX
from stardust.instrumentation import log


class ForgeGatewayWall:
    def __init__(self, forge: TilePosition = TilePositions.Invalid, gateway: TilePosition = TilePositions.Invalid,
                 pylon: TilePosition = TilePositions.Invalid, gap_size: int = INT_MAX,
                 gap_center: Position = Positions.Invalid, gap_end1: Position = Positions.Invalid,
                 gap_end2: Position = Positions.Invalid) -> None:
        self.forge = forge
        self.gateway = gateway
        self.pylon = pylon
        self.cannons: list[TilePosition] = []
        self.natural_cannons: list[TilePosition] = []

        self.gap_size = gap_size
        self.gap_center = gap_center
        self.gap_end1 = gap_end1
        self.gap_end2 = gap_end2

        self.probe_blocking_positions: set[Position] = set()

        self.tiles_inside_wall: set[TilePosition] = set()
        self.tiles_outside_wall: set[TilePosition] = set()
        self.tiles_outside_but_close_to_wall: set[TilePosition] = set()

    def is_valid(self) -> bool:
        return self.pylon.isValid() and self.forge.isValid() and self.gateway.isValid()

    def __str__(self) -> str:
        if not self.is_valid():
            return "invalid"
        return (f"pylon@{self.pylon};forge@{self.forge};gate@{self.gateway}cannons@"
                + "".join(str(tile) for tile in self.cannons)
                + ";natCannons@" + "".join(str(tile) for tile in self.natural_cannons)
                + f";gapSize={self.gap_size}")

    def add_to_heatmap(self, wall_heatmap: list[int]) -> None:
        """Values: gateway and forge 10, pylon 40, cannon 20, natural cannon 25, tiles inside the wall +2, tiles
        outside the wall -2, tiles outside but close to the wall -1."""
        game = bwapi.Broodwar
        map_width = game.mapWidth()
        map_height = game.mapHeight()

        def add_location(tile: TilePosition, width: int, height: int, value: int) -> None:
            for y in range(tile.y, tile.y + height):
                if y > map_height - 1:
                    log.get(f"ERROR: WALL LOCATION OUT OF BOUNDS @ {tile}")
                    continue
                for x in range(tile.x, tile.x + width):
                    if x > map_width - 1:
                        log.get(f"ERROR: WALL LOCATION OUT OF BOUNDS @ {tile}")
                        continue
                    wall_heatmap[x + y * map_width] = value

        add_location(self.gateway, 4, 3, 10)
        add_location(self.forge, 3, 2, 10)
        add_location(self.pylon, 2, 2, 40)
        for cannon in self.cannons:
            add_location(cannon, 2, 2, 20)
        for cannon in self.natural_cannons:
            add_location(cannon, 2, 2, 25)

        for tile in self.tiles_inside_wall:
            wall_heatmap[tile.x + tile.y * map_width] += 2
        for tile in self.tiles_outside_wall:
            wall_heatmap[tile.x + tile.y * map_width] -= 2
        for tile in self.tiles_outside_but_close_to_wall:
            wall_heatmap[tile.x + tile.y * map_width] -= 1

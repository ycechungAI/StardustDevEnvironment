"""Port of Map/MapSpecificOverride.h: hooks for maps that need special handling (mineral walking, backdoor
naturals, hard-coded walls, ...). Implementations are in map_specific_overrides/."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
import bwem
from bwapi import Position, TilePosition, UnitType

if TYPE_CHECKING:
    from stardust.builder.forge_gateway_wall import ForgeGatewayWall
    from stardust.general.unit_cluster.unit_cluster import UnitCluster
    from stardust.map.base import Base
    from stardust.map.choke import Choke
    from stardust.map.starting_location import StartingLocation
    from stardust.strategist.strategy_engine import StrategyEngine


class MapSpecificOverride:
    def has_mineral_walking(self) -> bool:
        return False

    def has_attack_clearable_chokes(self) -> bool:
        return False

    def initialize_chokes(self, chokes: dict[bwem.ChokePoint, Choke]) -> None:
        pass

    def can_use_bwem_path(self, unit_type: UnitType) -> bool:
        """Whether the given unit type can be pathed by BWEM on this map."""
        # Assumes mineral walking chokes are marked as blocked by BWEM, so workers need special pathing but other
        # units behave correctly
        return not self.has_mineral_walking() or not unit_type.isWorker()

    def on_unit_destroy(self, unit: bwapi.Unit) -> None:
        pass

    def cluster_move(self, cluster: UnitCluster, target_position: Position) -> bool:
        """Hook to perform special pathing for clusters moving. Returns True if the override has handled the move."""
        return False

    def modify_main_base_building_placement_areas(self, areas: set[bwem.Area]) -> None:
        pass

    def create_strategy_engine(self) -> StrategyEngine | None:
        return None

    def allow_diagonal_pathing_through(self, x: int, y: int) -> bool:
        return False

    def enemy_starting_main_determined(self) -> None:
        """Hook to do special processing when we know the enemy's starting main base."""

    def has_backdoor_natural(self) -> bool:
        """Whether the natural base is behind the main base relative to the enemy starting position."""
        return False

    def add_island_areas(self, island_areas: set[bwem.Area]) -> None:
        """Adds island areas that aren't marked as such by BWEM."""

    def get_wall(self, start_tile: TilePosition) -> ForgeGatewayWall | None:
        """A hard-coded wall for a start position, for cases where automatic wall generation doesn't work."""
        return None

    def modify_bases(self, bases: list[Base]) -> None:
        """Map-specific changes to the list of bases available on this map."""

    def modify_starting_location(self, starting_location: StartingLocation) -> StartingLocation:
        """Map-specific changes to starting location data, like its natural and chokes. Returns the (possibly
        replaced) starting location."""
        return starting_location

    def natural_for_wall_placement(self, main: Base) -> Base | None:
        return None

    def starting_worker_positions(self, start_position: TilePosition) -> list[Position]:
        pos = Position(start_position)
        return [pos + Position(16, 104), pos + Position(40, 104), pos + Position(64, 104), pos + Position(88, 104)]

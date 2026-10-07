"""FAP combat simulation, as modified and driven by Stardust."""

from bwapi import Position, UnitType

def set_unit_scores(base: list[int], scaled: list[int]) -> None:
    """Per-unit-type score tables (indexed by UnitType id) used to value units."""

class ChokeGeometry:
    def __init__(
        self,
        tile_side: list[int],
        end1_center: Position,
        end2_center: Position,
        end1_exit: Position,
        end2_exit: Position,
    ) -> None:
        """tile_side has one entry per half-tile cell: map_width * map_height * 4."""

class CombatSimulator:
    def __init__(self, map_width: int, map_height: int) -> None: ...
    def reset(self, choke: ChokeGeometry | None = None) -> None:
        """Clear all units; simulate through the given choke if not None."""
    def add_unit(
        self,
        player1: bool,
        unit_type: UnitType,
        position: Position,
        target_position: Position,
        health: int,
        shields: int,
        flying: bool,
        speed: float,
        armor: int,
        ground_cooldown: int,
        ground_damage: int,
        ground_max_range: int,
        air_cooldown: int,
        air_damage: int,
        air_max_range: int,
        elevation: int,
        attacker_count: int,
        attack_cooldown_remaining: int,
        stimmed: bool,
        undetected: bool,
        id: int,
        target: int,
        collision_value: int,
        collision_value_choke: int,
    ) -> None: ...
    def run(self, max_iterations: int, time_limit_us: int) -> tuple[int, int, int, int, int]:
        """Simulate; returns (initial p1 score, initial p2 score, final p1 score, final p2 score, iterations)."""
    def state(
        self,
    ) -> tuple[
        list[tuple[int, UnitType, int, int, int, int, int, int]],
        list[tuple[int, UnitType, int, int, int, int, int, int]],
    ]:
        """(p1 units, p2 units), each a list of (id, unit_type, x, y, health, shields, cooldown, target)."""

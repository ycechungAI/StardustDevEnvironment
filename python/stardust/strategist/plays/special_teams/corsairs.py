"""Port of Strategist/Plays/SpecialTeams/Corsairs.{h,cpp}: all of our corsairs, controlled by a CorsairSquad.

Stardust's disruption web research here is commented out."""

from __future__ import annotations

from bwapi import UnitTypes
from stardust.general import general
from stardust.general.squads.corsair_squad import CorsairSquad
from stardust.map import game_map
from stardust.strategist.play import Play, PlayUnitRequirement, ProductionGoals


class Corsairs(Play):
    def __init__(self) -> None:
        super().__init__("Corsairs")
        self._squad = CorsairSquad()
        general.add_squad(self._squad)

    def get_squad(self) -> CorsairSquad:
        return self._squad

    def update(self) -> None:
        # Always request corsairs so all created corsairs get assigned to this play
        my_main = game_map.get_my_main()
        assert my_main is not None
        self.status.unit_requirements.append(PlayUnitRequirement(10, UnitTypes.Protoss_Corsair,
                                                                 my_main.get_position()))

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        pass

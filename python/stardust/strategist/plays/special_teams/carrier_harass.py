"""Port of Strategist/Plays/SpecialTeams/CarrierHarass.{h,cpp}: harasses enemy expansions with a group of carriers."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from bwapi import UnitTypes, UpgradeTypes
from stardust.cpp import INT_MIN
from stardust.general import general
from stardust.general.squads.attack_base_squad import AttackBaseSquad
from stardust.map import game_map
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.producer.production_goals.upgrade_production_goal import UpgradeProductionGoal
from stardust.strategist.play import PRIORITY_NORMAL, PRIORITY_SPECIALTEAMS, Play, PlayUnitRequirement, \
    ProductionGoals, add_goal
from stardust.units import units
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType

if TYPE_CHECKING:
    from stardust.map.base import Base


def _get_target_base(current: Base | None) -> Base | None:
    enemy = bwapi.Broodwar.enemy()

    # Only switch bases when the current target is dead
    if current is not None and current.owner == enemy:
        return current

    # By default target the newest expansion beyond the enemy main/natural
    enemy_starting_main = game_map.get_enemy_starting_main()
    enemy_starting_natural = game_map.get_enemy_starting_natural()
    best: Base | None = None
    oldest = INT_MIN
    for base in game_map.get_enemy_bases():
        if base is enemy_starting_main or base is enemy_starting_natural:
            continue
        if base.owned_since > oldest:
            best = base
            oldest = base.owned_since

    # Otherwise take the main, then the natural
    for fallback in (enemy_starting_main, enemy_starting_natural):
        if best is None and fallback is not None and fallback.owner == enemy:
            best = fallback

    return best


class CarrierHarass(Play):
    def __init__(self) -> None:
        super().__init__("CarrierHarass")
        self._target_base: Base | None = None
        self._squad: AttackBaseSquad | None = None
        self._attacking = False

    def get_squad(self) -> AttackBaseSquad | None:
        return self._squad

    def update(self) -> None:
        # Set the target base
        new_target_base = _get_target_base(self._target_base) if self._attacking else game_map.get_my_main()
        if self._attacking and new_target_base is None:
            self._attacking = False
            new_target_base = game_map.get_my_main()
        if new_target_base is not self._target_base:
            # Remove the current squad
            if self._squad is not None:
                self.status.removed_units = self._squad.get_units()
                general.remove_squad(self._squad)
                self._squad = None

            # Add the new squad
            if new_target_base is not None:
                self._squad = AttackBaseSquad(new_target_base, "Carriers")
                general.add_squad(self._squad)
                for unit in self.status.removed_units:
                    self._squad.add_unit(unit)
                self.status.removed_units.clear()

            self._target_base = new_target_base

        # When we don't have a target base, the play stays but doesn't do anything
        target_base = self._target_base
        squad = self._squad
        if target_base is None or squad is None:
            return

        # Update detection - release observers when no longer needed, request observers when needed
        need_detection = squad.needs_detection()
        detectors = squad.get_detectors()
        if not need_detection and detectors:
            self.status.removed_units.extend(detectors)
        elif need_detection and not detectors:
            self.status.unit_requirements.append(
                PlayUnitRequirement(1, UnitTypes.Protoss_Observer, squad.get_target_position()))

            # Release the squad units until we get the detector
            self.status.removed_units = squad.get_units()
            return

        carrier_count = squad.combat_unit_count()
        if carrier_count < 4:
            self.status.unit_requirements.append(
                PlayUnitRequirement(4 - carrier_count, UnitTypes.Protoss_Carrier, target_base.get_position()))

        total_interceptors = sum(unit.bwapi_unit.getInterceptorCount() for unit in squad.get_units()
                                 if unit.type == UnitTypes.Protoss_Carrier and unit.bwapi_unit is not None)

        # Switch to attacking when we have enough carriers and interceptors
        if (not self._attacking
                and (carrier_count == 4 or (carrier_count == 3 and bwapi.Broodwar.self().supplyUsed() > 388))
                and total_interceptors // carrier_count >= 6):
            self._attacking = True

        # Switch to not attacking if we don't have enough carriers or interceptors
        if self._attacking and (carrier_count < 2 or total_interceptors // carrier_count < 2):
            self._attacking = False

    def add_prioritized_production_goals(self, prioritized_production_goals: ProductionGoals) -> None:
        for unit_requirement in self.status.unit_requirements:
            if unit_requirement.count < 1:
                continue

            add_goal(prioritized_production_goals, PRIORITY_SPECIALTEAMS,
                     UnitProductionGoal(self.label, unit_requirement.type, unit_requirement.count,
                                        2 if unit_requirement.type == UnitTypes.Protoss_Carrier else 1))

        weapons = UpgradeOrTechType.of(UpgradeTypes.Protoss_Air_Weapons)
        weapon_level = bwapi.Broodwar.self().getUpgradeLevel(UpgradeTypes.Protoss_Air_Weapons)
        if weapon_level < 3 and not units.is_being_upgraded_or_researched(weapons):
            add_goal(prioritized_production_goals, PRIORITY_NORMAL,
                     UpgradeProductionGoal(self.label, weapons, weapon_level + 1, 1))

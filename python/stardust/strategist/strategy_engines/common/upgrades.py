"""Port of Strategist/StrategyEngines/Common/Upgrades.cpp: upgrade helpers shared by the strategy engines."""

from __future__ import annotations

import bwapi
from bwapi import UnitType, UnitTypes, UpgradeTypes
from stardust.producer.production_goal import ProductionGoal
from stardust.producer.production_goals.unit_production_goal import UnitProductionGoal
from stardust.producer.production_goals.upgrade_production_goal import UpgradeProductionGoal
from stardust.strategist.play import PRIORITY_HIGHPRIORITYUPGRADES, PRIORITY_MAINARMYBASEPRODUCTION, \
    PRIORITY_NORMAL, ProductionGoals, add_goal
from stardust.units import units
from stardust.util.upgrade_or_tech_type import UpgradeOrTechType


def upgrade_at_count(prioritized_production_goals: ProductionGoals, upgrade_or_tech_type: UpgradeOrTechType,
                     unit_type: UnitType, unit_count: int, level: int = 1) -> None:
    """Queues the upgrade in the production goals at the point where we will have the given number of units."""
    # First bail out if the upgrade is already done or queued
    if upgrade_or_tech_type.current_level() >= level:
        return
    if units.is_being_upgraded_or_researched(upgrade_or_tech_type):
        return

    # Now loop through all of the prioritized production goals, keeping track of how many of the desired unit we have
    unit_total = units.count_completed(unit_type) + units.count_incomplete(unit_type)
    for priority in sorted(prioritized_production_goals):
        goals = prioritized_production_goals[priority]
        for index, goal in enumerate(goals):
            # Bail out if this upgrade is already queued
            if isinstance(goal, UpgradeProductionGoal):
                if goal.upgrade_type() == upgrade_or_tech_type:
                    return
                continue

            # If we are producing an unlimited number of a different unit type first, or at emergency priority, bail
            # out
            if goal.count_to_produce() == -1 and (goal.unit_type() != unit_type
                                                  or priority < PRIORITY_MAINARMYBASEPRODUCTION):
                return

            # Skip other unit types
            if goal.unit_type() != unit_type:
                continue

            upgrade_goal = UpgradeProductionGoal("SE", upgrade_or_tech_type)

            # If we already have enough units, insert the upgrade now
            if unit_total >= unit_count:
                goals.insert(index, upgrade_goal)
                return

            # If producing an unlimited number, split and insert here: the remaining units, then the upgrade
            if goal.count_to_produce() == -1:
                goals.insert(index, upgrade_goal)
                goals.insert(index, UnitProductionGoal(goal.requester, unit_type, unit_count - unit_total,
                                                       goal.get_producer_limit(), goal.get_location()))
                return

            # If the unit count is reached, split and insert here
            if unit_total + goal.count_to_produce() >= unit_count:
                remaining_count = unit_total + goal.count_to_produce() - unit_count
                replacement: list[ProductionGoal] = [
                    UnitProductionGoal(goal.requester, unit_type, unit_count - unit_total, goal.get_producer_limit(),
                                       goal.get_location()),
                    upgrade_goal,
                ]

                # Add the remainder after the upgrade
                if remaining_count > 0:
                    replacement.append(UnitProductionGoal(goal.requester, unit_type, remaining_count,
                                                          goal.get_producer_limit(), goal.get_location()))
                goals[index:index + 1] = replacement
                return

            # Otherwise add the units and continue
            unit_total += goal.count_to_produce()


def upgrade_when_unit_created(prioritized_production_goals: ProductionGoals, upgrade_or_tech_type: UpgradeOrTechType,
                              unit_type: UnitType, require_completed_unit: bool = False,
                              require_producer: bool = False, priority: int = PRIORITY_NORMAL) -> None:
    # First bail out if the upgrade is already done or queued
    if upgrade_or_tech_type.current_level() >= upgrade_or_tech_type.max_level():
        return
    if units.is_being_upgraded_or_researched(upgrade_or_tech_type):
        return

    # Now check if we have at least one of the unit
    if units.count_completed(unit_type) == 0 and (require_completed_unit or units.count_incomplete(unit_type) == 0):
        return

    # Now check if we have the required producer, if specified
    producer = upgrade_or_tech_type.what_upgrades_or_researches()
    if require_producer and units.count_incomplete(producer) == 0 and units.count_completed(producer) == 0:
        return

    add_goal(prioritized_production_goals, priority, UpgradeProductionGoal("SE", upgrade_or_tech_type))


def default_ground_upgrades(prioritized_production_goals: ProductionGoals) -> None:
    me = bwapi.Broodwar.self()

    # Start when we have completed our second nexus and have an army
    if (units.count_completed(UnitTypes.Protoss_Nexus) < 2
            or (units.count_completed(UnitTypes.Protoss_Zealot)
                + units.count_completed(UnitTypes.Protoss_Dragoon)) <= 10):
        return

    weapons = UpgradeOrTechType.of(UpgradeTypes.Protoss_Ground_Weapons)
    armor = UpgradeOrTechType.of(UpgradeTypes.Protoss_Ground_Armor)
    weapon_level = me.getUpgradeLevel(UpgradeTypes.Protoss_Ground_Weapons)
    armor_level = me.getUpgradeLevel(UpgradeTypes.Protoss_Ground_Armor)

    # Upgrade past 1 and potentially on two forges when we have completed our third nexus
    # (Stardust leaves the forge count uninitialized with three nexuses before armor is started; 1 is used here.)
    if units.count_completed(UnitTypes.Protoss_Nexus) >= 3:
        max_level = 3
        forge_count = 1
        if armor_level >= 1 or units.is_being_upgraded_or_researched(armor):
            forge_count = 2
    else:
        max_level = 1
        forge_count = 1

    # Weapons -> 1, Armor -> 1, Weapons -> 3, Armor -> 3
    if ((weapon_level == 0 or armor_level >= 1) and weapon_level < max_level
            and not units.is_being_upgraded_or_researched(weapons)):
        add_goal(prioritized_production_goals, PRIORITY_HIGHPRIORITYUPGRADES,
                 UpgradeProductionGoal("SE", weapons, weapon_level + 1, forge_count))
    if not units.is_being_upgraded_or_researched(armor) and armor_level < max_level:
        add_goal(prioritized_production_goals, PRIORITY_HIGHPRIORITYUPGRADES,
                 UpgradeProductionGoal("SE", armor, armor_level + 1, forge_count))
    if (weapon_level > 0 and armor_level == 0 and weapon_level < max_level
            and not units.is_being_upgraded_or_researched(weapons)):
        add_goal(prioritized_production_goals, PRIORITY_HIGHPRIORITYUPGRADES,
                 UpgradeProductionGoal("SE", weapons, weapon_level + 1, forge_count))

    # Upgrade shields when we are maxed
    if me.supplyUsed() > 300 and me.minerals() > 2000 and me.gas() > 1500:
        add_goal(prioritized_production_goals, PRIORITY_NORMAL,
                 UpgradeProductionGoal("SE", UpgradeOrTechType.of(UpgradeTypes.Protoss_Plasma_Shields),
                                       me.getUpgradeLevel(UpgradeTypes.Protoss_Plasma_Shields) + 1, forge_count))


def upgrade(prioritized_production_goals: ProductionGoals, upgrade_or_tech_type: UpgradeOrTechType, level: int = 1,
            priority: int = PRIORITY_NORMAL) -> None:
    # First bail out if the upgrade is already done or queued
    if upgrade_or_tech_type.current_level() >= level:
        return
    if units.is_being_upgraded_or_researched(upgrade_or_tech_type):
        return

    # Queue the upgrade
    add_goal(prioritized_production_goals, priority, UpgradeProductionGoal("SE", upgrade_or_tech_type, level))

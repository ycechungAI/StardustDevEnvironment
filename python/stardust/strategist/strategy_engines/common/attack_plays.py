"""Port of Strategist/StrategyEngines/Common/AttackPlays.cpp: chooses what our main army attacks, and attacks weakly
defended expansions with separate plays."""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

import bwapi
from bwapi import UnitTypes, WalkPosition
from stardust import common
from stardust.builder import building_placement
from stardust.cpp import INT_MAX
from stardust.general.unit_cluster import combat_sim
from stardust.general.unit_cluster.unit_cluster import Activity
from stardust.instrumentation import cherryvis
from stardust.map import game_map
from stardust.strategist.play import Play
from stardust.strategist.plays.main_army.attack_enemy_base import AttackEnemyBase
from stardust.strategist.plays.main_army.defend_my_main import DefendMyMain
from stardust.strategist.plays.main_army.forge_fast_expand import ForgeFastExpand
from stardust.strategist.plays.main_army.main_army_play import MainArmyPlay
from stardust.strategist.plays.main_army.mop_up import MopUp
from stardust.strategist.plays.offensive.attack_expansion import AttackExpansion
from stardust.strategist.plays.offensive.attack_island_expansion import AttackIslandExpansion
from stardust.strategist.strategy_engine import before_play_index, get_main_army_play
from stardust.units import units
from stardust.util import unit_util

if TYPE_CHECKING:
    from stardust.map.base import Base


def _set_main_play(current: MainArmyPlay, play_type: type[MainArmyPlay], *args: Any) -> None:
    if type(current) is play_type:
        return
    current.status.transition_to = play_type(*args)


def update_attack_plays(plays: list[Play], defend_our_main: bool) -> None:
    main_army_play = get_main_army_play(plays)
    assert main_army_play is not None
    enemy = bwapi.Broodwar.enemy()

    # If we want to defend our main, cancel all attack plays and transition the main army
    if defend_our_main:
        if main_army_play.is_defensive():
            return

        for play in plays:
            if isinstance(play, AttackExpansion):
                play.status.complete = True

        # If we have built a wall, defend it
        # The ForgeFastExpand play will transition itself to DefendMyMain if it determines the wall is indefensible
        if building_placement.has_forge_gateway_wall():
            wall = building_placement.get_forge_gateway_wall()
            forge = units.my_building_at(wall.forge)
            if (forge is not None and forge.type == UnitTypes.Protoss_Forge and forge.exists()
                    and forge.bwapi_unit is not None and forge.bwapi_unit.isPowered()):
                _set_main_play(main_army_play, ForgeFastExpand)
                return

        _set_main_play(main_army_play, DefendMyMain)
        return

    enemy_starting_main = game_map.get_enemy_starting_main()
    enemy_starting_natural = game_map.get_enemy_starting_natural()
    my_natural = game_map.get_my_natural()
    attack_enemy_base_play = main_army_play if isinstance(main_army_play, AttackEnemyBase) else None

    def get_current_main_army_target() -> Base | None:
        """The current main army target base, if it is locked-in, to avoid indecision."""
        # Must be an attack base play against an enemy base
        if attack_enemy_base_play is None:
            return None
        if attack_enemy_base_play.base.owner != enemy:
            return None

        # Must have a vanguard cluster
        vanguard = attack_enemy_base_play.get_squad().vanguard_cluster()
        if vanguard is None:
            return None

        # If the army is attacking the enemy main but has just discovered the enemy natural, switch to it
        if (attack_enemy_base_play.base is enemy_starting_main and enemy_starting_natural is not None
                and enemy_starting_natural.owner == enemy):
            assert enemy_starting_main is not None
            # Verify the cluster is closer to the natural than the main
            if (vanguard.center.getApproxDistance(enemy_starting_natural.get_position())
                    < vanguard.center.getApproxDistance(enemy_starting_main.get_position())):
                return enemy_starting_natural

        # Stay locked in to the current base if the army is on the offensive
        if vanguard.current_activity != Activity.Regrouping:
            return attack_enemy_base_play.base

        # Stay locked in to the current base if it is an expansion and we haven't been consistently regrouping for the
        # past 5 seconds
        if (attack_enemy_base_play.base is not enemy_starting_main
                and attack_enemy_base_play.base is not enemy_starting_natural
                and vanguard.last_activity_change > common.current_frame - 120):
            return attack_enemy_base_play.base

        return None

    main_army_target = get_current_main_army_target()

    # Now analyze all of the enemy bases to determine how we want to attack them
    # Main army target is either a fortified expansion, the enemy natural, or the enemy main
    # Exception is if the enemy took our natural early as a proxy
    lowest_enemy_value = INT_MAX
    lowest_enemy_value_base: Base | None = None
    attackable_expansions_to_enemy_unit_value: dict[Base, int] = {}
    island_expansions: list[Base] = []
    dragoon_value = combat_sim.unit_value(UnitTypes.Protoss_Dragoon)
    for base in game_map.get_enemy_bases():
        # Main and natural are default targets for our main army, so don't need to be analyzed
        if base is game_map.get_enemy_main() or base is enemy_starting_natural:
            continue

        # Skip if the main army is already attacking this base
        if base is main_army_target:
            continue

        # Handle islands separately
        if base.island:
            if base not in island_expansions:
                island_expansions.append(base)
            continue

        # If we haven't seen the depot and the base is in the starting area of the main, don't attack it
        # This is to handle maps like Andromeda where the enemy will often build buildings close to the min-only that
        # are actually logically part of their main
        if (enemy_starting_main is not None and enemy_starting_main.owner == enemy and base.resource_depot is None
                and base.get_area() in game_map.get_starting_base_areas(enemy_starting_main)):
            continue

        # Gather enemy threats at the base
        enemy_value = sum(combat_sim.unit_value(unit) for unit in units.enemy_at_base(base)
                          if unit_util.is_combat_unit(unit.type))

        # Attack with a separate play if the unit value corresponds to three dragoons or less
        # Give up attacking an expansion in this way if it hasn't succeeded in 3000 frames (a bit over 2 minutes)
        if (enemy_value <= 3 * dragoon_value and base.owned_since > common.current_frame - 3000
                and base is not my_natural):
            attackable_expansions_to_enemy_unit_value[base] = enemy_value
            continue

        # Target the main army at this base if it does not already have a locked target and this base is most
        # defended
        if enemy_value < lowest_enemy_value:
            lowest_enemy_value = enemy_value
            lowest_enemy_value_base = base

    # Choose the target for the main army
    if main_army_target is None:
        main_army_target = lowest_enemy_value_base
    if main_army_target is None:
        main_army_target = (enemy_starting_natural
                            if enemy_starting_natural is not None and enemy_starting_natural.owner == enemy
                            else game_map.get_enemy_main())

    # Ensure the main army play is correctly set
    if main_army_target is not None:
        if attack_enemy_base_play is not None:
            if attack_enemy_base_play.base is not main_army_target:
                main_army_play.status.transition_to = AttackEnemyBase(main_army_target)
        else:
            _set_main_play(main_army_play, AttackEnemyBase, main_army_target)
    else:
        _set_main_play(main_army_play, MopUp)

    # Remove AttackExpansion plays that are no longer needed
    for play in plays:
        if isinstance(play, AttackIslandExpansion):
            if play.base in island_expansions:
                island_expansions.remove(play.base)
            continue

        if not isinstance(play, AttackExpansion):
            continue

        # If we no longer need to attack this base, remove the play
        if play.base not in attackable_expansions_to_enemy_unit_value:
            play.status.complete = True
            continue

        play.enemy_defense_value = attackable_expansions_to_enemy_unit_value.pop(play.base)

    # Add missing plays
    for base, enemy_value in attackable_expansions_to_enemy_unit_value.items():
        plays.insert(before_play_index(plays, MainArmyPlay), AttackExpansion(base, enemy_value))
        cherryvis.log(f"Added attack expansion play for base @ {WalkPosition(base.get_position())}")
    for island_expansion in island_expansions:
        plays.insert(before_play_index(plays, MainArmyPlay), AttackIslandExpansion(island_expansion))
        cherryvis.log(f"Added attack island expansion play for base @ {WalkPosition(island_expansion.get_position())}")

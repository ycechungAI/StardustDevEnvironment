"""Port of General/General.{h,cpp} (the General namespace): owns the squads, assigns cannons to defensive squads and
groups enemy units out on the map into armies.

The CombatSim namespace declared in General.h is in unit_cluster/combat_sim.py.
"""

from __future__ import annotations

import math
from typing import TYPE_CHECKING, Any

import bwem
from bwapi import UnitTypes, WalkPosition
from stardust import config
from stardust.builder import building_placement
from stardust.cpp import INT_MAX
from stardust.general.enemy_army import EnemyArmy
from stardust.instrumentation import cherryvis, log
from stardust.map import game_map
from stardust.units import units

if TYPE_CHECKING:
    from stardust.general.squad import Squad
    from stardust.general.squads.attack_base_squad import AttackBaseSquad
    from stardust.map.base import Base
    from stardust.units.my_unit import MyUnit
    from stardust.units.unit import Unit

_squads: dict[Squad, None] = {}  # insertion-ordered set

_base_to_attack_squad: dict[Base, AttackBaseSquad] = {}
_base_to_defend_squad: dict[Base | None, Squad] = {}
_cannon_to_squad: dict[MyUnit, Squad] = {}

_defend_wall_squad: Squad | None = None

_enemy_armies: list[EnemyArmy] = []
_enemy_unit_to_army: dict[Unit, EnemyArmy] = {}


def initialize() -> None:
    global _defend_wall_squad
    _squads.clear()
    _base_to_attack_squad.clear()
    _base_to_defend_squad.clear()
    _cannon_to_squad.clear()
    _defend_wall_squad = None
    _enemy_armies.clear()
    _enemy_unit_to_army.clear()


def update_clusters() -> None:
    # Add completed and powered cannons to an appropriate squad
    # We currently only use cannons defensively, so we don't need to ever add them to attack squads
    for cannon in list(units.all_mine_completed_of_type(UnitTypes.Protoss_Photon_Cannon)):
        # Clean up dead or unpowered cannons
        if not cannon.exists() or cannon.bwapi_unit is None or not cannon.bwapi_unit.isPowered():
            squad = _cannon_to_squad.pop(cannon, None)
            if squad is not None:
                squad.remove_unit(cannon)
            continue

        # Check if the cannon should belong to a defend wall squad
        if _defend_wall_squad is not None:
            found_cannon = False

            for cannon_placement in building_placement.get_forge_gateway_wall().cannons:
                if cannon.get_tile_position() != cannon_placement:
                    continue

                current_squad = _cannon_to_squad.get(cannon)
                if current_squad is not _defend_wall_squad:
                    if current_squad is not None:
                        current_squad.remove_unit(cannon)

                    _defend_wall_squad.add_unit(cannon)
                    _cannon_to_squad[cannon] = _defend_wall_squad

                found_cannon = True
                break

            if found_cannon:
                continue

        # Determine the base the cannon belongs to
        base: Base | None = None
        if bwem.Instance().GetArea(WalkPosition(cannon.last_position)) in game_map.get_my_main_areas():
            base = game_map.get_my_main()
        else:
            closest = INT_MAX
            for my_base in game_map.get_my_bases():
                dist = cannon.last_position.getApproxDistance(my_base.get_position())
                if dist < 320 and dist < closest:
                    closest = dist
                    base = my_base

        # Update the assignment
        base_squad = _base_to_defend_squad.get(base)
        current_squad = _cannon_to_squad.get(cannon)
        if current_squad is not None and (base_squad is None or base_squad is not current_squad):
            current_squad.remove_unit(cannon)
            del _cannon_to_squad[cannon]
            current_squad = None
        if base_squad is not None and (current_squad is None or base_squad is not current_squad):
            base_squad.add_unit(cannon)
            _cannon_to_squad[cannon] = base_squad

    for squad in list(_squads):
        squad.update_clusters()

    # Group enemy units not at any base into armies
    # This is using a fairly fast and loose clustering
    _enemy_armies.clear()
    _enemy_unit_to_army.clear()
    for enemy_unit in units.enemy_combat_units_not_at_an_enemy_base():
        if not enemy_unit.sim_position_valid:
            continue

        # Try to find an army this unit fits into, or create a new army with this unit
        if not any(army.try_add_unit(enemy_unit) for army in _enemy_armies):
            _enemy_armies.append(EnemyArmy(enemy_unit))

    for enemy_army in _enemy_armies:
        for enemy_unit in enemy_army.units:
            _enemy_unit_to_army.setdefault(enemy_unit, enemy_army)

        if config.INSTRUMENTATION_ENABLED_VERBOSE:
            ball_radius = int(math.sqrt((32 * 32 * len(enemy_army.units)) / math.pi))
            cherryvis.draw_circle(enemy_army.center.x, enemy_army.center.y, ball_radius + 16,
                                  cherryvis.DrawColor.Blue)


def issue_orders() -> None:
    for squad in list(_squads):
        squad.execute()


def add_squad(squad: Squad) -> None:
    from stardust.general.squads.attack_base_squad import AttackBaseSquad
    from stardust.general.squads.defend_base_squad import DefendBaseSquad
    from stardust.general.squads.defend_wall_squad import DefendWallSquad
    from stardust.general.squads.early_game_defend_main_base_squad import EarlyGameDefendMainBaseSquad

    global _defend_wall_squad

    _squads[squad] = None

    if isinstance(squad, AttackBaseSquad):
        _base_to_attack_squad[squad.base] = squad
    if isinstance(squad, DefendBaseSquad):
        _base_to_defend_squad[squad.base] = squad
    if isinstance(squad, EarlyGameDefendMainBaseSquad):
        _base_to_defend_squad[game_map.get_my_main()] = squad
    if isinstance(squad, DefendWallSquad):
        _defend_wall_squad = squad


def remove_squad(squad: Squad) -> None:
    global _defend_wall_squad

    squad.disband()
    _squads.pop(squad, None)

    # (Stardust's cleanup of the base and cannon maps works on copies of them, so the entries are kept.)

    if _defend_wall_squad is squad:
        _defend_wall_squad = None


def get_attack_base_squad(target_base: Base | None) -> AttackBaseSquad | None:
    if target_base is None:
        return None
    return _base_to_attack_squad.get(target_base)


def army_for_enemy_unit(enemy_unit: Unit) -> EnemyArmy | None:
    """The army the specified unit is part of, or None if it is not part of any army."""
    return _enemy_unit_to_army.get(enemy_unit)


def get_enemy_armies() -> list[EnemyArmy]:
    """The enemy armies, which are groups of enemy units out on the map."""
    return _enemy_armies


def write_instrumentation() -> None:
    if not config.INSTRUMENTATION_ENABLED:
        return

    squad_array: list[Any] = []
    squad_labels: set[str] = set()
    for squad in _squads:
        # Ignore squads with no units
        if squad.combat_unit_count() == 0:
            continue

        # Check if we have multiple squads with the same label
        if squad.label in squad_labels:
            log.get(f"Instrumentation Error: Duplicate squad label {squad.label}")
        squad_labels.add(squad.label)

        squad.add_instrumentation(squad_array)

    cherryvis.write_frame_data("squads", squad_array, 250)

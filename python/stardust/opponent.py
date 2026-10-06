"""Port of Opponent.{h,cpp}: what we know about the opponent, including results and observations from previous games
against them (stored as one JSON object per line in bwapi-data)."""

from __future__ import annotations

import json
import math
from typing import Any

import bwapi
from bwapi import Race, Races, UnitType, UnitTypes
from stardust import common, config
from stardust.cpp import INT_MAX
from stardust.instrumentation import cherryvis, log
from stardust.util import file_tools

_name = ""
_race_unknown = False
_built_unit_requiring_detection = False
_previous_games: list[dict[str, Any]] = []
_current_game: dict[str, Any] = {}
_set_keys: set[str] = set()


def _normalized_enemy_name() -> str:
    return "".join(bwapi.Broodwar.enemy().getName().lower().split())


def _opponent_data_filename(writing: bool = False) -> str:
    return file_tools.get_file_path(f"stardust_{_name}", "json", writing)


def _is_previous_game_on_this_map(previous_game: dict[str, Any]) -> bool:
    map_hash = previous_game.get("mapHash")
    return isinstance(map_hash, str) and map_hash == bwapi.Broodwar.mapHash()


def _is_race_unknown() -> bool:
    return bwapi.Broodwar.enemy().getRace() not in (Races.Protoss, Races.Terran, Races.Zerg)


def initialize() -> None:
    global _name, _race_unknown, _built_unit_requiring_detection, _current_game

    _set_keys.clear()

    _name = _normalized_enemy_name()
    _race_unknown = _is_race_unknown()
    _built_unit_requiring_detection = False

    _previous_games.clear()
    _current_game = {}

    try:
        with open(_opponent_data_filename(), encoding="utf-8") as file:
            for line in file:
                line = line.rstrip("\n")
                if not line:
                    break
                try:
                    game = json.loads(line)
                except ValueError as error:
                    log.get(f"Exception caught parsing previous game: {error}; line: {line}")
                    break
                if isinstance(game, dict):
                    _previous_games.append(game)
    except OSError:
        pass

    log.get(f"Read {len(_previous_games)} previous game result(s)")

    _current_game["mapHash"] = bwapi.Broodwar.mapHash()

    # Default values for some items
    _current_game["pylonInOurMain"] = INT_MAX
    _current_game["firstDarkTemplarCompleted"] = INT_MAX
    _current_game["firstMutaliskCompleted"] = INT_MAX
    _current_game["firstLurkerAtOurMain"] = INT_MAX
    _current_game["sneakAttack"] = INT_MAX
    _current_game["elevatoredUnits"] = 0
    _current_game["myStrategy"] = []
    _current_game["enemyStrategy"] = []


def update() -> None:
    from stardust.map import game_map
    from stardust.map.path_finding import path_finding
    from stardust.map.path_finding.path_finding import PathFindingOptions
    from stardust.units import units

    # Detect sneak attacks
    # A sneak attack is when the enemy has at least 4 units in our main base in the early game while our army is out
    # on the map. This might happen because of a runaround or a drop.
    if common.current_frame < 12000 and not is_game_value_set("sneakAttack"):
        main_base = game_map.get_my_main()
        enemy_main = game_map.get_enemy_starting_main()
        if main_base is not None and enemy_main is not None and len(units.enemy_at_base(main_base)) >= 4:
            # Verify we have at least four combat units closer to the enemy's main than ours
            combat_units_on_map = 0

            def count_type(unit_type: UnitType) -> None:
                nonlocal combat_units_on_map
                assert main_base is not None and enemy_main is not None
                for unit in units.all_mine_completed_of_type(unit_type):
                    if not unit.exists():
                        continue
                    if not unit.completed:
                        continue
                    dist_ours = path_finding.get_ground_distance(unit.last_position, main_base.get_position(),
                                                                 unit.type, PathFindingOptions.UseNeighbouringBWEMArea)
                    dist_theirs = path_finding.get_ground_distance(unit.last_position, enemy_main.get_position(),
                                                                   unit.type,
                                                                   PathFindingOptions.UseNeighbouringBWEMArea)
                    if dist_theirs <= dist_ours:
                        combat_units_on_map += 1

            count_type(UnitTypes.Protoss_Zealot)
            count_type(UnitTypes.Protoss_Dragoon)
            if combat_units_on_map >= 4:
                set_game_value("sneakAttack", common.current_frame)


def game_end(is_winner: bool) -> None:
    _current_game["won"] = is_winner

    _previous_games.append(_current_game)

    # (nlohmann::json objects keep their keys sorted, so Stardust writes them in sorted order.)
    with open(_opponent_data_filename(True), "w", encoding="utf-8") as file:
        file.write("\n".join(json.dumps(game, separators=(",", ":"), sort_keys=True, ensure_ascii=True)
                             for game in _previous_games))


def get_name() -> str:
    global _name
    if not _name:
        _name = _normalized_enemy_name()
    return _name


def is_unknown_race() -> bool:
    return _race_unknown


def has_race_just_been_determined() -> bool:
    global _race_unknown

    if not _race_unknown:
        return False

    _race_unknown = _is_race_unknown()
    if _race_unknown:
        return False

    log.get(f"Enemy identified as {bwapi.Broodwar.enemy().getRace()}")
    return True


def can_be_race(race: Race) -> bool:
    return _race_unknown or bwapi.Broodwar.enemy().getRace() == race


def set_has_built_unit_requiring_detection() -> None:
    global _built_unit_requiring_detection
    _built_unit_requiring_detection = True


def has_built_unit_requiring_detection() -> bool:
    return _built_unit_requiring_detection


def set_game_value(key: str, value: int) -> None:
    if _current_game.get(key) != value:
        if config.CHERRYVIS_ENABLED:
            cherryvis.log(f"Set game value {key} to {value}")
        log.get(f"Set game value {key} to {value}")
    _current_game[key] = value
    _set_keys.add(key)


def increment_game_value(key: str, delta: int = 1) -> None:
    current_value = 0
    if is_game_value_set(key):
        current_value = int(_current_game[key])

    _current_game[key] = current_value + delta

    if config.CHERRYVIS_ENABLED:
        cherryvis.log(f"Incremented game value {key} from {current_value} to {_current_game[key]}")
    log.get(f"Incremented game value {key} from {current_value} to {_current_game[key]}")
    _set_keys.add(key)


def is_game_value_set(key: str) -> bool:
    return key in _set_keys


def add_my_strategy_change(strategy: str) -> None:
    _current_game["myStrategy"].append([common.current_frame, strategy])


def add_enemy_strategy_change(strategy: str) -> None:
    _current_game["enemyStrategy"].append([common.current_frame, strategy])


def min_value_in_previous_games(key: str, default_no_data: int, max_count: int = INT_MAX, min_count: int = 0) -> int:
    if len(_previous_games) < min_count:
        return default_no_data

    result = INT_MAX

    count = 0
    for previous_game in reversed(_previous_games):
        if count >= max_count:
            break

        # If we hit a record where this data point isn't recorded, this means it was played by a previous version of
        # the bot. Apply the minimum game count restriction and return.
        if key not in previous_game:
            if count < min_count:
                return default_no_data
            return result

        value = previous_game[key]
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            result = min(result, int(value))

        count += 1

    return result


def win_loss_ratio(default_value: float, max_count: int = INT_MAX) -> float:
    wins = 0
    losses = 0

    count = 0
    for previous_game in reversed(_previous_games):
        if count >= max_count:
            break

        won = previous_game.get("won")
        if isinstance(won, bool):
            if won:
                wins += 1
            else:
                losses += 1

        count += 1

    if wins == 0 and losses == 0:
        return default_value
    return wins / (wins + losses)


def select_opening_ucb1(openings: list[str], decay_factor: float = 0.05) -> str:
    """Picks the next opening from the given set of openings using UCB1 based on win rate of the given openings."""
    # Gather wins, losses, reward, and potential for previous games
    # To account for the opponent changing strategies, we weight recent results above older ones using the decay
    # factor. We also give a bonus weighting to results on the same map.
    results_by_opening: dict[str, list[float]] = {}  # wins, losses, reward, potential
    total_potential = 0.0
    count = 0
    for previous_game in reversed(_previous_games):
        won = previous_game.get("won")
        if not isinstance(won, bool):
            continue

        my_strategies = previous_game.get("myStrategy")
        if not isinstance(my_strategies, list):
            continue
        try:
            initial_strategy = my_strategies[0]
            if initial_strategy[0] != 0:
                continue
            opening = initial_strategy[1]
        except (IndexError, KeyError, TypeError):
            count += 1
            continue
        if not isinstance(opening, str):
            count += 1
            continue

        results = results_by_opening.setdefault(opening, [0, 0, 0.0, 0.0])

        this_potential = 2.0 if _is_previous_game_on_this_map(previous_game) else 1.0
        this_potential *= math.exp(decay_factor * -1 * (count + 1))
        results[3] += this_potential
        total_potential += this_potential
        if won:
            results[0] += 1
            results[2] += this_potential
        else:
            results[1] += 1

        count += 1

    log.get(f"Previous opening results for past {count} games:")
    for opening, (wins, losses, reward, potential) in sorted(results_by_opening.items()):
        log.get(f"{opening}: {int(wins)} won {int(losses)} lost; weighted result {100.0 * reward / potential:.1f}%")

    # If there is one, select the first opening that has either never lost or hasn't been attempted
    for opening in openings:
        opening_results = results_by_opening.get(opening)
        if opening_results is None or opening_results[1] == 0:
            return opening

    # Run UCB1 on each opening
    best_score = 0.0
    best_opening = ""
    for opening in openings:
        _, _, reward, potential = results_by_opening[opening]

        score = (reward / potential) + math.sqrt(2.0 * math.log(total_potential) / potential)

        if score > best_score:
            best_score = score
            best_opening = opening

    return best_opening

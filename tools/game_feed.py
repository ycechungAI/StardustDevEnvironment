"""Reads a game's status feed (OPENBW_STATUS_FILE, written by OpenBW once a game second) into what tools/watch.py
shows: each player's toolbar numbers and current production, a log of notable events, and end-of-game statistics.

One JSON object per line:
    {"frame": F, "players": [{"slot", "name", "race", "color", "local", "minerals", "gas", "gathered": [m, g],
                              "supply": [used, max], "workers", "units": {name: [completed, in progress]},
                              "production": [{"name", "kind", "progress", "queued"}],
                              "attacked": [{"name", "x", "y"}], "born": [names], "died": [names]}]}
and, when the game ends, {"end": {"result": "WON" | "LOST" | "DRAW", "us", "opponent", "map"}} (result for "us").
"""

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

FRAMES_PER_SECOND = 1000 / 42  # Fastest

BUILDINGS = {
    # Terran
    "Command Center", "Comsat Station", "Nuclear Silo", "Supply Depot", "Refinery", "Barracks", "Academy", "Factory",
    "Starport", "Control Tower", "Science Facility", "Covert Ops", "Physics Lab", "Machine Shop", "Engineering Bay",
    "Armory", "Missile Turret", "Bunker",
    # Zerg
    "Hatchery", "Lair", "Hive", "Nydus Canal", "Hydralisk Den", "Defiler Mound", "Greater Spire", "Queens Nest",
    "Evolution Chamber", "Ultralisk Cavern", "Spire", "Spawning Pool", "Creep Colony", "Spore Colony",
    "Sunken Colony", "Extractor", "Infested CC",
    # Protoss
    "Nexus", "Robotics Facility", "Pylon", "Assimilator", "Observatory", "Gateway", "Photon Cannon",
    "Citadel of Adun", "Cybernetics Core", "Templar Archives", "Forge", "Stargate", "Fleet Beacon",
    "Arbiter Tribunal", "Robotics Support Bay", "Shield Battery",
}
NOT_ARMY = {"SCV", "Drone", "Probe", "Overlord", "Larva", "Egg", "Lurker Egg", "Cocoon", "Spider Mine",
            "Interceptor", "Scarab", "Nuclear Missile"}
ATTACK_REPEAT_SECONDS = 15  # report the same building type under attack again after this long


def game_time(frame: int) -> str:
    seconds = int(frame / FRAMES_PER_SECOND)
    return f"{seconds // 60}:{seconds % 60:02d}"


@dataclass
class Event:
    frame: int
    slot: int  # the player it is about
    text: str


@dataclass
class PlayerStats:
    produced_units: int = 0
    produced_buildings: int = 0
    lost_units: int = 0
    lost_buildings: int = 0
    killed_units: int = 0
    killed_buildings: int = 0
    peak_supply: int = 0
    peak_workers: int = 0
    peak_army_supply: int = 0


@dataclass
class Sample:
    frame: int
    workers: int
    army_supply: int
    gathered: int


@dataclass
class GameModel:
    """Everything known about one game so far."""
    snapshot: dict[str, Any] = field(default_factory=dict)  # the latest status line
    end: dict[str, Any] | None = None
    events: list[Event] = field(default_factory=list)
    stats: dict[int, PlayerStats] = field(default_factory=dict)
    history: dict[int, list[Sample]] = field(default_factory=dict)
    last_attack_report: dict[tuple[int, str], int] = field(default_factory=dict)
    first: bool = True

    @property
    def frame(self) -> int:
        return int(self.snapshot.get("frame", 0))

    @property
    def players(self) -> list[dict[str, Any]]:
        return list(self.snapshot.get("players", []))

    def player(self, slot: int) -> dict[str, Any] | None:
        return next((p for p in self.players if p["slot"] == slot), None)

    def apply(self, line: dict[str, Any]) -> list[Event]:
        """Takes one feed line; returns the events it brought."""
        if "end" in line:
            self.end = line["end"]
            return []
        previous = {p["slot"]: p for p in self.players}
        self.snapshot = line
        frame = int(line["frame"])
        new: list[Event] = []
        players = line.get("players", [])
        for player in players:
            slot = player["slot"]
            stats = self.stats.setdefault(slot, PlayerStats())
            before = previous.get(slot)
            others = [p for p in players if p["slot"] != slot]

            # The units a player starts with are not "produced"
            if not self.first:
                for name in player.get("born", []):
                    if name in BUILDINGS:
                        stats.produced_buildings += 1
                    elif name not in ("Larva", "Egg", "Lurker Egg", "Cocoon", "Interceptor", "Scarab"):
                        stats.produced_units += 1
            for name in player.get("died", []):
                building = name in BUILDINGS
                if building:
                    stats.lost_buildings += 1
                elif name not in ("Larva", "Egg", "Lurker Egg", "Cocoon", "Interceptor", "Scarab"):
                    stats.lost_units += 1
                else:
                    continue
                for other in others:
                    other_stats = self.stats.setdefault(other["slot"], PlayerStats())
                    if building:
                        other_stats.killed_buildings += 1
                    else:
                        other_stats.killed_units += 1

            supply_used = int(player.get("supply", [0, 0])[0])
            workers = int(player.get("workers", 0))
            army = army_supply(player)
            stats.peak_supply = max(stats.peak_supply, supply_used)
            stats.peak_workers = max(stats.peak_workers, workers)
            stats.peak_army_supply = max(stats.peak_army_supply, army)
            gathered = sum(int(v) for v in player.get("gathered", [0, 0]))
            self.history.setdefault(slot, []).append(Sample(frame, workers, army, gathered))

            if self.first or before is None:
                continue
            new += self._events(frame, slot, before, player)
        self.first = False
        self.events += new
        return new

    def _events(self, frame: int, slot: int, before: dict[str, Any], now: dict[str, Any]) -> list[Event]:
        events: list[Event] = []
        # Buildings under attack, at most once per type every few seconds
        for name in sorted({a["name"] for a in now.get("attacked", [])}):
            key = (slot, name)
            last = self.last_attack_report.get(key)
            if last is None or frame - last >= ATTACK_REPEAT_SECONDS * FRAMES_PER_SECOND:
                self.last_attack_report[key] = frame
                events.append(Event(frame, slot, f"{name} under attack!"))
        # Losses, grouped by type
        died: dict[str, int] = {}
        for name in now.get("died", []):
            if name not in ("Larva", "Egg", "Lurker Egg", "Cocoon", "Interceptor", "Scarab"):
                died[name] = died.get(name, 0) + 1
        for name, count in sorted(died.items()):
            events.append(Event(frame, slot, f"lost {count} {name}" if count > 1 else f"lost a {name}"))
        # Buildings started and finished
        for name in now.get("born", []):
            if name in BUILDINGS:
                events.append(Event(frame, slot, f"started {name}"))
        before_units = before.get("units", {})
        for name, (completed, _) in now.get("units", {}).items():
            gained = completed - before_units.get(name, [0, 0])[0]
            if name in BUILDINGS and gained > 0 and name not in now.get("born", []):
                events.append(Event(frame, slot, f"{name} finished" if gained == 1 else f"{gained} {name} finished"))
        # Research, upgrades and building morphs that started
        started_before = {(p["kind"], p["name"]) for p in before.get("production", [])}
        for item in now.get("production", []):
            key = (item["kind"], item["name"])
            if key in started_before:
                continue
            if item["kind"] == "research":
                events.append(Event(frame, slot, f"researching {item['name']}"))
            elif item["kind"] == "upgrade":
                events.append(Event(frame, slot, f"upgrading {item['name']}"))
            elif item["kind"] == "morph" and item["name"] in BUILDINGS:
                events.append(Event(frame, slot, f"morphing into {item['name']}"))
        return events

    def result_for(self, slot: int) -> str | None:
        """VICTORY, DEFEAT or DRAW for this player, once the game has ended."""
        if not self.end:
            return None
        result = self.end.get("result")
        if result == "DRAW":
            return "DRAW"
        player = self.player(slot)
        is_us = bool(player and player.get("local"))
        won = (result == "WON") == is_us
        return "VICTORY" if won else "DEFEAT"


def army_supply(player: dict[str, Any]) -> int:
    """Supply used by everything but workers and overlord-like units: the supply total less workers' supply."""
    supply = int(player.get("supply", [0, 0])[0])
    return max(0, supply - int(player.get("workers", 0)))


def army_units(player: dict[str, Any]) -> int:
    return sum(counts[0] for name, counts in player.get("units", {}).items()
               if name not in BUILDINGS and name not in NOT_ARMY)


class FeedReader:
    """Follows a status file as the game writes it."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.offset = 0
        self.partial = ""
        self.model = GameModel()

    def poll(self) -> list[Event]:
        try:
            with self.path.open("r", encoding="utf-8", errors="replace") as f:
                f.seek(self.offset)
                data = f.read()
                self.offset = f.tell()
        except OSError:
            return []
        text = self.partial + data
        lines = text.split("\n")
        self.partial = lines.pop()  # an incomplete last line waits for the rest
        events: list[Event] = []
        for line in lines:
            if not line.strip():
                continue
            try:
                events += self.model.apply(json.loads(line))
            except (json.JSONDecodeError, KeyError, TypeError, ValueError):
                continue
        return events

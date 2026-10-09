"""Looks back at past test games through their replays: lists them, writes a summary of each game's highlights, plays
one in a window, and keeps what the people watching said about it.

Usage:
  python tools/replays.py                      pick one of the latest games, then summarise, watch or comment on it
  python tools/replays.py list [FILTERS] [--last N]
  python tools/replays.py summary [GAME ...] [FILTERS] [--last N] [--refresh]
  python tools/replays.py watch GAME [--at MM:SS | --at decisive | --at fight N]
  python tools/replays.py comment GAME [--text TEXT] [--by NAME]
  python tools/replays.py comments [GAME ...] [FILTERS]

The test harness saves a replay of every game to build/test/replays/, and this reads them from there (--dir picks
another folder). GAME is the number `list` shows (1 is the newest game), part of a replay's file name, or a path.
FILTERS are --bot, --opponent, --result (won, lost or draw) and --map, each matching part of the name.

A summary covers the result, a short story of the game, the key moments, every sizeable fight, the problems each side
had (supply blocks, banked money, lost workers, units camped in its base), both build orders and a minute-by-minute
table, so the game can be understood without watching it. Summaries are written to build/test/replays/summaries/ and
comments to build/test/replays/comments/, one Markdown file per replay; a game's summary includes its comments.

Summaries come from tools/replay/replay_dump, which replays the game in OpenBW without graphics (about three seconds a
game; build it with `cmake --build build --target replay_dump`). Watching needs replay_viewer from the UI build
(`cmake --build build-ui --target replay_viewer`, in a build directory configured with -DOPENBW_ENABLE_UI=ON).
"""

import argparse
import bisect
import getpass
import json
import math
import re
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPLAYS = ROOT / "build" / "test" / "replays"
DUMP = ROOT / "build" / "tools" / "replay_dump"
VIEWER = ROOT / "build-ui" / "tools" / "replay_viewer"
DATA_DIRS = [ROOT / "build" / "test", ROOT / "build-ui" / "test"]  # where StarDat.mpq and the other MPQs are

# Comments on these bots' games are human advice, which bots/genai/README.md has to list
GENAI_BOTS = {"ClaudeOpus55", "LunaOpus55", "GrokOpus55"}

FPS = 1000 / 42  # game frames per second at the fastest speed
MINUTE = 60 * FPS
BUILD_ORDER_FRAMES = round(6 * MINUTE)

# <bot>_vs_<opponent>_<map>_<seed>_<result>_<date>_<time>_<outcome>.rep, or without "<bot>_vs_" for the Python port
NAME = re.compile(r"^(?:(?P<bot>.+?)_vs_)?(?P<opponent>.+?)_(?P<map>\(\d\).+)_(?P<seed>\d+)_(?P<result>WON|LOST|DRAW)"
                  r"_(?P<date>\d{8})_(?P<time>\d{6})(?:_(?P<outcome>[A-Z]+))?$")
# The earliest replays: <opponent>[_<map>_<seed>]_<date>_<time>_<outcome>.rep, with no result
EARLY_NAME = re.compile(r"^(?P<opponent>.+?)(?:_(?P<map>\(\d\).+)_(?P<seed>\d+))?_(?P<date>\d{8})_(?P<time>\d{6})"
                        r"_(?P<outcome>[A-Z]+)$")

WORKERS = {"Protoss_Probe", "Terran_SCV", "Zerg_Drone"}
DEPOTS = {"Protoss_Nexus", "Terran_Command_Center", "Zerg_Hatchery", "Zerg_Lair", "Zerg_Hive"}
BUILDINGS = set("""
    Protoss_Nexus Protoss_Pylon Protoss_Assimilator Protoss_Gateway Protoss_Forge Protoss_Photon_Cannon
    Protoss_Cybernetics_Core Protoss_Shield_Battery Protoss_Robotics_Facility Protoss_Stargate Protoss_Citadel_of_Adun
    Protoss_Robotics_Support_Bay Protoss_Fleet_Beacon Protoss_Templar_Archives Protoss_Observatory
    Protoss_Arbiter_Tribunal Terran_Command_Center Terran_Comsat_Station Terran_Nuclear_Silo Terran_Supply_Depot
    Terran_Refinery Terran_Barracks Terran_Academy Terran_Factory Terran_Starport Terran_Control_Tower
    Terran_Science_Facility Terran_Covert_Ops Terran_Physics_Lab Terran_Machine_Shop Terran_Engineering_Bay
    Terran_Armory Terran_Missile_Turret Terran_Bunker Zerg_Hatchery Zerg_Lair Zerg_Hive Zerg_Nydus_Canal
    Zerg_Hydralisk_Den Zerg_Defiler_Mound Zerg_Greater_Spire Zerg_Queens_Nest Zerg_Evolution_Chamber
    Zerg_Ultralisk_Cavern Zerg_Spire Zerg_Spawning_Pool Zerg_Creep_Colony Zerg_Spore_Colony Zerg_Sunken_Colony
    Zerg_Extractor""".split())
PRODUCTION = {"Protoss_Gateway", "Protoss_Robotics_Facility", "Protoss_Stargate", "Terran_Barracks", "Terran_Factory",
              "Terran_Starport", "Zerg_Spawning_Pool"}
FIRST_PRODUCTION = {"Protoss": "Gateway", "Terran": "Barracks", "Zerg": "Spawning Pool"}
DEFENCE = {"Protoss_Photon_Cannon", "Protoss_Shield_Battery", "Terran_Bunker", "Terran_Missile_Turret",
           "Zerg_Sunken_Colony", "Zerg_Spore_Colony"}
ROUTINE = {"Protoss_Pylon", "Protoss_Assimilator", "Terran_Supply_Depot", "Terran_Refinery", "Zerg_Extractor",
           "Zerg_Creep_Colony", "Zerg_Overlord"}
NOT_ARMY = WORKERS | BUILDINGS | {"Zerg_Overlord", "Zerg_Egg", "Zerg_Cocoon", "Zerg_Lurker_Egg",
                                  "Terran_Vulture_Spider_Mine"}
HATCHING = {"Zerg_Egg", "Zerg_Cocoon", "Zerg_Lurker_Egg"}
ORDINALS = {2: "a second", 3: "a third", 4: "a fourth", 5: "a fifth"}


def clock_time(frame: float) -> str:
    seconds = max(0, round(frame / FPS))
    return f"{seconds // 60}:{seconds % 60:02d}"


def duration(frames: float) -> str:
    seconds = max(0, round(frames / FPS))
    return f"{seconds} s" if seconds < 60 else f"{seconds // 60}:{seconds % 60:02d}"


def parse_time(text: str) -> int:
    minutes, _, seconds = text.partition(":")
    return round((int(minutes) * 60 + int(seconds or 0)) * FPS)


def unit_name(unit_type: str) -> str:
    unit_type = re.sub(r"^(Protoss|Terran|Zerg)_", "", unit_type)
    unit_type = re.sub(r"Siege_Tank_(Tank|Siege)_Mode", "Siege_Tank", unit_type)
    return unit_type.replace("_", " ")


def article(name: str) -> str:
    return f"{'an' if name[0] in 'AEIOU' else 'a'} {name}"


def plural(name: str, n: int) -> str:
    if n == 1 or name.endswith(("Templar", "Scourge", "Barracks", "Archon s")):
        return name
    if name.endswith("Nexus"):
        return name + "es"
    if name.endswith("y") and not name.endswith(("ay", "ey", "oy")):
        return name[:-1] + "ies"
    return name + "s"


def counted(counter: Counter, limit: int = 0) -> str:
    """'4 Zealots, 2 Probes' from a Counter of unit types, largest first."""
    items = counter.most_common()
    parts = [f"{n} {plural(unit_name(t), n)}" for t, n in items[:limit or None]]
    if limit and len(items) > limit:
        parts.append(f"{sum(n for _, n in items[limit:])} more")
    return ", ".join(parts)


def supply_text(value: float) -> str:
    return f"{value:g}"


# --- Finding replays ---

@dataclass
class Replay:
    path: Path
    bot: str
    opponent: str
    map: str
    result: str
    played: datetime
    seed: str = ""

    @property
    def stem(self) -> str:
        return self.path.name.removesuffix(".rep")

    def summary_path(self) -> Path:
        return self.path.parent / "summaries" / f"{self.stem}.md"

    def comments_path(self) -> Path:
        return self.path.parent / "comments" / f"{self.stem}.md"


def parse_name(path: Path) -> Replay:
    stem = path.name.removesuffix(".rep")
    if match := NAME.match(stem):
        played = datetime.strptime(match["date"] + match["time"], "%Y%m%d%H%M%S")
        return Replay(path, match["bot"] or "Stardust", match["opponent"], match["map"], match["result"], played,
                      match["seed"])
    if match := EARLY_NAME.match(stem):
        played = datetime.strptime(match["date"] + match["time"], "%Y%m%d%H%M%S")
        return Replay(path, "Stardust", match["opponent"], match["map"] or "?", "?", played, match["seed"] or "")
    return Replay(path, "?", stem, "?", "?", datetime.fromtimestamp(path.stat().st_mtime))


def find_replays(folder: Path) -> list[Replay]:
    replays = [parse_name(p) for p in folder.glob("*.rep") if p.is_file()]
    replays.sort(key=lambda r: (r.played, r.path.name), reverse=True)
    return replays


def matches(replay: Replay, args) -> bool:
    return ((not args.bot or args.bot.lower() in replay.bot.lower())
            and (not args.opponent or args.opponent.lower() in replay.opponent.lower())
            and (not args.result or replay.result == args.result.upper())
            and (not args.map or args.map.lower() in replay.map.lower()))


def resolve(spec: str, replays: list[Replay], folder: Path) -> Replay:
    if spec.isdigit():
        n = int(spec)
        if 1 <= n <= len(replays):
            return replays[n - 1]
        sys.exit(f"There are {len(replays)} replays, so {n} is out of range")
    for candidate in (Path(spec), folder / spec, folder / f"{spec}.rep"):
        if candidate.is_file():
            return parse_name(candidate)
    found = [r for r in replays if spec.lower() in r.path.name.lower()]
    if not found:
        sys.exit(f"No replay in {folder} matches {spec!r}")
    if len(found) > 1:
        print(f"({len(found)} replays match {spec!r}; using the newest)", file=sys.stderr)
    return found[0]


# --- Reading a game ---

def data_dir() -> Path:
    for folder in DATA_DIRS:
        if (folder / "StarDat.mpq").is_file():
            return folder
    sys.exit("StarDat.mpq, BrooDat.mpq and patch_rt.mpq weren't found in build/test or build-ui/test")


def run_dump(replay: Replay) -> list[dict]:
    if not DUMP.is_file():
        sys.exit(f"{DUMP.relative_to(ROOT)} isn't built yet: cmake --build build --target replay_dump")
    result = subprocess.run([str(DUMP), str(replay.path), "--data", str(data_dir())], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip()[-500:] or f"replay_dump exited with {result.returncode}")
    return [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]


@dataclass
class Player:
    p: int
    name: str
    race: str
    start: tuple[int, int]
    tested: bool  # the bot the harness was testing, rather than its opponent


@dataclass
class Base:
    x: float
    y: float
    name: str = ""


@dataclass
class Fight:
    deaths: list[dict] = field(default_factory=list)

    @property
    def start(self) -> int:
        return self.deaths[0]["f"]

    @property
    def end(self) -> int:
        return self.deaths[-1]["f"]

    @property
    def centre(self) -> tuple[float, float]:
        return (sum(d["x"] for d in self.deaths) / len(self.deaths), sum(d["y"] for d in self.deaths) / len(self.deaths))

    def losses(self, p: int) -> Counter:
        return Counter(d["type"] for d in self.deaths if d["p"] == p)

    def value(self, p: int) -> int:
        return sum(d["m"] + d["g"] for d in self.deaths if d["p"] == p)

    def unit_value(self, p: int) -> int:
        return sum(d["m"] + d["g"] for d in self.deaths if d["p"] == p and d["type"] not in BUILDINGS)

    @property
    def total(self) -> int:
        return sum(d["m"] + d["g"] for d in self.deaths)


class Game:
    def __init__(self, replay: Replay, events: list[dict]):
        self.replay = replay
        meta = events[0]
        # The file name's map name, since a replay's own can be in Korean (단장의 능선 is Heartbreak Ridge)
        self.map_name = re.sub(r"^\(\d\)", "", replay.map).replace("_", " ") if replay.map != "?" else meta["map"]
        self.width, self.height = meta["width"], meta["height"]
        end = next(e for e in reversed(events) if e["t"] == "end")
        self.frames = end["f"]
        self.results = end["results"]

        self.players: dict[int, Player] = {}
        for info in meta["players"]:
            name = {"Tests": replay.bot, "Opponent": replay.opponent}.get(info["name"], info["name"])
            self.players[info["p"]] = Player(info["p"], name, info["race"], tuple(info["start"]),
                                             info["name"] == "Tests")
        names = Counter(p.name for p in self.players.values())
        for player in self.players.values():
            if names[player.name] > 1:
                player.name += f" (player {player.p + 1})"
        order = sorted(self.players.values(), key=lambda pl: not pl.tested)
        self.us, self.them = order[0], order[-1]

        # The earliest replays' names don't give the result, so it comes from the replay itself
        eliminated = {r["player"] for r in self.results if "eliminated" in r}
        won = {r["player"] for r in self.results if r.get("victory_state") == 3}
        if replay.result != "?":
            self.result = replay.result
        elif self.them.p in eliminated or self.us.p in won:
            self.result = "WON"
        elif self.us.p in eliminated or self.them.p in won:
            self.result = "LOST"
        else:
            self.result = "DRAW"

        self.snaps = [{"f": s["f"], "players": {d["p"]: d for d in s["players"]}}
                      for s in events if s["t"] == "snap"]
        self.snap_frames = [s["f"] for s in self.snaps]
        self.comps = [{"f": c["f"], "players": {d["p"]: d["units"] for d in c["players"]}}
                      for c in events if c["t"] == "comp"]
        self.events = [e for e in events if e["t"] in ("new", "morph", "done", "dead", "upgrade")]
        self.deaths = [e for e in self.events if e["t"] == "dead"]
        self.bases = self.find_bases(meta["resources"])
        self.fights = self.find_fights()

    # Places

    def find_bases(self, resources: list[list[int]]) -> list[Base]:
        """Groups minerals and geysers into bases and names them: each player's main and natural, then by clock."""
        parent = list(range(len(resources)))

        def root(i: int) -> int:
            while parent[i] != i:
                parent[i] = parent[parent[i]]
                i = parent[i]
            return i

        for i, a in enumerate(resources):
            for j in range(i + 1, len(resources)):
                b = resources[j]
                if abs(a[0] - b[0]) < 300 and abs(a[1] - b[1]) < 300 and math.dist(a[:2], b[:2]) < 300:
                    parent[root(i)] = root(j)
        groups: dict[int, list] = {}
        for i, r in enumerate(resources):
            groups.setdefault(root(i), []).append(r)
        bases = []
        for group in groups.values():
            minerals = sum(1 for r in group if not r[2])
            if minerals >= 6 or (minerals >= 4 and len(group) > minerals):
                bases.append(Base(sum(r[0] for r in group) / len(group), sum(r[1] for r in group) / len(group)))

        mains = {}
        for player in self.players.values():
            if bases:
                main = min(bases, key=lambda b: math.dist((b.x, b.y), player.start))
                main.x, main.y = player.start  # the depot, rather than the middle of the minerals
                main.name = f"{player.name}'s main"
                mains[player.p] = main
        for player in self.players.values():
            if player.p not in mains:
                continue
            main = mains[player.p]
            others = [b for b in bases if not b.name]
            if others:
                natural = min(others, key=lambda b: math.dist((b.x, b.y), (main.x, main.y)))
                natural.name = f"{player.name}'s natural"
        for base in bases:
            if not base.name:
                base.name = f"the {self.clock(base.x, base.y)} base"
        return bases

    def clock(self, x: float, y: float) -> str:
        dx, dy = x - self.width / 2, y - self.height / 2
        if math.hypot(dx, dy) < 0.12 * min(self.width, self.height):
            return "centre"
        hour = round((math.degrees(math.atan2(dx, -dy)) % 360) / 30) % 12 or 12
        return f"{hour} o'clock"

    def where(self, x: float, y: float) -> str:
        if self.bases:
            base = min(self.bases, key=lambda b: math.dist((b.x, b.y), (x, y)))
            distance = math.dist((base.x, base.y), (x, y))
            if distance <= (640 if base.name.endswith("main") else 480):
                return base.name
            if distance <= 900:
                return f"near {base.name}"
        position = self.clock(x, y)
        return "the middle of the map" if position == "centre" else f"open ground at {position}"

    def at(self, x: float, y: float) -> str:
        """'at P's main', 'near the 7 o'clock base' or 'in the middle of the map'."""
        place = self.where(x, y)
        if place.startswith("near "):
            return place
        return f"in {place}" if place.startswith(("the middle", "open ground")) else f"at {place}"

    # Lookups

    def snap(self, frame: int, p: int) -> dict:
        i = max(0, bisect.bisect_right(self.snap_frames, frame) - 1)
        return self.snaps[i]["players"].get(p, {}) if self.snaps else {}

    def comp(self, frame: int, p: int) -> dict:
        frames = [c["f"] for c in self.comps]
        i = max(0, bisect.bisect_right(frames, frame) - 1)
        return self.comps[i]["players"].get(p, {}) if self.comps else {}

    def army(self, frame: int, p: int) -> Counter:
        return Counter({t: n for t, n in self.comp(frame, p).items() if t not in NOT_ARMY})

    def enemies(self, p: int) -> list[int]:
        return [q for q in self.players if q != p]

    # Analysis

    def find_fights(self) -> list[Fight]:
        """Clusters deaths caused by an enemy into fights: within 10 seconds and 800 pixels of each other."""
        fights: list[Fight] = []
        open_fights: list[Fight] = []
        for death in self.deaths:
            if death["by"] not in self.players or death["by"] == death["p"]:
                continue
            open_fights = [f for f in open_fights if death["f"] - f.end <= 240]
            near = [f for f in open_fights if math.dist(f.centre, (death["x"], death["y"])) <= 800]
            if near:
                min(near, key=lambda f: math.dist(f.centre, (death["x"], death["y"]))).deaths.append(death)
            else:
                fight = Fight([death])
                fights.append(fight)
                open_fights.append(fight)
        return fights

    def big_fights(self) -> list[Fight]:
        return [f for f in self.fights if f.total >= 250 or len(f.deaths) >= 4]

    def decisive_fight(self) -> Fight | None:
        fights = self.big_fights()
        if not fights:
            return None
        us, them = self.us.p, self.them.p
        return max(fights, key=lambda f: (abs(f.unit_value(us) - f.unit_value(them)), f.total))

    def fight_winner(self, fight: Fight) -> str:
        us, them = fight.value(self.us.p), fight.value(self.them.p)
        if us == them:
            return "even"
        return f"{(self.them if us > them else self.us).name} won"

    def build_order(self, p: int, until: int = BUILD_ORDER_FRAMES) -> list[tuple[int, str, float, int]]:
        """(frame, unit type, supply, count) for everything but workers started before `until`, repeats grouped."""
        items: list[tuple[int, str]] = []
        for e in self.events:
            if e["p"] != p or e["f"] == 0 or e["f"] > until or e["t"] not in ("new", "morph"):
                continue
            item = None
            if e["t"] == "new":
                if e["type"] == "Zerg_Egg":
                    item = e.get("into")
                elif e["type"].startswith("Zerg_") and e["type"] not in BUILDINGS:
                    continue  # the second zergling or scourge from an egg
                else:
                    item = e["type"]
            else:
                if e["from"] in HATCHING or ("Siege_Tank" in e["from"] and "Siege_Tank" in e["type"]):
                    continue
                item = e.get("into") if e["type"] in HATCHING else e["type"]
            if item and item not in WORKERS and item != "Terran_Vulture_Spider_Mine":
                items.append((e["f"], item))
        grouped: list[tuple[int, str, float, int]] = []
        for frame, item in items:
            if grouped and grouped[-1][1] == item:
                f, t, s, n = grouped[-1]
                grouped[-1] = (f, t, s, n + 1)
            else:
                grouped.append((frame, item, self.snap(frame, p).get("supply", 0), 1))
        return grouped

    def first_started(self, p: int, types: set[str]) -> int | None:
        for e in self.events:
            if e["p"] == p and e["f"] > 0 and e["t"] in ("new", "morph") and e["type"] in types:
                return e["f"]
        return None

    def periods(self, p: int, condition, minimum: float) -> list[tuple[int, int, list[dict]]]:
        """(start, end, snapshots) for each stretch of at least `minimum` frames where condition(snapshot) holds."""
        found, current = [], []
        for s in self.snaps:
            d = s["players"].get(p)
            if d is not None and condition(d):
                current.append(s)
                continue
            if current and current[-1]["f"] - current[0]["f"] + 24 >= minimum:
                found.append((current[0]["f"], current[-1]["f"] + 24, [c["players"][p] for c in current]))
            current = []
        if current and current[-1]["f"] - current[0]["f"] + 24 >= minimum:
            found.append((current[0]["f"], current[-1]["f"] + 24, [c["players"][p] for c in current]))
        return found

    def key_moments(self) -> list[tuple[int, str]]:
        moments: list[tuple[int, str]] = []
        for player in self.players.values():
            p, name = player.p, player.name
            for enemy in self.enemies(p):
                ename = self.players[enemy].name
                worker_seen = army_seen = False
                biggest = (0, 0)
                for s in self.snaps:
                    near = s["players"].get(p, {}).get("near", {}).get(str(enemy))
                    if not near or s["f"] == 0:
                        continue
                    if near[1] and not worker_seen:
                        moments.append((s["f"], f"First {name} worker in {ename}'s base"))
                        worker_seen = True
                    if near[0] and not army_seen:
                        moments.append((s["f"], f"{name}'s army first reaches {ename}'s base ({near[0]} "
                                                f"{plural('unit', near[0])})"))
                        army_seen = True
                    if near[0] > biggest[1]:
                        biggest = (s["f"], near[0])
                if biggest[1] >= 4:
                    moments.append((biggest[0], f"{name}'s biggest push: {biggest[1]} units in {ename}'s base"))

            # Proxies, cannon rushes and expansions
            proxies: Counter = Counter()
            in_main = 0
            for e in self.events:
                if e["p"] != p or e["f"] == 0 or e["t"] not in ("new", "morph") or e["type"] not in BUILDINGS:
                    continue
                if e["t"] == "morph" and e["from"] != "Zerg_Drone":
                    continue
                own = math.dist((e["x"], e["y"]), player.start)
                enemy_starts = [self.players[q].start for q in self.enemies(p)]
                closest_enemy = min((math.dist((e["x"], e["y"]), s) for s in enemy_starts), default=1e9)
                if e["type"] in DEPOTS:
                    place = self.where(e["x"], e["y"])
                    if place == f"{name}'s main":
                        in_main += 1
                        moments.append((e["f"], f"{name} builds {ORDINALS.get(in_main + 1, 'another')} "
                                                f"{unit_name(e['type'])} in its main"))
                    else:
                        moments.append((e["f"], f"{name} expands: {unit_name(e['type'])} {self.at(e['x'], e['y'])}"))
                elif own > 1200 and closest_enemy < own and e["f"] <= BUILD_ORDER_FRAMES:
                    # Early buildings nearer the enemy than home: a proxy, or a cannon rush in the enemy's base
                    proxies[e["type"]] += 1
                    if proxies[e["type"]] == 1:
                        place = self.where(e["x"], e["y"])
                        inside = any(place in (f"{self.players[q].name}'s main", f"{self.players[q].name}'s natural")
                                     for q in self.enemies(p))
                        kind = ("cannon rush" if e["type"] == "Protoss_Photon_Cannon" else "proxy") if inside \
                            else "proxy"
                        moments.append((e["f"], f"{name} builds {article(unit_name(e['type']))} "
                                                f"{self.at(e['x'], e['y'])} ({kind})"))

            # First of each unit type and each later tech building, and upgrades
            seen: set[str] = set()
            for e in self.events:
                if e["p"] != p or e["f"] == 0:
                    continue
                if e["t"] == "upgrade":
                    level = f" {e['level']}" if e["level"] > 1 else ""
                    moments.append((e["f"], f"{name} finishes {e['name'].replace('_', ' ')}{level}"))
                if e["t"] != "done" or unit_name(e["type"]) in seen:
                    continue
                t = e["type"]
                if t not in NOT_ARMY:
                    moments.append((e["f"], f"{name}'s first {unit_name(t)}"))
                elif t in DEFENCE:
                    moments.append((e["f"], f"{name}'s first {unit_name(t)}, {self.at(e['x'], e['y'])}"))
                elif t in BUILDINGS and t not in ROUTINE and t not in DEPOTS and e["f"] > BUILD_ORDER_FRAMES:
                    moments.append((e["f"], f"{name} finishes its first {unit_name(t)}"))
                seen.add(unit_name(t))

            for e in self.deaths:
                if e["p"] == p and e["type"] in DEPOTS and e["done"]:
                    moments.append((e["f"], f"{name} loses its {unit_name(e['type'])} "
                                            f"{self.at(e['x'], e['y'])}"))
            maxed = next((s["f"] for s in self.snaps if s["players"].get(p, {}).get("supply", 0) >= 199), None)
            if maxed:
                moments.append((maxed, f"{name} reaches 200 supply"))

        if first := next((d for d in self.deaths if d["by"] in self.players and d["by"] != d["p"]), None):
            moments.append((first["f"], f"First blood: {self.players[first['by']].name} kills a "
                                        f"{unit_name(first['type'])} {self.at(first['x'], first['y'])}"))
        decisive = self.decisive_fight()
        for i, fight in enumerate(self.big_fights(), 1):
            mark = " (decisive)" if fight is decisive else ""
            moments.append((fight.start, f"Fight {i}{mark} {self.at(*fight.centre)}: {self.fight_line(fight)}"))
        moments.append((self.frames, self.ending()))
        moments.sort(key=lambda m: m[0])
        return moments

    def fight_line(self, fight: Fight) -> str:
        parts = []
        for player in (self.us, self.them):
            losses = fight.losses(player.p)
            parts.append(f"{player.name} lost {counted(losses, 4) if losses else 'nothing'}")
        return "; ".join(parts) + f". {self.fight_winner(fight)}"

    def ending(self) -> str:
        eliminated = [r for r in self.results if "eliminated" in r]
        winners = [r for r in self.results if r.get("victory_state") == 3]
        if eliminated:
            names = " and ".join(self.players[r["player"]].name for r in eliminated if r["player"] in self.players)
            return f"{names} {'is' if len(eliminated) == 1 else 'are'} eliminated"
        if winners:
            return f"{self.players[winners[0]['player']].name} wins"
        if self.results and all(r.get("victory_state") == 2 for r in self.results):
            return f"The game reaches the frame limit, and the harness scores it {self.result} for {self.us.name}"
        return "The replay ends"

    def problems(self, player: Player) -> list[str]:
        """What went wrong for a player, the things most likely to have decided the game first."""
        p, name = player.p, player.name
        found = []
        game_minutes = self.frames / MINUTE

        production = self.first_started(p, PRODUCTION)
        first = FIRST_PRODUCTION.get(player.race, "production building")
        if production is None and game_minutes > 4:
            found.append(f"Never started a {first}")
        elif production is not None and production > 4 * MINUTE:
            found.append(f"First production building only at {clock_time(production)}")
        if game_minutes > 3 and all(s["players"].get(p, {}).get("army", 0) == 0 for s in self.snaps):
            found.append("Never had a single army unit")

        for enemy in self.enemies(p):
            ename = self.players[enemy].name
            camped = self.periods(enemy, lambda d: sum(d.get("near", {}).get(str(p), [0, 0])) >= 5, 60 * FPS)
            if camped:
                peak = max(sum(d["near"][str(p)]) for c in camped for d in c[2])
                workers = max(d["near"][str(p)][1] for c in camped for d in c[2])
                kind = "units" if workers < peak / 2 else "units, mostly workers,"
                found.append(f"{ename} kept 5 or more {kind} in {name}'s bases from {clock_time(camped[0][0])} for "
                             f"{duration(sum(c[1] - c[0] for c in camped))} (up to {peak})")

        early = [d for d in self.deaths if d["p"] == p and d["type"] in WORKERS and d["f"] <= BUILD_ORDER_FRAMES
                 and d["by"] in self.players and d["by"] != p]
        if early:
            places = Counter(self.at(d["x"], d["y"]) for d in early)
            killers = Counter(self.players[d["by"]].name for d in early)
            found.append(f"Lost {len(early)} {plural(unit_name(early[0]['type']), len(early))} before "
                         f"{clock_time(BUILD_ORDER_FRAMES)}, the first at {clock_time(early[0]['f'])} "
                         f"({counted_places(places)}" + (f"; killed by {', '.join(killers)})" if len(killers) > 1
                                                          or len(self.players) > 2 else ")"))

        blocks = self.periods(p, lambda d: d["max"] < 200 and d["max"] - d["supply"] < 1 and d["min"] >= 100,
                              20 * FPS)
        if blocks:
            longest = max(blocks, key=lambda b: b[1] - b[0])
            total = sum(b[1] - b[0] for b in blocks)
            found.append(f"Supply blocked {len(blocks)} {plural('time', len(blocks))} for {duration(total)} in all; "
                         f"the longest from {clock_time(longest[0])} for {duration(longest[1] - longest[0])} at "
                         f"{supply_text(longest[2][0]['supply'])}/{supply_text(longest[2][0]['max'])} "
                         f"with up to {max(d['min'] for d in longest[2]):,} minerals")

        floats = self.periods(p, lambda d: d["min"] > 800, 60 * FPS)
        if floats:
            peak = max(d["min"] for f in floats for d in f[2])
            maxed = all(d["supply"] >= 190 for f in floats for d in f[2])
            found.append(f"Banked over 800 minerals from {clock_time(floats[0][0])} "
                         f"({duration(sum(f[1] - f[0] for f in floats))} in all, peaking at {peak:,})"
                         + (" while maxed out" if maxed else ""))
        gas = self.periods(p, lambda d: d["gas"] > 400, 120 * FPS)
        if gas:
            found.append(f"Sat on over 400 gas from {clock_time(gas[0][0])} for "
                         f"{duration(sum(g[1] - g[0] for g in gas))} (peak {max(d['gas'] for g in gas for d in g[2])})")

        if self.snaps:
            last = self.snaps[-1]["players"].get(p)
            if last and last["min"] > 1000:
                found.append(f"Ended with {last['min']:,} minerals and {last['gas']:,} gas unspent")
        return found

    def story(self) -> list[str]:
        us, them = self.us, self.them
        verb = {"WON": "beat", "LOST": "lost to", "DRAW": "drew with"}[self.result]
        lines = [f"{us.name} ({us.race}) {verb} {them.name} ({them.race}) in {clock_time(self.frames)} on "
                 f"{self.map_name}. {self.ending()}."]

        openings = []
        for player in (us, them):
            order = [b for b in self.build_order(player.p) if b[1] not in ("Protoss_Pylon", "Terran_Supply_Depot",
                                                                            "Zerg_Overlord")][:4]
            if order:
                steps = ", ".join(f"{unit_name(t)}{f' ×{n}' if n > 1 else ''} at {supply_text(s)} ({clock_time(f)})"
                                  for f, t, s, n in order)
                openings.append(f"{player.name} opened {steps}")
            else:
                openings.append(f"{player.name} built nothing but workers and supply in the first six minutes")
        lines.append("; ".join(openings) + ".")

        first = next((d for d in self.deaths if d["by"] in self.players and d["by"] != d["p"]), None)
        if first:
            lines.append(f"First blood at {clock_time(first['f'])}: {self.players[first['by']].name} killed "
                         f"{self.players[first['p']].name}'s {unit_name(first['type'])} "
                         f"{self.at(first['x'], first['y'])}.")
        else:
            lines.append("Nothing was killed all game.")

        decisive = self.decisive_fight()
        fights = self.big_fights()
        if decisive:
            before = (self.snap(decisive.start - 24, us.p).get("army_supply", 0),
                      self.snap(decisive.start - 24, them.p).get("army_supply", 0))
            lines.append(f"The decisive fight was at {clock_time(decisive.start)} {self.at(*decisive.centre)}, "
                         f"{supply_text(before[0])} army supply against {supply_text(before[1])}: "
                         f"{self.fight_line(decisive)}. There {'was' if len(fights) == 1 else 'were'} "
                         f"{len(fights)} sizeable {plural('fight', len(fights))} in all.")
        lost = {p.p: sum(d["m"] + d["g"] for d in self.deaths if d["p"] == p.p and d["by"] in self.enemies(p.p))
                for p in (us, them)}
        if any(lost.values()):
            lines.append(f"Over the game {us.name} lost {lost[us.p]:,} minerals and gas worth of units and "
                         f"{them.name} lost {lost[them.p]:,}.")

        for player in (us, them):
            problems = self.problems(player)
            if problems:
                problem = problems[0]
                if not any(problem.startswith(p.name) for p in (us, them)):  # a name keeps its capital
                    problem = problem[0].lower() + problem[1:]
                lines.append(f"{player.name}'s biggest problem: {problem}.")

        end = {p.p: self.snap(self.frames, p.p) for p in (us, them)}
        lines.append("At the end: " + "; ".join(
            f"{p.name} {end[p.p].get('workers', 0)} {plural('worker', end[p.p].get('workers', 0))}, {supply_text(end[p.p].get('army_supply', 0))} army "
            f"supply, {supply_text(end[p.p].get('supply', 0))}/{supply_text(end[p.p].get('max', 0))} supply"
            for p in (us, them)) + ".")
        return lines


def shown_path(path: Path) -> Path:
    return path.relative_to(ROOT) if path.is_relative_to(ROOT) else path


def counted_places(places: Counter) -> str:
    return ", ".join(f"{n} {place}" for place, n in places.most_common(3))


# --- Writing the summary ---

def summarise(game: Game) -> str:
    replay, us, them = game.replay, game.us, game.them
    out = [f"# {replay.bot} vs {replay.opponent}: {game.result}", ""]
    out.append(f"{game.map_name} · {clock_time(game.frames)} game time ({game.frames:,} frames) · seed "
               f"{replay.seed or '?'} · played {replay.played:%Y-%m-%d %H:%M} · `{replay.path.name}`")
    out.append("")
    for player in (us, them):
        out.append(f"- **{player.name}**: {player.race}, starting at {game.clock(*player.start)}")
    out += ["", "## What happened", ""]
    out += game.story()

    out += ["", "## Key moments", ""]
    for frame, text in game.key_moments():
        out.append(f"- **{clock_time(frame)}** {text}")

    fights = game.big_fights()
    out += ["", "## Fights", ""]
    if fights:
        decisive = game.decisive_fight()
        out.append(f"| # | Time | Where | Army supply before ({us.name} v {them.name}) | {us.name} lost | "
                   f"{them.name} lost | Result |")
        out.append("|---|---|---|---|---|---|---|")
        for i, fight in enumerate(fights, 1):
            mark = " **decisive**" if fight is decisive else ""
            before = [supply_text(game.snap(fight.start - 24, p.p).get("army_supply", 0)) for p in (us, them)]
            cells = []
            for player in (us, them):
                losses = fight.losses(player.p)
                cells.append(f"{counted(losses, 4)} ({fight.value(player.p):,})" if losses else "nothing")
            out.append(f"| {i}{mark} | {clock_time(fight.start)}–{clock_time(fight.end)} | "
                       f"{game.where(*fight.centre)} | {before[0]} v {before[1]} | {cells[0]} | {cells[1]} | "
                       f"{game.fight_winner(fight)} |")
        out.append("")
        out.append("Losses are in minerals plus gas. A fight is the deaths within 10 seconds and 800 pixels of each "
                   "other; smaller skirmishes are left out.")
    else:
        out.append("No sizeable fights.")

    out += ["", "## Problems spotted", ""]
    for player in (us, them):
        problems = game.problems(player)
        out.append(f"**{player.name}**" + (":" if problems else ": none found"))
        out += [f"- {p}" for p in problems]
        out.append("")

    out += [f"## Build orders (first {clock_time(BUILD_ORDER_FRAMES)}, workers left out)", ""]
    for player in (us, them):
        out += [f"**{player.name}**", "", "| Supply | Time | Started |", "|---|---|---|"]
        for frame, item, supply, n in game.build_order(player.p):
            out.append(f"| {supply_text(supply)} | {clock_time(frame)} | {unit_name(item)}"
                       f"{f' ×{n}' if n > 1 else ''} |")
        out.append("")

    out += ["## Minute by minute", ""]
    out.append(f"Workers, army supply, supply used and resources mined so far.")
    out.append("")
    out.append(f"| Time | {us.name} workers | army | supply | mined | {them.name} workers | army | supply | mined |")
    out.append("|---|---|---|---|---|---|---|---|---|")
    frames = [round(m * MINUTE) for m in range(1, int(game.frames / MINUTE) + 1)] + [game.frames]
    for frame in frames:
        cells = []
        for player in (us, them):
            d = game.snap(frame, player.p)
            cells += [str(d.get("workers", 0)), supply_text(d.get("army_supply", 0)),
                      f"{supply_text(d.get('supply', 0))}/{supply_text(d.get('max', 0))}",
                      f"{d.get('mined', 0) + d.get('gassed', 0):,}"]
        out.append(f"| {clock_time(frame)} | " + " | ".join(cells) + " |")
    out += ["", "| Time | " + " | ".join(f"{p.name}'s army" for p in (us, them)) + " |", "|---|---|---|"]
    for frame in frames[1::2] if len(frames) > 12 else frames:
        out.append(f"| {clock_time(frame)} | "
                   + " | ".join(counted(game.army(frame, p.p), 5) or "none" for p in (us, them)) + " |")

    comments = replay.comments_path()
    if comments.is_file():
        entries = comments.read_text().split("\n## ")[1:]  # past the file's own heading
        out += ["", "## Observers' comments", ""] + [f"### {entry.strip()}\n" for entry in entries]
    return "\n".join(out) + "\n"


def load_game(replay: Replay) -> Game:
    return Game(replay, run_dump(replay))


def summary_for(replay: Replay, refresh: bool = False, game: Game | None = None) -> str:
    """The summary, from the cache unless the replay, its comments, this script or replay_dump changed since."""
    path = replay.summary_path()
    if path.is_file() and not refresh:
        sources = [replay.path, replay.comments_path(), Path(__file__), DUMP]
        if all(not s.exists() or s.stat().st_mtime <= path.stat().st_mtime for s in sources):
            return path.read_text()
    text = summarise(game or load_game(replay))
    path.parent.mkdir(exist_ok=True)
    path.write_text(text)
    return text


def headline(summary: str) -> str:
    lines = summary.splitlines()
    i = lines.index("## What happened") if "## What happened" in lines else 0
    return lines[i + 2] if i + 2 < len(lines) else lines[0]


# --- Watching and commenting ---

def read_comment() -> str:
    print("\nWhat did you notice? Start a line with a time like 3:20 to point at a moment. Finish with an empty line "
          "twice, or Ctrl-D; enter nothing to skip.")
    lines: list[str] = []
    try:
        while True:
            line = input("> ")
            if not line and lines and not lines[-1]:
                break
            lines.append(line)
    except EOFError:
        print()
    return "\n".join(lines).strip()


def save_comment(replay: Replay, text: str, by: str) -> Path:
    path = replay.comments_path()
    path.parent.mkdir(exist_ok=True)
    if not path.is_file():
        path.write_text(f"# Comments on {replay.stem}\n\n{replay.bot} vs {replay.opponent}, {replay.result}, "
                        f"{replay.map}\n")
    with path.open("a") as f:
        f.write(f"\n## {datetime.now():%Y-%m-%d %H:%M}, {by}\n\n{text}\n")
    print(f"Saved to {shown_path(path)}")
    if GENAI_BOTS & {replay.bot, replay.opponent}:
        print("This is a GenAI bot's game, so the comment counts as human advice: it belongs in the list in "
              "bots/genai/README.md.")
    return path


def comment(replay: Replay, text: str | None, by: str) -> None:
    if text is None:
        text = read_comment() if sys.stdin.isatty() else sys.stdin.read().strip()
    if text:
        save_comment(replay, text, by)
    else:
        print("Nothing saved.")


def start_frame(game: Game, at: str | None) -> int:
    if not at:
        return 0
    fights = game.big_fights()
    if at == "decisive":
        fight = game.decisive_fight()
        return max(0, fight.start - round(10 * FPS)) if fight else 0
    if match := re.fullmatch(r"fight ?(\d+)", at):
        n = int(match[1])
        if not 1 <= n <= len(fights):
            sys.exit(f"The game has {len(fights)} fights in its summary")
        return max(0, fights[n - 1].start - round(10 * FPS))
    return parse_time(at)


def watch(replay: Replay, at: str | None, by: str, refresh: bool) -> None:
    if not VIEWER.is_file():
        sys.exit(f"{VIEWER.relative_to(ROOT)} isn't built yet: cmake --build build-ui --target replay_viewer")
    game = load_game(replay)
    print(summary_for(replay, refresh, game).split("\n## Build orders")[0])
    frame = start_frame(game, at)
    print(f"\nOpening the replay{f' at {clock_time(frame)}' if frame else ''}. Space pauses, a and z change the speed, "
          "the arrow keys go 10 seconds back or forward and [ ] a minute; close the window when you're done.")
    subprocess.run([str(VIEWER), str(replay.path), "--data", str(data_dir()), "--frame", str(frame),
                    "--player", str(game.us.p)])
    if sys.stdin.isatty():
        comment(replay, None, by)
    else:
        print("(No terminal to type a comment in; add one with the comment command.)")


# --- Commands ---

def print_list(replays: list[Replay], all_replays: list[Replay], last: int) -> None:
    index = {r.path: i for i, r in enumerate(all_replays, 1)}
    shown = replays[:last] if last else replays
    print(f"{'#':>4}  {'Played':16}  {'Bot':14}  {'Opponent':20}  {'Map':24}  {'Result':6}  Notes")
    for r in shown:
        notes = []
        if r.summary_path().is_file():
            notes.append("summary")
        if r.comments_path().is_file():
            n = r.comments_path().read_text().count("\n## ")
            notes.append(f"{n} {plural('comment', n)}")
        print(f"{index[r.path]:>4}  {r.played:%Y-%m-%d %H:%M}  {r.bot[:14]:14}  {r.opponent[:20]:20}  "
              f"{r.map[:24]:24}  {r.result:6}  {', '.join(notes)}")
    if len(replays) > len(shown):
        print(f"      ... {len(replays) - len(shown)} older; --last 0 lists them all")


def selected(args, replays: list[Replay]) -> list[Replay]:
    if args.games:
        return [resolve(g, replays, args.dir) for g in args.games]
    chosen = [r for r in replays if matches(r, args)]
    return chosen[:args.last] if args.last else chosen


def interactive(args, replays: list[Replay]) -> None:
    print_list(replays, replays, 15)
    while True:
        try:
            spec = input("\nWhich game? (number or part of the name; Enter to quit) ").strip()
            if not spec:
                return
            replay = resolve(spec, replays, args.dir)
            print(f"{replay.path.name}")
            action = input("[s]ummary, [w]atch and comment, [c]omment, or Enter to pick another: ").strip().lower()
        except EOFError:
            return
        except SystemExit as e:
            print(e)
            continue
        if action.startswith("s"):
            print(summary_for(replay))
        elif action.startswith("w"):
            at = input("Start at (mm:ss, 'decisive', 'fight N', or Enter for the beginning): ").strip() or None
            watch(replay, at, args.by, False)
        elif action.startswith("c"):
            comment(replay, None, args.by)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dir", type=Path, default=REPLAYS, help="folder of replays (default build/test/replays)")
    parser.add_argument("--by", default=getpass.getuser(), help="name to put on comments (default: your login)")
    commands = parser.add_subparsers(dest="command")

    def filters(sub, games: bool):
        if games:
            sub.add_argument("games", nargs="*", metavar="GAME", help="list number, part of the file name, or path")
        sub.add_argument("--bot", help="only games played by this bot")
        sub.add_argument("--opponent", help="only games against this opponent")
        sub.add_argument("--result", choices=["won", "lost", "draw", "WON", "LOST", "DRAW"])
        sub.add_argument("--map", help="only games on this map")

    sub = commands.add_parser("list", help="list the replays, newest first")
    filters(sub, False)
    sub.add_argument("--last", type=int, default=30, help="how many to show (default 30; 0 for all)")
    sub = commands.add_parser("summary", help="write and show the highlights of games")
    filters(sub, True)
    sub.add_argument("--last", type=int, default=0, help="only the newest N games matching the filters")
    sub.add_argument("--refresh", action="store_true", help="rebuild summaries even if they're up to date")
    sub = commands.add_parser("watch", help="play a replay in a window, then comment on it")
    sub.add_argument("game", metavar="GAME")
    sub.add_argument("--at", help="where to start: mm:ss, 'decisive' or 'fight N' (a fight in the summary)")
    sub.add_argument("--refresh", action="store_true")
    sub = commands.add_parser("comment", help="add a comment on a game without watching it")
    sub.add_argument("game", metavar="GAME")
    sub.add_argument("--text", help="the comment (otherwise it's typed in)")
    sub = commands.add_parser("comments", help="show what observers said")
    filters(sub, True)
    sub.add_argument("--last", type=int, default=0)
    args = parser.parse_args()

    if not args.dir.is_dir():
        sys.exit(f"{args.dir} doesn't exist; the test harness saves replays there when games are played")
    replays = find_replays(args.dir)

    if args.command is None:
        interactive(args, replays)
    elif args.command == "list":
        print_list([r for r in replays if matches(r, args)], replays, args.last)
    elif args.command == "summary":
        chosen = selected(args, replays)
        if not args.games and not any((args.bot, args.opponent, args.result, args.map, args.last)):
            chosen = replays[:1]
        if len(chosen) == 1:
            print(summary_for(chosen[0], args.refresh))
        else:
            for replay in chosen:
                try:
                    text = summary_for(replay, args.refresh)
                except RuntimeError as e:
                    print(f"{replay.path.name}: couldn't be read ({e})")
                    continue
                print(f"- {headline(text)}\n  {shown_path(replay.summary_path())}")
    elif args.command == "watch":
        watch(resolve(args.game, replays, args.dir), args.at, args.by, args.refresh)
    elif args.command == "comment":
        comment(resolve(args.game, replays, args.dir), args.text, args.by)
    elif args.command == "comments":
        chosen = selected(args, replays) if (args.games or any((args.bot, args.opponent, args.result, args.map))) \
            else replays
        found = [r for r in chosen if r.comments_path().is_file()]
        for replay in found:
            print(replay.comments_path().read_text())
        if not found:
            print("No comments yet.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

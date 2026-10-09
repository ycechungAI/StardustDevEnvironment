"""Watch bots play each other: every pairing of the bots given plays one game at once, each in its own game window,
arranged in a grid that fits the screen, with a details window beside them.

Usage: python tools/watch.py BOT_A BOT_B [BOT_C [BOT_D]] [--speed 2] [--build build-ui]
       python tools/watch.py --view <status files>     (only the details window, for games already played)

Two bots play 1 game (A vs B), three play 3 (A-B, A-C, B-C) in a row of three windows, four play 6
(A-B, A-C, A-D, B-C, B-D, C-D) in two rows of three. StardustPy is the Python port; any bot from bots/ or the
harness's built-in opponents can play (see tests --gtest_filter=Bots.List).

The details window has a tab per game:
- a toolbar for each bot: minerals, gas, supply (used/available), workers and army units;
- what each bot is building, training, researching and upgrading right now, with progress;
- events: buildings started and finished, research and upgrades, buildings under attack, units lost;
- when the game ends, a results screen: victory or defeat, resources collected, units and structures produced,
  lost and killed, peaks, and graphs of workers, army supply and resources over the game.

--speed 1 is StarCraft's normal (Fastest) speed, 2 twice that, 0 as fast as the bots allow. Needs the window build
(cmake -S . -B build-ui -DOPENBW_ENABLE_UI=ON) with the MPQs in build-ui/test/. Closing the details window stops the
games.
"""

import argparse
import itertools
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import tkinter as tk
import tkinter.font as tkfont
from dataclasses import dataclass
from pathlib import Path
from tkinter import ttk

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_feed import (FeedReader, GameModel, PlayerStats, army_units, game_time)  # noqa: E402
from run_games import prepare_worker_directory  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PYTHON_PORT = "StardustPy"

GAME_WIDTH = 800  # the game window's own size: the 800x600 view and its 146-pixel HUD panel
GAME_HEIGHT = 600 + 146
HUD_MIN_SCALE = 0.6  # smaller than this, the window's HUD panel is unreadable and left out (the details window has it)
TITLE_BAR = 28  # macOS window title bar
MENU_BAR = 25  # macOS menu bar
DOCK = 70  # room left at the bottom for the Dock
DETAILS_WIDTH = 460

# Brood War's player colors, by color index
PLAYER_COLORS = ["#f40404", "#0c48cc", "#2cb494", "#88409c", "#f88c14", "#703014", "#cce0d0", "#fcfc38"]
BACKGROUND = "#16181d"
PANEL = "#22252c"
TEXT = "#e4e6eb"
DIM = "#8c929e"
MINERALS = "#6ebeff"
GAS = "#6ee182"
SUPPLY = "#f0c85a"


# ---------------------------------------------------------------------------------------------------------------------
# Layout


@dataclass
class Placement:
    x: int
    y: int  # top of the window's content (below its title bar)
    scale: float
    hud: bool = True


def grid_shape(games: int) -> tuple[int, int]:
    """(rows, columns): 1 game alone, 2 side by side, 3 in a row, 4 to 6 in two rows of three."""
    if games <= 3:
        return 1, max(1, games)
    return 2, 3


def layout(games: int, screen_width: int, screen_height: int, details: bool) -> tuple[list[Placement], Placement]:
    """Where each game window goes, and the details window (its scale field unused), on a screen of this size."""
    usable_height = screen_height - MENU_BAR - DOCK
    grid_width = screen_width - (DETAILS_WIDTH if details else 0)
    rows, columns = grid_shape(games)
    tile_width = grid_width // columns
    tile_height = usable_height // rows
    scale = min(1.0, tile_width / GAME_WIDTH, (tile_height - TITLE_BAR) / GAME_HEIGHT)
    hud = scale >= HUD_MIN_SCALE
    if not hud:
        scale = min(1.0, tile_width / GAME_WIDTH, (tile_height - TITLE_BAR) / (GAME_HEIGHT - 146))
    placements = []
    for index in range(games):
        row, column = divmod(index, columns)
        placements.append(Placement(column * tile_width, MENU_BAR + row * tile_height + TITLE_BAR, scale, hud))
    details_x = min(grid_width, columns * int(GAME_WIDTH * scale))
    return placements, Placement(details_x, MENU_BAR + TITLE_BAR, 1.0)


def pairings(bots: list[str]) -> list[tuple[str, str]]:
    """Every pairing, in order (A-B, A-C, A-D, B-C, ...). The Python port can only play as "us", the first bot."""
    games = []
    for a, b in itertools.combinations(bots, 2):
        games.append((b, a) if b == PYTHON_PORT else (a, b))
    return games


def ms_per_frame(speed: float) -> int:
    return 0 if speed <= 0 else max(1, round(42 / speed))


# ---------------------------------------------------------------------------------------------------------------------
# Games


@dataclass
class Game:
    index: int
    us: str
    opponent: str
    status_file: Path
    process: subprocess.Popen[bytes] | None = None
    log: Path | None = None

    @property
    def title(self) -> str:
        return f"[{self.index}] {self.us} vs {self.opponent}"


def launch(games: list[Game], placements: list[Placement], test_dir: Path, speed: float) -> Path:
    socket_root = Path(tempfile.mkdtemp(prefix="ob", dir="/tmp"))
    for game, place in zip(games, placements):
        directory = prepare_worker_directory(test_dir, game.index - 1)
        game.status_file = directory / "status.jsonl"
        game.status_file.write_text("")
        env = dict(os.environ)
        env.update({
            "STARDUST_OPPONENT": game.opponent,
            "STARDUST_GAMES": "1",
            "OPENBW_LOCAL_AUTO_DIRECTORY": str(socket_root / str(game.index)),
            "OPENBW_STATUS_FILE": str(game.status_file),
            "OPENBW_WINDOW_TITLE": game.title,
            "OPENBW_WINDOW_X": str(place.x),
            "OPENBW_WINDOW_Y": str(place.y),
            "OPENBW_WINDOW_SCALE": f"{place.scale:.3f}",
            "OPENBW_HUD": "1" if place.hud else "0",
        })
        env.pop("STARDUST_BOT", None)
        env.pop("OPENBW_ENABLE_UI", None)
        if game.us != PYTHON_PORT:
            env["STARDUST_BOT"] = game.us
        frame_ms = ms_per_frame(speed)
        if frame_ms:
            env["OPENBW_GAME_SPEED"] = str(frame_ms)
        else:
            env.pop("OPENBW_GAME_SPEED", None)
        game.log = directory / "watch.log"
        with game.log.open("w") as output:
            game.process = subprocess.Popen([str(test_dir / "tests"), "--gtest_filter=Bots.Play"], cwd=directory,
                                            env=env, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        print(f"{game.title}: window at {place.x},{place.y}, {place.scale:.0%} size; log {game.log}", flush=True)
    return socket_root


def stop(games: list[Game]) -> None:
    for game in games:
        if game.process and game.process.poll() is None:
            try:
                os.killpg(game.process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass


def screen_size(root: tk.Tk, override: str | None) -> tuple[int, int]:
    if override:
        width, height = override.lower().split("x")
        return int(width), int(height)
    return root.winfo_screenwidth(), root.winfo_screenheight()


# ---------------------------------------------------------------------------------------------------------------------
# Details window


class GameTab:
    """One game's tab: live details while it plays, then its results."""

    def __init__(self, notebook: ttk.Notebook, title: str, reader: FeedReader, fonts: dict[str, tkfont.Font]) -> None:
        self.reader = reader
        self.fonts = fonts
        self.frame = tk.Frame(notebook, bg=BACKGROUND)
        notebook.add(self.frame, text=title)
        self.title = title
        self.showing_results = False

        self.header = tk.Label(self.frame, text="waiting for the game to start...", bg=BACKGROUND, fg=TEXT,
                               font=fonts["title"], anchor="w")
        self.header.pack(fill="x", padx=10, pady=(8, 4))
        self.toolbar = tk.Frame(self.frame, bg=PANEL)
        self.toolbar.pack(fill="x", padx=10)
        self.production = tk.Frame(self.frame, bg=BACKGROUND)
        self.production.pack(fill="x", padx=10, pady=6)
        tk.Label(self.frame, text="Events", bg=BACKGROUND, fg=DIM, font=fonts["small"], anchor="w").pack(fill="x", padx=10)
        self.log = tk.Text(self.frame, bg=PANEL, fg=TEXT, font=fonts["small"], height=14, relief="flat",
                           wrap="word", state="disabled")
        self.log.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        self.results: tk.Frame | None = None

    def color_of(self, slot: int) -> str:
        player = self.reader.model.player(slot)
        index = int(player.get("color", 0)) if player else 0
        return PLAYER_COLORS[index] if 0 <= index < len(PLAYER_COLORS) else TEXT

    def update(self) -> None:
        events = self.reader.poll()
        model = self.reader.model
        if model.end and not self.showing_results:
            self.show_results(model)
            return
        if self.showing_results or not model.players:
            return
        self.header.config(text=f"{game_time(model.frame)}   frame {model.frame}")
        self.draw_toolbar(model)
        self.draw_production(model)
        if events:
            self.log.config(state="normal")
            for event in events:
                tag = f"p{event.slot}"
                self.log.tag_config(tag, foreground=self.color_of(event.slot))
                player = model.player(event.slot)
                name = player["name"] if player else "?"
                self.log.insert("end", f"{game_time(event.frame):>6}  ", "time")
                self.log.insert("end", f"{name}: ", tag)
                self.log.insert("end", event.text + "\n")
            self.log.tag_config("time", foreground=DIM)
            self.log.see("end")
            self.log.config(state="disabled")

    def draw_toolbar(self, model: GameModel) -> None:
        for child in self.toolbar.winfo_children():
            child.destroy()
        for player in model.players:
            row = tk.Frame(self.toolbar, bg=PANEL)
            row.pack(fill="x", padx=6, pady=3)
            tk.Frame(row, bg=self.color_of(player["slot"]), width=12, height=12).pack(side="left", padx=(0, 6))
            label = f"{player['name']} ({player['race']})" + ("  *" if player.get("local") else "")
            tk.Label(row, text=label, fg=TEXT, bg=PANEL, font=self.fonts["bold"], width=24, anchor="w").pack(side="left")
            supply = player.get("supply", [0, 0])
            for text, color in ((f"M {player['minerals']}", MINERALS), (f"G {player['gas']}", GAS),
                                (f"S {supply[0]}/{supply[1]}", SUPPLY), (f"W {player['workers']}", DIM),
                                (f"Army {army_units(player)}", TEXT)):
                tk.Label(row, text=text, fg=color, bg=PANEL, font=self.fonts["mono"], padx=6).pack(side="left")

    def draw_production(self, model: GameModel) -> None:
        for child in self.production.winfo_children():
            child.destroy()
        for player in model.players:
            box = tk.Frame(self.production, bg=BACKGROUND)
            box.pack(fill="x", pady=(0, 6))
            tk.Label(box, text=f"{player['name']} is working on", fg=self.color_of(player["slot"]), bg=BACKGROUND,
                     font=self.fonts["small"], anchor="w").pack(fill="x")
            items = sorted(player.get("production", []), key=lambda item: -float(item["progress"]))
            if not items:
                tk.Label(box, text="  nothing", fg=DIM, bg=BACKGROUND, font=self.fonts["mono"], anchor="w").pack(fill="x")
            for item in items[:6]:
                progress = max(0.0, min(1.0, float(item["progress"])))
                verb = {"build": "building", "train": "training", "morph": "morphing", "research": "researching",
                        "upgrade": "upgrading"}.get(item["kind"], item["kind"])
                queued = f"  (+{item['queued']} queued)" if item.get("queued") else ""
                line = tk.Frame(box, bg=BACKGROUND)
                line.pack(fill="x")
                bar = tk.Canvas(line, width=70, height=10, bg=PANEL, highlightthickness=0)
                bar.create_rectangle(0, 0, int(70 * progress), 10, fill=self.color_of(player["slot"]), width=0)
                bar.pack(side="left", padx=(8, 6), pady=3)
                tk.Label(line, text=f"{progress:4.0%}  {verb} {item['name']}{queued}", fg=TEXT, bg=BACKGROUND,
                         font=self.fonts["mono"], anchor="w").pack(side="left")
            if len(items) > 6:
                tk.Label(box, text=f"  and {len(items) - 6} more", fg=DIM, bg=BACKGROUND, font=self.fonts["mono"],
                         anchor="w").pack(fill="x")

    def show_results(self, model: GameModel) -> None:
        """The end-of-game screen, in place of the live details."""
        self.showing_results = True
        for widget in (self.header, self.toolbar, self.production):
            widget.pack_forget()
        self.log.pack_forget()
        for child in self.frame.winfo_children():
            if isinstance(child, tk.Label):
                child.pack_forget()
        results = tk.Frame(self.frame, bg=BACKGROUND)
        results.pack(fill="both", expand=True, padx=10, pady=10)
        self.results = results

        players = model.players
        banner = tk.Frame(results, bg=BACKGROUND)
        banner.pack(fill="x")
        for player in players:
            outcome = model.result_for(player["slot"]) or ""
            color = {"VICTORY": "#4cd964", "DEFEAT": "#ff5a5f", "DRAW": SUPPLY}.get(outcome, TEXT)
            cell = tk.Frame(banner, bg=BACKGROUND)
            cell.pack(side="left", expand=True, fill="x")
            tk.Label(cell, text=outcome, fg=color, bg=BACKGROUND, font=self.fonts["banner"]).pack()
            tk.Label(cell, text=f"{player['name']} ({player['race']})", fg=self.color_of(player["slot"]),
                     bg=BACKGROUND, font=self.fonts["bold"]).pack()
        end = model.end or {}
        tk.Label(results, text=f"Game length {game_time(model.frame)}   on {end.get('map', '?')}", fg=DIM,
                 bg=BACKGROUND, font=self.fonts["small"]).pack(pady=(4, 8))

        table = tk.Frame(results, bg=PANEL)
        table.pack(fill="x")
        rows: list[tuple[str, list[str]]] = []

        def stat(slot: int) -> PlayerStats:
            return model.stats.get(slot, PlayerStats())

        rows.append(("Resources collected", [f"{p['gathered'][0]} / {p['gathered'][1]}" for p in players]))
        rows.append(("Units produced", [str(stat(p["slot"]).produced_units) for p in players]))
        rows.append(("Units killed", [str(stat(p["slot"]).killed_units) for p in players]))
        rows.append(("Units lost", [str(stat(p["slot"]).lost_units) for p in players]))
        rows.append(("Structures built", [str(stat(p["slot"]).produced_buildings) for p in players]))
        rows.append(("Structures razed", [str(stat(p["slot"]).killed_buildings) for p in players]))
        rows.append(("Structures lost", [str(stat(p["slot"]).lost_buildings) for p in players]))
        rows.append(("Peak supply", [str(stat(p["slot"]).peak_supply) for p in players]))
        rows.append(("Peak workers", [str(stat(p["slot"]).peak_workers) for p in players]))
        rows.append(("Peak army supply", [str(stat(p["slot"]).peak_army_supply) for p in players]))
        tk.Label(table, text="", bg=PANEL).grid(row=0, column=0)
        for column, player in enumerate(players, 1):
            tk.Label(table, text=player["name"], fg=self.color_of(player["slot"]), bg=PANEL,
                     font=self.fonts["bold"]).grid(row=0, column=column, padx=10, pady=2)
        for row, (name, values) in enumerate(rows, 1):
            tk.Label(table, text=name, fg=DIM, bg=PANEL, font=self.fonts["small"], anchor="w").grid(
                row=row, column=0, sticky="w", padx=10, pady=1)
            for column, value in enumerate(values, 1):
                tk.Label(table, text=value, fg=TEXT, bg=PANEL, font=self.fonts["mono"]).grid(row=row, column=column,
                                                                                            padx=10)

        for title, attribute in (("Workers", "workers"), ("Army supply", "army_supply"),
                                 ("Resources collected", "gathered")):
            self.draw_graph(results, model, title, attribute)

    def draw_graph(self, parent: tk.Frame, model: GameModel, title: str, attribute: str) -> None:
        tk.Label(parent, text=title, fg=DIM, bg=BACKGROUND, font=self.fonts["small"], anchor="w").pack(fill="x",
                                                                                                    pady=(8, 0))
        width, height, margin = DETAILS_WIDTH - 40, 90, 4
        canvas = tk.Canvas(parent, width=width, height=height, bg=PANEL, highlightthickness=0)
        canvas.pack(fill="x")
        series = {slot: [(s.frame, getattr(s, attribute)) for s in samples] for slot, samples in model.history.items()}
        last_frame = max((points[-1][0] for points in series.values() if points), default=0)
        top = max((value for points in series.values() for _, value in points), default=0)
        if last_frame <= 0 or top <= 0:
            return
        canvas.create_text(width - margin, margin, text=str(top), anchor="ne", fill=DIM, font=self.fonts["small"])
        for slot, points in series.items():
            coords: list[float] = []
            for frame, value in points:
                coords += [margin + (width - 2 * margin) * frame / last_frame,
                           height - margin - (height - 2 * margin) * value / top]
            if len(coords) >= 4:
                canvas.create_line(*coords, fill=self.color_of(slot), width=2)


class DetailsWindow:
    def __init__(self, root: tk.Tk, titles: list[str], files: list[Path], place: Placement | None,
                 height: int) -> None:
        self.root = root
        root.title("Game details")
        root.configure(bg=BACKGROUND)
        if place:
            root.geometry(f"{DETAILS_WIDTH}x{height}+{place.x}+{place.y - TITLE_BAR}")
        family = "Menlo" if sys.platform == "darwin" else "DejaVu Sans Mono"
        self.fonts = {
            "mono": tkfont.Font(family=family, size=11),
            "small": tkfont.Font(family=family, size=10),
            "bold": tkfont.Font(family=family, size=11, weight="bold"),
            "title": tkfont.Font(family=family, size=13, weight="bold"),
            "banner": tkfont.Font(family=family, size=20, weight="bold"),
        }
        style = ttk.Style(root)
        style.configure("TNotebook", background=BACKGROUND)
        notebook = ttk.Notebook(root)
        notebook.pack(fill="both", expand=True)
        self.tabs = [GameTab(notebook, title, FeedReader(path), self.fonts) for title, path in zip(titles, files)]

    def refresh(self) -> None:
        for tab in self.tabs:
            tab.update()


# ---------------------------------------------------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("bots", nargs="*", help="2 to 4 bots; every pairing plays one game")
    parser.add_argument("--speed", type=float, default=1.0,
                        help="game speed: 1 is normal (Fastest), 2 twice that, 0 as fast as the bots allow")
    parser.add_argument("--build", default="build-ui", help="window build directory (default: build-ui)")
    parser.add_argument("--screen", help="screen size in points, e.g. 1512x982 (default: detected)")
    parser.add_argument("--no-details", action="store_true", help="no details window")
    parser.add_argument("--view", nargs="+", type=Path, metavar="STATUS_FILE",
                        help="only show the details of games already played (their status.jsonl files)")
    args = parser.parse_args()

    root = tk.Tk()
    width, height = screen_size(root, args.screen)

    if args.view:
        window = DetailsWindow(root, [path.parent.name for path in args.view], args.view, None, 0)
        root.geometry(f"{DETAILS_WIDTH}x{min(900, height - MENU_BAR - DOCK)}")

        def refresh_view() -> None:
            window.refresh()
            root.after(500, refresh_view)

        refresh_view()
        root.mainloop()
        return 0

    if not 2 <= len(args.bots) <= 4:
        parser.error("give 2 to 4 bots")
    if len(set(args.bots)) != len(args.bots):
        parser.error("give each bot once")
    test_dir = ROOT / args.build / "test"
    if not (test_dir / "tests").exists():
        parser.error(f"no test harness in {test_dir}: build the window build first")

    games = [Game(index, us, opponent, Path()) for index, (us, opponent) in enumerate(pairings(args.bots), 1)]
    placements, details_place = layout(len(games), width, height, not args.no_details)
    rows, columns = grid_shape(len(games))
    speed = f"{args.speed:g}x speed" if args.speed > 0 else "full speed"
    print(f"{len(games)} game(s) at once, {rows}x{columns} grid on a {width}x{height} screen, {speed}", flush=True)
    socket_root = launch(games, placements, test_dir, args.speed)

    if args.no_details:
        root.withdraw()
    window = DetailsWindow(root, [game.title for game in games], [game.status_file for game in games],
                           None if args.no_details else details_place, height - MENU_BAR - DOCK - TITLE_BAR)
    start = time.time()
    finished: set[int] = set()

    def tick() -> None:
        window.refresh()
        for game in games:
            if game.index in finished or not game.process or game.process.poll() is None:
                continue
            finished.add(game.index)
            model = window.tabs[game.index - 1].reader.model
            result = (model.end or {}).get("result", f"no result (exit {game.process.returncode}; see {game.log})")
            print(f"{game.title}: {result} after {game_time(model.frame)} ({time.time() - start:.0f} s)", flush=True)
        if len(finished) == len(games) and args.no_details:
            root.quit()
            return
        root.after(500, tick)

    def close() -> None:
        stop(games)
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", close)
    tick()
    try:
        root.mainloop()
    except KeyboardInterrupt:
        pass
    finally:
        stop(games)
        shutil.rmtree(socket_root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())

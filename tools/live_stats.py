"""Shows the games being played, live, in one browser window with a tab per game (and one with all of them at once):
minerals, gas, supply, workers, army, units and buildings for both players, with charts over game time. Works the
same for headless games and window games.

Each game's harness writes live.json in its working folder about twice a second (LiveStats in test/GameStats.h).
tools/run_games.py starts this viewer itself and prints its address (--live also opens it). On its own it watches
any folders:

    python tools/live_stats.py                        # every live.json under build*/test, as games write them
    python tools/live_stats.py build/test/parallel --open
"""

import argparse
import json
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
PAGE = Path(__file__).resolve().parent / "live_stats.html"
DEFAULT_PORT = 8765
SCAN_SECONDS = 0.5
STALE_SECONDS = 15  # a game whose file hasn't changed for this long has stopped (killed, or hung)
ACTIVE = ("playing", "paused")  # states of a game still going (a paused game keeps rewriting its file)
KEEP_FINISHED = 60  # finished games kept for their tabs
HISTORY_POINTS = 400  # chart points kept per game; older ones are thinned out


class LiveGames:
    """Reads the live.json files and keeps every game seen, with a history of its numbers for the charts."""

    def __init__(self, sources: list[Path], max_age: float | None = None) -> None:
        self.sources = sources  # live.json files, or folders to search for them
        self.max_age = max_age  # ignore files older than this many seconds when first seen (old runs' leftovers)
        self.games: dict[str, dict[str, Any]] = {}
        self.run: dict[str, Any] = {}
        self.lock = threading.Lock()
        self.started = time.time()

    def files(self) -> list[Path]:
        found = []
        for source in self.sources:
            if source.is_dir():
                found += source.rglob("live.json")
            elif source.exists():
                found.append(source)
        return found

    def scan(self) -> None:
        now = time.time()
        for path in self.files():
            try:
                modified = path.stat().st_mtime
                data = json.loads(path.read_text())
            except (OSError, ValueError):
                continue  # being replaced, or gone
            key = f"{path.parent}|{data.get('pid')}|{data.get('game')}|{data.get('seed')}"
            with self.lock:
                game = self.games.get(key)
                if game is None:
                    if self.max_age is not None and now - modified > self.max_age:
                        continue
                    players = data.get("players") or [{}, {}]
                    names = " vs ".join(p.get("name", "?") for p in players)
                    game = {"id": key, "folder": path.parent.name, "label": names, "first": now,
                            "number": len(self.games) + 1, "history": [], "speed": 0.0}
                    self.games[key] = game
                    # A newer game in the same folder means the one before it there has ended
                    for other in self.games.values():
                        if other is not game and other["folder"] == game["folder"] and other.get("path") == str(path) \
                                and other["data"].get("state") in ACTIVE:
                            other["data"]["state"] = "stopped"
                game["path"] = str(path)
                previous = game.get("data")
                game["data"] = data
                game["seen"] = now
                game["modified"] = modified
                frame = data.get("frame", 0)
                if previous and data.get("wallSeconds", 0) > previous.get("wallSeconds", 0):
                    seconds = data["wallSeconds"] - previous["wallSeconds"]
                    rate = (frame - previous.get("frame", 0)) / 24 / seconds
                    game["speed"] = rate if not game["speed"] else 0.7 * game["speed"] + 0.3 * rate
                history = game["history"]
                if not history or frame > history[-1][0]:
                    me, them = (data.get("players") or [{}, {}])[:2]
                    history.append([frame] + [p.get(k, 0) for p in (me, them)
                                              for k in ("workers", "armySupply", "minerals", "gas",
                                                        "mineralsGathered", "gasGathered", "units")])
                    if len(history) > HISTORY_POINTS:
                        del history[1:len(history) - HISTORY_POINTS // 2:2]  # thin out the older half
        with self.lock:
            for game in self.games.values():
                data = game["data"]
                if data.get("state") in ACTIVE and now - game["modified"] > STALE_SECONDS:
                    data["state"] = "stopped"
            finished = [k for k, g in self.games.items() if g["data"].get("state") not in ACTIVE]
            for key in sorted(finished, key=lambda k: self.games[k]["seen"])[:-KEEP_FINISHED or None]:
                del self.games[key]

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return {"now": time.time(), "run": dict(self.run),
                    "games": sorted(self.games.values(), key=lambda g: g["number"])}


class Viewer:
    """The live stats page, served on this computer only (127.0.0.1)."""

    def __init__(self, sources: list[Path], port: int = DEFAULT_PORT, max_age: float | None = None) -> None:
        self.games = LiveGames(sources, max_age)
        games = self.games

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self) -> None:  # noqa: N802 (http.server's name)
                if self.path.startswith("/data"):
                    body = json.dumps(games.snapshot()).encode()
                    kind = "application/json"
                elif self.path in ("/", "/index.html"):
                    body = PAGE.read_bytes()
                    kind = "text/html; charset=utf-8"
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Type", kind)
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args: Any) -> None:
                pass

        self.server: ThreadingHTTPServer | None = None
        for candidate in range(port, port + 20):
            try:
                self.server = ThreadingHTTPServer(("127.0.0.1", candidate), Handler)
                break
            except OSError:
                continue
        if self.server is None:
            raise OSError(f"no free port from {port} to {port + 19}")
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}/"
        self.stopping = threading.Event()

    def start(self) -> str:
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        threading.Thread(target=self._scan_loop, daemon=True).start()
        return self.url

    def _scan_loop(self) -> None:
        while not self.stopping.is_set():
            try:
                self.games.scan()
            except Exception as error:  # keep watching: a bad file must not stop the viewer
                print(f"live stats: {error}", file=sys.stderr)
            self.stopping.wait(SCAN_SECONDS)

    def set_run(self, **info: Any) -> None:
        """The run's own progress (games done, at once, results...), shown above the games."""
        with self.games.lock:
            self.games.run.update(info)

    def open(self) -> None:
        webbrowser.open(self.url)

    def stop(self) -> None:
        self.stopping.set()
        self.games.scan()  # the last numbers, so the page shows how the games ended
        if self.server is not None:
            self.server.shutdown()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sources", nargs="*", help="folders to search for live.json, or the files (default: "
                                                   "every build*/test folder)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--open", action="store_true", help="open the page in the browser")
    parser.add_argument("--max-age", type=float, default=300,
                        help="leave out games whose file is older than this many seconds (default 300)")
    args = parser.parse_args()
    sources = [Path(s) for s in args.sources] or [d / "test" for d in ROOT.glob("build*") if (d / "test").is_dir()]
    viewer = Viewer(sources, args.port, args.max_age)
    url = viewer.start()
    print(f"Live stats at {url} (watching {', '.join(str(s) for s in sources)}); Ctrl-C to stop", flush=True)
    if args.open:
        viewer.open()
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        viewer.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())

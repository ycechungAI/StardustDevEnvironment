# Stardust (Python)

A StarCraft: Brood War bot written in Python, built on [StardustDevEnvironment](https://github.com/bmnielsen/StardustDevEnvironment), the development environment of the [Stardust](https://github.com/bmnielsen/Stardust) bot.

## Changes made by Ycechung

This fork ([ycechungAI](https://github.com/ycechungAI)) adds the following to Bruce Nielsen's dev environment (October 2026):

- **A Python port of Stardust.** BWAPI bindings for Python (pybind11), generated from the BWAPI headers with a type stub for editors and mypy. A C++ host embeds CPython in the bot, and Stardust's logic is ported to `python/stardust/`. pytest and mypy check it without StarCraft.
- **Game windows.** OpenBW shows each game in a window with a toolbar (army supply, minerals, gas). Keys save the replay so far, pause the game, change its speed or show a stats screen with both players' Elo.
- **Many games at once.** `tools/run_games.py` plays 4 games at once by default (up to 6 with `--ui 6`), each in its own 640x480 window, tiled on the screen. `--ui none` plays them headless instead. When memory runs short, fewer games play at once. A moving progress line in the terminal shows the elapsed and remaining time. Window drawing was reworked so that a window holds up its game for less time.
- **Live stats.** `tools/live_stats.py` is a window of its own with a tab for each game being played, plus one tab with all of them. It shows minerals, gas, supply, workers and army, live, with charts. It works for both headless and window games.
- **Opponent bots.** `bots/` holds about twenty bots to play against: recent AIIDE tournament bots, Tier 2 bots that each play one unusual strategy, and stand-ins for the computer players. Some are recipes that `tools/fetch_bot.py` downloads and patches. `--bot` plays as any of them. See [bots/README.md](bots/README.md).
- **Results, Elo and replays.**
  - Every game goes into `results.csv` with resources mined and units killed and lost, and `tools/elo.py` rates the bots from it.
  - `tools/ladder.py` plays a bot up the ladder of opponents.
  - `tools/replays.py` summarises a replay (fights, build orders, problems), plays it in a window and keeps viewers' comments.
- **AI-written bots.** `ClaudeOpus55` is a Protoss bot whose strategy and code are by Claude Opus 5.5, under rules adapted from the StarSkirmish benchmark. It beats 5 of the 7 Tier 2 bots. The prompts and every piece of human advice given are listed in [bots/genai/README.md](bots/genai/README.md). The `claudeopus55-rl` branch trains it further with headless self-play (`tools/selfplay.py`).
- **A sturdier harness.**
  - A hang watchdog records why a game got stuck, then ends the game.
  - Exits are crash-safe, and a fix tears down the sync server cleanly.
  - New settings: a fixed seed for replaying a game (`STARDUST_TEST_SEED`), a unit-count printout (`STARDUST_OBSERVE`), and a way to keep practice games out of the ratings (`STARDUST_NO_RESULTS`).
  - Build fixes for Linux and for current macOS toolchains.

The bot itself is Python (`python/stardust/`). A small C++ host embeds CPython in a BWAPI AI module and exposes the complete BWAPI API to it. It keeps the rest of the dev environment:

- [OpenBW](http://www.openbw.com/) as the game engine.
- [Steamhammer](http://satirist.org/ai/starcraft/steamhammer/) and [Locutus](https://github.com/bmnielsen/Locutus) as test opponents.
- The googletest-based game harness.
- [CherryVis](https://torchcraft.github.io/TorchCraftAI/blog/2019/02/20/releasing-cherryvis.html) instrumentation.

```
test/tests (C++ harness: runs an OpenBW game, opponent in a forked process)
 └─ StardustAI  (C++: PythonAIModule, a BWAPI::AIModule)
     └─ embedded CPython
         ├─ bwapi            BWAPI bindings (pybind11, generated from the BWAPI headers)
         ├─ instrumentation  Log + CherryVis
         └─ python/stardust  the bot
```

## Setup

You need:

- CMake 3.25+ and a C++17 compiler.
- Python 3.12+; [uv](https://docs.astral.sh/uv/) is recommended.
- The three Brood War data files from StarCraft 1.16.1: `STARDAT.MPQ`, `BROODAT.MPQ` and `Patch_rt.mpq`.

`./build.sh` does all of the steps below, and also downloads the opponent bots that are only recipes (`JOBS=8 ./build.sh` for 8 build jobs instead of 4). Or by hand:

```bash
uv sync
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

`uv sync` creates `.venv`. CMake builds against that interpreter, and the bot can import any package installed in it (numpy etc.).

Copy the three MPQ files into `build/test/`, next to the `tests` binary. Without them, game tests fail with `failed to open ./Patch_rt.mpq`.

Tested on macOS (Apple Silicon, Apple clang 21, CMake 4). The Windows/MSVC configuration from upstream has not been tried with the Python host.

## Running games

```bash
cd build/test && ./tests --gtest_filter=Steamhammer.4PoolHard
```

The test harness, maps and opponents are described in `test/`: `Steamhammer.cpp`, `Locutus.cpp`, and `RushDefense.cpp` for a scripted scenario. Replays, CherryVis data and logs go to `build/test/replays/`.

To play other bots, put them in `bots/` (see [bots/README.md](bots/README.md)) and run `.venv/bin/python tools/run_games.py --opponent <bot>`. Steamhammer, Locutus, Iron, McRave, the `WorkerRush` example and five recent tournament bots, among them the original C++ Stardust (`Stardust2025`), are available out of the box. `--bot <name>` plays as one of them instead of the Python port.

### Several games at once

`tools/run_games.py` runs the same tests, several at a time, each game in its own window by default:

```bash
.venv/bin/python tools/run_games.py Steamhammer.4PoolHard
```

```bash
.venv/bin/python tools/run_games.py --opponent Stone --games 12
```

- **`--ui N`** plays up to N games at once (1 to 6; 4 by default, as 6 at once slows a 10-core Mac), each in its own 640x480 window. The windows tile from the top left of the screen:
  - 6 games: 3 columns by 2 rows.
  - 4 games: 2 by 2.
  - 2 games: side by side.

  On a screen too small for the grid (1920x1080, say), the windows shrink and keep their shape. `--window-size` changes the size. The windows come from the window build, `build-ui` (see "Watching a game"). Without that build, games play headless and a note says so.
- **`--ui none`** plays the games headless, from `build`. `--parallel N` sets how many play at once (4 by default, up to 6). Self-play training (`tools/selfplay.py`, on the `claudeopus55-rl` branch) plays many games at once, so it always runs headless.
- **Memory.** Fewer games play at once when memory runs short: 6, then 4, 2 or 1. The check runs before starting and every 2 seconds during play. It steps down when less than 2 GB is free, or when the games use more than three quarters of the machine's memory. The newest games are then stopped and played again later. A game that uses over 4 GB has a leaking bot, so it is stopped and not played again.
- **Folders.** Each game is its own test process, so each gets its own random map and seed (unless `STARDUST_TEST_MAP` or `STARDUST_TEST_SEED` is set). The process runs in `<build>/test/parallel/<slot>/` with its own `bwapi-data/write` and writes its output to `run_games.log` there. The maps, data files and replays folder are shared.

While games play, the bottom line of the terminal shows:
- a spinner, a progress bar and moving dots;
- the time elapsed and the time left;
- how many games are playing, and each one's game time.

A `PROGRESS` line is printed every 30 seconds as well (`--interval`), for output that isn't a terminal. For each finished game, the script prints:
- a `GAME` line with the result and the replay's name;
- its `STATS` line;
- its rating changes.

At the end it prints the tally (`DONE`), any failed tests or Python errors, and the Elo leaderboard. Ctrl-C stops every game.

### Live stats

`tools/live_stats.py` shows the games as they play, in one window of its own (a pywebview window, from the dev dependencies; the web browser if pywebview isn't installed). There is a tab for each game and one tab with all of them. Each shows both players' minerals, gas, supply, workers, army, units and buildings, with charts over game time. It works the same for headless and window games.

`tools/run_games.py` starts the page and prints its address (`LIVE stats at http://localhost:...`). The window opens by itself when the games have windows, or with `--live`, and stays open after the run with how the games ended. There is only one: the next run shows its games in the window already open. `--no-live` turns it off. The address also works in a web browser. It uses port 8765, or the next free port after it.

About twice a second, each game's harness writes its numbers to `live.json` in its working folder. On its own, the viewer watches any folders, by default every `live.json` under `build*/test`:

```bash
.venv/bin/python tools/live_stats.py
```

```bash
.venv/bin/python tools/live_stats.py build/test/parallel --open
```

### Settings

Test harness options, as environment variables:

| Variable | |
|---|---|
| `STARDUST_TEST_MAP` | map to play on (a name such as `Benzene`) instead of a random one |
| `STARDUST_TEST_FRAME_LIMIT` | end the game after this many frames, e.g. to look only at startup |
| `STARDUST_TEST_SEED` | random seed for games against other bots (`--opponent`), to replay the same game |
| `STARDUST_OBSERVE` | print both players' unit counts every this many frames, like watching the replay |
| `STARDUST_PROFILE_STARTUP` | write a cProfile of the bot's `onStart` to this file |
| `STARDUST_PROFILE_FRAMES` | write a cProfile of all `onFrame` calls to this file (every 1000 frames and at the end) |
| `STARDUST_LOG_GC` | log Python garbage collections taking at least this many milliseconds to the bot log |
| `STARDUST_HANG_SECONDS` | end a game whose frame hasn't moved for this many seconds. The cause is written to `replays/unfinished/`. `tools/run_games.py` sets 120 |
| `STARDUST_NO_RESULTS` | `1` keeps games out of `results.csv`, and so out of the Elo ratings (for practice and timing runs) |
| `STARDUST_LIVE_FILE` | where the game writes its live stats (`live.json` by default); `0` turns them off |
| `OPENBW_GAME_SPEED` | milliseconds per frame in the game window (42 is StarCraft's "fastest"); by default it runs as fast as the bot allows |
| `OPENBW_HUD` | `0` hides the panel with resources, timer and armies below the game window |

`kill -USR1 <pid>` on a running `tests` process prints the bot's Python stack.

### Watching a game

OpenBW can show the game in a window as it runs. This needs SDL2 (`brew install sdl2`) and a build with the UI enabled. A separate build directory keeps the normal headless build as it is:

```bash
cmake -S . -B build-ui -DCMAKE_BUILD_TYPE=Release -DOPENBW_ENABLE_UI=ON
```

```bash
cmake --build build-ui -j
```

Put the MPQ files in `build-ui/test/` as well. `tools/run_games.py` plays from `build-ui` when it exists, with up to 6 windows tiled on the screen (see "Several games at once"); you can also run `./tests` from `build-ui/test` yourself. Only our bot's game gets a window, not the opponent's.

The window is drawn every 40 ms. The game waits only while the picture is drawn, not while it's put on the screen. Drawing still slows the game a little. Window settings, as environment variables (`tools/run_games.py` sets the size, grid and title):

| Variable | |
|---|---|
| `OPENBW_WINDOW_SIZE` | the window's size, e.g. `640x480` (800x600 by default) |
| `OPENBW_WINDOW_GRID` | `i,columns,rows`: put the window in cell `i` (from 0) of a grid packed into the top left of the screen. The window shrinks to fit its cell |
| `OPENBW_WINDOW_TITLE` | the window's title |
| `OPENBW_UI_DRAW_MS` | milliseconds between draws (40 by default) |
| `OPENBW_UI_TIMING` | `1` prints, every 10 seconds, how long drawing held up the game and how long showing it took |
| `OPENBW_UI_DEFER` | `0` shows the picture while the game waits, as before, for comparison |
| `OPENBW_ENABLE_UI` | `0` plays headless from a window build |

`tools/bench_ui.py` measures what the windows cost. It plays the same 10000-frame game (fixed map and seed, kept out of the results) 6 at a time in windows, 6 at a time headless, and 1 at a time in a window, then prints the frames per second of each. Run it with nothing else busy. On a 10-core M-series Mac, 6 windows played about 15% slower than 6 headless games, and 3.5 times as fast as one window at a time:

```bash
.venv/bin/python tools/bench_ui.py
```

A toolbar along the top of the window shows each player's army supply (with the most it has had this game), minerals and gas.

Below the game view, a panel shows both players live:

- **Toolbar:** the game time (as StarCraft shows it on Fastest) and frame number; then for each player, their minerals (M), gas (G), supply used and available (S) and workers (w).
- **Army table:** for each player, the size of their army (units, supply, and its mineral/gas cost), then its composition by unit type.

The army counts completed combat units and spellcasters, not workers, overlords, buildings, eggs, spider mines or hallucinations. Siege tanks in both modes count together. The player whose bot owns the window is marked `*`. `OPENBW_HUD=0` turns the panel off. The window is 146 pixels taller than the 800×600 game view.

To change the panel's layout without running a game, edit `3rdparty/openbw/openbw/ui/hud.h`, then render it with sample numbers: `cmake --build build --target hud_preview && build/hud_preview hud.ppm`.

### Watching several games at once

`tools/watch.py` plays every pairing of 2 to 4 bots at the same time, each in its own game window, arranged in a grid that fits your screen, with a details window beside them:

```bash
.venv/bin/python tools/watch.py StardustPy BananaBrain McRaveZ CreativeZerg --speed 2
```

- **Grid:** 2 bots play 1 game, 3 bots play 3 games in a row of three, and 4 bots play 6 games in two rows of three: `[1] A-B [2] A-C [3] A-D` above `[4] B-C [5] B-D [6] C-D`. `StardustPy` is the Python port.
- **Window size:** the windows shrink to fit the screen. Below 60% of full size they leave out the HUD panel, which the details window repeats.
- **Speed:** `--speed 1` is normal (Fastest), `2` twice that, and `0` as fast as the bots allow.
- **Details window**, one tab per game:
  - a toolbar for each bot (minerals, gas, supply such as 9/10, workers, army units);
  - what each bot is building, training, morphing, researching and upgrading, with progress bars;
  - an event log: buildings started and finished, research and upgrades, buildings under attack, units lost.
- **End of a game:** its tab turns into a results screen: victory or defeat, game length, resources collected, units and structures produced, killed and lost, peaks, and graphs of workers, army supply and resources over the game.

Closing the details window stops the games. `--screen 1512x982` sets the screen size if the detected one is wrong, and `--no-details` leaves the details window out.

`--view` shows the details window alone, for games already played. Each game's feed is kept in `build-ui/test/parallel/<n>/status.jsonl`:

```bash
.venv/bin/python tools/watch.py --view build-ui/test/parallel/0/status.jsonl
```

The details come from a feed OpenBW writes once per game second when `OPENBW_STATUS_FILE` is set. It reads the game's own state, so it works whichever bots play. `OPENBW_WINDOW_X`, `_Y`, `_SCALE` and `_TITLE` place, size and name a game window.

Keys in the game window:

| Key | |
|---|---|
| `s` | show or hide the stats screen: both players' Elo, race, minerals, gas, supply, workers, resources mined, units killed and lost |
| `r` | save the replay so far, to `replays/<us>_vs_<opponent>_<map>_<seed>_frame<N>_<time>.rep` (the full replay is still saved when the game ends) |
| `space`/`p` | pause |
| `a`/`z` | speed up/slow down |

### Results and Elo

Every game against a named opponent (`Bots.Play`, and the Steamhammer and Locutus tests) is added to `build/test/replays/results.csv` when it ends: who played whom, the result (`WON`, `LOST`, or `DRAW` when the frame or time limit ended it), the map and seed, and both sides' resources mined and units killed and lost. A one-line `STATS` summary is printed at the end of each game too.

`tools/elo.py` rates that history (everyone starts at 1500; K = 32), writes `replays/ratings.json` for the stats screen and prints a leaderboard:

```bash
.venv/bin/python tools/elo.py
```

`tools/run_games.py` does this after every game, printing each game's rating changes and the leaderboard at the end. The Python port is rated as `StardustPy`.

Python is loaded from `python/` in the source tree, so edits to the bot take effect on the next run without rebuilding. At the end of each game the host prints the bot's frame times against the usual tournament limits.

## Tests without StarCraft

```bash
uv run pytest
```

```bash
uv run mypy
```

```bash
cd build/test && ./tests --gtest_filter='PythonHost.*'
```

- **pytest** runs against an offline build of the same bindings (`build/python/bwapi*.so`), so bot logic and BWAPI types (unit stats, positions, tech trees) can be tested without a game.
- **mypy** type-checks the bot against `python/bwapi.pyi`. Editors use the same stub for completion.
- **`PythonHost.*`** checks the C++ host: interpreter startup, callback forwarding and error handling.

## Writing the bot

`stardust.create_bot()` returns the bot object. The host calls its methods for each BWAPI event, using the names from `BWAPI::AIModule`: `onStart`, `onFrame`, `onUnitCreate`, and so on. Methods the bot doesn't define are skipped. The starting point in `python/stardust/bot.py` is the dev environment's demo bot ported to Python: it mines, trains workers and builds supply.

The API mirrors the C++ BWAPI, so the [BWAPI documentation](https://bwapi.github.io/) and C++ bot code translate almost line for line:

| C++ | Python |
|---|---|
| `BWAPI::Broodwar->self()->getUnits()` | `bwapi.Broodwar.self().getUnits()` |
| `u->getType() == UnitTypes::Protoss_Probe` | `u.getType() == UnitTypes.Protoss_Probe` |
| `Unitset`, `UnitType::set`, ... | Python `set` (any iterable is accepted as input) |
| `u->getClosestUnit(IsMineralField)` | `u.getClosestUnit(lambda x: x.getType().isMineralField())` |
| `nullptr` | `None` |
| `Broodwar->drawTextMap(p, "%c%s", Text::Green, s)` | `Broodwar.drawTextMap(p, Text.Green + s)` |
| `UnitTypes::None` | `UnitTypes.None_` (`None` is a Python keyword) |
| `Position(tilePos)`, `if (pos)` | `Position(tilePos)`, `if pos:` (`isValid()`) |
| `u->setClientInfo(...)` | not exposed: keep per-unit state in a dict keyed by unit or `u.getID()` |

Differences from C++:

- `Position`, `WalkPosition` and `TilePosition` are immutable and hashable, so `pos.x += 1` becomes `pos = pos + Position(1, 0)`. In exchange they work as dict keys.
- Units, players and other game objects compare and hash by identity.
- Always read the game through `bwapi.Broodwar`. A `from bwapi import Broodwar` at module import time would capture a stale value.

Instrumentation:

```python
from instrumentation import CherryVis, Log
Log.write("hello")                    # bwapi-data/write/Stardust_log_*.txt
CherryVis.log("attacking", unit)      # per-unit log in the CherryVis replay viewer
```

Environment variables read by the host:

| Variable | Default | |
|---|---|---|
| `STARDUST_BOT` | `stardust:create_bot` | `module:factory` that creates the bot |
| `STARDUST_PYTHON_PATH` | `<repo>/python` | prepended to `sys.path` |
| `VIRTUAL_ENV` | `<repo>/.venv` | its site-packages are importable |
| `STARDUST_PY_ERRORS` | `raise` | `raise`: a Python exception ends the game, with its traceback printed. `log`: print and keep playing |

### Performance

Tournaments (SSCAIT, AIIDE, ...) forfeit a bot that has 320 frames over 55 ms, 10 frames over 1 s, or any frame over 10 s. As of October 2026, a full game against Steamhammer on Tau Cross averages about 10 ms a frame with under 20 frames over 55 ms (Apple Silicon); the slowest frames are the ones where one of our buildings appears, which re-paths every navigation grid. A call into BWAPI from Python costs roughly 0.1–0.4 µs, measured on an Apple Silicon Mac. That is fine for per-unit logic, even across hundreds of units per frame. Heavy computation should stay out of pure Python: combat simulation, pathfinding and map analysis are what Stardust uses BWEM and FAP for in C++. Use numpy, or bind the C++ library next to `bwapi`.

## Regenerating the bindings

The generated `src/python/generated/*.cpp` and `python/bwapi.pyi` files are committed. Regenerate them after changing the BWAPI headers or the generator:

```bash
uv run python tools/gen_bwapi_bindings.py
```

On macOS this uses the Command Line Tools' libclang. It skips methods it can't convert safely and lists them; currently those are the `va_list` variants, harness-only setup calls, a raw framebuffer accessor and one deprecated method.

To check that the stub matches the compiled module:

```bash
PYTHONPATH=build/python MYPYPATH=python uv run python -m mypy.stubtest bwapi instrumentation --allowlist tools/stubtest_allowlist.txt
```

## Changes from upstream StardustDevEnvironment

- The C++ `DemoAIModule` is replaced by `PythonAIModule` (`src/python/`), and the demo is ported to `python/stardust/bot.py`.
- Fixes for current toolchains:
  - CMake 4 policy minimum for the vendored zstd and googletest.
  - `operator""` spacing in `nlohmann/json.hpp`.
  - `std::vector<const std::string>` in Steamhammer.
  - Missing includes in `test/Maps.h`.
  - Removed a hardcoded Homebrew LLVM 13 library path.
- Added the missing definition of `CherryVis::log(BWAPI::Unit)`. It was declared but never defined.
- The log file is now `Stardust_log_*.txt`.

## CherryVis

Replays are annotated for [CherryVis](https://torchcraft.github.io/TorchCraftAI/blog/2019/02/20/releasing-cherryvis.html). The bot shows a heatmap (buildable tiles) and per-unit logs. To run CherryVis, see [cherryvis-docker](https://github.com/bmnielsen/cherryvis-docker).

## Changes to OpenBW

Upstream applied these modifications to OpenBW to integrate it into the environment:

- The BWAPI 4.4 latcom changes have been applied to OpenBW's BWAPI fork.
- The CherryVis OpenBW patch has been applied to support unit creation by triggers.

This fork changes OpenBW's window (`3rdparty/openbw/openbw/ui/` and `OpenBWData/BW/BWData.cpp`):

- A toolbar, the stats screen (`s`) and the save-replay key (`r`).
- The window's size, place on the screen, title and drawing rate come from environment variables (see "Watching a game"). That lets `tools/run_games.py` tile several games.
- The picture is drawn while the game waits, then put on the screen after the game has moved on. Before, the game waited for both.

# Stardust (Python)

A StarCraft: Brood War bot written in Python, built on [StardustDevEnvironment](https://github.com/bmnielsen/StardustDevEnvironment), the development environment of the [Stardust](https://github.com/bmnielsen/Stardust) bot.

The bot itself is Python (`python/stardust/`). A small C++ host embeds CPython in a BWAPI AI module and exposes the complete BWAPI API to it. Everything else from the dev environment is unchanged:

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

Tournaments (SSCAIT, AIIDE, ...) forfeit a bot that has 320 frames over 55 ms, 10 frames over 1 s, or any frame over 10 s. A call into BWAPI from Python costs roughly 0.1–0.4 µs, measured on an Apple Silicon Mac. That is fine for per-unit logic, even across hundreds of units per frame. Heavy computation should stay out of pure Python: combat simulation, pathfinding and map analysis are what Stardust uses BWEM and FAP for in C++. Use numpy, or bind the C++ library next to `bwapi`.

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

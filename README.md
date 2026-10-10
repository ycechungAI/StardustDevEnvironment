# Stardust (Python)

A StarCraft: Brood War bot written in Python, built on [StardustDevEnvironment](https://github.com/bmnielsen/StardustDevEnvironment).

## Overview

This fork ports the [Stardust](https://github.com/bmnielsen/Stardust) bot to Python (October 2026). A small C++ host embeds CPython in a BWAPI AI module and exposes the complete BWAPI API to it. The bot lives in `python/stardust/`.

Also included:

- **Game windows** with HUD, replay saving, and a stats screen (`s` key)
- **Parallel games** — `tools/run_games.py` plays up to 6 at once, headless or windowed
- **Live stats** — `tools/live_stats.py` charts minerals, gas, supply, workers and army in real time
- **~20 opponent bots** — tournament bots, Tier 2 strategy bots, and AI-written bots (see [bots/README.md](bots/README.md))
- **Elo ratings** — every game goes into `results.csv`; `tools/elo.py` rates the bots
- **Replay analysis** — `tools/replays.py` summarizes fights, build orders, and problems
- **AI-written bots** — `ClaudeOpus55` and `RL_ClaudeOpus55` (see [bots/genai/README.md](bots/genai/README.md))
- **Self-play training** — `tools/selfplay.py` tunes `RL_ClaudeOpus55` via reinforcement learning

## Setup

**Requirements:** CMake 3.25+, a C++17 compiler, Python 3.12+, [uv](https://docs.astral.sh/uv/), and the three Brood War data files from StarCraft 1.16.1 (`STARDAT.MPQ`, `BROODAT.MPQ`, `Patch_rt.mpq`).

```bash
./build.sh
```

This installs Python dependencies, downloads recipe bots, and builds the test harness. Copy the MPQ files into `build/test/` next to the `tests` binary.

For a UI build (game windows):

```bash
cmake -S . -B build-ui -DCMAKE_BUILD_TYPE=Release -DOPENBW_ENABLE_UI=ON
cmake --build build-ui -j 5
```

Copy the MPQ files into `build-ui/test/` as well.

## Running Games

```bash
cd build/test && ./tests --gtest_filter=Steamhammer.4PoolHard
```

To play other bots:

```bash
.venv/bin/python tools/run_games.py --opponent Stone --games 12
.venv/bin/python tools/run_games.py --bot Stardust2025 --opponent BananaBrain --games 10
```

- `--bot <name>` plays as any bot in `bots/` instead of the Python port
- `--ui N` shows up to N game windows (4 by default); `--ui none` is headless
- `--parallel N` sets headless parallelism (up to 6)

### Useful Environment Variables

| Variable | Purpose |
|---|---|
| `STARDUST_TEST_MAP` | Play on a specific map |
| `STARDUST_TEST_SEED` | Fix the random seed |
| `STARDUST_NO_RESULTS` | Keep games out of Elo ratings |
| `STARDUST_HANG_SECONDS` | Auto-end stuck games (default 120 in `run_games.py`) |
| `OPENBW_GAME_SPEED` | Milliseconds per frame (42 = "fastest") |
| `OPENBW_HUD` | `0` hides the HUD panel |

### Other Tools

```bash
.venv/bin/python tools/live_stats.py    # Live stats window
.venv/bin/python tools/elo.py           # Elo leaderboard
.venv/bin/python tools/ladder.py        # Play up the opponent ladder
.venv/bin/python tools/replays.py       # Replay summaries and playback
.venv/bin/python tools/watch.py         # Multi-bot round robin in windows
.venv/bin/python tools/selfplay.py      # Train RL_ClaudeOpus55
```

## Tests Without StarCraft

```bash
uv run pytest    # Bot logic and BWAPI types against an offline build
uv run mypy      # Type-check the bot (strict)
cd build/test && ./tests --gtest_filter='PythonHost.*'  # C++ host tests
```

## Writing the Bot

`stardust.create_bot()` returns the bot object. The host calls its methods for each BWAPI event: `onStart`, `onFrame`, `onUnitCreate`, and so on. The starting point is `python/stardust/bot.py`.

The API mirrors C++ BWAPI — see the [BWAPI documentation](https://bwapi.github.io/) and the table in [PORTING.md](porting.md).

## Project Layout

```
test/            C++ test harness (OpenBW game, opponent in forked process)
src/python/      C++ host: PythonAIModule, BWAPI bindings (pybind11)
python/stardust/ The bot (loaded from source, no install needed)
bots/            Opponent bots and AI-written bots
tools/           Python tools (run_games, live_stats, elo, ladder, ...)
3rdparty/        BWAPI, OpenBW, BWEM, FAP, zstd
```

## Documentation

- [PORTING.md](porting.md) — Python port conventions and status
- [bots/README.md](bots/README.md) — Opponent bots, tiers, and the ladder
- [bots/genai/README.md](bots/genai/README.md) — AI-written bots and training

## CherryVis

Replays are annotated for [CherryVis](https://torchcraft.github.io/TorchCraftAI/blog/2019/02/20/releasing-cherryvis.html). See [cherryvis-docker](https://github.com/bmnielsen/cherryvis-docker) to run it.

## License

Stardust is (c) Bruce Mackenzie Nielsen under the Stardust License (see `LICENSE-STARDUST`). Works containing substantial portions may not be entered in public StarCraft tournaments without the author's permission.

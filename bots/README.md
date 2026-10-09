# Opponent bots

Put other StarCraft: Brood War bots here to play-test Stardust against them. Each bot goes in its own folder, `bots/<BotName>/`, with a `bot.cmake` file that tells the build how to compile it. Every bot with a `bot.cmake` is picked up automatically the next time CMake configures.

```bash
.venv/bin/python tools/run_games.py --opponent WorkerRush
```

```bash
.venv/bin/python tools/run_games.py --opponent Iron --games 10
```

The bots already built into the test harness (Steamhammer, Locutus, Iron, McRave) can be played the same way. To list every bot the build knows about:

```bash
cd build/test && ./tests --gtest_filter=Bots.List
```

## Bots included

These come with the repository, already patched to build here (the two Creative bots are this repository's own settings for Steamhammer) (each folder's licence files say what you may do with it; most forbid entering them in public tournaments without their author's permission):

| Bot | Race | Notes |
|-----|------|-------|
| `Stardust2025` | Protoss | The original C++ Stardust (AIIDE 2025), with its trained mining data |
| `BananaBrain` | Protoss | AIIDE 2025 champion |
| `Steamhammer2025` | Zerg | Steamhammer 5.3.6 (AIIDE 2025) |
| `Microwave` | Zerg | AIIDE 2025 |
| `McRaveZ` | Zerg | McRave as entered in AIIDE 2024/2025 |
| `CreativeZerg` | Zerg | Steamhammer2025 opening only with gambits: proxy hatchery, hydralisk and lurker rushes, queens, defilers, guardians (see its README) |
| `CreativeTerran` | Terran | Steamhammer2025 opening only with gambits: proxy barracks and factory, bunker rush, drops, wraiths (see its README) |

These are included only as recipes (`bots/recipes/<Name>/`), either because their source states no licence or because it's large. Fetch them, then re-run CMake:

```bash
.venv/bin/python tools/fetch_bot.py SAIDA WillyT Dragon
```

| Bot | Race | Notes |
|-----|------|-------|
| `SAIDA` | Terran | AIIDE 2018 champion. States no licence. Its patch also makes its managers' singletons hand back an object still under construction, as MSVC does; SAIDA depends on that. |
| `Dragon` | Terran | AIIDE 2021, built on Facebook's CherryPi (MIT). 37 MB download. It thinks on its own thread and waits up to 30 ms a frame for it, so its games don't repeat exactly from a seed. Set `DRAGON_LOG=1` for its (very long) logs. |
| `WillyT` | Terran | AIIDE 2021. States no licence. Learns openings per opponent in `bwapi-data/write/WillyT_<opponent>.txt`. |

With Iron built in, that's four Terran opponents to go with the three Zerg ones above.

### How strong they are

Wins, losses and draws (frame limit) for each Terran and Zerg bot against the three Protoss bots: 4 games per pairing, on the harness's random maps, with a limit of 24,000 frames (October 2026):

| Bot | vs Stardust2025 | vs BananaBrain | vs Locutus | Total |
|-----|-----------------|----------------|------------|-------|
| `SAIDA` | 0-2-2 | 3-1-0 | 2-1-1 | 5-4-3 |
| `McRaveZ` | 1-3-0 | 2-1-1 | 2-1-1 | 5-5-2 |
| `Microwave` | 0-4-0 | 1-3-0 | 1-3-0 | 2-10-0 |
| `Dragon` | 0-4-0 | 0-1-3 | 1-2-1 | 1-7-4 |
| `Steamhammer2025` | 0-4-0 | 0-2-2 | 0-3-1 | 0-9-3 |
| `Iron` | 0-4-0 | 0-3-1 | 0-2-2 | 0-9-3 |
| `WillyT` | 0-3-1 | 0-4-0 | 0-3-1 | 0-10-2 |

None of them is a match for the Protoss bots, and no stronger ones exist to add: Microwave, McRaveZ and Steamhammer have been the best Zerg bots in every AIIDE and CoG tournament from 2023 to 2026, and the only newer Terran entrants (insanitybot, VOID) score below Dragon. The one bot that beats the Protoss ones, CoG 2026 winner Pluto (Random, a neural network), is a closed Windows binary.

## Playing as another bot

`--bot` plays any of these bots in Stardust's place, with the game window too (`--build build-ui`):

```bash
.venv/bin/python tools/run_games.py --bot Stardust2025 --opponent BananaBrain --games 10
```

Without `--bot`, the Python port plays. Replays are then named `<bot>_vs_<opponent>_<map>_<seed>_WON` or `_LOST`.

Games run under OpenBW like the other tests. Replays go to `build/test/replays/`, named `<bot>_<map>_<seed>_WON` or `_LOST`. `STARDUST_TEST_MAP` picks the map (otherwise it's a random SSCAIT map) and the live game window works too (see "Watching a game" in the top-level README).

Bots built with `stardust_bot` export only their factory (hidden symbol visibility). Several bots carry their own, different BWEM and BWEB; with default visibility macOS merges their inline functions across bots and they crash.

## What kind of bot works

Bots written in **C++ against BWAPI 4.x**, built from source. They're compiled into the test harness and run in the opponent's game process, so:

- A bot distributed only as a Windows DLL or EXE can't run here.
- Java, Scala and other client bots (for example PurpleWave) can't either: the harness has no BWAPI client/server bridge.
- Windows-only code (`windows.h`, MSVC-specific extensions) has to be fixed or stubbed out before the bot builds on macOS.
- Bots written for BWAPI 4.1 or 4.2 usually build against this BWAPI (4.4) with no or small changes.

## Adding a bot

1. Copy its source into `bots/<BotName>/`, for example with `git clone`.
2. Add `bots/<BotName>/bot.cmake`. `bots/WorkerRush/bot.cmake` is a template:

   ```cmake
   stardust_bot(
           NAME WorkerRush                 # used with --opponent; letters, digits and _
           RACE Terran                     # Protoss, Terran, Zerg or Random
           SOURCES *.cpp                   # source globs, relative to the bot folder (searched recursively)
           INCLUDE_DIRS .                  # extra include directories, relative to the bot folder
           HEADER WorkerRush.h             # declares the bot's BWAPI::AIModule subclass
           CREATE "new WorkerRush()"       # C++ expression that creates the module
   )
   ```

   Optional arguments: `DEFINITIONS` (preprocessor definitions) and `CXX_STANDARD` (for example `17`, if the bot doesn't build as C++20).

   If the bot has its own CMake build, `bot.cmake` can instead `add_subdirectory()` it and call `stardust_register_bot(NAME ... RACE ... TARGET <its library target> HEADER ... CREATE ...)`. The target must make the header's directory a public include directory.
3. Re-run CMake and build:

   ```bash
   cmake -S . -B build && cmake --build build -j
   ```

A folder without `bot.cmake` is skipped with a message, so a half-copied bot doesn't break the build.

Bots that read or write files use `build/test/bwapi-data/read`, `write` and `AI`, shared with Stardust. Copy any data files a bot needs (opening books, learned data) there.

## Recipes

`bots/recipes/<Name>/` records where each bot came from (`recipe.json`, with the download's checksum), how it builds (`bot.cmake`) and what was changed to build it here (`macos.patch`). `tools/fetch_bot.py <Name>` recreates `bots/<Name>/` from them, replacing what is there; use it to update a bot or to get one that isn't included.

## Git

Other bots dropped into `bots/` are ignored: they come with their own licences. Tracked are this framework, the `WorkerRush` example, the recipes and the bots listed above (without their DLLs, and without Stardust's `mining-training` folder, which is its author's whole training environment).

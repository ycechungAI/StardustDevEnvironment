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

Games run under OpenBW like the other tests. Replays go to `build/test/replays/`, named `<bot>_<map>_<seed>_WON` or `_LOST`. `STARDUST_TEST_MAP` picks the map (otherwise it's a random SSCAIT map) and the live game window works too (see "Watching a game" in the top-level README).

## What kind of bot works

Bots written in **C++ against BWAPI 4.x**, built from source. They're compiled into the test harness and run in the opponent's game process, so:

- A bot distributed only as a Windows DLL or EXE (for example BananaBrain) can't run here.
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

## Git

Everything in `bots/` except this README, `StardustBots.cmake` and the `WorkerRush` example is ignored. Third-party bots come with their own licences and stay out of this repository.

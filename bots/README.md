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

These come with the repository, already patched to build here (each folder's licence files say what you may do with it; most forbid entering them in public tournaments without their author's permission):

| Bot | Race | Notes |
|-----|------|-------|
| `Stardust2025` | Protoss | The original C++ Stardust (AIIDE 2025), with its trained mining data |
| `BananaBrain` | Protoss | AIIDE 2025 champion |
| `Steamhammer2025` | Zerg | Steamhammer 5.3.6 (AIIDE 2025) |
| `Microwave` | Zerg | AIIDE 2025 |
| `McRaveZ` | Zerg | McRave as entered in AIIDE 2024/2025 |

These are included only as recipes (`bots/recipes/<Name>/`), either because their source states no licence or because it's large. Fetch them, then re-run CMake:

```bash
.venv/bin/python tools/fetch_bot.py SAIDA WillyT Dragon
```

| Bot | Race | Notes |
|-----|------|-------|
| `SAIDA` | Terran | AIIDE 2018 champion. States no licence. Its patch also makes its managers' singletons hand back an object still under construction, as MSVC does; SAIDA depends on that. **Incomplete:** some of its games hang and never finish (2 of 6 against UAlbertaBotProtoss, and the whole UAlbertaBotTerran pairing once), so it is left out of the ladder's Tier 1. It still builds and can be played directly. |
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

## Tiers and the ladder

A new bot learns little from losing every game to Stardust, so the opponents form a ladder from weakest to strongest:

1. **Computer**: `ComputerZerg`, `ComputerProtoss` and `ComputerTerran` (`bots/ComputerAI/`, in the repository). OpenBW has no built-in computer players, so these stand in for them. They follow a fixed build for their race, keep making workers and supply, expand at set times, and attack in growing waves with no micro.
2. **Tier 2**: bots that weren't tournament winners in their time, or are now well behind the best, but are known for an unusual or single-minded strategy. Each one tests one thing: holding a rush, finding cannons, or surviving workers in the base. They are recipes only. The two Protoss bots, PylonPuller and UAlbertaBotProtoss, are the hardest of them, closer to Tier 1 than Tier 2: PylonPuller's dark templar and UAlbertaBotProtoss's zealot rush held off every opening ClaudeOpus55 tried.
3. **Tier 1**: the tournament bots above, plus the built-in Steamhammer, Locutus and Iron, except SAIDA (incomplete; see above).

```bash
.venv/bin/python tools/fetch_bot.py BunkerBoxer PylonPuller UAlbertaBot Stone ZZZKBot
```

| Bot | Race | What it plays |
|-----|------|---------------|
| `BunkerBoxer` | Terran | AIIDE 2019. A bunker rush, then hard-coded timing attacks. States no licence. |
| `PylonPuller` | Protoss | AIIDE 2022, by Hao Pan (MIT). Picks from cannon rushes, proxy gateways, two-gateway zealots and dark templar. |
| `UAlbertaBotProtoss`, `UAlbertaBotTerran`, `UAlbertaBotZerg` | any | David Churchill's UAlbertaBot (MIT; won AIIDE 2013), which Steamhammer and Locutus grew from. Its config rushes with zealots, marines or zerglings. After that it plans builds with its BOSS build-order search and picks its fights with the SparCraft combat simulator. One library, registered once per race. |
| `Stone` | Terran | AIIDE 2015, by Igor Dimitrijevic (MIT), Iron's predecessor. Attacks with SCVs only, which harass, chase, flee and repair. |
| `ZZZKBot` | Zerg | 2020 version, by Chris Coxe (LGPL 3); won AIIDE and CIG 2017. Four-pool and other early zergling rushes. |

`tools/ladder.py` plays a bot up the ladder: a few games against each opponent in turn. It stops at the first one the bot doesn't beat in more than half its games, and draws count as not winning:

```bash
.venv/bin/python tools/ladder.py --bot ClaudeOpus55 --games 3
```

`--from` and `--to` pick part of the ladder (for example `--to ZZZKBot` stops before Tier 1). `--keep-going` plays every opponent for a full report. Without `--bot`, the Python port plays.

## Playing as another bot

`--bot` plays any of these bots in Stardust's place. The games get windows, like any other `run_games.py` run (`--ui none` for headless):

```bash
.venv/bin/python tools/run_games.py --bot Stardust2025 --opponent BananaBrain --games 10
```

Without `--bot`, the Python port plays. Replays are then named `<bot>_vs_<opponent>_<map>_<seed>_WON` or `_LOST`.

Games run under OpenBW like the other tests. Replays go to `build/test/replays/`, named `<bot>_<map>_<seed>_WON` or `_LOST`. `STARDUST_TEST_MAP` picks the map (otherwise it's a random SSCAIT map) and the live game window works too (see "Watching a game" in the top-level README).

Bots built with `stardust_bot` export only their factory (hidden symbol visibility). Several bots carry their own, different BWEM and BWEB; with default visibility macOS merges their inline functions across bots and they crash.

## Looking back at games

`tools/replays.py` works from the replays in `build/test/replays/`. It lists them, sums up what happened in a game so it needn't be watched, plays one in a window, and keeps what the people watching said about it:

```bash
.venv/bin/python tools/replays.py list --bot ClaudeOpus55 --result lost --last 10
.venv/bin/python tools/replays.py summary 1
.venv/bin/python tools/replays.py watch 1 --at decisive
```

A game is the number `list` gives it (1 is the newest), part of its file name, or a path. Without a command it lists the latest games and asks which one to look at.

A summary has:
- a short story of the game;
- the key moments: scouting, first units, proxies and cannon rushes, expansions, upgrades;
- every sizeable fight, with where it was, the army supply on each side beforehand and what each side lost, marking the decisive one;
- the problems each side had: no production, losing workers early, enemy units camped in its base, supply blocks, banked minerals and gas;
- both build orders for the first six minutes, and a minute-by-minute table.

`summary --bot ClaudeOpus55 --last 20` prints a line for each of several games. Summaries come from `replay_dump`, which replays a game without graphics in about three seconds (`cmake --build build --target replay_dump`). They're saved in `build/test/replays/summaries/` and rebuilt when anything they depend on changes.

`watch` prints the summary and plays the replay in OpenBW's window, from the start, a time (`--at 4:30`), the decisive fight or another fight (`--at "fight 2"`). It needs `replay_viewer` from the UI build (`cmake --build build-ui --target replay_viewer`). In the window:
- space pauses;
- `a` and `z` change the speed;
- the arrow keys go back or forward 10 seconds, and `[` `]` a minute.

When the window closes, it asks what the observer noticed. `comment 1` adds a note without watching, or `--text` gives it directly. Comments are saved in `build/test/replays/comments/`, under the observer's login name or `--by`. `comments` shows them, and a game's summary includes its comments.

`build/` isn't in git, so copy anything worth keeping out of it. Comments on a GenAI bot's games count as human advice, so they go into the list in [genai/README.md](genai/README.md) too.

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

Other bots dropped into `bots/` are ignored: they come with their own licences. Tracked are this framework, the `WorkerRush` example, the `ComputerAI` stand-ins, the recipes and the bots listed above (without their DLLs, and without Stardust's `mining-training` folder, which is its author's whole training environment).

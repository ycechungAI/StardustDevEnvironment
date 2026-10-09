# Playing a bot in the real StarCraft on Windows

The test harness runs games under OpenBW, which doesn't have StarCraft's built-in computer players. To play a bot from
`bots/` against them, build it as a BWAPI DLL on Windows and run it in StarCraft 1.16.1. This works for bots that need
only BWAPI and name their AIModule class and header after their folder, such as `ClaudeOpus55`.

## What you need

- A Windows PC or VM with **StarCraft: Brood War 1.16.1**. BWAPI doesn't work with Remastered.
- **BWAPI 4.4.0**: run `BWAPI_Setup.exe` from <https://github.com/bwapi/bwapi/releases/tag/v4.4.0>. It installs
  Chaoslauncher and sets `BWAPI_DIR`.
- **Visual Studio 2019 or 2022** with "Desktop development with C++", which includes CMake.
- This repository, cloned on that machine.

## Build

In a Developer PowerShell, at the root of this repository:

```powershell
cmake -S tools/windows-bot -B build-win -A Win32 -DBOT=ClaudeOpus55
```

```powershell
cmake --build build-win --config Release
```

This makes `build-win/Release/ClaudeOpus55.dll`. Copy it to `<StarCraft>/bwapi-data/AI/`. If CMake can't find BWAPI,
add `-DBWAPI_DIR="C:/path/to/BWAPI"` to the first command.

## Play against the computer

1. Copy `build/test/maps/sscai` from the Mac to `<StarCraft>/maps/sscai`, or use any map you already have.
2. In `<StarCraft>/bwapi-data/bwapi.ini`, set these lines (each section already exists; change the values):

   ```ini
   [ai]
   ai = bwapi-data/AI/ClaudeOpus55.dll

   [auto_menu]
   auto_menu = SINGLE_PLAYER
   map = maps/sscai/(4)Python.scx
   race = Protoss
   enemy_count = 1
   enemy_race = Terran
   game_type = MELEE
   auto_restart = OFF

   [starcraft]
   speed_override = 0
   ```

   `speed_override = 0` runs the game as fast as it can; leave it empty to watch at normal speed.
3. Start Chaoslauncher, tick **BWAPI 4.4.0 Injector [RELEASE]**, and press Start. StarCraft goes straight into a game
   against one computer player of `enemy_race`.
4. For each of the other races, set `enemy_race` to `Zerg`, then `Protoss`, and start again.

To play several games in a row, set `auto_restart = ON`, and set `map = maps/sscai/*.scx` with
`mapiteration = RANDOM` to vary the map.

## Results

Each game's result is appended to `<StarCraft>/bwapi-data/write/ClaudeOpus55_results.txt`:

```
WON  vs Terran Computer on (4)Python.scx after 14211 frames
```

BWAPI saves replays to the folder that `save_replay` in `bwapi.ini` names, by default `maps/replays/`.

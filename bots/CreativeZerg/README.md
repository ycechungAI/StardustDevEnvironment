# CreativeZerg

Steamhammer 5.3.6 by Jay Scott (the source in `bots/Steamhammer2025`), playing Zerg and opening only with gambits. Against a bot that prepares for standard play, a gambit sometimes wins outright. Against a careful bot it often loses. Expect a lower win rate overall, with the occasional upset of a stronger bot.

After the opening it plays like Steamhammer. When it recognizes a rush coming at it, it still switches to a defensive opening.

| Opening | What it does |
|---------|--------------|
| `Proxy8HatchNatural` | A hatchery and sunkens built in the enemy natural, with zerglings |
| `4PoolHard` | The fastest zergling rush |
| `6PoolSpeedBurrow` | Fast speedlings that burrow to hide and ambush |
| `7-7HydraLingRush` | Hydralisks long before anyone expects them |
| `973HydraBust` | Two hatcheries of upgraded hydralisks, all at once |
| `3HatchLingBust` | A wall of speedlings off three hatcheries |
| `7Pool6GasLurker A` | Lurkers before the enemy has detection |
| `2HatchLurkerDrop` | Lurkers dropped by overlords (not against Zerg) |
| `7Pool6GasMuta` | One-base mutalisks |
| `QueenRush` | Queens: broodlings on tanks and big units, ensnare |
| `DefilerRush` | Early defilers with dark swarm and plague (not against Zerg) |
| `GuardianRush` | Guardians outranging static defence (not against Zerg) |
| `UltraRush` | Ultralisks with carapace (not against Zerg) |

Each opening is picked at random, weighted. The weights are in `tools/make_creative_steamhammer.py`. To change them, edit that file, run `python3 tools/make_creative_steamhammer.py` to rewrite `CreativeZerg.json` here, then re-run CMake.

It keeps what it learns about each opponent in `bwapi-data/read/CreativeZerg/` and `bwapi-data/write/CreativeZerg/`, apart from Steamhammer2025's.

Licence: MIT, as Steamhammer's. See `bots/Steamhammer2025/src/licenses/`.

# CreativeTerran

Steamhammer 5.3.6 by Jay Scott (the source in `bots/Steamhammer2025`), playing Terran and opening only with gambits. Against a bot that prepares for standard play, a gambit sometimes wins outright. Against a careful bot it often loses. Expect a lower win rate overall, with the occasional upset of a stronger bot.

After the opening it plays like Steamhammer. When it recognizes a rush coming at it, it still switches to a defensive opening.

| Opening | What it does |
|---------|--------------|
| `ProxyBBS` | Two barracks next to the enemy base |
| `ProxyFactory` | A factory hidden near the enemy, early vultures |
| `BunkerNatural` | A bunker in the enemy natural (not against Protoss) |
| `BBS` | Two barracks at home, early marines |
| `8RaxRush` | Marines on 8 supply (not against Protoss) |
| `BioDrop` | Stimmed marines and medics dropped in the enemy main |
| `VultureDrop` | Vultures dropped behind the enemy |
| `10-10-10-2Port` | Two starports of wraiths |
| `VultureWraith` | Vultures and wraiths raiding together |
| `10-10-10FD` | Fast siege tanks pushing early |

Each opening is picked at random, weighted. The weights are in `tools/make_creative_steamhammer.py`. To change them, edit that file, run `python3 tools/make_creative_steamhammer.py` to rewrite `CreativeTerran.json` here, then re-run CMake.

It keeps what it learns about each opponent in `bwapi-data/read/CreativeTerran/` and `bwapi-data/write/CreativeTerran/`, apart from Steamhammer2025's.

Licence: MIT, as Steamhammer's. See `bots/Steamhammer2025/src/licenses/`.

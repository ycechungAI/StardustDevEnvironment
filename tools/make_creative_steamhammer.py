"""Generates the configuration of the creative Steamhammer bots, bots/CreativeZerg and bots/CreativeTerran.

Both are Steamhammer 5.3.6 (the source in bots/Steamhammer2025) with its usual skills, but opening only with gambits:
proxies, rushes, drops, early tech and odd unit mixes. Against a bot that prepares for standard play they sometimes
win outright; against a careful one they often lose. Edit the opening weights below, then re-run:

    python3 tools/make_creative_steamhammer.py

Each configuration is Steamhammer's own (bots/Steamhammer2025/dll/Steamhammer_5.3.6.json) with these changes:
- the opening weights for each matchup (XvT, XvP, XvZ, and XvU for an unknown or random race) below;
- no opponent-specific openings;
- its learning files in its own folders (bwapi-data/read/<Name>/, bwapi-data/write/<Name>/).
Counter strategies stay: when Steamhammer recognizes a rush coming at it, it still switches to a defensive opening.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STEAMHAMMER_CONFIG = ROOT / "bots" / "Steamhammer2025" / "dll" / "Steamhammer_5.3.6.json"

# Opening name -> weight (relative chance of picking it). Names are Steamhammer's openings ("Strategies").
ZERG_GAMBITS = {
    "Proxy8HatchNatural": 10,  # hatchery and sunkens in the enemy natural, with zerglings
    "4PoolHard": 4,  # the fastest zergling rush
    "6PoolSpeedBurrow": 5,  # fast speedlings that burrow to hide and ambush
    "7-7HydraLingRush": 5,  # hydralisks long before anyone expects them
    "973HydraBust": 7,  # two hatcheries of hydras with upgrades, all at once
    "3HatchLingBust": 6,  # a wall of speedlings off three hatcheries
    "7Pool6GasLurker A": 6,  # lurkers before detection is ready
    "2HatchLurkerDrop": 8,  # lurkers dropped by overlords
    "7Pool6GasMuta": 6,  # one-base mutalisks
    "QueenRush": 6,  # queens: broodlings on tanks and big units, ensnare
    "DefilerRush": 6,  # early defilers: dark swarm and plague
    "GuardianRush": 6,  # guardians outranging static defence
    "UltraRush": 5,  # ultralisks with carapace
}
# Slow tech is too slow against zerg's early aggression
ZVZ_SKIP = {"DefilerRush", "GuardianRush", "UltraRush", "2HatchLurkerDrop"}

TERRAN_GAMBITS = {
    "ProxyBBS": 10,  # two barracks next to the enemy
    "ProxyFactory": 8,  # a factory hidden near the enemy, early vultures
    "BunkerNatural": 7,  # a bunker in the enemy natural
    "BBS": 5,  # two barracks at home, early marines
    "8RaxRush": 5,  # marines on 8 supply
    "BioDrop": 8,  # stimmed marines and medics dropped in the enemy main
    "VultureDrop": 8,  # vultures dropped behind the enemy
    "10-10-10-2Port": 8,  # two starports of wraiths
    "VultureWraith": 6,  # vultures and wraiths raiding together
    "10-10-10FD": 5,  # fast tanks pushing early
}
# Marine-only pressure does little against protoss zealots; raids do more
TVP_SKIP = {"8RaxRush", "BunkerNatural"}

BOTS = {
    "CreativeZerg": ("Zerg", {
        "ZvT": ZERG_GAMBITS,
        "ZvP": ZERG_GAMBITS,
        "ZvZ": {name: weight for name, weight in ZERG_GAMBITS.items() if name not in ZVZ_SKIP},
        "ZvU": ZERG_GAMBITS,
    }),
    "CreativeTerran": ("Terran", {
        "TvT": TERRAN_GAMBITS,
        "TvP": {name: weight for name, weight in TERRAN_GAMBITS.items() if name not in TVP_SKIP},
        "TvZ": TERRAN_GAMBITS,
        "TvU": TERRAN_GAMBITS,
    }),
}


def load_steamhammer_config() -> dict:
    text = STEAMHAMMER_CONFIG.read_text(encoding="utf-8-sig")
    # Steamhammer allows // comment lines in its JSON
    return json.loads(re.sub(r"(?m)^\s*//.*$", "", text))


def main() -> int:
    for name, (race, matchups) in BOTS.items():
        config = load_steamhammer_config()
        strategy = config["Strategy"]
        openings = strategy["Strategies"]
        for matchup, weights in matchups.items():
            for opening in weights:
                if opening not in openings:
                    raise SystemExit(f"{name}: no opening named {opening!r} in {STEAMHAMMER_CONFIG.name}")
                if openings[opening].get("Race") != race:
                    raise SystemExit(f"{name}: {opening!r} is a {openings[opening].get('Race')} opening")
            strategy[matchup] = {race: [{"Weight": weight, "Strategy": opening} for opening, weight in weights.items()]}
        strategy["UseEnemySpecificStrategy"] = False
        strategy["EnemySpecificStrategy"] = {}
        strategy["Crazyhammer"] = False
        config["IO"]["ReadDirectory"] = f"bwapi-data/read/{name}/"
        config["IO"]["WriteDirectory"] = f"bwapi-data/write/{name}/"
        config["IO"]["ErrorLogFilename"] = f"bwapi-data/write/{name}/{name}_ErrorLog.txt"

        output = ROOT / "bots" / name / f"{name}.json"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(config, indent=2) + "\n")
        print(f"{output.relative_to(ROOT)}: {', '.join(f'{m} {len(w)} openings' for m, w in matchups.items())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

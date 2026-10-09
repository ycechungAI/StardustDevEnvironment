"""Plays a bot up the opponent ladder in bots/README.md, weakest opponent first, stopping at the first one it can't beat.

Usage: python tools/ladder.py [--bot NAME] [--games N] [--from OPPONENT] [--to OPPONENT] [--keep-going] [--parallel N]

Each opponent gets N games (default 3) through tools/run_games.py, which prints the time estimate and progress. The
bot moves up when it wins more than half of them; draws (the frame or time limit) count as not winning. Without --bot
it plays as the Python port, like run_games.py. --keep-going plays every opponent regardless, for a full report.
Opponents that aren't built (a recipe that hasn't been fetched) are skipped.
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Weakest first within each rung; see bots/README.md for what each one plays. SAIDA is left out: it is incomplete
# here (some of its games hang), see bots/README.md
LADDER = [
    ("Computer", ["ComputerZerg", "ComputerProtoss", "ComputerTerran"]),
    ("Tier 2", ["BunkerBoxer", "PylonPuller", "UAlbertaBotTerran", "UAlbertaBotZerg", "UAlbertaBotProtoss", "Stone",
                "ZZZKBot"]),
    ("Tier 1", ["Steamhammer2025", "Microwave", "McRaveZ", "Iron", "WillyT", "Dragon", "Locutus",
                "BananaBrain", "Stardust2025"]),
]


def fmt(seconds: float) -> str:
    seconds = max(0, int(seconds))
    return f"{seconds // 60}:{seconds % 60:02d}"


def play(bot: str | None, opponent: str, games: int, parallel: int) -> tuple[int, int] | None:
    """Plays the games, passing run_games.py's output through; returns (won, played), or None if nothing was played."""
    command = [sys.executable, str(ROOT / "tools" / "run_games.py"), "--opponent", opponent, "--games", str(games)]
    if bot:
        command += ["--bot", bot]
    if parallel:
        command += ["--parallel", str(parallel)]
    won = played = 0
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as process:
        for line in process.stdout:
            print(f"  {line}", end="", flush=True)
            if match := re.match(r"GAME \d+/\d+ (\w+)", line):
                played += 1
                won += match.group(1) == "won"
    return (won, played) if played else None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bot", help="bot to play as (default: the Python port)")
    parser.add_argument("--games", type=int, default=3, help="games against each opponent (default 3)")
    parser.add_argument("--from", dest="start", help="opponent to start at, skipping the ones below it")
    parser.add_argument("--to", dest="end", help="last opponent to play, e.g. ZZZKBot to stop before Tier 1")
    parser.add_argument("--keep-going", action="store_true", help="play every opponent, even after a failed one")
    parser.add_argument("--parallel", type=int, default=0, help="passed on to run_games.py")
    args = parser.parse_args()

    opponents = [(rung, opponent) for rung, names in LADDER for opponent in names]
    names = [opponent for _, opponent in opponents]
    for name in (args.start, args.end):
        if name and name not in names:
            parser.error(f"{name} isn't on the ladder: {', '.join(names)}")
    first = names.index(args.start) if args.start else 0
    last = names.index(args.end) if args.end else len(names) - 1
    opponents = opponents[first:last + 1]

    who = args.bot or "the Python port"
    print(f"LADDER {who}: {len(opponents)} opponent(s), {args.games} game(s) each; "
          f"each takes about 1-3 minutes (at most 10 per game)", flush=True)
    start = time.time()
    results: list[tuple[str, str, str]] = []
    reached = None
    for index, (rung, opponent) in enumerate(opponents, 1):
        print(f"\nRUNG {index}/{len(opponents)} {rung}: {opponent} at {fmt(time.time() - start)} elapsed", flush=True)
        outcome = play(args.bot, opponent, args.games, args.parallel)
        if outcome is None:
            results.append((rung, opponent, "skipped (not built?)"))
            continue
        won, played = outcome
        passed = won * 2 > played
        results.append((rung, opponent, f"{won}/{played} won{'' if passed else ' - not beaten'}"))
        if passed:
            reached = (rung, opponent)
        elif not args.keep_going:
            break

    print(f"\nLADDER {who} done in {fmt(time.time() - start)}:", flush=True)
    for rung, opponent, outcome in results:
        print(f"  {rung:<9} {opponent:<20} {outcome}", flush=True)
    print(f"  highest beaten: {f'{reached[1]} ({reached[0]})' if reached else 'none'}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())

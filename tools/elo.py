"""Elo ratings for Stardust and the opponent bots, from the results history the test harness keeps.

Usage: python tools/elo.py [--build DIR] [--k K]

Every game played with an opponent name (Bots.Play, the Steamhammer and Locutus tests) is appended to
<build>/test/replays/results.csv when it ends. This replays that history in order, with every player starting at
1500 and a draw (frame or time limit reached) counting half, writes the ratings to replays/ratings.json (the stats
screen in the game window shows them) and prints a leaderboard.
"""

import argparse
import csv
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INITIAL_RATING = 1500.0
DEFAULT_K = 32.0
SCORES = {"WON": 1.0, "DRAW": 0.5, "LOST": 0.0}


@dataclass
class Record:
    rating: float = INITIAL_RATING
    wins: int = 0
    losses: int = 0
    draws: int = 0
    opponents: dict[str, list[int]] = field(default_factory=dict)  # name -> [wins, losses, draws]

    @property
    def games(self) -> int:
        return self.wins + self.losses + self.draws


@dataclass
class Game:
    row: dict[str, str]
    player_before: float
    opponent_before: float
    player_after: float
    opponent_after: float


def expected(rating: float, other: float) -> float:
    return 1.0 / (1.0 + float(10.0 ** ((other - rating) / 400.0)))


def rate(rows: list[dict[str, str]], k: float = DEFAULT_K) -> tuple[dict[str, Record], list[Game]]:
    """Ratings after playing through the games in order, and each game's rating changes."""
    records: dict[str, Record] = {}
    games: list[Game] = []
    for row in sorted(rows, key=lambda r: r["time"]):
        score = SCORES.get(row["result"])
        if score is None:
            continue
        player = records.setdefault(row["player"], Record())
        opponent = records.setdefault(row["opponent"], Record())
        before = (player.rating, opponent.rating)
        change = k * (score - expected(player.rating, opponent.rating))
        player.rating += change
        opponent.rating -= change

        tally = player.opponents.setdefault(row["opponent"], [0, 0, 0])
        other_tally = opponent.opponents.setdefault(row["player"], [0, 0, 0])
        if score == 1.0:
            player.wins += 1
            opponent.losses += 1
            tally[0] += 1
            other_tally[1] += 1
        elif score == 0.0:
            player.losses += 1
            opponent.wins += 1
            tally[1] += 1
            other_tally[0] += 1
        else:
            player.draws += 1
            opponent.draws += 1
            tally[2] += 1
            other_tally[2] += 1
        games.append(Game(row, before[0], before[1], player.rating, opponent.rating))
    return records, games


def read_results(replays_dir: Path) -> list[dict[str, str]]:
    path = replays_dir / "results.csv"
    if not path.exists():
        return []
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def update(replays_dir: Path, k: float = DEFAULT_K) -> tuple[dict[str, Record], list[Game]]:
    """Re-rates the history and writes replays/ratings.json."""
    records, games = rate(read_results(replays_dir), k)
    ratings = {"ratings": {name: round(record.rating, 1) for name, record in records.items()},
               "games": {name: record.games for name, record in records.items()}}
    replays_dir.mkdir(parents=True, exist_ok=True)
    temporary = replays_dir / "ratings.json.tmp"
    temporary.write_text(json.dumps(ratings, indent=2, sort_keys=True) + "\n")
    temporary.replace(replays_dir / "ratings.json")  # the game reads it at start; never let it see half a file
    return records, games


def game_line(game: Game) -> str:
    row = game.row
    player_change = game.player_after - game.player_before
    opponent_change = game.opponent_after - game.opponent_before
    return (f"{row['player']} {game.player_after:.0f} ({player_change:+.0f}) vs "
            f"{row['opponent']} {game.opponent_after:.0f} ({opponent_change:+.0f})")


def leaderboard(records: dict[str, Record]) -> str:
    lines = [f"{'#':>2}  {'Player':16} {'Elo':>5} {'Games':>5} {'W':>4} {'L':>4} {'D':>3} {'Win %':>6}"]
    ranked = sorted(records.items(), key=lambda item: -item[1].rating)
    for place, (name, record) in enumerate(ranked, 1):
        win_rate = 100.0 * (record.wins + record.draws / 2) / record.games if record.games else 0.0
        lines.append(f"{place:>2}  {name:16} {record.rating:5.0f} {record.games:5} {record.wins:4} {record.losses:4} "
                     f"{record.draws:3} {win_rate:5.1f}%")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build", help="build directory whose test/replays to rate")
    parser.add_argument("--k", type=float, default=DEFAULT_K, help="how far one game moves a rating")
    args = parser.parse_args()
    replays_dir = ROOT / args.build / "test" / "replays"
    records, games = update(replays_dir, args.k)
    if not records:
        print(f"No rated games yet in {replays_dir / 'results.csv'}")
        return 0
    print(f"{len(games)} games from {replays_dir / 'results.csv'}\n")
    print(leaderboard(records))
    return 0


if __name__ == "__main__":
    sys.exit(main())

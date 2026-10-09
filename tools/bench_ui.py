#!/usr/bin/env python3
"""Measures how much the game windows slow games down, with tools/run_games.py.

Usage: python tools/bench_ui.py [--games N] [--frames N] [--bot Stone] [--opponent BunkerBoxer] [--runs ui6,none6,ui1]

Plays the same capped game (fixed map and seed, kept out of the results) several ways and prints the frames played per
second of each, since a game can end early with a win:
  ui6    6 games at once in windows (the run_games default)
  none6  6 games at once headless (--ui none)
  ui1    1 game at a time in a window
then how the windows compare with headless, and 6 windows with 1. Run it with nothing else busy: other games or a
selfplay run change the numbers far more than the windows do. The window runs open the live stats page, as
run_games does; --no-live leaves it out.
"""

import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

RUNS = {
    'ui6': ['--ui', '6'],
    'none6': ['--ui', 'none', '--parallel', '6'],
    'ui1': ['--ui', '1'],
}

GAME_LINE = re.compile(r'^GAME \d+/\d+ .* frame (\d+)')


def play(name: str, args: argparse.Namespace) -> tuple[float, int, int]:
    """Plays one run; returns its wall seconds, total frames and games."""
    games = args.games if name != 'ui1' else max(1, args.games // 2)
    env = dict(os.environ, STARDUST_TEST_SEED=str(args.seed), STARDUST_TEST_MAP=args.map,
               STARDUST_TEST_FRAME_LIMIT=str(args.frames), STARDUST_NO_RESULTS='1')
    command = [sys.executable, str(ROOT / 'tools' / 'run_games.py'), '--bot', args.bot, '--opponent', args.opponent,
               '--games', str(games), '--interval', '3600', *RUNS[name]]
    if args.no_live:
        command.append('--no-live')
    started = time.monotonic()
    result = subprocess.run(command, env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True)
    seconds = time.monotonic() - started
    frames = [int(m.group(1)) for line in result.stdout.splitlines() if (m := GAME_LINE.match(line))]
    if result.returncode != 0 or len(frames) != games:
        sys.exit(f'{name}: run_games exited {result.returncode} after {len(frames)}/{games} games\n'
                 + result.stdout[-2000:] + result.stderr[-2000:])
    return seconds, sum(frames), games


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--games', type=int, default=12, help='games per run (half that for ui1; default 12)')
    parser.add_argument('--frames', type=int, default=10000, help='frame limit per game (default 10000)')
    parser.add_argument('--bot', default='Stone')
    parser.add_argument('--opponent', default='BunkerBoxer')
    parser.add_argument('--map', default='Fighting Spirit')
    parser.add_argument('--seed', type=int, default=4242)
    parser.add_argument('--no-live', action='store_true', help='no live stats page (it opens for the window runs)')
    parser.add_argument('--runs', default='ui6,none6,ui1', help='which runs, from ' + ','.join(RUNS))
    args = parser.parse_args()

    names = [name for name in args.runs.split(',') if name]
    if unknown := [name for name in names if name not in RUNS]:
        parser.error(f'unknown runs: {", ".join(unknown)}')

    rates: dict[str, float] = {}
    for name in names:
        print(f'{name:6} playing...', end='', flush=True)
        seconds, frames, games = play(name, args)
        rates[name] = frames / seconds
        print(f'\r{name:6} {games:3} games, {frames:7} frames in {seconds:6.1f} s: {rates[name]:7.0f} frames/s')

    if 'ui6' in rates and 'none6' in rates:
        print(f'6 windows vs 6 headless: {100 * (rates["ui6"] / rates["none6"] - 1):+.0f}% frames/s')
    if 'ui6' in rates and 'ui1' in rates:
        print(f'6 windows vs 1 window:   {rates["ui6"] / rates["ui1"]:.1f}x frames/s')


if __name__ == '__main__':
    main()

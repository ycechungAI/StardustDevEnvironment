# ClaudeOpus55RL

ClaudeOpus55 (`bots/ClaudeOpus55`, the AI-written bot) taught by self-play. It started from ClaudeOpus55's code of 9 October 2026. ClaudeOpus55 itself stays unchanged, as the record of its experiment.

The approach follows [Leela Zero](https://github.com/ycechungAI/leela-zero), with a set of parameters in place of a neural network: a StarCraft network would need far more games than one computer can play. The parameters are the bot's key numbers and choices, read from a weights file at the start of each game:

| Parameter | ClaudeOpus55's value | What it decides |
|---|---|---|
| `probes_per_base`, `one_base_probes`, `max_probes` | 24, 22, 60 | how many workers to make |
| `first_attack_army`, `later_attack_army` | 12, 16 | army size before attacking |
| `retreat_below`, `retreat_ratio` | 6, 1.25 | when to retreat: too few units, or the enemy this many times stronger |
| `regroup_distance` | 450 | how far a unit may stray before rejoining the army |
| `army_first_ratio` | 2.0 | how far behind our army must be before units come before buildings |
| `terran_opening`, `protoss_opening` | 0, 0 | two gateways first, or one gateway and the core |
| **Build order and macro** | | |
| `zealots_before_core`, `zealots_before_core_vs_terran` | 4, 3 | zealots before the cybernetics core (2 more after a seen rush, 1 more against a barracks rush) |
| `army_for_natural`, `army_for_natural_vs_protoss` | 6, 8 | army size before taking the natural |
| `expand_strength_ratio` | 0.8 | how strong our army must be, relative to the enemy's, to expand |
| `probes_per_base_before_next` | 18 | probes per nexus before another base |
| `gateways_per_base`, `max_gateways` | 3, 12 | gateways added per base, and the most |
| **Situational awareness** | | |
| `home_threat_radius`, `natural_threat_radius` | 900, 600 | how close enemies must be to the main or natural to count as an attack |
| `rush_window`, `rush_min_units` | 9000, 4 | how long (frames) a seen rush shapes the build, and the army that ends rush defence |
| `scout_supply`, `scout_until` | 9, 6000 | when the scout leaves, and the frame it stops scouting |
| **Unit reactions** | | |
| `uphill_penalty` | 2.0 | how many times stronger an enemy on high ground counts when deciding to retreat |

Against Zerg the cannon opening stays as it is: its comments in the code record the alternatives losing.

## Training

```bash
.venv/bin/python tools/selfplay.py --hours 12
```

Each round, `tools/selfplay.py`:
1. **Proposes a candidate:** the best parameters with one to three changed, by steps that grow while candidates keep passing and shrink while they don't.
2. **Plays it, 6 games at a time**, headless at full speed:
   - **Self-play:** one slot is always the old version (`ClaudeOpus55RL`) against the new one (`ClaudeOpus55RLCandidate`), 20 games, taking turns at being "us".
   - **Training bots:** the other 5 slots play the new version against PylonPuller, UAlbertaBot (Terran, Zerg, Protoss) and Stone, at least 2 games each, and keep going round them while self-play lasts.
3. **Gates it:** the candidate must score 55% against the old version, as in Leela Zero, by more than luck (a standard error clear of 50%: 13 of 20), and at least the best's score against the 5, less 5%.
4. **Asks you:** a candidate that passes is shown with its results, and promoted only if you answer `y`. Instead, `--approver COMMAND` runs a command (another AI, say) with the report's path, where exit status 0 approves; `--auto-approve` promotes every one that passes.
5. **Tracks Elo:** each promotion adds the self-play margin. Generation 0, ClaudeOpus55's own parameters, is 0 Elo.

Of Tier 2 (`tools/ladder.py`), BunkerBoxer, the weakest, is left out as too easy to teach anything. ZZZKBot, the strongest, is held out as the test: it is never trained against.

**The goal**, after which the first iteration has succeeded and training stops:
1. The best wins every game against the 5 training bots, twice in a row (a confirming gauntlet follows a perfect one).
2. It then wins every test game against ZZZKBot (4 by default).

A failed test is tried again once a new generation has been promoted.

Everything is saved as it happens, in `training/`:
- `state.json`: the best parameters, generation, Elo and step size.
- `best.json`, plus `generations/NNN.json` for every promotion.
- `history.jsonl`: every candidate and its games.
- `selfplay.log`: the run's log.
- `issues.log` and `issues/`: the bug-finding log (above).

**Memory.** Every 2 seconds the trainer adds up the memory its games use (each game is the harness plus the opponent it starts). Over the limit, three quarters of the computer's RAM by default (12 GB on a 16 GB Mac), it stops the newest games, plays them again later and steps down from 6 games at once to 4, then 2, then 1 for the rest of the run. A single game over 4 GB is a bot leaking memory: it is stopped and reported in `issues.log`. Closing the terminal or Ctrl+C stops every game.

**Bug finding.** Training finds games that go wrong, stops them, logs them and carries on:
- **Crash:** the game exits with an error.
- **Stuck:** its processes use no CPU for 2 minutes (a deadlock, or waiting forever), or it is still running after 30 minutes.
- **Memory leak:** one game over 4 GB.
- **Error in the output:** an assertion, exception, segmentation fault or Python traceback printed by a game that still finished.
- **Draw:** the game hit the frame or time limit, usually a bot that stops attacking or can't finish off the enemy.
- **Missing result:** a game that ended normally but recorded nothing.

A pairing that fails 3 times in a row is skipped until training restarts, so one broken bot can't stall a run. Games left running by an earlier run that didn't get to stop them are cleared at start.

Each issue is a JSON line in `training/issues.log` with the bot, the opponent, the weights it played with and the last 30 lines of the game's output; the whole output is kept in `training/issues/`. `--issues` sums the log up, most frequent first. Parameters can't fix a bug in the code: give `issues.log` to a Claude session to find and fix the cause in `ClaudeOpus55RL.cpp` (or work around an opponent's), then rebuild and carry on training.

Stopping (Ctrl+C) and running again resumes. `--status` shows where training stands, `--issues` what went wrong.

Useful options:
- `--games`: self-play games per candidate (default 20).
- `--gauntlet-games`: games at least against each training bot (default 2).
- `--test-games`: games against ZZZKBot (default 4).
- `--parallel`: games at once (default 6).
- `--memory-limit-gb`: the most memory the games may use together (default: three quarters of the RAM).
- `--hours`: stop after this long.
- `--seed`: random seed for the candidates.

It needs the headless build with the 7 Tier 2 bots fetched and built. It checks at start and says what's missing.

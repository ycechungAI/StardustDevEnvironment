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
3. **Gates it:** the candidate must score 55% against the old version, as in Leela Zero, and at least the best's score against the 5, less 5%.
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

Stopping (Ctrl+C) and running again resumes. `--status` shows where training stands.

Useful options:
- `--games`: self-play games per candidate (default 20).
- `--gauntlet-games`: games at least against each training bot (default 2).
- `--test-games`: games against ZZZKBot (default 4).
- `--parallel`: games at once (default 6).
- `--hours`: stop after this long.
- `--seed`: random seed for the candidates.

It needs the headless build with the 7 Tier 2 bots fetched and built. It checks at start and says what's missing.

# ClaudeOpus55 strategy

Written by Claude Opus 5.5 before coding `bots/ClaudeOpus55`, and given to Grok 4.7 and GPT-6 Luna as the starting point they may change (see `strategy-request.md`).

## Plan: one-base four-gate dragoons

**Economy**
- Train probes continuously up to 22 (one base: about 2 per mineral patch plus 3 on gas).
- Idle probes mine the nearest mineral patch to the nexus; 3 probes on each finished assimilator.
- Builders return to mining once their building has started, or after 600 frames if it never did.

**Supply**
- Build a pylon when free supply is at most 3 + 3 per finished gateway, starting at 7–8 supply.
- At most 1 + (finished gateways / 3) pylons in progress at once.
- Pylons go on the side of the nexus away from the minerals, spreading out in a 3×3 pattern 3 tiles apart.

**Build order (by supply used)**
1. Pylon at 8
2. Gateway at 10
3. Gateway at 12
4. Assimilator at 13
5. Cybernetics Core at 15, once a gateway has finished
6. Then up to 4 gateways whenever 200 minerals are spare
7. Singularity Charge (dragoon range) as soon as the core is done and 150/150 is available

**Production**
- Gateways make dragoons when the core is done and 125/50 is spare, otherwise zealots.
- Before the core starts, make at most 2 zealots (unless the base is under attack) so the core isn't delayed.
- Minerals and gas promised to buildings that haven't started yet are kept aside.

**Scouting**
- At 9 supply, send one probe to the other start locations, nearest first, until an enemy resource depot is seen; then it goes back to mining.
- On two-player maps the enemy base is known from the start.
- Remember every enemy building seen and forget it once its tile is visible and it is gone.

**Army**
- **Defend first:** any visible enemy ground unit (or anything that can attack) within 900 pixels of the nexus draws the whole army at home. If the army is less than half the size of the attackers, pull up to 10 probes onto enemies within 250 pixels of them and 450 of the nexus; they go back to mining afterwards.
- **Waves:** attack when the army reaches 8 units for the first wave and 12 for later ones; go back to the rally point (a quarter of the way from the base to the map center) when it falls below 4.
- **Attack target:** the enemy base, otherwise the first known enemy building, otherwise the next unscouted start location. When the enemy main is visible and empty, sweep the other start locations.
- **Targeting (every 4 frames):** each unit shoots the enemy within its range plus 64–160 pixels with the highest priority (attackers 3, workers 2, pylons and resource depots 1, other buildings 0), breaking ties by lowest hit points plus shields. Undetected enemies are skipped. With nothing in range it attack-moves to the target.

**Not done (because of the one-hour limit)**
- Expanding.
- Kiting or retreating individual damaged units.
- Observers for detection.
- Reacting to the enemy's build.

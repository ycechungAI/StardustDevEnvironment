# SparkTerran strategy

Written by Muse Spark before coding `bots/SparkTerran`. Strategy by Muse Spark, code by Muse Spark.

## 1. Name and overview

**SparkTerran: standard macro Terran with per-matchup openings.** Against Zerg it opens 2-rax marines and pressures early while teching to tanks behind the push. Against Protoss (and unknown races) it walls the ramp with depot and barracks, fast-expands, and plays the classic marine/medic into siege tanks and vessels. Against Terran it stays home behind a bunker and tanks, then out-expands. The idea is to be the "solid ladder player": no gambits, no cheese of its own, just clean production, timely expansions, and fights picked only when the army outpowers what it can see. That should beat rush bots (it scouts and holds), macro bots that tech greedily (the early pressure punishes), and one-trick bots (detection, turrets and bunkers answer each trick).

## 2. Build order

Supply counts are supply used (halved, as BWAPI reports).

**Vs Zerg — 2-rax pressure:**
1. Supply depot at 8
2. Barracks at 9
3. Barracks at 11
4. Supply depot at 13
5. Refinery at 15
6. Academy at 19 (once ~6 marines are out)
7. Supply depot at 21
8. Factory at 24, then engineering bay at 26, comsat on the main CC
9. Natural command center when minerals pass 400 and 8+ marines are out (about 5:00), unless a rush is being held

**Vs Protoss / unknown — 1-rax wall-in fast expand:**
1. Supply depot at 8, at the ramp
2. Barracks at 10, at the ramp (loose wall: buildings near the ramp top, not a perfect seal)
3. Refinery at 12
4. Supply depot at 14
5. Command center at the natural at 16 (about 1:30–2:00)
6. Second barracks at 18, academy at 20, factory at 24, engineering bay at 28, machine shop, comsat
7. Missile turret in each mineral line once the engineering bay is done (dark templar answer)

**Vs Terran — defensive:**
1. Supply depot at 8
2. Barracks at 10
3. Refinery at 12
4. Supply depot at 14
5. Bunker at the ramp at 15
6. Factory at 18, machine shop, engineering bay at 24, comsat; siege mode first
7. Natural command center around 6:00 once tanks hold the ramp; starport for wraiths/goliath answer if needed

**Supply rule past the opening:** start a depot when free supply is at most 4 (+1 per 4 build-order steps done), never more than one depot queued at a time, cap 200. Depots spread around the main away from the mineral line.

**Adaptation:** if the race is unknown and the 8-supply scout finds Zerg before frame 4000, the second barracks goes down immediately (switch to the pressure plan). If a rush is seen, the natural command center step is skipped until 6 marines are out.

## 3. Economy

- SCVs train continuously from every command center until 22 per base, cap 66 total. Rally to minerals.
- 3 SCVs per finished refinery; pull extras back to minerals if gas banks over 300. First refinery with the opening; second when gas drops under 100 after 4:00.
- Expansions: natural at 400 banked minerals when safe (see above); third base at 500 banked minerals after 8:00 when the army is 12+ supply. Bases are the precomputed mineral clusters nearest home that we can walk to; the command center goes on the precomputed buildable spot nearest the minerals.
- Builders return to mining once their building is completed (Terran SCVs build to completion) or after 1500 frames if it never started. One builder per building.
- Idle SCVs go to the base with the fewest miners per patch, onto its least-mined patch. Every 10 seconds, oversaturated bases (over 2 miners per patch) send SCVs to the neediest base.
- An SCV repairs the ramp bunker when it takes damage and no enemies are within 400 pixels.

## 4. Production and tech

**Barracks:** marines by default. With an academy: one medic per four marines (cap 8 medics). Vs Zerg: up to 4 firebats once 8 marines are out (zergling answer). Never queue more than 2 per barracks.

**Factory:** siege tanks by default (machine shop addon). Goliaths instead when mutalisks, wraiths or carriers are seen (up to tanks + 4). Second and third factory when minerals bank over 600 after 6:00.

**Starport (after 5:00):** science vessels first (control tower addon, cap 3) once the science facility is done. Battlecruisers only vs carriers after 11:00 with 400/300 banked. Wraiths only if enemy wraiths are seen.

**Research priority:** academy — stim packs, then U-238 shells (marine range), then caduceus reactor (medic energy). Machine shop — siege mode first, then charon boosters (goliath range) if air is about, then ion thrusters. Science facility — EMP shockwave, then irradiate vs Zerg, then titan reactor. Engineering bay — infantry weapons and armor alternating (weapons first), vehicle weapons first instead in TvT or when enemy tanks are seen.

**Static defence:** bunker at the ramp when rushed or in TvT (load 4 marines). Two missile turrets per mineral line when air, cloaked ground, or drops are seen (after 3:30).

## 5. Scouting and information

- One SCV scouts at 8 supply: nearest start locations first. On finding the enemy base it notes the race and circles the base edge for up to 8:00, running from anything that shoots, noting tech buildings.
- Everything seen is remembered: enemy buildings by position (forgotten when their tile is visible and empty), enemy army units by type/health/position (forgotten after 3 minutes unseen).
- Conclusions: early pool / early rax / proxy buildings near our base / 3+ enemy workers in our main before 3:30 → rush defence mode. Spire, stargate, starport with air → turrets and goliaths. Lurkers, dark templar, cloaked wraiths → comsat scans and turrets. Enemy siege tanks → careful pushes only. Fast enemy natural → match with our own expand timing.
- Comsat stations (one per command center once the academy is done): scan cloaked enemies fighting our army; otherwise scan the enemy base every ~100 seconds.

## 6. Army control

- **Defence first:** enemies within 900 pixels of a command center pull the army home; units fight with a 900-pixel leash around the base so they don't chase across the map. Bunkers near a threatened base get loaded with up to 4 marines.
- **Attack judgment:** the army gathers at the ramp-top rally. First push at 10 army supply, later pushes at 18. It attacks the enemy base (or last known building, or next unscouted start) when its power beats the known enemy power near the target by 1.15×, and retreats to the rally when the enemy's nearby power beats its own by 1.4× or the army falls under 6 supply. Power = durability × (0.5 + damage per frame), summed per side — concentration matters, so the army fights as one group.
- **Targeting (every 4 frames):** each unit shoots the visible, detected enemy in sight range with the highest priority (static defence, siege tanks, lurkers, reavers: 5; turrets, detectors, shuttles/dropships: 4; other attackers: 3; workers: 2; resource depots: 1; other buildings: 0), ties broken by lowest hit points, minus distance; targets others have already covered to lethal are skipped. Otherwise attack-move to the goal.
- **Marines:** stim when researched, above 20 HP, and enemies within 320 pixels. Focus fire per the targeting rule.
- **Tanks:** siege when researched and enemy ground is within 400 pixels; unsiege and leapfrog forward when attacking and nothing is in range within 700 pixels of the target. Sieged tanks only shoot ground in 64–384 range.
- **Medics:** never lead; move to the most damaged marine within 320 pixels, else hold 128 pixels behind the marine centroid.
- **Vessels:** stay 256 pixels behind the army. EMP the densest clump (3+) of shielded Protoss units; irradiate the highest-HP organic Zerg; otherwise defense matrix the most damaged sieged tank. All need 100 energy.
- **Goliaths:** shoot air first, fight with the tanks otherwise.
- **Vs lurkers:** marines with 4+ neighbours within 64 pixels spread out every 24 frames so one volley doesn't catch the clump.
- **Worker rush / proxy in our base:** up to 8 SCVs attack the intruders until they're gone.

## 7. Edge cases

- **Cloaked units:** comsat scans in fights, turrets in mineral lines; the army skips undetected targets.
- **Air (mutalisks, wraiths):** turrets in mineral lines, goliaths from the factory, marines focus them; SCVs keep mining under turret cover.
- **Drops (shuttle/dropship):** turret at the ramp, army leash defends bases; vessels and goliaths answer the air itself.
- **Supply block:** the depot rule keeps one queued; if blocked anyway, the next 100 minerals go to a depot before anything else.
- **Losing the main:** the natural's command center becomes the rally and production continues; SCVs from the dead base re-mine at the neediest base.
- **Enemy expands:** note it and take our next base one step sooner; the attack judgment uses the newest known position.
- **Cannon/bunker rush at our base:** SCVs pull onto the building workers and the rising buildings; marines focus the buildings over workers.
- **Battlecruisers/carriers late:** goliaths mass, vessels stay back, EMP carriers; battlecruisers of our own only vs carriers with a bank.

## 8. What to leave out

- Vultures and spider mines (good vs Zerg, but another production line to manage).
- Ghosts, lockdown, nukes (micro-heavy, marginal payoff at this level).
- Dropship play (needs transport micro and timing the bot can't tune in the budget).
- A perfect wall-in seal (the ramp geometry varies per map; buildings near the ramp top plus a bunker hold well enough).
- Learning the enemy's build between games (no file I/O; every game starts from the same plans).

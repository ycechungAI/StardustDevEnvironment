# SparkZerg strategy — Strategy by Muse Spark, Code by Muse Spark

A Zerg bot for the Stardust ladder, written under the same rules as ClaudeOpus55
(C++ against BWAPI 4.4.0, on OpenBW, Zerg only, no human-written bot code read).
It is a macro bot: safe openings per matchup, a hydralisk/lurker mid-game with a
mutalisk harass option, hive tech late, and simple per-unit micro. It is designed
to survive early rushes first and win long games second.

## 1. Name and overview

SparkZerg plays standard, greedy-when-safe Zerg. It scouts with overlords, picks
an opening from the enemy race (overpool against Zerg rush risk, 12-hatch against
Protoss and Terran when the map and scout allow it), walls nothing but defends
with sunken colonies and zerglings, and wins with the classic hydra/lurker push,
mutalisk harass when the enemy's anti-air is thin, and ultralisk/defiler in the
late game. It should beat the simple Tier 2 bots because it never dies to the
first rush and its macro keeps producing behind its army.

## 2. Build order

Supply counts are supply used. Drones are made continuously unless a step says
otherwise. An extractor is taken when the spawning pool starts, except in ZvZ
where it waits for the first zerglings.

**ZvZ — overpool** (safe against 6–9 pool):
- 9 Overlord, 10 Spawning Pool.
- When the pool finishes: 6 Zerglings, Metabolic Boost, scout with 2 lings.
- If an enemy pool or zerglings are seen before our pool finishes: lings
  non-stop, a sunken colony at the main, no gas until the rush is held.
- Otherwise: 18 Hatchery at the natural, Extractor, Lair at ~24, Hydralisk Den.

**ZvP / ZvT — 12 hatch** (14 hatch if the map's rush distance is very short):
- 9 Overlord, 12 Hatchery at the natural, 11 Spawning Pool, 10 Extractor.
- ZvP reaction to 2-gate/early zealots: stop drones at ~12, lings + 1 sunken.
- ZvT reaction to 2-rax or bunker: lings, 1–2 sunkens, drones fight the bunker.
- Lair at ~30, then Hydralisk Den (always) and Spire only if the scout shows
  weak anti-air (few marines/cannons/turrets).

**Zerg vs unknown/random:** play the ZvT 12-hatch shape with the pool one supply
earlier (11), and send the first overlord to the enemy at once.

The natural is the closest resource cluster to the main by ground distance.
Further expansions are taken when minerals bank above 500 with no immediate
threat, preferring the cluster nearest the army.

## 3. Economy

- Drones: ~2.2 per mineral patch plus 3 per extractor, capped at 66 total. Drone
  production pauses while holding an early rush and resumes after.
- Gas: first extractor with the pool (ZvZ: after first lings). 3 drones per gas.
  Second gas when the lair starts; third gas with the hive.
- Supply: never block. A larva becomes an overlord whenever supply used is
  within 2 of supply total and no overlord is already morphing.
- Overlords double as scouts and detectors; keep 1–2 with the army from the
  mid-game on, pulled back when they take fire.

## 4. Production and tech

Larvae are spent in this priority: overlord if needed, drones if below target,
then army. The standing army mix:

- ZvZ: zerglings (speed) into hydralisks; lurkers if it goes long; ultralisks
  late. Upgrades: melee 1–2, carapace, then missile.
- ZvP: hydralisks + lurkers (lurker aspect as soon as the den finishes), a
  mutalisk pack (6–9) only when the scout shows fewer than 3 cannons/corsairs.
  Upgrades: missile 1–3, carapace 1–3, grooved spines, muscular augments.
- ZvT: hydralisks + lurkers against bio, mutalisks when turrets are thin;
  hive into ultralisks + defilers with plague against mech. Upgrades as ZvP,
  plus burrow early.

Tech buildings: evolution chamber (one early, second with the third base),
hydralisk den, queen's nest → hive when on 3 bases and banking gas, ultralisk
cavern, defiler mound. Lurker aspect is the first den research; plague and
consume when defilers appear. Overlord speed (pneumatized carapace) mid-game.

## 5. Scouting and information

- First overlord rallies to the nearest enemy start; if empty, it tours the
  other starts. Later overlords spread to map corners and outside enemy bases.
- 2 zerglings scout the enemy natural/front at ~3:30 in ZvZ, or after the pool
  in other matchups when safe.
- Reactions: early pool/barracks/gateways → lings and sunkens, drones pause;
  fast enemy expansion → take our own expansion sooner and drone harder;
  stargate/starport/spire → spore colonies in the mineral lines; robotics or
  templar archives → keep overlords spread (observers/shuttles); many factories
  or tanks → hive faster, skip mutalisks.

## 6. Army control

- The army gathers at the natural's front. It attacks when it holds at least
  ~20 army supply and its estimated strength exceeds the seen enemy's by 1.3×;
  it retreats home when the nearby enemy is 1.25× stronger, then re-gathers.
- Target priority: lurkers, siege tanks, reavers, bunkers and sunkens first,
  then shooting army units, then workers, then buildings. Units keep a target
  in range rather than re-targeting every frame; hydralisks focus-fire a target
  others are already shooting without overkilling it.
- Micro: hydralisks step back from melee while their weapon reloads; zerglings
  surround via attack-move and dive workers when raiding; mutalisks stack (all
  move to one point before fighting) and snipe workers/turrets; lurkers burrow
  with the army when enemies close and unburrow to reposition; defilers plague
  clumps of 4+ enemies and consume a zergling below 75 energy; damaged units
  pull back only when a fresher unit is beside them.
- Defence: the army holds at home on a leash (nothing chased past ~700 px from
  the base) while a rush is incoming; sunkens cover the mineral lines.

## 7. Edge cases

- Cloaked attackers (dark templar, wraiths, burrowed lurkers): spores at bases
  and an overlord over the army; the army waits rather than walking into them.
- Proxy/cannon rush: an enemy pylon, cannon or bunker near our main draws up
  to 8 drones until it dies, and the natural waits while lings come out.
- Air armies: hydralisks and spores answer mutalisks/wraiths/scouts; scourge
  (4–8) against carriers/battlecruisers.
- Supply block: an emergency overlord is the highest larva priority, always.
- Losing the main: the natural becomes home; drones flee to the newest base.
- Enemy expansions: overlords watch them; a ling run-by denies them, or the
  main push swings through them.

## 8. What to leave out

Nydus canals, queen spells beyond defilers, drops (ventral sacs), infested
command centers, neural parasite, precise sim-city walling, and multi-prong
harass beyond one mutalisk pack and ling run-bys. The bot wins with macro,
one strong push timing, and hive tech — not tricks.

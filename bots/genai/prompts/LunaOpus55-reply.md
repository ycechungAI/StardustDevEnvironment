# LunaOpus55 strategy (reply from ChatGPT)

- **Prompt:** `strategy-request.md` followed by `ClaudeOpus55-strategy.md`, sent as one message.
- **Where:** chatgpt.com, logged out (free), on 7 October 2026. The logged-out page has no model picker and doesn't name the model it uses; it was meant to be GPT-6 Luna, the free model, but this couldn't be confirmed.
- **Reply:** saved verbatim below as the page displayed it. The stray "B BWAPI", "G GitHub" and "+2" lines are ChatGPT's web-citation chips. ChatGPT searched the web while answering ("Worked for 10s").

---

Worked for 10s
1. Name and overview

Fortress Dragoon: 2-Gate → Robotics → Safe Expansion. The bot should replace the baseline's blind four-gate commitment with a safer opening that gets Dragoons and Dragoon range quickly, produces an Observer early enough to handle cloaking, and expands once a simple safety test passes. The core combat plan is ranged Dragoon pressure with conservative retreating, backed by Zealots and a small number of emergency Cannons/Probes against rushes; this is substantially less brittle against Zergling rushes, Protoss aggression, Terran tanks/bunkers, and cloaked units than a pure four-gate all-in.

The key implementation principle is finite-state behavior rather than sophisticated strategic inference: OPENING → DEFEND → PRESSURE → EXPAND → TWO_BASE_ATTACK, with a few explicit enemy-triggered overrides. BWAPI directly exposes the unit, weapon, upgrade and radius information needed for these rules. 
B
BWAPI
+2

2. Build order

Use supply as current supply used, and execute the next item whenever resources and prerequisites permit. Do not blindly wait for an exact frame because worker/building timing varies slightly.

Standard opening

8 supply: Pylon.

Send the builder back to minerals immediately after construction begins.

Place it within 8 tiles of the Nexus and as close as possible to the Nexus without blocking mineral access.

10 supply: Gateway.

Place within 7 tiles of the Nexus.

If a visible enemy rush is already approaching, place it between the Nexus and the approach direction.

12 supply: Assimilator.

Immediately put 3 Probes on it when complete.

13 supply: second Gateway.

Do this before the Cybernetics Core; this is the main anti-rush insurance.

15 supply: Cybernetics Core.

Start as soon as a Gateway has completed.

Do not delay it for additional Zealots unless enemy units are already attacking the base.

16 supply: Zealot from the first Gateway.

Make a second Zealot only if:

≥4 enemy melee units are seen before the first Dragoon, or

an enemy worker rush is detected, or

the enemy has entered the main mineral area.

18 supply: Pylon.

20 supply: first Dragoon.

From either Gateway.

Thereafter, prioritize Dragoons whenever 125 minerals + 50 gas are available and doing so does not prevent the next required Pylon/building.

As soon as the Cybernetics Core finishes and 150/150 is available: research Singularity Charge.

This is mandatory before additional tech/upgrades. Singularity Charge adds 64 pixels to Dragoon weapon range, taking it from 128 to 192 pixels under BWAPI's range representation. 
G
GitHub
+1

At ~24 supply / immediately after range starts: Robotics Facility.

This is deliberately earlier than the baseline.

Its purpose is detection and eventual Reaver production, not an elaborate Robo build.

Next Pylon: whenever projected free supply after current production is ≤4.

Never allow the bot to reach 0 free supply if a Pylon can be started.

At 6–8 combat units, move the army to the defensive rally point just outside the main.

After Robotics Facility completes: build an Observatory.

Make one Observer immediately.

Make a second Observer only if cloaked units have actually been seen or the first Observer is lost.

After the first Observer: resume normal Dragoon/Zealot production.

Rush override

At any time before the first Observer, if either:

≥6 enemy Zerglings/Zealots are within 700 px of the Nexus, or

≥4 enemy workers are within 500 px of the Nexus, or

any enemy combat unit is inside the mineral line,

enter DEFEND_RUSH.

In DEFEND_RUSH:

Stop expansion.

Produce Zealots if minerals are available and fewer than 4 Zealots exist.

Continue producing Dragoons whenever gas permits.

If ≥4 enemy melee units are within 350 px of the Nexus, build one Photon Cannon if a Forge already exists; otherwise build a Forge only if ≥300 minerals are available after reserving 125 for the next Dragoon and the next Pylon.

Pull 6–10 Probes, not the entire economy, only when enemy melee units reach the mineral line.

Return to normal production once there have been no enemy combat units within 700 px of the Nexus for 240 consecutive frames.

The Probe pull is an emergency measure, not a normal combat tactic.

3. Economy
Workers

Continuously produce Probes until 24 workers on one base.

Target:

16–18 mineral workers

3 gas workers

remaining workers temporarily building or moving.

Once 24 workers exist, stop Probe production until a second Nexus is operational.

After expansion, resume Probe production to 42 total workers:

24 at the main,

18 at the expansion.

Do not try to achieve perfect mineral-patch saturation. The priority is never allowing production buildings to idle because all minerals are tied up in unnecessary workers.

Gas

One Assimilator immediately at 12 supply.

Keep exactly 3 Probes on it.

Do not construct a second gas before the expansion unless:

≥3 Gateways are active, and

the first Observer exists, and

there is consistently >150 gas available while Gateways are waiting for gas.

After the second Nexus is complete, build the second Assimilator and put 3 Probes on it.

Gateways

Use:

2 Gateways through the early game.

Add a third Gateway after:

Robotics Facility,

first Observer,

expansion Nexus,

and at least 6 Dragoons/Zealots.

Add a fourth Gateway once the second base has ≥12 mineral workers.

This prevents the baseline's problem of spending enormous amounts of minerals on four Gateways while still having only one economy.

Expansion

The first expansion is conditional, not timed.

Expand when all are true:

first Observer is complete;

≥8 combat units exist;

≥5 Dragoons exist;

no enemy combat unit has been within 900 px of the main during the previous 240 frames;

at least 400 minerals are available after accounting for:

the next Pylon,

currently queued units,

and required buildings.

Choosing the expansion

Do not implement a general map analyzer.

Instead, compute a crude expansion candidate from BWAPI's static resources:

Enumerate all static mineral fields.

For each mineral field, count other static minerals within 320 px.

A candidate cluster requires:

at least 5 mineral fields within 320 px,

a static geyser within 400 px,

and distance from the main Nexus between 500 and 1800 px.

Use the average position of the minerals as the candidate position.

Sort candidates by:

shortest ground distance from the main, approximated by Euclidean distance;

closest candidate to the main;

closest to the center of the map.

Call getBuildLocation(Nexus, candidateTile) and use the first valid result.

If no candidate passes, do not expand. Continue the one-base strategy.

This is intentionally crude. The important point is that the bot gets a reasonable natural expansion on the benchmark maps without implementing a pathfinder or chokepoint analyzer. BWAPI exposes the static mineral/geyser sets and building-location query needed for this. 
B
BWAPI

4. Production and tech
Default priority

Every frame, buildings follow this priority:

Required Pylon.

Emergency defensive building.

Cybernetics Core.

Robotics Facility.

Observatory.

Nexus expansion.

Gateway production.

Forge/upgrades.

Additional Gateways.

Do not queue a lower-priority item if doing so would make the next higher-priority item unaffordable.

Unit composition

Target approximately:

60–70% Dragoons

25–35% Zealots

0–10% other combat units

The first 6–8 combat units should be mostly Dragoons.

Dragoons cost 125 minerals/50 gas and have 100 HP + 80 shields; their weapon attacks both ground and air, making them a particularly useful single production-line answer to the benchmark's mixed opponents. 
B
BWAPI

Robotics

After the first Observer:

If enemy has ≥4 Siege Tanks, ≥6 Bunkers/defensive structures, or ≥8 large ground units: produce Reavers.

Otherwise leave the Robotics Facility producing nothing after the second Observer.

Do not implement Shuttle drops.

Upgrades

Priority:

Singularity Charge.

Protoss Ground Weapons +1 after the first expansion and once ≥3 Gateways exist.

Protoss Ground Armor +1.

Further ground weapons.

Everything else omitted.

Do not research Shield upgrades during the one-hour implementation.

Singularity Charge costs 150/150 and is specifically the Dragoon range upgrade. 
B
BWAPI

5. Scouting and information
Initial scout

At 9 supply, send one Probe to scout.

On a two-player map: go directly toward the enemy start.

On a 3/4-player map: visit start locations in increasing Euclidean distance.

Stop as soon as an enemy worker, building, or unit is found.

Return the Probe to mining after identifying the enemy main.

Record:

enemy race;

enemy main location;

first production building;

number of enemy workers seen;

whether gas is being mined;

whether the enemy has expanded;

any tech building;

any cloaked/detection-related structure;

approximate army count.

Second scout

After the initial scout returns, use an Observer once available.

Before an Observer exists, use a Probe only if it can safely enter unexplored territory.

Enemy classification

Every 24 frames, classify the enemy using only currently visible units/buildings.

Rush

Set RUSH = true if:

≥6 Zerglings/Zealots within 700 px of main, or

≥4 enemy workers within 500 px, or

≥2 enemy Bunkers completed/progressing within 900 px, or

≥3 enemy combat units within 500 px.

Response: DEFEND_RUSH.

Fast expansion

Set FAST_EXPAND = true if the enemy's natural Nexus/Hatchery/Command Center is seen before the enemy has ≥8 combat units.

Response:

keep producing Dragoons;

attack at 8 units rather than waiting for 12;

do not chase the enemy into its main;

establish own expansion as soon as the safety condition passes.

Heavy Terran defense

If ≥2 Siege Tanks or ≥3 Bunkers are observed, stop attacking the defended position with ordinary Dragoons. Produce Reavers and attack elsewhere or wait for reinforcements.

Cloak

If any cloaked/burrowed enemy is detected or a structure/unit capable of producing cloaked threats is confirmed:

keep an Observer with the army;

keep a second Observer at the main;

never send the only Observer forward beyond 1000 px of the army.

6. Army control
Army states

Maintain exactly one main army plus, optionally, one Observer.

States:

HOME

RALLY

PRESSURE

ATTACK

RETREAT

Home defense

The army stays at home if:

any enemy combat unit is within 900 px of the Nexus;

≥4 enemy units are within 1200 px;

or the enemy has entered the mineral line.

Do not send the army across the map while this condition holds.

Rally

When there is no immediate threat, rally to a position 600 px from the Nexus toward the enemy start.

The army should remain within 900 px of the Nexus until it reaches the attack threshold.

Attack thresholds

Attack when:

8 combat units exist and the enemy has expanded early or appears weak;

otherwise 12 combat units.

After attacking, continue until one of these occurs:

army falls below 6 units;

≥30% of army's total HP+shields is lost in the last 120 frames;

≥2 Siege Tanks are encountered;

army is outnumbered by visible enemy combat units by more than 1.5:1;

no Observer is present while enemy cloak is suspected.

Then retreat.

Retreat point

Retreat to the rally point, not directly to the Nexus.

If the enemy follows to within 450 px of the Nexus, switch to HOME.

Target selection

Re-evaluate targets every 8 frames.

For each combat unit, score visible enemy targets:

Enemy unit currently attacking the Protoss army: +100

Enemy worker attacking or repairing: +70

Siege Tank: +65

Bunker: +55

Other combat unit: +50

Detector: +45

Production building: +25

Pylon/resource structure: +10

Then add:

+20 if target is already within weapon range;

+20 if target can attack this unit;

+10 if target's HP+shields < 50;

subtract 1 point per 16 pixels of distance.

Do not attack buildings if a dangerous combat unit is currently in weapon range.

Dragoon micro

Every 4 frames:

If an enemy melee unit is within 128 px, move the Dragoon away from it.

If the Dragoon can fire and has an enemy target within range, attack.

If it has fired and the nearest melee enemy is within 192 px, move backward 128 px toward the army center.

Do not individually kite if ≥4 enemy units are within 300 px; instead retreat the whole army.

The implementation does not need animation-perfect stutter stepping. A 4-frame control interval is enough to exploit Dragoon range without producing excessive commands.

The Dragoon's base range is 4 tiles and Singularity Charge adds 2 tiles; BWAPI represents the upgrade as an additional 2×32 pixels. 
G
GitHub
+1

Zealot micro

Zealots:

attack the closest enemy ground unit;

prioritize melee units attacking Dragoons;

never chase fleeing ranged units more than 300 px ahead of the army;

retreat with the army rather than independently.

Focus fire

If at least 3 Dragoons can attack the same target:

all three use the same target until it dies or leaves 256 px range;

then select another target.

This prevents the common failure mode where every Dragoon attacks a different Zergling.

Probe combat

Normally no Probes fight.

Pull 6–10 Probes only when:

enemy melee units are inside 250 px of the mineral line;

or ≥4 enemy workers are attacking the Nexus/mineral line.

Target enemy workers first.

Release the Probes as soon as no enemy combat unit is within 300 px of the mineral line for 96 frames.

7. Edge cases
Cloaked units

The first Observer is mandatory.

If cloaked units are suspected but no Observer is available:

retreat the army to the main;

do not chase invisible units;

keep Gateways producing;

prioritize Observatory/Observer.

Once an Observer is present, keep it 96–160 px behind the army center, rather than leading the army.

Air units

Dragoons attack air as well as ground, so produce additional Dragoons rather than introducing a separate anti-air branch. 
B
BWAPI

If ≥8 enemy air units are visible, stop making Zealots until the visible air count falls below 5.

Supply block

At every frame:

freeSupply = supplyTotal - supplyUsed.

If freeSupply <= 4 and no Pylon is under construction, immediately start a Pylon if affordable.

If blocked:

do not start another expensive tech building;

spend available minerals on the Pylon first;

then resume normal production.

Losing the main Nexus

If the Nexus is destroyed:

cancel all expansion plans;

retreat all surviving combat units toward the nearest surviving Pylon/Gateway;

make a replacement Nexus at the best valid resource cluster found by the expansion algorithm;

stop Probe production until the replacement Nexus is complete if minerals are scarce;

use all surviving Gateways for Dragoons.

Do not attempt a complicated hidden-base strategy.

Losing the Robotics Facility

Immediately rebuild it if:

enemy cloak is known/suspected, or

the first Observer was lost.

Otherwise delay rebuilding it until ≥8 combat units exist.

Enemy expands

If an enemy expansion is seen:

set attack threshold to 8 units;

attack its expansion if it is lightly defended;

otherwise attack the enemy's connecting army or main;

never sit outside a fortified expansion indefinitely.

If the enemy has two or more bases while Protoss has one and the army cannot safely attack, force the expansion unless a rush is active.

Enemy turtle

If the enemy has ≥3 defensive structures around its main and no significant mobile army:

do not repeatedly attack the front;

expand;

add third/fourth Gateway;

add Reavers;

attack only after the army reaches 16+ combat units or the enemy leaves its defenses.

Empty enemy main

If the enemy main is visible and contains no Nexus/Command Center/Hatchery:

do not assume the game is won;

search known enemy expansions/start locations with the Observer;

send the main army only after no army threat exists near the Protoss base.

Lost scouting

Never infer that an enemy structure was destroyed merely because it is no longer visible.

Maintain lastSeenPosition and lastSeenFrame for important enemy buildings. A building is considered destroyed only when its tile becomes visible and the building is absent.

BWAPI's exploration/visibility information supports exactly this fog-of-war distinction. 
B
BWAPI

8. What to leave out given the 1-hour limit

Do not implement:

detailed choke-point detection;

terrain/pathfinding;

map-specific build orders;

worker mineral optimization;

gas-stealing;

proxy buildings;

DT openings;

Corsair/Carrier strategies;

Stargate play;

High Templar/Storm;

Archons;

Shuttle drops;

sophisticated Reaver scarab micro;

multi-army control;

hallucinations;

Dark Archons;

wall-off detection;

sophisticated threat maps;

influence maps;

opponent-specific learned builds;

replay analysis;

machine learning;

hard-coded locations for individual benchmark maps.

The one exception is the small resource-cluster expansion finder, because it can be implemented with static minerals/geysers plus getBuildLocation and avoids hard-coding map coordinates. BWAPI explicitly exposes static neutral resources and building-location queries. 
B
BWAPI

The resulting bot is deliberately less ambitious than a tournament bot, but it addresses the baseline's biggest structural weaknesses: no detection, no expansion, no response to enemy composition, and excessive commitment to four one-base Gateways. The central Dragoon plan remains simple enough that Claude Opus 5.5 can implement and debug the whole state machine, production manager, resource-cluster expansion selection, and basic micro inside the one-hour budget.

B
G
Sources

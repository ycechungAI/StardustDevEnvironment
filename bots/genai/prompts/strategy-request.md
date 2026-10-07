# Strategy request (sent unchanged to Grok 4.7 and GPT-6 Luna)

This is the exact prompt given to each strategy model. Paste everything below the line, followed by the full contents of `ClaudeOpus55-strategy.md`, as one message in a fresh chat. Save the reply verbatim as `<Bot>-reply.md`.

---

You are designing the strategy for a StarCraft: Brood War bot. You will not write code: another model, Claude Opus 5.5, will implement your design in C++ exactly as you specify. You may use at most 1 hour of its coding time, and it is a careful but not superhuman programmer, so specify something that can be built and debugged in that hour.

## Rules (from the StarSkirmish benchmark)

- **Race:** Protoss only.
- **Platform:** C++ against BWAPI 4.4.0, running on OpenBW (an open-source reimplementation of the Brood War engine).
- **No outside bot code:** the implementation may not use or copy any human-written bot (Stardust, BananaBrain, Steamhammer and so on), and may not read opponents' source code. Only the BWAPI library itself and the BWAPI documentation may be used. Do not base your answer on reproducing a specific existing bot.
- **Maps:** the opponents play on standard 2- to 4-player ladder maps such as Heartbreak Ridge, Benzene, Destination, Fighting Spirit and Circuit Breaker.
- **Information:** the bot only knows what it sees (fog of war). No map hacks, no cheats.
- **Ending:** a game ends when one side's buildings are all destroyed, or at a frame limit.

## Opponents it will face

These are strong tournament bots:
- Protoss: Stardust, BananaBrain and Locutus.
- Zerg: Steamhammer, McRave and Microwave, which open with early zergling rushes as well as macro play.
- Terran: SAIDA and Iron, which use bunkers, tanks and vultures.
- Simpler and AI-written bots.

It needs to survive early rushes (zerglings, zealots, worker rushes) and still win long games.

## What the implementer can easily use

BWAPI gives, among other things:
- unit types with their costs, build times, weapons and ranges;
- `Broodwar->getBuildLocation(type, nearTile)` to find a valid building spot;
- `unit->train/build/attack/move/gather/upgrade/research`;
- `getUnitsInRadius`, unit hit points and shields, cooldowns, and the start locations;
- `isExplored` and `isVisible` per tile, and the enemy units currently seen.

There is no map analysis library (no chokepoint or expansion finder) unless your plan explains how to compute what it needs simply.

## Baseline

Below is the strategy of ClaudeOpus55, a bot built under the same rules. Use it as your starting point: keep, change or replace any part, so that the result is the strategy *you* think is strongest within the 1-hour implementation limit.

## Your answer

Reply with a complete specification in this format:

1. **Name and overview** (2–3 sentences): the plan and why it should beat the opponents above.
2. **Build order:** the step-by-step opening, with exact supply counts or triggers.
3. **Economy:** worker targets, gas timing, expansion rules (if any) with how to choose the location.
4. **Production and tech:** what to build from which buildings, upgrades and research, and in what priority.
5. **Scouting and information:** what to scout, when, and how to react to what is seen (for example an early rush versus a fast expansion).
6. **Army control:** defence rules, when to attack and retreat, unit targeting, and any micro (kiting, focus fire, spellcasting), each with exact numbers (distances in pixels, unit counts, frame intervals).
7. **Edge cases:** cloaked units, air units, being supply blocked, losing the main base, the enemy expanding.
8. **What to leave out** given the 1-hour limit.

Be concrete enough that two different programmers would implement the same bot.

---

*(ClaudeOpus55-strategy.md follows here.)*

# Generative-AI opponent bots

Three Protoss bots written by AI models under rules adapted from the [StarSkirmish benchmark](https://starskirmish.com/bench/), as opponents for Stardust. StarSkirmish didn't publish its AI-written bots, so these are new runs of the same idea. They are not its bots.

| Bot | Strategy by | Code by | Status |
|---|---|---|---|
| `ClaudeOpus55` | Claude Opus 5.5 | Claude Opus 5.5 | first version done: beats 5 of the 7 Tier 2 bots |
| `LunaOpus55` | ChatGPT, free and logged out (meant to be GPT-6 Luna; the page doesn't name the model) | — | dropped on 8 October 2026: its plan (two gateways, early range, robotics facility, then a safe expansion) is mostly what ClaudeOpus55 already plays; only dragoon focus fire and kiting, and reavers against tanks, were new |
| `GrokOpus55` | Grok 4.7 | Claude Opus 5.5 | waiting: grok.com won't answer without an account |

Opus 5.5 writes all three bots' code so that the comparison is about the strategy each model chooses, not about how well each one programs.

## Rules

From StarSkirmish:
- C++ against BWAPI 4.4.0, on OpenBW.
- Protoss only.
- No human-written bot code: the bots in `bots/` and Stardust itself are off-limits, and opponents' source code may not be read. The BWAPI headers are allowed.

Changed for this repository, by the user:
- **Time:** 3 hours of writing and practice games per bot, instead of 1 hour, counted from when coding starts. Time spent paused because the session ran out of credits doesn't count. On 8 October 2026 the user extended ClaudeOpus55's budget to 6 hours, with a first version counted as done once it beats at least half of the Tier 2 bots.
- **Practice games:** the bot may play any of the bots in `bots/`. Observing an opponent's play during those games counts as practice (for example the build timings in the original Stardust's own game log), but its source code still may not be read.
- **Human advice allowed:** the user may give strategy advice while a bot is being trained. Everything given is listed below, because it means the bots are no longer purely the models' own work.

## Practice ladder

Rather than practising against Stardust, one of the strongest bots, a bot works its way up the ladder in `bots/README.md` ("Tiers and the ladder"). It starts with stand-ins for StarCraft's computer players, moves on to Tier 2 bots that each play one niche strategy, and finishes with the Tier 1 tournament bots:

```bash
.venv/bin/python tools/ladder.py --bot ClaudeOpus55 --games 3
```

## Prompts

`prompts/` holds everything needed to repeat the experiment:
- `strategy-request.md`: the exact request sent to the strategy models.
- `ClaudeOpus55-strategy.md`: ClaudeOpus55's own plan, which is also the baseline the other models were given to change.
- `LunaOpus55-reply.md` and `GrokOpus55-reply.md`: the models' replies, verbatim, with notes on where and how they were obtained.

To get a strategy from another model, open a fresh chat and paste the request followed by `ClaudeOpus55-strategy.md` as one message. Save the reply verbatim.

## Human advice given while training ClaudeOpus55 (7 October 2026)

In the user's words, lightly condensed:

1. Only build a robotics facility if the opponent builds Templar Archives.
2. Always build dragoons once the core is built (to have access to robotics), but the quickest detection is cannons.
3. Don't keep more than 400 minerals and 100 gas.
4. With 400+ minerals, build a base if you aren't being attacked, or strengthen the army; build a base if you think you're winning but lack bases.
5. Stick to several good build orders, to avoid being stuck at 9/9 supply.
6. Save enough minerals for the pylon once you have 8 of 9 probes.
7. Learn from the other bots' games, especially same-race ones: their mistakes and strengths.
8. Two gateways and one robotics facility is safe, or four gateways when building a mass army; the timing to get there is important.
9. Don't use a fixed plan; react to the opponent, so you need to scout.
10. In a battle, let full-health units come forward when the front-line units are damaged.
11. Positioning matters: a good surround is always better than single file.
12. High ground is always an advantage if you place units correctly, and the reverse; be careful attacking up a ramp.
13. Against aggression and worker rushes, use workers to attack, build cannons, and make as many units as quickly as possible.
14. The first gateway should be early: at 10 workers is good, but watch out for an all-in Zerg rush at 6 workers.
15. The games are saved: spend time noticing the other bots' patterns and how to beat them in theory, then put that in code and practise it.
16. Have the other bots play each other, see which strategy wins, and copy it; that is faster at this point.
17. Learning from their play is fine under the 3-hour rule, as long as no code is copied.
18. Against Zerg, get zealots and, once you have enough of them, a few dragoons if the Zerg means to be aggressive.
19. If the Zerg isn't timing mutalisks, massed dragoons are good; if it is, you need air units.
20. Zealots are bad against hydralisks; that needs reavers and dragoons.
21. From the middle to the late game, high templar with storm will turn battles, once the bot is good enough to use them.
22. A forge fast expand isn't always an advantage, for example when the natural has two entrances or on a small map.
23. Pylon placement is important.

## Human advice given while training ClaudeOpus55 (8 October 2026)

24. Rush defence is what is losing games; work on that.
25. Against Protoss, an early gateway and one zealot is better than playing for a long game. Only fast expand when you know the opponent is fast expanding.
26. Against Zerg, get the gateway faster, especially after scouting an early spawning pool (like a 6-pool). The pylon still comes first, because the gateway needs its power, but the gateway goes down as soon as the pylon allows: before more probes or the next pylon. Then make as many units as possible. Don't build the second gateway until you have about 2 zealots, or until 1 zealot has held off an attack and killed zerglings.
27. To get better strategies against a Tier 2 bot you are stuck on, have the Tier 1 bots play it, and take ideas from how they win.
28. Against Protoss, only stay on one gateway if they don't rush you. If they rush, a second and third gateway are necessary: that is the stable strategy.
29. If a bot stays stuck on the two Protoss bots, PylonPuller and UAlbertaBotProtoss, leave them: they are simply better, closer to Tier 1 than Tier 2. Tier 2 only needs 4 of the 7 bots beaten.
30. Against the Tier 2 bots already beaten, raise the average Elo and win more consistently: 100% of games, or close to it, if possible.
31. Protoss units have shields: shield batteries, and waiting for shields to regenerate, greatly improve how long units last. The attack need not wait for full shields: about 40% recharged is a worthwhile delay.
32. Units low on shields should move to the back of a fight, or go home to defend the base, unless adding the damaged units gives a decisive edge.
33. Newly made units, with full shields, make good front-line attackers and defenders.
34. Three probes on an assimilator is the efficient number, but two to start is the way to go.
35. Units cost more minerals than gas, so a mineral-heavy build gains nothing from a second geyser.
36. Monitor mineral and gas income, army composition, and the largest army each side has had.
37. If the enemy's army is constantly bigger, we need to make more units than they do in less time: four gateways, and expand for the minerals to pay for them. Mass units are always good for a one-base all-in, but more bases are preferred whenever it is safe.
38. Three on gas when a quick gas boost is needed: two is not set in stone. Move probes between minerals and gas based on what you need.
39. A lot of gas banked means advanced buildings and upgrades can be considered.
40. Upgrades matter more when both armies are nearly the same size.
41. On Protoss, gas goes mostly on upgrades and templar. From the middle to the late game, high templar are good against mass units, because of storm.
42. When you know your unit composition and army size don't match the enemy's, don't engage: pull back.
43. Keep producing units, unless saving for a nexus or an upgrade.
44. If minerals go above 400, build another gateway, a stargate or a robotics facility, to produce more units.
45. Which of the three to build depends on our army composition and the enemy's.
46. The standard four gateways, then more gateways in the middle game, is a workable plan.
47. Commit only part of the way to a strategy: each one has trade-offs. One or two high templar for storm is enough; five is too many. Two: one for defence, one for offence. They are slow, so losing one hurts: keep them from being sniped, because they are valuable and can turn a battle.
48. Defence is a 50/50 call, so how far to go with it is a matter of degree too.
49. Against Zerg, corsairs are good for sniping overlords to limit the enemy's supply, in any situation.
50. Against Terran, our units are far superior except to siege tanks and battlecruisers: keep the pressure up. Unless it is massing marines, producing a lot of units and attacking wins; once it builds tanks or battlecruisers, be more careful.
51. More defence early against Zerg, and against Terran, defence against drops.
52. The reaver plan can go further: a shuttle dropping two reavers to harass and kill workers. Less money for the opponent means it builds fewer units.
53. If a base and its workers go down, rebuild the right number of workers to keep production going: expanding and making probes both matter.
54. Storm works well on Zerg ground units too, because Zerg mass-produces them.
55. Find replays of the world's top three Protoss players and copy their style: what they do right against each race.
    What Claude found in 43 tl.net replays of Bisu, Best and Stork (about 30 read cleanly in OpenBW):
    - PvT: 1-gate core, dragoon range at once, natural at 4-5 minutes behind 2-4 dragoons (or nexus first), robotics
      and an observer; only 1-2 gateways until 6-8 minutes; probes never stop (21-25 at 5:00, 40-55 at 10:00); the army
      is mostly dragoons and a citadel before 7:00 is rare.
    - PvZ: forge expand (forge 1:40, cannon 2:20, nexus 2:10-2:45, gateway 3:00, core 3:45), stargate and corsairs about
      4:30, citadel and zealot speed about 4:50, templar archives about 7:00, 2-5 high templar and 4 gateways by 8-10
      minutes, 40-50 probes.
    - PvP (one clean game): 1-gate core, zealot then dragoons, natural at 4:30 behind 3 dragoons.
56. A shuttle can also drop a high templar on a saturated mineral line to storm the workers. Pick it back up after the storm and get out, unless it has energy for a second storm, and the enemy will be more alert after the first.
57. Against a Protoss that puts cannons at its second base, drop gateway units into its main from two shuttles while its army is away: a decent chance of winning.
58. Two high templar if the enemy is all in on ground units; otherwise spend the gas on something else.
59. Against an opponent with no detection, hide the templar archives and make dark templar instead of high templar: either a stealth attack on its workers, or mixed in with the ground army for its damage if there is enough gas.
60. Build what counters mech and heavily armoured units like ultralisks (the user asked for immortals, which are StarCraft 2 only; in Brood War, dragoons and reavers).

The user also chose to carry LunaOpus55's new ideas (from ChatGPT's plan in `prompts/LunaOpus55-reply.md`) over to ClaudeOpus55 once LunaOpus55 was dropped: dragoon focus fire, and reavers against siege tanks and static defence. Its third idea, dragoons kiting melee units, was already in ClaudeOpus55.

# Generative-AI opponent bots

Three Protoss bots written by AI models under rules adapted from the [StarSkirmish benchmark](https://starskirmish.com/bench/), as opponents for Stardust. StarSkirmish didn't publish its AI-written bots, so these are new runs of the same idea. They are not its bots.

| Bot | Strategy by | Code by | Status |
|---|---|---|---|
| `ClaudeOpus55` | Claude Opus 5.5 | Claude Opus 5.5 | training, budget extended to 6 hours; done when it beats half the Tier 2 bots |
| `LunaOpus55` | ChatGPT, free and logged out (meant to be GPT-6 Luna; the page doesn't name the model) | Claude Opus 5.5 | strategy received, not yet coded |
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

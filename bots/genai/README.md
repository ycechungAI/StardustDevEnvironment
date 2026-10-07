# Generative-AI opponent bots

Three Protoss bots written by AI models under rules adapted from the [StarSkirmish benchmark](https://starskirmish.com/bench/), as opponents for Stardust. StarSkirmish didn't publish its AI-written bots, so these are new runs of the same idea. They are not its bots.

| Bot | Strategy by | Code by | Status |
|---|---|---|---|
| `ClaudeOpus55` | Claude Opus 5.5 | Claude Opus 5.5 | written and being trained |
| `LunaOpus55` | ChatGPT, free and logged out (meant to be GPT-6 Luna; the page doesn't name the model) | Claude Opus 5.5 | strategy received, not yet coded |
| `GrokOpus55` | Grok 4.7 | Claude Opus 5.5 | waiting: grok.com won't answer without an account |

Opus 5.5 writes all three bots' code so that the comparison is about the strategy each model chooses, not about how well each one programs.

## Rules

From StarSkirmish:
- C++ against BWAPI 4.4.0, on OpenBW.
- Protoss only.
- No human-written bot code: the bots in `bots/` and Stardust itself are off-limits, and opponents' source code may not be read. The BWAPI headers are allowed.

Changed for this repository, by the user:
- **Time:** 3 hours of writing and practice games per bot, instead of 1 hour, counted from when coding starts. Time spent paused because the session ran out of credits doesn't count.
- **Practice games:** the bot may play any of the bots in `bots/`. Observing an opponent's play during those games counts as practice (for example the build timings in the original Stardust's own game log), but its source code still may not be read.
- **Human advice allowed:** the user may give strategy advice while a bot is being trained. Everything given is listed below, because it means the bots are no longer purely the models' own work.

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

# ClaudeOpus55RL: ClaudeOpus55 (bots/ClaudeOpus55), started from its code of 9 October 2026, with its key numbers and
# opening choices read from a weights file that tools/selfplay.py tunes through self-play and games against Tier 2.
# Two builds of the same code: ClaudeOpus55RL plays the current best weights, ClaudeOpus55RLCandidate the ones under
# test (bwapi-data/AI/ClaudeOpus55RL-best.json and -candidate.json). See README.md here.
stardust_bot(
        NAME ClaudeOpus55RL
        RACE Protoss
        SOURCES *.cpp
        HEADER ClaudeOpus55RL.h
        CREATE "new ClaudeOpus55RL()"
        DEFINITIONS CLAUDEOPUS55RL_WEIGHTS=best
)
stardust_bot(
        NAME ClaudeOpus55RLCandidate
        RACE Protoss
        SOURCES *.cpp
        HEADER ClaudeOpus55RL.h
        CREATE "new ClaudeOpus55RL()"
        DEFINITIONS CLAUDEOPUS55RL_WEIGHTS=candidate
)

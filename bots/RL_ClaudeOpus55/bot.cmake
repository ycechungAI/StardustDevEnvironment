# RL_ClaudeOpus55: ClaudeOpus55 (bots/ClaudeOpus55), started from its code of 9 October 2026, with its key numbers and
# opening choices read from a weights file that tools/selfplay.py tunes through self-play and games against Tier 2.
# Two builds of the same code: RL_ClaudeOpus55 plays the current best weights, RL_ClaudeOpus55Candidate the ones under
# test (bwapi-data/AI/RL_ClaudeOpus55-best.json and -candidate.json). See README.md here.
stardust_bot(
        NAME RL_ClaudeOpus55
        RACE Protoss
        SOURCES *.cpp
        HEADER RL_ClaudeOpus55.h
        CREATE "new RL_ClaudeOpus55()"
        DEFINITIONS RL_CLAUDEOPUS55_WEIGHTS=best
)
stardust_bot(
        NAME RL_ClaudeOpus55Candidate
        RACE Protoss
        SOURCES *.cpp
        HEADER RL_ClaudeOpus55.h
        CREATE "new RL_ClaudeOpus55()"
        DEFINITIONS RL_CLAUDEOPUS55_WEIGHTS=candidate
)

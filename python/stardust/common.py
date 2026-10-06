"""State shared by the whole bot (Common.h, plus `currentFrame` from Log.h).

Read as `common.current_frame`; importing the value would freeze it.
"""

# Frames since the bot started playing (BWAPI frame count minus any frames skipped by tests)
current_frame: int = 0

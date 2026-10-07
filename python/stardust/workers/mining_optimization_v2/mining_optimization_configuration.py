"""Port of Workers/MiningOptimizationV2/MiningOptimizationConfiguration.h: configuration of the mining optimization."""

# Whether to use the next path lengths when scoring a path
# Disabled in Stardust as it doesn't seem to have any net positive effect (some patches are improved, some are
# worsened), so the next path length data is not ported
USE_NEXT_PATH_LENGTHS = False

# How much we weight the estimated length of the next path in the scoring of a planned path
NEXT_PATH_LENGTH_WEIGHT = 0.0

# The probability threshold we use to go for patch locking
PATCH_LOCK_THRESHOLD = 0.99

# Which path cache to use: the deserialized path cache (True) or no caching (False)
USE_DESERIALIZED_PATH_CACHE = True

# In the absence of pathing information, the distance from the patch where we assume a resend will always allow the
# worker to arrive on time
# TODO: Analyze the resend always arrives data to validate this value
ASSUME_RESEND_ALWAYS_ARRIVES_DISTANCE = 20

# Whether to enable the gather takeover logic or just let the workers patch switch and resend
ENABLE_TAKEOVER_LOGIC = True

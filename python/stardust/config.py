"""Build-time switches from Stardust's CMake configuration and Instrumentation/DebugFlag_*.h.

Stardust's non-Release OpenBW builds enable INSTRUMENTATION_ENABLED and INSTRUMENTATION_ENABLED_VERBOSE. Verbose
instrumentation is expensive in Python, so it is off by default here; set STARDUST_INSTRUMENTATION_VERBOSE=1 to enable
it.
"""

import os


def _flag(name: str, default: bool) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    return value.lower() in ("1", "true", "yes", "on")


IS_OPENBW = True
VS_HUMAN = _flag("STARDUST_VS_HUMAN", False)  # CMake option: Vs. Human Build
LOGGING_ENABLED = _flag("STARDUST_LOGGING", True)
INSTRUMENTATION_ENABLED = _flag("STARDUST_INSTRUMENTATION", True)
INSTRUMENTATION_ENABLED_VERBOSE = INSTRUMENTATION_ENABLED and _flag("STARDUST_INSTRUMENTATION_VERBOSE", False)

# Instrumentation/Log.h
DEBUG_LOGGING_ENABLED = LOGGING_ENABLED and INSTRUMENTATION_ENABLED_VERBOSE

# Instrumentation/CherryVis.h
CHERRYVIS_ENABLED = INSTRUMENTATION_ENABLED

# Instrumentation/DebugFlag_CombatSim.h
DEBUG_COMBATSIM = False  # Draws each sim result and writes log messages for interesting transitions
DEBUG_COMBATSIM_LOG = False  # Writes log messages for each sim result
DEBUG_COMBATSIM_CVIS = INSTRUMENTATION_ENABLED  # Writes combat sim data to cherryvis
# Stardust records each unit's sim state after every simulated frame whenever DEBUG_COMBATSIM_CVIS is on; that needs a
# round trip to the native sim per frame, so here it is a separate flag that is off by default
DEBUG_COMBATSIM_CVIS_UNIT_LOG = DEBUG_COMBATSIM_CVIS and _flag("STARDUST_COMBATSIM_UNIT_LOG", False)
DEBUG_COMBATSIM_EACHFRAME = False  # Writes values after each frame of the sim (verbose only in Stardust)

# Instrumentation/DebugFlag_GridUpdates.h
DEBUG_GRID_UPDATES = False  # Writes a log message whenever a grid is updated (verbose only in Stardust)

# Instrumentation/DebugFlag_MiningOptimization.h
OUTPUT_STATISTICS = LOGGING_ENABLED  # High-level statistics about mining optimization
VERBOSE_PATH_LOGGING = False
VERBOSE_TAKEOVER_LOGGING = False

# Instrumentation/DebugFlag_UnitOrders.h (verbose only in Stardust)
DEBUG_UNIT_ORDERS = INSTRUMENTATION_ENABLED_VERBOSE  # Writes a log message for each order sent to a unit
DEBUG_UNIT_BOIDS = INSTRUMENTATION_ENABLED_VERBOSE  # Writes a log message for all boids

# Instrumentation/DebugFlag_WorkerMiningOptimization.h
TAKEOVER_DEBUG = False
OPTIMALPOSITIONS_DEBUG = False
OPTIMALRETURN_DEBUG = False
OPTIMALPOSITIONS_DEBUG_VERBOSE = False
OPTIMALRETURN_DEBUG_VERBOSE = False

# Units/Units.h
USE_BUNKER_ATTACKER_LOGIC = True  # Track how many marines are in enemy bunkers from observed bullets

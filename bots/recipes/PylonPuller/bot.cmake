# Pylon Puller (AIIDE 2022 version) by Hao Pan. See bots/recipes/PylonPuller/recipe.json. The whole bot is in its
# header, which the factory includes, so Dll.cpp is left out.
stardust_bot(
        NAME PylonPuller
        RACE Protoss
        SOURCES BWEB/*.cpp BWEM/*.cpp BWEM/BaseFinder/*.cpp
        INCLUDE_DIRS .
        HEADER "Pylon Puller.h"
        CREATE "new PylonPuller()"
        CXX_STANDARD 17
        DEFINITIONS NDEBUG
        MSVC_COMPAT
)

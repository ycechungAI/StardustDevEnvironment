# BunkerBoxer (AIIDE 2019 version). See bots/recipes/BunkerBoxer/recipe.json.
stardust_bot(
        NAME BunkerBoxer
        RACE Terran
        VCXPROJ BunkerBoxerModule/BunkerBoxerModule.vcxproj
        EXCLUDE BunkerBoxerModule/Dll.cpp
        INCLUDE_DIRS BunkerBoxerModule BunkerBoxerModule/BWEM-1.4.1/src
        HEADER BunkerBoxerModule.h
        CREATE "new BunkerBoxerModule()"
        CXX_STANDARD 17
        DEFINITIONS NDEBUG
        MSVC_COMPAT
)

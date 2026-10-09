# ZZZKBot 1.7.0 by Chris Coxe. See bots/recipes/ZZZKBot/recipe.json.
stardust_bot(
        NAME ZZZKBot
        RACE Zerg
        SOURCES ZZZKBot/Source/*.cpp
        EXCLUDE ZZZKBot/Source/Dll.cpp
        INCLUDE_DIRS ZZZKBot/Source
        HEADER ZZZKBotAIModule.h
        CREATE "new ZZZKBotAIModule()"
        CXX_STANDARD 17
        DEFINITIONS NDEBUG
        MSVC_COMPAT
)

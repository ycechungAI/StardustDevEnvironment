# Stardust (the original C++ bot, AIIDE 2025 version) by Bruce Nielsen. See bots/recipes/Stardust2025/recipe.json.
# Play as it instead of the Python port with tools/run_games.py --bot Stardust2025.
stardust_bot(
        NAME Stardust2025
        RACE Protoss
        SOURCES src/src/*.cpp src/3rdparty/BWEM/src/*.cpp src/3rdparty/zstdstream/zstdstream.cpp
        EXCLUDE src/src/Dll.cpp
        INCLUDE_DIRS src/3rdparty src/3rdparty/nlohmann src/3rdparty/BWEM src/3rdparty/BWEM/include src/3rdparty/FAP src/3rdparty/cppcrc
                     src/3rdparty/zstdstream src/3rdparty/bitsery/include
                     src/src src/src/Builder src/src/General src/src/Instrumentation src/src/Map
                     src/src/Map/PathFinding src/src/Players src/src/Producer src/src/Strategist src/src/Units
                     src/src/Util src/src/Workers
        HEADER StardustAIModule.h
        CREATE "new StardustAIModule()"
        CXX_STANDARD 20
        DEFINITIONS LOGGING_ENABLED=1   # the plain game log, which tools/run_games.py reads for progress
        DATA_AI bwapi-data/AI/*.zstd bwapi-data/AI/*.json
)
target_link_libraries(bot_Stardust2025 PRIVATE libzstd_static)

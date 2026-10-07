# Stardust (the original C++ bot, AIIDE 2025 version) by Bruce Nielsen. See bots/recipes/Stardust2025/recipe.json.
# Play as it instead of the Python port with tools/run_games.py --bot Stardust2025.
# BWEM is built separately, as in Stardust's own build: its headers (base.h, map.h) would otherwise be found for
# Stardust's Base.h and Map.h on a case-insensitive filesystem (macOS). Stardust reaches BWEM through
# 3rdparty/BWEM/bwem.h instead.
file(GLOB stardust2025_bwem_sources ${CMAKE_CURRENT_LIST_DIR}/src/3rdparty/BWEM/src/*.cpp)
add_library(bot_Stardust2025_bwem STATIC ${stardust2025_bwem_sources})
target_include_directories(bot_Stardust2025_bwem PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/3rdparty/BWEM/include
                           ${CMAKE_CURRENT_LIST_DIR}/src/3rdparty/BWEM)
target_link_libraries(bot_Stardust2025_bwem PUBLIC BWAPILIB)
set_target_properties(bot_Stardust2025_bwem PROPERTIES CXX_STANDARD 20 POSITION_INDEPENDENT_CODE ON)
target_compile_options(bot_Stardust2025_bwem PRIVATE -w)

stardust_bot(
        NAME Stardust2025
        RACE Protoss
        SOURCES src/src/*.cpp src/3rdparty/zstdstream/zstdstream.cpp
        EXCLUDE src/src/Dll.cpp
        INCLUDE_DIRS src/3rdparty src/3rdparty/nlohmann src/3rdparty/BWEM src/3rdparty/FAP src/3rdparty/cppcrc
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
target_link_libraries(bot_Stardust2025 PRIVATE libzstd_static bot_Stardust2025_bwem)

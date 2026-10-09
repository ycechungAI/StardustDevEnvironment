# CreativeTerran: Steamhammer 5.3.6 (the source in bots/Steamhammer2025) opening only with gambits. See README.md here.
stardust_bot(
        NAME CreativeTerran
        RACE Terran
        VCXPROJ ../Steamhammer2025/src/Steamhammer/VisualStudio/UAlbertaBot.vcxproj
        SOURCES ../Steamhammer2025/src/RC/Source/*.cpp
        EXCLUDE ../Steamhammer2025/src/Steamhammer/Source/Dll.cpp
        INCLUDE_DIRS ../Steamhammer2025/src/Steamhammer/Source
        HEADER UAlbertaBotModule.h
        CREATE "new UAlbertaBot::UAlbertaBotModule()"
        CXX_STANDARD 17
        DEFINITIONS STEAMHAMMER_CONFIG_NAME=CreativeTerran   # reads bwapi-data/AI/CreativeTerran.json
        DATA_AI CreativeTerran.json
        MSVC_COMPAT
)

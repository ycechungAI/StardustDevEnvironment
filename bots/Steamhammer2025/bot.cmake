# Steamhammer 5.3.6 (AIIDE 2025) by Jay Scott, descended from UAlbertaBot. See bots/recipes/Steamhammer2025/recipe.json.
# (The harness's built-in Steamhammer opponent is an older version.)
stardust_bot(
        NAME Steamhammer2025
        RACE Zerg
        VCXPROJ src/Steamhammer/VisualStudio/UAlbertaBot.vcxproj
        SOURCES src/RC/Source/*.cpp       # the RC library it links
        EXCLUDE src/Steamhammer/Source/Dll.cpp
        INCLUDE_DIRS src/Steamhammer/Source
        HEADER UAlbertaBotModule.h
        CREATE "new UAlbertaBot::UAlbertaBotModule()"
        CXX_STANDARD 17
        DATA_AI dll/*.json dll/prepared
        MSVC_COMPAT
)

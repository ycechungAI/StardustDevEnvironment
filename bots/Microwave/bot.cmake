# Microwave (AIIDE 2025) by Micky Holdorf, a Steamhammer fork. See bots/recipes/Microwave/recipe.json.
stardust_bot(
        NAME Microwave
        RACE Zerg
        VCXPROJ src/Microwave/VisualStudio/UAlbertaBot.vcxproj
        EXCLUDE src/Microwave/Source/Dll.cpp
        INCLUDE_DIRS src/Microwave/Source src/BWEM-community/src src/BWEB/Source src/BwapiAutoObs/src
        HEADER UAlbertaBotModule.h
        CREATE "new UAlbertaBot::UAlbertaBotModule()"
        CXX_STANDARD 17
        DATA_AI dll/*.json dll/Training
        MSVC_COMPAT
)

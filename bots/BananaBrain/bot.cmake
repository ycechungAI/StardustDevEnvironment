# BananaBrain (AIIDE 2025 version) by Johan de Jong. See bots/recipes/BananaBrain/recipe.json.
stardust_bot(
        NAME BananaBrain
        RACE Protoss
        SOURCES src/Source/*.cpp src/BWEM/*.cpp
        EXCLUDE src/Source/Dll.cpp
        INCLUDE_DIRS src/Source src/BWEM
        HEADER BananaBrain.h
        CREATE "new BananaBrain()"
        CXX_STANDARD 17
        DATA_AI AI/*.txt
        MSVC_COMPAT
)

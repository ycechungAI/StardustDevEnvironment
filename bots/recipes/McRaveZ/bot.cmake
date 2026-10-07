# McRave (Zerg, AIIDE 2024/2025 entry "McRaveZ") by Christian McCrave. See bots/recipes/McRaveZ/recipe.json.
# (The harness's built-in McRave opponent is an older version.)
stardust_bot(
        NAME McRaveZ
        RACE Zerg
        VCXPROJ VisualStudio/McRave.vcxproj
        EXCLUDE Source/McRave/Main/Dll.cpp
        INCLUDE_DIRS Source Source/McRave Source/McRave/Main Source/BWEM Source/BWEB Source/Horizon
        HEADER Header.h
        CREATE "new McRaveModule()"
        MSVC_COMPAT
)

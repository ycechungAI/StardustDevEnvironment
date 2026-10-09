# WillyT (AIIDE 2021 version) by Nico Klausner. See bots/recipes/WillyT/recipe.json.
stardust_bot(
        NAME WillyT
        RACE Terran
        VCXPROJ WillyT.vcxproj
        EXCLUDE Source/Dll.cpp
        INCLUDE_DIRS Source
        HEADER WillytAI.h
        CREATE "new WillytAI()"
        CXX_STANDARD 17
        DEFINITIONS NDEBUG
        MSVC_COMPAT
)

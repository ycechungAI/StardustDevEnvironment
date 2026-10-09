# Stone (AIIDE 2015 version) by Igor Dimitrijevic. See bots/recipes/Stone/recipe.json.
stardust_bot(
        NAME Stone
        RACE Terran
        VCXPROJ Stone/Stone.vcxproj
        EXCLUDE Stone/dllmain.cpp
        INCLUDE_DIRS Stone
        HEADER Stone.h
        CREATE "new stone::Stone()"
        CXX_STANDARD 14
        DEFINITIONS NDEBUG
        MSVC_COMPAT
)

# SAIDA (AIIDE 2018 version) by Samsung SDS. See bots/recipes/SAIDA/recipe.json.
stardust_bot(
        NAME SAIDA
        RACE Terran
        VCXPROJ src/SAIDA.vcxproj
        EXCLUDE src/SAIDA/main.cpp src/SAIDA/dllmain.cpp
        INCLUDE_DIRS src/SAIDA src/SAIDA/BWEM
        HEADER MyBotModule.h
        CREATE "new MyBot::MyBotModule()"
        CXX_STANDARD 17
        DEFINITIONS SERVERLOG SERVERLOGDLL NDEBUG   # as in its tournament build (Release_Server_DLL)
        MSVC_COMPAT
)

# UAlbertaBot with its build-order search (BOSS) and combat simulator (SparCraft) compiled in, from the files each
# Visual Studio project builds. It plays any race, with the strategy UAlbertaBot_Config.txt gives that race; one library
# is registered once per race.
stardust_bot(
        NAME UAlbertaBotProtoss
        RACE Protoss
        SOURCES UAlbertaBot/Source/*.cpp BOSS/source/*.cpp SparCraft/source/*.cpp
        EXCLUDE UAlbertaBot/Source/main.cpp UAlbertaBot/Source/UnitInfoManager.cpp UAlbertaBot/Source/research/*.cpp
                BOSS/source/BOSSExperiments.cpp BOSS/source/BOSSParameters.cpp BOSS/source/BOSSPlotBuildOrders.cpp
                BOSS/source/BOSS_main.cpp BOSS/source/BuildOrderTester.cpp BOSS/source/CombatSearchExperiment.cpp
                BOSS/source/DFBB_BuildOrderSearchSaveState.cpp BOSS/source/StarCraftGUI.cpp BOSS/source/deprecated/*.cpp
                SparCraft/source/TutorialCode.cpp SparCraft/source/gui/*.cpp SparCraft/source/main/*.cpp
        INCLUDE_DIRS UAlbertaBot/Source BOSS/source SparCraft/source
        HEADER UAlbertaBotAIModule.h
        CREATE "new UAlbertaBotAIModule()"
        CXX_STANDARD 17
        DEFINITIONS NDEBUG
        MSVC_COMPAT
        DATA_AI UAlbertaBot/bin/UAlbertaBot_Config.txt
)
foreach (race Terran Zerg)
    stardust_register_bot(NAME UAlbertaBot${race} RACE ${race} TARGET bot_UAlbertaBotProtoss
                          HEADER UAlbertaBotAIModule.h CREATE "new UAlbertaBotAIModule()")
endforeach ()

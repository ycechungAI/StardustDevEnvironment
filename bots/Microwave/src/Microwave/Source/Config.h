#pragma once

#include "BWAPI.h"
#include <cassert>
#include <set>

namespace Config
{
    namespace ConfigFile
    {
        extern bool ConfigFileFound;
        extern bool ConfigFileParsed;
        extern std::string ConfigFileLocation;
    }

    namespace Modules
    {
        extern bool UsingGameCommander;			
        extern bool UsingScoutManager;
        extern bool UsingCombatCommander;
        extern bool UsingUnitCommandManager;
        extern bool UsingBuildOrderSearch;
		extern bool UsingAutoObserver;
        extern bool UsingStrategyIO;
		extern bool UsingStrategyLearning;
    }
    
    namespace BotInfo
    {
        extern std::string BotName;
		extern std::string Version;
        extern std::string Authors;
        extern bool PrintInfoOnStart;
    }

    namespace Strategy
    {
        extern std::string StrategyName;
		extern std::string ReadDirTraining;
		extern std::string ReadDir;
        extern std::string WriteDir;
		extern bool ScoutHarassEnemy;
		extern bool PlayGoodStrategiesFirst;
        extern bool UseEnemySpecificStrategy;
        extern bool FoundEnemySpecificStrategy;
		extern bool UseMapSpecificStrategy;
		extern bool FoundMapSpecificStrategy;
		extern bool UseHumanSpecificStrategy;
		extern bool FoundHumanSpecificStrategy;
		extern bool UsePlanRecognizer;
		extern int MaxGameRecords;
		extern bool TrainingMode;
    }

    namespace BWAPIOptions
    {
        extern int SetLocalSpeed;
        extern int SetFrameSkip;
        extern bool EnableUserInput;
        extern bool EnableCompleteMapInformation;
    }

    namespace Tournament
    {
        extern int GameEndFrame;	
    }

    namespace Debug
    {
        extern bool DrawGameInfo;
        extern bool DrawBuildOrderSearchInfo;
        extern bool DrawUnitHealthBars;
        extern bool DrawResourceInfo;
        extern bool DrawProductionInfo;
        extern bool DrawScoutInfo;
        extern bool DrawWorkerInfo;
        extern bool DrawModuleTimers;
        extern bool DrawReservedBuildingTiles;
        extern bool DrawCombatSimulationInfo;
        extern bool DrawBuildingInfo;
        extern bool DrawMouseCursorInfo;
        extern bool DrawEnemyUnitInfo;
		extern bool DrawHiddenEnemies;
        extern bool DrawBWTAInfo;
		extern bool DrawBWEMInfo;
		extern bool DrawBWEBInfo;
		extern bool DrawMapInfo;
        extern bool DrawMapGrid;
		extern bool DrawMapDistances;
		extern bool DrawBaseInfo;
		extern bool DrawUnitTargetInfo;
		extern bool DrawUnitOrders;
        extern bool DrawSquadInfo;
		extern bool DrawClusters;
		extern bool DrawDefenseClusters;
		extern bool PrintModuleTimeout;
		extern bool DrawStrategyInfo;
		extern bool DrawStrategyDebug;
		extern bool LogDebug;

        extern std::string ErrorLogFilename;
        extern bool LogAssertToErrorFile;

        extern BWAPI::Color ColorLineTarget;
        extern BWAPI::Color ColorLineMineral;
        extern BWAPI::Color ColorUnitNearEnemy;
        extern BWAPI::Color ColorUnitNotNearEnemy;
    }

    namespace Micro
    {
		extern bool UseCombatSimulation;
        extern bool KiteWithRangedUnits;
        extern std::set<BWAPI::UnitType> KiteLongerRangedUnits;
        extern bool WorkersDefendRush;
        extern int RetreatMeleeUnitShields;
        extern int RetreatMeleeUnitHP;
		extern int CombatRegroupRadius;
        extern int UnitNearEnemyRadius;
		extern int ScoutDefenseRadius;
    }

    namespace Macro
    {
        extern int WorkersPerRefinery;
		extern double WorkersPerPatch;
        extern int BuildingSpacing;
        extern int PylonSpacing;
    }

    namespace Tools
    {
        extern int MAP_GRID_SIZE;
    }
}
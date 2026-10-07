#include "Config.h"
#include "UABAssert.h"

namespace Config
{
    namespace ConfigFile
    {
        bool ConfigFileFound                = false;
        bool ConfigFileParsed               = false;
        std::string ConfigFileLocation      = "bwapi-data/AI/Microwave_Config.json";
    }

    namespace Strategy
    {
        std::string StrategyName            = "9PoolSpeed";					// default
		std::string ReadDirTraining			= "bwapi-data/AI/Training/";
		std::string ReadDir					= "bwapi-data/read/";
        std::string WriteDir                = "bwapi-data/write/";
        bool ScoutHarassEnemy               = true;
		bool PlayGoodStrategiesFirst        = true;
        bool UseEnemySpecificStrategy       = true;
        bool FoundEnemySpecificStrategy     = false;
		bool UseMapSpecificStrategy			= true;
		bool FoundMapSpecificStrategy		= false;
		bool UseHumanSpecificStrategy		= false;
		bool FoundHumanSpecificStrategy		= false;
		bool UsePlanRecognizer				= true;
		int MaxGameRecords					= 1000;
		bool TrainingMode					= false;
    }

    namespace Modules						    
    {
        // the default tournament bot modules
        bool UsingGameCommander             = true;     // toggle GameCommander, effectively UAlbertaBot
        bool UsingScoutManager              = true;
        bool UsingCombatCommander           = true;
        bool UsingBuildOrderSearch          = false;    // toggle use of Build Order Search
		bool UsingAutoObserver              = false;
        bool UsingStrategyIO                = false;    // toggle the use of file io for strategy learning
		bool UsingStrategyLearning          = false;    // toggle the use of strategy learning
		bool UsingUnitCommandManager        = false;    // handles all unit commands
		
        // extra things, don't enable unless you know what they are
        bool UsingBuildOrderDemo            = false;
    }

    namespace BotInfo
    {
        std::string BotName                 = "Microwave";
		std::string Version					= "v2.38";
        std::string Authors                 = "Micky Holdorf";
        bool PrintInfoOnStart               = true;
    }

    namespace BWAPIOptions
    {
        int SetLocalSpeed                   = 21;
        int SetFrameSkip                    = 0;
        bool EnableUserInput                = false;
        bool EnableCompleteMapInformation   = false;
    }
    
    namespace Tournament						
    {
        int GameEndFrame                    = 86400;	
    }
    
    namespace Debug								
    {
        bool DrawGameInfo                   = false;
        bool DrawUnitHealthBars             = false;
        bool DrawProductionInfo             = false;
        bool DrawBuildOrderSearchInfo       = false;
        bool DrawScoutInfo                  = false;
        bool DrawResourceInfo               = false;
        bool DrawWorkerInfo                 = false;
        bool DrawModuleTimers               = false;
        bool DrawReservedBuildingTiles      = false;
        bool DrawCombatSimulationInfo       = false;
        bool DrawBuildingInfo               = false;
        bool DrawMouseCursorInfo            = false;
        bool DrawEnemyUnitInfo              = false;
		bool DrawHiddenEnemies				= false;
        bool DrawBWTAInfo                   = false;
		bool DrawBWEMInfo					= false;
		bool DrawBWEBInfo					= false;
		bool DrawMapInfo					= false;
		bool DrawMapGrid					= false;
		bool DrawMapDistances				= false;
		bool DrawBaseInfo					= false;
		bool DrawUnitTargetInfo				= false;
		bool DrawUnitOrders					= false;
        bool DrawSquadInfo                  = false;
		bool DrawClusters					= false;
		bool DrawDefenseClusters			= false;
		bool PrintModuleTimeout				= false;
		bool DrawStrategyInfo				= false;
		bool DrawStrategyDebug				= false;
		bool LogDebug						= false;

        std::string ErrorLogFilename        = "Microwave_ErrorLog.txt";
        bool LogAssertToErrorFile           = false;

        BWAPI::Color ColorLineTarget        = BWAPI::Colors::White;
        BWAPI::Color ColorLineMineral       = BWAPI::Colors::Cyan;
        BWAPI::Color ColorUnitNearEnemy     = BWAPI::Colors::Red;
        BWAPI::Color ColorUnitNotNearEnemy  = BWAPI::Colors::Green;
    }

    namespace Micro								
    {
        bool UseCombatSimulation            = true;
        bool KiteWithRangedUnits            = true;
        std::set<BWAPI::UnitType> KiteLongerRangedUnits;
        bool WorkersDefendRush              = true;
		int RetreatMeleeUnitShields         = 0;
        int RetreatMeleeUnitHP              = 8;
		int CombatRegroupRadius				= 600;      // radius of units around frontmost unit for combat sim
		int UnitNearEnemyRadius				= 600;      // radius to consider a unit 'near' to an enemy unit
		int ScoutDefenseRadius				= 600;		// radius to chase enemy scout worker
	}

    namespace Macro
    {
        int BOSSFrameLimit                  = 160;
        int WorkersPerRefinery              = 3;
		double WorkersPerPatch				= 1.5;
		int BuildingSpacing					= 1;
        int PylonSpacing                    = 3;
    }

    namespace Tools								
    {
        extern int MAP_GRID_SIZE            = 320;      // size of grid spacing in MapGrid
    }
}
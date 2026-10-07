#pragma once

#include "BuildingPlacer.h"
#include "CombatSimulation.h"
#include "GridAttacks.h"
#include "GridCreep.h"
#include "GridInset.h"
#include "GridRoom.h"
#include "GridSafePath.h"
#include "GridTileRoom.h"
#include "GridZone.h"
#include "MapPartitions.h"
#include "MapTools.h"
#include "Micro.h"
#include "OpsBoss.h"
#include "PlayerSnapshot.h"
#include "SkillKit.h"
#include "Tasks.h"

// Central access to many components.
#define the (The::Root())

namespace UAlbertaBot
{
	// Forward declarations.
    class Bases;
	class BuildingPilot;
	class Evaluator;
	class Forecast;
	class Maker;
    class MapGrid;
	class Moveout;
    class InformationManager;
    class ProductionManager;
    class OpeningTiming;
	class OpponentModel;
	class OpsStrategy;
    class Random;
	class SpiderMineData;
    class StaticDefense;
	class MicroStaticDefense;

	// For example, the.my.completed.count(BWAPI::UnitTypes::Zerg_Lurker).
    struct My
    {
        PlayerSnapshot completed;		// my completed units
        PlayerSnapshot all;				// my completed and uncompleted units
    };
	
	// For all unit types the enemy is known to have or have had, the.your.ever.count(type) + the.your.inferred.count(type).
	// To count all units the enemy is known to have now, the.your.seen.count(type) + the.your.inferred.count(type).
	// Usually you don't need to know about the inferred units, which are tech requirements.
	// Steamhammer does not try to infer more than one unit of a type. Zealot implies gateway, but many zealots do not
	// imply more gateways, as far as Steamhammer knows.
    struct Your
    {
        PlayerSnapshot seen;			// counts of your seen units
        PlayerSnapshot ever;			// your unit types that have ever been seen in this game (0 or 1)
        PlayerSnapshot inferred;		// unit types that you are inferred to have, excluding seen unit types (0 or 1)
    };

    class The
    {
    private:
        BWAPI::Race _selfRace;
		bool _airTasksStarted;

		void maybeInitializeEvaluator();		// can't be done up front if the enemy is random
		void maybeStartAirTasks();

    public:
        The();
        // Initialize The. Call this once per game in onStart().
        void initialize();

        // Map information.

        GridRoom vWalkRoom;
        // How much room is there around this tile? A rough estimate of how much stuff fits there.
        GridTileRoom tileRoom;
        // How far is this walk tile from the nearest wall?
        GridInset inset;
        // What zone is this tile in?
        GridZone zone;
        // What map partition is this walk tile in? You can walk between places in the same partition.
        MapPartitions partitions;
        // Map information and calculations.
        MapTools map;
		// Creep.
		GridCreep creep;

        // Managers.

        // Information about bases and resources.
        Bases & bases;
        // Combat sim does keep some game-duration info of its own.
        CombatSimulation combatSim;
        // Large cells laid over the map.
        MapGrid & grid;
        // Game state information, especially stored information about the enemy.
        InformationManager & info;
        // Perform unit control actions ("unit micro").
        Micro micro;
		// Lift and land floating buildings.
		BuildingPilot & buildingPilot;
        // Compare openings by the times of events in them.
        OpeningTiming & openingTiming;
        // Place buildings. Find macro locations.
        BuildingPlacer placer;
        // Make stuff.
        ProductionManager & production;		// queue production
		Maker & maker;						// terran and protoss non-queue production (most production)
		// Moveout.
		Moveout & moveout;
        // Operations.
        OpsBoss ops;
		// Operations strategy. Mostly prospective.
		OpsStrategy & opsStrategy;
		// Generate random numbers.
		Random & random;
		// Extensible set of skills using the opponent model.
        SkillKit skillkit;
		// Tracking spider mines and minelaying operations.
		SpiderMineData & mines;
        // Defense buildings.
        StaticDefense & staticDefense;
		MicroStaticDefense & microStaticDefense;
		// Position evaluator.
		Evaluator & evaluator;
		// Forecast the enemy.
		Forecast & forecast;
		// Asynchronous tasks.
        Tasks tasks;

        // Varying during the game.

        // My current unit counts.
        My my;
        // Your current unit counts.
        Your your;
		// Unit counts for either side.
		int getKnownCount(BWAPI::Player side, BWAPI::UnitType type) const;

		// How many enemy units hit each tile on the ground?
        GroundAttacksFixed groundHitsFixed;
        GroundAttacksMobile groundHitsMobile;
        // How many enemy units hit each tile in the air?
        AirAttacksFixed airHitsFixed;
        AirAttacksMobile airHitsMobile;
        // How many immobile enemy units could hit this unit, at its actual or another location?
        int staticHits(BWAPI::Unit unit, const BWAPI::TilePosition & tile) const;
        int staticHits(BWAPI::Unit unit) const;
		// How many total enemy units could hit ground/air at this location?
		int groundHits(const BWAPI::TilePosition & tile) const;
		int groundHits(const BWAPI::Position & pos) const;
		int airHits(const BWAPI::TilePosition & tile) const;
		int airHits(const BWAPI::Position & pos) const;
		int hits(bool air, const BWAPI::TilePosition & tile) const;
		int hits(bool air, const BWAPI::Position & pos) const;
		// How many total enemy units could hit this unit?
        int hits(BWAPI::Unit unit) const;
		// See also the.info.getEnemyFireteam(myUnit).

		bool enemyBaseUnderSurveillance() const;

        // Update the varying values.
        void update();

        // Utility.

        // The player (this bot).
        BWAPI::Player self()      const { return BWAPI::Broodwar->self(); };
        // The enemy player.
        BWAPI::Player enemy()     const { return BWAPI::Broodwar->enemy(); };
        // The neutral player, which owns things like resources and critters.
        BWAPI::Player neutral()   const { return BWAPI::Broodwar->neutral(); };

        // The bot's race, terran protoss zerg.
        BWAPI::Race selfRace()    const { return _selfRace; };
        // The enemy's race, terran protoss zerg unknown.
        // It changes from unknown to another value when a random player is first scouted.
        BWAPI::Race enemyRace()   const { return BWAPI::Broodwar->enemy()->getRace(); };

        // Current frame count, same as Broodwar->getFrameCount().
        int now() const { return BWAPI::Broodwar->getFrameCount(); };

		OpponentModel & oppModel();

        static inline The & Root() {
            static The TheRoot;
            return TheRoot;
        };
    };
}

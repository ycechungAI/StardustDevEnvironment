
#include "The.h"

#include "Bases.h"
#include "BuildingPilot.h"
#include "Evaluator.h"
#include "Forecast.h"
#include "GridTasks.h"
#include "InformationManager.h"
#include "Maker.h"
#include "MapGrid.h"
#include "Moveout.h"
#include "OpeningTiming.h"
#include "OpponentModel.h"
#include "OpsStrategy.h"
#include "ParseUtils.h"
#include "ProductionManager.h"
#include "Random.h"
#include "ScoutManager.h"
#include "SpiderMineData.h"
#include "StaticDefense.h"
#include "MicroStaticDefense.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

// The evaluator can't be initialized before the enemy race is known.
// If the enemy is random, we delay it.
void The::maybeInitializeEvaluator()
{
	if (the.enemyRace() != BWAPI::Races::Unknown &&
		Config::IO::UseEvaluator &&
		!evaluator.isInitialized())
	{
		evaluator.initialize();
		if (Config::IO::UpdateEvaluator)
		{
			tasks.post(new EvaluatorTask());
		}
	}
}

// Start the tasks to compute various aspects of air safety for our units.
// Zerg has overlords and starts the tasks immediately.
void The::maybeStartAirTasks()
{
	if (!_airTasksStarted && the.info.weHaveAirUnits())
	{
		tasks.post(new AirHitsMobileTask());
		tasks.post(new AirSafePathTask());
		_airTasksStarted = true;
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

The::The()
	: bases(Bases::Instance())
	, evaluator(*new Evaluator())
	, forecast(*new Forecast())
	, grid(MapGrid::Instance())
	, info(InformationManager::Instance())
	, buildingPilot(*new BuildingPilot)
	, openingTiming(OpeningTiming::Instance())
	, production(ProductionManager::Instance())
	, maker(*new Maker)
	, moveout(*new Moveout)
	, opsStrategy(*new OpsStrategy())
	, mines(*new SpiderMineData())
	, random(Random::Instance())
	, staticDefense(*new StaticDefense)
	, microStaticDefense(*new MicroStaticDefense)
{
}

// NOTE The skill kit is initialized in OpponentModel and updated in GameCommander.

void The::initialize()
{
	_selfRace = BWAPI::Broodwar->self()->getRace();
	
	// The order of initialization is important because of dependencies.
    partitions.initialize();
    inset.initialize();				// depends on partitions
    vWalkRoom.initialize();			// depends on inset
    tileRoom.initialize();			// depends on vWalkRoom
    zone.initialize();				// depends on tileRoom
    map.initialize();
	creep.initialize();				// depends on map
	
	bases.initialize();             // depends on map
    info.initialize();              // depends on bases
    placer.initialize();
    ops.initialize();
	
	// Read the opening timing file, to make opening decisions.
	// TODO unimplemented
    // openingTiming.read();

    // Parse the bot's configuration file.
    // Change this file path (in config.cpp) to point to your config file.
    // Any relative path name will be relative to Starcraft installation folder.
    // The config depends on the map and must be read after the map is analyzed.
    // This also reads the opponent model data and decides on the opening.
    ParseUtils::ParseConfigFile(Config::ConfigFile::ConfigFileLocation);

	// Sets the initial production queue to the book opening chosen above.
    production.initialize();

	// The learned evaluation of winning chances.
	// Updating the evaluator implies using it!
	if (Config::IO::UpdateEvaluator)
	{
		Config::IO::UseEvaluator = true;
	}
	maybeInitializeEvaluator();
	
	// Add tasks that will run throughout the game.
	tasks.post(new AirHitsFixedTask());
	tasks.post(new GroundHitsFixedTask());
    tasks.post(new GroundHitsMobileTask());
	tasks.post(new GroundSafePathTask());
	tasks.post(new BaseBuildingCostTask());
	// The air mobile and air safe path tasks are started if and when we get air units.
	// See maybeStartAirTasks().
}

// Return a unit count for the given side.
int The::getKnownCount(BWAPI::Player side, BWAPI::UnitType type) const
{
	if (side == self())
	{
		return my.completed.count(type);
	}

	if (side == enemy())
	{
		return your.seen.count(type);
	}

	UAB_MESSAGE("won't count neutral units");
	return 0;
}

// Hits from static defense, sieged tanks, and burrowed lurkers.
int The::staticHits(BWAPI::Unit unit, const BWAPI::TilePosition & tile) const
{
    return unit->isFlying()
        ? airHitsFixed.at(tile)
        : groundHitsFixed.at(tile);
}

// Hits from static defense, sieged tanks, and burrowed lurkers.
int The::staticHits(BWAPI::Unit unit) const
{
    return unit->isFlying()
        ? airHitsFixed.at(unit->getTilePosition())
        : groundHitsFixed.at(unit->getTilePosition());
}

// Hits from all enemies on ground units, static and mobile.
int The::groundHits(const BWAPI::TilePosition & tile) const
{
	return groundHitsFixed.at(tile) + groundHitsMobile.at(tile);
}

// Hits from all enemies on ground units, static and mobile.
int The::groundHits(const BWAPI::Position & pos) const
{
	return groundHits(BWAPI::TilePosition(pos));
}

// Hits from all enemies on air units, static and mobile.
int The::airHits(const BWAPI::TilePosition & tile) const
{
	return airHitsFixed.at(tile) + airHitsMobile.at(tile);
}

// Hits from all enemies on air units, static and mobile.
int The::airHits(const BWAPI::Position & pos) const
{
	return airHits(BWAPI::TilePosition(pos));
}

// Air or ground hits.
int The::hits(bool air, const BWAPI::TilePosition & tile) const
{
	return air ? the.airHits(tile) > 0 : the.groundHits(tile);
}

// Air or ground hits.
int The::hits(bool air, const BWAPI::Position & pos) const
{
	return hits(air, BWAPI::TilePosition(pos));
}

// Hits from all enemies, static and mobile.
int The::hits(BWAPI::Unit unit) const
{
    return unit->isFlying()
        ? airHits(unit->getTilePosition())
        : groundHits(unit->getTilePosition());
}

// We have a scout watching the enemy base.
// Only the overlord scout counts; worker scouts are not reliable.
// So this is true only for zerg, and not always then.
bool The::enemyBaseUnderSurveillance() const
{
	return ScoutManager::Instance().enemyBaseUnderSurveillance();
}

// Every frame.
void The::update()
{
	maybeInitializeEvaluator();		// can't be done up front if the enemy is random

    my.completed.takeSelf();
    my.all.takeSelfAll();
    your.seen.takeEnemy();
    your.ever.takeEnemyEver(your.seen);
    your.inferred.takeEnemyInferred(your.ever);
	grid.update();	
	creep.update();
	forecast.update();

    ops.update();
    staticDefense.update();

	maybeStartAirTasks();
}

OpponentModel & The::oppModel()
{
	return OpponentModel::Instance();
}

#include "StrategyManager.h"
#include "ProductionManager.h"
#include "StrategyBossZerg.h"
#include "Random.h"

using namespace UAlbertaBot;

StrategyManager::StrategyManager()
	: _self(BWAPI::Broodwar->self())
	, _enemy(BWAPI::Broodwar->enemy())
	, _selfRace(BWAPI::Broodwar->self()->getRace())
	, _enemyRace(BWAPI::Broodwar->enemy()->getRace())
	, _emptyBuildOrder(BWAPI::Broodwar->self()->getRace())
	, _lingScore(0)
	, _hydraScore(0)
	, _mutaScore(0)
	, _lurkerScore(0)
	, _ultraScore(0)
	, _guardianScore(0)
	, _devourerScore(0)
	, _techTarget("None")
	, _minUnit("None")
	, _gasUnit("None")
	, _buildOrderZerg(BWAPI::Races::Zerg)
	, _droneEco(0)
	, _hasIslandBases(false)
	, _enemyIsRandom(false)
{
	// Profile debug
	//PROFILE_FUNCTION();

	if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Unknown)
	{
		_enemyIsRandom = true;
	}

	_hasIslandBases = MapTools::Instance().hasIslandBases();
}

StrategyManager & StrategyManager::Instance() 
{
	static StrategyManager instance;
	return instance;
}

void  StrategyManager::update()
{
	// Profile debug
	//PROFILE_FUNCTION();

	_enemyRace = BWAPI::Broodwar->enemy()->getRace();
	_recognizedEnemyPlan.update();
	reconsiderEnemyPlan();
}

const int StrategyManager::getScore(BWAPI::Player player) const
{
	return player->getBuildingScore() + player->getKillScore() + player->getRazingScore() + player->getUnitScore();
}

const BuildOrder & StrategyManager::getOpeningBookBuildOrder() const
{
	//if (_selfRace == BWAPI::Races::Zerg && BWAPI::Broodwar->mapFileName() == "(8)Big Game Hunters.scm")
	//{
	//	Config::Strategy::StrategyName = "3HatchHydra_BHG";
	//}
	//else if (_selfRace == BWAPI::Races::Zerg && _hasIslandBases)
	//{
	//	Config::Strategy::StrategyName = "2HatchMuta";
	//}

	auto buildOrderIt = _strategies.find(Config::Strategy::StrategyName);

    // look for the build order in the build order map
	if (buildOrderIt != std::end(_strategies))
    {
        return (*buildOrderIt).second._buildOrder;
    }
    else
    {
        UAB_ASSERT_WARNING(false, "Strategy not found: %s, returning empty initial build order", Config::Strategy::StrategyName.c_str());
        return _emptyBuildOrder;
    }
}

const bool StrategyManager::shouldExpandNow() const
{
	// if there is no place to expand to, we can't expand
	if (MapTools::Instance().getNextExpansion(false, false) == BWAPI::TilePositions::None)
	{
        //BWAPI::Broodwar->printf("No valid expansion location");
		return false;
	}

	// if we have idle workers then we need a new expansion
	if (WorkerManager::Instance().getNumIdleWorkers() > 3)
	{
		return true;
	}

    // if we have a stockpile of minerals, expand
	if (BWAPI::Broodwar->self()->minerals() > 600)
    {
        return true;
    }

	size_t numDepots = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Command_Center)
		+ UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Nexus)
		+ UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
		+ UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair)
		+ UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	int frame = BWAPI::Broodwar->getFrameCount();
	int minute = frame / (24 * 60);

	// we will make expansion N after array[N] minutes have passed
    std::vector<int> expansionTimes = {5, 10, 14, 17, 20, 22};

    for (size_t i(0); i < expansionTimes.size(); ++i)
    {
		if (numDepots < (i + 2) && minute > expansionTimes[i] && 
			BWAPI::Broodwar->self()->minerals() > 150)
        {
            return true;
        }
    }

	return false;
}

void StrategyManager::addStrategy(const std::string & name, const Strategy & strategy)
{
    _strategies[name] = strategy;
}

void StrategyManager::addStrategyMatchup(const std::string & name, int weight)
{
	_strategiesMatchup[name] = weight;
}

const MetaPairVector StrategyManager::getBuildOrderGoal()
{
    if (_selfRace == BWAPI::Races::Protoss)
    {
		return getProtossBuildOrderGoal();
    }
	else if (_selfRace == BWAPI::Races::Terran)
	{
		return getTerranBuildOrderGoal();
	}

    return MetaPairVector();
}

const MetaPairVector StrategyManager::getProtossBuildOrderGoal() const
{
	// the goal to return
	MetaPairVector goal;

	int numWorkers			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Probe);
	int numZealots			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Zealot);
    int numPylons           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Pylon);
	int numDragoons         = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Dragoon);
	int numProbes           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Probe);
	int numNexusCompleted   = BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Nexus);
	int numNexusAll         = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Nexus);
	int numCyber            = BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Cybernetics_Core);
	int numCannon           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Photon_Cannon);
    int numScout            = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Corsair);
    int numReaver           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Reaver);
    int numDarkTeplar       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar);

    if (Config::Strategy::StrategyName == "Protoss_ZealotRush")
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 8));

        // once we have a 2nd nexus start making dragoons
        if (numNexusAll >= 2)
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 4));
        }
    }
    else if (Config::Strategy::StrategyName == "Protoss_DragoonRush")
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 6));
    }
    else if (Config::Strategy::StrategyName == "Protoss_Drop")
    {
        if (numZealots == 0)
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 4));
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Shuttle, 1));
        }
        else
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 8));
        }
    }
    else if (Config::Strategy::StrategyName == "Protoss_DTRush")
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dark_Templar, numDarkTeplar + 2));

        // if we have a 2nd nexus then get some goons out
        if (numNexusAll >= 2)
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 4));
        }
    }
    else
    {
		BWAPI::Broodwar->printf("Warning: Unknown Protoss Strategy: %s", Config::Strategy::StrategyName.c_str());
    }

    // if we have 3 nexus, make an observer
    if (numNexusCompleted >= 3)
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Observer, 1));
    }
    
    // add observer to the goal if the enemy has cloaked units
	if (InformationManager::Instance().enemyHasCloakTech())
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Robotics_Facility, 1));
		
		if (BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Robotics_Facility) > 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Observatory, 1));
		}
		if (BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Observatory) > 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Observer, 1));
		}
	}

    // if we want to expand, insert a nexus into the build order
	if (shouldExpandNow())
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Nexus, numNexusAll + 1));
	}

	return goal;
}

const MetaPairVector StrategyManager::getTerranBuildOrderGoal() const
{
	// the goal to return
	std::vector<MetaPair> goal;

    int numWorkers      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_SCV);
    int numCC           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Command_Center);            
    int numMarines      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Marine);
	int numMedics       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Medic);
	int numWraith       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Wraith);
    int numVultures     = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Vulture);
    int numGoliath      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Goliath);
    int numTanks        = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode);
    int numBay          = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Engineering_Bay);

    if (Config::Strategy::StrategyName == "Terran_MarineRush")
    {
	    goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Marine, numMarines + 8));

        if (numMarines > 5)
        {
            goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Engineering_Bay, 1));
        }
    }
    else if (Config::Strategy::StrategyName == "Terran_4RaxMarines")
    {
	    goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Marine, numMarines + 8));
    }
    else if (Config::Strategy::StrategyName == "Terran_VultureRush")
    {
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Vulture, numVultures + 8));

        if (numVultures > 8)
        {
            goal.push_back(std::pair<MacroAct, int>(BWAPI::TechTypes::Tank_Siege_Mode, 1));
            goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, 4));
        }
    }
    else if (Config::Strategy::StrategyName == "Terran_TankPush")
    {
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, 6));
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Goliath, numGoliath + 6));
        goal.push_back(std::pair<MacroAct, int>(BWAPI::TechTypes::Tank_Siege_Mode, 1));
    }
    else
    {
        BWAPI::Broodwar->printf("Warning: Unknown Terran Strategy: %s", Config::Strategy::StrategyName.c_str());
    }

    if (shouldExpandNow())
    {
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Command_Center, numCC + 1));
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_SCV, numWorkers + 10));
    }

	return goal;
}


void StrategyManager::getProtossBuildOrder(BuildOrderQueue & queue)
{
	// Profile debug
	//PROFILE_FUNCTION();

	int minerals = std::max(0, _self->minerals() - BuildingManager::Instance().getReservedMinerals());
	int gas = std::max(0, _self->gas() - BuildingManager::Instance().getReservedGas());
	int nMinPatches = InformationManager::Instance().getMyNumMineralPatches();
	int nSupplyUsed = BWAPI::Broodwar->self()->supplyUsed();
	int nSupplyTotal = BWAPI::Broodwar->self()->supplyTotal();

	int nGasWorkers = WorkerManager::Instance().getNumGasWorkers();
	int nMinWorkers = WorkerManager::Instance().getNumMineralWorkers();
	int numWorkers = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Probe);
	int numZealots = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Zealot);
	int numDragoons = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Dragoon);
	int numProbes = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Probe);
	int numScout = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Corsair);
	int numReaver = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Reaver);
	int numDarkTeplar = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar);
	int numShuttle = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Shuttle);
	int numObserver = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Observer);

	int numPylons = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Pylon);
	int numNexus = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Nexus);
	int numNexusAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Nexus);
	int numCyber = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Cybernetics_Core);
	int numCannon = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Photon_Cannon);
	int numGates = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Gateway);
	int numCores = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Cybernetics_Core);

	int workerMax = maxWorkers();

	// compensate for ingame min / gas counter, counting 16 each frame
	if (nMinWorkers > 9)
	{
		minerals = std::max(0, minerals - 16);
	}
	if (nGasWorkers > 3)
	{
		gas = std::max(0, gas - 16);
	}

	int mineralsLeft = minerals;
	int gasLeft = gas;
	int supplyLeft = nSupplyTotal - nSupplyUsed;

	if (Config::Strategy::StrategyName == "9-9Gate")
	{
		if (numCyber > 0 && numGates > 0 && mineralsLeft > 0 && supplyLeft > 0 &&
			numDragoons < 0.33 * numZealots) {
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Dragoon));
			mineralsLeft -= 125;
			gasLeft -= 50;
			supplyLeft -= 4;
		}
		if (numGates > 0 && mineralsLeft > 0 && supplyLeft > 0) {
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Zealot));
			mineralsLeft -= 100;
			supplyLeft -= 4;
		}

		if (numWorkers >= 9 && numGates < 3 * numNexus && minerals > 100 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Gateway) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Protoss_Gateway) && 
			!queue.buildingInQueue()) {
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Gateway));
		}
		if (numWorkers >= 9 && numGates >= 2 && minerals > 150 && numZealots > 8 && numCyber == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Cybernetics_Core) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Protoss_Cybernetics_Core) &&
			!queue.buildingInQueue()) {
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Cybernetics_Core));
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Assimilator));
			WorkerManager::Instance().setCollectGas(true);
		}
	}
	else
	{
		BWAPI::Broodwar->printf("Warning: Unknown Protoss Strategy: %s", Config::Strategy::StrategyName.c_str());
	}

	if (numWorkers < workerMax) {
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Probe));
	}

	// if we want to expand, insert a nexus into the build order
	if (shouldExpandNow() && numNexusAll < 4 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Nexus) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Protoss_Nexus))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Nexus));
	}
}


void StrategyManager::getTerranBuildOrder(BuildOrderQueue & queue)
{
	// Profile debug
	//PROFILE_FUNCTION();

	int minerals = std::max(0, _self->minerals() - BuildingManager::Instance().getReservedMinerals());
	int gas = std::max(0, _self->gas() - BuildingManager::Instance().getReservedGas());
	int nMinPatches = InformationManager::Instance().getMyNumMineralPatches();
	int nSupplyUsed = BWAPI::Broodwar->self()->supplyUsed();
	int nSupplyTotal = BWAPI::Broodwar->self()->supplyTotal();

	int nGasWorkers = WorkerManager::Instance().getNumGasWorkers();
	int nMinWorkers = WorkerManager::Instance().getNumMineralWorkers();
	int numWorkers = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_SCV);
	int numMarines = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Marine);
	int numMedics = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Medic);
	int numWraith = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Wraith);
	int numVultures = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Vulture);
	int numGoliath = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Goliath);
	int numTanks = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
		+ UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode);

	int numCC = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Terran_Command_Center);
	int numCCAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Command_Center);
	int numBay = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Engineering_Bay);
	int numRax = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Barracks);
	int numAcademy = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Terran_Academy);

	int workerMax = maxWorkers();

	// compensate for ingame min / gas counter, counting 16 each frame
	if (nMinWorkers > 9)
	{
		minerals = std::max(0, minerals - 16);
	}
	if (nGasWorkers > 3)
	{
		gas = std::max(0, gas - 16);
	}

	int mineralsLeft = minerals;
	int gasLeft = gas;
	int supplyLeft = nSupplyTotal - nSupplyUsed;

	if (Config::Strategy::StrategyName == "8Rax")
	{
		if (numRax > 0 && numAcademy > 0 && mineralsLeft > 0 && supplyLeft > 0 &&
			numMedics < 0.33 * numMarines) {
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Terran_Medic));
			mineralsLeft -= 50;
			gasLeft -= 25;
			supplyLeft -= 2;
		}
		if (numRax > 0 && mineralsLeft > 0 && supplyLeft > 0) {
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Terran_Marine));
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Terran_Marine));
			mineralsLeft -= 100;
			supplyLeft -= 4;
		}

		if (numWorkers >= 8 && numRax < 4 * numCC && minerals > 150 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Barracks) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Terran_Barracks) &&
			!queue.buildingInQueue()) {
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Terran_Barracks));
		}
		if (numWorkers >= 8 && numAcademy == 0 && numRax >= 2 && minerals > 150 && numMarines > 8 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Academy) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Terran_Academy) &&
			!queue.buildingInQueue()) {
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Terran_Academy));
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Terran_Refinery));
			WorkerManager::Instance().setCollectGas(true);
		}
	}
	else
	{
		BWAPI::Broodwar->printf("Warning: Unknown Terran Strategy: %s", Config::Strategy::StrategyName.c_str());
	}

	if (numWorkers < workerMax) {
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Terran_SCV));
	}

	if (shouldExpandNow() && numCCAll < 4 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Command_Center) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Terran_Command_Center))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Terran_Command_Center));
	}

}

void StrategyManager::getZergBuildOrder(BuildOrderQueue & queue)
{
	// Profile debug
	//PROFILE_FUNCTION();

	//queue.clearAll();

	int minerals		= std::max(0, _self->minerals() - BuildingManager::Instance().getReservedMinerals());
	int gas				= std::max(0, _self->gas() - BuildingManager::Instance().getReservedGas());
	int nLarvas			= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Larva);
	int nMinPatches		= InformationManager::Instance().getMyNumMineralPatches();
	int nSupplyUsed		= BWAPI::Broodwar->self()->supplyUsed();
	int nSupplyTotal	= BWAPI::Broodwar->self()->supplyTotal();

	// Tech stuff. It has to be completed for the tech to be available.
	int nLairs			= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	int nLairsAll		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	int nHives			= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	int nHivesAll		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	int nHatches		= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
						+ nLairsAll + nHivesAll;
	int nHatchesAll		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
						+ nLairsAll + nHivesAll;

	int nGas			= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Extractor);
	int nGasAll			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Extractor);
	int nEvo			= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber);
	int nEvoAll			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber);
	bool hasPool		= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0;
	bool hasPoolAll		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0;
	bool hasDen			= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0;
	bool hasDenAll		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0;
	bool hasLurker		= _self->hasResearched(BWAPI::TechTypes::Lurker_Aspect);
	bool hasSpire		= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Spire) > 0 ||
						UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	bool hasSpireAll	= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) > 0 ||
						UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	bool hasUltra		= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) > 0;
	bool hasUltraAll	= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) > 0;
	bool hasGreaterSpire = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	bool hasGreaterSpireAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	bool hasQueensNest	= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Queens_Nest) > 0;
	bool hasQueensNestAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Queens_Nest) > 0;

	int numBuldingsInQueue = 
		queue.numInQueue(BWAPI::UnitTypes::Zerg_Spawning_Pool) +
		queue.numInQueue(BWAPI::UnitTypes::Zerg_Hydralisk_Den) + 
		queue.numInQueue(BWAPI::UnitTypes::Zerg_Spire) +
		queue.numInQueue(BWAPI::UnitTypes::Zerg_Greater_Spire) +
		queue.numInQueue(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) +
		queue.numInQueue(BWAPI::UnitTypes::Zerg_Queens_Nest) +
		queue.numInQueue(BWAPI::UnitTypes::Zerg_Extractor) +
		queue.numInQueue(BWAPI::UnitTypes::Zerg_Evolution_Chamber) +
		queue.numInQueue(BWAPI::UnitTypes::Zerg_Hatchery) + 
		queue.numInQueue(BWAPI::UnitTypes::Zerg_Lair) +
		queue.numInQueue(BWAPI::UnitTypes::Zerg_Hive);

	// Unit stuff. This includes uncompleted units.
	int nDrones			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Drone);
	int nGasDrones		= WorkerManager::Instance().getNumGasWorkers();
	int nMinDrones		= WorkerManager::Instance().getNumMineralWorkers();
	int nLings			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Zergling);
	int nHydras			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk);
	int nHydrasComp		= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk);
	int nLurkers		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lurker);
	int nMutas			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Mutalisk);
	int nMutasComp		= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Mutalisk);
	int nScourge		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Scourge);
	int nUltras			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk);
	int nGuardians		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Guardian);
	int nDevourers		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Devourer);
	int nLurkerEggs		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lurker_Egg);
	int nCocoons		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Cocoon);
	int nGasUnits		= nHydras + nLurkers + nMutas + nScourge + nUltras + nGuardians + nDevourers;
	int nMinUnits		= nLings;

	// hasLairTech means "can research stuff in the lair" (not "can research stuff that needs lair").
	bool hasHive		= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hive) > 0;
	bool hasHiveTech	= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hive) > 0;
	bool hasLair		= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Lair) > 0;
	bool hasLairTech	= hasLair || nHives > 0;

	BWAPI::UpgradeType melee = BWAPI::UpgradeTypes::Zerg_Melee_Attacks;
	BWAPI::UpgradeType missile = BWAPI::UpgradeTypes::Zerg_Missile_Attacks;
	BWAPI::UpgradeType armor = BWAPI::UpgradeTypes::Zerg_Carapace;
	BWAPI::UpgradeType airAttack = BWAPI::UpgradeTypes::Zerg_Flyer_Attacks;
	BWAPI::UpgradeType airArmor = BWAPI::UpgradeTypes::Zerg_Flyer_Carapace;
	BWAPI::TechType lurkerAspect = BWAPI::TechTypes::Lurker_Aspect;
	int meleeUps		= _self->getUpgradeLevel(melee);
	int missileUps		= _self->getUpgradeLevel(missile);
	int armorUps		= _self->getUpgradeLevel(armor);
	int airarmorUps		= _self->getUpgradeLevel(airArmor);
	int airattackUps	= _self->getUpgradeLevel(airAttack);

	// compensate for ingame min / gas counter, counting 16 each frame
	//nLarvas = std::max(0, nLarvas - 1);
	if (nMinDrones > 9) { minerals = std::max(0, minerals - 16); }
	if (nGasDrones > 3) { gas = std::max(0, gas - 16); }

	// maximum drones
	int droneMax = maxWorkers();
	// minimum drones
	int droneMinimal = minWorkers();
	// global drone eco
	double droneRatio = droneEco();
	_droneEco = droneRatio;

	// emergency
	int ourForce = InformationManager::Instance().getPower(_self);
	int enemyForce = InformationManager::Instance().getPower(_enemy);
	int ourForceGround = InformationManager::Instance().getMyPowerGroundWeapon(true);
	int enemyForceGround = InformationManager::Instance().getEnemyPowerGroundUnits();
	int enemyForceGroundNearby = InformationManager::Instance().getEnemyPowerGroundUnitsIncoming();
	bool emergencyGroundDefense = StrategyBossZerg::Instance().emergencyGroundDefense();

	// Decide if we have enough units to tech up
	bool hasEnoughUnits = nLings + 2*nHydras + 2*nLurkers + nScourge + 2*nMutas + 2*nUltras + 2*nGuardians + 2*nDevourers >= 12;
	bool hasEnoughUnitsMelee = nLings + nUltras >= 6;
	bool hasEnoughUnitsMissile = nHydras + nLurkers >= 6;
	bool hasEnoughUnitsGround = nLings + nHydras + nLurkers + nUltras >= 6;
	bool hasEnoughUnitsAir = nMutas + nScourge + nGuardians + nDevourers >= 6;
	// Decide if we have enough units to drone up
	bool hasEnoughArmy = enoughArmy();

	// Decide gas unit
	chooseGasUnit();
	// Decide mineral unit
	chooseMinUnit();
	// Decide techTarget
	chooseTechTarget();

	double ecoRatio = 0.70;
	if (_enemyRace == BWAPI::Races::Zerg)
	{
		ecoRatio = 0.60;
	}

	// To make drones or not to make drones?
	if (nDrones < droneMinimal && nDrones < droneMax && droneRatio < ecoRatio && !emergencyGroundDefense)
	{ _minUnit = "Drone"; }
	else if (hasEnoughArmy && nDrones < droneMax && droneRatio < ecoRatio && !emergencyGroundDefense)
	{ _minUnit = "Drone"; }
	else if (!_hasIslandBases && (_gasUnit == "None" || nLarvas > 1 || gas < 100))
	{ _minUnit = "Lings";  }
	else if (_hasIslandBases && nLings < 6 && (_gasUnit == "None" || nLarvas > 1 || gas < 100))
	{ _minUnit = "Lings"; }
	// Don't ever make drones during ground emergency
	if (emergencyGroundDefense) { chooseMinUnit(); }


	// Decide unit mix
	bool makeHydras = false;
	bool makeLurkers = false;
	bool makeMutas = false;
	bool makeUltras = false;
	bool makeGuardians = false;
	bool makeDevourers = false;

	// Hydras
	if (_gasUnit == "Hydras") { makeHydras = true; }
	if (_minUnit == "Hydras") { makeHydras = true; }
	// Lurkers
	if (_gasUnit == "Lurkers") { makeLurkers = true; }
	// Mutas
	if (_gasUnit == "Mutas") { makeMutas = true; }
	// Ultras
	if (_gasUnit == "Ultras") { makeUltras = true; }
	// Guardians
	if (_gasUnit == "Guardians") { makeGuardians = true; }
	// Devourers
	if (_gasUnit == "Devourers") { makeDevourers = true; }


	// Start gas if making gas units
	if (_gasUnit != "None" &&
		gas < 100 && !WorkerManager::Instance().isCollectingGas())
	{
		if (nGas > 0 && nDrones > 3 * nGas)
		{
			WorkerManager::Instance().setCollectGas(true);
		}
	}

	int larvasLeft = nLarvas;
	int mineralsLeft = minerals;
	int gasLeft = gas;
	int supplyLeft = nSupplyTotal - nSupplyUsed;

	
	// STRATEGY CODE

	//BWAPI::Broodwar->printf("Get production");
	// UNITS

	// Drones
	if (_minUnit == "Drone" && !emergencyGroundDefense) {
		//for (int i = 0; i < std::max(9, droneMax - nDrones); ++i)
		//{
			if (larvasLeft > 0 && mineralsLeft > 0 && supplyLeft > 1) {
				queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
				--larvasLeft;
				mineralsLeft -= 50;
				supplyLeft -= 2;
			}
		//}
	}

	// Guardians
	if (hasGreaterSpire && makeGuardians && nMutasComp > 3 && nGuardians < 2 * nMutas &&
		mineralsLeft > 0 && gasLeft > 0 && supplyLeft > 3) {
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Guardian));
		--larvasLeft;
		mineralsLeft -= 150;
		gasLeft -= 200;
		supplyLeft -= 4;
	}

	// Devourers
	else if (hasGreaterSpire && makeDevourers && nMutasComp > 3 && 4 * nDevourers < nMutas &&
		mineralsLeft > 0 && gasLeft > 0 && supplyLeft > 3 && (nDevourers + nCocoons) < 9) {
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Devourer));
		--larvasLeft;
		mineralsLeft -= 150;
		gasLeft -= 50;
		supplyLeft -= 4;
	}

	// Mutalisks
	else if (hasSpire && (makeMutas || makeDevourers || makeGuardians) &&
		larvasLeft > 0 && mineralsLeft > 0 && gasLeft > 0 && supplyLeft > 3) {
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Mutalisk));
		--larvasLeft;
		mineralsLeft -= 100;
		gasLeft -= 100;
		supplyLeft -= 4;
	}

	// Ultralisks
	if (hasUltra && makeUltras && larvasLeft > 0 && mineralsLeft > 0 && gasLeft > 0 && supplyLeft > 3) {
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Ultralisk));
		--larvasLeft;
		mineralsLeft -= 200;
		gasLeft -= 200;
		supplyLeft -= 4;
	}

	// Lurkers vs Terran, make more lurkers
	if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Terran &&
		hasDen && hasLurker && makeLurkers && nHydrasComp > 3 && nLurkers < 2 * nHydras &&
		mineralsLeft > 0 && gasLeft > 0 && supplyLeft > 3) {
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Lurker));
		mineralsLeft -= 125;
		gasLeft -= 125;
		supplyLeft -= 4;
	}

	// Lurkers vs Protoss, make fewer lurkers
	else if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Protoss &&
		hasDen && hasLurker && makeLurkers && nHydrasComp > 3 && 3 * nLurkers < nHydras &&
		mineralsLeft > 0 && gasLeft > 0 && supplyLeft > 3) {
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Lurker));
		mineralsLeft -= 125;
		gasLeft -= 125;
		supplyLeft -= 4;
	}

	// Hydralisks
	else if (hasDen && (makeHydras || makeLurkers) && larvasLeft > 0 &&
		mineralsLeft > 0 && gasLeft > 0 && supplyLeft > 1) {
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hydralisk));
		--larvasLeft;
		mineralsLeft -= 75;
		gasLeft -= 25;
		supplyLeft -= 2;
	}

	// Zerglings - only make a small amount on island maps
	if (hasPool && _minUnit == "Lings" && larvasLeft > 0 && mineralsLeft > 0 && supplyLeft > 1) {
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Zergling));
		--larvasLeft;
		mineralsLeft -= 50;
		supplyLeft -= 2;
		//BWAPI::Broodwar->printf("Lings!");
	}

	// Enemy has air. Make scourge if possible (but in ZvZ only after we have some mutas).
	int nScourgeNeeded = std::min(18, InformationManager::Instance().nScourgeNeeded());
	int totalScourge = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Scourge) +
		2 * StrategyBossZerg::Instance().numInEgg(BWAPI::UnitTypes::Zerg_Scourge) +
		2 * queue.numInQueue(BWAPI::UnitTypes::Zerg_Scourge);

	if (hasSpire && InformationManager::Instance().enemyHasAirTech() &&
		(_enemyRace != BWAPI::Races::Zerg || nMutas >= 6))
	{
		if (nScourgeNeeded > totalScourge && larvasLeft > 0 && gasLeft > 75 && supplyLeft > 1)
		{
			// Not too many, and not too many at once. They cost a lot of gas.
			// Allow one if we have 75 gas.
			queue.queueAsLowestPriority(BWAPI::UnitTypes::Zerg_Scourge);
			--larvasLeft;
			gasLeft -= 75;
			mineralsLeft -= 25;
			supplyLeft -= 2;
		}
		// And keep going.
	}
	
	// Extra Drones
	if (_minUnit == "Drone" && !emergencyGroundDefense && 
		larvasLeft > 0 && mineralsLeft > 0 && supplyLeft > 1) {
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
		--larvasLeft;
		mineralsLeft -= 50;
		supplyLeft -= 2;
	}
	
	if (larvasLeft < nLarvas)
	{
		Log().Debug() << "MU: " << _minUnit << " GU: " << _gasUnit
			<< " makeHydras: " << makeHydras << " makeLurkers: " << makeLurkers
			<< " makeMutas: " << makeMutas << " makeUltras: " << makeUltras
			<< " makeGuardians: " << makeGuardians << " makeDevourers:  " << makeDevourers;
	}

	// BUILDINGS

	// Too many buildings in queue, do not make buildings
	if (queue.buildingInQueue()) {
		return;
	}

	// Zerglings
	if (!hasPoolAll &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spawning_Pool))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Spawning_Pool));
		mineralsLeft -= 200;

		// If we're low on drones, replace the drone.
		if (nDrones <= 9) {
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			--larvasLeft;
			mineralsLeft -= 50;
			supplyLeft -= 2;
		}
	}

	// Ground emgergency, do not make buildings and upgrades
	if (emergencyGroundDefense) {
		return;
	}

	// Hydralisks
	if (_techTarget == "Hydras" && hasPool && !hasDenAll && nDrones >= 9 && nGas > 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hydralisk_Den))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hydralisk_Den));
		mineralsLeft -= 100;
		gasLeft -= 50;
	}

	// Lurkers
	else if (_techTarget == "Lurkers" && hasPool && !hasDenAll && nLairsAll > 0 && nDrones >= 9 && nGas > 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hydralisk_Den))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hydralisk_Den));
		mineralsLeft -= 100;
		gasLeft -= 50;
	}

	// Mutalisks + Scourge
	if ((_techTarget == "Mutas" || nScourgeNeeded > 3) && 
		!hasSpireAll && hasLairTech && nDrones >= 9 && nGas > 0 && hasEnoughUnits && gasLeft >= 75 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spire))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Spire));
		mineralsLeft -= 200;
		gasLeft -= 150;
	}

	// Guardians + Devourers
	if ((_techTarget == "Guardians" || _techTarget == "Devourers") &&
		hasSpire && !hasGreaterSpireAll && hasHiveTech && nDrones >= 15 && nGas >= 2 && hasEnoughUnits && gasLeft >= 75 &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Flyer_Carapace) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Flyer_Attacks) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Ventral_Sacs) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Antennae) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Greater_Spire) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Greater_Spire))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Greater_Spire));
		mineralsLeft -= 200;
		gasLeft -= 150;
	}

	// Ultralisks
	if (_techTarget == "Ultras" && !hasUltraAll && hasHiveTech && nDrones >= 24 && nGas >= 3 && hasEnoughUnits &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern));
		mineralsLeft -= 150;
		gasLeft -= 200;
	}

	// Make lair early for mutalisks / lurkers 
	if ((_techTarget == "Mutas" || _techTarget == "Lurkers") && hasPool &&
		(nLairsAll + nHivesAll == 0) && nGas > 0 && nDrones >= 9 && gasLeft >= 75 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Lair) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Lair))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Lair));
		mineralsLeft -= 150;
		gasLeft -= 100;
	}
	// Make lair later for Ultralisks / Guardians / Devourers
	else if ((_techTarget == "Ultras" || _techTarget == "Guardians" || _techTarget == "Devourers") && hasPool &&
		(nLairsAll + nHivesAll == 0) && nDrones >= 12 && nGas >= 2 && gasLeft >= 75 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Lair) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Lair))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Lair));
		mineralsLeft -= 150;
		gasLeft -= 100;
	}
	// Make lair for upgrades.
	else if ((armorUps == 1 || meleeUps == 1 || missileUps == 1 || airarmorUps == 1) &&
		hasPool && (nLairsAll + nHivesAll == 0) && gasLeft >= 75 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Lair) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Lair))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Lair));
		mineralsLeft -= 150;
		gasLeft -= 100;
	}

	// Make Pneumatized_Carapace before moving on to hive tech
	if ((_techTarget == "Ultras" || _techTarget == "Guardians" || _techTarget == "Devourers" ||
		armorUps == 1 || meleeUps == 1 || missileUps == 1 || airarmorUps == 1) &&
		hasLair && nHivesAll == 0 && nDrones >= 15 && nGas >= 1 && hasEnoughUnits &&
		_self->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) == 0 &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Antennae) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Ventral_Sacs) &&
		!queue.anyInQueue(BWAPI::UpgradeTypes::Pneumatized_Carapace))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UpgradeTypes::Pneumatized_Carapace));
		mineralsLeft -= 150;
		gasLeft -= 150;
	}

	// Get overlord sight range if we're accumulating resources.
	if (mineralsLeft > 500 && gasLeft > 500 &&
		hasLair && nHivesAll == 0 && nGas >= 3 && nDrones >= 60 &&
		_self->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) == 1 &&
		_self->getUpgradeLevel(BWAPI::UpgradeTypes::Antennae) == 0 &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Antennae) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Ventral_Sacs) &&
		!queue.anyInQueue(BWAPI::UpgradeTypes::Antennae))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UpgradeTypes::Antennae));
		mineralsLeft -= 150;
		gasLeft -= 150;
	}

	// Make a queen's nest for Ultralisks / Guardians / Devourers
	if ((_techTarget == "Ultras" || _techTarget == "Guardians" || _techTarget == "Devourers") && 
		!hasQueensNestAll && hasLair && nDrones >= 15 && nGas >= 2 && hasEnoughUnits && (_enemyRace != BWAPI::Races::Zerg || nMutas >= 12) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Queens_Nest) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Queens_Nest))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Queens_Nest));
		mineralsLeft -= 150;
		gasLeft -= 100;
	}
	// Make a queen's nest for upgrades.
	else if ((armorUps == 1 || meleeUps == 1 || missileUps == 1 || airarmorUps == 1) &&
		!hasQueensNestAll && hasLair && hasEnoughUnits && (_enemyRace != BWAPI::Races::Zerg || nMutas >= 12) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Queens_Nest) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Queens_Nest))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Queens_Nest));
		mineralsLeft -= 150;
		gasLeft -= 100;
	}

	// Make a hive for Ultralisks / Guardians / Devourers
	if ((_techTarget == "Ultras" || _techTarget == "Guardians" || _techTarget == "Devourers") &&
		hasQueensNest && hasLair && nHivesAll == 0 && nDrones >= 16 && nGas >= 2 && hasEnoughUnits && (_enemyRace != BWAPI::Races::Zerg || nMutas >= 12) &&
		(_self->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) == 1 || _enemyRace == BWAPI::Races::Zerg) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Ventral_Sacs) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Antennae) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hive) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hive))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hive));
		mineralsLeft -= 200;
		gasLeft -= 150;
	}
	// Make a hive for upgrades.
	else if ((armorUps == 2 || meleeUps == 2 || missileUps == 2 || airarmorUps == 2) &&
		hasQueensNest && hasLair && nHivesAll == 0 && hasEnoughUnits && (_enemyRace != BWAPI::Races::Zerg || nMutas >= 12) &&
		(_self->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) == 1 || _enemyRace == BWAPI::Races::Zerg) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Ventral_Sacs) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Antennae) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hive) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hive))
	{
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hive));
		mineralsLeft -= 200;
		gasLeft -= 150;
	}

	// Evo chamber
	if (hasPool && hasEnoughUnitsGround && nGas > 0 && minerals > 50 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Evolution_Chamber) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Evolution_Chamber) && !queue.buildingInQueue())
	{
		BWAPI::Unit ourNatural = nullptr;
		const BWEM::Base * ourNaturalLocation = InformationManager::Instance().getMyNaturalLocation();
		ourNatural = InformationManager::Instance().getBaseDepot(ourNaturalLocation);
		/*
		// BUG: This will wall us self in on maps with small natural :(
		// Natural Evo vs Protoss for simcity
		if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Protoss && 
			minerals > 75 && nEvoAll < 1 && UnitUtil::IsValidUnit(ourNatural))
		{
			const UnitInfo & ourBaseUnitInfo = InformationManager::Instance().getUnit(_self, ourNatural);

			if (ourBaseUnitInfo.completedFrame + 3000 < BWAPI::Broodwar->getFrameCount())
			{
				MacroLocation loc = MacroLocation::Natural;
				queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Evolution_Chamber, loc), false, DefenseLocation::Chokepoint);
			}
		}
		*/
		// Main Evo for upgrades
		if (nEvoAll == 0 && nDrones >= 12 && (_enemyRace != BWAPI::Races::Terran || hasDen || hasSpire || hasUltra) || 
			nEvoAll == 0 && nDrones >= 30 && hasLairTech && _hasIslandBases)
		{
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Evolution_Chamber));
			mineralsLeft -= 75;
		}

		// Main Evo for upgrades
		else if (nEvoAll == 1 && nDrones >= 30 && nGas >= 2 && (hasDen || hasSpire || hasUltra) && !_hasIslandBases &&
			(_self->isUpgrading(armor) || _self->isUpgrading(melee) || _self->isUpgrading(missile)))
		{
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Evolution_Chamber));
			mineralsLeft -= 75;
		}
	}


	// UPGRADES AND TECH

	// Ground emgergency, do not make upgrades
	if (emergencyGroundDefense) {
		return;
	}

	// Decide about lurker aspect tech.
	if (hasDen && hasLair && _techTarget == "Lurkers" && minerals > 50 && nGas > 0 &&
		!queue.upgradeInQueue() && !queue.buildingInQueue())
	{
		if (!_self->hasResearched(BWAPI::TechTypes::Lurker_Aspect) &&
			!_self->isResearching(BWAPI::TechTypes::Lurker_Aspect) &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Muscular_Augments) &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Grooved_Spines) &&
			!queue.anyInQueue(BWAPI::TechTypes::Lurker_Aspect) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Muscular_Augments) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Grooved_Spines))
		{
			queue.queueAsLowestPriority(MacroAct(BWAPI::TechTypes::Lurker_Aspect));
		}
	}

	// Decide about zergling upgrades
	if (hasPool && _minUnit == "Lings" && minerals > 50 && nGas > 0 &&
		!queue.upgradeInQueue() && !queue.buildingInQueue())
	{
		if (nDrones >= 9 && nLings >= 6 &&
			_self->getUpgradeLevel(BWAPI::UpgradeTypes::Metabolic_Boost) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Metabolic_Boost) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Metabolic_Boost))
		{
			queue.queueAsLowestPriority(MacroAct(BWAPI::UpgradeTypes::Metabolic_Boost));
		}
		else if (hasHiveTech && nDrones >= 12 && nLings >= 8 &&
			_self->getUpgradeLevel(BWAPI::UpgradeTypes::Metabolic_Boost) == 1 &&
			_self->getUpgradeLevel(BWAPI::UpgradeTypes::Adrenal_Glands) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Adrenal_Glands) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Adrenal_Glands))
		{
			queue.queueAsLowestPriority(MacroAct(BWAPI::UpgradeTypes::Adrenal_Glands));
		}
	}

	// Decide about hydralisk upgrades.
	else if (hasDen && makeHydras && nHydras >= 3 && nDrones >= 12 && minerals > 50 && nGas > 0 &&
		!queue.upgradeInQueue() && !queue.buildingInQueue())
	{
		if (_self->getUpgradeLevel(BWAPI::UpgradeTypes::Muscular_Augments) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Muscular_Augments) &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Grooved_Spines) &&
			!_self->isResearching(BWAPI::TechTypes::Lurker_Aspect) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Muscular_Augments) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Grooved_Spines) &&
			!queue.anyInQueue(BWAPI::TechTypes::Lurker_Aspect))
		{
			queue.queueAsLowestPriority(MacroAct(BWAPI::UpgradeTypes::Muscular_Augments));
		}
		else if (_self->getUpgradeLevel(BWAPI::UpgradeTypes::Muscular_Augments) == 1 &&
			_self->getUpgradeLevel(BWAPI::UpgradeTypes::Grooved_Spines) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Muscular_Augments) &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Grooved_Spines) &&
			!_self->isResearching(BWAPI::TechTypes::Lurker_Aspect) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Muscular_Augments) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Grooved_Spines) &&
			!queue.anyInQueue(BWAPI::TechTypes::Lurker_Aspect))
		{
			queue.queueAsLowestPriority(MacroAct(BWAPI::UpgradeTypes::Grooved_Spines));
		}
	}

	// Decide about ultralisk upgrades.
	if (hasUltra && makeUltras && nUltras >= 3 && nDrones >= 24 && 
		nGas >= 3 && minerals > 50 && gas > 50 && 
		!queue.upgradeInQueue() && !queue.buildingInQueue())
	{
		if (_self->getUpgradeLevel(BWAPI::UpgradeTypes::Anabolic_Synthesis) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Anabolic_Synthesis) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Anabolic_Synthesis))
		{
			queue.queueAsLowestPriority(MacroAct(BWAPI::UpgradeTypes::Anabolic_Synthesis));
		}
		else if (_self->getUpgradeLevel(BWAPI::UpgradeTypes::Anabolic_Synthesis) != 0 &&
			_self->getUpgradeLevel(BWAPI::UpgradeTypes::Chitinous_Plating) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Chitinous_Plating) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Chitinous_Plating))
		{
			queue.queueAsLowestPriority(MacroAct(BWAPI::UpgradeTypes::Chitinous_Plating));
		}
	}

	// Decide about flyer armor upgrades.
	if (nDrones >= 12 && nGas > 0 && minerals > 50 && gas > 50 &&
		hasPool && hasSpire && (makeMutas || makeGuardians || makeDevourers) && hasEnoughUnitsAir &&
		!_self->isUpgrading(airArmor) && !queue.anyInQueue(airArmor) &&
		!_self->isUpgrading(airAttack) && !queue.anyInQueue(airAttack) &&
		!queue.upgradeInQueue() && !queue.buildingInQueue())
	{
		if (airarmorUps == 0 && !hasGreaterSpireAll ||
			airarmorUps == 1 && hasLairTech && !hasGreaterSpireAll ||
			airarmorUps == 2 && hasHiveTech && !hasGreaterSpireAll) {
			queue.queueAsLowestPriority(MacroAct(airArmor));
		}
	}

	// Decide about ground armor upgrades.
	if (nEvo >= 1 && nDrones >= 12 && nGas > 0 && minerals > 50 && gas > 50 &&
		(hasPool || hasDen || hasUltra) && hasEnoughUnitsGround &&
		!_self->isUpgrading(armor) && !queue.anyInQueue(armor) &&
		!queue.upgradeInQueue() && !queue.buildingInQueue())
	{
		// But delay if we're going mutas or lurkers and don't have many yet. They want the gas.
		if (!(hasSpire && makeMutas && gas < 300 && nMutas < 6) && 
			!(hasDen && makeLurkers && gas < 300 && nLurkers < 6))
		{
			if (armorUps == 0 ||
				armorUps == 1 && hasLairTech ||
				armorUps == 2 && hasHiveTech) 
			{
				queue.queueAsLowestPriority(MacroAct(armor));
			}
		}
	}

	// Decide about ground melee upgrades.
	if ((nEvo >= 2 || nEvo >= 1 && armorUps == 3) && nDrones >= 12 && nGas > 1 && minerals > 50 && gas > 50 &&
		(hasPool || hasUltra) && hasEnoughUnitsMelee &&
		!_self->isUpgrading(melee) && !queue.anyInQueue(melee) &&
		!_self->isUpgrading(missile) && !queue.anyInQueue(missile) &&
		!queue.upgradeInQueue() && !queue.buildingInQueue())
	{
		// But delay if we're going mutas or lurkers and don't have many yet. They want the gas.
		if (!(hasSpire && makeMutas && gas < 300 && nMutas < 6) &&
			!(hasDen && makeLurkers && gas < 300 && nLurkers < 6))
		{
			if (meleeUps == 0 ||
				meleeUps == 1 && hasLairTech ||
				meleeUps == 2 && hasHiveTech)
			{
				queue.queueAsLowestPriority(MacroAct(melee));
			}
		}
	}

	// Decide about ground missile upgrades.
	if ((nEvo >= 2 || nEvo >= 1 && armorUps == 3) && nDrones >= 12 && nGas > 1 && minerals > 50 && gas > 50 &&
		hasDen && hasEnoughUnitsMissile &&
		!_self->isUpgrading(missile) && !queue.anyInQueue(missile) &&
		!_self->isUpgrading(melee) && !queue.anyInQueue(melee) &&
		!queue.upgradeInQueue() && !queue.buildingInQueue())
	{
		// But delay if we're going mutas or lurkers and don't have many yet. They want the gas.
		if (!(hasSpire && makeMutas && gas < 300 && nMutas < 6) &&
			!(hasDen && makeLurkers && gas < 300 && nLurkers < 6))
		{
			if (missileUps == 0 ||
				missileUps == 1 && hasLairTech ||
				missileUps == 2 && hasHiveTech)
			{
				queue.queueAsLowestPriority(MacroAct(missile));
			}
		}
	}


}

void StrategyManager::readResults()
{
    if (!Config::Modules::UsingStrategyIO)
    {
        return;
    }

    std::string enemyName = BWAPI::Broodwar->enemy()->getName();
    std::replace(enemyName.begin(), enemyName.end(), ' ', '_');

	std::string enemyResultsFileTraining = Config::Strategy::ReadDirTraining + "result_" + enemyName + ".txt";
    std::string enemyResultsFile = Config::Strategy::ReadDir + "result_" + enemyName + ".txt";
   
	FILE *file1 = fopen(enemyResultsFileTraining.c_str(), "r");
	FILE *file2 = fopen(enemyResultsFile.c_str(), "r");
	FILE *file = file2;

	if (file1 != nullptr && file2 == nullptr) {
		file = file1;
	}

	std::string strategyName;
	int wins = 0;
	int losses = 0;

    if (file != nullptr)
    {
        char line[ 4096 ]; /* or other suitable maximum line size */
        while ( fgets ( line, sizeof line, file ) != nullptr ) /* read a line */
        {
            std::stringstream ss(line);

            ss >> strategyName;
            ss >> wins;
            ss >> losses;

            //BWAPI::Broodwar->printf("Results Found: %s %d %d", strategyName.c_str(), wins, losses);

            if (_strategies.find(strategyName) == _strategies.end())
            {
                //BWAPI::Broodwar->printf("Warning: Results file has unknown Strategy: %s", strategyName.c_str());
            }
            else
            {
                _strategies[strategyName]._wins = wins;
                _strategies[strategyName]._losses = losses;
            }
        }

        fclose ( file );
    }
    else
    {
        //BWAPI::Broodwar->printf("No results file found: %s", enemyResultsFile.c_str());
    }
}

void StrategyManager::writeResults()
{
    if (!Config::Modules::UsingStrategyIO)
    {
        return;
    }

	//Setting up output file
    std::string enemyName = BWAPI::Broodwar->enemy()->getName();
    std::replace(enemyName.begin(), enemyName.end(), ' ', '_');
	
	std::string enemyResultsFile = Config::Strategy::WriteDir + "result_" + enemyName + ".txt";
	
	if (!Config::Strategy::UseHumanSpecificStrategy)
		BWAPI::Broodwar->sendText("Adapting result %s", enemyResultsFile.c_str());

    std::stringstream ss;

	//Loop strategies and write to stream
    for (auto & kv : _strategies)
    {
        const Strategy & strategy = kv.second;

		int wins = strategy._wins;
		int losses = strategy._losses;
		//double winrate = strategy._winRate;
/*
		// Normalize wins and losses to maximum 10
		if (strategy._wins > 10 || strategy._losses > 10)
		{
			wins = (int)(10 * strategy._wins / std::max(strategy._wins, strategy._losses));
			losses = (int)(10 * strategy._losses / std::max(strategy._wins, strategy._losses));
		}
*/
		// Write strategy if it has been played
		if (wins + losses > 0) 
		{
			ss << strategy._name << " " 
			   << wins << " " 
			   << losses  << "\n";
			   //<< winrate << "\n"; 
		}
    }

	//Write output file
    Logger::LogOverwriteToFile(enemyResultsFile, ss.str());
}

void StrategyManager::readHistory()
{
	if (!Config::Modules::UsingStrategyIO)
	{
		return;
	}

	std::string enemyName = BWAPI::Broodwar->enemy()->getName();
	std::replace(enemyName.begin(), enemyName.end(), ' ', '_');

	std::string enemyHistoryFileTraining = Config::Strategy::ReadDirTraining + "history_" + enemyName + ".txt";
	std::string enemyHistoryFile = Config::Strategy::ReadDir + "history_" + enemyName + ".txt";

	FILE *file1 = fopen(enemyHistoryFileTraining.c_str(), "r");
	FILE *file2 = fopen(enemyHistoryFile.c_str(), "r");
	FILE *file = file2;

	if (file1 != nullptr && file2 == nullptr) {
		file = file1;
	}

	HistoryRecord record;
	std::string line_string;
	std::string myRace;
	std::string enemyRace;
	std::string recognizedEnemyPlan;
	std::string expectedEnemyPlan;

	if (file != nullptr)
	{
		//while (std::getline(stream, line))
		char line_char[4096]; /* or other suitable maximum line size */
		while (fgets(line_char, sizeof line_char, file) != nullptr) /* read a line */
		{
			//convert char to string
			std::stringstream line_ss(line_char);
			line_ss << line_char;
			line_ss >> line_string;

			// Read history lines
			std::replace(line_string.begin(), line_string.end(), ' ', '_');
			std::replace(line_string.begin(), line_string.end(), ';', ' ');
			std::stringstream ss(line_string);

			ss >> record.fileFormatVersion
				>> record.time
				>> record.mapStartLocations
				>> record.mapName
				>> record.myName
				>> myRace
				>> record.enemyName
				>> enemyRace
				>> record.enemyIsRandom
				>> record.isWinner
				>> record.myStrategyName
				>> expectedEnemyPlan
				>> recognizedEnemyPlan;
				//>> record.frameGameEnds;

			record.myRace = ReadRace(myRace);
			record.enemyRace = ReadRace(enemyRace);
			record.recognizedEnemyPlan = OpeningPlanFromString(recognizedEnemyPlan);
			record.expectedEnemyPlan = OpeningPlanFromString(expectedEnemyPlan);

			_historyRecords.push_back(record);
		}
	}
}

void StrategyManager::writeHistory()
{
	if (!Config::Modules::UsingStrategyIO)
	{
		return;
	}

	//Create current record
	std::string mapName = BWAPI::Broodwar->mapFileName();
	mapName.erase(std::remove(mapName.begin(), mapName.end(), ' '), mapName.end());
	mapName.erase(std::remove(mapName.begin(), mapName.end(), '_'), mapName.end());
	_currentRecord.mapName = mapName;

	std::string myName = BWAPI::Broodwar->self()->getName();
	std::replace(myName.begin(), myName.end(), ' ', '_');
	_currentRecord.myName = myName;

	std::string enemyName = BWAPI::Broodwar->enemy()->getName();
	std::replace(enemyName.begin(), enemyName.end(), ' ', '_');
	_currentRecord.enemyName = enemyName;

	std::string strategyName = Config::Strategy::StrategyName;
	std::replace(strategyName.begin(), strategyName.end(), ' ', '_');
	_currentRecord.myStrategyName = strategyName;

	_currentRecord.myRace = _selfRace;
	_currentRecord.enemyRace = _enemyRace;
	_currentRecord.enemyIsRandom = _enemyIsRandom;
	_currentRecord.isWinner = _isWinner;

	_currentRecord.recognizedEnemyPlan = getRecognizedEnemyPlan();
	_currentRecord.expectedEnemyPlan = getExpectedEnemyPlan();

	_currentRecord.frameGameEnds = BWAPI::Broodwar->getFrameCount();

	//Add the current record
	_historyRecords.push_back(_currentRecord);

	//Setting up output file
	std::string enemyHistoryFile = Config::Strategy::WriteDir + "history_" + enemyName + ".txt";
	
	if (!Config::Strategy::UseHumanSpecificStrategy)
		BWAPI::Broodwar->sendText("Adapting history %s", enemyHistoryFile.c_str());

	std::stringstream ss;
	std::string del = ";";
	std::string eol = "\n";

	int history_size = _historyRecords.size();
	int max_size = Config::Strategy::MaxGameRecords;
	int nToSkip = 0;

	if (history_size >= max_size)
	{
		nToSkip = history_size - max_size;
	}

	//Loop history records and write to stream
	for (auto & record : _historyRecords)
	{
		if (nToSkip > 0)
		{
			--nToSkip;
			continue;
		}

		ss << record.fileFormatVersion << del
			<< record.time << del
			<< record.mapStartLocations << del
			<< record.mapName << del
			<< record.myName << del
			<< WriteRace(record.myRace) << del
			<< record.enemyName << del
			<< WriteRace(record.enemyRace) << del
			<< record.enemyIsRandom << del
			<< record.isWinner << del;

		if (record.fileFormatVersion == "v2.1")
		{
			ss << record.myStrategyName << eol;
		}

		if (record.fileFormatVersion == "v2.7")
		{
			ss << record.myStrategyName << del
			   << OpeningPlanString(record.expectedEnemyPlan) << del
			   << OpeningPlanString(record.recognizedEnemyPlan) << eol;
		}

			//<< record.frameGameEnds << eol;
	}

	//Write stream to file
	Logger::LogOverwriteToFile(enemyHistoryFile, ss.str());
}

void StrategyManager::readResultsFromHistory()
{
	if (!Config::Modules::UsingStrategyIO)
	{
		return;
	}

	int history_size = _historyRecords.size();
	int max_size = Config::Strategy::MaxGameRecords;
	int nToSkip = 0;

	if (history_size >= max_size)
	{
		nToSkip = history_size - max_size;
	}

	for (auto & record : _historyRecords)
	{
		if (nToSkip > 0)
		{
			--nToSkip;
			continue;
		}

		_strategies[record.myStrategyName]._games += 1;
		if (record.isWinner)
		{
			_strategies[record.myStrategyName]._wins += 1;
		}
		else
		{
			_strategies[record.myStrategyName]._losses += 1;
		}
		_strategies[record.myStrategyName]._winRate = 
			(double)_strategies[record.myStrategyName]._wins / (double)_strategies[record.myStrategyName]._games;
	}
}

void StrategyManager::writeResultsFromHistory()
{
	if (!Config::Modules::UsingStrategyIO)
	{
		return;
	}

	//Setting up output file
	int history_size = _historyRecords.size();
	int max_size = Config::Strategy::MaxGameRecords;
	int nToSkip = 0;

	if (history_size >= max_size)
	{
		nToSkip = history_size - max_size;
	}

	//Reset strategies
	resetStrategies();

	//Loop history records and update strategy wins and losses
	for (auto & record : _historyRecords)
	{
		if (nToSkip > 0)
		{
			--nToSkip;
			continue;
		}

		_strategies[record.myStrategyName]._games += 1;
		if (record.isWinner)
		{
			_strategies[record.myStrategyName]._wins += 1;
		}
		else
		{
			_strategies[record.myStrategyName]._losses += 1;
		}
		_strategies[record.myStrategyName]._winRate =
			(double)_strategies[record.myStrategyName]._wins / (double)_strategies[record.myStrategyName]._games;
	}

	//Write output file
	writeResults();
}

void StrategyManager::resetStrategies()
{
	for (auto & kv : _strategies)
	{
		const Strategy & strategy = kv.second;
		_strategies[strategy._name]._games = 0;
		_strategies[strategy._name]._wins = 0;
		_strategies[strategy._name]._losses = 0;
		_strategies[strategy._name]._winRate = 0.0;
		_strategies[strategy._name]._sameGames = 0;
		_strategies[strategy._name]._sameWins = 0;
		_strategies[strategy._name]._otherGames = 0;
		_strategies[strategy._name]._otherWins = 0;
		_strategies[strategy._name]._weightedGames = 0.0;
		_strategies[strategy._name]._weightedWins = 0.0;
		_strategies[strategy._name]._weightedWinrate = 0.0;
	}
}

BWAPI::Race StrategyManager::ReadRace(std::string str)
{
	if (str == "Zerg")
	{
		return BWAPI::Races::Zerg;
	}
	if (str == "Protoss")
	{
		return BWAPI::Races::Protoss;
	}
	if (str == "Terran")
	{
		return BWAPI::Races::Terran;
	}
	return BWAPI::Races::Unknown;
}

std::string StrategyManager::WriteRace(BWAPI::Race race)
{
	if (race == BWAPI::Races::Zerg)
	{
		return "Zerg";
	}
	if (race == BWAPI::Races::Protoss)
	{
		return "Protoss";
	}
	if (race == BWAPI::Races::Terran)
	{
		return "Terran";
	}
	return "Unknown";
}

void StrategyManager::onEnd(const bool isWinner)
{
	if (gameEnded) return;

    if (!Config::Modules::UsingStrategyIO)
    {
        return;
    }
	
    if (isWinner)
    {
		_isWinner = true;
    }
    else
    {
		_isWinner = false;
    }
	
	writeHistory();
	writeResultsFromHistory();
}

void StrategyManager::setBestRandomStrategy()
{
	if (Config::Strategy::PlayGoodStrategiesFirst == true) {

		// 1.1 Collect weights and strategies with at least 1 game played and winrate > 0.75
		std::vector<std::string> strategies1;    // strategy name
		std::vector<int> weights1;               // cumulative weight of strategy
		int totalWeight1 = 0;                    // cumulative weight of last strategy (so far)

		for (auto & kv : _strategiesMatchup)
		{
			std::string name1 = kv.first;
			int weight1 = kv.second;

			if (weight1 > 0)
			{
				int strategyGamesPlayed1 = _strategies[name1]._losses + _strategies[name1]._wins;
				int strategyWins = _strategies[name1]._wins;
				double strategyWinRate = (double)strategyWins / (double)strategyGamesPlayed1;
				if (strategyGamesPlayed1 > 0 && strategyWinRate >= 0.75)
				{
					strategies1.push_back(name1);
					totalWeight1 += weight1;
					weights1.push_back(totalWeight1);
				}
			}
		}
	
	
		// 1.2 Do we have a strategy with at least 1 game played and winrate > 0.75?
		int w1 = Random::Instance().index(totalWeight1);
		//int rnd = Random::Instance().index(6);
		//BWAPI::Broodwar->printf("Using random good strategy: %d", w1);

		for (size_t i = 0; i < weights1.size(); ++i)
		{
			int strategyGamesPlayed1 = _strategies[strategies1[i]]._losses + _strategies[strategies1[i]]._wins;
			if (strategyGamesPlayed1 > 0)
			{
				int strategyWins = _strategies[strategies1[i]]._wins;
				double strategyWinRate = (double)strategyWins / (double)strategyGamesPlayed1;
				//BWAPI::Broodwar->printf("Using random good strategy: games = %d  winrate = %.3lf", strategyGamesPlayed1, strategyWinRate);
				if (strategyWinRate >= 0.90)
				{
					Config::Strategy::StrategyName = strategies1[i];
					//BWAPI::Broodwar->printf("Using good strategy: rnd = %d  w = %d", rnd, w1);
					//BWAPI::Broodwar->printf("Using good strategy: games = %d  winrate = %.3lf", strategyGamesPlayed1, strategyWinRate);
					return;
				}
				if (w1 < weights1[i] && strategyWinRate >= 0.75)
				{
					Config::Strategy::StrategyName = strategies1[i];
					//BWAPI::Broodwar->printf("Using good strategy: rnd = %d  w = %d", rnd, w1);
					//BWAPI::Broodwar->printf("Using good strategy: games = %d  winrate = %.3lf", strategyGamesPlayed1, strategyWinRate);
					return;
				}
			}
		}

	}
	

	// 2.1 Collect weights and strategies with less than minGamesToPlay games played
	std::vector<std::string> strategies2;    // strategy name
	std::vector<int> weights2;               // cumulative weight of strategy
	int totalWeight2 = 0;                    // cumulative weight of last strategy (so far)
	int minGamesToPlay = 2;					 // Minimum games to play

	if (Config::Strategy::PlayGoodStrategiesFirst != true)
	{
		minGamesToPlay = 5;
	}

	for (auto & kv : _strategiesMatchup)
	{
		std::string name2 = kv.first;
		int weight2 = kv.second;

		if (weight2 > 0)
		{
			int strategyGamesPlayed2 = _strategies[name2]._losses + _strategies[name2]._wins;
			if (strategyGamesPlayed2 < minGamesToPlay)
			{
				strategies2.push_back(name2);
				totalWeight2 += weight2;
				weights2.push_back(totalWeight2);
			}
		}
	}

	// 2.2 Choose a strategy at random by weight with less than minGamesToPlay games played
	int w2 = Random::Instance().index(totalWeight2);
	//BWAPI::Broodwar->printf("Using random new strategy: %d", w2);

	for (size_t i = 0; i < weights2.size(); ++i)
	{
		int strategyGamesPlayed2 = _strategies[strategies2[i]]._losses + _strategies[strategies2[i]]._wins;
		//BWAPI::Broodwar->printf("Using random new strategy: games = %d", strategyGamesPlayed2);
		if (w2 < weights2[i] && strategyGamesPlayed2 < minGamesToPlay)
		{
			Config::Strategy::StrategyName = strategies2[i];
			//BWAPI::Broodwar->printf("Using random strategy: rnd = %d  w = %d", rnd, w2);
			//BWAPI::Broodwar->printf("Using random strategy: games = %d", strategyGamesPlayed2);
			return;
		}
	}

	// 3. Choose default strategy
	return;
}

void StrategyManager::setExplorerStrategy()
{
	// 1. Collect weights and strategies with less than minGamesToPlay games played
	std::vector<std::string> strategies2;    // strategy name
	std::vector<int> weights2;               // cumulative weight of strategy
	int totalWeight2 = 0;                    // cumulative weight of last strategy (so far)
	int minGamesToPlay = 2;					 // Minimum games to play

	if (Config::Strategy::PlayGoodStrategiesFirst != true)
	{
		minGamesToPlay = 5;
	}

	for (auto & kv : _strategiesMatchup)
	{
		std::string name2 = kv.first;
		int weight2 = kv.second;

		if (weight2 > 0)
		{
			int strategyGamesPlayed2 = _strategies[name2]._losses + _strategies[name2]._wins;
			if (strategyGamesPlayed2 < minGamesToPlay)
			{
				strategies2.push_back(name2);
				totalWeight2 += weight2;
				weights2.push_back(totalWeight2);

				Log().Get() << "Exploration Strategy: " << name2 << "  games: " << strategyGamesPlayed2 << "  weight: " << weight2;
			}
		}
	}

	// 2. Choose a strategy at random by weight with less than minGamesToPlay games played
	int w2 = Random::Instance().index(totalWeight2);
	//BWAPI::Broodwar->printf("Using random new strategy: %d", w2);

	for (size_t i = 0; i < weights2.size(); ++i)
	{
		int strategyGamesPlayed2 = _strategies[strategies2[i]]._losses + _strategies[strategies2[i]]._wins;
		//BWAPI::Broodwar->printf("Using random new strategy: games = %d", strategyGamesPlayed2);
		if (w2 < weights2[i] && strategyGamesPlayed2 < minGamesToPlay)
		{
			Config::Strategy::StrategyName = strategies2[i];
			//BWAPI::Broodwar->printf("Using random strategy: rnd = %d  w = %d", rnd, w2);
			//BWAPI::Broodwar->printf("Using random strategy: games = %d", strategyGamesPlayed2);
			Log().Get() << "Exploration Strategy: " << Config::Strategy::StrategyName
				<< "  games: " << _strategies[Config::Strategy::StrategyName]._games
				<< "  winrate: " << _strategies[Config::Strategy::StrategyName]._winRate
				<< "  w: " << w2;
			return;
		}
	}

	// 3. Choose default strategy
	return;
}

void StrategyManager::setRandomStrategy()
{
	// 1. Collect weights and strategies
	std::vector<std::string> strategies;    // strategy name
	std::vector<int> weights;               // cumulative weight of strategy
	int totalWeight = 0;                    // cumulative weight of last strategy (so far)

	for (auto & kv : _strategiesMatchup)
	{
		std::string name = kv.first;
		int weight = kv.second;

		if (weight > 0)
		{
			strategies.push_back(name);
			totalWeight += weight;
			weights.push_back(totalWeight);
		}
	}

	// 2. Choose a strategy at random
	int w = Random::Instance().index(totalWeight);
	//BWAPI::Broodwar->printf("Using random new strategy: %d", w2);

	for (size_t i = 0; i < weights.size(); ++i)
	{
		//BWAPI::Broodwar->printf("Using random new strategy: games = %d", strategyGamesPlayed2);
		if (w < weights[i])
		{
			Config::Strategy::StrategyName = strategies[i];
			//BWAPI::Broodwar->printf("Using random strategy: rnd = %d  w = %d", rnd, w2);
			//BWAPI::Broodwar->printf("Using random strategy: games = %d", strategyGamesPlayed2);
			return;
		}
	}

	// 3. Choose default strategy
	return;
}

void StrategyManager::setLearnedStrategy()
{
    // we are currently not using this functionality for the competition so turn it off 
    //return;

    if (!Config::Modules::UsingStrategyLearning)
    {
        return;
    }

	// if we are using an enemy specific strategy
	//if (Config::Strategy::FoundEnemySpecificStrategy)
	//{
	//	return;
	//}

	int totalGamesPlayed = 0;

    // get the total number of games played so far with this race
    for (auto & kv : _strategies)
    {
        Strategy & strategy = kv.second;
		if (strategy._race == _selfRace)
        {
            totalGamesPlayed += strategy._wins + strategy._losses;
        }
    }


	// best random strategy
	setBestRandomStrategy();


	// calculate win rate for choosen strategy
	std::string & strategyName = Config::Strategy::StrategyName;
	Strategy & currentStrategy = _strategies[strategyName];
	int strategyGamesPlayed = currentStrategy._wins + currentStrategy._losses;
	double strategyWinRate = strategyGamesPlayed > 0 ? currentStrategy._wins / static_cast<double>(strategyGamesPlayed) : 0;
	
	int minGamesToPlay = 2; // Minimum games to play
	if (Config::Strategy::PlayGoodStrategiesFirst != true)
	{
		minGamesToPlay = 5;
	}

	// if our win rate with the current strategy is high don't change
	// also don't change if insufficient games have been played
	if (strategyGamesPlayed > 0 && strategyWinRate >= 0.75)
	{
		if (!Config::Strategy::UseHumanSpecificStrategy)
			BWAPI::Broodwar->sendText("Strategy: games = %d  winrate = %.3lf", strategyGamesPlayed, strategyWinRate);

		Log().Get() << "High Winrate: games = " << strategyGamesPlayed << " winrate = " << strategyWinRate;
		Log().Get() << "Strategy: " << Config::Strategy::StrategyName; 
		return;
	}
	if (strategyGamesPlayed < minGamesToPlay)
	{
		if (!Config::Strategy::UseHumanSpecificStrategy)
			BWAPI::Broodwar->sendText("Strategy: games = %d  winrate = %.3lf", strategyGamesPlayed, strategyWinRate);

		Log().Get() << "Exploration Strategy: games = " << strategyGamesPlayed << " winrate = " << strategyWinRate;
		Log().Get() << "Strategy: " << Config::Strategy::StrategyName; 
		return;
	}


	// use random strategy if we are 0-10 in all strategies
	int numStrategies = 0;
	int numWins = 0;
	int numLosses = 0;
	for (auto & kv : _strategies)
	{	
		// Do not consider strategies for another race
		Strategy & strategy = kv.second;
		if (strategy._race != _selfRace)
		{
			continue;
		}

		// Do not consider strategies that we have not played
		int sGamesPlayed = strategy._wins + strategy._losses;
		if (sGamesPlayed == 0)
		{
			continue;
		}

		numStrategies++;
		numWins += strategy._wins;
		numLosses += strategy._losses;
	}
	if (numWins == 0 && numLosses == numStrategies * 10)
	{
		if (!Config::Strategy::UseHumanSpecificStrategy)
			BWAPI::Broodwar->sendText("Strategy: games = %d  winrate = %.3lf", strategyGamesPlayed, strategyWinRate);

		Log().Get() << "Exploration Strategy: games = " << strategyGamesPlayed << " winrate = " << strategyWinRate;
		Log().Get() << "Strategy: " << Config::Strategy::StrategyName;
		return;
	}


    // calculate the UCB value and store the highest
    double C = 0.5;
    std::string bestUCBStrategy;
    double bestUCBStrategyVal = std::numeric_limits<double>::lowest();
	double bestUCBWinRate = std::numeric_limits<double>::lowest();
	int bestUCBStrategyGames = 0;
    for (auto & kv : _strategies)
    {
		// Do not consider strategies for another race
		Strategy & strategy = kv.second;
		if (strategy._race != _selfRace)
        {
            continue;
        }

		// Do not consider strategies that we have not played
		int sGamesPlayed = strategy._wins + strategy._losses;
		if (sGamesPlayed == 0)
		{
			continue;
		}

		double sWinRate = sGamesPlayed > 0 ? strategy._wins / static_cast<double>(sGamesPlayed) : 0;
        double ucbVal = C * sqrt( log( (double)totalGamesPlayed / sGamesPlayed ) );
        double val = sWinRate + ucbVal;

        if (val > bestUCBStrategyVal)
        {
            bestUCBStrategy = strategy._name;
            bestUCBStrategyVal = val;
			bestUCBStrategyGames = sGamesPlayed;
			bestUCBWinRate = sWinRate;
        }
    }

	if(!Config::Strategy::UseHumanSpecificStrategy)
		BWAPI::Broodwar->sendText("Strategy: games = %d  winrate = %.3lf  ucb1 = %.3lf", bestUCBStrategyGames, bestUCBWinRate, bestUCBStrategyVal);
	
	Log().Get() << "Adapting strategy: games = " << bestUCBStrategyGames << " winrate = " << bestUCBWinRate;
	Log().Get() << "Strategy: " << bestUCBStrategy;
	Config::Strategy::StrategyName = bestUCBStrategy;
}

void StrategyManager::setLearnedStrategyFromHistory()
{
	if (!Config::Modules::UsingStrategyLearning)
	{
		return;
	}

	 
	struct OpeningInfoType
	{
		int sameWins;		// on the same map as this game, or following the same plan as this game
		int sameGames;
		int otherWins;		// across all other maps/plans
		int otherGames;
		double winRate;
		double sameWinrate;
		double weightedWins;
		double weightedGames;

		OpeningInfoType()
			: sameWins(0)
			, sameGames(0)
			, otherWins(0)
			, otherGames(0)
			// The weighted values doesn't need to be initialized up front.
		{
		}
	};

	int totalWins = 0;
	int totalGames = 0;
	std::map<std::string, OpeningInfoType> openingInfo;		// opening name -> opening info
	OpeningInfoType planInfo;								// summary of the recorded enemy plans

	std::string mapName = BWAPI::Broodwar->mapFileName();
	mapName.erase(std::remove(mapName.begin(), mapName.end(), ' '), mapName.end());
	mapName.erase(std::remove(mapName.begin(), mapName.end(), '_'), mapName.end());


	for (auto & record : _historyRecords)
	{
		if (sameMatchup(record))
		{
			// Gather basic information from history records
			totalGames += 1;
			if (record.isWinner == true)
			{
				totalWins += 1;
			}

			OpeningInfoType & info = openingInfo[record.myStrategyName];
			if (mapName == record.mapName)
			{
				info.sameGames += 1;
				if (record.isWinner == true)
				{
					info.sameWins += 1;
				}
			}
			else
			{
				info.otherGames += 1;
				if (record.isWinner == true)
				{
					info.otherWins += 1;
				}
			}

			if (record.expectedEnemyPlan == record.recognizedEnemyPlan)
			{
				// The plan was recorded as correctly predicted in that game.
				planInfo.sameGames += 1;
				if (record.isWinner == true)
				{
					planInfo.sameWins += 1;
				}
			}
			else
			{
				// The plan was not correctly predicted.
				planInfo.otherGames += 1;
				if (record.isWinner == true)
				{
					planInfo.otherWins += 1;
				}
			}
		}
	}


	if (Config::Strategy::PlayGoodStrategiesFirst == true)
	{
		// Randomly choose any opening that always wins, or always wins on this map, or has a high win rate
		// This bypasses the map weighting.
		// The algorithm is reservoir sampling with reservoir size = 1.
		// It gives equal probabilities without remembering all the elements.

		std::string alwaysWins;
		double nAlwaysWins = 0.0;
		double winrateAlwaysWins = 0.0;
		int gamesAlwaysWins = 0;

		std::string alwaysWinsMap;
		double nAlwaysWinsOnThisMap = 0.0;
		double winrateAlwaysWinsMap = 0.0;
		int gamesAlwaysWinsMap = 0;

		std::string highWinrate;
		double nHighWinrate = 0.0;
		double winrateHighWinrate = 0.0;
		int gamesHighWinrate = 0;

		for (auto & item : openingInfo)
		{
			const OpeningInfoType & info = item.second;
			const std::string strategyName = item.first;

			const int games = info.sameGames + info.otherGames;
			const double winRate = (info.sameGames + info.otherGames) ?
				(double)(info.sameWins + info.otherWins) / (double)(info.sameGames + info.otherGames) : 0.0;
			const double sameWinRate = info.sameGames ? (double)info.sameWins / (double)info.sameGames : 0.0;

			// DEBUG
			Log().Get() << "Winrate: "
				<< strategyName << " "
				<< info.sameWins << " "
				<< info.otherWins << " "
				<< info.sameGames << " "
				<< info.otherGames << " "
				<< winRate << " "
				<< sameWinRate;

			if (winRate == 1.0)
			{
				nAlwaysWins += 1.0;
				if (Random::Instance().flag(1.0 / nAlwaysWins))
				{
					alwaysWins = strategyName;
					winrateAlwaysWins = winRate;
					gamesAlwaysWins = games;
				}
			}
			if (winRate >= 0.75)
			{
				nHighWinrate += 1.0;
				if (Random::Instance().flag(1.0 / nHighWinrate))
				{
					highWinrate = strategyName;
					winrateHighWinrate = winRate;
					gamesHighWinrate = games;
				}
			}
			if (sameWinRate == 1.0)
			{
				nAlwaysWinsOnThisMap += 1.0;
				if (Random::Instance().flag(1.0 / nAlwaysWinsOnThisMap))
				{
					alwaysWinsMap = strategyName;
					winrateAlwaysWinsMap = winRate;
					gamesAlwaysWinsMap = games;
				}
			}
		}
		if (!alwaysWins.empty())
		{
			Log().Get() << "Always Wins: games = " << gamesAlwaysWins << "  winrate = " << winrateAlwaysWins;
			Config::Strategy::StrategyName = alwaysWins;
			return;
		}
		if (!highWinrate.empty())
		{
			Log().Get() << "High Winrate: games = " << gamesHighWinrate << "  winrate = " << winrateHighWinrate;
			Config::Strategy::StrategyName = highWinrate;
			return;
		}
		/*if (!alwaysWinsMap.empty())
		{
			Log().Get() << "Always Wins Map: games = " << gamesAlwaysWinsMap << "  winrate = " << winrateAlwaysWinsMap;
			_recommendedOpening = alwaysWinsMap;
			return;
		}*/
	}

	
	setExplorerStrategy();


	// calculate win rate for choosen strategy
	std::string & strategyName = Config::Strategy::StrategyName;
	Strategy & currentStrategy = _strategies[strategyName];
	int strategyGamesPlayed = currentStrategy._wins + currentStrategy._losses;
	double strategyWinRate = strategyGamesPlayed > 0 ? currentStrategy._wins / static_cast<double>(strategyGamesPlayed) : 0;

	int minGamesToPlay = 2; // Minimum games to play
	if (Config::Strategy::PlayGoodStrategiesFirst != true)
	{
		minGamesToPlay = 5;
	}

	// don't change if insufficient games have been played
	if (strategyGamesPlayed < minGamesToPlay)
	{
		Log().Get() << "Exploration Strategy: games = " << strategyGamesPlayed << " winrate = " << strategyWinRate;
		return;
	}


	// Compute "weighted" win rates which combine map win rates and overall win rates, as an
	// estimate of the true win rate on this map. The estimate is ad hoc, using an assumption
	// that is sure to be wrong.
	for (auto it = openingInfo.begin(); it != openingInfo.end(); ++it)
	{
		OpeningInfoType & info = it->second;

		// Evidence provided by game results is proportional to the square root of the number of games.
		// So let's pretend that a game played on the same map provides mapPower times as much evidence
		// as a game played on another map.
		double mapPower = info.sameGames ? (info.sameGames + info.otherGames) / sqrt(info.sameGames) : 0.0;

		info.weightedWins = mapPower * info.sameWins + info.otherWins;
		info.weightedGames = mapPower * info.sameGames + info.otherGames;
	}


	// We're not exploring. Choose an opening with the best weighted win rate.
	double bestScore = -1.0;		// every opening will have a win rate >= 0
	double nBest = 1.0;
	std::string bestStrategy;
	int bestGames = 0;
	double bestWinrate = 0.0;
	for (auto it = openingInfo.begin(); it != openingInfo.end(); ++it)
	{
		const OpeningInfoType & info = it->second;
		const std::string strategyName = it->first;

		const double score = info.weightedGames < 0.1 ? 0.0 : info.weightedWins / info.weightedGames;
		const int games = info.sameGames + info.otherGames;

		// DEBUG
		Log().Get() << "Weighted Winrate: "
			<< strategyName << " "
			<< info.weightedWins << " "
			<< info.weightedGames << " "
			<< score;

		if (score > bestScore)
		{
			bestStrategy = strategyName;
			bestGames = games;
			bestWinrate = score;
			bestScore = score;
			nBest = 1.0;
		}
		else if (bestScore - score < 0.001)	// we know score <= bestScore
		{
			// We choose randomly among openings with essentially equal score, using reservoir sampling.
			nBest += 1.0;
			if (Random::Instance().flag(1.0 / nBest))
			{
				bestStrategy = strategyName;
				bestGames = games;
				bestWinrate = score;
			}
		}
	}

	Log().Get() << "Weighted Winrate: games = " << bestGames << "  weighted winrate = " << bestWinrate;
	Config::Strategy::StrategyName = bestStrategy;
	return;

}

void StrategyManager::setLearnedStrategyFromEnemyPlan()
{
	if (!Config::Modules::UsingStrategyLearning)
	{
		return;
	}


	struct OpeningInfoType
	{
		int sameWins;		// on the same map as this game, or following the same plan as this game
		int sameGames;
		int otherWins;		// across all other maps/plans
		int otherGames;
		int wins;
		int games;
		double winRate;
		double sameWinrate;
		double weightedWins;
		double weightedGames;
		double ageFactor;

		OpeningInfoType()
			: sameWins(0)
			, sameGames(0)
			, otherWins(0)
			, otherGames(0)
			, wins(0)
			, games(0)
			// The weighted values don't need to be initialized up front.
		{
		}
	};


	// Gather information from history records
	int totalWins = 0;
	int totalGames = 0;
	std::map<std::string, OpeningInfoType> openingInfo;		// opening name -> opening info
	OpeningInfoType planInfo;								// summary of the recorded enemy plans

	std::string mapName = BWAPI::Broodwar->mapFileName();
	mapName.erase(std::remove(mapName.begin(), mapName.end(), ' '), mapName.end());
	mapName.erase(std::remove(mapName.begin(), mapName.end(), '_'), mapName.end());


	for (auto & record : _historyRecords)
	{
		if (sameMatchup(record))
		{
			// Basic information
			totalGames += 1;
			if (record.isWinner == true)
			{
				totalWins += 1;
			}

			OpeningInfoType & info = openingInfo[record.myStrategyName];
			if (mapName == record.mapName)
			{
				// Games on the same map
				info.sameGames += 1;
				if (record.isWinner == true)
				{
					info.sameWins += 1;
				}
			}
			else
			{
				// Games on other maps
				info.otherGames += 1;
				if (record.isWinner == true)
				{
					info.otherWins += 1;
				}
			}
			info.games = info.sameGames + info.otherGames;
			info.wins = info.sameWins + info.otherWins;

			if (record.expectedEnemyPlan == record.recognizedEnemyPlan)
			{
				// The plan was recorded as correctly predicted in that game.
				planInfo.sameGames += 1;
				if (record.isWinner == true)
				{
					planInfo.sameWins += 1;
				}
			}
			else
			{
				// The plan was not correctly predicted.
				planInfo.otherGames += 1;
				if (record.isWinner == true)
				{
					planInfo.otherWins += 1;
				}
			}
			planInfo.games = planInfo.sameGames + planInfo.otherGames;
			planInfo.wins = planInfo.sameWins + planInfo.otherWins;
		}
	}


	// Disable the rest in training mode
	/*if (Config::Strategy::TrainingMode)
	{
		_recommendedOpening = getOpeningForEnemyPlan(_expectedEnemyPlan);
		Log().Get() << "Training Mode: " << _recommendedOpening;
		return;
	}*/

	// For the first games, stick to the counter openings based on the predicted plan.
	if (totalGames <= 5)
	{
		_recommendedOpening = getOpeningForEnemyPlan(_expectedEnemyPlan);
		Log().Get() << "5 games or less: " << _recommendedOpening;
		return;										// with or without expected play
	}
	/*
	// If we keep winning, stick to the winning track.
	if (totalWins == totalGames ||
		_singleStrategy && planInfo.sameWins > 0 && 
		planInfo.sameWins == planInfo.sameGames)   // Unknown plan is OK
	{
		_recommendedOpening = getOpeningForEnemyPlan(_expectedEnemyPlan);
		Log().Get() << "We keep wining: " << _recommendedOpening;
		return;										// with or without expected play
	}
	*/

	//Highest strategy winrate
	double winrateStratHighest = 0.0;
	std::string stratHighest;

	if (Config::Strategy::PlayGoodStrategiesFirst == true)
	{
		// Randomly choose any opening that always wins, or always wins on this map, or has a high win rate
		// This bypasses the map weighting.
		// The algorithm is reservoir sampling with reservoir size = 1.
		// It gives equal probabilities without remembering all the elements.

		std::string alwaysWins;
		double nAlwaysWins = 0.0;
		double winrateAlwaysWins = 0.0;
		int gamesAlwaysWins = 0;

		std::string alwaysWinsMap;
		double nAlwaysWinsOnThisMap = 0.0;
		double winrateAlwaysWinsMap = 0.0;
		int gamesAlwaysWinsMap = 0;

		std::string highWinrate;
		double nHighWinrate = 0.0;
		double winrateHighWinrate = 0.0;
		int gamesHighWinrate = 0;

		for (auto & item : openingInfo)
		{
			const OpeningInfoType & info = item.second;
			const std::string strategyName = item.first;

			const int games = info.sameGames + info.otherGames;
			const double winRate = (info.sameGames + info.otherGames) ?
				(double)(info.sameWins + info.otherWins) / (double)(info.sameGames + info.otherGames) : 0.0;
			const double sameWinRate = info.sameGames ? (double)info.sameWins / (double)info.sameGames : 0.0;

			// DEBUG
			Log().Debug() << "Winrate: "
				<< strategyName << " "
				<< info.sameWins << " "
				<< info.otherWins << " "
				<< info.sameGames << " "
				<< info.otherGames << " "
				<< winRate << " "
				<< sameWinRate;

			if (winRate > winrateStratHighest)
			{
				stratHighest = strategyName;
				winrateStratHighest = winRate;
			}

			if (winRate == 1.0)
			{
				nAlwaysWins += 1.0;
				if (Random::Instance().flag(1.0 / nAlwaysWins))
				{
					alwaysWins = strategyName;
					winrateAlwaysWins = winRate;
					gamesAlwaysWins = games;
				}
			}
			if (sameWinRate == 1.0 && info.sameGames >= 3)
			{
				nAlwaysWinsOnThisMap += 1.0;
				if (Random::Instance().flag(1.0 / nAlwaysWinsOnThisMap))
				{
					alwaysWinsMap = strategyName;
					winrateAlwaysWinsMap = winRate;
					gamesAlwaysWinsMap = games;
				}
			}
			if (winRate >= 0.80)
			{
				nHighWinrate += 1.0;
				if (Random::Instance().flag(1.0 / nHighWinrate))
				{
					highWinrate = strategyName;
					winrateHighWinrate = winRate;
					gamesHighWinrate = games;
				}
			}
		}
		if (!alwaysWins.empty())
		{	
			Log().Get() << "Always Wins: games = " << gamesAlwaysWins << "  winrate = " << winrateAlwaysWins;
			_recommendedOpening = alwaysWins;
			return;
		}
		if (!alwaysWinsMap.empty())
		{
			Log().Get() << "Always Wins Map: games = " << gamesAlwaysWinsMap << "  winrate = " << winrateAlwaysWinsMap;
			_recommendedOpening = alwaysWinsMap;
			return;
		}
		/*if (!highWinrate.empty())
		{
			Log().Get() << "High Winrate: games = " << gamesHighWinrate << "  winrate = " << winrateHighWinrate;
			_recommendedOpening = highWinrate;
			return;
		}*/
	}

	// Compute "weighted" win rates which combine map win rates and overall win rates, as an
	// estimate of the true win rate on this map. The estimate is ad hoc, using an assumption
	// that is sure to be wrong.
	for (auto it = openingInfo.begin(); it != openingInfo.end(); ++it)
	{
		OpeningInfoType & info = it->second;

		// Evidence provided by game results is proportional to the square root of the number of games.
		// So let's pretend that a game played on the same map provides mapPower times as much evidence
		// as a game played on another map.
		double mapPower = info.sameGames ? (info.sameGames + info.otherGames) / sqrt(info.sameGames) : 0.0;

		info.weightedWins = mapPower * info.sameWins + info.otherWins;
		info.weightedGames = mapPower * info.sameGames + info.otherGames;
	}

	// Compute winrate for the last 30 games
	double agingWinrate = getAgingWinrate();

	// Explore different actions this proportion of the time.
	// The number varies depending on the overall win rate: Explore less if we're usually winning.
	const double overallWinRate = double(totalWins) / totalGames;
	UAB_ASSERT(overallWinRate >= 0.0 && overallWinRate <= 1.0, "bad total");

	// Use the highest winrate in calculation of exploration rate
	double useWinrate = std::max({ overallWinRate, agingWinrate, winrateStratHighest });

	// Linear exploration rate
	//const double explorationRate = 0.05 + ((1.0 - useWinrate) * 0.35);
	
	// Sigmoid exploration rate
	const double explorationRate = 1 / (1 + exp(-1.73 * (1.0 - useWinrate))) - 0.5;

	// Wrong plan rate
	const double wrongPlanRate = double(planInfo.otherGames) / totalGames;

	Log().Get() << "overallWinRate: " << overallWinRate;
	Log().Get() << "explorationRate: " << explorationRate;
	Log().Get() << "wrongPlanRate: " << wrongPlanRate;
	Log().Get() << "agingWinrate: " << agingWinrate;
	Log().Get() << "stratHighWinrate: " << winrateStratHighest;
	Log().Get() << "useWinrate: " << useWinrate;
	
	// Decide whether to explore.
	//if (totalWins == 0 || Random::Instance().flag(explorationRate))
	if (agingWinrate == 0.0 || Random::Instance().flag(explorationRate))
	{
		// Is the predicted enemy plan likely to be right?
		if (totalGames > 30 && Random::Instance().flag(0.66))
		{
			_recommendedOpening = "Random";
			Log().Get() << "Exploration: Random";
		}
		else if (totalGames > 20 && totalGames <= 30 && Random::Instance().flag(0.66))
		{
			_recommendedOpening = "Matchup";
			Log().Get() << "Exploration: Matchup";
		}
		else if (Random::Instance().flag(0.8 * wrongPlanRate * double(std::min(totalGames, 20)) / 20.0))
		{
			_recommendedOpening = "Matchup";
			Log().Get() << "Exploration: Matchup";
		}
		else
		{
			_recommendedOpening = getOpeningForEnemyPlan(_expectedEnemyPlan);
			Log().Get() << "Exploration: " << _recommendedOpening;
		}

		return;
	}


	if (_singleStrategy)
	{
		Log().Get() << "Single Strategy Enemy";

		// We're not exploring. Choose an opening with the best weighted win rate.
		double bestScore = -1.0;		// every opening will have a win rate >= 0
		double nBest = 1.0;
		std::string bestStrategy;
		int bestGames = 0;
		double bestWinrate = 0.0;

		// compute weight factors considering age of games and recognised enemy plan
		//auto& strategyWeightFactors = getStrategyAgingFactors();

		for (auto it = openingInfo.begin(); it != openingInfo.end(); ++it)
		{
			const OpeningInfoType & info = it->second;
			const std::string strategyName = it->first;

			const double score = info.weightedGames < 0.1 ? 0.0 : info.weightedWins / info.weightedGames;
			const int games = info.sameGames + info.otherGames;
			//const double ageFactor = strategyWeightFactors[strategyName];
			const double ageFactor = 1.0;

			double totalscore;
			if (ageFactor > 0.0) totalscore = score * ageFactor;
			else totalscore = score;

			// DEBUG
			Log().Debug() << "Weighted Winrate: "
				<< strategyName << " "
				<< info.weightedWins << " "
				<< info.weightedGames << " "
				<< score << " "
				<< ageFactor << " "
				<< totalscore;

			if (totalscore > bestScore)
			{
				bestStrategy = strategyName;
				bestGames = games;
				bestScore = totalscore;
				nBest = 1.0;
			}
			else if (bestScore - totalscore < 0.001)	// we know score <= bestScore
			{
				// We choose randomly among openings with essentially equal score, using reservoir sampling.
				nBest += 1.0;
				if (Random::Instance().flag(1.0 / nBest))
				{
					bestStrategy = strategyName;
					bestGames = games;
					bestScore = totalscore;
				}
			}
		}
	
		Log().Get() << "Weighted Winrate: games = " << bestGames << "  weighted winrate = " << bestScore;
		_recommendedOpening = bestStrategy;
		return;

	}
	else
	{
		Log().Get() << "Multiple Strategy Enemy";

		// compute weight factors considering age of games and recognised enemy plan
		//auto& strategyWeightFactors = getStrategyAgingFactors();

		// find max weighted winrate.
		double maxWeightedWinrate = 0.0;
		double maxTotalScore = 0.0;
		for (auto it = openingInfo.begin(); it != openingInfo.end(); ++it)
		{
			const OpeningInfoType & info = it->second;
			const std::string strategyName = it->first;

			double weightedWinrate = info.weightedGames < 0.1 ? 0.0 : info.weightedWins / info.weightedGames;
			//double ageFactor = strategyWeightFactors[strategyName];
			double ageFactor = 1.0;

			double totalscore;
			if (ageFactor > 0.0) totalscore = weightedWinrate * ageFactor;
			else totalscore = weightedWinrate;

			if (weightedWinrate > maxWeightedWinrate)
			{
				maxWeightedWinrate = weightedWinrate;
			}
		}

		// The specific openings already tried.
		double nextTotal = 0;
		std::vector< std::pair<std::string, double> > openings;

		for (auto it = openingInfo.begin(); it != openingInfo.end(); ++it)
		{
			const OpeningInfoType & info = it->second;
			const std::string strategyName = it->first;

			const double weightedWinrate = info.weightedGames < 0.1 ? 0.0 : info.weightedWins / info.weightedGames;
			//const double ageFactor = strategyWeightFactors[strategyName];
			const double ageFactor = 1.0;

			double totalscore;
			if (ageFactor > 0.0) totalscore = weightedWinrate * ageFactor;
			else totalscore = weightedWinrate;

			if (info.weightedWins > 0.0 && 
				weightedWinrate > 0.5 * maxWeightedWinrate)
			{

				nextTotal += totalscore;
				openings.push_back(std::pair<std::string, double>(it->first, nextTotal));

				// DEBUG
				Log().Debug() << "Weighted Winrate: "
					<< strategyName << " "
					<< info.weightedWins << " "
					<< info.weightedGames << " "
					<< weightedWinrate << " "
					<< ageFactor << " "
					<< totalscore;
			}
		}

		// Choose randomly by weight.
		double w = Random::Instance().range(nextTotal);
		for (size_t i = 0; i < openings.size(); ++i)
		{
			if (w < openings[i].second)
			{
				_recommendedOpening = openings[i].first;
				break;
			}
		}

	}
}

// Look through past games and adjust our strategy weights considering map, aging and recognised enemy plan
std::map<std::string, double> StrategyManager::getStrategyWeightFactors() const
{
	std::map<std::string, double> result;
	std::map<std::string, int> strategyCount;
	std::map<std::string, int> strategyLosses;

	std::ostringstream log;
	log << "Deciding strategy aging factors for enemy plan and map:";

	// If we have a game record in the past 25 games where the enemy used the expected plan for this game,
	// only consider games that used this plan
	bool onlyConsiderSamePlan = false;
	int count = 0;
	if (_expectedEnemyPlan != OpeningPlan::Unknown && _expectedEnemyPlan != OpeningPlan::NotFastRush)
		for (auto it = _historyRecords.rbegin(); it != _historyRecords.rend() && count < 25; it++)
		{
			if (!sameMatchup(*it)) continue;
			count++;

			if ((it)->recognizedEnemyPlan == _expectedEnemyPlan)
			{
				onlyConsiderSamePlan = true;
				break;
			}
		}

	// Compute a factor to adjust the weight for each strategy
	// More recent results are weighted more heavily
	// Results on the same map are weighted more heavily
	count = 0;
	for (auto it = _historyRecords.rbegin(); it != _historyRecords.rend(); it++)
	{
		if (!sameMatchup(*it)) continue;
		//if (onlyConsiderSamePlan && (it)->recognizedEnemyPlan != _expectedEnemyPlan) continue;
		if (onlyConsiderSamePlan && (it)->recognizedEnemyPlan != _expectedEnemyPlan
			&& (it)->recognizedEnemyPlan != OpeningPlan::Unknown) continue;

		count++;

		std::string mapName = BWAPI::Broodwar->mapFileName();
		mapName.erase(std::remove(mapName.begin(), mapName.end(), ' '), mapName.end());
		mapName.erase(std::remove(mapName.begin(), mapName.end(), '_'), mapName.end());
		bool sameMap = (it)->mapName == mapName;

		auto& strategy = (it)->myStrategyName;

		log << "\n" << count << ": " << strategy << " " << ((it)->isWinner ? "won" : "lost") << " on " << (it)->mapName << ". ";

		if (result.find(strategy) == result.end())
		{
			result[strategy] = 1.0;
			strategyCount[strategy] = 0;
			strategyLosses[strategy] = 0;
		}

		double factor = result[strategy];
		strategyCount[strategy] = strategyCount[strategy] + 1;

		double aging = std::pow(strategyCount[strategy], 1.1);

		log << "Aging factor " << aging << "; initial weight factor " << factor;

		if ((it)->isWinner)
		{
			factor *= 1.0 + (sameMap ? 0.6 : 0.4) / aging;
		}
		else
		{
			factor *= 1.0 - (sameMap ? 0.7 : 0.5) / aging;
			strategyLosses[strategy] = strategyLosses[strategy] + 1;
		}

		log << "; updated to " << factor;

		result[strategy] = factor;
	}

	// Analyze the losses
	for (auto it = strategyLosses.begin(); it != strategyLosses.end(); it++)
	{
		// Any strategies that have never lost are given a boost
		/*if (it->second == 0)
		{
			result[it->first] = result[it->first] * 2;
			log << "\nBoosting " << it->first << " to " << result[it->first] << " as it has never lost";
		}*/

		// Any strategies that have been played at least 3 times and always lost are given a large penalty
		/*if (it->second >= 3 && strategyCount[it->first] == it->second)
		{
			result[it->first] = result[it->first] * 0.1;
			log << "\nLowering " << it->first << " to " << result[it->first] << " as it has never won";
		}*/
	}

	Log().Debug() << log.str();

	return result;
}

// Look through past games and adjust our strategy weights considering aging and recognised enemy plan
std::map<std::string, double> StrategyManager::getStrategyAgingFactors() const
{
	std::map<std::string, double> result;
	std::map<std::string, int> strategyCount;
	std::map<std::string, int> strategyLosses;

	std::ostringstream log;
	log << "Deciding strategy aging factors for enemy plan:";

	// If we have a game record in the past 25 games where the enemy used the expected plan for this game,
	// only consider games that used this plan
	bool onlyConsiderSamePlan = false;
	int count = 0;
	if (_expectedEnemyPlan != OpeningPlan::Unknown && _expectedEnemyPlan != OpeningPlan::NotFastRush)
		for (auto it = _historyRecords.rbegin(); it != _historyRecords.rend() && count < 25; it++)
		{
			if (!sameMatchup(*it)) continue;
			count++;

			if ((it)->recognizedEnemyPlan == _expectedEnemyPlan)
			{
				onlyConsiderSamePlan = true;
				break;
			}
		}

	// Compute a factor to adjust the weight for each strategy
	// More recent results are weighted more heavily
	count = 0;
	for (auto it = _historyRecords.rbegin(); it != _historyRecords.rend(); it++)
	{
		if (!sameMatchup(*it)) continue;
		//if (onlyConsiderSamePlan && (it)->recognizedEnemyPlan != _expectedEnemyPlan) continue;
		if (onlyConsiderSamePlan && (it)->recognizedEnemyPlan != _expectedEnemyPlan 
			&& (it)->recognizedEnemyPlan != OpeningPlan::Unknown) continue;

		count++;
		auto& strategy = (it)->myStrategyName;

		log << "\n" << count << ": " << strategy << " " << ((it)->isWinner ? "won" : "lost") << " on " << (it)->mapName << ". ";

		if (result.find(strategy) == result.end())
		{
			result[strategy] = 1.0;
			strategyCount[strategy] = 0;
			strategyLosses[strategy] = 0;
		}

		double factor = result[strategy];
		strategyCount[strategy] = strategyCount[strategy] + 1;

		double aging = std::pow(strategyCount[strategy], 1.1);

		log << "Aging factor " << aging << "; initial weight factor " << factor;

		if ((it)->isWinner)
		{
			factor *= 1.0 + (0.4 / aging);
		}
		else
		{
			factor *= 1.0 - (0.5 / aging);
			strategyLosses[strategy] = strategyLosses[strategy] + 1;
		}

		log << "; updated to " << factor;

		result[strategy] = factor;
	}

	// Analyze the losses
	for (auto it = strategyLosses.begin(); it != strategyLosses.end(); it++)
	{
		// Any strategies that have never lost are given a boost
		/*if (it->second == 0)
		{
			result[it->first] = result[it->first] * 2;
			log << "\nBoosting " << it->first << " to " << result[it->first] << " as it has never lost";
		}*/

		// Any strategies that have been played at least 3 times and always lost are given a large penalty
		/*if (it->second >= 3 && strategyCount[it->first] == it->second)
		{
			result[it->first] = result[it->first] * 0.1;
			log << "\nLowering " << it->first << " to " << result[it->first] << " as it has never won";
		}*/
	}

	Log().Debug() << log.str();

	return result;
}

// Look through past games and calculate our aging winrate
double StrategyManager::getAgingWinrate() const
{
	double result = 1.0;
	double winrate = 1.0;
	int strategyCount = 0;
	double strategyLosses = 0;
	double strategyWins = 0;
	int count = 0;

	std::ostringstream log;
	log << "Deciding aging winrate:";

	// Compute a aging factor from the last 30 games
	// More recent results are weighted more heavily
	count = 0;
	for (auto it = _historyRecords.rbegin(); it != _historyRecords.rend(); it++)
	{
		if (!sameMatchup(*it)) continue;
		if (count > 29) break;

		count++;

		std::string mapName = BWAPI::Broodwar->mapFileName();
		mapName.erase(std::remove(mapName.begin(), mapName.end(), ' '), mapName.end());
		mapName.erase(std::remove(mapName.begin(), mapName.end(), '_'), mapName.end());
		bool sameMap = (it)->mapName == mapName;

		auto& strategy = (it)->myStrategyName;

		log << "\n" << count << ": " << strategy << " " << ((it)->isWinner ? "won" : "lost") << " on " << (it)->mapName << ". ";

		double factor = result;

		// sq. root of 0.5 = 0.707106781186548;
		// cubic root of 0.5 = 0.7937005259841;
		// 5th root of 0.5 = 0.870550563296124;
		// 10th root of 0.5 = 0.933032991536807;
		// 20th root of 0.5 = 0.965936328924846;
		// 30th root of 0.5 = 0.977159968434246;
		if (count == 1)
			factor = 1.0;
		else
			factor *= 1.0;

		if ((it)->isWinner)
		{
			strategyWins = strategyWins + factor;
		}
		else
		{
			strategyLosses = strategyLosses + factor;
		}

		log << "aging weight " << factor << "; weighted wins " << strategyWins << "; weighted losses " << strategyLosses;

		result = factor;
	}

	winrate = count == 0 ? 0.0 : strategyWins / (strategyWins + strategyLosses);

	Log().Debug() << log.str();

	return winrate;
}

// Read past game records from the opponent model file, and do initial analysis.
void StrategyManager::setInitialExpectedEnemyPlan()
{
	// Make immediate decisions that may take into account the game records.
	// The initial expected enemy plan is set only here. That's the idea.
	// The current expected enemy plan may be reset later.
	_expectedEnemyPlan = _initialExpectedEnemyPlan = predictEnemyPlan();
	considerSingleStrategy();

	Log().Get() << "Initial Expected Plan: " << OpeningPlanString(_expectedEnemyPlan);
}

// Possibly update the expected enemy plan.
// This runs later in the game, when we may have more information.
void StrategyManager::reconsiderEnemyPlan()
{
	if (_recognizedEnemyPlan.getPlan() != OpeningPlan::Unknown)
	{
		// We already know the actual plan. No need to form an expectation.
		return;
	}

	if (!_currentRecord.enemyIsRandom || BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Unknown)
	{
		// For now, we only update the expected plan if the enemy went random
		// and we have now learned its race. 
		// The new information should narrow down the possibilities.
		return;
	}

	// Don't reconsider too often.
	if (BWAPI::Broodwar->getFrameCount() % 12 != 8)
	{
		return;
	}

	// We set the new expected plan even if it is Unknown. Better to know that we don't know.
	_expectedEnemyPlan = predictEnemyPlan();
}

OpeningPlan StrategyManager::predictEnemyPlan() const
{
	// Don't bother to predict on island maps.
	// Data from other maps will only be misleading.
	if (MapTools::Instance().hasIslandBases())
	{
		return OpeningPlan::Unknown;
	}

	struct PlanInfoType
	{
		int wins;
		int games;
		double weight;
	};
	PlanInfoType planInfo[int(OpeningPlan::Size)];

	// 1. Initialize.
	for (int plan = int(OpeningPlan::Unknown); plan < int(OpeningPlan::Size); ++plan)
	{
		planInfo[plan].wins = 0;
		planInfo[plan].games = 0;
		planInfo[plan].weight = 0.0;
	}

	std::ostringstream log;
	log << "Predicting enemy plan:";

	// 2. Weight opponent plans
	double weight = 1.0;
	int count = 0;
	for (auto & record : _historyRecords)
	{
		count++;

		if (sameMatchup(record))
		{
			log << "\n" << count << ": " << OpeningPlanString(record.recognizedEnemyPlan);
			if (record.isWinner)
				log <<" (win): ";
			else
				log << " (loss): ";

			PlanInfoType & info = planInfo[int(record.recognizedEnemyPlan)];
			info.games += 1;
			//info.weight += weight;

			// Weight games we won lower
			double change = weight * (record.isWinner ? 0.5 : 1.0);
			info.weight += change;
			log << "Increased by " << change << " to " << info.weight;

			weight *= 1.25;        // more recent game records are more heavily weighted
		}
	}

	// 3. Decide.
	// For now, set the most heavily weighted plan other than Unknown as the expected plan. Ignore the other info.
	OpeningPlan bestPlan = OpeningPlan::Unknown;
	double bestWeight = 0.0;
	for (int plan = int(OpeningPlan::Unknown) + 1; plan < int(OpeningPlan::Size); ++plan)
	{
		if (planInfo[plan].weight > bestWeight)
		{
			bestPlan = OpeningPlan(plan);
			bestWeight = planInfo[plan].weight;
		}
	}

	if (count > 0)
	{
		log << "\nDecided on " << OpeningPlanString(bestPlan);
	}

	Log().Debug() << log.str();

	return bestPlan;
}

// Does the opponent seem to play the same strategy every game?
// If we're pretty sure, set _singleStrategy to true.
// So far, we only check the plan. We have plenty of other data that could be helpful.
void StrategyManager::considerSingleStrategy()
{
	// Only use single strategy...
	_singleStrategy = true;
	return;

	/* --- */

	// Humans, we assume, are unpredictable.
	if (Config::Strategy::UseHumanSpecificStrategy)
	{
		_singleStrategy = false;
		return;
	}

	// Random, better be safe...
	if (_enemyIsRandom)
	{
		_singleStrategy = true;
		return;
	}

	// Gather info.
	int knownPlan = 0;
	int unknownPlan = 0;
	std::set<OpeningPlan> plansSeen;

	for (auto & record : _historyRecords)
	{
		if (sameMatchup(record))
		{
			OpeningPlan plan = record.recognizedEnemyPlan;
			if (plan == OpeningPlan::Unknown)
			{
				unknownPlan += 1;
			}
			else
			{
				knownPlan += 1;
				plansSeen.insert(plan);
			}
		}
	}

	// Decide.
	// If we don't recognize the majority of plans, we're not sure.
	if (knownPlan >= 2 && plansSeen.size() == 1 && unknownPlan <= knownPlan)
	{
		_singleStrategy = true;
	}
}

// We expect the enemy to follow the given opening plan.
// Recommend an opening to counter that plan.
// The counters are configured; all we have to do is name the strategy mix.
// The empty opening "" means play the regular openings, no plan recognized.
std::string StrategyManager::getOpeningForEnemyPlan(OpeningPlan enemyPlan)
{
	if (enemyPlan == OpeningPlan::Unknown)
	{
		return "";
	}
	return "Counter " + OpeningPlanString(enemyPlan);
}

// The game records have the same matchup, as best we can tell so far.
// For checks at the start of the game, when the enemy's race may be unknown, allow
// a special case for random enemies.
bool StrategyManager::sameMatchup(const HistoryRecord & record) const
{
	return _selfRace == record.myRace &&
		(
			_enemyRace == record.enemyRace ||
			_enemyIsRandom && record.enemyIsRandom &&
			(_enemyRace == BWAPI::Races::Unknown || record.enemyRace == BWAPI::Races::Unknown)
		);
}

// The inferred enemy opening plan from playing.
OpeningPlan StrategyManager::getRecognizedEnemyPlan() const
{
	return _recognizedEnemyPlan.getPlan();
}

// The expected enemy opening plan from history.
OpeningPlan StrategyManager::getExpectedEnemyPlan() const
{
	return _expectedEnemyPlan;
}

// String for displaying the recognized enemy opening plan in the UI.
std::string StrategyManager::getRecognizedEnemyPlanString() const
{
	return OpeningPlanString(_recognizedEnemyPlan.getPlan());
}

// String for displaying the expected enemy opening plan in the UI.
std::string StrategyManager::getExpectedEnemyPlanString() const
{
	return OpeningPlanString(_expectedEnemyPlan);
}

// Will this count of unit type arrive at main base by this frame?
bool StrategyManager::unitTypeArrivesBy(BWAPI::UnitType type, int frame, int count)
{
	int unitCount = 0;
	BWAPI::Position origin;
	BWAPI::Position destination;

	for (const auto & kv : InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.completed && ui.type == type)
		{
			int dist = 0;
			if (ui.type.isFlyer())
			{
				dist = (int)origin.getApproxDistance(destination);
			}
			else
			{
				dist = (int)(BWEB::Map::getGroundDistance(origin, destination));
			}

			if (dist > 0)
			{
				int distFrames = (int)(dist / ui.type.topSpeed());
			}

		}
	}

	return false;
}

void StrategyManager::handleUrgentProductionIssues(BuildOrderQueue & queue)
{
	// Profile debug
	//PROFILE_FUNCTION();

	if (_selfRace == BWAPI::Races::Zerg)
	{
		StrategyBossZerg::Instance().handleUrgentProductionIssues(queue);
	}
	else
	{
		// detect if there's a supply block once per second
		if ((BWAPI::Broodwar->getFrameCount() % 24 == 0) && detectSupplyBlock(queue) &&
			!queue.anyInQueue(BWAPI::Broodwar->self()->getRace().getSupplyProvider()))
		{
			if (Config::Debug::DrawBuildOrderSearchInfo)
			{
				BWAPI::Broodwar->printf("Supply block, building supply!");
			}

			queue.queueAsHighestPriority(MacroAct(BWAPI::Broodwar->self()->getRace().getSupplyProvider()));
		}

		// If they have mobile cloaked units, get some static detection.
		if (InformationManager::Instance().enemyHasMobileCloakTech())
		{
			if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Protoss)
			{
				if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Protoss_Forge) == 0 &&
					!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Forge) &&
					!queue.anyInQueue(BWAPI::UnitTypes::Protoss_Forge) &&
					!queue.buildingInQueue())
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Forge));
				}

				if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Protoss_Photon_Cannon) < 2 &&
					BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Protoss_Forge) > 0 &&
					!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Photon_Cannon) &&
					!queue.anyInQueue(BWAPI::UnitTypes::Protoss_Photon_Cannon) &&
					!queue.buildingInQueue())
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Photon_Cannon));
				}
			}
			else if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Terran)
			{
				if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Terran_Engineering_Bay) == 0 &&
					!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Engineering_Bay) &&
					!queue.anyInQueue(BWAPI::UnitTypes::Terran_Engineering_Bay) &&
					!queue.buildingInQueue())
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Engineering_Bay));
				}

				if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Terran_Missile_Turret) < 3 &&
					BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Terran_Engineering_Bay) > 0 &&
					!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Missile_Turret) &&
					!queue.anyInQueue(BWAPI::UnitTypes::Terran_Missile_Turret) &&
					!queue.buildingInQueue())
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Missile_Turret));
				}

			}

			if (Config::Debug::DrawBuildOrderSearchInfo)
			{
				BWAPI::Broodwar->printf("Enemy has cloaking tech!");
			}
		}
	}
}

// Return true if we're supply blocked and should build supply.
// Note: This understands zerg supply but is not used when we are zerg.
bool StrategyManager::detectSupplyBlock(BuildOrderQueue & queue)
{

	// If we are maxed out there is no block.
	if (BWAPI::Broodwar->self()->supplyTotal() >= 400)
	{
		return false;
	}

	// If supply is being built now, there is no block.
	if (BuildingManager::Instance().isBeingBuilt(BWAPI::Broodwar->self()->getRace().getSupplyProvider()))
	{
		return false;
	}

	// Terran and protoss calculation:
	int supplyAvailable = BWAPI::Broodwar->self()->supplyTotal() - BWAPI::Broodwar->self()->supplyUsed();

	// Zerg calculation:
	// Zerg can create an overlord that doesn't count toward supply until the next check.
	// To work around it, add up the supply by hand, including hatcheries.
	if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg) {
		supplyAvailable = -BWAPI::Broodwar->self()->supplyUsed();
		for (auto & unit : BWAPI::Broodwar->self()->getUnits())
		{
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
			{
				supplyAvailable += 16;
			}
			else if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg &&
				unit->getBuildType() == BWAPI::UnitTypes::Zerg_Overlord)
			{
				return false;    // supply is building, return immediately
				// supplyAvailable += 16;
			}
			else if ((unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery && unit->isCompleted()) ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Lair ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Hive)
			{
				supplyAvailable += 2;
			}
		}
	}

	int supplyCost = 0;
	if (!queue.isEmpty())
	{
		supplyCost = queue.getHighestPriorityItem().macroAct.supplyRequired();
	}

	// Available supply can be negative, which breaks the test below. Fix it.
	supplyAvailable = std::max(0, supplyAvailable);

	//BWAPI::Broodwar->printf("supplyAvailable %d", supplyAvailable);
	//BWAPI::Broodwar->printf("supplyCost %d", supplyCost);

	// if we don't have enough supply, we're supply blocked
	if (supplyAvailable < supplyCost + 8)
	{
		// If we're zerg, check to see if a building is planned to be built.
		// Only count it as releasing supply very early in the game.
		if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg
			&& BuildingManager::Instance().buildingsQueued().size() > 0
			&& BWAPI::Broodwar->self()->supplyTotal() <= 18)
		{
			return false;
		}
		return true;
	}

	return false;
}


int StrategyManager::updateLingScore()
{
	double score = 0;

	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		// Mobile combat units
		if (!ui.type.isWorker() && !ui.type.isBuilding() &&
			ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Zergling, ui.type);
			score += dps;
		}
		// Static defense
		else if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
			ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Zergling, ui.type);
			score += dps;
		}
	}

	_lingScore = (int)score;

	return (int)score;
}


int StrategyManager::updateHydraScore()
{
	double score = 0;

	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		// Mobile combat units
		if (!ui.type.isWorker() && !ui.type.isBuilding() &&
			ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Hydralisk, ui.type);
			score += dps;
		}
		// Static defense
		else if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
			ui.type == BWAPI::UnitTypes::Terran_Bunker ||
			ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Hydralisk, ui.type);
			score += dps;
		}
	}

	_hydraScore = (int)score;

	return (int)score;
}

int StrategyManager::updateMutaScore()
{
	double score = 0;

	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		// Mobile combat units
		if (!ui.type.isWorker() && !ui.type.isBuilding() &&
			ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Mutalisk, ui.type);
			score += dps;
		}
		// Static defense
		else if (ui.type == BWAPI::UnitTypes::Zerg_Sunken_Colony)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Mutalisk, ui.type);
			score += dps;
		}
	}

	_mutaScore = (int)score;

	return (int)score;
}


int StrategyManager::updateLurkerScore()
{
	double score = 0;

	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		// Mobile combat units
		if (!ui.type.isWorker() && !ui.type.isBuilding() &&
			ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Lurker, ui.type);
			score += dps;
		}
		// Static defense
		else if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
			ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Lurker, ui.type);
			score += dps;
		}
	}

	_lurkerScore = (int)score;

	return (int)score;
}


int StrategyManager::updateUltraScore()
{
	double score = 0;

	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		// Mobile combat units
		if (!ui.type.isWorker() && !ui.type.isBuilding() &&
			ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Ultralisk, ui.type);
			score += dps;
		}
		// Static defense
		else if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
			ui.type == BWAPI::UnitTypes::Terran_Bunker ||
			ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
			ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony ||
			ui.type == BWAPI::UnitTypes::Zerg_Sunken_Colony)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Ultralisk, ui.type);
			score += dps;
		}
	}

	_ultraScore = (int)score;

	return (int)score;
}

int StrategyManager::updateGuardianScore()
{
	double score = 0;

	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		// Mobile combat units
		if (!ui.type.isWorker() && !ui.type.isBuilding() &&
			ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Guardian, ui.type);
			score += dps;
		}
		// Static defense
		else if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
			ui.type == BWAPI::UnitTypes::Terran_Bunker ||
			ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
			ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony ||
			ui.type == BWAPI::UnitTypes::Zerg_Sunken_Colony)
		{
			double dps = UnitUtil::CalculateDPS(BWAPI::UnitTypes::Zerg_Guardian, ui.type);
			score += dps;
		}
	}

	_guardianScore = (int)score;

	return (int)score;
}

// Unused for now
// Ling score, cost / supply factor = 25
int StrategyManager::getLingScore()
{
	int score = 0;

	// vs Protoss
	if (_enemyRace == BWAPI::Races::Protoss)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding() &&
				ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
			{
				// Gate units
				if (ui.type == BWAPI::UnitTypes::Protoss_Dragoon ||
					ui.type == BWAPI::UnitTypes::Protoss_High_Templar)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Fusion
				if (ui.type == BWAPI::UnitTypes::Protoss_Dark_Archon)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Robotics Facility
				if (ui.type == BWAPI::UnitTypes::Protoss_Reaver)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target ground
				if (!UnitUtil::TypeCanAttackGround(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}
		}
	}

	// vs Terran
	if (_enemyRace == BWAPI::Races::Terran)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{
				// Bio
				if (ui.type == BWAPI::UnitTypes::Terran_Marine ||
					ui.type == BWAPI::UnitTypes::Terran_Medic ||
					ui.type == BWAPI::UnitTypes::Terran_Ghost)	
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
				
				// Tanks
				if (ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
					ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target ground
				if (!UnitUtil::TypeCanAttackGround(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Static defense
			else if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}
		}
	}

	// vs Zerg
	if (_enemyRace == BWAPI::Races::Zerg)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{
				// Can not target ground
				if (!UnitUtil::TypeCanAttackGround(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Static defense
			else if (ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}
		}
	}

	//int costLing = InformationManager::Instance().getCostUnit(_self, BWAPI::UnitTypes::Zerg_Zergling);
	//score -= costLing;
	score = std::max(0, score);

	// Island hack
	_hasIslandBases = MapTools::Instance().hasIslandBases();
	if (_hasIslandBases)
	{
		score = 0;
	}

	techScores[int(TechUnit::Zerglings)] = score;
	_lingScore = score;

	return score;
}

// Hydra score, cost / supply factor = 25
int StrategyManager::getHydraScore()
{
	if (Config::Strategy::StrategyName == "5Pool Zergling Hell") return 0;
	int score = 0;

	// vs Protoss
	if (_enemyRace == BWAPI::Races::Protoss)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding() &&
				ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
			{
				//Gate units
				//if (ui.type == BWAPI::UnitTypes::Protoss_Zealot)
				//{
				//	score += (ui.type.mineralPrice() + ui.type.gasPrice());
				//}
				//Do not go Hydra vs Dark Templars
				if (ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar)
				{
					score -= (ui.type.mineralPrice() + ui.type.gasPrice());
				}
				//Do not go Hydra vs High Templars
				if (ui.type == BWAPI::UnitTypes::Protoss_High_Templar)
				{
					score -= (ui.type.mineralPrice() + ui.type.gasPrice());
				}
				//Do not go Hydra vs Dragoons
				if (ui.type == BWAPI::UnitTypes::Protoss_Dragoon)
				{
					score -= (ui.type.mineralPrice() + ui.type.gasPrice());
				}

				// Air
				if (ui.type == BWAPI::UnitTypes::Protoss_Carrier ||
					ui.type == BWAPI::UnitTypes::Protoss_Scout ||
					ui.type == BWAPI::UnitTypes::Protoss_Corsair)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Fusion
				if (ui.type == BWAPI::UnitTypes::Protoss_Archon || 
					ui.type == BWAPI::UnitTypes::Protoss_Dark_Archon)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target ground
				if (!UnitUtil::TypeCanAttackGround(ui.type))
				{
					//score += InformationManager::Instance().getCostUnit(ui.type);
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Static defense
			else if (ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}
		}
	}

	// vs Terran
	if (_enemyRace == BWAPI::Races::Terran)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{				
				// Vulture + Goliath
				if (ui.type == BWAPI::UnitTypes::Terran_Vulture || 
					ui.type == BWAPI::UnitTypes::Terran_Goliath)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			
				// Air
				if (ui.type == BWAPI::UnitTypes::Terran_Battlecruiser || 
					ui.type == BWAPI::UnitTypes::Terran_Wraith)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target ground
				if (!UnitUtil::TypeCanAttackGround(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Static defense
			else if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
				ui.type == BWAPI::UnitTypes::Terran_Bunker)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}

		}
	}
	

	//int costArmy = InformationManager::Instance().getCostArmy(_self);
	//score += costArmy;

	//int costHydra = InformationManager::Instance().getCostUnit(_self, BWAPI::UnitTypes::Zerg_Hydralisk);
	//score -= costHydra;
	score = std::max(0, score);


	// Activate hydras when score is above techcost
	int techcost = 0;

	int nDenAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den);
	if (nDenAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Hydralisk_Den).mineralPrice() + (BWAPI::UnitTypes::Zerg_Hydralisk_Den).gasPrice();
	}

	score -= techcost;
	score = std::max(0, score);


	// Bias
	if (/*_enemyRace == BWAPI::Races::Terran || */_enemyRace == BWAPI::Races::Protoss)
	{
		score += 300;
	}

	// Hysteresis
	if (_techTarget == "Hydras") { score += 300; };


	// Disable hydralisks vs Zerg
	if (_enemyRace == BWAPI::Races::Zerg)
	{
		score = 0;
	}

	// Island hack
	_hasIslandBases = MapTools::Instance().hasIslandBases();
	if (_hasIslandBases)
	{
		score = 0;
	}

	techScores[int(TechUnit::Hydralisks)] = score;
	_hydraScore = score;

	return score;
}

// Muta score, cost / supply factor = 25
int StrategyManager::getMutaScore()
{
	if (Config::Strategy::StrategyName == "5Pool Zergling Hell") return 0;
	int score = 0;

	// vs Protoss
	if (_enemyRace == BWAPI::Races::Protoss)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding() &&
				ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
			{
				// Gate units
				if (ui.type == BWAPI::UnitTypes::Protoss_Dragoon || 
					ui.type == BWAPI::UnitTypes::Protoss_High_Templar ||
					ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
				//Do not go Mutas vs Zealots, they are counted in can not target air
				if (ui.type == BWAPI::UnitTypes::Protoss_Zealot)
				{
					score -= ui.type.mineralPrice() + ui.type.gasPrice();
				}
				
				// Robotics Facility
				if (ui.type == BWAPI::UnitTypes::Protoss_Reaver)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice() + 150;
				}

				// Can not target air
				if (!UnitUtil::TypeCanAttackAir(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Mutalisks are good against anything from the robo fac.
			else if (ui.type == BWAPI::UnitTypes::Protoss_Robotics_Facility)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}
			// Mutalisks are especially good against reavers.
			else if (ui.type == BWAPI::UnitTypes::Protoss_Robotics_Support_Bay)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice() + 150;
			}
		}
	}

	// vs Terran
	if (_enemyRace == BWAPI::Races::Terran)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{
				// Mutalisks are especially good against tanks
				if (ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode || 
					ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice() + 300;
				}

				// Vulture
				if (ui.type == BWAPI::UnitTypes::Terran_Vulture)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target air
				if (!UnitUtil::TypeCanAttackAir(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

		}
	}

	// vs Zerg
	if (_enemyRace == BWAPI::Races::Zerg)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{				
				// Can not target air
				if (!UnitUtil::TypeCanAttackAir(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Static defense
			else if (ui.type == BWAPI::UnitTypes::Zerg_Sunken_Colony)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}
		}
	}

	//int costArmy = InformationManager::Instance().getCostArmy(_self);
	//score += costArmy;

	//int costMuta = InformationManager::Instance().getCostUnit(_self, BWAPI::UnitTypes::Zerg_Mutalisk);
	//score -= costMuta;
	score = std::max(0, score);


	// Activate mutas when score is above techcost
	int techcost = 0;

	int nLairsAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	if (nLairsAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Lair).mineralPrice() + (BWAPI::UnitTypes::Zerg_Lair).gasPrice();
	}

	int nSpireAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire);
	if (nSpireAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Spire).mineralPrice() + (BWAPI::UnitTypes::Zerg_Spire).gasPrice();
	}

	score -= techcost;
	score = std::max(0, score);


	// Bias
	if (/*_enemyRace != BWAPI::Races::Terran && */_enemyRace != BWAPI::Races::Protoss)
	{
		score += 300;
	}

	// Hysteresis
	if (_techTarget == "Mutas") { score += 300; };


	// Island hack
	_hasIslandBases = MapTools::Instance().hasIslandBases();
	if (_hasIslandBases)
	{
		score += 300;
	}


	techScores[int(TechUnit::Mutalisks)] = score;
	_mutaScore = score;

	return score;
}

// Lurker score, cost / supply factor = 25
int StrategyManager::getLurkerScore()
{
	if (Config::Strategy::StrategyName == "5Pool Zergling Hell") return 0;
	int score = 0;

	// vs Protoss
	if (_enemyRace == BWAPI::Races::Protoss)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding() &&
				ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
			{
				// Gate units
				if (ui.type == BWAPI::UnitTypes::Protoss_Zealot)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target ground
				if (!UnitUtil::TypeCanAttackGround(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}
		}
	}

	// vs Terran
	if (_enemyRace == BWAPI::Races::Terran)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{
				// Bio
				if (ui.type == BWAPI::UnitTypes::Terran_Marine || 
					ui.type == BWAPI::UnitTypes::Terran_Medic ||
					ui.type == BWAPI::UnitTypes::Terran_Firebat ||
					ui.type == BWAPI::UnitTypes::Terran_Ghost)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target ground
				if (!UnitUtil::TypeCanAttackGround(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}
		}
	}

	// vs Zerg
	if (_enemyRace == BWAPI::Races::Zerg)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{
				// Zerglings
				if (ui.type == BWAPI::UnitTypes::Zerg_Zergling)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target ground
				if (!UnitUtil::TypeCanAttackGround(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}
		}
	}

	//int costArmy = InformationManager::Instance().getCostArmy(_self);
	//score += costArmy;

	//int costLurker = InformationManager::Instance().getCostUnit(_self, BWAPI::UnitTypes::Zerg_Lurker);
	//score -= costLurker;
	score = std::max(0, score);


	// Activate lurkers when score is above techcost
	int techcost = 0;

	int nDenAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den);
	if (!nDenAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Hydralisk_Den).mineralPrice() + (BWAPI::UnitTypes::Zerg_Hydralisk_Den).gasPrice();
	}

	int nLairsAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	if (nLairsAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Lair).mineralPrice() + (BWAPI::UnitTypes::Zerg_Lair).gasPrice();
	}

	bool lurkerAsp = _self->hasResearched(BWAPI::TechTypes::Lurker_Aspect);
	if (!lurkerAsp) {
		techcost += (BWAPI::TechTypes::Lurker_Aspect).mineralPrice() + (BWAPI::TechTypes::Lurker_Aspect).gasPrice();
	}

	score -= techcost;
	score = std::max(0, score);


	// Bias

	// Hysteresis
	if (_techTarget == "Lurkers") { score += 300; };


	// Disable lurkers at hive tech vs Terran
	bool hasHive = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hive) > 0;
	if (hasHive && _enemyRace == BWAPI::Races::Terran)
	{
		score = 0;
	}

	// Activate lurkers at hive tech vs Zerg
	if (!hasHive && _enemyRace == BWAPI::Races::Zerg)
	{
		score = 0;
	}

	// Island hack
	_hasIslandBases = MapTools::Instance().hasIslandBases();
	if (_hasIslandBases)
	{
		score = 0;
	}

	techScores[int(TechUnit::Lurkers)] = score;
	_lurkerScore = score;

	return score;
}

// Ultra score, cost / supply factor = 25
int StrategyManager::getUltraScore()
{
	if (Config::Strategy::StrategyName == "5Pool Zergling Hell") return 0;
	int score = 0;

	// vs Protoss
	if (_enemyRace == BWAPI::Races::Protoss)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding() &&
				ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
			{
				// Gate units
				if (ui.type == BWAPI::UnitTypes::Protoss_High_Templar ||
					ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Fusion
				if (ui.type == BWAPI::UnitTypes::Protoss_Archon ||
					ui.type == BWAPI::UnitTypes::Protoss_Dark_Archon)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target ground
				if (!UnitUtil::TypeCanAttackGround(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Static defense
			else if (ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}

		}
	}

	// vs Terran
	if (_enemyRace == BWAPI::Races::Terran)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{
				// Bio
				if (ui.type == BWAPI::UnitTypes::Terran_Marine ||
					ui.type == BWAPI::UnitTypes::Terran_Medic ||
					ui.type == BWAPI::UnitTypes::Terran_Firebat ||
					ui.type == BWAPI::UnitTypes::Terran_Ghost)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target ground
				if (!UnitUtil::TypeCanAttackGround(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Static defense
			else if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
					ui.type == BWAPI::UnitTypes::Terran_Bunker)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}

		}
	}

	// vs Zerg
	if (_enemyRace == BWAPI::Races::Zerg)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{
				// Can not target ground
				if (!UnitUtil::TypeCanAttackGround(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Static defense
			else if (ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony ||
				ui.type == BWAPI::UnitTypes::Zerg_Sunken_Colony)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}
		}
	}

	//int costUltra = InformationManager::Instance().getCostUnit(_self, BWAPI::UnitTypes::Zerg_Ultralisk);
	//score -= costUltra;
	score = std::max(0, score);


	// Activate ultras when score is above techcost
	int techcost = 0;

	int nLairsAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	if (nLairsAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Lair).mineralPrice() + (BWAPI::UnitTypes::Zerg_Lair).gasPrice();
	}

	int nHiveAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	if (nHiveAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Hive).mineralPrice() + (BWAPI::UnitTypes::Zerg_Hive).gasPrice();
	}

	int nUltraAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) > 0;
	if (nUltraAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Ultralisk_Cavern).mineralPrice() + (BWAPI::UnitTypes::Zerg_Ultralisk_Cavern).gasPrice();
	}

	score -= techcost;
	score = std::max(0, score);


	// Bias

	// Hysteresis
	if (_techTarget == "Ultras") { score += 300; };


	// Island hack
	_hasIslandBases = MapTools::Instance().hasIslandBases();
	if (_hasIslandBases)
	{
		score = 0;
	}

	techScores[int(TechUnit::Ultralisks)] = score;
	_ultraScore = score;

	return score;
}

// Guardian score, cost / supply factor = 25
int StrategyManager::getGuardianScore()
{
	if (Config::Strategy::StrategyName == "5Pool Zergling Hell") return 0;
	int score = 0;

	// vs Protoss
	if (_enemyRace == BWAPI::Races::Protoss)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding() &&
				ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
			{
				// Slow defense units
				if (ui.type == BWAPI::UnitTypes::Protoss_Reaver || 
					ui.type == BWAPI::UnitTypes::Protoss_High_Templar)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target air
				if (!UnitUtil::TypeCanAttackAir(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Guardians are especially good against reavers.
			else if (ui.type == BWAPI::UnitTypes::Protoss_Robotics_Support_Bay)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice() + 150;
			}

			// Static defense
			else if (ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}

		}
	}

	// vs Terran
	if (_enemyRace == BWAPI::Races::Terran)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{
				// Slow defense units
				if (ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
					ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Can not target air
				if (!UnitUtil::TypeCanAttackAir(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Static defense
			else if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
				ui.type == BWAPI::UnitTypes::Terran_Bunker)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}

		}
	}

	// vs Zerg
	if (_enemyRace == BWAPI::Races::Zerg)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{
				// Can not target air
				if (!UnitUtil::TypeCanAttackAir(ui.type))
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}

			// Static defense
			else if (ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony ||
				ui.type == BWAPI::UnitTypes::Zerg_Sunken_Colony)
			{
				score += ui.type.mineralPrice() + ui.type.gasPrice();
			}
		}
	}

	//int costUltra = InformationManager::Instance().getCostUnit(_self, BWAPI::UnitTypes::Zerg_Ultralisk);
	//score -= costUltra;
	score = std::max(0, score);


	// Activate guardians when score is above techcost
	int techcost = 0;

	int nLairsAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	if (nLairsAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Lair).mineralPrice() + (BWAPI::UnitTypes::Zerg_Lair).gasPrice();
	}

	int nHiveAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	if (nHiveAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Hive).mineralPrice() + (BWAPI::UnitTypes::Zerg_Hive).gasPrice();
	}

	int nSpireAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) > 0;
	if (nSpireAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Spire).mineralPrice() + (BWAPI::UnitTypes::Zerg_Spire).gasPrice();
	}

	int nGreaterSpireAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	if (nGreaterSpireAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Greater_Spire).mineralPrice() + (BWAPI::UnitTypes::Zerg_Greater_Spire).gasPrice();
	}

	score -= techcost;
	score = std::max(0, score);


	// Bias
	
	// Hysteresis
	if (_techTarget == "Guardians") { score += 300; };


	// Island hack
	_hasIslandBases = MapTools::Instance().hasIslandBases();
	if (_hasIslandBases)
	{
		score += 900;
	}

	techScores[int(TechUnit::Guardians)] = score;
	_guardianScore = score;

	return score;
}

// Guardian score, cost / supply factor = 25
int StrategyManager::getDevourerScore()
{
	if (Config::Strategy::StrategyName == "5Pool Zergling Hell") return 0;
	int score = 0;

	// vs Protoss
	if (_enemyRace == BWAPI::Races::Protoss)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding() &&
				ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
			{
				// Flying
				if (ui.type.isFlyer())
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Flying can not target air
				if (!UnitUtil::TypeCanAttackAir(ui.type) && ui.type.isFlyer())
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}
		}
	}

	// vs Terran
	if (_enemyRace == BWAPI::Races::Terran)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{
				// Flying
				if (ui.type.isFlyer())
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Flying can not target air
				if (!UnitUtil::TypeCanAttackAir(ui.type) && ui.type.isFlyer())
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}
		}
	}

	// vs Zerg
	if (_enemyRace == BWAPI::Races::Zerg)
	{
		for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Mobile combat units
			if (!ui.type.isWorker() && !ui.type.isBuilding())
			{
				// Flying
				if (ui.type.isFlyer())
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}

				// Flying can not target air
				if (!UnitUtil::TypeCanAttackAir(ui.type) && ui.type.isFlyer())
				{
					score += ui.type.mineralPrice() + ui.type.gasPrice();
				}
			}
		}
	}

	//int costUltra = InformationManager::Instance().getCostUnit(_self, BWAPI::UnitTypes::Zerg_Ultralisk);
	//score -= costUltra;
	score = std::max(0, score);


	// Activate guardians when score is above techcost
	int techcost = 0;

	int nLairsAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	if (nLairsAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Lair).mineralPrice() + (BWAPI::UnitTypes::Zerg_Lair).gasPrice();
	}

	int nHiveAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	if (nHiveAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Hive).mineralPrice() + (BWAPI::UnitTypes::Zerg_Hive).gasPrice();
	}

	int nSpireAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) > 0;
	if (nSpireAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Spire).mineralPrice() + (BWAPI::UnitTypes::Zerg_Spire).gasPrice();
	}

	int nGreaterSpireAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	if (nGreaterSpireAll == 0) {
		techcost += (BWAPI::UnitTypes::Zerg_Greater_Spire).mineralPrice() + (BWAPI::UnitTypes::Zerg_Greater_Spire).gasPrice();
	}

	score -= techcost;
	score = std::max(0, score);


	// Bias

	// Hysteresis
	if (_techTarget == "Devourers") { score += 300; };


	techScores[int(TechUnit::Devourers)] = score;
	_devourerScore = score;

	return score;
}

// Choose which mineral unit to create
void StrategyManager::chooseMinUnit()
{
	int lingcore = getLingScore();
	int hydrascore = getHydraScore();

	// Mark which tech units are available for the unit mix.
	// If we have the tech for it, it can be in the unit mix.
	std::array<bool, int(TechUnit::Size)> available;
	setAvailableTechUnits(available);

	_minUnit = "None";
	if (available[int(TechUnit::Hydralisks)] && _hydraScore > _lingScore) _minUnit = "Hydras";
	else if (available[int(TechUnit::Zerglings)]) _minUnit = "Lings";

	// min unit and gas unit = Hydralisks
	if (_minUnit == "Hydras" && _gasUnit == "Hydras") { _minUnit = "Lings"; }
}

// Choose which gas unit to create
void StrategyManager::chooseGasUnit()
{
	int mutascore = getMutaScore();
	int hydrascore = getHydraScore();
	int lurkerscore = getLurkerScore();
	int ultrascore = getUltraScore();
	int guardianscore = getGuardianScore();
	int devourerScore = getDevourerScore();

	// Mark which tech units are available for the unit mix.
	// If we have the tech for it, it can be in the unit mix.
	std::array<bool, int(TechUnit::Size)> available;
	setAvailableTechUnits(available);

	// Find the best available unit to be the main unit of the mix.
	// Special case: Zerglings are not a gas units.
	// Other units are excluded above by calling them unavailable.
	TechUnit bestUnit = TechUnit::None;
	int techScore = -99999;
	for (int i = int(TechUnit::None); i < int(TechUnit::Size); ++i)
	{
		if (available[i] && techScores[i] > techScore && TechUnit(i) != TechUnit::Zerglings)
		{
			bestUnit = TechUnit(i);
			_gasUnit = techTargetToString(bestUnit);
			techScore = techScores[i];
		}
	}
}

// Choose the next tech to aim for
void StrategyManager::chooseTechTarget()
{
	bool hasLurker = _self->hasResearched(BWAPI::TechTypes::Lurker_Aspect);
	bool hasSpire = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Spire) > 0 ||
		UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;

	bool hasLair = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Lair) > 0;
	int nHives = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	bool hasHiveTech = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hive) > 0;
	bool hasLairTech = hasLair || nHives > 0;

	// Find our current tech tier.
	int theTier = 1;           // we can assume a spawning pool
	if (hasHiveTech)
	{
		theTier = 3;
	}
	else if (hasLairTech)
	{
		theTier = 2;
	}

	// 1a: Mark which tech units are available as tech targets.
	// If we already have it, it's not a target.
	std::array<bool, int(TechUnit::Size)> targetTaken;
	setAvailableTechUnits(targetTaken);

	// 1b: Find the score of the best taken tech unit up to our current tier,
	// considering only positive scores. We never want to take a zero or negative score.
	// Do this before adding fictional taken techs.
	// Skip over the potential complication of a lost lair or hive: We may in fact have tech
	// that is beyond our current tech level because we have been set back.
	int maxTechScore = 0;
	for (int i = int(TechUnit::None); i < int(TechUnit::Size); ++i)
	{
		if (targetTaken[i] && techScores[i] > maxTechScore && techTier(TechUnit(i)) <= theTier)
		{
			maxTechScore = techScores[i];
		}
	}

	// 1c: If we don't have either lair tech yet, and they're not both useless,
	// then don't jump ahead to hive tech. Fictionally call the hive tech units "taken".
	// A tech is useless if it's worth 0 or less, or if it's worth less than the best current tech.
	// (The best current tech might have negative value, though it's rare.)
	if (!hasSpire && !hasLurker &&
		(techScores[int(TechUnit::Mutalisks)] > 0 || techScores[int(TechUnit::Lurkers)] > 0) &&
		(techScores[int(TechUnit::Mutalisks)] >= maxTechScore || techScores[int(TechUnit::Lurkers)] >= maxTechScore))
	{
		targetTaken[int(TechUnit::Ultralisks)] = true;
		targetTaken[int(TechUnit::Guardians)] = true;
		targetTaken[int(TechUnit::Devourers)] = true;
	}

	// 1d: On an island map, go all air until nydus canals are established.
	_hasIslandBases = MapTools::Instance().hasIslandBases();
	if (_hasIslandBases)
	{
		targetTaken[int(TechUnit::Hydralisks)] = true;
		targetTaken[int(TechUnit::Lurkers)] = true;
		targetTaken[int(TechUnit::Ultralisks)] = true;
	}

	// Default. Value at the start of the game and after all tech is taken.
	_techTarget = "None";

	// Choose the tech target, an untaken tech.
	// 2. If a tech at the current tier or below beats the best taken tech so far, take it.
	// That is, stay at the same tier or drop down if we can do better.
	// If we're already at hive tech, no need for this step. Keep going.
	if (theTier != 3)
	{
		int techScore = maxTechScore;    // accept only a tech which exceeds this value
		for (int i = int(TechUnit::None); i < int(TechUnit::Size); ++i)
		{
			if (!targetTaken[i] && techScores[i] > techScore && techTier(TechUnit(i)) <= theTier)
			{
				_techTarget = techTargetToString(TechUnit(i));
				techScore = techScores[i];
			}
		}
		if (_techTarget != "None")
		{
			return;
		}
	}

	// 3. Otherwise choose a target at any tier. Just pick the highest score.
	// If we should not skip from tier 1 to hive, that has already been coded into targetTaken[].
	int techScore = maxTechScore;    // accept only a tech which exceeds this value
	for (int i = int(TechUnit::None); i < int(TechUnit::Size); ++i)
	{
		if (!targetTaken[i] && techScores[i] > techScore)
		{
			_techTarget = techTargetToString(TechUnit(i));
			techScore = techScores[i];
		}
	}
}

// A tech unit is available for selection in the unit mix if we have the tech for it.
// That's what this routine figures out.
// It is available for selection as a tech target if we do NOT have the tech for it.
void StrategyManager::setAvailableTechUnits(std::array<bool, int(TechUnit::Size)> & available)
{
	const bool ZvZ = _enemyRace == BWAPI::Races::Zerg;
	int gas = std::max(0, _self->gas() - BuildingManager::Instance().getReservedGas());
	int nGas = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Extractor);
	bool hasPool = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0;
	bool hasDen = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0;
	bool hasLurker = _self->hasResearched(BWAPI::TechTypes::Lurker_Aspect);
	bool hasSpire = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Spire) > 0 ||
		UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	bool hasGreaterSpire = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	bool hasUltra = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) > 0;

	available[int(TechUnit::None)] = false;       // avoid doing nothing if at all possible

	// Tier 1
	available[int(TechUnit::Zerglings)] = hasPool;
	available[int(TechUnit::Hydralisks)] = hasDen && nGas >= 1;

	// Tier 2: Lair tech
	available[int(TechUnit::Lurkers)] = hasLurker && nGas >= 1;
	available[int(TechUnit::Mutalisks)] = hasSpire && nGas >= 1;
	available[int(TechUnit::Scourge)] = hasSpire && nGas >= 1;

	// Tier 3: Hive tech
	available[int(TechUnit::Ultralisks)] = hasUltra && nGas >= 2;
	available[int(TechUnit::Guardians)] = hasGreaterSpire && nGas >= 2;
	available[int(TechUnit::Devourers)] = hasGreaterSpire && nGas >= 2;
}

int StrategyManager::techTier(TechUnit techUnit) const
{
	if (techUnit == TechUnit::Zerglings || techUnit == TechUnit::Hydralisks)
	{
		return 1;
	}

	if (techUnit == TechUnit::Lurkers || techUnit == TechUnit::Mutalisks || techUnit == TechUnit::Scourge)
	{
		// Lair tech
		return 2;
	}

	if (techUnit == TechUnit::Ultralisks || techUnit == TechUnit::Guardians || techUnit == TechUnit::Devourers)
	{
		// Hive tech
		return 3;
	}

	return 0;
}

std::string StrategyManager::techTargetToString(TechUnit target)
{
	if (target == TechUnit::Zerglings) return "Zerglings";
	if (target == TechUnit::Hydralisks) return "Hydras";
	if (target == TechUnit::Lurkers) return "Lurkers";
	if (target == TechUnit::Mutalisks) return "Mutas";
	if (target == TechUnit::Scourge) return "Scourge";
	if (target == TechUnit::Ultralisks) return "Ultras";
	if (target == TechUnit::Guardians) return "Guardians";
	if (target == TechUnit::Devourers) return "Devourers";
	return "None";
}

TechUnit StrategyManager::techTargetToTextUnit(std::string target)
{
	if (target == "Zerglings") return TechUnit::Zerglings;
	if (target == "Hydras") return TechUnit::Hydralisks;
	if (target == "Lurkers") return TechUnit::Lurkers;
	if (target == "Mutas") return TechUnit::Mutalisks;
	if (target == "Scourge") return TechUnit::Scourge;
	if (target == "Ultras") return TechUnit::Ultralisks;
	if (target == "Guardians") return TechUnit::Guardians;
	if (target == "Devourers") return TechUnit::Devourers;
	return TechUnit::None;
}

// Return true if the building is in the building queue with any status.
bool StrategyManager::isBeingBuilt(BWAPI::UnitType unitType)
{
	UAB_ASSERT(unitType.isBuilding(), "not a building");
	return BuildingManager::Instance().isBeingBuilt(unitType);
}

int StrategyManager::maxWorkers()
{
	int droneMax;
	droneMax = std::max(9, std::min(WorkerManager::Instance().getMaxWorkers(), absoluteMaxDrones));

	return droneMax;
}

int StrategyManager::minWorkers()
{
	// number of unfinished hatches including lair and hive
	int nLairsAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	int nHivesAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	int nHatchesAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
		+ nLairsAll + nHivesAll;

	// minimum drones to mine minerals at all
	int nGas = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Extractor);
	int droneAbsMinimum = (3 * nGas) + 1;

	// McRave Notes:
	// Lings: 3 drones per hatch
	// Hydra: 5.5 drones per hatch - 0.5 gas per hatch
	// Muta: 8 drones per hatch - 1 gas per hatch
	double dronePerHatch = 3;
	if (_gasUnit == "Hydras") { dronePerHatch = 5.5; }
	if (_gasUnit == "Lurkers") { dronePerHatch = 8; }
	if (_gasUnit == "Mutas") { dronePerHatch = 8; }
	if (_gasUnit == "Ultras") { dronePerHatch = 10; }
	if (_gasUnit == "Guardians") { dronePerHatch = 10; }
	if (_gasUnit == "Devourers") { dronePerHatch = 8; }
	int droneMinimum = std::max(droneAbsMinimum, int(nHatchesAll * dronePerHatch));

	return droneMinimum;
}

double StrategyManager::droneEco()
{
	int nDrones = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Drone);
	int nLings = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Zergling);
	int nHydras = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk);
	int nLurkers = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lurker);
	int nMutas = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Mutalisk);
	int nScourge = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Scourge);
	int nUltras = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk);
	int nGuardians = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Guardian);
	int nDevourers = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Devourer);
	int nUnitsTotal = nDrones + nLings + nHydras + nLurkers + nScourge + nMutas + nUltras + nGuardians + nDevourers;

	// global drone eco
	double droneEco = double(nDrones + 1) / double(nUnitsTotal + 1);

	return droneEco;
}

// Do we have "enough" army that we can safely focus on making drones?
bool StrategyManager::enoughArmy() const
{
	int ourForce = InformationManager::Instance().getPower(_self);
	int enemyForce = InformationManager::Instance().getPower(_enemy);

	if (enemyForce == 0)
	{
		return true;
	}

	double ratio = double(ourForce + 1) / double(enemyForce + 1);

	if (_enemyRace == BWAPI::Races::Zerg)
	{
		return ratio >= 1.00;
	}
	if (_enemyRace == BWAPI::Races::Protoss)
	{
		return ratio >= 0.80;
	}

	// Other races.
	return ratio >= 0.75;
}

void StrategyManager::printInfoOnStart()
{
	if (Config::BotInfo::PrintInfoOnStart && !Config::Strategy::UseHumanSpecificStrategy)
	{
		BWAPI::Broodwar->sendText("Strategy: games = %d  winrate = %.3lf",
			_strategies[Config::Strategy::StrategyName]._games, _strategies[Config::Strategy::StrategyName]._winRate);
	}

	Log().Get() << "Strategy: " << Config::Strategy::StrategyName 
		<< "  games: " << _strategies[Config::Strategy::StrategyName]._games
		<< "  winrate: " << _strategies[Config::Strategy::StrategyName]._winRate;
}

void StrategyManager::drawStrategyInformation(int x, int y)
{
	if (!Config::Debug::DrawStrategyInfo)
	{
		return;
	}

	std::string specific;
	if (Config::Strategy::FoundMapSpecificStrategy) specific = " (map)";
	if (Config::Strategy::FoundEnemySpecificStrategy) specific = " (enemy)";
	if (Config::Strategy::FoundHumanSpecificStrategy) specific = " (human)";

	BWAPI::Broodwar->drawTextScreen(x, y, "\x04Strategy:");
	BWAPI::Broodwar->drawTextScreen(x + 50, y, "\x03%s%s  %s%s  %s%s  %s%s  %s%d  %s%d  %s%d  %s%d  %s%d  %s%d  %s%d",
		Config::Strategy::StrategyName.c_str(), specific.c_str(),
		"T: ", _techTarget.c_str(),
		"MU: ", _minUnit.c_str(),
		"GU: ", _gasUnit.c_str(),
		"Z: ", _lingScore,
		"H: ", _hydraScore,
		"M: ", _mutaScore,
		"L: ", _lurkerScore,
		"U: ", _ultraScore,
		"G: ", _guardianScore,
		"D: ", _devourerScore);
	
	y += 12;
	BWAPI::Broodwar->drawTextScreen(x, y, "\x04Opp.Plan:");
	BWAPI::Broodwar->drawTextScreen(x + 50, y, "\x03%s  %s", 
		getExpectedEnemyPlanString().c_str(), getRecognizedEnemyPlanString().c_str());
}

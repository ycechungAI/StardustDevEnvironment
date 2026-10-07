#include "StrategyBossZerg.h"
#include "Strategymanager.h"
#include "ProductionManager.h"
#include "UnitUtil.h"
#include "WorkerManager.h"

using namespace UAlbertaBot;

StrategyBossZerg::StrategyBossZerg()
	: _self(BWAPI::Broodwar->self())
	, _enemy(BWAPI::Broodwar->enemy())
	, _selfRace(BWAPI::Broodwar->self()->getRace())
	, _enemyRace(BWAPI::Broodwar->enemy()->getRace())
	, _existingSupply(-1)
	, _pendingSupply(-1)
	, _lastUpdateFrame(-1)
	, _emergencyGroundDefense(false)
	, _emergencyStartFrame(-1)
	, _ourForceGround(0)
	, _enemyForceGround(0)
	, _enemyForceGroundIncoming(0)
	, _nScourgeNeeded(0)
	, _queueMinerals(0)
	, _queueGas(0)
	, _nDrones(0)
	, _nGasDrones(0)
	, _nMineralDrones(0)
	, _nMineralPatches(0)
	, _nGas(0)
	, _nHatches(0)
	, _nMaxDrones(0)
	, _nMinDrones(0)
	, _nLarvas(0)
	, _giveUp(false)
{
}

StrategyBossZerg & StrategyBossZerg::Instance()
{
	static StrategyBossZerg instance;
	return instance;
}

// Existing and pending supply.
void StrategyBossZerg::figureSupply()
{
	int existingSupply = 0;
	int pendingSupply = 0;
	int supplyUsed = 0;

	for (auto & unit : _self->getUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
		{
			if (unit->getOrder() == BWAPI::Orders::ZergBirth)
			{
				// Overlord is just hatching and doesn't provide supply yet.
				pendingSupply += 16;
			}
			else
			{
				existingSupply += 16;
			}
		}
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg)
		{
			if (unit->getBuildType() == BWAPI::UnitTypes::Zerg_Overlord)
			{
				pendingSupply += 16;
			}
			else if (unit->getBuildType().isTwoUnitsInOneEgg())
			{
				supplyUsed += 2 * unit->getBuildType().supplyRequired();
			}
			else
			{
				supplyUsed += unit->getBuildType().supplyRequired();
			}
		}
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery && !unit->isCompleted())
		{
			// Count an unfinished hatchery as pending supply only if it is close to finishing.
			// NOTE Hatchery build time = 1800; overlord build time = 600.
			// Hatcheries take too long to build to be worth counting otherwise.
			if (unit->getRemainingBuildTime() < 300)
			{
				pendingSupply += 2;
			}
		}
		else if (unit->getType().isResourceDepot())
		{
			// Only counts complete hatcheries because incomplete hatcheries are checked above.
			// Also counts lairs and hives whether complete or not, of course.
			existingSupply += 2;
		}
		else
		{
			supplyUsed += unit->getType().supplyRequired();
		}
	}

	_existingSupply = std::min(existingSupply, absoluteMaxSupply);
	_pendingSupply = pendingSupply;
	_supplyUsed = supplyUsed;

	// Note: _existingSupply is less than _self->supplyTotal() when an overlord
	// has just died. In other words, it recognizes the lost overlord sooner,
	// which is better for planning.
}


// Solve urgent production issues. Called once per frame.
// If we're in trouble, clear the production queue and/or add emergency actions.
// Or if we just need overlords, make them.
// This routine is allowed to take direct actions or cancel stuff to get or preserve resources.
void StrategyBossZerg::handleUrgentProductionIssues(BuildOrderQueue & queue)
{
	// Profile debug
	//PROFILE_FUNCTION();

	if (_lastUpdateFrame == BWAPI::Broodwar->getFrameCount())
	{
		// No need to update more than once per frame.
		return;
	}
	_lastUpdateFrame = BWAPI::Broodwar->getFrameCount();

	//Update unit scores
	StrategyManager::Instance().chooseGasUnit();

	
	int nSupplyUsed = BWAPI::Broodwar->self()->supplyUsed();
	int nSupplyTotal = BWAPI::Broodwar->self()->supplyTotal();
	int supplyLeft = nSupplyTotal - nSupplyUsed;

	int minerals = std::max(0, _self->minerals() - BuildingManager::Instance().getReservedMinerals());
	int gas = std::max(0, _self->gas() - BuildingManager::Instance().getReservedGas());
	int nLarvas = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Larva);
	int nMinPatches = InformationManager::Instance().getMyNumMineralPatches();

	// Building stuff, including uncompleted buildings.
	int nLairs = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	int nHives = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	int nHatches = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
		+ nLairs + nHives;
	int nHatchesComp = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
		+ nLairs + nHives;
	int nHatchesOnly = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery);
	int nGas = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Extractor);
	int nFreeGas = InformationManager::Instance().getMyNumGeysers() -
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Extractor);
	int nSpores = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spore_Colony);
	int nSunken = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Sunken_Colony);
	int nCreep = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Creep_Colony);
	int nCreepComp = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Creep_Colony);

	// Unit stuff, including uncompleted units.
	int nDrones = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Drone);
	int nGasDrones = WorkerManager::Instance().getNumGasWorkers();
	int nMinDrones = WorkerManager::Instance().getNumMineralWorkers();
	int nLings = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Zergling);
	int nHydras = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk);
	int nMutas = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Mutalisk);
	int nGuardians = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Guardian);

	// Tech stuff. It has to be completed for the tech to be available.
	int nEvo = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber);
	int nEvoAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber);
	bool hasPool = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0;
	bool hasDen = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0;
	bool hasUltra = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) > 0;
	bool hasSpire = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Spire) > 0 ||
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	bool hasGreaterSpire = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	bool hasQueensNest = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Queens_Nest) > 0;

	// hasLair means "can research stuff in the lair", like overlord speed.
	// hasLairTech means "can do stuff that needs lair", like research lurker aspect.
	// NOTE The two are different in game, but even more different in the bot because
	//      of a BWAPI 4.1.2 bug: You can't do lair research in a hive.
	//      This code reflects the bug so we can work around it as much as possible.
	bool hasHiveTech = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hive) > 0;
	bool hasLair = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Lair) > 0;
	bool hasLairTech = hasLair || nHives > 0;
	
	bool outOfBook = ProductionManager::Instance().isOutOfBook();
	int nEnemyBases = InformationManager::Instance().getNumBases(_enemy);
	int nBases = InformationManager::Instance().getNumBases(_self);
	int nFreeBases = InformationManager::Instance().getNumFreeLandBases();
	//bool expoAvailable = MapTools::Instance().getNextExpansion(false, false) != BWAPI::TilePositions::None;
	int nMineralPatches = InformationManager::Instance().getMyNumMineralPatches();
	int droneMax = StrategyManager::Instance().maxWorkers();
	int droneMinimum = StrategyManager::Instance().minWorkers();
	_nDrones = nDrones;
	_nGasDrones = nGasDrones;
	_nMineralDrones = nMinDrones;
	_nMineralPatches = nMineralPatches;
	_nGas = nGas;
	_nHatches = nHatches;
	_nMaxDrones = droneMax;
	_nMinDrones = droneMinimum;
	_nLarvas = nLarvas;
	
	// Get our anti-ground strength and enemy ground strength
	if (nHatches > 0) {
		BWAPI::Unit ourNatural = nullptr;
		BWAPI::Unit ourMain = nullptr;

		ourNatural = InformationManager::Instance().getBaseDepot(InformationManager::Instance().getMyNaturalLocation());
		ourMain = InformationManager::Instance().getBaseDepot(InformationManager::Instance().getMyMainBaseLocation());

		if (UnitUtil::IsValidUnit(ourNatural) || UnitUtil::IsValidUnit(ourMain))
		{
			_ourForceGround = InformationManager::Instance().getMyPowerGroundWeapon(true);
			_enemyForceGround = InformationManager::Instance().getEnemyPowerGroundUnits();
			_enemyForceGroundIncoming = InformationManager::Instance().getEnemyPowerGroundUnitsIncoming();
		}
	}

	if (_emergencyGroundDefense &&
		_enemyForceGroundIncoming < _ourForceGround &&
		_lastUpdateFrame >= _emergencyStartFrame + (10 * 24))
	{
		// Danger has been past for 10 seconds. Call the end of the emergency.
		_emergencyGroundDefense = false;
	}

	//if (nSupplyUsed > 180)
	//{
		//ProductionManager::Instance().setAggression(true);
	//}

	// START SERIOUS EMERGENCIES

	
	// If we do not have enough gas for the next item and we will not get this soon, remove the item.
	if (!queue.isEmpty())
	{
		if (nGas > 0 && gas < queue.getHighestPriorityItem().macroAct.gasPrice() && nDrones < 4)
		{
			queue.removeHighestPriorityItem();
			return;
		}
	}

	// We don't need overlords now. Drop any that are queued next and continue.
	if ((nDrones < 8) && _self->supplyTotal() - _self->supplyUsed() > 16)
	{
		while (!queue.isEmpty() &&
			queue.getHighestPriorityItem().macroAct.isUnit() &&
			queue.getHighestPriorityItem().macroAct.getUnitType() == BWAPI::UnitTypes::Zerg_Overlord)
		{
			queue.removeHighestPriorityItem();
		}
		// TODO if we're really badly off, also cancel overlords in the egg
	}

	// Find the next thing in the queue, but only if it is a unit.
	BWAPI::UnitType nextInQueue = BWAPI::UnitTypes::None;
	if (!queue.isEmpty())
	{
		const MacroAct & act = queue.getHighestPriorityItem().macroAct;
		if (act.isUnit())
		{
			nextInQueue = act.getUnitType();
		}
	}

	// There are no drones.
	// NOTE maxDrones is never zero.
	if (nDrones == 0 && BWAPI::Broodwar->getFrameCount() % 400)
	{
		WorkerManager::Instance().setCollectGas(false);
		BuildingManager::Instance().cancelQueuedBuildings();
		if (nHatches == 0 &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hatchery) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery))
		{
			Log().Get() << "No drones, no hatcheries.";
			// No hatcheries either. Queue drones for a hatchery and mining.
			ProductionManager::Instance().goOutOfBook();
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery));
			//cancelStuff(400);
		}
		else
		{
			if (!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Drone) && 
				numInEgg(BWAPI::UnitTypes::Zerg_Drone) == 0)
			{
				Log().Get() << "No drones.";
				// Queue one drone to mine minerals.
				ProductionManager::Instance().goOutOfBook();
				queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
				//cancelStuff(50);
			}
			BuildingManager::Instance().cancelBuildingType(BWAPI::UnitTypes::Zerg_Hatchery);
		}
		return;
	}

	// There are no hatcheries.
	if (nHatches == 0 &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery))
	{
		ProductionManager::Instance().goOutOfBook();
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery));
		if (!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Drone) &&
			numInEgg(BWAPI::UnitTypes::Zerg_Drone) == 0)
		{
			//ScoutManager::Instance().releaseWorkerScout();
			queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			//cancelStuff(350);
		}
		else {
			//cancelStuff(300);
		}
		return;
	}

	// There are < 3 drones. Make up to 3.
	// Making more than 3 breaks 4 pool openings.
	if (!_emergencyGroundDefense && 
		nDrones < 3 && nextInQueue != BWAPI::UnitTypes::Zerg_Drone)
	{
		//ScoutManager::Instance().releaseWorkerScout();
		queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
		if (nDrones < 2)
		{
			queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
		}
		// Don't cancel stuff. A drone should be mining, it's not that big an emergency.
		return;
	}

	// There are no drones on minerals. Turn off gas collection.
	// TODO we may want to switch between gas and minerals to make tech units
	// TODO more efficient test in WorkerMan
	if (BWAPI::Broodwar->getFrameCount() % 400 &&           // give it time!
		nMineralPatches > 0 &&
		WorkerManager::Instance().isCollectingGas() &&
		WorkerManager::Instance().getNumMineralWorkers() == 0 &&
		WorkerManager::Instance().getNumReturnCargoWorkers() == 0 &&
		WorkerManager::Instance().getNumCombatWorkers() == 0 &&
		WorkerManager::Instance().getNumIdleWorkers() == 0)
	{
		// Leave the queue in place.
		WorkerManager::Instance().setCollectGas(false);
		return;
	}

	// END SERIOUS EMERGENCIES


	// START NEXT ITEM IS USELESS
	
	// The next item in the queue is useless and can be dropped.
	// Top goal: Do not freeze the production queue by asking the impossible.
	// But also try to reduce wasted production.
	// NOTE Useless stuff is not always removed before it is built.
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Zergling)
	{
		// We lost the tech.
		if (UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool))
		{
			queue.clearAll();
			return;
		}
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Hydralisk)
	{
		// We lost the tech.
		if (UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den))
		{
			queue.clearAll();
			return;
		}

	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Mutalisk || nextInQueue == BWAPI::UnitTypes::Zerg_Scourge)
	{
		// We lost the tech.
		if (UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire) &&
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Greater_Spire))
		{
			queue.clearAll();
			return;
		}

	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Ultralisk)
	{
		// We lost the tech.
		if (UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern))
		{
			queue.clearAll();
			return;
		}
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Lurker)
	{
		if (nHydras == 0 ||
			!_self->hasResearched(BWAPI::TechTypes::Lurker_Aspect) &&
			!_self->isResearching(BWAPI::TechTypes::Lurker_Aspect))
		{
			queue.clearAll();
			return;
		}
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Guardian)
	{
		if (nMutas == 0 ||
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Greater_Spire))
		{
			queue.clearAll();
			return;
		}
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Devourer)
	{
		if (nMutas == 0 ||
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Greater_Spire))
		{
			queue.clearAll();
			return;
		}
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Hatchery)
	{
		// We're planning a hatchery but no longer have the drones to support it.
		// 3 drones/hatchery is the minimum: It can support ling production.
		// Also, it may still be OK if we have lots of minerals to spend.
		if (nDrones < 3 * nHatches &&
			minerals < 50 + 300 * nHatchesComp &&
			nHatchesComp > 0)
		{
			queue.clearAll();
			return;
		}
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Lair)
	{
		if (UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 && 
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool) ||
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery) == 0 && 
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery))
		{
			queue.clearAll();
			return;
		}
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Hive)
	{
		if (UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Queens_Nest) == 0 && 
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Queens_Nest) ||
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Lair))
		{
			queue.clearAll();
			return;
		}
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Spire)
	{
		if (UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Lair) &&
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hive))
		{
			queue.clearAll();
			return;
		}
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Greater_Spire)
	{
		if (UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire) &&
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive) == 0 &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hive))
		{
			queue.clearAll();
			return;
		}
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Hydralisk_Den)
	{
		if (UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 && 
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool))
		{
			queue.clearAll();
			return;
		}
	}

	// A sunken or spore is in the queue, but the corresponding creep colony died.
	if ((nextInQueue == BWAPI::UnitTypes::Zerg_Sunken_Colony || nextInQueue == BWAPI::UnitTypes::Zerg_Spore_Colony) &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Creep_Colony) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// A sunken is next in the queue, but we have lost our spawning pool.
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Sunken_Colony &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// A spore is next in the queue, but we have lost our evolution chamber.
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Spore_Colony &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Evolution_Chamber))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Metabolic Boost is next in the queue, but we have lost our spawning pool.
	if ((nextInQueue == BWAPI::UpgradeTypes::Metabolic_Boost &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool)) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Metabolic_Boost) && nextInQueue == BWAPI::UpgradeTypes::Metabolic_Boost))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Adrenal Glands is next in the queue, but we have lost our spawning pool.
	if ((nextInQueue == BWAPI::UpgradeTypes::Adrenal_Glands &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool)) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Adrenal_Glands) && nextInQueue == BWAPI::UpgradeTypes::Adrenal_Glands))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Muscular Augments is next in the queue, but we have lost our hydralisk den.
	if ((nextInQueue == BWAPI::UpgradeTypes::Muscular_Augments &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den)) ||
		(_self->isResearching(BWAPI::TechTypes::Lurker_Aspect) && nextInQueue == BWAPI::UpgradeTypes::Muscular_Augments) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Grooved_Spines) && nextInQueue == BWAPI::UpgradeTypes::Muscular_Augments) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Muscular_Augments) && nextInQueue == BWAPI::UpgradeTypes::Muscular_Augments))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Grooved Spines is next in the queue, but we have lost our hydralisk den.
	if ((nextInQueue == BWAPI::UpgradeTypes::Grooved_Spines &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den)) ||
		(_self->isResearching(BWAPI::TechTypes::Lurker_Aspect) && nextInQueue == BWAPI::UpgradeTypes::Grooved_Spines) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Grooved_Spines) && nextInQueue == BWAPI::UpgradeTypes::Grooved_Spines) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Muscular_Augments) && nextInQueue == BWAPI::UpgradeTypes::Grooved_Spines))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Lurker Aspect is next in the queue, but we have lost our hydralisk den.
	if ((nextInQueue == BWAPI::TechTypes::Lurker_Aspect &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den) &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Lair) &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hive)) ||
		(_self->isResearching(BWAPI::TechTypes::Lurker_Aspect) && nextInQueue == BWAPI::TechTypes::Lurker_Aspect) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Grooved_Spines) && nextInQueue == BWAPI::TechTypes::Lurker_Aspect) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Muscular_Augments) && nextInQueue == BWAPI::TechTypes::Lurker_Aspect))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Flyer Carapace is next in the queue, but we have lost our spire.
	if ((nextInQueue == BWAPI::UpgradeTypes::Zerg_Flyer_Carapace &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) == 0 && UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire) && !BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Greater_Spire)) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Flyer_Carapace) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Flyer_Carapace) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Flyer_Attacks) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Flyer_Carapace))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Flyer Attacks is next in the queue, but we have lost our spire.
	if ((nextInQueue == BWAPI::UpgradeTypes::Zerg_Flyer_Attacks &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) == 0 && UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire) && !BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Greater_Spire)) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Flyer_Carapace) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Flyer_Attacks) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Flyer_Attacks) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Flyer_Attacks))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Carapace is next in the queue, but we have lost our evolution chamber.
	if ((nextInQueue == BWAPI::UpgradeTypes::Zerg_Carapace &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Evolution_Chamber)) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Carapace) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Carapace) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Missile_Attacks) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Carapace) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Melee_Attacks) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Carapace))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Missile Attacks is next in the queue, but we have lost our evolution chamber.
	if ((nextInQueue == BWAPI::UpgradeTypes::Zerg_Missile_Attacks &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Evolution_Chamber)) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Carapace) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Missile_Attacks) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Missile_Attacks) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Missile_Attacks) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Melee_Attacks) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Missile_Attacks))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Melee Attacks is next in the queue, but we have lost our evolution chamber.
	if ((nextInQueue == BWAPI::UpgradeTypes::Zerg_Melee_Attacks &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Evolution_Chamber)) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Carapace) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Melee_Attacks) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Missile_Attacks) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Melee_Attacks) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Melee_Attacks) && nextInQueue == BWAPI::UpgradeTypes::Zerg_Melee_Attacks))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Pneumatized Carapace is next in the queue, but we have lost our lair or it is morphed to hive.
	if ((nextInQueue == BWAPI::UpgradeTypes::Pneumatized_Carapace &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Lair)) ||
		(nextInQueue == BWAPI::UpgradeTypes::Antennae &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive) > 0) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) && nextInQueue == BWAPI::UpgradeTypes::Pneumatized_Carapace))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// Overlord sight range is next in the queue, but we have lost our lair or it is morphed to hive.
	if ((nextInQueue == BWAPI::UpgradeTypes::Antennae &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair) == 0 && 
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Lair)) ||
		(nextInQueue == BWAPI::UpgradeTypes::Antennae &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive) > 0) ||
		(_self->isUpgrading(BWAPI::UpgradeTypes::Antennae) && nextInQueue == BWAPI::UpgradeTypes::Antennae))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// END NEXT ITEM IS USELESS


	// START STRATEGY EMERGENCIES

	// Other emergencies are less urgent. Don't check every frame.
	if (BWAPI::Broodwar->getFrameCount() % 24 != 0) // 24 = every 1 seconds
	{
		return;
	}

	if (outOfBook && hasPool && nDrones >= 9)
	{
		WorkerManager::Instance().handleBlockingMineralsWorkers();
	}

	// We have seen incoming enemy workers.
	// Make sunken for protection.
	if (!outOfBook && !MapTools::Instance().hasIslandBases() && 
		(hasPool || BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool)))
	{

		if (!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Creep_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony))
		{
			BWAPI::Unit ourNatural = nullptr;
			ourNatural = InformationManager::Instance().getBaseDepot(InformationManager::Instance().getMyNaturalLocation());
			int enemyForceWorkers = InformationManager::Instance().getNumWorkersIncoming();

			if (UnitUtil::IsValidUnit(ourNatural))
			{
				const UnitInfo & ourBaseUnitInfo = InformationManager::Instance().getUnit(_self, ourNatural);

				if (ourBaseUnitInfo.completedFrame + 1000 < _lastUpdateFrame)
				{
					int nSunkenNatural = InformationManager::Instance().getNumSunkens(InformationManager::Instance().getMyNaturalLocation());
					int nCreepNatural = InformationManager::Instance().getNumCreep(InformationManager::Instance().getMyNaturalLocation());

					if (nSunkenNatural == 0 && nCreepNatural == 0 && enemyForceWorkers > 1)
					{
						MacroLocation loc = MacroLocation::Natural;
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Sunken_Colony));
						if (supplyLeft >= 2 && nDrones < droneMax)
						{
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
						}
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Chokepoint);
					}
				}
			}

		}

		if (!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Creep_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony))
		{

			BWAPI::Unit ourMain = nullptr;
			ourMain = InformationManager::Instance().getBaseDepot(InformationManager::Instance().getMyMainBaseLocation());
			int enemyForceWorkers = InformationManager::Instance().getNumWorkersIncoming();

			if (UnitUtil::IsValidUnit(ourMain))
			{
				const UnitInfo & ourBaseUnitInfo = InformationManager::Instance().getUnit(_self, ourMain);

				if (ourBaseUnitInfo.completedFrame + 1000 < _lastUpdateFrame)
				{
					int nSunkenMain = InformationManager::Instance().getNumSunkens(InformationManager::Instance().getMyMainBaseLocation());
					int nCreepMain = InformationManager::Instance().getNumCreep(InformationManager::Instance().getMyMainBaseLocation());

					if (nSunkenMain == 0 && nCreepMain == 0 && enemyForceWorkers > 1)
					{
						MacroLocation loc = MacroLocation::Macro;
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Sunken_Colony));
						if (supplyLeft >= 2 && nDrones < droneMax)
						{
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
						}
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Minerals);
					}
				}
			}
		}
	}

	// We have seen enemy vultures. 
	// Make sunken for protection.
	if (hasPool && nDrones >= 3 && !MapTools::Instance().hasIslandBases())
	{

		if (!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Creep_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony))
		{
			BWAPI::Unit ourNatural = nullptr;
			ourNatural = InformationManager::Instance().getBaseDepot(InformationManager::Instance().getMyNaturalLocation());
			bool enemyHasVultures = InformationManager::Instance().enemyHasVultures();

			if (UnitUtil::IsValidUnit(ourNatural))
			{
				const UnitInfo & ourBaseUnitInfo = InformationManager::Instance().getUnit(_self, ourNatural);

				if (ourBaseUnitInfo.completedFrame + 1000 < _lastUpdateFrame)
				{
					int nSunkenNatural = InformationManager::Instance().getNumSunkens(InformationManager::Instance().getMyNaturalLocation());
					int nCreepNatural = InformationManager::Instance().getNumCreep(InformationManager::Instance().getMyNaturalLocation());

					if (nSunkenNatural == 0 && nCreepNatural == 0 && enemyHasVultures)
					{
						MacroLocation loc = MacroLocation::Natural;
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Sunken_Colony));
						if (supplyLeft >= 2 && nDrones < droneMax)
						{
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
						}
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Chokepoint);
					}
				}
			}
		}

		if (!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Creep_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony))
		{

			BWAPI::Unit ourMain = nullptr;
			ourMain = InformationManager::Instance().getBaseDepot(InformationManager::Instance().getMyMainBaseLocation());
			bool enemyHasVultures = InformationManager::Instance().enemyHasVultures();

			if (UnitUtil::IsValidUnit(ourMain))
			{
				const UnitInfo & ourBaseUnitInfo = InformationManager::Instance().getUnit(_self, ourMain);

				if (ourBaseUnitInfo.completedFrame + 1000 < _lastUpdateFrame)
				{
					int nSunkenMain = InformationManager::Instance().getNumSunkens(InformationManager::Instance().getMyMainBaseLocation());
					int nCreepMain = InformationManager::Instance().getNumCreep(InformationManager::Instance().getMyMainBaseLocation());

					if (nSunkenMain == 0 && nCreepMain == 0 && enemyHasVultures)
					{
						MacroLocation loc = MacroLocation::Macro;
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Sunken_Colony));
						if (supplyLeft >= 2 && nDrones < droneMax)
						{
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
						}
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Minerals);
					}
				}
			}
		}
	}

	// We have seen incoming enemy ground units. Make sunken for protection.
	// TODO: Make zerglings instead / also?
	if (hasPool && nDrones >= 9 && !MapTools::Instance().hasIslandBases())
	{
		if (_enemyForceGroundIncoming > _ourForceGround) {
			_emergencyGroundDefense = true;
			_emergencyStartFrame = _lastUpdateFrame;
		}

		// Make sunkens at the front, one at a time.
		if (!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Creep_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony))
		{
			BWAPI::Unit ourNatural = nullptr;
			BWAPI::Unit ourMain = nullptr;
			const BWEM::Base * ourNaturalLocation = InformationManager::Instance().getMyNaturalLocation();
			const BWEM::Base * ourMainLocation = InformationManager::Instance().getMyMainBaseLocation();
			ourNatural = InformationManager::Instance().getBaseDepot(ourNaturalLocation);
			ourMain = InformationManager::Instance().getBaseDepot(ourMainLocation);

			if (UnitUtil::IsValidUnit(ourNatural))
			{
				const UnitInfo & ourBaseUnitInfo = InformationManager::Instance().getUnit(_self, ourNatural);

				if (ourBaseUnitInfo.completedFrame + 1000 < _lastUpdateFrame)
				{
					int nSunkenNatural = InformationManager::Instance().getNumSunkens(InformationManager::Instance().getMyNaturalLocation());
					int nCreepNatural = InformationManager::Instance().getNumCreep(InformationManager::Instance().getMyNaturalLocation());
					int nEnemeyNatural = InformationManager::Instance().getNumEnemyUnitsVsGround(ourNaturalLocation);

					if (outOfBook && ((_enemyRace == BWAPI::Races::Protoss && nSunkenNatural < 6) ||
						(_enemyRace != BWAPI::Races::Protoss && nSunkenNatural < 4)) &&
						nCreepNatural == 0 && _enemyForceGround > _ourForceGround && nEnemeyNatural == 0)
					{
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Sunken_Colony));
						MacroLocation loc = MacroLocation::Natural;
						if (supplyLeft >= 2 && nDrones < droneMax)
						{
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
						}
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Chokepoint);
					}

					else if (!outOfBook && ((_enemyRace == BWAPI::Races::Protoss && nSunkenNatural < 4) ||
						(_enemyRace != BWAPI::Races::Protoss && nSunkenNatural < 4)) &&
						nCreepNatural == 0 && _enemyForceGround > _ourForceGround && nEnemeyNatural == 0)
					{
						MacroAct top = queue.getHighestPriorityItem().macroAct;
						if (top.isUnit() && top.getUnitType() != BWAPI::Broodwar->self()->getRace().getSupplyProvider())
						{
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Sunken_Colony));
							MacroLocation loc = MacroLocation::Natural;
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Chokepoint);
						}
					}

				}
			}

			else if (UnitUtil::IsValidUnit(ourMain) && !UnitUtil::IsValidUnit(ourNatural))
			{
				const UnitInfo & ourBaseUnitInfo = InformationManager::Instance().getUnit(_self, ourMain);

				if (ourBaseUnitInfo.completedFrame + 1000 < _lastUpdateFrame)
				{
					int nSunkenMain = InformationManager::Instance().getNumSunkens(InformationManager::Instance().getMyMainBaseLocation());
					int nCreepMain = InformationManager::Instance().getNumCreep(InformationManager::Instance().getMyMainBaseLocation());
					int nEnemeyMain = InformationManager::Instance().getNumEnemyUnitsVsGround(ourMainLocation);

					if (outOfBook && ((_enemyRace == BWAPI::Races::Protoss && nSunkenMain < 6) ||
						(_enemyRace != BWAPI::Races::Protoss && nSunkenMain < 4)) &&
						nCreepMain == 0 && _enemyForceGround > _ourForceGround && nEnemeyMain == 0)
					{
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Sunken_Colony));
						MacroLocation loc = MacroLocation::Macro;
						if (supplyLeft >= 2 && nDrones < droneMax)
						{
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
						}
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Chokepoint);
					}

					else if (!outOfBook && ((_enemyRace == BWAPI::Races::Protoss && nSunkenMain < 4) ||
						(_enemyRace != BWAPI::Races::Protoss && nSunkenMain < 4)) &&
						nCreepMain == 0 && _enemyForceGround > _ourForceGround && nEnemeyMain == 0)
					{
						MacroAct top = queue.getHighestPriorityItem().macroAct;
						if (top.isUnit() && top.getUnitType() != BWAPI::Broodwar->self()->getRace().getSupplyProvider())
						{
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Sunken_Colony));
							MacroLocation loc = MacroLocation::Macro;
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Chokepoint);
						}
					}

				}
			}
		}
	}


	// If the enemy has overlord hunters such as corsairs, prepare appropriately.
	if (InformationManager::Instance().enemyHasOverlordHunters())
	{
		if (nEvo == 0 && nDrones >= 9 && hasPool &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Evolution_Chamber) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Evolution_Chamber) &&
			!queue.upgradeInQueue() && !queue.buildingInQueue())
		{
			queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Evolution_Chamber));
		}

		else if (nEvo > 0 && nDrones >= 9)
		{
			BWAPI::Unit ourMain = nullptr;
			ourMain = InformationManager::Instance().getBaseDepot(InformationManager::Instance().getMyMainBaseLocation());

			if (UnitUtil::IsValidUnit(ourMain) && 
				!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
				!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
				!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
				!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
				!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Creep_Colony) &&
				!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony))
			{
				int nSporeMain = InformationManager::Instance().getNumSpores(InformationManager::Instance().getMyMainBaseLocation());
				int nCreepMain = InformationManager::Instance().getNumCreep(InformationManager::Instance().getMyMainBaseLocation());
				int nSporeNatural = InformationManager::Instance().getNumSpores(InformationManager::Instance().getMyNaturalLocation());
				int nCreepNatural = InformationManager::Instance().getNumCreep(InformationManager::Instance().getMyNaturalLocation());

				if (nSporeMain == 0 && nSporeNatural == 0 && nCreepMain == 0)
				{
					MacroLocation loc = MacroLocation::Macro;
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Spore_Colony));
					if (supplyLeft >= 2 && nDrones < droneMax)
					{
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
					}
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Minerals);
				}
				else if (nSporeMain == 1 && nSporeNatural == 1 && nCreepMain == 0)
				{
					MacroLocation loc = MacroLocation::Macro;
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Spore_Colony));
					if (supplyLeft >= 2 && nDrones < droneMax)
					{
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
					}
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Chokepoint);
				}
			}

			BWAPI::Unit ourNatural = nullptr;
			ourNatural = InformationManager::Instance().getBaseDepot(InformationManager::Instance().getMyNaturalLocation());

			if (UnitUtil::IsValidUnit(ourNatural) &&
				!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
				!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
				!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
				!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
				!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Creep_Colony) &&
				!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony))
			{
				int nSporeMain = InformationManager::Instance().getNumSpores(InformationManager::Instance().getMyMainBaseLocation());
				int nCreepMain = InformationManager::Instance().getNumCreep(InformationManager::Instance().getMyMainBaseLocation());
				int nSporeNatural = InformationManager::Instance().getNumSpores(InformationManager::Instance().getMyNaturalLocation());
				int nCreepNatural = InformationManager::Instance().getNumCreep(InformationManager::Instance().getMyNaturalLocation());

				if (nSporeMain == 1 && nSporeNatural == 0 && nCreepNatural == 0)
				{
					MacroLocation loc = MacroLocation::Natural;
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Spore_Colony));
					if (supplyLeft >= 2 && nDrones < droneMax)
					{
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
					}
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Minerals);
				}
				else if (nSporeMain == 2 && nSporeNatural == 1 && nCreepNatural == 0)
				{
					MacroLocation loc = MacroLocation::Natural;
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Spore_Colony));
					if (supplyLeft >= 2 && nDrones < droneMax)
					{
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
					}
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Chokepoint);
				}
			}

		}

		else if (hasLair && nHives == 0 &&
			minerals >= 100 && gas >= 100 &&
			_enemyRace != BWAPI::Races::Zerg &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hive) &&
			_self->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
			!queue.upgradeInQueue() && !queue.buildingInQueue())
		{
			queue.queueAsLowestPriority(MacroAct(BWAPI::UpgradeTypes::Pneumatized_Carapace));
		}
	}


	// Make air defense in expansions
	if (outOfBook && nEvo > 0 && nDrones >= 9 && InformationManager::Instance().enemyHasOverlordHunters())
	{
		if (!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Creep_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony) &&
			!queue.upgradeInQueue() && !queue.buildingInQueue())
		{
			const BWEM::Base * ourBase = InformationManager::Instance().getLeastDefendedBaseAir(_self);

			if (ourBase)
			{
				BWAPI::Unit ourBaseUnit = InformationManager::Instance().getBaseDepot(ourBase);

				if (UnitUtil::IsValidUnit(ourBaseUnit))
				{
					int nSporeBase = InformationManager::Instance().getNumSpores(ourBase);
					int nCreepBase = InformationManager::Instance().getNumCreep(ourBase);

					if (nSporeBase < 3 && nCreepBase == 0 && minerals > 300)
					{
						MacroLocation loc = MacroLocation::LeastDefendedAir;
						if (ourBase == InformationManager::Instance().getMyMainBaseLocation())
						{
							loc = MacroLocation::Macro;
						}
						else if (ourBase == InformationManager::Instance().getMyNaturalLocation())
						{
							loc = MacroLocation::Natural;
						}

						if (nSporeBase == 0)
						{
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Spore_Colony));
							if (supplyLeft >= 2 && nDrones < droneMax)
							{
								queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
							}
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Minerals);
						}
						else if (nSporeBase > 0 && nSporeBase < 3)
						{
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Spore_Colony));
							if (supplyLeft >= 2 && nDrones < droneMax)
							{
								queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
							}
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Chokepoint);
						}
					}
				}
			}
		}
	}


	// Make ground defense in expansions
	if (outOfBook && hasPool && nDrones >= 9)
	{
		if (!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Creep_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony) &&
			!queue.upgradeInQueue() && !queue.buildingInQueue())
		{
			const BWEM::Base * ourBase = InformationManager::Instance().getLeastDefendedBaseGround(_self);

			if (ourBase)
			{
				BWAPI::Unit ourBaseUnit = InformationManager::Instance().getBaseDepot(ourBase);			

				if (UnitUtil::IsValidUnit(ourBaseUnit))
				{
					const UnitInfo & ourBaseUnitInfo = InformationManager::Instance().getUnit(_self, ourBaseUnit);

					if (ourBaseUnitInfo.completedFrame + 1000 < _lastUpdateFrame)
					{

						int nSunkenBase = InformationManager::Instance().getNumSunkens(ourBase);
						int nCreepBase = InformationManager::Instance().getNumCreep(ourBase);
						bool islandbases = MapTools::Instance().hasIslandBases();

						if ((nSunkenBase < 3 && !islandbases || nSunkenBase < 1 && islandbases) && nCreepBase == 0 && minerals > 400)
						{
							MacroLocation loc = MacroLocation::LeastDefendedGround;
							if (ourBase == InformationManager::Instance().getMyMainBaseLocation())
							{
								loc = MacroLocation::Macro;
							}
							else if (ourBase == InformationManager::Instance().getMyNaturalLocation())
							{
								loc = MacroLocation::Natural;
							}

							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Sunken_Colony));
							if (supplyLeft >= 2 && nDrones < droneMax)
							{
								queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
							}
							queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, loc), false, DefenseLocation::Chokepoint);
						}
					}			
				}
			}
		}
	}


	// If the enemy has cloaked stuff, consider overlord speed.
	if (InformationManager::Instance().enemyHasMobileCloakTech())
	{
		if (hasLair && nHives == 0 &&
			minerals >= 100 && gas >= 100 &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hive) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hive) &&
			_self->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
			!queue.upgradeInQueue() && !queue.buildingInQueue())
		{
			queue.queueAsHighestPriority(MacroAct(BWAPI::UpgradeTypes::Pneumatized_Carapace));
		}
		// And keep going.
	}

	
	// Enemy has air. Make scourge if possible (but in ZvZ only after we have some mutas).
	int nScourgeNeeded = std::min(18, InformationManager::Instance().nScourgeNeeded());
	_nScourgeNeeded = nScourgeNeeded;


	int queueMinerals, queueGas;
	queue.totalCosts(queueMinerals, queueGas);
	_queueMinerals = queueMinerals;
	_queueGas = queueGas;


	// Make at least one extractor
	if (outOfBook && !_emergencyGroundDefense && gas < 300 &&
		hasPool && nDrones >= 9 && nGas == 0 && nFreeGas > 0 &&
		nextInQueue != BWAPI::UnitTypes::Zerg_Extractor &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor) &&
		!queue.buildingInQueue())
	{
		//BWAPI::Broodwar->printf("Extractor");
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Extractor));
		// And keep going.
	}

	// Make up to four for hydras
	else if (outOfBook && !_emergencyGroundDefense &&
		hasPool && (hasDen || BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den)) &&
		nDrones >= 9 && nGas < 4 && nFreeGas > 0 &&
		nextInQueue != BWAPI::UnitTypes::Zerg_Extractor &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor) &&
		!queue.buildingInQueue())
	{
		//BWAPI::Broodwar->printf("Hydras Extractor");
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Extractor));
		// And keep going.
	}

	// Make up to six for mutas
	else if (outOfBook && !_emergencyGroundDefense &&
		hasPool && (hasSpire || BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire)) &&
		nDrones >= 13 && nGas < 6 && nFreeGas > 0 &&
		nextInQueue != BWAPI::UnitTypes::Zerg_Extractor &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor) &&
		!queue.buildingInQueue())
	{
		//BWAPI::Broodwar->printf("Mutas Extractor");
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Extractor));
		// And keep going.
	}

	// Make up to eight for ultras
	else if (outOfBook && !_emergencyGroundDefense &&
		hasPool && (hasUltra || BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern)) &&
		nDrones >= 24 && nGas < 8 && nFreeGas > 0 &&
		nextInQueue != BWAPI::UnitTypes::Zerg_Extractor &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor) &&
		!queue.buildingInQueue())
	{
		//BWAPI::Broodwar->printf("Ultras Extractor");
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Extractor));
		// And keep going.
	}


	// We're in book and should have enough gas but it's off. Something went wrong.
	// This ties in via ELSE with the next check!
	if (!outOfBook && nextInQueue != BWAPI::UnitTypes::None && nextInQueue.gasPrice() > gas &&
		!WorkerManager::Instance().isCollectingGas())
	{
		if (nGas == 0 || nDrones < 9)
		{
			Log().Get() << "isCollectingGas is false and we do not have enough gas.";
			// Emergency. Give up and clear the queue.
			ProductionManager::Instance().goOutOfBook();
			return;
		}
		// Not such an emergency. Turn gas on and keep going.
		WorkerManager::Instance().setCollectGas(true);
	}

	// We have too much gas. Turn off gas collection.
	// Opening book sometimes collects extra gas on purpose.
	// Note ELSE!
	else if (outOfBook && BWAPI::Broodwar->getFrameCount() % 400 &&
		WorkerManager::Instance().isCollectingGas() && gas > queueGas &&
		((minerals < 100 && gas > 300) || (minerals >= 100 && gas > 3 * minerals)))
	{
		WorkerManager::Instance().setCollectGas(false);
		// And keep going.
	}

	// Another cause of production freezes: nGas can be wrong after a hatchery is destroyed.
	// Note ELSE!
	else if (outOfBook && nextInQueue.gasPrice() > gas && nGas > 0 && nGasDrones == 0 &&
		WorkerManager::Instance().isCollectingGas())
	{
		Log().Get() << "isCollectingGas is true but no drones are collecting gas.";
		// Deadlock. Can't get gas. Give up and clear the queue.
		ProductionManager::Instance().goOutOfBook();
		return;
	}

	// Gas is turned off, and upcoming items cost more gas than we have. Get gas.
	// NOTE isCollectingGas() can return false when gas is in the process of being turned off,
	// and some will still be collected.
	// Note ELSE!
	else if (outOfBook && BWAPI::Broodwar->getFrameCount() % 400 &&
		!WorkerManager::Instance().isCollectingGas() && 
		(queueGas > gas || ((gas < 100 && minerals > 300) || (gas >= 100 && minerals > 3 * gas))))
	{
		if (nGas > 0 && nDrones > 3 * nGas)
		{
			// Leave it to the regular queue refill to add more extractors.
			WorkerManager::Instance().setCollectGas(true);
		}
		else if (nGas > 0 && nDrones <= 3 * nGas)
		{
			// nGas > 0 but we do not have enough drones
			if (nextInQueue != BWAPI::UnitTypes::Zerg_Drone && 
				!_emergencyGroundDefense)
			{
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			}
		}
		else
		{
			// Well, we can't collect gas.
			// Make enough drones to get an extractor.
			if (!_emergencyGroundDefense && nDrones >= 5 && nFreeGas > 0 &&
				!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Extractor) &&
				!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor) && 
				!queue.buildingInQueue())
			{
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Extractor));
			}
			else if (nDrones >= 4 && 
				BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor))
			{
				// We have an unfinished extractor. Wait for it to finish.
				// Need 4 drones so that 1 can keep mining minerals (or the rules will loop).
				WorkerManager::Instance().setCollectGas(true);
			}
			else if (!_emergencyGroundDefense && 
				nextInQueue != BWAPI::UnitTypes::Zerg_Drone && nFreeGas > 0)
			{
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			}
		}
		// And keep going.
	}
	

	// Gas limited. We need a gas expansion.
	if (outOfBook && !_emergencyGroundDefense && WorkerManager::Instance().isCollectingGas() &&
		nFreeGas == 0 && nFreeBases > 0 && gas < 100 && minerals > 350 &&
		nDrones > 3 * nBases + 3 * nGas + 6 && nDrones < 0.95 * absoluteMaxDrones &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Extractor) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!queue.buildingInQueue())
	{
		Log().Get() << "Build expanion hatchery, gas limited";
		MacroLocation loc = MacroLocation::Expo;
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
		// And keep going.
	}

	// Drone saturation. We want an expansion.
	else if (outOfBook && !_emergencyGroundDefense && minerals > 350 && nFreeBases > 0 &&
		nDrones >= (0.90 * droneMax) && nDrones < 0.95 * absoluteMaxDrones &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hatchery) && 
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!queue.buildingInQueue())
	{
		Log().Get() << "Build expanion hatchery, drone saturation";
		MacroLocation loc = MacroLocation::Expo;
		if (nBases % 2 == 0)
		{
			loc = MacroLocation::MinOnly;
		}
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
		// And keep going.
	}

	// Compensate for enemy bases
	// Zerg needs hatcheries equal to 2.5 * num enemy bases vs Terran and Protoss
	else if (outOfBook && BWAPI::Broodwar->enemy()->getRace() != BWAPI::Races::Zerg && minerals > 50 && 
		!_emergencyGroundDefense && (nDrones >= nMineralPatches || minerals > 300) && nHatches < nEnemyBases * 2.49 &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!queue.buildingInQueue())
	{
		BWAPI::Unit ourNatural = nullptr;
		const BWEM::Base * ourNaturalLocation = InformationManager::Instance().getMyNaturalLocation();

		// We already have Natural Hatch
		if (ourNaturalLocation)
		{
			ourNatural = InformationManager::Instance().getBaseDepot(ourNaturalLocation);
			int numHatchNatural = InformationManager::Instance().getNumBuildings(ourNaturalLocation, BWAPI::UnitTypes::Zerg_Hatchery);
			int numLairNatural = InformationManager::Instance().getNumBuildings(ourNaturalLocation, BWAPI::UnitTypes::Zerg_Lair);
			int numHiveNatural = InformationManager::Instance().getNumBuildings(ourNaturalLocation, BWAPI::UnitTypes::Zerg_Hive);

			// 3rd Hatch vs Protoss - Natural Hatch
			if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Protoss &&
				numHatchNatural + numLairNatural + numHiveNatural == 1 &&
				UnitUtil::IsValidUnit(ourNatural))
			{
				if (MapTools::Instance().haveWallPosition(BWAPI::UnitTypes::Zerg_Hatchery))
				{
					Log().Get() << "Build wall hatchery, compensate for enemy bases";
					MacroLocation loc = MacroLocation::Natural;
					queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc), false, DefenseLocation::Chokepoint);
				}
				else
				{
					Log().Get() << "Build macro hatchery, no wall position, compensate for enemy bases";
					MacroLocation loc = MacroLocation::Macro;
					queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
				}
			}

			// 3rd Hatch vs Terran - Mainbase Macro Hatch
			else if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Terran &&
				numHatchNatural + numLairNatural + numHiveNatural == 1 &&
				UnitUtil::IsValidUnit(ourNatural))
			{
				Log().Get() << "Build macro hatchery, compensate for enemy bases";
				MacroLocation loc = MacroLocation::Macro;
				queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
			}

			// 3rd Base vs Protoss and Terran
			else if (BWAPI::Broodwar->enemy()->getRace() != BWAPI::Races::Zerg &&
				nBases < nEnemyBases + 1 && nBases < 3 && nFreeBases > 0)
			{
				Log().Get() << "Build expansion hatchery, compensate for enemy bases";
				MacroLocation loc = MacroLocation::Expo;
				queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
			}

		}

		// Natural (2nd) Base vs Protoss and Terran
		else if (BWAPI::Broodwar->enemy()->getRace() != BWAPI::Races::Zerg &&
			nBases < nEnemyBases + 1 && nBases < 2 && nFreeBases > 0)
		{
			Log().Get() << "Build natural hatchery, compensate for enemy bases";
			MacroLocation loc = MacroLocation::Natural;
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
		}

		// Main Hatch
		else {
			Log().Get() << "Build macro hatchery, compensate for enemy bases";
			MacroLocation loc = MacroLocation::Macro;
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
		}
		// And keep going.
	}

	// Compensate for mining out or having too few mineral patches, 
	// we should have enough mineral patches for mutas / lurkers
	else if (outOfBook && !_emergencyGroundDefense && hasLairTech && minerals > 50 &&
		nDrones >= nMineralPatches && nFreeBases > 0 && nMineralPatches < nHatches * 4.5 &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!queue.buildingInQueue())
	{
		Log().Get() << "Build expansion hatchery, compensate for having too few mineral patches";
		MacroLocation loc = MacroLocation::MinOnly;
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
	// And keep going.
	}

	// Defensive hatch vs protoss and terran
	else if (outOfBook && BWAPI::Broodwar->enemy()->getRace() != BWAPI::Races::Zerg &&
		!_emergencyGroundDefense && minerals > 50 && nDrones >= 9 &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!queue.buildingInQueue())
	{
		BWAPI::Unit ourNatural = nullptr;
		const BWEM::Base * ourNaturalLocation = InformationManager::Instance().getMyNaturalLocation();

		// We already have Natural Hatch
		if (ourNaturalLocation)
		{
			ourNatural = InformationManager::Instance().getBaseDepot(ourNaturalLocation);
			int numHatchNatural = InformationManager::Instance().getNumBuildings(ourNaturalLocation, BWAPI::UnitTypes::Zerg_Hatchery);
			int numLairNatural = InformationManager::Instance().getNumBuildings(ourNaturalLocation, BWAPI::UnitTypes::Zerg_Lair);
			int numHiveNatural = InformationManager::Instance().getNumBuildings(ourNaturalLocation, BWAPI::UnitTypes::Zerg_Hive);

			// 3rd Hatch vs Protoss - Natural Hatch
			if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Protoss &&
				numHatchNatural + numLairNatural + numHiveNatural == 1 &&
				UnitUtil::IsValidUnit(ourNatural))
			{
				if (MapTools::Instance().haveWallPosition(BWAPI::UnitTypes::Zerg_Hatchery))
				{
					Log().Get() << "Build wall hatchery, defensive hatch";
					MacroLocation loc = MacroLocation::Natural;
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc), false, DefenseLocation::Chokepoint);
				}
			}

			// 3rd Hatch vs Terran - Mainbase Macro Hatch
			else if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Terran &&
				numHatchNatural + numLairNatural + numHiveNatural == 1 &&
				UnitUtil::IsValidUnit(ourNatural))
			{
				Log().Get() << "Build macro hatchery, defensive hatch";
				MacroLocation loc = MacroLocation::Macro;
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc), false, DefenseLocation::Chokepoint);
			}
		}
	// And keep going.
	}

	// Larva limited. We want a macro hatchery.
	else if (outOfBook && !_emergencyGroundDefense && nLarvas <= 1 &&
		minerals > 300 && (nDrones >= nMineralPatches || minerals > 400) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!queue.buildingInQueue())
	{
		Log().Get() << "Build macro hatchery, larva limited";
		MacroLocation loc = MacroLocation::Macro;
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));

		if (minerals > 600) {
			Log().Get() << "Build macro hatchery, larva limited";
			MacroLocation loc = MacroLocation::Macro;
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
		}
	// And keep going.
	}

	// Larva limited. We want a macro hatchery even if we are already building one.
	else if (outOfBook && !_emergencyGroundDefense && nLarvas <= 1 &&
		minerals > 600 && (nDrones >= nMineralPatches || minerals > 700) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hatchery) &&
		BuildingManager::Instance().numBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery) < 3 &&
		!queue.buildingInQueue())
	{
		Log().Get() << "Build macro hatchery, larva limited";
		MacroLocation loc = MacroLocation::Macro;
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
	// And keep going.
	}


	// We need overlords.
	nextInQueue = queue.getNextUnit();
	figureSupply();
	int totalSupply = std::min(_existingSupply + _pendingSupply, 400);
	if (totalSupply < 400 && nextInQueue != BWAPI::UnitTypes::Zerg_Overlord)
	{
		int supplyDiff = totalSupply - _self->supplyUsed();
		if (nextInQueue != BWAPI::UnitTypes::None)
		{
			if (nextInQueue.isBuilding())
			{
				if (!UnitUtil::IsMorphedBuildingType(nextInQueue))
				{
					supplyDiff += 2;   // for the drone that will be used
				}
			}
			else
			{
				if (nextInQueue.isTwoUnitsInOneEgg())
				{
					supplyDiff -= 2 * nextInQueue.supplyRequired();
				}
				else
				{
					supplyDiff -= nextInQueue.supplyRequired();
				}
			}
		}
		if (!outOfBook)
		{
			supplyDiff += 2 * BuildingManager::Instance().buildingsQueued().size();    // for each drone to be used
		}
		if (supplyDiff < 0)
		{
			for (int diff = supplyDiff; diff < 0; diff += 16)
			{
				queue.queueAfterSunken(BWAPI::UnitTypes::Zerg_Overlord);
			}
		}
		else if (_existingSupply > 20 && supplyDiff <= 0)                       // 20 = overlord + 2 hatcheries
		{
			queue.queueAfterSunken(BWAPI::UnitTypes::Zerg_Overlord);
		}
		else if (_existingSupply > 36 && supplyDiff <= _existingSupply / 8 - 1)     // 36 = 2 overlord + 2 hatchery
		{
			queue.queueAfterSunken(BWAPI::UnitTypes::Zerg_Overlord);
		}
		else if (_existingSupply > 64 && supplyDiff <= _existingSupply / 8 + 8)
		{
			queue.queueAfterSunken(BWAPI::UnitTypes::Zerg_Overlord);
		}
	}
	
	// END STRATEGY EMERGENCIES
}

// How many of our eggs will hatch into the given unit type?
// This does not adjust for zerglings or scourge, which are 2 to an egg.
int StrategyBossZerg::numInEgg(BWAPI::UnitType type) const
{
	int count = 0;

	for (auto & unit : _self->getUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg && unit->getBuildType() == type)
		{
			++count;
		}
	}

	return count;
}

bool StrategyBossZerg::emergencyGroundDefense()
{
	return _emergencyGroundDefense;
}

void StrategyBossZerg::drawStrategyInformation(int x, int y)
{
	if (!Config::Debug::DrawStrategyDebug)
	{
		return;
	}

	std::string emergency = "False";
	if (_emergencyGroundDefense) { emergency = "True"; }

	int myForce = InformationManager::Instance().getPower(_self);
	int enForce = InformationManager::Instance().getPower(_enemy);
	double ratio = double(myForce + 1) / double(enForce + 1);
	int nEnemyBases = InformationManager::Instance().getNumBases(_enemy);
	double drEco = StrategyManager::Instance().droneEco();

	y += 12;
	BWAPI::Broodwar->drawTextScreen(x, y, "%s", "Debug:");
	BWAPI::Broodwar->drawTextScreen(x + 50, y, "%s%s  %s%.2f  %s%d  %s%d  %s%d  %s%d  %s%d  %s%d  %s%d",
		"Alert: ", emergency.c_str(),
		"Ratio: ", ratio,
		"MyAntiGr: ", _ourForceGround,
		"MyForce: ", myForce,
		"EnGrNby: ", _enemyForceGroundIncoming,
		"EnGr: ", _enemyForceGround,
		"EnForce: ", enForce,
		"EnBases: ", nEnemyBases,
		"ScNd: ", _nScourgeNeeded);

	y += 12;
	BWAPI::Broodwar->drawTextScreen(x, y, "%s", "Debug:");
	BWAPI::Broodwar->drawTextScreen(x + 50, y, "%s%d  %s%.2f  %s%d  %s%d  %s%d  %s%d  %s%d  %s%d  %s%d  %s%d",
		"nDr: ", _nDrones,
		"ecoDr: ", drEco,
		"maxDr: ", _nMaxDrones,
		"minDr: ", _nMinDrones,
		"nPat: ", _nMineralPatches,
		"nMinDr: ", _nMineralDrones,
		"nGasDr: ", _nGasDrones,
		"nHat: ", _nHatches,
		"nGas: ", _nGas,
		"nLarva: ", _nLarvas);

}

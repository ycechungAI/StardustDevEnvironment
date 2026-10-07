#include "ProductionManager.h"
#include "GameCommander.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;	

ProductionManager::ProductionManager() 
	: _lastProductionFrame					(0)
	, _assignedWorkerForThisBuilding		(nullptr)
	, _haveLocationForThisBuilding			(false)
	, _delayBuildingPredictionUntilFrame	(0)
	, _outOfBook							(false)
	, _targetGasAmount						(0)
	, _extractorTrickState					(ExtractorTrick::None)
	, _extractorTrickBuilding				(nullptr)
	, _extractorTrickUnitType				(BWAPI::UnitTypes::Zerg_Drone)
	, _goAggressive							(true)
{
    setBuildOrder(StrategyManager::Instance().getOpeningBookBuildOrder());
}

void ProductionManager::setBuildOrder(const BuildOrder & buildOrder)
{
	_queue.clearAll();

	for (size_t i(0); i<buildOrder.size(); ++i)
	{
		_queue.queueAsLowestPriority(buildOrder[i]);
	}

	_queue.resetModified();
}

void ProductionManager::update()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// If we have reached a target amount of gas, take workers off gas.
	if (_targetGasAmount > 0 && BWAPI::Broodwar->self()->gatheredGas() >= _targetGasAmount)
	{
		WorkerManager::Instance().setCollectGas(false);
		_targetGasAmount = 0;           // clear the target
	}

	// If we're in trouble, adjust the production queue to help.
	// Includes scheduling supply as needed.
	StrategyManager::Instance().handleUrgentProductionIssues(_queue);

	// If queue is empty or we only have 1 item left, get new stuff to do
	// If queue is empty, go out of book
	if (_queue.isEmpty() || _queue.size() == 1)
	{
		if (Config::Debug::DrawBuildOrderSearchInfo)
		{
			BWAPI::Broodwar->drawTextScreen(150, 10, "Nothing left to build, new search!");
		}

		if (_queue.isEmpty())
		{
			goOutOfBook();
		}
		
		int getNewStuff = 0;
		if (_queue.size() > 0)
		{
			const MacroAct & act = _queue[0].macroAct;
			if (!_queue.combatUnitInQueue())
			{
				getNewStuff = 1;
			}

			int minerals = getFreeMinerals();
			int gas = getFreeGas();
			if(_outOfBook && minerals > 500)
			{
				getNewStuff = 1;
			}
		}

		if (_queue.isEmpty() || getNewStuff == 1)
		{
			if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg)
			{
				StrategyManager::Instance().getZergBuildOrder(_queue);
			}
			else if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Terran)
			{
				StrategyManager::Instance().getTerranBuildOrder(_queue);
			}
			else if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Protoss)
			{
				StrategyManager::Instance().getProtossBuildOrder(_queue);
			}
		}
	}

	// Build stuff from the production queue.
	manageBuildOrderQueue();
}

void ProductionManager::onUnitDestroy(BWAPI::Unit unit)
{
	// If it's not our unit, we don't care.
	if (!unit || unit->getPlayer() != BWAPI::Broodwar->self())
	{	
		return;
	}
	
	if (unit->getType().isBuilding())
	{
		Log().Get() << "Lost " << unit->getType() << " @ " << unit->getTilePosition();
	}
}

void ProductionManager::manageBuildOrderQueue() 
{
	//DEBUG Latency
	//BWAPI::Broodwar->drawTextScreen(100, 89, "%s%d", "Minerals: ", BWAPI::Broodwar->self()->minerals());
	//BWAPI::Broodwar->drawTextScreen(100, 100, "%s%d", "FreeMinerals: ", getFreeMinerals());
	//BWAPI::Broodwar->drawTextScreen(100, 111, "%s%d", "ReservedMinerals: ", BuildingManager::Instance().getReservedMinerals());
	//BWAPI::Broodwar->drawTextScreen(100, 122, "%s%d", "Gas: ", BWAPI::Broodwar->self()->gas());
	//BWAPI::Broodwar->drawTextScreen(100, 133, "%s%d", "FreeGas: ", getFreeGas());
	//BWAPI::Broodwar->drawTextScreen(100, 144, "%s%d", "ReservedGas: ", BuildingManager::Instance().getReservedGas());
	//BWAPI::Broodwar->drawTextScreen(100, 155, "%s%d", "Latency: ", BWAPI::Broodwar->getLatency());
	//BWAPI::Broodwar->drawTextScreen(100, 166, "%s%d", "LatencyFrames: ", BWAPI::Broodwar->getLatencyFrames());
	//BWAPI::Broodwar->drawTextScreen(100, 177, "%s%d", "RemainingLatencyFrames: ", BWAPI::Broodwar->getRemainingLatencyFrames());
	//DEBUG Latency
	
	// Workaround: Compensate for latency
	// Disabled after converting to BWAPI 4.4
	/*
	if (BWAPI::Broodwar->getLatencyFrames() > 0) {
		if (BWAPI::Broodwar->getFrameCount() % (2 * BWAPI::Broodwar->getLatencyFrames()) != 0)
		{
			return;
		}
	}
	*/

	// If the extractor trick is in progress, do that.
	if (_extractorTrickState != ExtractorTrick::None)
	{
		doExtractorTrick();
		return;
	}

	// if there is nothing in the _queue, oh well
	if (_queue.isEmpty()) 
	{
		return;
	}

	// Profile debug
	//PROFILE_FUNCTION();

	// If we were planning to build and assigned a worker, but the queue was then
	// changed behind our back, release the worker and continue.
	// BUG: The worker can be half way to the building when we order it to return.
	if (_queue.isModified() && _assignedWorkerForThisBuilding)
	{
		//BWAPI::Broodwar->printf("queue modified");
		Log().Debug() << "Releasing worker " << _assignedWorkerForThisBuilding->getID() << " as queue was modified";

		WorkerManager::Instance().finishedWithWorker(_assignedWorkerForThisBuilding);
		_assignedWorkerForThisBuilding = nullptr;
	}

	_queue.resetModified();

	// while there is still something left in the _queue
	while (!_queue.isEmpty()) 
	{
		maybeReorderQueue();

		const BuildOrderItem & currentItem = _queue.getHighestPriorityItem();

		// If this is a command, execute it and keep going.
		if (currentItem.macroAct.isCommand())
		{
			_queue.removeHighestPriorityItem();
			executeCommand(currentItem.macroAct);
			_lastProductionFrame = BWAPI::Broodwar->getFrameCount();
			continue;
		}

		// The unit which can produce the currentItem. May be null.
        BWAPI::Unit producer = getProducer(currentItem.macroAct);

		// check to see if we can make it right now
		bool canMake = producer && canMakeNow(producer, currentItem.macroAct);

		// if the next item in the list is a building and we can't yet make it
		if (!canMake &&
			nextIsBuilding() &&
			BWAPI::Broodwar->getFrameCount() >= _delayBuildingPredictionUntilFrame)
		{
			// construct a temporary building object
			Building b(currentItem.macroAct.getUnitType(), InformationManager::Instance().getMyMainBaseLocation()->Location());
			b.macroAct = currentItem.macroAct;
			b.macroLocation = currentItem.macroAct.getMacroLocation();
			b.isGasSteal = currentItem.isGasSteal;
			b.defenseLocation = currentItem.defenseLocation;
			if (currentItem.macroAct.hasReservedPosition())
			{
				b.finalPosition = currentItem.macroAct.getReservedPosition();
			}

			// predict the worker movement to that building location
			predictWorkerMovement(b);
			break;
		}

		// if we can make the current item
		if (canMake)
		{
			Log().Debug() << "Producing " << currentItem.macroAct.getName();

			// create it
			create(producer, currentItem);
			_assignedWorkerForThisBuilding = nullptr;
			_haveLocationForThisBuilding = false;
			_delayBuildingPredictionUntilFrame = 0;

			// and remove it from the _queue
			_queue.removeHighestPriorityItem();
			_lastProductionFrame = BWAPI::Broodwar->getFrameCount();

			// don't actually loop around in here
			// TODO because we don't keep track of resources used,
			//      we wait until the next frame to build the next thing.
			//      Can cause bad delays in late game!
			break;
		}

		// Might have a production jam
		int productionJamFrameLimit = 60 * 24;

		// If we are still in the opening book, give some more leeway
		if (!_outOfBook) productionJamFrameLimit = (int)(1.5 * productionJamFrameLimit);

		// If we are blocked on resources, give some more leeway, we might just not have any money right now
		else if (!meetsReservedResources(currentItem.macroAct)) productionJamFrameLimit = (int)(2.0 * productionJamFrameLimit);

		// We didn't make anything. Check for a possible production jam.
		// Jams can happen due to bugs, or due to losing prerequisites for planned items.
		if (BWAPI::Broodwar->getFrameCount() > _lastProductionFrame + productionJamFrameLimit)
		{
			// Looks very like a jam. Clear the queue and hope for better luck next time.
			//BWAPI::Broodwar->printf("breaking a production jam %d", _lastProductionFrame);
			Log().Get() << "Breaking a production jam; current queue item is " << currentItem.macroAct.getName();

			goOutOfBook();

			if (_assignedWorkerForThisBuilding)
			{
				WorkerManager::Instance().finishedWithWorker(_assignedWorkerForThisBuilding);
			}
			_assignedWorkerForThisBuilding = nullptr;
			_haveLocationForThisBuilding = false;
			_delayBuildingPredictionUntilFrame = 0;
		}

		// TODO not much of a loop, eh? breaks on all branches
		break;
	}
}

// If we can't immediately produce the top item in the queue but we can produce a
// later item, we may want to move the later item to the front.
// For now, this happens under very limited conditions.
void ProductionManager::maybeReorderQueue()
{
	if (_queue.size() < 2)
	{
		return;
	}

	// If we're in a severe emergency situation, don't try to reorder the queue.
	// We need a resource depot and a few workers.
	if (InformationManager::Instance().getNumBases(BWAPI::Broodwar->self()) == 0 ||
		WorkerManager::Instance().getNumMineralWorkers() <= 3)
	{
		return;
	}

	MacroAct top = _queue.getHighestPriorityItem().macroAct;

	// Don't move anything in front of a command.
	if (top.isCommand())
	{
		return;
	}

	// If next up is supply, don't reorder it.
	// Supply is usually made automatically. If we move something above supply, code below
	// will sometimes have to check whether we have supply to make a unit.
	if (top.isUnit() && top.getUnitType() == BWAPI::Broodwar->self()->getRace().getSupplyProvider())
	{
		return;
	}

	int minerals = getFreeMinerals();
	int gas = getFreeGas();

	BWAPI::Unit producer = getProducer(top);
	bool canMake = producer && canMakeNow(producer, top);

	// We can reorder the queue if:
	// We are out of book and waiting for a dependency,
	// and we can move a later no-dependency item to the front,
	// and we have the minerals and gas to cover the item.
	if ((_outOfBook || 
		(top.isUnit() && top.getUnitType() == BWAPI::UnitTypes::Zerg_Sunken_Colony) ||
		(top.isUnit() && top.getUnitType() == BWAPI::UnitTypes::Zerg_Spore_Colony)) &&
		!canMake && (top.mineralPrice() <= minerals && top.gasPrice() > gas || top.gasPrice() <= gas || top.mineralPrice() > minerals))
	{
		for (int i = _queue.size() - 2; i >= std::max(0, int(_queue.size()) - 5); --i)
		{
			const MacroAct & act = _queue[i].macroAct;
			if (act.isCommand())
			{
				break;
			}
			if(act.isUnit() && act.getUnitType() == BWAPI::UnitTypes::Zerg_Creep_Colony)
			{
				break;
			}
			BWAPI::Unit producer2 = getProducer(act);
			bool canMake2 = producer2 && canMakeNow(producer2, act);
			if (canMake2 && 
				((act.mineralPrice() + top.mineralPrice() <= minerals) ||
				(act.mineralPrice() == 0)) &&
				((act.gasPrice() + top.gasPrice() <= gas) ||
				(act.gasPrice() == 0)))
			{
				//BWAPI::Broodwar->printf("reorder %d and %d", _queue.size() - 1, i);
				Log().Debug() << "Pulling " << act.getName() << " to front of queue, top was " << top.getName();

				_queue.pullToTop(i);
				return;
			}
		}
	}

}

// The unit type has no dependencies: We can always make it if we have the minerals and a worker.
bool ProductionManager::independentUnitType(BWAPI::UnitType type) const
{
	return
		type.supplyProvided() > 0 ||    // includes resource depot and supply unit
		type.isRefinery() ||
		type == BWAPI::UnitTypes::Zerg_Creep_Colony;
}

// May return null if no producer is found.
// NOTE closestTo defaults to BWAPI::Positions::None, meaning we don't care.
BWAPI::Unit ProductionManager::getProducer(MacroAct t, BWAPI::Position closestTo)
{
	UAB_ASSERT(!t.isCommand(), "no producer of a command");

	// get the type of unit that builds this
	BWAPI::UnitType producerType = t.whatBuilds();

	// make a set of all candidate producers
	BWAPI::Unitset candidateProducers;
	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		// Reasons that a unit cannot produce the desired type:

		if (producerType != unit->getType()) { continue; }

		// TODO Due to a BWAPI 4.1.2 bug, lair research can't be done in a hive.
		//      Also spire upgrades can't be done in a greater spire.
		//      The bug is supposedly fixed in the next version.
		//      When a fixed version is available, change the above line to the following:
		// If the producerType is a lair, a hive will do as well.
		// Note: Burrow research in a hatchery can also be done in a lair or hive, but we rarely want to.
		// Ignore the possibility so that we don't accidentally waste lair time.
		//if (!(
		//	producerType == unit->getType() ||
		//	producerType == BWAPI::UnitTypes::Zerg_Lair && unit->getType() == BWAPI::UnitTypes::Zerg_Hive ||
		//  producerType == BWAPI::UnitTypes::Zerg_Spire && unit->getType() == BWAPI::UnitTypes::Zerg_Greater_Spire
		//	))
		//{
		//	continue;
		//}

		if (!unit->isCompleted())  { continue; }
		if (unit->isTraining())    { continue; }
		if (unit->isLifted())      { continue; }
		if (!unit->isPowered())    { continue; }
		if (unit->isUpgrading())   { continue; }
		if (unit->isResearching()) { continue; }

		// if the type is an addon, some special cases
		if (t.isUnit() && t.getUnitType().isAddon())
		{
			// Already has an addon, or is otherwise unable to make one.
			if (!unit->canBuildAddon())
			{
				continue;
			}

			// if we just told this unit to build an addon, then it will not be building another one
			// this deals with the frame-delay of telling a unit to build an addon and it actually starting to build
			if (unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Build_Addon
				&& (BWAPI::Broodwar->getFrameCount() - unit->getLastCommandFrame() < 10))
			{
				continue;
			}
		}

		// if a unit requires an addon and the producer doesn't have one
		// TODO Addons seem a bit erratic. Bugs are likely.
		// TODO What exactly is requiredUnits()? On the face of it, the story is that
		//      this code is for e.g. making tanks, built in a factory which has a machine shop.
		//      Research that requires an addon is done in the addon, a different case.
		//      Apparently wrong for e.g. ghosts, which require an addon not on the producer.
		if (t.isUnit())
		{
			bool reject = false;   // innocent until proven guilty
			typedef std::pair<BWAPI::UnitType, int> ReqPair;
			for (const ReqPair & pair : t.getUnitType().requiredUnits())
			{
				BWAPI::UnitType requiredType = pair.first;
				if (requiredType.isAddon())
				{
					if (!unit->getAddon() || (unit->getAddon()->getType() != requiredType))
					{
						reject = true;
						continue;    // out of inner loop
					}
				}
			}
			if (reject)
			{
				continue;
			}
		}

		// if we haven't rejected it, add it to the set of candidates
		candidateProducers.insert(unit);
	}

	// If we are zerg and morphing a lurker, guardian or devourer, make it close to our main
	if (t.isUnit() &&
		(t.getUnitType() == BWAPI::UnitTypes::Zerg_Lurker ||
		t.getUnitType() == BWAPI::UnitTypes::Zerg_Guardian ||
		t.getUnitType() == BWAPI::UnitTypes::Zerg_Devourer))
	{
		return getClosestUnitToPosition(candidateProducers,
			InformationManager::Instance().getMyMainBaseLocation()->Center());
	}
	// If we are zerg and morphing an unit, find a larva at the hatch with the most amount of larva
	else if (t.isUnit() && t.whatBuilds() == BWAPI::UnitTypes::Zerg_Larva)
	{
		// BUG: This sometimes makes zerglings skip :(
		return getBestLarva(candidateProducers, closestTo);
	}
	// If we are zerg and making a building
	else if (t.isUnit() && BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg)
	{
		return getClosestUnitToPosition(candidateProducers, closestTo);
	}

	// If we are protoss or terran and making a worker
	else if (t.isUnit() && t.getUnitType().isWorker())
	{
		return getFarthestUnitFromPosition(candidateProducers,
			InformationManager::Instance().getMyMainBaseLocation()->Center());
	}
	// If we are protoss or terran
	else
	{
		return getClosestUnitToPosition(candidateProducers, closestTo);
	}
}

BWAPI::Unit ProductionManager::getClosestUnitToPosition(const BWAPI::Unitset & units, BWAPI::Position closestTo)
{
    if (units.size() == 0)
    {
        return nullptr;
    }

    // if we don't care where the unit is return the first one we have
    if (closestTo == BWAPI::Positions::None)
    {
        return *(units.begin());
    }

    BWAPI::Unit closestUnit = nullptr;
    int minDist(1000000);

	for (auto & unit : units) 
    {
        UAB_ASSERT(unit != nullptr, "Unit was null");

		int distance = unit->getDistance(closestTo);
		if (distance < minDist) 
        {
			closestUnit = unit;
			minDist = distance;
		}
	}

    return closestUnit;
}

BWAPI::Unit ProductionManager::getFarthestUnitFromPosition(const BWAPI::Unitset & units, BWAPI::Position farthest)
{
	if (units.size() == 0)
	{
		return nullptr;
	}

	// if we don't care where the unit is return the first one we have
	if (farthest == BWAPI::Positions::None)
	{
		return *(units.begin());
	}

	BWAPI::Unit farthestUnit = nullptr;
	int maxDist(-1);

	for (auto & unit : units)
	{
		UAB_ASSERT(unit != nullptr, "Unit was null");

		int distance = unit->getDistance(farthest);
		if (distance > maxDist)
		{
			farthestUnit = unit;
			maxDist = distance;
		}
	}

	return farthestUnit;
}

BWAPI::Unit ProductionManager::getClosestLarvaToPosition(BWAPI::Position closestTo)
{
	BWAPI::Unitset larvas;

	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Larva)
		{
			larvas.insert(unit);
		}
	}

	return getClosestUnitToPosition(larvas, closestTo);
}

BWAPI::Unit ProductionManager::getBestLarva(const BWAPI::Unitset & units, BWAPI::Position closestTo)
{
	// 1. Pick a hatchery, lair or hive that has the most larvas.
	// This reduces wasted larvas; a hatchery won't make another if it has three.
	BWAPI::Unit bestHatchery = nullptr;
	BWAPI::Unitset maxLarvaSet;
	size_t maxLarva = 0;
	for (BWAPI::Unit hatchery : BWAPI::Broodwar->self()->getUnits())
	{
		if (UnitUtil::IsCompletedResourceDepot(hatchery))
		{
			auto larvaSet = hatchery->getLarva();
			if (larvaSet.size() > maxLarva)
			{
				bestHatchery = hatchery;
				maxLarvaSet = larvaSet;
				maxLarva = larvaSet.size();
				if (maxLarva >= 3)
				{
					break;
				}
			}
		}
	}
	if (bestHatchery)
	{
		//return *bestHatchery->getLarva().begin();
		return getClosestUnitToPosition(units, bestHatchery->getPosition());
	}

	// 2. There might be a larva not attached to any hatchery.
	for (BWAPI::Unit larva : BWAPI::Broodwar->self()->getUnits())
	{
		if (larva->getType() == BWAPI::UnitTypes::Zerg_Larva)
		{
			return larva;
		}
	}

	return nullptr;
}

// check to see if all preconditions are met and then create a unit
void ProductionManager::create(BWAPI::Unit producer, const BuildOrderItem & item)
{
	if (!producer)
	{
		return;
	}

	MacroAct act = item.macroAct;

	// if we're dealing with a building
	if (act.isBuilding()                                        // implies act.isUnit()
		&& !UnitUtil::IsMorphedBuildingType(act.getUnitType())  // morphed from another zerg building, not built
		&& !act.getUnitType().isAddon())                        // terran addon
	{
		// By default, build in the main base.
		// BuildingManager will override the location if it needs to.
		// Otherwise it will find some spot near desiredLocation.
		BWAPI::TilePosition desiredLocation = InformationManager::Instance().getMyMainBaseLocation()->Location();

		if (act.getMacroLocation() == MacroLocation::Natural)
		{
			const BWEM::Base * natural = InformationManager::Instance().getMyNaturalLocation();
			if (natural)
			{
				desiredLocation = natural->Location();
			}
		}

		BuildingManager::Instance().addBuildingTask(act, desiredLocation, item);
	}
	else if (act.isUnit() && act.getUnitType().isAddon())
	{
		//BWAPI::TilePosition addonPosition(producer->getTilePosition().x + producer->getType().tileWidth(), producer->getTilePosition().y + producer->getType().tileHeight() - t.unitType.tileHeight());
		producer->buildAddon(act.getUnitType());
	}
	// if we're dealing with a non-building unit
	else if (act.isUnit())
	{
		if (act.getUnitType().getRace() == BWAPI::Races::Zerg)
		{
			// if the race is zerg, morph the unit
			producer->morph(act.getUnitType());
			Log().Get() << "Started morping " << act.getUnitType();
		}
		else
		{
			// if not, train the unit
			producer->train(act.getUnitType());
		}
	}
	// if we're dealing with a tech research
	else if (act.isTech())
	{
		producer->research(act.getTechType());
	}
	else if (act.isUpgrade())
	{
		producer->upgrade(act.getUpgradeType());
	}
	else
	{
		UAB_ASSERT(false, "don't know how to create that");
	}
}

bool ProductionManager::canMakeNow(BWAPI::Unit producer, MacroAct t)
{
	UAB_ASSERT(producer != nullptr, "producer was null");

	bool canMake = meetsReservedResources(t);
	if (canMake)
	{
		if (t.isUnit())
		{
			canMake = BWAPI::Broodwar->canMake(t.getUnitType(), producer);
		}
		else if (t.isTech())
		{
			canMake = BWAPI::Broodwar->canResearch(t.getTechType(), producer);
		}
		else if (t.isUpgrade())
		{
			canMake = BWAPI::Broodwar->canUpgrade(t.getUpgradeType(), producer);
		}
		else if (t.isCommand())
		{
			canMake = true;
		}
		else
		{
			UAB_ASSERT(false, "Unknown type");
		}
	}

	return canMake;
}

// When the next item in the _queue is a building, this checks to see if we should move to
// its location in preparation for construction. If so, it orders the move.
// This function is here as it needs to access prodction manager's reserved resources info.
void ProductionManager::predictWorkerMovement(const Building & b)
{
	if (b.isGasSteal || _assignedWorkerForThisBuilding)
	{
		return;
	}

	// get a possible building location for the building
	if (!_haveLocationForThisBuilding)
	{
		_predictedTilePosition = BuildingManager::Instance().getBuildingLocation(b);
	}

	if (_predictedTilePosition != BWAPI::TilePositions::None)
	{
		_haveLocationForThisBuilding = true;
	}
	else
	{
		//BWAPI::Broodwar->printf("can't place building %s", b.type.getName().c_str());
		// If we can't place the building now, we probably can't place it next frame either.
		// Delay for a while before trying again. We could overstep the time limit.
		_delayBuildingPredictionUntilFrame = 13 + BWAPI::Broodwar->getFrameCount();
		return;
	}

	int x1 = _predictedTilePosition.x * 32;
	int y1 = _predictedTilePosition.y * 32;

	// draw a box where the building will be placed
	if (Config::Debug::DrawWorkerInfo)
	{
		int x2 = x1 + (b.type.tileWidth()) * 32;
		int y2 = y1 + (b.type.tileHeight()) * 32;
		BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Blue, false);
	}

	// where we want the worker to walk to
	BWAPI::Position walkToPosition		= BWAPI::Position(x1 + (b.type.tileWidth() / 2) * 32, y1 + (b.type.tileHeight() / 2) * 32);

	// compute how many resources we need to construct this building
	int mineralsRequired				= std::max(0, b.type.mineralPrice() - getFreeMinerals());
	int gasRequired						= std::max(0, b.type.gasPrice() - getFreeGas());

	// get a candidate worker to move to this location
	BWAPI::Unit moveWorker				= WorkerManager::Instance().getBuilder(b);
	if (!moveWorker) return;
	if (!moveWorker->getPosition().isValid()) return;
	if (!walkToPosition.isValid()) return;

	// how many frames it will take us to move to the building location
	// We add some time since the actual pathfinding of the workers is bad
	int distanceToMove = MapTools::Instance().getGroundDistanceBWEB(moveWorker->getPosition(), walkToPosition, false);

	// Determine if we can build at the time when we have all dependencies and the worker has arrived
	if (!WorkerManager::Instance().willHaveResources(mineralsRequired, gasRequired, distanceToMove)) return;

	// Do we have the tech to build it?
	// If the building is a tech bulding and need another building dependency that is not finished,
	// don't predict worker movement since the building will be put in main close to workers.
	if (!b.macroAct.hasTech()) return;

	// we have assigned a worker
	_assignedWorkerForThisBuilding = moveWorker;

	// tell the worker manager to move this worker
	WorkerManager::Instance().setMoveWorker(moveWorker, mineralsRequired, gasRequired, walkToPosition);

	//BWAPI::Broodwar->printf("predict worker movement %d", moveWorker->getID());
	Log().Debug() << "Predict movement worker " << moveWorker->getID() << " to build " << b.type << " @ " << _predictedTilePosition;
}

int ProductionManager::getFreeMinerals()
{
	int minerals = std::max(0, BWAPI::Broodwar->self()->minerals());
	int getReservedMinerals = std::max(0, BuildingManager::Instance().getReservedMinerals());
	int freeMinerals = minerals - getReservedMinerals;

	return freeMinerals;
}

int ProductionManager::getFreeGas()
{
	int gas = std::max(0, BWAPI::Broodwar->self()->gas());
	int getReservedGas = std::max(0, BuildingManager::Instance().getReservedGas());
	int freeGas = gas - getReservedGas;

	return freeGas;
}

void ProductionManager::executeCommand(MacroAct act)
{
	UAB_ASSERT(act.isCommand(), "executing a non-command");

	MacroCommandType cmd = act.getCommandType().getType();

	if (cmd == MacroCommandType::Scout) {
		if (MapTools::Instance().hasIslandBases()) return;
		GameCommander::Instance().goScoutAlways();
	}
	else if (cmd == MacroCommandType::ScoutIfNeeded) {
		if (MapTools::Instance().hasIslandBases()) return;
		GameCommander::Instance().goScoutIfNeeded();
	}
	else if (cmd == MacroCommandType::ScoutLocation) {
		if (MapTools::Instance().hasIslandBases()) return;
		GameCommander::Instance().goScoutIfNeeded();
		ScoutManager::Instance().setScoutLocationOnly();
	}
	else if (cmd == MacroCommandType::ScoutOnceOnly) {
		if (MapTools::Instance().hasIslandBases()) return;
		GameCommander::Instance().goScoutIfNeeded();
		ScoutManager::Instance().setScoutOnceOnly();
	}
	else if (cmd == MacroCommandType::ScoutOnMin) {
		const int scoutOn = act.getCommandType().getAmount();
		GameCommander::Instance().setScoutOnMinerals(scoutOn);
	}
	else if (cmd == MacroCommandType::OverlordClosestBase) {
		ScoutManager::Instance().goOverlordClosestBase();
	}
	else if (cmd == MacroCommandType::StealGas) {
		ScoutManager::Instance().setGasSteal();
		GameCommander::Instance().goScoutAlways();
	}
	else if (cmd == MacroCommandType::StopGas) {
		WorkerManager::Instance().setCollectGas(false);
	}
	else if (cmd == MacroCommandType::StartGas) {
		WorkerManager::Instance().setCollectGas(true);
	}
	else if (cmd == MacroCommandType::GasUntil) {
		WorkerManager::Instance().setCollectGas(true);
		_targetGasAmount = BWAPI::Broodwar->self()->gatheredGas()
			- BWAPI::Broodwar->self()->gas()
			+ act.getCommandType().getAmount();
	}
	else if (cmd == MacroCommandType::ExtractorTrick) {
		_extractorTrickUnitType = BWAPI::UnitTypes::Zerg_Drone;
		startExtractorTrick();
	}
	else if (cmd == MacroCommandType::ExtractorTrickZergling) {
		_extractorTrickUnitType = BWAPI::UnitTypes::Zerg_Zergling;
		startExtractorTrick();
	}
	else if (cmd == MacroCommandType::Aggressive) {
		setAggression(true);
		//BWAPI::Broodwar->printf("Set Agressive");
	}
	else if (cmd == MacroCommandType::Defensive) {
		setAggression(false);
		//BWAPI::Broodwar->printf("Set Defensive");
	}
	else {
		UAB_ASSERT(false, "unknown macro command");
	}
}

// Can we afford it, taking into account reserved resources?
bool ProductionManager::meetsReservedResources(MacroAct act)
{
	return (act.mineralPrice() <= getFreeMinerals()) && (act.gasPrice() <= getFreeGas());
}

// selects a unit of a given type
BWAPI::Unit ProductionManager::selectUnitOfType(BWAPI::UnitType type, BWAPI::Position closestTo) 
{
	// if we have none of the unit type, return nullptr right away
	if (BWAPI::Broodwar->self()->completedUnitCount(type) == 0) 
	{
		return nullptr;
	}

	BWAPI::Unit unit = nullptr;

	// if we are concerned about the position of the unit, that takes priority
    if (closestTo != BWAPI::Positions::None) 
    {
		double minDist(1000000);

		for (auto & u : BWAPI::Broodwar->self()->getUnits()) 
        {
			if (u->getType() == type) 
            {
				int distance = u->getDistance(closestTo);
				if (!unit || distance < minDist) {
					unit = u;
					minDist = distance;
				}
			}
		}

	// if it is a building and we are worried about selecting the unit with the least
	// amount of training time remaining
	} 
    else if (type.isBuilding()) 
    {
		for (auto & u : BWAPI::Broodwar->self()->getUnits()) 
        {
            UAB_ASSERT(u != nullptr, "Unit was null");

			if (u->getType() == type && u->isCompleted() && !u->isTraining() && !u->isLifted() && u->isPowered()) {

				return u;
			}
		}
		// otherwise just return the first unit we come across
	} 
    else 
    {
		for (auto & u : BWAPI::Broodwar->self()->getUnits()) 
		{
            UAB_ASSERT(u != nullptr, "Unit was null");

			if (u->getType() == type && u->isCompleted() && u->getHitPoints() > 0 && !u->isLifted() && u->isPowered()) 
			{
				return u;
			}
		}
	}

	return nullptr;
}

void ProductionManager::drawProductionInformation(int x, int y)
{
    if (!Config::Debug::DrawProductionInfo)
    {
        return;
    }

	BWAPI::Broodwar->drawTextScreen(x - 30, y, "\x04 TIME");
	BWAPI::Broodwar->drawTextScreen(x, y, "\x04 UNIT NAME");

	y += 10;
	if (_extractorTrickState == ExtractorTrick::None)
	{
		if (WorkerManager::Instance().isCollectingGas())
		{
			BWAPI::Broodwar->drawTextScreen(x - 30, y, "\x04 gas %d, target %d", BWAPI::Broodwar->self()->gatheredGas(), _targetGasAmount);
		}
		else
		{
			BWAPI::Broodwar->drawTextScreen(x - 30, y, "\x04 gas %d, stopped", BWAPI::Broodwar->self()->gatheredGas());
		}
	}
	else if (_extractorTrickState == ExtractorTrick::Start)
	{
		BWAPI::Broodwar->drawTextScreen(x - 30, y, "\x04 extractor trick: start");
	}
	else if (_extractorTrickState == ExtractorTrick::ExtractorOrdered)
	{
		BWAPI::Broodwar->drawTextScreen(x - 30, y, "\x04 extractor trick: extractor ordered");
	}
	else if (_extractorTrickState == ExtractorTrick::DroneOrdered)
	{
		BWAPI::Broodwar->drawTextScreen(x - 30, y, "\x04 extractor trick: drone ordered");
	}

	// fill prod with each unit which is under construction
	std::vector<BWAPI::Unit> prod;
	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		UAB_ASSERT(unit != nullptr, "Unit was null");

		//if (unit->isBeingConstructed())
		if (unit->isBeingConstructed() || unit->isResearching() || unit->isUpgrading())
		{
			prod.push_back(unit);
		}
	}

	// sort it based on the time it was started
	std::sort(prod.begin(), prod.end(), CompareWhenStarted());

	// for each unit in the _queue
	for (auto & unit : prod) 
    {
		std::string prefix = "\x07";
		BWAPI::UnitType t = unit->getType();

		if (unit->isResearching() && unit->getTech() != BWAPI::TechTypes::None) 
		{
			y += 10;
			BWAPI::Broodwar->drawTextScreen(x, y, " %s%s", prefix.c_str(), TrimRaceName(unit->getTech().c_str()).c_str());
			BWAPI::Broodwar->drawTextScreen(x - 35, y, "%s%6d", prefix.c_str(), unit->getRemainingResearchTime());
		}
		else if (unit->isUpgrading() && unit->getUpgrade() != BWAPI::UpgradeTypes::None) 
		{
			y += 10;
			BWAPI::Broodwar->drawTextScreen(x, y, " %s%s", prefix.c_str(), TrimRaceName(unit->getUpgrade().c_str()).c_str());
			BWAPI::Broodwar->drawTextScreen(x - 35, y, "%s%6d", prefix.c_str(), unit->getRemainingUpgradeTime());
		}
		else
		{
			if (t == BWAPI::UnitTypes::Zerg_Egg)
			{
				t = unit->getBuildType();
			}

			y += 10;
			BWAPI::Broodwar->drawTextScreen(x, y, " %s%s", prefix.c_str(), TrimRaceName(t.getName()).c_str());
			BWAPI::Broodwar->drawTextScreen(x - 35, y, "%s%6d", prefix.c_str(), unit->getRemainingBuildTime());
		}
	}

	_queue.drawQueueInformation(x, y+10, _outOfBook);
}

ProductionManager & ProductionManager::Instance()
{
	static ProductionManager instance;
	return instance;
}

void ProductionManager::queueGasSteal()
{
    _queue.queueAsHighestPriority(MacroAct(BWAPI::Broodwar->self()->getRace().getRefinery()), true);
}

// We're zerg and doing the extractor trick to get an extra drone.
// Set a flag to start the procedure.
void ProductionManager::startExtractorTrick()
{
	// Only zerg can do the extractor trick.
	if (BWAPI::Broodwar->self()->getRace() != BWAPI::Races::Zerg)
	{
		return;
	}

	// If we're not supply blocked, then we may have lost units earlier.
	// No need to do the trick, just queue up a drone.
	if (BWAPI::Broodwar->self()->supplyTotal() - BWAPI::Broodwar->self()->supplyUsed() >= 2)
	{
		_queue.queueAsHighestPriority(MacroAct(_extractorTrickUnitType));
		return;
	}
	
	// We need a free drone to execute the trick.
	if (WorkerManager::Instance().getNumMineralWorkers() <= 0)
	{
		return;
	}

	// And we need a free geyser to do it on.
	if (BuildingPlacer::Instance().getRefineryPosition() == BWAPI::TilePositions::None)
	{
		return;
	}
	
	_extractorTrickState = ExtractorTrick::Start;
}

// The extractor trick is in progress. Take the next step, when possible.
// At most one step occurs per frame.
void ProductionManager::doExtractorTrick()
{
	if (_extractorTrickState == ExtractorTrick::Start)
	{
		UAB_ASSERT(!_extractorTrickBuilding, "already have an extractor trick building");
		int nDrones = WorkerManager::Instance().getNumMineralWorkers();
		if (nDrones <= 0)
		{
			// Oops, we can't do it without a free drone. Give up.
			_extractorTrickState = ExtractorTrick::None;
		}
		// If there are "many" drones mining, assume we'll get resources to finish the trick.
		// Otherwise wait for the full 100 before we start.
		else if (getFreeMinerals() >= 100 || (nDrones >= 6 && getFreeMinerals() >= 76))
		{
			// We also need a larva to make the drone.
			if (BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Zerg_Larva) > 0)
			{
				BWAPI::TilePosition loc = InformationManager::Instance().getMyMainBaseLocation()->Location();
				MacroAct m(BWAPI::UnitTypes::Zerg_Extractor);
				Building & b = BuildingManager::Instance().addTrackedBuildingTask(m, loc, BuildOrderItem(m, false));
				b.builderUnit = WorkerManager::Instance().getMoveWorker(BWAPI::Position(loc));
				_extractorTrickState = ExtractorTrick::ExtractorOrdered;
				_extractorTrickBuilding = &b;
			}
		}
	}
	else if (_extractorTrickState == ExtractorTrick::ExtractorOrdered)
	{
		int supplyAvail = BWAPI::Broodwar->self()->supplyTotal() - BWAPI::Broodwar->self()->supplyUsed();
		if (supplyAvail >= 2 && getFreeMinerals() >= 50)
		{
			// We can build a drone now: The extractor finished, or another unit died somewhere.
			// Well, there is one more condition: We need a larva.
			BWAPI::Unit larva = getClosestLarvaToPosition(InformationManager::Instance().getMyMainBaseLocation()->Center());
			if (larva)
			{
				larva->morph(_extractorTrickUnitType);
				_extractorTrickState = ExtractorTrick::DroneOrdered;
			}
		}
		else if (supplyAvail < -2)
		{
			// Uh oh, we must have lost an overlord or a hatchery. Give up by moving on.
			_extractorTrickState = ExtractorTrick::DroneOrdered;
		}
		else if (WorkerManager::Instance().getNumMineralWorkers() <= 0)
		{
			// Yow, there must have been a drone massacre. Give up by moving on.
			_extractorTrickState = ExtractorTrick::DroneOrdered;
		}
	}
	else if (_extractorTrickState == ExtractorTrick::DroneOrdered)
	{
		UAB_ASSERT(_extractorTrickBuilding, "no extractor to cancel");
		BuildingManager::Instance().cancelBuilding(*_extractorTrickBuilding);
		_extractorTrickState = ExtractorTrick::None;
		_extractorTrickBuilding = nullptr;
	}
	else
	{
		UAB_ASSERT(false, "unexpected extractor trick state (possibly none)");
	}
}

// this will return true if any unit is on the first frame of its training time remaining
// this can cause issues for the build order search system so don't plan a search on these frames
bool ProductionManager::canPlanBuildOrderNow() const
{
    for (const auto & unit : BWAPI::Broodwar->self()->getUnits())
    {
        if (unit->getRemainingTrainTime() == 0)
        {
            continue;       
        }

        BWAPI::UnitType trainType = unit->getLastCommand().getUnitType();

        if (unit->getRemainingTrainTime() == trainType.buildTime())
        {
            return false;
        }
    }

    return true;
}

// The next item in the queue is a building that requires a worker to construct.
// Addons and morphed buildings (e.g. lair) do not need a worker.
bool ProductionManager::nextIsBuilding() const
{
	if (_queue.isEmpty())
	{
		return false;
	}

	const MacroAct & next = _queue.getHighestPriorityItem().macroAct;

	return next.isBuilding() &&
		!next.getUnitType().isAddon() &&
		!UnitUtil::IsMorphedBuildingType(next.getUnitType());
}

// We have finished our book line, or are breaking out of it early.
// Clear the queue, set _outOfBook, go aggressive.
void ProductionManager::goOutOfBook()
{
	if (_outOfBook == false) {
		// go out of book
		_outOfBook = true;
		setAggression(true);

		// only send messages vs non-humans.
		if (!Config::Strategy::UseHumanSpecificStrategy)
		{
			/* initialize random seed */
			srand((int)time(NULL));
			/* generate number between 1 and 5 */
			int r = rand() % 6 + 1;

			// goOutOfBook message
			if (r == 1) {
				BWAPI::Broodwar->sendText("Ok Google, help me create an army! :)");
			}
			else if (r == 2) {
				BWAPI::Broodwar->sendText("Hmmm... is this the right strategy? :)");
			}
			else if (r == 3) {
				BWAPI::Broodwar->sendText("End of line... I'm free!!! :)");
			}
			else if (r == 4) {
				BWAPI::Broodwar->sendText("Hey Siri, where can I hide my army? :)");
			}
			else if (r == 5) {
				BWAPI::Broodwar->sendText("Alexa, play zerg game music. :)");
			}
			else if (r == 6) {
				BWAPI::Broodwar->sendText("Hmmm... your strategy looks suspicious. :)");
			}
		}
	}

	_queue.clearAll();
	_lastProductionFrame = BWAPI::Broodwar->getFrameCount();
}
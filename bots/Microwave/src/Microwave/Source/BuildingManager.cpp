#include "Common.h"
#include "BuildingManager.h"
#include "Micro.h"
#include "ProductionManager.h"
#include "ScoutManager.h"
#include "WorkerManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;
using namespace BWAPI::Filter;

BuildingManager::BuildingManager()
	: _debugMode(false)
	, _reservedMinerals(0)
	, _reservedGas(0)
{
}

BuildingManager & BuildingManager::Instance()
{
	static BuildingManager instance;
	return instance;
}

// gets called every frame from GameCommander
void BuildingManager::update()
{
	// Profile debug
	//PROFILE_FUNCTION();

	validateWorkersAndBuildings();          // check to see if assigned workers have died en route or while constructing
	assignWorkersToUnassignedBuildings();   // assign workers to the unassigned buildings and label them 'planned'    
	constructAssignedBuildings();           // for each planned building, if the worker isn't constructing, send the command    
	checkForStartedConstruction();          // check to see if any buildings have started construction and update data structures    
	checkForDeadTerranBuilders();           // if we are terran and a building is under construction without a worker, assign a new one    
	checkForCompletedBuildings();           // check to see if any buildings have completed and update data structures
	checkReservedResources();               // verify that reserved minerals and gas are counted correctly
}

// The building took too long to start, or we lost too many workers trying to build it.
// If true, the building gets canceled.
bool BuildingManager::buildingTimedOut(const Building & b) const
{
	if (b.status == BuildingStatus::UnderConstruction) return false;

	// Many builders have failed to build this
	if (b.buildersSent > 2)
	{
		return true;
	}

	// The first build command was issued over 10 seconds ago, but the building still hasn't been built
	if (b.buildFrame > 0 &&
		BWAPI::Broodwar->getFrameCount() - b.buildFrame > 10 * 24)
	{
		return true;
	}

	// No building should take over 60 secs total to be built
	if (BWAPI::Broodwar->getFrameCount() - b.startFrame > 60 * 24)
	{
		return true;
	}

	return false;
}

// STEP 1: DO BOOK KEEPING ON BUILDINGS WHICH MAY HAVE DIED OR TIMED OUT
void BuildingManager::validateWorkersAndBuildings()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// find any buildings which have become obsolete
	for (auto it = _buildings.begin(); it != _buildings.end(); )
	{
		auto & b = *it;

		if (buildingTimedOut(b) &&
			ProductionManager::Instance().isOutOfBook() &&
			(b.type.getRace() != BWAPI::Races::Terran || b.type.getRace() == BWAPI::Races::Terran && !b.builderUnit))
		{
			undoBuilding(b);
			it = _buildings.erase(it);
			continue;
		}

		if (b.status == BuildingStatus::UnderConstruction)
		{
			if (!b.buildingUnit ||
				!b.buildingUnit->exists() ||
				b.buildingUnit->getHitPoints() <= 0 ||
				!b.buildingUnit->getType().isBuilding())
			{
				undoBuilding(b);
				it = _buildings.erase(it);
				continue;
			}
		}

		it++;
	}
}

// STEP 2: ASSIGN WORKERS TO BUILDINGS WITHOUT THEM
// Also places the building.
void BuildingManager::assignWorkersToUnassignedBuildings()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// for each building that doesn't have a builder, assign one
	for (Building & b : _buildings)
	{
		if (b.status != BuildingStatus::Unassigned)
		{
			continue;
		}

		//BWAPI::Broodwar->printf("Assigning Worker To: %s", b.type.getName().c_str());

		//2nd Hatch position natural
		BWAPI::Unit ourNatural = nullptr;
		const BWEM::Base * ourNaturalLocation = InformationManager::Instance().getMyNaturalLocation();
		if (ourNaturalLocation && 
			b.macroLocation == MacroLocation::Natural &&
			b.type == BWAPI::UnitTypes::Zerg_Hatchery)
		{
			ourNatural = InformationManager::Instance().getBaseDepot(ourNaturalLocation);
			int numHatchNatural = InformationManager::Instance().getNumBuildings(ourNaturalLocation, BWAPI::UnitTypes::Zerg_Hatchery);
			int numLairNatural = InformationManager::Instance().getNumBuildings(ourNaturalLocation, BWAPI::UnitTypes::Zerg_Lair);
			int numHiveNatural = InformationManager::Instance().getNumBuildings(ourNaturalLocation, BWAPI::UnitTypes::Zerg_Hive);

			if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Protoss &&
				(UnitUtil::IsValidUnit(ourNatural) || BWEB::Map::isUsed(ourNaturalLocation->Location()) != BWAPI::UnitTypes::None) &&
				numHatchNatural + numLairNatural + numHiveNatural == 1)
			{
				b.defenseLocation = DefenseLocation::Chokepoint;
			}
		}


		//2nd Hatch position main
		BWAPI::Unit ourMain = nullptr;
		const BWEM::Base * ourMainLocation = InformationManager::Instance().getMyMainBaseLocation();
		if (ourMainLocation && 
			(b.macroLocation == MacroLocation::Macro || b.macroLocation == MacroLocation::Main) &&
			b.type == BWAPI::UnitTypes::Zerg_Hatchery)
		{
			ourMain = InformationManager::Instance().getBaseDepot(ourMainLocation);
			int numHatchMain = InformationManager::Instance().getNumBuildings(ourMainLocation, BWAPI::UnitTypes::Zerg_Hatchery);
			int numLairMain = InformationManager::Instance().getNumBuildings(ourMainLocation, BWAPI::UnitTypes::Zerg_Lair);
			int numHiveMain = InformationManager::Instance().getNumBuildings(ourMainLocation, BWAPI::UnitTypes::Zerg_Hive);

			if (UnitUtil::IsValidUnit(ourMain) &&
				numHatchMain + numLairMain + numHiveMain == 1)
			{
				b.defenseLocation = DefenseLocation::Chokepoint;
			}
		}

		// The builder may have already been decided before we got this far.
		// If we don't already have a valid builder, choose one.
		// NOTE There is a dependency loop here. To get the builder, we want to know the
		//      building's position. But to check its final position we need to know the
		//      builder unit, because other units block the building placement. To solve this,
		//      we get the builder first based on the desired rather than the final position.
		//      Better solutions are possible!
		if (!b.builderUnit ||
			!b.builderUnit->exists() ||
			b.builderUnit->getPlayer() != BWAPI::Broodwar->self() ||
			!b.builderUnit->getType().isWorker())
		{
			releaseBuilderUnit(b);		// does nothing if it's null
			setBuilderUnit(b);			// tries to set b.builderUnit
		}
    
		// No good, we can't get a worker. Give up on the building for this frame.
		if (!b.builderUnit || !b.builderUnit->exists())
		{
			continue;
		}

		// We want the worker before the location, so that we can avoid counting the worker
		// as an obstacle for the location it is already in--ProductionManager sends the worker early.
		// Otherwise we may have to re-place the building elsewhere, causing unnecessary movement.
		BWAPI::TilePosition testLocation = getBuildingLocation(b);
		if (!testLocation.isValid())
		{
			continue;
		}
		b.finalPosition = testLocation;

		++b.buildersSent;    // count workers ever assigned to build it

		// reserve this building's space
		BuildingPlacer::Instance().reserveTiles(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
		// reserve this building's space in BWEB
		BWEB::Map::addUsed(b.finalPosition, b.type);

		b.status = BuildingStatus::Assigned;

		//BWAPI::Broodwar->printf("worker assigned %d, %s", b.buildersSent, b.type.getName().c_str());
		Log().Debug() << "Assigned worker " << b.builderUnit->getID() << " to build " << b.type << " @ " << b.finalPosition;
	}
}

// STEP 3: ISSUE CONSTRUCTION ORDERS TO ASSIGNED BUILDINGS AS NEEDED
void BuildingManager::constructAssignedBuildings()
{
	// Profile debug
	//PROFILE_FUNCTION();

	for (Building & b : _buildings)
	{
		if (b.status != BuildingStatus::Assigned)
		{
			continue;
		}

		if (!b.builderUnit ||
			b.builderUnit->getPlayer() != BWAPI::Broodwar->self() ||
			(!b.builderUnit->exists() && b.type != BWAPI::UnitTypes::Zerg_Extractor))
		{
			//BWAPI::Broodwar->printf("b.builderUnit gone, worker unassigned, %s", b.type.getName().c_str());
			Log().Debug() << "Unassigned worker (b.builderUnit gone) to build " << b.type << " @ " << b.finalPosition;

			// NOTE A zerg drone builderUnit no longer exists() after starting an extractor.
			//      The geyser unit changes into the building, the drone vanishes.
			//      For other zerg buildings, the drone changes into the building.
			releaseBuilderUnit(b);

			b.builderUnit = nullptr;
			b.buildCommandGiven = false;
			b.status = BuildingStatus::Unassigned;
			BuildingPlacer::Instance().freeTiles(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
			// unreserve this building's space in BWEB
			BWEB::Map::removeUsed(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
		}

		// if that worker is not currently constructing
		else if (!b.builderUnit->isConstructing())
		{
			// Move towards the position if either:
			// - it hasn't been explored yet
			// - it is still far away
			bool moveToPosition = !isBuildingPositionExplored(b);
			/*
			if (!moveToPosition)
			{
				int distance = b.builderUnit->getDistance(BWAPI::Position(b.finalPosition));
				// The worker can not walk onto the geyser
				if (b.type == BWAPI::UnitTypes::Zerg_Extractor || 
					b.type == BWAPI::UnitTypes::Protoss_Assimilator ||
					b.type == BWAPI::UnitTypes::Terran_Refinery)
				{
					moveToPosition = distance > 160; //5x32
				}
				else
				{
					moveToPosition = distance > 128; //4x32
				}
			}*/

			if (moveToPosition)
			{
				/*if ((b.type == BWAPI::UnitTypes::Zerg_Hatchery || b.type == BWAPI::UnitTypes::Zerg_Extractor) &&
					WorkerManager::Instance().handleBlockingMineralsBuilders(b.builderUnit))
				{
					continue;
				}
				else
				{*/	
					//if (BWAPI::Broodwar->getFrameCount() % 2 == 0)
					//{
						bool move = Micro::SmartMove(b.builderUnit, BWAPI::Position(b.finalPosition));
						if(move)
							Log().Debug() << "Move builder unit " << b.builderUnit->getID() << " to build " << b.type << " @ " << b.finalPosition;
					//}
				//}
			}
			// if this is not the first time we've sent this guy to build this
			// it must be the case that something was in the way
			else if (b.buildCommandGiven)
			{
				// Give it a little time to happen.
				// This mainly prevents over-frequent retrying.
				if (BWAPI::Broodwar->getFrameCount() > b.placeBuildingDeadline)
				{
					//BWAPI::Broodwar->printf("b.buildCommandGiven true, worker unassigned, %s", b.type.getName().c_str());
					Log().Debug() << "Unassigned worker (b.buildCommandGiven true) " << b.builderUnit->getID() << " to build " << b.type << " @ " << b.finalPosition;

					// tell worker manager the unit we had is not needed now, since we might not be able
					// to get a valid location soon enough
					releaseBuilderUnit(b);
					// nullify its current builder unit
					b.builderUnit = nullptr;

					// reset the build command given flag
					b.buildCommandGiven = false;
					// add the building back to be unassigned
					b.status = BuildingStatus::Unassigned;
					b.buildFrame = 0;

					// This is a normal event that may happen repeatedly before the building starts.
					// Don't count it as a failure.
					--b.buildersSent;

					// free the previous location in reserved
					BuildingPlacer::Instance().freeTiles(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
					// free the previous location in reserved in BWEB
					BWEB::Map::removeUsed(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
				}
			}
			else
			{
				// Issue the build order and record whether it succeeded.
				// If the builderUnit is zerg, it changes to !exists() when it builds.
				b.buildCommandGiven = b.builderUnit->build(b.type, b.finalPosition);
				//BWAPI::Broodwar->printf("issue build order, %s", b.type.getName().c_str());

				Log().Debug() << "Gave build command worker " << b.builderUnit->getID() << " to build " << b.type << " @ "
					<< b.finalPosition << "; result " << b.buildCommandGiven;

				if (b.buildCommandGiven) {
					b.placeBuildingDeadline = 
						BWAPI::Broodwar->getFrameCount() + 2 * BWAPI::Broodwar->getLatencyFrames();
				}

				// If we can't currently build it, at least move to where we want to build it
				// Helps when we try to build something in a mineral line
				if (!b.buildCommandGiven) {
					//b.placeBuildingDeadline = BWAPI::Broodwar->getFrameCount() + 2 * BWAPI::Broodwar->getLatencyFrames();

					//b.builderUnit->move(BWAPI::Position(b.finalPosition) + BWAPI::Position(32, 32));				
					//if (BWAPI::Broodwar->getFrameCount() % 3 == 0)
					//{
						//Micro::SmartMove(b.builderUnit, BWAPI::Position(b.finalPosition) + BWAPI::Position(32, 32));
						//BWAPI::Broodwar->printf("b.buildCommandGiven false, move worker, %s", b.type.getName().c_str());
						//Log().Debug() << "Move worker (b.buildCommandGiven false) " << b.builderUnit->getID() << " to build " << b.type << " @ " << b.finalPosition;
					//}

					// Record the first frame we attempted to build, we will use this to detect timeouts
					if (b.buildFrame == 0) b.buildFrame = BWAPI::Broodwar->getFrameCount();
				}
			}
		}
	}
}

// STEP 4: UPDATE DATA STRUCTURES FOR BUILDINGS STARTING CONSTRUCTION
void BuildingManager::checkForStartedConstruction()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// for each building unit which is being constructed
	for (const BWAPI::Unit buildingStarted : BWAPI::Broodwar->self()->getUnits())
	{
		// filter out units which aren't buildings under construction
		if (!buildingStarted->getType().isBuilding() || !buildingStarted->isBeingConstructed())
		{
			continue;
		}

		// check all our building status objects to see if we have a match and if we do, update it
		for (Building & b : _buildings)
		{
			if (b.status != BuildingStatus::Assigned)
			{
				continue;
			}

			// check if the positions match
			if (b.finalPosition == buildingStarted->getTilePosition())
			{
				//BWAPI::Broodwar->printf("building under construction, %s", b.type.getName().c_str());
				Log().Get() << "Started building " << b.type << " @ " << b.finalPosition;

				// the resources should now be spent, so unreserve them
				_reservedMinerals -= buildingStarted->getType().mineralPrice();
				_reservedGas -= buildingStarted->getType().gasPrice();

				// flag it as started and set the buildingUnit
				b.underConstruction = true;
				b.buildingUnit = buildingStarted;

				// The building is started; handle zerg and protoss builders.
				// Terran builders are dealt with after the building finishes.
				if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg)
				{
					// If we are zerg, the builderUnit no longest exists.
					// If the building later gets canceled, a new drone will "mysteriously" appear.
					b.builderUnit = nullptr;
					// There's no drone to release, but we still want to let the ScoutManager know
					// that the gas steal is accomplished. If it's not a gas steal, this does nothing.
					releaseBuilderUnit(b);
				}
				// if we are protoss, give the worker back to worker manager
				else if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Protoss)
				{
					releaseBuilderUnit(b);
					b.builderUnit = nullptr;
				}

				// put it in the under construction vector
				b.status = BuildingStatus::UnderConstruction;

				// free this space
				BuildingPlacer::Instance().freeTiles(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());

				// only one building will match
				// We keep running the outer loop to look for more buildings starting construction.
				break;
			}
		}
	}
}

// STEP 5: IF THE SCV DIED DURING CONSTRUCTION, ASSIGN A NEW ONE
void BuildingManager::checkForDeadTerranBuilders()
{
	if (BWAPI::Broodwar->self()->getRace() != BWAPI::Races::Terran)
	{
		return;
	}

	// Profile debug
	//PROFILE_FUNCTION();

	for (Building & b : _buildings)
	{
		if (b.status != BuildingStatus::UnderConstruction)
		{
			continue;
		}

		UAB_ASSERT(b.buildingUnit, "null buildingUnit");

		if (!UnitUtil::IsValidUnit(b.builderUnit))
		{
			b.builderUnit = WorkerManager::Instance().getBuilder(b);
			if (b.builderUnit && b.builderUnit->exists())
			{
				b.builderUnit->rightClick(b.buildingUnit);
			}
		}
	}
}

// STEP 6: CHECK FOR COMPLETED BUILDINGS
// In case of terran gas steal, stop construction a little early,
// so we can cancel the refinery later and recover resources. 
// Zerg and protoss can't do that.
void BuildingManager::checkForCompletedBuildings()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// for each of our buildings under construction
	for (auto it = _buildings.begin(); it != _buildings.end(); )
	{
		auto & b = *it;

		if (b.status != BuildingStatus::UnderConstruction)
		{
			it++;
			continue;
		}

		UAB_ASSERT(b.buildingUnit, "null buildingUnit");

		// if the unit has completed
		if (b.buildingUnit->isCompleted())
		{
			Log().Debug() << "Completed building " << b.type << " @ " << b.finalPosition;

			// if we are terran, give the worker back to worker manager
			// Zerg and protoss are handled when the building starts.
			if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Terran)
			{
				releaseBuilderUnit(b);
			}

			// And we don't want to keep the building record any more.
			it = _buildings.erase(it);
			continue;
		}

		// The building, whatever it is, is not completed.
		// If it is a terran gas steal, stop construction early.
		if (b.isGasSteal &&
			b.type == BWAPI::UnitTypes::Terran_Refinery &&
			b.builderUnit &&
			b.builderUnit->canHaltConstruction() &&
			b.buildingUnit->getRemainingBuildTime() < 24)
		{
			b.builderUnit->haltConstruction();
			releaseBuilderUnit(b);

			// Call the building done. It's as finished as we want it to be.
			it = _buildings.erase(it);
			continue;
		}

		it++;
	}
}

// Error check: A bug in placing hatcheries can cause resources to be reserved and
// never released.
// We correct the values as a workaround, since the underlying bug has not been found.
void BuildingManager::checkReservedResources()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// Check for errors.
	int minerals = 0;
	int gas = 0;

	for (auto & b : _buildings)
	{
		if (b.status == BuildingStatus::Assigned || b.status == BuildingStatus::Unassigned)
		{
			minerals += b.type.mineralPrice();
			gas += b.type.gasPrice();
		}
	}

	if (minerals != _reservedMinerals || gas != _reservedGas)
	{
		Log().Get() << "Reserves wrong " << _reservedMinerals << " " << _reservedGas << " should be " << minerals << " " << gas;

		// This message should ideally never happen. If it does, we correct the error and carry on.
		//BWAPI::Broodwar->printf("reserves wrong: %d %d should be %d %d", _reservedMinerals, _reservedGas, minerals, gas);
		_reservedMinerals = minerals;
		_reservedGas = gas;
	}
}

// Add a new building to be constructed and return it.
Building & BuildingManager::addTrackedBuildingTask(const MacroAct & act, BWAPI::TilePosition desiredLocation, const BuildOrderItem & item)
{
	UAB_ASSERT(act.isBuilding(), "trying to build a non-building");

	BWAPI::UnitType type = act.getUnitType();

	_reservedMinerals += type.mineralPrice();
	_reservedGas += type.gasPrice();

	Building b(type, desiredLocation);
	b.macroAct = act;
	b.macroLocation = act.getMacroLocation();
	b.isGasSteal = item.isGasSteal;
	b.defenseLocation = item.defenseLocation;
	b.status = BuildingStatus::Unassigned;

	Log().Debug() << "Queued building task for " << type << " - gasSteal " << item.isGasSteal;

	_buildings.push_back(b);      // makes a "permanent" copy of the Building object
	return _buildings.back();     // return a reference to the permanent copy
}

// Add a new building to be constructed.
void BuildingManager::addBuildingTask(const MacroAct & act, BWAPI::TilePosition desiredLocation, const BuildOrderItem & item)
{
	(void)addTrackedBuildingTask(act, desiredLocation, item);
}

bool BuildingManager::isBuildingPositionExplored(const Building & b) const
{
	BWAPI::TilePosition tile = b.finalPosition;

	// for each tile where the building will be built
	for (int x = 0; x<b.type.tileWidth(); ++x)
	{
		for (int y = 0; y<b.type.tileHeight(); ++y)
		{
			if (!BWAPI::Broodwar->isExplored(tile.x + x, tile.y + y))
			{
				return false;
			}
		}
	}

	return true;
}

char BuildingManager::getBuildingWorkerCode(const Building & b) const
{
	return b.builderUnit == nullptr ? 'X' : 'W';
}

void BuildingManager::setBuilderUnit(Building & b)
{
	if (b.isGasSteal)
	{
		// If it's a gas steal, use the scout worker.
		// Even if other workers are close by, they may have different jobs.
		b.builderUnit = ScoutManager::Instance().getWorkerScout();
	}
	else
	{
		// Otherwise, grab the closest worker from WorkerManager.
		b.builderUnit = WorkerManager::Instance().getBuilder(b);
		if (b.builderUnit)
		{
			WorkerManager::Instance().setBuildingWorker(b.builderUnit, b.type);
		}
	}
}

// Notify the worker manager that the worker is free again,
// but not if the scout manager owns the worker.
void BuildingManager::releaseBuilderUnit(const Building & b)
{
	if (b.isGasSteal)
	{
		ScoutManager::Instance().gasStealOver();
	}
	else
	{
		if (b.builderUnit)
		{
			WorkerManager::Instance().finishedWithWorker(b.builderUnit);
		}
	}
}

int BuildingManager::getReservedMinerals() const
{
	return _reservedMinerals;
}

int BuildingManager::getReservedGas() const
{
	return _reservedGas;
}

// In the building queue with any status.
int BuildingManager::numBeingBuilt(BWAPI::UnitType type) const
{
	int result = 0;
	for (const auto & b : _buildings)
	{
		if (b.type == type)
		{
			result++;
		}
		else if (b.macroAct.hasThen())
		{
			auto & then = b.macroAct.getThen();
			if (then.isBuilding() && then.getUnitType() == type) result++;
		}
	}

	return result;
}

// In the building queue with any status.
bool BuildingManager::isBeingBuilt(BWAPI::UnitType type)
{
	return numBeingBuilt(type) > 0;
}

// Number in the building queue with status other than "under constrution".
size_t BuildingManager::getNumUnstarted() const
{
	size_t count = 0;

	for (const Building & b : _buildings)
	{
		if (b.status != BuildingStatus::UnderConstruction)
		{
			++count;
		}
	}

	return count;
}

// Number of a given type in the building queue with status other than "under constrution".
size_t BuildingManager::getNumUnstarted(BWAPI::UnitType type) const
{
	size_t count = 0;

	for (const Building & b : _buildings)
	{
		if (b.type == type && b.status != BuildingStatus::UnderConstruction)
		{
			++count;
		}
	}

	return count;
}

// Number of a given type in the building queue with status "under constrution".
size_t BuildingManager::getNumStarted(BWAPI::UnitType type) const
{
	size_t count = 0;

	for (const Building & b : _buildings)
	{
		if (b.type == type && b.status == BuildingStatus::UnderConstruction)
		{
			++count;
		}
	}

	return count;
}

// The set of buildings queued and not yet started.
std::vector<BWAPI::UnitType> BuildingManager::buildingsQueued()
{
	std::vector<BWAPI::UnitType> buildingsQueued;

	for (const Building & b : _buildings)
	{
		if (b.status == BuildingStatus::Unassigned || b.status == BuildingStatus::Assigned)
		{
			buildingsQueued.push_back(b.type);
		}
	}

	return buildingsQueued;
}

bool BuildingManager::isGasStealInQueue() const
{
	for (const Building & b : _buildings)
	{
		if (b.isGasSteal)
		{
			return true;
		}
	}

	return false;
}

void BuildingManager::drawBuildingInformation(int x, int y)
{
	if (!Config::Debug::DrawBuildingInfo)
	{
		return;
	}

	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		BWAPI::Broodwar->drawTextMap(unit->getPosition().x, unit->getPosition().y + 5, "\x07%d", unit->getID());
	}

	for (auto geyser : BWAPI::Broodwar->getAllUnits())
	{
		if (geyser->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser)
		{
			BWAPI::Broodwar->drawTextMap(geyser->getPosition().x, geyser->getPosition().y + 5, "\x07%d", geyser->getType());
		}
	}

	BWAPI::Broodwar->drawTextScreen(x, y, "\x04 Building Information:");
	BWAPI::Broodwar->drawTextScreen(x, y + 10, "\x04 Name");
	BWAPI::Broodwar->drawTextScreen(x + 150, y + 10, "\x04 State");

	int yspace = 0;

	for (const Building & b : _buildings)
	{
		if (b.status == BuildingStatus::Unassigned)
		{
			int x1 = b.desiredPosition.x * 32;
			int y1 = b.desiredPosition.y * 32;
			int x2 = (b.desiredPosition.x + b.type.tileWidth()) * 32;
			int y2 = (b.desiredPosition.y + b.type.tileHeight()) * 32;

			BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Green, false);
			BWAPI::Broodwar->drawTextScreen(x, y + 20 + ((yspace) * 10), "\x03 %s", b.type.getName().c_str());
			BWAPI::Broodwar->drawTextScreen(x + 150, y + 20 + ((yspace++) * 10), "\x03 Need %c", getBuildingWorkerCode(b));
		}
		else if (b.status == BuildingStatus::Assigned)
		{
			BWAPI::Broodwar->drawTextScreen(x, y + 20 + ((yspace) * 10), "\x03 %s %d", b.type.getName().c_str(), b.builderUnit->getID());
			BWAPI::Broodwar->drawTextScreen(x + 150, y + 20 + ((yspace++) * 10), "\x03 Assign %c (%d,%d)", getBuildingWorkerCode(b), b.finalPosition.x, b.finalPosition.y);

			int x1 = b.finalPosition.x * 32;
			int y1 = b.finalPosition.y * 32;
			int x2 = (b.finalPosition.x + b.type.tileWidth()) * 32;
			int y2 = (b.finalPosition.y + b.type.tileHeight()) * 32;

			BWAPI::Broodwar->drawLineMap(b.builderUnit->getPosition().x, b.builderUnit->getPosition().y, (x1 + x2) / 2, (y1 + y2) / 2, BWAPI::Colors::Orange);
			BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Red, false);
		}
		else if (b.status == BuildingStatus::UnderConstruction)
		{
			BWAPI::Broodwar->drawTextScreen(x, y + 20 + ((yspace) * 10), "\x03 %s %d", b.type.getName().c_str(), b.buildingUnit->getID());
			BWAPI::Broodwar->drawTextScreen(x + 150, y + 20 + ((yspace++) * 10), "\x03 Const %c", getBuildingWorkerCode(b));
		}
	}
}

// TODO fails in placing a hatchery after all others are destroyed - why?
BWAPI::TilePosition BuildingManager::getBuildingLocation(const Building & b)
{
	// Profile debug
	//PROFILE_FUNCTION();

	if (b.isGasSteal)
	{
		const BWEM::Base * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();
		UAB_ASSERT(enemyBaseLocation, "Should find enemy base before gas steal");
		UAB_ASSERT(enemyBaseLocation->Geysers().size() > 0, "Should have spotted an enemy geyser");

		for (auto & unit : enemyBaseLocation->Geysers())
		{
			return unit->TopLeft();
		}
	}

	int numPylons = BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Pylon);
	if (b.type.requiresPsi() && numPylons == 0)
	{
		return BWAPI::TilePositions::None;
	}

	if (b.type.isRefinery())
	{
		return BuildingPlacer::Instance().getRefineryPosition();
	}

	if (b.type.isResourceDepot())
	{
		if (b.macroLocation == MacroLocation::Anywhere ||
			b.macroLocation == MacroLocation::Expo ||
			b.macroLocation == MacroLocation::Hidden ||
			b.macroLocation == MacroLocation::MinOnly)
		{
			return MapTools::Instance().getNextExpansion(b.macroLocation == MacroLocation::Hidden, b.macroLocation == MacroLocation::MinOnly);
		}
		// Else treat it like any other building.
	}

	int distance = Config::Macro::BuildingSpacing;

	const BWEM::Base * ourMainLocation = InformationManager::Instance().getMyMainBaseLocation();
	int numHatchMain = InformationManager::Instance().getNumBuildings(ourMainLocation, BWAPI::UnitTypes::Zerg_Hatchery);
	int numLairMain = InformationManager::Instance().getNumBuildings(ourMainLocation, BWAPI::UnitTypes::Zerg_Lair);
	int numHiveMain = InformationManager::Instance().getNumBuildings(ourMainLocation, BWAPI::UnitTypes::Zerg_Hive);

	if (b.type == BWAPI::UnitTypes::Terran_Bunker ||
		b.type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
		b.type == BWAPI::UnitTypes::Zerg_Creep_Colony) {
		// Pack defenses tightly together.
		distance = 0;
		// Get a position toward the choke or mineral line
		BWAPI::TilePosition tile = BuildingPlacer::Instance().getDefenseBuildLocationNear(b, distance);
		return tile;
	}
	else if (b.type == BWAPI::UnitTypes::Protoss_Pylon)
	{
		if (numPylons < 3)
		{
			// Early pylons may be spaced differently than other buildings.
			distance = Config::Macro::PylonSpacing;
		}
		else
		{
			// Building spacing == 1 is usual. Be more generous with pylons.
			distance = 2;
		}
	}
	else if (b.type == BWAPI::UnitTypes::Zerg_Spawning_Pool ||
		b.type == BWAPI::UnitTypes::Zerg_Hydralisk_Den)
	{
		// Pack the first few zerg buildings to make room on limited creep
		distance = 0;
	}
	else if (b.type == BWAPI::UnitTypes::Zerg_Hatchery && numHatchMain + numLairMain + numHiveMain == 1 &&
		(b.macroLocation == MacroLocation::Macro || b.macroLocation == MacroLocation::Main))
	{
		// The first hatchery in main.
		distance = 1;
	}
	else if (b.type == BWAPI::UnitTypes::Zerg_Hatchery &&
		b.macroLocation == MacroLocation::Natural)
	{
		// Pack simcity hatcheries in natural
		distance = 0;
	}
	else if (b.type == BWAPI::UnitTypes::Zerg_Hatchery &&
		b.macroLocation == MacroLocation::Third)
	{
		// Pack simcity hatcheries in third
		distance = 0;
	}
	else if (b.defenseLocation == DefenseLocation::Chokepoint || 
		b.defenseLocation == DefenseLocation::Minerals)
	{
		// Pack defenses tightly together.
		distance = 0;
	}

	// Try to pack protoss buildings more closely together. Space can run out.
	bool horizontalOnly = false;

	if (b.type == BWAPI::UnitTypes::Protoss_Gateway ||
		b.type == BWAPI::UnitTypes::Protoss_Forge ||
		b.type == BWAPI::UnitTypes::Protoss_Stargate ||
		b.type == BWAPI::UnitTypes::Protoss_Citadel_of_Adun ||
		b.type == BWAPI::UnitTypes::Protoss_Templar_Archives ||
		b.type == BWAPI::UnitTypes::Protoss_Gateway)
	{
		horizontalOnly = true;
	}

	// Get a position within our region.
	BWAPI::TilePosition tile = BuildingPlacer::Instance().getBuildLocationNear(b, distance, horizontalOnly);

	if (tile == BWAPI::TilePositions::None)
	{
		//Log().Get() << "Failed to find location " << b.type << " macrolocation " << 
		//	MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
	}

	return tile;
}

// It's an emergency. Cancel all buildings which are not yet started.
void BuildingManager::cancelQueuedBuildings()
{
	for (Building & b : _buildings)
	{
		if (b.status == BuildingStatus::Unassigned || b.status == BuildingStatus::Assigned)
		{
			cancelBuilding(b);
		}
	}
}

// It's an emergency. Cancel all buildings of a given type.
void BuildingManager::cancelBuildingType(BWAPI::UnitType t)
{
	for (Building & b : _buildings)
	{
		if (b.type == t)
		{
			cancelBuilding(b);
		}
	}
}

// Cancel a given building when possible.
// Used as part of the extractor trick or in an emergency.
// NOTE CombatCommander::cancelDyingBuildings() can also cancel buildings, including
//      morphing zerg structures which the BuildingManager does not handle.
void BuildingManager::cancelBuilding(Building & b)
{
	if (b.status == BuildingStatus::Unassigned)
	{
		undoBuilding(b);
	}
	else if (b.status == BuildingStatus::Assigned)
	{
		undoBuilding(b);
	}
	else if (b.status == BuildingStatus::UnderConstruction)
	{
		if (b.buildingUnit && b.buildingUnit->exists() && !b.buildingUnit->isCompleted())
		{
			b.buildingUnit->cancelConstruction();
		}
		undoBuilding(b);
	}
	else
	{
		UAB_ASSERT(false, "unexpected building status");
	}

	removeBuildings({b});
}

// The building failed or is canceled.
// Undo any connections with other data structures, then delete.
void BuildingManager::undoBuilding(Building& b)
{
	// If the building was to establish a base, unreserve the base location.
	if (b.type.isResourceDepot() &&
		(b.macroLocation != MacroLocation::Macro || b.macroLocation != MacroLocation::Main) &&
		b.finalPosition.isValid())
	{
		InformationManager::Instance().unreserveBase(b.finalPosition);
	}

	// Free reserved tiles
	if (b.finalPosition.isValid())
	{
		BuildingPlacer::Instance().freeTiles(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
		// Free reserved tiles in BWEB
		BWEB::Map::removeReserve(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
	}

	// If the building is not yet under construction, release its resources.
	if (b.status == BuildingStatus::Unassigned || b.status == BuildingStatus::Assigned)
	{
		_reservedMinerals -= b.type.mineralPrice();
		_reservedGas -= b.type.gasPrice();
	}

	// Cancel a terran building under construction. Zerg and protoss finish on their own,
	// but terran needs another SCV to be sent, and it won't happen.
	if (b.buildingUnit &&
		b.buildingUnit->getType().getRace() == BWAPI::Races::Terran &&
		b.buildingUnit->exists() &&
		b.buildingUnit->canCancelConstruction())
	{
		b.buildingUnit->cancelConstruction();
	}

	// Release the worker, if necessary.
	releaseBuilderUnit(b);
	b.builderUnit = nullptr;
}

// Remove buildings from the list of buildings--nothing more, nothing less.
void BuildingManager::removeBuildings(const std::vector< std::reference_wrapper<Building> > & toRemove)
{
	for (Building & b : toRemove)
	{
		auto it = std::find(_buildings.begin(), _buildings.end(), b);

		if (it != _buildings.end())
		{
			_buildings.erase(it);
		}
	}
}


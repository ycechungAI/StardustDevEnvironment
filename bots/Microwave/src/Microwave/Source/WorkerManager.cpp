#include "Common.h"
#include "WorkerManager.h"
#include "Micro.h"
#include "ProductionManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

WorkerManager::WorkerManager() 
{
    previousClosestWorker = nullptr;
	previousClosestBlockingMinWorker = nullptr;
	_collectGas = false;
}

WorkerManager & WorkerManager::Instance() 
{
	static WorkerManager instance;
	return instance;
}

bool WorkerManager::isBusy(BWAPI::Unit worker) const
{
	return busy.contains(worker);
}

void WorkerManager::makeBusy(BWAPI::Unit worker)
{
	busy.insert(worker);
}

void WorkerManager::update() 
{
	// Profile debug
	//PROFILE_FUNCTION();

	// NOTE Combat workers are placed in a combat squad and get their orders there.
	//      We ignore them here.
	updateWorkerStatus();

	handleGasWorkers();
	handleIdleWorkers();
	handleReturnCargoWorkers();
	handleMoveWorkers();
	handleRepairWorkers();
	handleMineralLocking(); // Do this last since the workers might get reassigned elsewhere first

	drawResourceDebugInfo();
	drawWorkerInformation(500,20);

	workerData.drawDepotDebugInfo();

	// Workers are not busy except while WorkerManager::update() is running, so that
	// other modules can steal workers (which can't be stolen when busy).
	busy.clear();
}

// Adjust worker jobs. This is done first, before handling each job.
// NOTE A mineral worker may go briefly idle after collecting minerals.
// That's OK; we don't change its status then.
void WorkerManager::updateWorkerStatus()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// If any buildings are due for construction, assume that builders are not idle.
	const bool catchIdleBuilders =
		!BuildingManager::Instance().anythingBeingBuilt() &&
		!ProductionManager::Instance().nextIsBuilding();

	// for each of our Workers
	for (auto & worker : workerData.getWorkers())
	{
		if (!worker ||
			!worker->getType().isWorker() ||
			!worker->exists() ||
			worker->getPlayer() != BWAPI::Broodwar->self())
		{
			UAB_ASSERT(false, "bad worker");
			continue;
		}

		if (!worker->isCompleted())
		{
			continue;     // the worker list includes drones in the egg
		}

		if (isBusy(worker))
		{
			continue;
		}

		// Workers with a combat order are managed fully by CombatCommander
		if (workerData.getWorkerJob(worker) == WorkerData::Combat) continue;

		// If it's supposed to be on minerals but is actually collecting gas, fix it.
		// This can happen when we stop collecting gas; the worker can be mis-assigned.
		if (workerData.getWorkerJob(worker) == WorkerData::Minerals &&
			(worker->getOrder() == BWAPI::Orders::MoveToGas ||
			worker->getOrder() == BWAPI::Orders::WaitForGas ||
			worker->getOrder() == BWAPI::Orders::ReturnGas))
		{
			Log().Debug() << "Idle1: Worker " << worker->getID() << " set to idle, worker job was minerals, worker order was " << worker->getOrder();
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}

		// Work around a bug that can cause building drones to go idle.
		// If there should be no builders, then ensure any idle drone is marked idle.
		if (catchIdleBuilders &&
			worker->getOrder() == BWAPI::Orders::PlayerGuard &&
			(workerData.getWorkerJob(worker) == WorkerData::Move || workerData.getWorkerJob(worker) == WorkerData::Build))
		{
			Log().Debug() << "Idle2: Worker " << worker->getID() << " set to idle, worker job was " << workerData.getWorkerJob(worker) << ", worker order was " << worker->getOrder();
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}

		// The worker's original job. It may change as we go!
		WorkerData::WorkerJob job = workerData.getWorkerJob(worker);

		// Idleness.
		// Order can be PlayerGuard for a drone that tries to build and fails.
		// There are other causes, e.g. being done with a job like repair.
		if ((worker->isIdle() || worker->getOrder() == BWAPI::Orders::PlayerGuard) &&
			job != WorkerData::Minerals &&
			job != WorkerData::Build &&
			job != WorkerData::Move &&
			job != WorkerData::Scout)
		{
			Log().Debug() << "Idle3: Worker " << worker->getID() << " set to idle, worker job was " << workerData.getWorkerJob(worker) << ", worker order was " << worker->getOrder();
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}

		// If the worker is busy mining gas and an enemy comes near, maybe fight it.
		else if (job == WorkerData::Gas)
		{
			// Special micro eg. storm dodge
			//if (Micro::CheckSpecialCases(worker)) continue;

			BWAPI::Unit refinery = workerData.getWorkerResource(worker);

			// If the refinery is gone.
			// A missing resource depot is dealt with in handleGasWorkers().
			if (!refinery || 
				!refinery->exists() || 
				refinery->getHitPoints() == 0 ||
				!refinery->getType().isRefinery() ||
				refinery->getPlayer() != BWAPI::Broodwar->self())
			{
				workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
			}
			else if (defendSelf(worker, workerData.getWorkerResource(worker)))
			{
				// defendSelf() does the work.
			}
			else if (worker->getOrder() != BWAPI::Orders::MoveToGas &&
				worker->getOrder() != BWAPI::Orders::WaitForGas &&
				worker->getOrder() != BWAPI::Orders::HarvestGas &&
				worker->getOrder() != BWAPI::Orders::ReturnGas &&
				worker->getOrder() != BWAPI::Orders::ResetCollision)
			{
				// The worker is not actually mining. Release it.
				workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
			}

		}

		// If the worker is busy mining minerals and an enemy comes near, maybe fight it.
		else if (job == WorkerData::Minerals)
		{
			// Special micro eg. storm dodge
			//if (Micro::CheckSpecialCases(worker)) continue;

			if (defendSelf(worker, workerData.getWorkerResource(worker)))
			{
				// defendSelf() does the work.
			}
			else if (worker->getOrder() == BWAPI::Orders::MoveToMinerals ||
				worker->getOrder() == BWAPI::Orders::WaitForMinerals)
			{
				// If the mineral patch is mined out, release the worker from its job.
				BWAPI::Unit patch = workerData.getWorkerResource(worker);
				if (!patch || (patch && !patch->exists()))
				{
					workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
				}
			}
			else if (
				worker->getOrder() != BWAPI::Orders::MoveToMinerals &&
				worker->getOrder() != BWAPI::Orders::WaitForMinerals &&
				worker->getOrder() != BWAPI::Orders::MiningMinerals &&
				worker->getOrder() != BWAPI::Orders::ReturnMinerals &&
				worker->getOrder() != BWAPI::Orders::ResetCollision)
			{
				// The worker is not actually mining. Release it.
				workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
			}
		}
	}
}

void WorkerManager::setRepairWorker(BWAPI::Unit worker, BWAPI::Unit unitToRepair)
{
    workerData.setWorkerJob(worker, WorkerData::Repair, unitToRepair);
}

void WorkerManager::stopRepairing(BWAPI::Unit worker)
{
    workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
}

// Move gas workers on or off gas as necessary.
// NOTE A worker inside a refinery does not accept orders.
void WorkerManager::handleGasWorkers() 
{
	// Profile debug
	//PROFILE_FUNCTION();

	// Don't collect gas if gas collection is off, or if the resource depot is missing.
	if (_collectGas)
	{
		// Gather gas where possible. Check each refinery.
		for (const auto refinery : BWAPI::Broodwar->self()->getUnits())
		{
			if (refinery->getType().isRefinery() && 
				refinery->isCompleted() &&
				refinery->getPlayer() == BWAPI::Broodwar->self())
			{
				if (refineryHasDepot(refinery))
				{
					// Gather gas: If too few are assigned to this refinery, add more.
					int numAssigned = workerData.getNumAssignedWorkers(refinery);
					for (int i = 0; i < (Config::Macro::WorkersPerRefinery - numAssigned); ++i)
					{
						BWAPI::Unit gasWorker = getGasWorker(refinery);
						if (gasWorker)
						{
							workerData.setWorkerJob(gasWorker, WorkerData::Gas, refinery);
						}
						else 
						{
							return;    // won't find any more
						}
					}
				}
				else
				{
					// Don't gather gas: if this refinery does not have a depot, remove workers from it
					std::set<BWAPI::Unit> gasWorkers;
					workerData.getGasWorkers(gasWorkers);
					for (auto gasWorker : gasWorkers)
					{
						if (refinery == workerData.getWorkerResource(gasWorker) &&
							gasWorker->getOrder() != BWAPI::Orders::HarvestGas)    // not inside the refinery
						{
							workerData.setWorkerJob(gasWorker, WorkerData::Idle, nullptr);
						}
					}
				}
			}
		}
	}
	else
	{
		// Don't gather gas: If any workers are assigned, take them off.
		std::set<BWAPI::Unit> gasWorkers;
		workerData.getGasWorkers(gasWorkers);
		for (const auto gasWorker : gasWorkers)
		{
			if (gasWorker->getOrder() != BWAPI::Orders::HarvestGas)    // not inside the refinery
			{
				workerData.setWorkerJob(gasWorker, WorkerData::Idle, nullptr);
				// An idle worker carrying gas will become a ReturnCargo worker,
				// so gas will not be lost needlessly.
			}
		}
	}
}

// Is our refinery near a resource depot that it can deliver gas to?
bool WorkerManager::refineryHasDepot(BWAPI::Unit refinery)
{
	// Iterate through units, not bases, because even if the main hatchery is destroyed
	// (so the base is considered gone), a macro hatchery may be close enough.
	// TODO could iterate through bases (from InfoMan) instead of units
	for (const auto unit : BWAPI::Broodwar->self()->getUnits())
	{
		if (unit->getType().isResourceDepot() &&
			(unit->isCompleted() || 
			unit->getType() == BWAPI::UnitTypes::Zerg_Lair ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Hive) &&
			unit->getDistance(refinery) < 400)
		{
			return true;
		}
	}

	return false;
}

void WorkerManager::handleIdleWorkers() 
{
	// Profile debug
	//PROFILE_FUNCTION();

	for (const auto worker : workerData.getWorkers())
	{
        UAB_ASSERT(worker, "Worker was null");

		// if it is idle
		if (worker->isCompleted() && 
			workerData.getWorkerJob(worker) == WorkerData::Idle)
		{
			if (worker->isCarryingMinerals() || worker->isCarryingGas()) 
			{
				// It's carrying something, set it to hand in its cargo.
				setReturnCargoWorker(worker);         // only happens if there's a resource depot
			}
			else {
				// Otherwise send it to mine minerals.
				setMineralWorker(worker);             // only happens if there's a resource depot
			}
		}
	}
}

void WorkerManager::handleReturnCargoWorkers()
{
	// Profile debug
	//PROFILE_FUNCTION();

	for (const auto worker : workerData.getWorkers())
	{
		UAB_ASSERT(worker, "Worker was null");

		if (workerData.getWorkerJob(worker) == WorkerData::ReturnCargo)
		{
			// Special micro eg. storm dodge
			//if (Micro::CheckSpecialCases(worker)) continue;

			// If it still needs to return cargo, return it; otherwise go idle.
			// We have to make sure it has a resource depot to return cargo to.
			BWAPI::Unit depot;
			if ((worker->isCarryingMinerals() || worker->isCarryingGas()) &&
				(depot = getAnyClosestDepot(worker)) &&
				worker->getDistance(depot) < 600)
			{
				bool returnCargo = Micro::SmartReturnCargo(worker);
				makeBusy(worker);
				if (returnCargo)
				{
					Log().Debug() << "Return cargo worker " << worker->getID() << " return cargo";
				}
			}
			else
			{
				// Can't return cargo. Let's be a mineral worker instead--if possible.
				setMineralWorker(worker);
				Log().Debug() << "Return cargo worker " << worker->getID() << " set to mineral";
			}
		}
	}
}

void WorkerManager::handleMoveWorkers()
{
	// Profile debug
	//PROFILE_FUNCTION();

	for (const auto worker : workerData.getWorkers())
	{
		UAB_ASSERT(worker, "Worker was null");

		// if it is a move worker
		if (workerData.getWorkerJob(worker) == WorkerData::Move)
		{
			BWAPI::Unit depot;
			if ((worker->isCarryingMinerals() || worker->isCarryingGas()) &&
				(depot = getAnyClosestDepot(worker)) &&
				worker->getDistance(depot) <= 256)
			{
				// A move worker is being sent to build or something.
				// Don't let it carry minerals or gas around wastefully.
				bool returnCargo = Micro::SmartReturnCargo(worker);
				makeBusy(worker);
				if (returnCargo)
				{
					Log().Debug() << "Move worker " << worker->getID() << " return cargo";
				}
			}
			else
			{
				WorkerMoveData data = workerData.getWorkerMoveData(worker);
				bool move = Micro::SmartMove(worker, data.position);
				if (move)
				{
					Log().Debug() << "Move worker " << worker->getID() << " to position @ " << BWAPI::TilePosition(data.position);
				}
			}
		}
	}
}

// Terran can assign SCVs to repair.
void WorkerManager::handleRepairWorkers()
{
	// Profile debug
	//PROFILE_FUNCTION();

    if (BWAPI::Broodwar->self()->getRace() != BWAPI::Races::Terran)
    {
        return;
    }

    for (const auto unit : BWAPI::Broodwar->self()->getUnits())
    {
        if (unit->getType().isBuilding() && (unit->getHitPoints() < unit->getType().maxHitPoints()))
        {
            BWAPI::Unit repairWorker = getClosestMineralWorkerTo(unit);
            setRepairWorker(repairWorker, unit);
            break;
        }
    }
}

void WorkerManager::handleMineralLocking()
{
	// Profile debug
	//PROFILE_FUNCTION();

	for (const auto worker : workerData.getWorkers())
	{
		// Check for valid worker
		/*if (!worker || !worker->exists())
		{
			continue;
		}*/

		// If the worker is not on minerals, leave it alone
		if (workerData.getWorkerJob(worker) != WorkerData::Minerals)
		{
			continue;
		}

		// Get mineral patch
		BWAPI::Unit patch = workerData.getWorkerResource(worker);
		if (!patch || (patch && !patch->exists()))
		{
			continue;
		}
		/*
		// If the worker is currently mining, leave it alone
		if (worker->getOrder() == BWAPI::Orders::MiningMinerals ||
			worker->getOrder() == BWAPI::Orders::ResetCollision)
		{
			continue;
		}

		// If the worker is returning cargo, leave it alone
		if (worker->getOrder() == BWAPI::Orders::ReturnMinerals)
		{
			continue;
		}
		
		// Check if another worker is currently mining this patch
		BWAPI::Unit otherWorker = nullptr;
		for (const auto other : workerData.getWorkers())
		{
			if (other == worker) continue;
			if (workerData.getWorkerResource(other) == patch)
			{
				otherWorker = other;
				break;
			}
		}

		// Resend the gather command when we expect the other worker to be finished mining in 9+LF frames
		if (otherWorker && otherWorker->getOrder() == BWAPI::Orders::MiningMinerals &&
			(otherWorker->getOrderTimer() + 7) == (10 + BWAPI::Broodwar->getLatencyFrames()))
		{
			// Exception: If we are not at the patch yet, and our last command was sent a long time ago,
			// we probably want to wait and allow our order timer optimization to send the command instead.
			int dist = worker->getDistance(patch);
			if (dist > 20 && worker->getLastCommandFrame() < (BWAPI::Broodwar->getFrameCount() - 20)) continue;

			// Log().Debug() << worker->getID() << ": mp=" << patch->getID() << "; resent because other worker soon to finish";
			Micro::MineMinerals(worker, patch);
			makeBusy(worker);
			continue;
		}
		*/

		// Mineral locking: if the unit is moving to or waiting for minerals, make sure it doesn't switch targets
		if (worker->getOrder() == BWAPI::Orders::MoveToMinerals ||
			worker->getOrder() == BWAPI::Orders::WaitForMinerals)
		{
			if (worker->getOrderTarget() && 
				worker->getOrderTarget()->getResources() && 
				worker->getOrderTarget() != patch && 
				worker->getLastCommandFrame() < (BWAPI::Broodwar->getFrameCount() - BWAPI::Broodwar->getLatencyFrames()))
			{
				// Log().Debug() << worker->getID() << ": mp=" << patch->getID() << "; resent because switched target";
				Micro::MineMinerals(worker, patch);
				makeBusy(worker);
			}

			continue;
		}

		// Otherwise for all other orders click on the mineral patch
		// Log().Debug() << worker->getID() << ": mp=" << patch->getID() << "; sent because fell through";
		//Micro::MineMinerals(worker, patch);
		//makeBusy(worker);
	}
}

// Used for worker self-defense.
// Only include enemy units within 64 pixels that can be targeted by workers
// and are not moving or are stuck and moving randomly to dislodge themselves.
BWAPI::Unit WorkerManager::findEnemyTargetForWorker(BWAPI::Unit worker) const
{
	UAB_ASSERT(worker, "Worker was null");

	BWAPI::Unit closestUnit = nullptr;
	int closestDist = 65;         // ignore anything farther away

	for (const auto unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		int dist;

		if (unit->isVisible() &&
			(!unit->isMoving() || unit->isBraking() || unit->isStuck()) &&
			unit->getPosition().isValid() &&
			(dist = unit->getDistance(worker)) < closestDist &&
			!unit->isFlying() &&
			unit->isCompleted() &&
			unit->isDetected() &&
			!unit->getType().isBuilding())
		{
			closestUnit = unit;
			closestDist = dist;
		}
	}

	return closestUnit;
}

// The worker is defending itself and wants to mineral walk out of trouble.
// Find a suitable mineral patch, if any.
BWAPI::Unit WorkerManager::findEscapeMinerals(BWAPI::Unit worker) const
{
	BWAPI::Unit farthestMinerals = nullptr;
	int farthestDist = 64;           // ignore anything closer

	for (const auto unit : BWAPI::Broodwar->getNeutralUnits())
	{
		int dist;

		if (unit->getType() == BWAPI::UnitTypes::Resource_Mineral_Field &&
			//unit->isVisible() &&
			(dist = worker->getDistance(unit)) < 400 &&
			dist > farthestDist)
		{
			farthestMinerals = unit;
			farthestDist = dist;
		}
	}

	return farthestMinerals;
}

// If appropriate, order the worker to defend itself.
// The "resource" is workerData.getWorkerResource(worker), passed in so it needn't be looked up again.
// Return whether self-defense was undertaken.
bool WorkerManager::defendSelf(BWAPI::Unit worker, BWAPI::Unit resource)
{
	// We want to defend ourselves if we are near home and we have a close enemy (the target).
	BWAPI::Unit target = findEnemyTargetForWorker(worker);

	if (resource && worker->getDistance(resource) < 200 && target)
	{
		int enemyWeaponRange = UnitUtil::GetAttackRange(target, worker);
		bool flee =
			enemyWeaponRange > 0 &&          // don't flee if the target can't hurt us
			enemyWeaponRange <= 32 &&        // no use to flee if the target has range
			worker->getHitPoints() <= 16;    // reasonable value for the most common attackers
			// worker->getHitPoints() <= UnitUtil::GetWeaponDamageToWorker(target);

		// TODO It's not helping. Reaction time is too slow.
		flee = false;

		if (flee)
		{
			// 1. We're in danger of dying. Flee by mineral walk.
			BWAPI::Unit escapeMinerals = findEscapeMinerals(worker);
			if (escapeMinerals)
			{
				//BWAPI::Broodwar->printf("%d fleeing to %d", worker->getID(), escapeMinerals->getID());
				workerData.setWorkerJob(worker, WorkerData::Minerals, escapeMinerals);
				return true;
			}
			else
			{
				//BWAPI::Broodwar->printf("%d cannot flee", worker->getID());
			}
		}

		// 2. We do not want to or are not able to run away. Fight.
		Micro::SmartAttackUnit(worker, target);
		makeBusy(worker);
		return true;
	}

	return false;
}

// Used for combat workers
// Only count enemy units that can be targeted by workers.
BWAPI::Unit WorkerManager::getBestEnemyUnit(BWAPI::Unit worker)
{
	UAB_ASSERT(worker != nullptr, "Worker was null");

	BWAPI::Unit closestUnit = nullptr;
	int dist = 9999;
	int closestDist = 9999;         // ignore anything farther away

	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->isFlying())
		{
			continue;
		}

		dist = unit->getDistance(worker);
		if (!unit->isMoving() || unit->isBraking() || unit->isStuck())
		{
			dist -= 128;
		}
		if (unit->getType().topSpeed() >= worker->getType().topSpeed())
		{
			dist += 64;
		}
		if (dist < closestDist)
		{
			closestUnit = unit;
			closestDist = dist;
		}
	}

	return closestUnit;
}

// Used for worker self-defense.
// Only count enemy units that can be targeted by workers.
BWAPI::Unit WorkerManager::getClosestEnemyUnit(BWAPI::Unit worker)
{
	UAB_ASSERT(worker != nullptr, "Worker was null");

	BWAPI::Unit closestUnit = nullptr;
	int closestDist = 1000;         // ignore anything farther away

	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->isFlying())
		{
			continue;
		}

		int dist = unit->getDistance(worker);
		if (dist < closestDist)
		{
			closestUnit = unit;
			closestDist = dist;
		}
	}

	return closestUnit;
}

BWAPI::Unit WorkerManager::getClosestMineralWorkerTo(BWAPI::Unit unit)
{
    UAB_ASSERT(unit, "Unit was null");

    BWAPI::Unit closestMineralWorker = nullptr;
    int closestDist = 100000;

	// Former closest worker may have died or (if zerg) morphed into a building.
	if (UnitUtil::IsValidUnit(previousClosestWorker) && previousClosestWorker->getType().isWorker())
	{
		return previousClosestWorker;
	}

    // for each of our workers
	for (const auto worker : workerData.getWorkers())
	{
        UAB_ASSERT(worker != nullptr, "Worker was null");

		if (isFree(worker))
		{
			int dist = worker->getDistance(unit);
			if (worker->isCarryingMinerals() || worker->isCarryingGas()) 
			{
				// If it has cargo, pretend it is farther away.
				// That way we prefer empty workers and lose less cargo.
				dist += 999;
			}

            if (dist < closestDist)
            {
                closestMineralWorker = worker;
                dist = closestDist;
            }
		}
	}

    previousClosestWorker = closestMineralWorker;
    return closestMineralWorker;
}

BWAPI::Unit WorkerManager::getClosestBlockingMinWorkerTo(BWAPI::Unit unit)
{
	UAB_ASSERT(unit != nullptr, "Unit was null");

	BWAPI::Unit closestWorker = nullptr;
	int closestDist = 100000;

	// Former closest worker may have died or (if zerg) morphed into a building.
	if (UnitUtil::IsValidUnit(previousClosestBlockingMinWorker) && previousClosestBlockingMinWorker->getType().isWorker())
	{
		return previousClosestBlockingMinWorker;
	}

	// for each of our workers
	for (auto & worker : workerData.getWorkers())
	{
		UAB_ASSERT(worker != nullptr, "Worker was null");

		// if it is a mineral worker
		if (worker->isCompleted() &&
			(workerData.getWorkerJob(worker) == WorkerData::Minerals ||
			workerData.getWorkerJob(worker) == WorkerData::Idle))
		{
			int dist = worker->getDistance(unit);
			if (!worker->isCarryingMinerals() && !worker->isCarryingGas() && 
				dist < closestDist && dist < 1200)
			{
				closestWorker = worker;
				closestDist = dist;
			}
		}
	}

	previousClosestBlockingMinWorker = closestWorker;
	return closestWorker;
}

BWAPI::Unit WorkerManager::getWorkerScout()
{
	for (const auto worker : workerData.getWorkers())
	{
		UAB_ASSERT(worker, "Worker was null");
		if (workerData.getWorkerJob(worker) == WorkerData::Scout)
		{
			return worker;
		}
	}

	return nullptr;
}

// Send the worker to mine minerals at the closest resource depot, if any.
void WorkerManager::setMineralWorker(BWAPI::Unit unit)
{
    UAB_ASSERT(unit, "Unit was null");

	BWAPI::Unit depot = getNonFullClosestDepot(unit);

	if (depot)
	{
		workerData.setWorkerJob(unit, WorkerData::Minerals, depot);
	}
	else
	{
		// BWAPI::Broodwar->printf("No valid depot for mineral worker");
	}
}

// Worker is carrying minerals or gas. Tell it to hand them in.
void WorkerManager::setReturnCargoWorker(BWAPI::Unit unit)
{
	UAB_ASSERT(unit, "Unit was null");

	BWAPI::Unit depot = getAnyClosestDepot(unit);

	if (depot)
	{
		workerData.setWorkerJob(unit, WorkerData::ReturnCargo, depot);
	}
	else
	{
		// BWAPI::Broodwar->printf("No valid depot to accept return cargo");
	}
}

// Get the closest resource depot with no other consideration.
BWAPI::Unit WorkerManager::getAnyClosestDepot(BWAPI::Unit worker)
{
	UAB_ASSERT(worker, "Worker was null");

	BWAPI::Unit closestDepot = nullptr;
	int closestDistance = 0;

	for (BWAPI::Unit unit : BWAPI::Broodwar->self()->getUnits())
	{
		UAB_ASSERT(unit, "Unit was null");

		if (UnitUtil::IsCompletedResourceDepot(unit))
		{
			int distance = unit->getDistance(worker);
			if (!closestDepot || distance < closestDistance)
			{
				closestDepot = unit;
				closestDistance = distance;
			}
		}
	}

	return closestDepot;
}

// Get the closest resource depot that can accept another mineral worker.
BWAPI::Unit WorkerManager::getNonFullClosestDepot(BWAPI::Unit worker)
{
	UAB_ASSERT(worker, "Worker was null");

	BWAPI::Unit closestDepot = nullptr;
	int closestDistance = 0;

	for (const auto unit : BWAPI::Broodwar->self()->getUnits())
	{
        UAB_ASSERT(unit, "Unit was null");

		if (UnitUtil::IsCompletedResourceDepot(unit) &&
			!workerData.depotIsFull(unit))
		{
			int distance = unit->getDistance(worker);
			if (!closestDepot || distance < closestDistance)
			{
				closestDepot = unit;
				closestDistance = distance;
			}
		}
	}

	return closestDepot;
}

// other managers that need workers call this when they're done with a unit
void WorkerManager::finishedWithWorker(BWAPI::Unit unit) 
{
	UAB_ASSERT(unit, "Unit was null");

	//BWAPI::Broodwar->printf("finished with worker %d", unit->getID());
	workerData.setWorkerJob(unit, WorkerData::Idle, nullptr);
}

// Find a worker to be reassigned to gas duty.
BWAPI::Unit WorkerManager::getGasWorker(BWAPI::Unit refinery)
{
	UAB_ASSERT(refinery, "Refinery was null");

	BWAPI::Unit closestWorker = nullptr;
	int closestDistance = 99999;

	for (const auto unit : workerData.getWorkers())
	{
		UAB_ASSERT(unit, "Unit was null");

		if (isFree(unit))
		{
			// Don't waste minerals. It's OK (and unlikely) to already be carrying gas.
			if (unit->isCarryingMinerals() ||                       // doesn't have minerals and
				unit->getOrder() == BWAPI::Orders::MiningMinerals)  // isn't about to get them
			{
				continue;
			}

			int distance = unit->getDistance(refinery);
			if (!closestWorker || distance < closestDistance)
			{
				closestWorker = unit;
				closestDistance = distance;
			}
		}
	}

	return closestWorker;
}

// Get a builder for BuildingManager.
// Don't give it any orders (that is for the caller).
BWAPI::Unit WorkerManager::getBuilder(const Building & b)
{
	// variables to hold the closest worker of each type to the building
	BWAPI::Unit closestMovingWorker = nullptr;
	BWAPI::Unit closestMiningWorker = nullptr;
	int closestMovingWorkerDistance = 99999;
	int closestMiningWorkerDistance = 99999;

	// look through each worker that had moved there first
	for (auto & unit : workerData.getWorkers())
	{
        UAB_ASSERT(unit, "Unit was null");

        // gas steal building uses scout worker
        if (b.isGasSteal && (workerData.getWorkerJob(unit) == WorkerData::Scout))
        {
            return unit;
        }

		// mining or idle worker check
		if (isFree(unit))
		{
			// if it is a new closest distance, set the pointer
			int distance = unit->getDistance(BWAPI::Position(b.finalPosition));
			if (unit->isCarryingMinerals() || unit->isCarryingGas() ||
				unit->getOrder() == BWAPI::Orders::MiningMinerals)
			{
				// If it has cargo or is busy getting some, pretend it is farther away.
				// That way we prefer empty workers and lose less cargo.
				distance += 999;
			}
			if (!closestMiningWorker || distance < closestMiningWorkerDistance)
			{
				closestMiningWorker = unit;
				closestMiningWorkerDistance = distance;
			}
		}

		// moving worker check
		if (unit->isCompleted() && (workerData.getWorkerJob(unit) == WorkerData::Move))
		{
			// if it is a new closest distance, set the pointer
			int distance = unit->getDistance(BWAPI::Position(b.finalPosition));
			if (unit->isCarryingMinerals() || unit->isCarryingGas() ||
				unit->getOrder() == BWAPI::Orders::MiningMinerals) {
				// If it has cargo or is busy getting some, pretend it is farther away.
				// That way we prefer empty workers and lose less cargo.
				distance += 999;
			}
			if (!closestMovingWorker || distance < closestMovingWorkerDistance)
			{
				closestMovingWorker = unit;
				closestMovingWorkerDistance = distance;
			}
		}
	}

	// if we found a moving worker, use it, otherwise using a mining worker
	BWAPI::Unit chosenWorker = closestMovingWorker ? closestMovingWorker : closestMiningWorker;

	return chosenWorker;
}

void WorkerManager::setBuildingWorker(BWAPI::Unit worker, BWAPI::UnitType buildingType)
{
	UAB_ASSERT(worker, "Worker was null");

	workerData.setWorkerJob(worker, WorkerData::Build, buildingType);
}

// sets a worker as a scout
void WorkerManager::setScoutWorker(BWAPI::Unit worker)
{
	UAB_ASSERT(worker, "Worker was null");

	workerData.setWorkerJob(worker, WorkerData::Scout, nullptr);
}

// Choose a worker to move to the given location.
// Don't give it any orders (that is for the caller).
BWAPI::Unit WorkerManager::getMoveWorker(BWAPI::Position p)
{
	// set up the pointer
	BWAPI::Unit closestWorker = nullptr;
	int closestDistance = 99999;

	// for each worker we currently have
	for (const auto unit : workerData.getWorkers())
	{
        UAB_ASSERT(unit, "Unit was null");

		// only consider it if it's a mineral worker or idle
		if (isFree(unit))
		{
			// if it is a new closest distance, set the pointer
			int distance = unit->getDistance(p);
			if (unit->isCarryingMinerals() || unit->isCarryingGas() ||
				unit->getOrder() == BWAPI::Orders::MiningMinerals) {
				// If it has cargo or is busy getting some, pretend it is farther away.
				// That way we prefer empty workers and lose less cargo.
				distance += 999;
			}
			if (!closestWorker || distance < closestDistance)
			{
				closestWorker = unit;
				closestDistance = distance;
			}
		}
	}

	// return the worker
	return closestWorker;
}

// Sets a worker to move to a given location. Use getMoveWorker() to choose the worker.
void WorkerManager::setMoveWorker(BWAPI::Unit worker, int mineralsNeeded, int gasNeeded, BWAPI::Position & p)
{
	UAB_ASSERT(worker && p.isValid(), "bad call");
	workerData.setWorkerJob(worker, WorkerData::Move, WorkerMoveData(mineralsNeeded, gasNeeded, p));
	//BWAPI::Broodwar->printf("set move worker %d", worker->getID());
}

// will we have the required resources by the time a worker can travel a certain distance
bool WorkerManager::willHaveResources(int mineralsRequired, int gasRequired, double distance)
{
	// if we don't require anything, we will have it
	if (mineralsRequired <= 0 && gasRequired <= 0)
	{
		return true;
	}

	// the speed of the worker unit
	double speed = BWAPI::Broodwar->self()->getRace().getWorker().topSpeed();

    UAB_ASSERT(speed > 0, "Speed is negative");

	// how many frames it will take us to move to the building location
	// add a second to account for worker getting stuck. better early than late
	double framesToMove = (distance / speed) + 48;

	// magic numbers to predict income rates
	double mineralRate = getNumMineralWorkers() * 0.045;
	double gasRate     = getNumGasWorkers() * 0.07;

	// calculate if we will have enough by the time the worker gets there
	return
		mineralRate * framesToMove >= mineralsRequired &&
		gasRate * framesToMove >= gasRequired;
}

void WorkerManager::setCombatWorker(BWAPI::Unit worker)
{
	UAB_ASSERT(worker, "Worker was null");

	//BWAPI::Broodwar->printf("set combat worker %d", worker->getID());
	workerData.setWorkerJob(worker, WorkerData::Combat, nullptr);
}

void WorkerManager::onUnitMorph(BWAPI::Unit unit)
{
	UAB_ASSERT(unit, "Unit was null");

	// if something morphs into a worker, add it
	if (unit->getType().isWorker() && unit->getPlayer() == BWAPI::Broodwar->self() && unit->getHitPoints() >= 0)
	{
		workerData.addWorker(unit);
	}

	// if something morphs into a building, was it a drone?
	if (unit->getType().isBuilding() && unit->getPlayer() == BWAPI::Broodwar->self() && unit->getPlayer()->getRace() == BWAPI::Races::Zerg)
	{
		//BWAPI::Broodwar->printf("A Drone started building");
		workerData.workerDestroyed(unit);
	}
}

void WorkerManager::onUnitShow(BWAPI::Unit unit)
{
	UAB_ASSERT(unit && unit->exists(), "bad unit");

	// add the depot if it exists
	if (unit->getType().isResourceDepot() && unit->getPlayer() == BWAPI::Broodwar->self())
	{
		workerData.addDepot(unit);
	}

	// if something morphs into a worker, add it
	if (unit->getType().isWorker() && unit->getPlayer() == BWAPI::Broodwar->self() && unit->getHitPoints() >= 0)
	{
		//BWAPI::Broodwar->printf("A worker was shown %d", unit->getID());
		workerData.addWorker(unit);
	}
}

// Possibly transfer workers to other bases.
void WorkerManager::rebalanceWorkers()
{
	// for each worker
	for (const auto worker : workerData.getWorkers())
	{
        UAB_ASSERT(worker, "Worker was null");

		if (!workerData.getWorkerJob(worker) == WorkerData::Minerals)
		{
			continue;
		}

		BWAPI::Unit depot = workerData.getWorkerDepot(worker);

		if (depot && workerData.depotIsFull(depot))
		{
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}
		else if (!depot)
		{
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}
	}
}

void WorkerManager::onUnitDestroy(BWAPI::Unit unit) 
{
	UAB_ASSERT(unit, "Unit was null");

	if (unit->getType().isResourceDepot() && unit->getPlayer() == BWAPI::Broodwar->self())
	{
		workerData.removeDepot(unit);
	}

	if (unit->getType().isWorker() && unit->getPlayer() == BWAPI::Broodwar->self()) 
	{
		workerData.workerDestroyed(unit);
	}

	if (unit->getType() == BWAPI::UnitTypes::Resource_Mineral_Field)
	{
		rebalanceWorkers();
	}
}

void WorkerManager::drawResourceDebugInfo() 
{
    if (!Config::Debug::DrawResourceInfo)
    {
        return;
    }

	// Profile debug
	//PROFILE_FUNCTION();

	for (auto & worker : workerData.getWorkers()) 
    {
        UAB_ASSERT(worker, "Worker was null");

		char job = workerData.getJobCode(worker);

		BWAPI::Position pos = worker->getTargetPosition();

		BWAPI::Broodwar->drawTextMap(worker->getPosition().x, worker->getPosition().y - 5, "\x07%c", job);
		BWAPI::Broodwar->drawTextMap(worker->getPosition().x, worker->getPosition().y + 5, "\x03%s", worker->getOrder().getName().c_str());

		BWAPI::Broodwar->drawLineMap(worker->getPosition().x, worker->getPosition().y, pos.x, pos.y, BWAPI::Colors::Cyan);

		BWAPI::Unit depot = workerData.getWorkerDepot(worker);
		if (depot)
		{
			BWAPI::Broodwar->drawLineMap(worker->getPosition().x, worker->getPosition().y, depot->getPosition().x, depot->getPosition().y, BWAPI::Colors::Orange);
		}
	}
}

void WorkerManager::drawWorkerInformation(int x, int y) 
{
    if (!Config::Debug::DrawWorkerInfo)
    {
        return;
    }

	// Profile debug
	//PROFILE_FUNCTION();

	BWAPI::Broodwar->drawTextScreen(x, y, "\x04 Workers %d", workerData.getNumWorkers());
	BWAPI::Broodwar->drawTextScreen(x, y+20, "\x04 UnitID");
	BWAPI::Broodwar->drawTextScreen(x+50, y+20, "\x04 State");

	int yspace = 0;

	for (const auto unit : workerData.getWorkers())
	{
        UAB_ASSERT(unit, "Worker was null");

		BWAPI::Broodwar->drawTextScreen(x, y+40+((yspace)*10), "\x03 %d", unit->getID());
		BWAPI::Broodwar->drawTextScreen(x+50, y+40+((yspace++)*10), "\x03 %c", workerData.getJobCode(unit));
	}
}

bool WorkerManager::isFree(BWAPI::Unit worker)
{
    UAB_ASSERT(worker, "Worker was null");

	WorkerData::WorkerJob job = workerData.getWorkerJob(worker);
	return 
		(job == WorkerData::Minerals || job == WorkerData::Idle && !worker->isBurrowed()) && 
		!isBusy(worker) && 
		worker->isCompleted();
}

bool WorkerManager::isWorkerScout(BWAPI::Unit worker)
{
    UAB_ASSERT(worker, "Worker was null");

	return workerData.getWorkerJob(worker) == WorkerData::Scout;
}

bool WorkerManager::isCombatWorker(BWAPI::Unit worker)
{
	UAB_ASSERT(worker, "Worker was null");

	return workerData.getWorkerJob(worker) == WorkerData::Combat;
}

bool WorkerManager::isBuilder(BWAPI::Unit worker)
{
    UAB_ASSERT(worker, "Worker was null");

	return workerData.getWorkerJob(worker) == WorkerData::Build;
}

int WorkerManager::getNumMineralWorkers() const
{
	return workerData.getNumMineralWorkers();	
}

int WorkerManager::getNumGasWorkers() const
{
	return workerData.getNumGasWorkers();
}

int WorkerManager::getNumReturnCargoWorkers() const
{
	return workerData.getNumReturnCargoWorkers();
}

int WorkerManager::getNumCombatWorkers() const
{
	return workerData.getNumCombatWorkers();
}

int WorkerManager::getNumIdleWorkers() const
{
	return workerData.getNumIdleWorkers();
}

// The largest number of workers that it is efficient to have right now.
// NOTE Does not take into account possible preparations for future expansions.
int WorkerManager::getMaxWorkers() const
{
	int patches = InformationManager::Instance().getMyNumMineralPatches();
	int refineries = InformationManager::Instance().getMyNumRefineries();

	// Never let the max number of workers fall to 0!
	// Set aside 1 for future opportunities.
	return std::min(
		80,
		1 + int(std::round(Config::Macro::WorkersPerPatch * patches + Config::Macro::WorkersPerRefinery * refineries))
	);
}

// Mine out any blocking minerals that the worker runs headlong into.
bool WorkerManager::maybeMineMineralBlocks(BWAPI::Unit worker)
{
	UAB_ASSERT(worker, "Worker was null");

	if (worker->isGatheringMinerals() &&
		worker->getTarget() &&
		worker->getTarget()->getInitialResources() <= 16)
	{
		// Still busy mining the block.
		return true;
	}

	for (const auto patch : worker->getUnitsInRadius(64, BWAPI::Filter::IsMineralField))
	{
		if (patch->getInitialResources() <= 16)    // any patch we can mine out quickly
		{
			// Go start mining.
			worker->gather(patch);
			return true;
		}
	}

	return false;
}

void WorkerManager::handleBlockingMineralsWorkers() {

	BWAPI::Unit worker;
	int x = 0;

	// Get all mineral patches
	BWAPI::Unitset minerals;
	for (auto & unit : BWAPI::Broodwar->getAllUnits())
	{
		//if (UnitUtil::IsValidUnit(unit))
		//{
			// if the minerals have <8 resouces, mine them
			if (unit->getType().isMineralField() && unit->getResources() < 8)
			{
				//BWAPI::Broodwar->printf("found blocking minerals");

				// get closest worker to this mineral patch
				worker = getClosestBlockingMinWorkerTo(unit);

				if (UnitUtil::IsValidUnit(worker))
				{
					x++;
					//BWAPI::Broodwar->printf("found blocking minerals worker %d: %d (%d,%d) dist = %d", x, unit->getResources(), unit->getPosition().x, unit->getPosition().y, unit->getDistance(worker->getPosition()));

					if (unit->getDistance(worker) < 1200)
					{
						//BWAPI::Broodwar->printf("mine blocking minerals worker %d: %d", x, unit->getResources());

						if (unit->getDistance(worker) < 32)
						{
							return;
						}

						worker->gather(unit);
						return;
					}
				}
			}
		//}
	}
}

bool WorkerManager::handleBlockingMineralsBuilders(BWAPI::Unit worker) {

	UAB_ASSERT(worker != nullptr, "worker was null");

	int x = 0;

	// Get all mineral patches
	BWAPI::Unitset minerals;
	for (auto & unit : BWAPI::Broodwar->getAllUnits())
	{
		//if (UnitUtil::IsValidUnit(unit))
		//{
			// if the minerals have <8 resouces, mine them
			if (unit->getType().isMineralField() && unit->getResources() < 8)
			{
				//BWAPI::Broodwar->printf("found blocking minerals");

				if (UnitUtil::IsValidUnit(worker) && unit->getDistance(worker) < 1200)
				{
					x++;
					//BWAPI::Broodwar->printf("found blocking minerals builder %d: %d (%d,%d) dist = %d", x, unit->getResources(), unit->getPosition().x, unit->getPosition().y, unit->getDistance(worker->getPosition()));

					if (unit->getDistance(worker) < 100)
					{
						//BWAPI::Broodwar->printf("mine blocking minerals builder %d: %d", x, unit->getResources());

						if (worker->isGatheringMinerals())
						{
							return true;
						}

						worker->gather(unit);
						return true;
					}
				}
			}
		//}
	}

	return false;
}



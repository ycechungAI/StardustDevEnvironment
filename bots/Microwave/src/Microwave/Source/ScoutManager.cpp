#include "ScoutManager.h"
#include "GameCommander.h"
#include "ProductionManager.h"
#include "CombatCommander.h"

using namespace UAlbertaBot;

ScoutManager::ScoutManager() 
    : _workerScout(nullptr)
	, _overlordScout(nullptr)
	, _scoutStatus("None")
	, _gasStealStatus("None")
	, _numWorkerScouts(0)
	, _scoutLocationOnly(false)
	, _scoutOnceOnly(false)
    , _scoutUnderAttack(false)
	, _overlordAtEnemyBase(false)
	, _overlordAtBaseTarget(BWAPI::Positions::Invalid)
	, _tryGasSteal(false)
	, _startedGasSteal(false)
    , _didGasSteal(false)
    , _gasStealFinished(false)
    , _currentRegionVertexIndex(-1)
    , _previousScoutHP(0)
	, _overlordClosestBase(false)
{
	setScoutTargets();
}

ScoutManager & ScoutManager::Instance() 
{
	static ScoutManager instance;
	return instance;
}

// Target locations are set while we are still looking for the enemy base.
// After the enemy base is found, we unset the targets and go to or ignore the enemy base as appropriate.
// If we have both an overlord and a worker, send them to search different places.
// Guarantee: We only set a target if the scout for the target is set.
void ScoutManager::setScoutTargets()
{
	const BWEM::Base * enemyBase = InformationManager::Instance().getEnemyMainBaseLocation();
	if (enemyBase)
	{
		_overlordScoutTarget = BWAPI::TilePositions::Invalid;
		_workerScoutTarget = BWAPI::TilePositions::Invalid;
		return;
	}

	if (!_overlordScout)
	{
		_overlordScoutTarget = BWAPI::TilePositions::Invalid;
	}
	if (!_workerScout)
	{
		_workerScoutTarget = BWAPI::TilePositions::Invalid;
	}

	// Unset any targets that we have searched.
	for (BWAPI::TilePosition pos : BWAPI::Broodwar->getStartLocations())
	{
		if (BWAPI::Broodwar->isExplored(pos))
		{
			// We've seen it. No need to look there any more.
			if (_overlordScoutTarget == pos)
			{
				_overlordScoutTarget = BWAPI::TilePositions::Invalid;
			}
			if (_workerScoutTarget == pos)
			{
				_workerScoutTarget = BWAPI::TilePositions::Invalid;
			}
		}
	}

	// Set any target that we need to search.
	BWAPI::Position home(BWAPI::Broodwar->self()->getStartLocation());
	BWAPI::TilePosition homeLocation(BWAPI::Broodwar->self()->getStartLocation());
	BWAPI::TilePosition overlordScoutTarget = BWAPI::TilePositions::Invalid;
	int dist = 0;
	int distNew = 10000;

	if (_overlordClosestBase) {
		dist = 10000;
		distNew = 0;
	}

	for (BWAPI::TilePosition tile : BWAPI::Broodwar->getStartLocations())
	{
		if (!BWAPI::Broodwar->isExplored(tile))
		{
			if (_overlordScout && !_overlordScoutTarget.isValid() && _workerScoutTarget != tile)
			{
				distNew = home.getApproxDistance(BWAPI::Position(tile));

				if (distNew < dist && _overlordClosestBase) 
				{
					//BWAPI::Broodwar->printf("_overlordClosestBase true");
					overlordScoutTarget = tile;
					dist = distNew;
				}
				else if (distNew > dist && !_overlordClosestBase)
				{
					//BWAPI::Broodwar->printf("_overlordClosestBase false");
					overlordScoutTarget = tile;
					dist = distNew;
				}
			}
		}
	}
	if (!_overlordScoutTarget.isValid())
	{
		_overlordScoutTarget = overlordScoutTarget;
	}

	for (BWAPI::TilePosition tile : BWAPI::Broodwar->getStartLocations())
	{
		if (!BWAPI::Broodwar->isExplored(tile))
		{
			if (_workerScout && !_workerScoutTarget.isValid() && _overlordScoutTarget != tile &&
				MapTools::Instance().getGroundTileDistance(homeLocation, tile) >= 0)
			{
				_workerScoutTarget = tile;
			}
		}
	}

	// NOTE There cannot be only one target. We found the base, or there are >= 2 possibilities.
	//      If there is one place left to search, then the enemy base is there and we know it,
	//      because InformationManager infers the enemy base position by elimination.
}

void ScoutManager::update()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// If we're not scouting now, minimum effort.
	if (!_workerScout && !_overlordScout)
	{
		return;
	}

	// If the worker scout is gone, admit it.
	if (_workerScout &&
		(!_workerScout->exists() || _workerScout->getHitPoints() <= 0 ||	   // it died (or became a zerg extractor)
			_workerScout->getPlayer() != BWAPI::Broodwar->self()))             // it got mind controlled!
	{
		_workerScout = nullptr;
	}
	if (_overlordScout &&
		(!_overlordScout->exists() || _overlordScout->getHitPoints() <= 0 ||   // it died
		_overlordScout->getPlayer() != BWAPI::Broodwar->self()))               // it got mind controlled!
	{
		_overlordScout = nullptr;
	}

	// Release the overlord scout if it is likely to meet trouble soon.
	if (_overlordScout && InformationManager::Instance().enemyHasAntiAir())
	{
		releaseOverlordScout();
	}

	// If we only want to locate the enemy base and we have, release the scout worker.
	if (_scoutLocationOnly && InformationManager::Instance().getEnemyMainBaseLocation() && 
		!_tryGasSteal)
	{
		releaseWorkerScout();
	}

	// Calculate waypoints around the enemy base if we expect to need them.
	// We use these to go directly to the enemy base if its location is known,
	// as well as to run circuits around it.
	if (_workerScout && _enemyRegionVertices.empty())
	{
		calculateEnemyRegionVertices();
	}

	// Do the actual scouting. Also steal gas if called for.
	setScoutTargets();

	if (_workerScout)
	{
		bool moveScout = true;
		if (_tryGasSteal)              // implies !_gasStealFinished
		{
			if (gasSteal())
			{
				moveScout = false;
			}
			else if (_didGasSteal)    // gas steal queued but not finished
			{
				// We're in the midst of stealing gas. Let BuildingManager control the worker.
				moveScout = false;
			}
		}
		else
		{
			if (_gasStealFinished)
			{
				_gasStealStatus = "Finished or failed";
			}
			else
			{
				_gasStealStatus = "Not planned";
			}
		}
		if (moveScout)
		{
			moveGroundScout();
		}
	}
	else if (_gasStealFinished)
	{
		// We get here if we're stealing gas as zerg when the worker turns into an extractor.
		_gasStealStatus = "Finished or failed";
	}

	if (_overlordScout)
	{
		moveAirScout();
	}

    drawScoutInformation(200, 320);
}

void ScoutManager::setWorkerScout(BWAPI::Unit unit)
{
    // if we have a previous worker scout, release it back to the worker manager
    if (_workerScout)
    {
		releaseWorkerScout();
    }

    _workerScout = unit;
    WorkerManager::Instance().setScoutWorker(_workerScout);
}

// If zerg, our first overlord is used to scout immediately at the start of the game.
void ScoutManager::setOverlordScout(BWAPI::Unit unit)
{
	_overlordScout = unit;
}

// Send the worker scout home.
void ScoutManager::releaseWorkerScout()
{
	if (_workerScout)
	{
		WorkerManager::Instance().finishedWithWorker(_workerScout);
		_gasStealFinished = true;
		_workerScout = nullptr;
	}
}

// Send the overlord scout home.
void ScoutManager::releaseOverlordScout()
{
	if (_overlordScout)
	{
		GameCommander::Instance().releaseOverlord(_overlordScout);
		_overlordScout = nullptr;
	}
}

void ScoutManager::setGasSteal()
{
	_tryGasSteal = true;
}

void ScoutManager::setScoutLocationOnly()
{
	_scoutLocationOnly = true;
}

void ScoutManager::setScoutOnceOnly()
{
	_scoutOnceOnly = true;
}

void ScoutManager::drawScoutInformation(int x, int y)
{
    if (!Config::Debug::DrawScoutInfo)
    {
        return;
    }

    BWAPI::Broodwar->drawTextScreen(x, y, "ScoutInfo: %s", _scoutStatus.c_str());
    BWAPI::Broodwar->drawTextScreen(x, y+10, "GasSteal: %s", _gasStealStatus.c_str());
    for (size_t i(0); i < _enemyRegionVertices.size(); ++i)
    {
        BWAPI::Broodwar->drawCircleMap(_enemyRegionVertices[i], 4, BWAPI::Colors::Green, false);
        BWAPI::Broodwar->drawTextMap(_enemyRegionVertices[i], "%d", i);
    }
}

void ScoutManager::moveGroundScout()
{
	if (_workerScout->isCarryingGas())
	{
		BWAPI::Broodwar->drawCircleMap(_workerScout->getPosition(), 10, BWAPI::Colors::Purple, true);
	}

	// get the enemy base location, if we have one
	// Note: In case of an enemy proxy or weird map, this might be our own base. Roll with it.
	const BWEM::Base * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();
	int scoutDistanceThreshold = 30;

	// if we don't know where the enemy region is
	if (!enemyBaseLocation)
	{
		// if we haven't explored scout target
		if (!BWAPI::Broodwar->isExplored(_workerScoutTarget))
		{
			_scoutStatus = "Enemy base unknown, exploring";

			// assign a worker to go scout it
			Micro::SmartMoveSafely(_workerScout, BWAPI::Position(_workerScoutTarget));
			return;
		}
	}

	// if we know where the enemy region is
	if (enemyBaseLocation)
	{
		// if we haven't explored it yet
		if (!BWAPI::Broodwar->isExplored(enemyBaseLocation->Location()))
		{
			_scoutStatus = "Enemy base known, going there";

			// assign a worker to go scout it
			Micro::SmartMoveSafely(_workerScout, BWAPI::Position(enemyBaseLocation->Location()));
			return;
		}
	}

	// if we know where the enemy region is
	if (enemyBaseLocation)
	{
        int scoutDistanceToEnemy = MapTools::Instance().getGroundTileDistance(enemyBaseLocation->Location(), _workerScout->getTilePosition());
        bool scoutInRangeOfenemy = scoutDistanceToEnemy <= scoutDistanceThreshold;
        
        // we only care if the scout is under attack within the enemy region
        // this ignores if their scout worker attacks it on the way to their base
		int scoutHP = _workerScout->getHitPoints() + _workerScout->getShields();
        if (scoutHP < _previousScoutHP)
        {
	        _scoutUnderAttack = true;
        }
		_previousScoutHP = scoutHP;

        if (!_workerScout->isUnderAttack() && !enemyWorkerInRadius())
        {
	        _scoutUnderAttack = false;
        }

		// if the scout is in the enemy region
		if (scoutInRangeOfenemy)
		{
			// if the worker scout is under attack
			if (_scoutUnderAttack)
			{
				_scoutStatus = "Under attack inside, fleeing";
				followPerimeter();
			}
			// if the worker scout is not under attack
			else
			{
				// get the closest enemy worker
				BWAPI::Unit closestWorker = enemyWorkerToHarass();

				// if configured and there is a worker nearby, harass it
				if (Config::Strategy::ScoutHarassEnemy && !_scoutOnceOnly &&
					(!_tryGasSteal || _gasStealFinished) && closestWorker && 
					(_workerScout->getDistance(closestWorker) < 600) &&
					_workerScout->getHitPoints() + _workerScout->getShields() > 20)
				{
                    _scoutStatus = "Harass enemy worker";
                    _currentRegionVertexIndex = -1;
					//Micro::CatchAndAttackUnit(_workerScout, closestWorker);
					Micro::SmartAttackUnit(_workerScout, closestWorker);
				}
				// otherwise keep circling the enemy region
				else
				{
                    _scoutStatus = "Following perimeter";
                    followPerimeter();  
                }
				
			}
		}
		// if the scout is not in the enemy region
		// if the worker scout is under attack
		else if (_scoutUnderAttack)
		{
            _scoutStatus = "Under attack outside, fleeing";
            followPerimeter();
		}
		// if the worker scout is not under attack
		else
		{
			// move to the enemy region
            _scoutStatus = "Enemy base known, going there";
			followPerimeter();
        }
	}
}

// Move the overlord scout.
void ScoutManager::moveAirScout()
{
	// get the enemy base location, if we have one
	// Note: In case of an enemy proxy or weird map, this might be our own base. Roll with it.
	const BWEM::Base * enemyBase = InformationManager::Instance().getEnemyMainBaseLocation();

	if (enemyBase)
	{
		// We know where the enemy base is.
		_overlordScoutTarget = BWAPI::TilePositions::Invalid;    // it's only set while we are seeking the enemy base
		if (!_overlordAtEnemyBase)
		{
			if (!_workerScout) // give the worker priority in reporting status
			{
				_scoutStatus = "Overlord to enemy base";
			}
			Micro::SmartMoveSafely(_overlordScout, enemyBase->Center());
			if (_overlordScout->getDistance(enemyBase->Center()) < 8)
			{
				_overlordAtEnemyBase = true;
			}
		}
		if (_overlordAtEnemyBase)
		{
			if (!_workerScout) // give the worker priority in reporting status
			{
				_scoutStatus = "Overlord at enemy base";
			}
			moveAirScoutAroundEnemyBase();
		}
	}
	else
	{
		// We haven't found the enemy base yet.
		if (!_workerScout) // give the worker priority in reporting status
		{
			_scoutStatus = "Overlord scouting";
		}

		if (_overlordScoutTarget.isValid())
		{
			Micro::SmartMoveSafely(_overlordScout, BWAPI::Position(_overlordScoutTarget));
		}
	}
}

// Called after the overlord has reached the enemy resource depot,
// or has been turned away from it by static defense.
void ScoutManager::moveAirScoutAroundEnemyBase()
{
	const BWEM::Base * enemyBase = InformationManager::Instance().getEnemyMainBaseLocation();
	//UAB_ASSERT(enemyBase, "no enemy base");
	//UAB_ASSERT(_overlordAtEnemyBase, "not at enemy base");

	if (!_overlordAtBaseTarget.isValid())
	{
		// Choose a new destination in or near the enemy base.
		if (_overlordScout->getDistance(enemyBase->Center()) < 32)
		{
			// Choose a new destination in or near the enemy base.
			_overlordAtBaseTarget = MapGrid::Instance().getLeastExploredNear(enemyBase->Center(), false);
		}
		else
		{
			// We're somewhere else. Go back to the enemy resource depot.
			_overlordAtBaseTarget = enemyBase->Center();
		}
	}

	if (_overlordAtBaseTarget.isValid())
	{
		Micro::SmartMoveSafely(_overlordScout, BWAPI::Position(_overlordAtBaseTarget));

		// If we arrived, choose somewhere else next time.
		if (_overlordScout->getDistance(_overlordAtBaseTarget) < 32)
		{
			_overlordAtBaseTarget = BWAPI::Positions::Invalid;
		}
	}
	else
	{
		// We apparently can't go anywhere. Let the overlord run free.
		releaseOverlordScout();
	}
}

void ScoutManager::followPerimeter()
{
	int previousIndex = _currentRegionVertexIndex;
    BWAPI::Position fleeTo = getFleePosition();

    if (Config::Debug::DrawScoutInfo)
    {
        BWAPI::Broodwar->drawCircleMap(fleeTo, 5, BWAPI::Colors::Red, true);
    }

	// We've been told to circle the enemy base only once.
	if (_scoutOnceOnly && !_tryGasSteal)
	{
		// Release worker if the overlord scout is at the enemy base
		if (_overlordAtEnemyBase)
		{
			releaseWorkerScout();
			return;
		}
		// NOTE previousIndex may be -1 if we're just starting the loop.
		if (_currentRegionVertexIndex < previousIndex)
		{
			releaseWorkerScout();
			return;
		}
	}

	Micro::SmartMoveSafely(_workerScout, fleeTo);
}

bool ScoutManager::gasSteal()
{
    if (!_tryGasSteal)
    {
        _gasStealStatus = "Not using gas steal";
        return false;
    }

    if (_didGasSteal)
    {
        return false;
    }

    if (!_workerScout)
    {
        _gasStealStatus = "No worker scout";
        return false;
    }

    const BWEM::Base * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();
    if (!enemyBaseLocation)
    {
        _gasStealStatus = "No enemy base found";
        return false;
    }

    BWAPI::Unit enemyGeyser = getEnemyGeyser();
	if (!enemyGeyser || !enemyGeyser->getInitialTilePosition().isValid())
    {
		_gasStealFinished = true;
        _gasStealStatus = "No enemy geyser";
        return false;
    }

	// The conditions are met. Do it!
	_startedGasSteal = true;

	if (enemyGeyser->isVisible() && enemyGeyser->getType() != BWAPI::UnitTypes::Resource_Vespene_Geyser)
	{
		// We can see the geyser and it has become something else.
		// That should mean that the geyser has been taken since we first saw it.
		_gasStealFinished = true;    // give up
		_gasStealStatus = "No enemy geyser";
		return false;
	}
	else if (enemyGeyser->isVisible() && _workerScout->getDistance(enemyGeyser) < 128)
	{
		// We see the geyser. Queue the refinery, if it's not already done.
		if (!_didGasSteal)
		{
			ProductionManager::Instance().queueGasSteal();
			Micro::SmartMove(_workerScout, enemyGeyser->getInitialPosition());
			_didGasSteal = true;
			_gasStealStatus = "Stealing gas";
		}
	}
	else
	{
		// We don't see the geyser yet. Move toward it.
		Micro::SmartMoveSafely(_workerScout, enemyGeyser->getInitialPosition());
		_gasStealStatus = "Moving to steal gas";
	}
	return true;
}

// Choose an enemy worker to harass, or none.
BWAPI::Unit ScoutManager::enemyWorkerToHarass()
{	
	// First look for any enemy worker that is building.
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->getType().isWorker() && unit->isConstructing())
		{
			return unit;
		}
	}

	// Failing that, find the enemy worker closest to the gas.
	BWAPI::Unit enemyWorker = nullptr;
	double maxDist = 500;  // ignore any beyond this range
	BWAPI::Unit geyser = getAnyEnemyGeyser();

	if (geyser)
	{
		for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
		{
			if (unit->getType().isWorker())
			{
				int dist = unit->getDistance(geyser);

				if (dist < maxDist)
				{
					maxDist = dist;
					enemyWorker = unit;
				}
			}
		}
	}

	return enemyWorker;
}

// Used in choosing an enemy worker to harass.
// Find an enemy geyser and return it, if there is one.
BWAPI::Unit ScoutManager::getAnyEnemyGeyser() const
{
	const BWEM::Base * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();

	const auto& geysers = enemyBaseLocation->Geysers();
	if (!geysers.empty())
	{
		return geysers.front()->Unit();
	}

	return nullptr;
}

// If there is exactly 1 geyser in the enemy base, return it.
// If there's 0 we can't steal it, and if >1 then it's no use to steal it.
BWAPI::Unit ScoutManager::getEnemyGeyser()
{
	const BWEM::Base * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();

	const auto& geysers = enemyBaseLocation->Geysers();
	if (geysers.size() == 1)
	{
		const BWAPI::Unit geyser = geysers.front()->Unit();
		// If the geyser is visible, we may be able to reject it as already taken.
		// TODO get the type from InformationManager, which may remember
		if (!geyser->isVisible() || geyser->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser)
		{
			// We see it is untaken, or we don't see the geyser. Assume the best.
			return geyser;
		}
	}

	return nullptr;
}

bool ScoutManager::enemyWorkerInRadius()
{
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->getType().isWorker() && (unit->getDistance(_workerScout) < 300))
		{
			return true;
		}
	}

	return false;
}

// Unused.
bool ScoutManager::immediateThreat()
{
	BWAPI::Unitset enemyAttackingWorkers;
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->getType().isWorker() && unit->isAttacking())
		{
			enemyAttackingWorkers.insert(unit);
		}
	}
	
	if (_workerScout->isUnderAttack())
	{
		return true;
	}

	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		int dist = unit->getDistance(_workerScout);
		double range = unit->getType().groundWeapon().maxRange();

		if (unit->getType().canAttack() && !unit->getType().isWorker() && (dist <= range + 32))
		{
			return true;
		}
	}

	return false;
}

int ScoutManager::getClosestVertexIndex(BWAPI::Unit unit)
{
    int closestIndex = -1;
    int closestDistance = 10000000;

    for (size_t i(0); i < _enemyRegionVertices.size(); ++i)
    {
        int dist = unit->getDistance(_enemyRegionVertices[i]);
        if (dist < closestDistance)
        {
            closestDistance = dist;
            closestIndex = i;
        }
    }

    return closestIndex;
}

BWAPI::Position ScoutManager::getFleePosition()
{
    UAB_ASSERT_WARNING(!_enemyRegionVertices.empty(), "We should have an enemy region vertices if we are fleeing");
    
    const BWEM::Base * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();

    // if this is the first flee, we will not have a previous perimeter index
    if (_currentRegionVertexIndex == -1)
    {
        // so return the closest position in the polygon
        int closestPolygonIndex = getClosestVertexIndex(_workerScout);

        UAB_ASSERT_WARNING(closestPolygonIndex != -1, "Couldn't find a closest vertex");

        if (closestPolygonIndex == -1)
        {
            return BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());
        }
        else
        {
            // set the current index so we know how to iterate if we are still fleeing later
            _currentRegionVertexIndex = closestPolygonIndex;
            return _enemyRegionVertices[closestPolygonIndex];
        }
    }
    // if we are still fleeing from the previous frame, get the next location if we are close enough
    else
    {
        double distanceFromCurrentVertex = _enemyRegionVertices[_currentRegionVertexIndex].getApproxDistance(_workerScout->getPosition());

        // keep going to the next vertex in the perimeter until we get to one we're far enough from to issue another move command
        while (distanceFromCurrentVertex < 128)
        {
            _currentRegionVertexIndex = (_currentRegionVertexIndex + 1) % _enemyRegionVertices.size();

            distanceFromCurrentVertex = _enemyRegionVertices[_currentRegionVertexIndex].getApproxDistance(_workerScout->getPosition());
        }

        return _enemyRegionVertices[_currentRegionVertexIndex];
    }

}

void ScoutManager::calculateEnemyRegionVertices()
{
    const BWEM::Base * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();
    //UAB_ASSERT_WARNING(enemyBaseLocation, "We should have an enemy base location if we are scouting");

    if (!enemyBaseLocation)
    {
        return;
    }

	const BWEM::Area * enemyRegion = enemyBaseLocation->GetArea();
    //UAB_ASSERT_WARNING(enemyRegion, "We should have an enemy region if we are scouting");

    if (!enemyRegion)
    {
        return;
    }

	const BWAPI::Position enemyCenter = enemyBaseLocation->Center();
    const BWAPI::TilePosition baseLocation = BWAPI::Broodwar->self()->getStartLocation();
	const BWAPI::Position basePosition(baseLocation);
    const std::vector<BWAPI::TilePosition> & closestTobase = MapTools::Instance().getClosestTilesTo(basePosition);

    std::set<BWAPI::Position> unsortedVertices;

    // check each tile position
    for (size_t i(0); i < closestTobase.size(); ++i)
    {
		const auto& tp = closestTobase[i];

        if (BWEMmap.GetArea(tp) != enemyRegion)
        {
            continue;
        }

		// a tile is 'on an edge' unless
		// 1) in all 4 directions there's a tile position in the current region
		// 2) in all 4 directions there's a buildable tile
		// 3) in all 4 directions the tile is valid
		const bool edge =
			!BWAPI::TilePosition(tp.x + 1, tp.y).isValid() || BWEMmap.GetArea(BWAPI::TilePosition(tp.x + 1, tp.y)) != enemyRegion || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x + 1, tp.y))
			|| !BWAPI::TilePosition(tp.x, tp.y + 1).isValid() || BWEMmap.GetArea(BWAPI::TilePosition(tp.x, tp.y + 1)) != enemyRegion || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x, tp.y + 1))
			|| !BWAPI::TilePosition(tp.x - 1, tp.y).isValid() || BWEMmap.GetArea(BWAPI::TilePosition(tp.x - 1, tp.y)) != enemyRegion || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x - 1, tp.y))
			|| !BWAPI::TilePosition(tp.x, tp.y - 1).isValid() || BWEMmap.GetArea(BWAPI::TilePosition(tp.x, tp.y - 1)) != enemyRegion || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x, tp.y - 1));

        
        // push the tiles that aren't surrounded
        if (edge && BWAPI::Broodwar->isBuildable(tp))
        {
            if (Config::Debug::DrawScoutInfo)
            {
                int x1 = tp.x * 32 + 2;
                int y1 = tp.y * 32 + 2;
                int x2 = (tp.x+1) * 32 - 2;
                int y2 = (tp.y+1) * 32 - 2;
        
                BWAPI::Broodwar->drawTextMap(x1+3, y1+2, "%d", MapTools::Instance().getGroundTileDistance(tp, baseLocation));
                BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Green, false);
            }
            
			BWAPI::Position vertex = BWAPI::Position(tp) + BWAPI::Position(16, 16);

			// Pull the vertex towards the enemy base center, unless it is already within 10 tiles
			int dist = enemyCenter.getApproxDistance(vertex);
			if (dist > 320.0)
			{
				int pullBy = std::min(dist - 320, 192);

				// Special case where the slope is infinite
				if (vertex.x == enemyCenter.x)
				{
					vertex = vertex + BWAPI::Position(0, vertex.y > enemyCenter.y ? -pullBy : pullBy);
				}
				else
				{
					// First get the slope, m = (y1 - y0)/(x1 - x0)
					double m = double(enemyCenter.y - vertex.y) / double(enemyCenter.x - vertex.x);

					// Now the equation for a new x is x0 +- d/sqrt(1 + m^2)
					double x = vertex.x + (vertex.x > enemyCenter.x ? -1.0 : 1.0) * pullBy / (sqrt(1 + m * m));

					// And y is m(x - x0) + y0
					double y = m * (x - vertex.x) + vertex.y;

					vertex = BWAPI::Position(int(x), int(y));
				}
			}

            unsortedVertices.insert(vertex);
        }
    }


    std::vector<BWAPI::Position> sortedVertices;
    BWAPI::Position current = *unsortedVertices.begin();

    _enemyRegionVertices.push_back(current);
    unsortedVertices.erase(current);

    // while we still have unsorted vertices left, find the closest one remaining to current
    while (!unsortedVertices.empty())
    {
        double bestDist = 1000000;
        BWAPI::Position bestPos;

        for (const BWAPI::Position & pos : unsortedVertices)
        {
            double dist = pos.getApproxDistance(current);

            if (dist < bestDist)
            {
                bestDist = dist;
                bestPos = pos;
            }
        }

        current = bestPos;
        sortedVertices.push_back(bestPos);
        unsortedVertices.erase(bestPos);
    }

    // let's close loops on a threshold, eliminating death grooves
    int distanceThreshold = 100;

    while (true)
    {
        // find the largest index difference whose distance is less than the threshold
        int maxFarthest = 0;
        int maxFarthestStart = 0;
        int maxFarthestEnd = 0;

        // for each starting vertex
        for (int i(0); i < (int)sortedVertices.size(); ++i)
        {
            int farthest = 0;
            int farthestIndex = 0;

            // only test half way around because we'll find the other one on the way back
            for (size_t j(1); j < sortedVertices.size()/2; ++j)
            {
                int jindex = (i + j) % sortedVertices.size();
            
                if (sortedVertices[i].getApproxDistance(sortedVertices[jindex]) < distanceThreshold)
                {
                    farthest = j;
                    farthestIndex = jindex;
                }
            }

            if (farthest > maxFarthest)
            {
                maxFarthest = farthest;
                maxFarthestStart = i;
                maxFarthestEnd = farthestIndex;
            }
        }
        
        // stop when we have no long chains within the threshold
        if (maxFarthest < 4)
        {
            break;
        }

        std::vector<BWAPI::Position> temp;

        for (size_t s(maxFarthestEnd); s != maxFarthestStart; s = (s+1) % sortedVertices.size())
        {
            temp.push_back(sortedVertices[s]);
        }

        sortedVertices = temp;
    }

    _enemyRegionVertices = sortedVertices;

	// Set the initial index to the vertex closest to the enemy main, so we get scouting information as soon as possible
	double bestDist = 1000000;
	for (size_t i = 0; i < sortedVertices.size(); i++)
	{
		double dist = sortedVertices[i].getApproxDistance(enemyCenter);
		if (dist < bestDist)
		{
			bestDist = dist;
			_currentRegionVertexIndex = i;
		}
	}
}

// Call when the strategy wants the scout overlord to go to closest base
void ScoutManager::goOverlordClosestBase()
{
	_overlordClosestBase = true;
}
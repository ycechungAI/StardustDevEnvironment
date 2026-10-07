#include "DetectorManager.h"

using namespace UAlbertaBot;

DetectorManager::DetectorManager() 
	: unitClosestToEnemy(nullptr)
{ 
}

void DetectorManager::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
	const BWAPI::Unitset & detectorUnits = getUnits();
	if (detectorUnits.empty())
	{
		return;
	}

	// NOTE targets is a list of nearby enemies.
	// Currently unused. Could use it to avoid enemy fire, among other possibilities.
	for (size_t i(0); i<targets.size(); ++i)
	{
		// do something here if there's targets
	}

	// Find enemy cloaked units.
	// NOTE This code is unused, but it is potentially useful.
	cloakedUnitMap.clear();
	BWAPI::Unitset cloakedUnits;
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		// conditions for targeting
		// TODO is this good enough for arbiters?
		if (unit->getType().hasPermanentCloak() ||     // dark templar, observer
			unit->getType().isCloakable() ||           // wraith, ghost
			unit->getType() == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Lurker ||
			unit->isBurrowed() ||
			(unit->isVisible() && !unit->isDetected()))
		{
			cloakedUnits.insert(unit);
			cloakedUnitMap[unit] = false;
		}
	}

	// for each detectorUnit
	for (auto & detectorUnit : detectorUnits)
	{
		/*
		//Assign detector to closest cloaked enemy unit
		BWAPI::Unit closestCloaked;
		closestCloaked = closestCloakedUnit(cloakedUnits, detectorUnit);

		// Move the detector toward the closest cloaked enemy unit.
		if (closestCloaked && closestCloaked->getPosition().isValid() &&
			detectorUnit->getDistance(closestCloaked) > 200 &&
			detectorUnit->getDistance(closestCloaked) < 600)
		{
			cloakedUnitMap[closestCloaked] = true;
			Micro::SmartMove(detectorUnit, closestCloaked->getPosition());
		}
		*/

		// Move the detector toward the squad unit closest to the enemy.
		if (unitClosestToEnemy && unitClosestToEnemy->getPosition().isValid())
		{
			Micro::SmartMove(detectorUnit, unitClosestToEnemy->getPosition());
		}

	}
}

BWAPI::Unit DetectorManager::closestCloakedUnit(const BWAPI::Unitset & cloakedUnits, BWAPI::Unit detectorUnit)
{
	BWAPI::Unit closestCloaked = nullptr;
	int closestDist = 10000;

	for (auto & unit : cloakedUnits)
	{
		// if we haven't already assigned a detectorUnit to this cloaked unit
		if (!cloakedUnitMap[unit])
		{
			int dist = unit->getDistance(detectorUnit);

			if (!closestCloaked || (dist < closestDist))
			{
				closestCloaked = unit;
				closestDist = dist;
			}
		}
	}

	return closestCloaked;
}
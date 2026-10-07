#include "SurveyerManager.h"
#include "MapGrid.h"
#include <random>

using namespace UAlbertaBot;

SurveyerManager::SurveyerManager()
	: _firstSurveyor(nullptr)
{ 
}

void SurveyerManager::update(SquadOrder order)
{
	const BWAPI::Unitset & surveyerUnits = getUnits();

	if (surveyerUnits.empty())
	{
		return;
	}

	return;
}

void SurveyerManager::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
	const BWAPI::Unitset & surveyerUnits = getUnits();
	if (surveyerUnits.empty())
	{
		return;
	}

	// NOTE targets is a list of nearby enemies.
	// Currently unused. Could use it to avoid enemy fire, among other possibilities.
	for (size_t i(0); i < targets.size(); ++i)
	{
		// do something here if there's targets
	}

	const BWEM::Base * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();
	const BWEM::Base * myMainBaseLocation = InformationManager::Instance().getMyMainBaseLocation();
	const BWEM::Base * myNaturalLocation = InformationManager::Instance().getMyNaturalLocation();
	BWAPI::Unit ourNatural = nullptr;
	ourNatural = InformationManager::Instance().getBaseDepot(InformationManager::Instance().getMyNaturalLocation());

	if (BWAPI::Broodwar->getFrameCount() < 10 && surveyerUnits.size() == 1)
	{
		for (auto & surveyor : surveyerUnits)
		{
			_firstSurveyor = surveyor;
		}
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

	// for each surveyorUnit
	int x = 0;
	for (auto & surveyor : surveyerUnits)
	{
		if (surveyor->getType() == BWAPI::UnitTypes::Zerg_Overlord) {
			// Set order position for a surveyer
			x++;

			std::uniform_int_distribution<int> uniform_dist(100, 300);
			std::random_device rng_seed;
			std::mt19937 rng(rng_seed());
			int w = uniform_dist(rng);        // a lot of trouble to get one random number...

			const BWEM::Base * ourBase = InformationManager::Instance().getMyMainBaseLocation();
			BWAPI::Position surveyorTarget(ourBase->Center());

			if (BWAPI::Broodwar->getFrameCount() < 10 ||
				(BWAPI::Broodwar->getFrameCount() % w == 0))
			{
				// Move the surveyor towards closest friendly base.
				const BWEM::Base * closestBase = InformationManager::Instance().getClosestFriendlyBaseLocation(surveyor->getTilePosition());

				if (closestBase)
				{
					BWAPI::TilePosition closestBaseTile = BWAPI::TilePosition(closestBase->Center());

					if (surveyor->getTilePosition().getApproxDistance(closestBaseTile) > 2)
					{
						surveyorTarget = BWAPI::Position(closestBaseTile);
						Micro::SmartMove(surveyor, surveyorTarget);
					}
				}
			}

			if (Config::Debug::DrawSquadInfo)
			{
				BWAPI::Broodwar->drawCircleMap(surveyorTarget, 10, BWAPI::Colors::Green, true);
				BWAPI::Broodwar->drawCircleMap(surveyorTarget, 100, BWAPI::Colors::Red, false);
				BWAPI::Broodwar->drawTextMap(surveyorTarget + BWAPI::Position(0, 10), "%s %d", "Surveyer", x);
			}
		}
	}

	return;
}

BWAPI::Position SurveyerManager::getNearestStartLocation()
{
	const BWEM::Base * ourBaseLocation = InformationManager::Instance().getMyMainBaseLocation();

	BWAPI::Position home(BWAPI::Broodwar->self()->getStartLocation());
	BWAPI::TilePosition location(ourBaseLocation->Center());
	double dist = 10000;
	double distNew = 0;

	// Pick closest startlocation that's not ours.
	for (BWAPI::TilePosition pos : BWAPI::Broodwar->getStartLocations())
	{
		if (!BWAPI::Broodwar->isExplored(pos))
		{
			distNew = home.getApproxDistance(BWAPI::Position(pos));
			if (distNew < dist) {
				location = pos;
				dist = distNew;
			}
		}
	}

	return BWAPI::Position(location);
}

BWAPI::Unit SurveyerManager::closestCloakedUnit(const BWAPI::Unitset & cloakedUnits, BWAPI::Unit detectorUnit)
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


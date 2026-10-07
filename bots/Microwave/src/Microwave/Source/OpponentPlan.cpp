#include "OpponentPlan.h"
#include "InformationManager.h"
#include "MapTools.h"
#include "PlayerSnapshot.h"

using namespace UAlbertaBot;

// Attempt to recognize what the opponent is doing, so we can cope with it.
// For now, only try to recognize a small number of opening situations that require
// different handling.

// This is part of the OpponentModel module. Access should normally be through the OpponentModel instance.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

bool OpponentPlan::fastPlan(OpeningPlan plan)
{
	return
		plan == OpeningPlan::Proxy ||
		plan == OpeningPlan::WorkerRush ||
		plan == OpeningPlan::FastRush;
}

// Return whether the enemy has a building in our main or natural base.
// Returns the plan as Proxy, Contain for a more distant defense building, or Unknown for none.
bool OpponentPlan::recognizeProxy()
{
	const auto mainZone = InformationManager::Instance().getMyMainBaseLocation()->GetArea();
	const auto naturalZone = InformationManager::Instance().getMyNaturalLocation() ? InformationManager::Instance().getMyNaturalLocation()->GetArea() : mainZone;
	bool naturalTaken = InformationManager::Instance().getMyNaturalLocation();

	for (const auto & kv : InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isBuilding() &&
			!ui.goneFromLastPosition &&
			!ui.type.isRefinery() &&
			ui.type != BWAPI::UnitTypes::Terran_Engineering_Bay &&
			ui.type != BWAPI::UnitTypes::Terran_Supply_Depot &&
			ui.type != BWAPI::UnitTypes::Protoss_Pylon)
		{
			const auto zone = BWEMmap.GetArea(BWAPI::TilePosition(ui.lastPosition));
			if (zone)
			{
				// NOTE A forward forge can only mean cannons.
				const bool possibleContain =
					ui.type == BWAPI::UnitTypes::Terran_Bunker ||
					ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
					ui.type == BWAPI::UnitTypes::Protoss_Forge ||
					ui.type == BWAPI::UnitTypes::Zerg_Creep_Colony ||
					ui.type == BWAPI::UnitTypes::Zerg_Sunken_Colony;

				const int mainDist = ui.lastPosition.getApproxDistance(InformationManager::Instance().getMyMainBaseLocation()->Center());
				if (!possibleContain)
				{
					if (zone == mainZone || mainDist <= 32 * 32)
					{
						return true;
					}
				}

				const int natDist = InformationManager::Instance().getMyNaturalLocation() ? ui.lastPosition.getApproxDistance(InformationManager::Instance().getMyNaturalLocation()->Center()) : 999999;
				if (naturalTaken || !possibleContain)
				{
					if (zone == naturalZone || natDist <= 32 * 32)
					{
						return true;
					}
				}

				// If it's static defense, the enemy may be trying to contain us with it.
				if (possibleContain)
				{
					if (zone == mainZone || mainDist <= 32 * 32)
					{
						return true;
					}
					if (zone == naturalZone || natDist <= 32 * 32)
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

// NOTE Incomplete test! We don't measure the distance of enemy units from the enemy base,
//      so we don't recognize all the rushes that we should.
bool OpponentPlan::recognizeWorkerRush()
{
	BWAPI::Position myOrigin = BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());

	int enemyWorkerRushCount = 0;

	for (const auto & kv : InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isWorker() && ui.unit->isVisible() && myOrigin.getApproxDistance(ui.unit->getPosition()) < 1000)
		{
			++enemyWorkerRushCount;
		}
	}

	return enemyWorkerRushCount >= 3;
}

// Factory, possibly with starport, and no sign of many marines intended.
bool OpponentPlan::recognizeFactoryTech()
{
	if (BWAPI::Broodwar->enemy()->getRace() != BWAPI::Races::Terran)
	{
		return false;
	}

	int nMarines = 0;
	int nBarracks = 0;
	int nTechProduction = 0;
	bool tech = false;

	for (const auto & kv : InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Terran_Marine)
		{
			++nMarines;
		}

		else if (ui.type.whatBuilds().first == BWAPI::UnitTypes::Terran_Barracks)
		{
			return false;			// academy implied, marines seem to be intended
		}

		else if (ui.type == BWAPI::UnitTypes::Terran_Barracks)
		{
			++nBarracks;
		}

		else if (ui.type == BWAPI::UnitTypes::Terran_Academy)
		{
			return false;			// marines seem to be intended
		}

		else if (ui.type == BWAPI::UnitTypes::Terran_Factory ||
			ui.type == BWAPI::UnitTypes::Terran_Starport)
		{
			++nTechProduction;
		}

		else if (ui.type.whatBuilds().first == BWAPI::UnitTypes::Terran_Factory ||
			ui.type.whatBuilds().first == BWAPI::UnitTypes::Terran_Starport ||
			ui.type == BWAPI::UnitTypes::Terran_Armory)
		{
			tech = true;			// indicates intention to rely on tech units
		}
	}

	if ((nTechProduction >= 2 || tech) && nMarines <= 6 && nBarracks <= 1)
	{
		return true;
	}

	return false;
}

void OpponentPlan::recognize()
{
	// Don't recognize island plans.
	// The regular plans and reactions do not make sense for island maps.
	if (MapTools::Instance().hasIslandBases())
	{
		return;
	}

	// Recognize fast plans first, slow plans below.

	// Recognize in-base proxy buildings and slightly more distant Contain buildings.
	if (recognizeProxy())
	{
		_openingPlan = OpeningPlan::Proxy;
		_planIsFixed = true;
		return;
	}

	int frame = BWAPI::Broodwar->getFrameCount();

	// Recognize worker rushes.
	if (frame < 3000 && recognizeWorkerRush())
	{
		_openingPlan = OpeningPlan::WorkerRush;
		return;
	}

	PlayerSnapshot snap;
	snap.takeEnemy();

	//DEBUG
	/*
	std::stringstream msg;
	msg << frame << '\n'
	    << snap.debugString()
		<< OpeningPlanString(_openingPlan) << '\n' << '\n';
	Logger::LogAppendToFile(Config::Debug::ErrorLogFilename, msg.str());
	*/

	// Recognize fast rushes.
	// TODO consider distance and speed: when might units have been produced?
	//      as it stands, 4 pool is unrecognized half the time because lings are seen too late
	if (
		frame < 1600 && snap.count(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0 ||
		frame < 3200 && snap.count(BWAPI::UnitTypes::Zerg_Zergling) > 0 ||
		frame < 1750 && snap.count(BWAPI::UnitTypes::Protoss_Gateway) > 0 ||
		frame < 3300 && snap.count(BWAPI::UnitTypes::Protoss_Zealot) > 0 ||
		frame < 1400 && snap.count(BWAPI::UnitTypes::Terran_Barracks) > 0 ||
		frame < 3000 && snap.count(BWAPI::UnitTypes::Terran_Marine) > 0
		)
	{
		_openingPlan = OpeningPlan::FastRush;
		_planIsFixed = true;
		return;
	}

	// Plans below here are slow plans. Do not overwrite a fast plan with a slow plan.
	if (fastPlan(_openingPlan))
	{
		return;
	}

	// When we know the enemy is not doing a fast plan, set it
	// May get overridden by a more appropriate plan below later on
	/*if (
		_openingPlan == OpeningPlan::Unknown && 
		(snap.count(BWAPI::UnitTypes::Zerg_Drone) > 6 ||     // 4- or 5-pool
		snap.count(BWAPI::UnitTypes::Terran_SCV) > 8 ||     // BBS
		snap.count(BWAPI::UnitTypes::Protoss_Probe) > 9) || // 9-gate
		frame > 8000 // Failsafe if we have no other information at this point
		)
	{
		_openingPlan = OpeningPlan::NotFastRush;
	}*/

	// Recognize slower rushes.
	// TODO make sure we've seen the bare geyser in the enemy base!
	// TODO seeing an enemy worker carrying gas also means the enemy has gas
	if (
		frame < 5500 &&
		snap.count(BWAPI::UnitTypes::Zerg_Zergling) > 10
		||
		snap.count(BWAPI::UnitTypes::Zerg_Hatchery) >= 1 &&
		snap.count(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0 &&
		snap.count(BWAPI::UnitTypes::Zerg_Extractor) == 1 &&
		snap.count(BWAPI::UnitTypes::Zerg_Zergling) > 6
		||
		InformationManager::Instance().getNumBases(BWAPI::Broodwar->enemy()) <= 1 &&
		snap.count(BWAPI::UnitTypes::Zerg_Hatchery) >= 2 &&
		snap.count(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0 &&
		snap.count(BWAPI::UnitTypes::Zerg_Extractor) == 0 &&
		snap.count(BWAPI::UnitTypes::Zerg_Zergling) > 6
		||
		snap.count(BWAPI::UnitTypes::Terran_Barracks) >= 2 &&
		snap.count(BWAPI::UnitTypes::Terran_Refinery) == 0 &&
		snap.count(BWAPI::UnitTypes::Terran_Command_Center) <= 1 &&
		snap.count(BWAPI::UnitTypes::Terran_Marine) > 3
		||
		snap.count(BWAPI::UnitTypes::Protoss_Gateway) >= 2 &&
		snap.count(BWAPI::UnitTypes::Protoss_Assimilator) == 0 &&
		snap.count(BWAPI::UnitTypes::Protoss_Nexus) <= 1 &&
		snap.count(BWAPI::UnitTypes::Protoss_Zealot) > 3
		)
	{
		_openingPlan = OpeningPlan::HeavyRush;
		_planIsFixed = true;
		return;
	}

	// Recognize terran factory tech openings.
	if (recognizeFactoryTech())
	{
		_openingPlan = OpeningPlan::Factory;
		return;
	}

	// Recognize expansions with pre-placed static defense.
	// Zerg can't do this.
	// NOTE Incomplete test! We don't check the location of the static defense
	if (InformationManager::Instance().getNumBases(BWAPI::Broodwar->enemy()) >= 2)
	{
		if (snap.count(BWAPI::UnitTypes::Terran_Bunker) > 0 ||
			snap.count(BWAPI::UnitTypes::Protoss_Photon_Cannon) > 0)
		{
			_openingPlan = OpeningPlan::SafeExpand;
			return;
		}
	}

	// Recognize a naked expansion.
	// This has to run after the SafeExpand check, since it doesn't check for what's missing.
	if (InformationManager::Instance().getNumBases(BWAPI::Broodwar->enemy()) >= 2)
	{
		_openingPlan = OpeningPlan::NakedExpand;
		return;
	}

	// Recognize a turtling enemy.
	// NOTE Incomplete test! We don't check where the defenses are placed.
	if (InformationManager::Instance().getNumBases(BWAPI::Broodwar->enemy()) < 2)
	{
		if (snap.count(BWAPI::UnitTypes::Terran_Bunker) >= 2 ||
			snap.count(BWAPI::UnitTypes::Protoss_Photon_Cannon) >= 2 ||
			snap.count(BWAPI::UnitTypes::Zerg_Sunken_Colony) >= 2)
		{
			_openingPlan = OpeningPlan::Turtle;
			return;
		}
	}

	// Nothing recognized: Opening plan remains unchanged.
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

OpponentPlan::OpponentPlan()
	: _openingPlan(OpeningPlan::Unknown)
	, _planIsFixed(false)
{
}

// Update the recognized plan.
// Call this every frame. It will take care of throttling itself down to avoid unnecessary work.
void OpponentPlan::update()
{
	if (!Config::Strategy::UsePlanRecognizer)
	{
		return;
	}

	// The plan is decided. Don't change it any more.
	if (_planIsFixed)
	{
		return;
	}

	int frame = BWAPI::Broodwar->getFrameCount();

	if (frame > 100 && frame < 10000 &&      // only try to recognize openings
		frame % 12 == 7)                     // update interval
	{
		recognize();
	}
}

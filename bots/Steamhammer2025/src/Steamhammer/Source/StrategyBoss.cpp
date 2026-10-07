#include "StrategyBoss.h"

// Parent class of StrategyBossTerran and StrategyBossProtoss.

#include "Bases.h"
#include "Maker.h"
#include "ProductionManager.h"
#include "The.h"
#include "UABAssert.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

// Predict when we might need detection, air defense, etc.
void StrategyBoss::makePredictions()
{
	// TODO not implemented

	_initialized = true;
}

// A general tech building is any building type other than supply, refinery,
// and static defense.
// E.g. a barracks counts because it allows the production of marines and bunkers.
// It is not the same as UnitUtil::IsTechBuildingType(), it's broader.
// NOTE This version does not support zerg!
bool StrategyBoss::isTechBuilding(BWAPI::UnitType type)
{
	return
		type.isBuilding() &&
		type != the.selfRace().getSupplyProvider() &&
		type != the.selfRace().getRefinery() &&
		type != BWAPI::UnitTypes::Terran_Bunker &&
		type != BWAPI::UnitTypes::Terran_Missile_Turret &&
		type != BWAPI::UnitTypes::Protoss_Pylon &&
		type != BWAPI::UnitTypes::Protoss_Photon_Cannon &&
		type != BWAPI::UnitTypes::Protoss_Shield_Battery;
}

// Maintain a set of my completed tech buildings, by type.
// And set a flag: Has the set changed?
// We want to regenerate the rules when the available tech changes.
void StrategyBoss::updateTechBuildings()
{
	_techBuildingsChangedThisFrame = false;

	for (auto it = _techBuildings.begin(); it != _techBuildings.end(); )
	{
		if (the.my.completed.count(*it) == 0)
		{
			it = _techBuildings.erase(it);
			_techBuildingsChangedThisFrame = true;
		}
		else
		{
			++it;
		}
	}

	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (u->isCompleted() && isTechBuilding(u->getType()))
		{
			auto result = _techBuildings.insert(u->getType());
			if (result.second)
			{
				_techBuildingsChangedThisFrame = true;
			}
		}
	}
}

// Updates that do not depend on the matchup,
// or are needed to decide whether to update the rules.
void StrategyBoss::updateStatus()
{
	_nBases = the.bases.completedBaseCount(the.self());
	_maxWorkers = WorkerManager::Instance().getMaxWorkers();
	_mineralWorkers = WorkerManager::Instance().getNumMineralWorkers();
	the.bases.gasCounts(_nGas, _nFreeGas);

	updateTechBuildings();			// might be used in isEmergency()

	_emergency = isEmergency();		// does depend on the matchup

	// For telling whether an emergency has started or ended.
	_lastFrameEmergency = _emergency;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Protected.

// We have a unit of the given type, or one is in production.
// Checks the building queue if the type is a building.
// Don't need to check the production queue: Maker pauses while anything is queued there.
// Used for checking whether a preprequisite is in production.
bool StrategyBoss::gettingUnit(BWAPI::UnitType type)
{
	return
		the.my.all.count(type) > 0 ||
		type.isBuilding() && BuildingManager::Instance().getNumUnstarted(type) > 0;
}

// We have a completed unit of the given type.
bool StrategyBoss::hasUnit(BWAPI::UnitType type)
{
	return the.my.completed.count(type) > 0;
}

// Count my combat supply.
// NOTE The name is about the future purpose I intend.
void StrategyBoss::analyzeMyMix()
{
	_mySupply = 0;
	for (std::pair<BWAPI::UnitType, int> typeCount : the.your.seen.getCounts())
	{
		if (UnitUtil::IsCombatUnit(typeCount.first) &&
			!typeCount.first.isWorker() &&
			typeCount.first.spaceProvided() == 0)				// exclude transports
		{
			_mySupply += typeCount.second * typeCount.first.supplyRequired();
		}
	}
}

// We want to choose a unit mix that counters the opponent's.
void StrategyBoss::analyzeOpponentMixT()
{
	detectors = the.your.seen.count(BWAPI::UnitTypes::Terran_Science_Vessel);
	droppers = the.your.seen.count(BWAPI::UnitTypes::Terran_Dropship);

	ghost = the.your.seen.count(BWAPI::UnitTypes::Terran_Ghost);

	// Supply of each combat unit type.
	int nMarines = 2 * the.your.seen.count(BWAPI::UnitTypes::Terran_Marine);
	int nInfantry = 2 * (
		the.your.seen.count(BWAPI::UnitTypes::Terran_Marine) +
		the.your.seen.count(BWAPI::UnitTypes::Terran_Firebat) +
		the.your.seen.count(BWAPI::UnitTypes::Terran_Medic) +
		ghost);
	int nTanks = 4 * (
		the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) +
		the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode));
	int nVultures = 4 * the.your.seen.count(BWAPI::UnitTypes::Terran_Vulture);
	int nGoliaths = 4 * the.your.seen.count(BWAPI::UnitTypes::Terran_Goliath);
	int nWraiths = 4 * the.your.seen.count(BWAPI::UnitTypes::Terran_Wraith);
	int nValkyries = 6 * the.your.seen.count(BWAPI::UnitTypes::Terran_Valkyrie);
	int nBattlecruisers = 12 * the.your.seen.count(BWAPI::UnitTypes::Terran_Battlecruiser);

	enemySupply = nTanks + nVultures + nGoliaths + nInfantry + nWraiths + nValkyries + nBattlecruisers;

	marine = double(nMarines) / enemySupply;
	infantry = double(nInfantry) / enemySupply;

	tank = double(nTanks) / enemySupply;
	vulture = double(nVultures) / enemySupply;
	goliath = double(nGoliaths) / enemySupply;
	mech = tank + vulture + goliath;

	wraith = double(nWraiths) / enemySupply;
	valkyrie = double(nValkyries) / enemySupply;
	battlecruiser = double(nBattlecruisers) / enemySupply;
	enemyAir = wraith + valkyrie + battlecruiser;
}

void StrategyBoss::analyzeOpponentMixP()
{
	detectors = the.your.seen.count(BWAPI::UnitTypes::Protoss_Observer);
	droppers = the.your.seen.count(BWAPI::UnitTypes::Protoss_Shuttle);

	darkArchon = the.your.seen.count(BWAPI::UnitTypes::Protoss_Dark_Archon);
	arbiter = the.your.seen.count(BWAPI::UnitTypes::Protoss_Arbiter);

	// Supply of each combat unit type.
	int nZealots = 4 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Zealot);
	int nDragoon = 4 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Dragoon);
	int nReaver = 8 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Reaver);
	int nTemplar = 4 * the.your.seen.count(BWAPI::UnitTypes::Protoss_High_Templar);
	int nDt = 4 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Dark_Templar);
	int nArchon = 8 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Archon);
	int nCorsair = 4 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Corsair);
	int nScout = 6 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Scout);
	int nCarrier = 12 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Carrier);

	enemySupply = nZealots + nDragoon + nReaver + nTemplar + nDt + nArchon + nCorsair + nScout + nCarrier;

	zealot = double(nZealots) / enemySupply;
	dragoon = double(nDragoon) / enemySupply;
	reaver = double(nReaver) / enemySupply;
	templar = double(nTemplar) / enemySupply;
	dt = double(nDt) / enemySupply;
	archon = double(nArchon) / enemySupply;
	corsair = double(nCorsair) / enemySupply;
	scout = double(nScout) / enemySupply;
	carrier = double(nCarrier) / enemySupply;
	
	infantry = zealot + dragoon;
	enemyAir = corsair + scout + carrier;
}

void StrategyBoss::analyzeOpponentMixZ()
{
	detectors = the.your.seen.count(BWAPI::UnitTypes::Zerg_Overlord);
	droppers = the.info.enemyHasTransport() ? detectors : 0;

	scourge = the.your.seen.count(BWAPI::UnitTypes::Zerg_Scourge);
	queen = the.your.seen.count(BWAPI::UnitTypes::Zerg_Queen);
	defiler = the.your.seen.count(BWAPI::UnitTypes::Zerg_Defiler);

	int nZergling = the.your.seen.count(BWAPI::UnitTypes::Zerg_Zergling);
	int nHydra = 2 * the.your.seen.count(BWAPI::UnitTypes::Zerg_Hydralisk);
	int nlurker = 4 * the.your.seen.count(BWAPI::UnitTypes::Zerg_Lurker);
	int nUltra = 8 *the.your.seen.count(BWAPI::UnitTypes::Zerg_Ultralisk);
	int nMuta = 4 * the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk);
	int nGuardian = 4 * the.your.seen.count(BWAPI::UnitTypes::Zerg_Guardian);
	int nDevourer = 4 * the.your.seen.count(BWAPI::UnitTypes::Zerg_Devourer);

	enemySupply = nZergling + nHydra + nlurker + nUltra + nMuta + nGuardian + nDevourer;

	zealot = double(nZergling) / enemySupply;
	hydralisk = double(nHydra) / enemySupply;
	lurker = double(nlurker) / enemySupply;
	ultralisk = double(nUltra) / enemySupply;
	mutalisk = double(nMuta) / enemySupply;
	guardian = double(nGuardian) / enemySupply;
	devourer = double(nDevourer) / enemySupply;

	infantry = zergling + hydralisk;
	enemyAir = mutalisk + guardian + defiler;
}

// Time remaining to complete the first of the given mobile unit type.
// If we already have one, the time remaining is zero.
// Return MAX_FRAME if we're not making one.
int StrategyBoss::trainTimeRemaining(BWAPI::UnitType unitType) const
{
	if (the.my.completed.count(unitType) > 0)
	{
		return 0;
	}

	int remaining = MAX_FRAME;

	for (BWAPI::Unit unit : the.self()->getUnits())
	{
		if (unit->isTraining() && unit->getTrainingQueue().front() == unitType)
		{
			remaining = std::min(remaining, unit->getRemainingTrainTime());
		}
	}
	return remaining;
}

// Time remaining to complete this upgrade, in frames.
// Return 0 if it's already done.
// Return MAX_FRAME if we're not upgrading it.
int StrategyBoss::upgradeTimeRemaining(BWAPI::UpgradeType upgrade, int level) const
{
	if (the.self()->getUpgradeLevel(upgrade) >= level)
	{
		return 0;
	}

	if (the.self()->getUpgradeLevel(upgrade) == level - 1)
	{
		// Find the unit that's doing the upgrade, if any.
		for (BWAPI::Unit unit : the.self()->getUnits())
		{
			if (unit->getUpgrade() == upgrade)
			{
				return unit->getRemainingUpgradeTime();
			}
		}
	}
	return MAX_FRAME;
}

void StrategyBoss::drawStrategyBossInfo() const
{
	if (!Config::Debug::DrawStrategyBossInfo || !ProductionManager::Instance().isOutOfBook())
	{
		return;
	}

	const int x = 500;
	int y = 30;

	BWAPI::Broodwar->drawTextScreen(x, y, "%cStrat Boss (%d rules)", white, the.maker.getRules().size());
	y += 13;
	BWAPI::Broodwar->drawTextScreen(x, y, "%c%s", cyan, getMixString().c_str());
	std::string aux = getAuxString();
	if (aux != "")
	{
		y += 10;
		BWAPI::Broodwar->drawTextScreen(x, y, "%c%s", cyan, aux.c_str());
	}
	if (inBuildupMode())
	{
		y += 10;
		BWAPI::Broodwar->drawTextScreen(x, y, "%cbuilding up mass", green);
	}
	y += 13;

	the.maker.drawRules(x, y);
}

// Can skip updating most frames.
bool StrategyBoss::shouldUpdate() const
{
	if (_emergency != _lastFrameEmergency)
	{
		//BWAPI::Broodwar->printf("emergency started or ended");
	}

	return
		the.maker.getRules().empty() ||
		_emergency != _lastFrameEmergency ||		// emergency just started or ended
		_emergency && the.now() % 7 == 0 ||
		_techBuildingsChangedThisFrame ||
		the.now() % UpdateInterval == 0;
}

void StrategyBoss::setRules()
{
	analyzeMyMix();
	setBasicsRules();
	if (the.enemyRace() == BWAPI::Races::Terran)
	{
		analyzeOpponentMixT();
		setRulesXvT();
	}
	else if (the.enemyRace() == BWAPI::Races::Protoss)
	{
		analyzeOpponentMixP();
		setRulesXvP();
	}
	else if (the.enemyRace() == BWAPI::Races::Zerg)
	{
		analyzeOpponentMixZ();
		setRulesXvZ();
	}
	else
	{
		// Treat an unknown race the same as protoss. We'll find out soon enough.
		setRulesXvP();
	}
}

double StrategyBoss::airToGroundT() const
{
	return wraith + battlecruiser;
}

double StrategyBoss::airToGroundZ() const
{
	return mutalisk + guardian;
}

double StrategyBoss::antiAirZ() const
{
	// NOTE scouge is a count, so it doesn't really fit into the scheme. But close enough.
	return hydralisk + scourge + mutalisk + devourer;
}

bool StrategyBoss::enemyMayHaveArbiters() const
{
	return
		arbiter > 0 ||
		the.your.seen.count(BWAPI::UnitTypes::Protoss_Arbiter_Tribunal) > 0 ||
		the.your.inferred.count(BWAPI::UnitTypes::Protoss_Arbiter_Tribunal) > 0;
}

bool StrategyBoss::expectLurkers() const
{
	return
		(the.your.seen.count(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0 ||
			the.your.inferred.count(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0) &&
		(the.your.seen.count(BWAPI::UnitTypes::Zerg_Lair) > 0 ||
			the.your.inferred.count(BWAPI::UnitTypes::Zerg_Lair) > 0);
}

bool StrategyBoss::expectMutalisks() const
{
	return
		(the.your.seen.count(BWAPI::UnitTypes::Zerg_Spire) > 0 ||
			the.your.inferred.count(BWAPI::UnitTypes::Zerg_Spire) > 0) ||

		!expectLurkers() &&
		(the.your.seen.count(BWAPI::UnitTypes::Zerg_Lair) > 0 ||
			the.your.inferred.count(BWAPI::UnitTypes::Zerg_Lair) > 0);
}

// Our current bases are low on minerals, either because they're mining out
// or because the enemy holds minerals under fire and we can't mine them.
// This remembers when it last ran, so that it doesn't run more than it needs to.
bool StrategyBoss::baseRunningLow() const
{
	const int interval = 30 * 24;
	static int lastCall = 8 * 60 * 24;

	if (the.now() < lastCall + interval)
	{
		return false;
	}

	lastCall = the.now();

	// Calculate the number of workers needed to mine minerals by the same method
	// as the mineral worker component of _maxWorkers, except that some mineral patches
	// are excluded:
	// Those under enemy fire, and those with <= 100 minerals left.
	int patches = 0;
	for (Base * base : the.bases.getAll())
	{
		if (base->isMyBase())
		{
			for (BWAPI::Unit mineral : base->getRemainingSafeMinerals())
			{
				if (the.info.getResourceAmount(mineral) > 100)
				{
					++patches;
				}
			}
		}
	}

	int continuingWorkers =
		1 + int(std::round(Config::Macro::WorkersPerPatch * patches));

	// True if we have enough workers that expanding to a mineral base will
	// employ some of them--at least as soon as the low patches mine out.
	return continuingWorkers < _mineralWorkers;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

StrategyBoss::StrategyBoss()
	: _initialized(false)
	, _lastFrameEmergency(false)
	, _buildupMode(false)
	, _airDefenseNeededFrame(MAX_FRAME)
	, _detectionNeededFrame(MAX_FRAME)
	, _emergency(false)
	, _emergencyDefense(false)
	, _allHands(false)
	, _techBuildingsChangedThisFrame(false)
	, _mySupply(0)

	, marine(0.0)
	, tank(0.0)
	, vulture(0.0)
	, goliath(0.0)
	, mech(0.0)
	, wraith(0.0)
	, valkyrie(0.0)
	, battlecruiser(0.0)

	, zealot(0.0)
	, dragoon(0.0)
	, reaver(0.0)
	, templar(0.0)
	, dt(0.0)
	, archon(0.0)
	, darkArchon(0)
	, corsair(0.0)
	, scout(0.0)
	, carrier(0.0)
	, arbiter(0)

	, zergling(0.0)
	, hydralisk(0.0)
	, lurker(0.0)
	, ultralisk(0.0)
	, queen(0)
	, mutalisk(0.0)
	, scourge(0)
	, guardian(0.0)
	, devourer(0.0)
	, defiler(0)
{};

// Called once per frame.
// Most frames, it doesn't do much.
void StrategyBoss::update()
{
	if (ProductionManager::Instance().isOutOfBook())
	{
		if (!_initialized)
		{
			makePredictions();
			strategyDecisions();
		}

		updateStatus();
		manageUrgentIssues();
		if (shouldUpdate())
		{
			the.maker.clearRules();
			recognizeEmergency();
			if (isEmergency())
			{
				setEmergencyRules();
			}
			manageBases();
			setRules();			// possibly added after emergency rules
		}
		drawStrategyBossInfo();
	}
}

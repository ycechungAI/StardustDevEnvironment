#include "CombatCommander.h"
#include "Random.h"
#include "UnitUtil.h"
#include "ProductionManager.h"
#include <chrono>

using namespace UAlbertaBot;

// Squad priorities: Which can steal units from others.
const size_t IdlePriority = 0;
const size_t SurveyPriority = 1;
const size_t AttackPriority = 2;
const size_t ReconPriority = 3;
const size_t BaseDefensePriority = 4;
const size_t ScoutDefensePriority = 5;
const size_t DropPriority = 7;			// don't steal from Drop squad for Defense squad
const size_t ScourgePriority = 8;		// all scourge and only scourge

// The attack squads
const int AttackRadius = 800;
const int DefendFrontRadius = 400;

// Recon squad
const int ReconTargetTimeout = 40 * 24;
const int ReconRadius = 400;

CombatCommander::CombatCommander()
	: _initialized(false)
	, _reconTarget(BWAPI::Positions::Invalid)   // it will be changed later
	, _lastReconTargetChange(0)
{
}

// Called once at the start of the game.
// You can also create new squads at other times.
void CombatCommander::initializeSquads()
{
	// Profile debug
	//PROFILE_FUNCTION();

	BWAPI::Position offset(64, 48);

	// The idle squad includes workers at work (not idle at all) and unassigned overlords.
    SquadOrder idleOrder(SquadOrderTypes::Idle, BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation()) + offset, 100, "Chill Out");
	_squadData.addSquad(Squad("Idle", idleOrder, IdlePriority));

    // the main attack squad will pressure the enemy's closest base location
	SquadOrder mainAttackOrder(getAttackOrder(nullptr));
	_squadData.addSquad(Squad("Ground", mainAttackOrder, AttackPriority));

	// The second attack squad will handle ground anti-air
	// It gets the same order as the main attack squad.
	//_squadData.addSquad(Squad("Ground #2", mainAttackOrder, AttackPriority));

	// The flying squad separates air units so they can act independently.
	// It gets the same order as the main attack squad.
	_squadData.addSquad(Squad("Flying", mainAttackOrder, AttackPriority));

	// The recon squad carries out reconnaissance in force to deny enemy bases.
	// It is filled in when enough units are available.
	Squad reconSquad = Squad("Recon", idleOrder, ReconPriority);
	reconSquad.setCombatSimRadius(200);  // combat sim includes units in a smaller radius than for a combat squad
	reconSquad.setFightVisible(true);    // combat sim sees only visible enemy units (not all known enemies)
	_squadData.addSquad(reconSquad);

	// the scout defense squad will handle chasing the enemy worker scout
	if (Config::Micro::ScoutDefenseRadius > 0)
	{
		BWAPI::Position ourBasePosition = BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());

		SquadOrder enemyScoutDefense(SquadOrderTypes::Defend, ourBasePosition, Config::Micro::ScoutDefenseRadius, "Get the scout");
		_squadData.addSquad(Squad("Scout Defense", enemyScoutDefense, ScoutDefensePriority));
	}

	// add a drop squad if we are using a drop strategy
    //if (Config::Strategy::StrategyName == "Protoss_Drop")
    //{
	//	SquadOrder doDrop(SquadOrderTypes::Drop, ourBasePosition, AttackRadius, "Wait for transport");
	//	_squadData.addSquad(Squad("Drop", doDrop, DropPriority));
    //}

	// Zerg can put overlords into a survey squad.
	if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg)
	{
		SquadOrder surveyOrder(SquadOrderTypes::Survey, BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation()) + offset, 100, "Chill Out");
		_squadData.addSquad(Squad("Survey", surveyOrder, SurveyPriority));
	}

    _initialized = true;
}

void CombatCommander::update(const BWAPI::Unitset & combatUnits)
{
	// Profile debug
	//PROFILE_FUNCTION();

    if (!Config::Modules::UsingCombatCommander)
    {
        return;
    }

    if (!_initialized)
    {
        initializeSquads();
    }

	_combatUnits = combatUnits;
	
	int frame8 = BWAPI::Broodwar->getFrameCount() % 8;

	if (frame8 == 1)
	{	
		updateIdleSquad();
		updateSurveySquad();
		//updateDropSquads();
        updateScoutDefenseSquad();
		updateDefenseSquads();
		updateReconSquad();
		updateAttackSquads();
	}

	_squadData.update();          // update() all the squads

	cancelDyingBuildings();
}

void CombatCommander::updateIdleSquad()
{
    Squad & idleSquad = _squadData.getSquad("Idle");
    for (auto & unit : _combatUnits)
    {
        // if it hasn't been assigned to a squad yet, put it in the low priority idle squad
        if (_squadData.canAssignUnitToSquad(unit, idleSquad))
        {
			_squadData.assignUnitToSquad(unit, idleSquad);
        }
    }
}

// Update the small recon squad which tries to find and deny enemy bases.
// All units in the recon squad are the same type, depending on what is available.
// Units available to the recon squad each have a "weight".
// Weights sum to no more than maxWeight, set below.
void CombatCommander::updateReconSquad()
{
	const int maxWeight = 6;
	Squad & reconSquad = _squadData.getSquad("Recon");

	chooseReconTarget(&reconSquad);

	// If nowhere needs seeing, disband the squad. We're done.
	if (!_reconTarget.isValid())
	{
		reconSquad.clear();
		return;
	}

	// Issue the order.
	SquadOrder reconOrder(SquadOrderTypes::Attack, _reconTarget, ReconRadius, "Recon in force");
	reconSquad.setSquadOrder(reconOrder);

	// What is already in the squad?
	int squadWeight = 0;
	int nMarines = 0;
	int nMedics = 0;
	for (const auto unit : reconSquad.getUnits())
	{
		squadWeight += weighReconUnit(unit);
		if (unit->getType() == BWAPI::UnitTypes::Terran_Marine)
		{
			++nMarines;
		}
		else if (unit->getType() == BWAPI::UnitTypes::Terran_Medic)
		{
			++nMedics;
		}
	}

	// If everything except the detector is gone, let the detector go too.
	// It can't carry out reconnaissance in force.
	if (squadWeight == 0 && !reconSquad.isEmpty())
	{
		reconSquad.clear();
	}

	// What is available to put into the squad?
	int availableWeight = 0;
	for (const auto unit : _combatUnits)
	{
		availableWeight += weighReconUnit(unit);
	}

	// The allowed weight of the recon squad. It should steal few units.
	int weightLimit = availableWeight >= 24
		? 2 + (availableWeight - 24) / 6
		: 0;
	if (weightLimit > maxWeight)
	{
		weightLimit = maxWeight;
	}

	// If the recon squad weighs more than it should, clear it and continue.
	// Also if all marines are gone, but medics remain.
	// Units will be added back in if they should be.
	if (squadWeight > weightLimit ||
		nMarines == 0 && nMedics > 0)
	{
		reconSquad.clear();
		squadWeight = 0;
		nMarines = nMedics = 0;
	}

	// Add units up to the weight limit.
	// In this loop, add no medics, and enough marines to allow for 2 medics.
	for (const auto unit : _combatUnits)
	{
		if (squadWeight >= weightLimit)
		{
			break;
		}
		BWAPI::UnitType type = unit->getType();
		if (MapTools::Instance().hasIslandBases() && !type.isFlyer())
		{
			continue;
		}
		int weight = weighReconUnit(type);
		if (weight > 0 && squadWeight + weight <= weightLimit && _squadData.canAssignUnitToSquad(unit, reconSquad))
		{
			if (type == BWAPI::UnitTypes::Terran_Marine)
			{
				if (nMarines * weight < maxWeight - 2 * weighReconUnit(BWAPI::UnitTypes::Terran_Medic))
				{
					_squadData.assignUnitToSquad(unit, reconSquad);
					squadWeight += weight;
					nMarines += 1;
				}
			}
			else if (type != BWAPI::UnitTypes::Terran_Medic)
			{
				_squadData.assignUnitToSquad(unit, reconSquad);
				squadWeight += weight;
			}
		}
	}

	// Now fill in any needed medics.
	if (nMarines > 0 && nMedics < 2)
	{
		for (const auto unit : _combatUnits)
		{
			if (squadWeight >= weightLimit || nMedics >= 2)
			{
				break;
			}
			if (unit->getType() == BWAPI::UnitTypes::Terran_Medic &&
				_squadData.canAssignUnitToSquad(unit, reconSquad))
			{
				_squadData.assignUnitToSquad(unit, reconSquad);
				squadWeight += weighReconUnit(BWAPI::UnitTypes::Terran_Medic);
				nMedics += 1;
			}
		}
	}
}

// The recon squad is allowed up to a certain "weight" of units.
int CombatCommander::weighReconUnit(const BWAPI::Unit unit) const
{
	return weighReconUnit(unit->getType());
}

// The recon squad is allowed up to a certain "weight" of units.
int CombatCommander::weighReconUnit(const BWAPI::UnitType type) const
{
	if (type == BWAPI::UnitTypes::Zerg_Zergling) return 4;
	if (type == BWAPI::UnitTypes::Zerg_Hydralisk) return 6;
	if (type == BWAPI::UnitTypes::Zerg_Scourge) return 6;
	if (type == BWAPI::UnitTypes::Zerg_Mutalisk) return 6;
	if (type == BWAPI::UnitTypes::Terran_Marine) return 4;
	if (type == BWAPI::UnitTypes::Terran_Medic) return 4;
	if (type == BWAPI::UnitTypes::Terran_Vulture) return 6;
	if (type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) return 6;
	if (type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) return 6;
	if (type == BWAPI::UnitTypes::Protoss_Zealot) return 4;
	if (type == BWAPI::UnitTypes::Protoss_Dragoon) return 6;
	if (type == BWAPI::UnitTypes::Protoss_Dark_Templar) return 6;
	if (type == BWAPI::UnitTypes::Protoss_Corsair) return 4;

	return 0;
}

// Keep the same reconnaissance target or switch to a new one, depending.
void CombatCommander::chooseReconTarget(const Squad * squad)
{
	bool change = false;       // switch targets?

	BWAPI::Position nextTarget = getReconLocation(squad);

	// There is nowhere that we need to see. Change to the invalid target.
	if (!nextTarget.isValid())
	{
		change = true;
	}

	// If the current target is invalid, we're starting up.
	else if (!_reconTarget.isValid())
	{
		change = true;
	}

	// If we have spent too long on one target, then probably we haven't been able to reach it.
	else if (BWAPI::Broodwar->getFrameCount() - _lastReconTargetChange >= ReconTargetTimeout)
	{
		change = true;
	}

	// If the target is in sight (of any unit, not only the recon squad) and empty of enemies, we're done.
	else if (BWAPI::Broodwar->isVisible(_reconTarget.x / 32, _reconTarget.y / 32))
	{
		BWAPI::Unitset enemies;
		MapGrid::Instance().GetUnits(enemies, _reconTarget, ReconRadius, false, true);
		// We don't particularly care about air units, even when we could engage them.
		for (auto it = enemies.begin(); it != enemies.end();)
		{
			if ((*it)->isFlying())
			{
				it = enemies.erase(it);
			}
			else
			{
				++it;
			}
		}
		if (enemies.empty())
		{
			change = true;
		}
	}

	if (change)
	{
		_reconTarget = nextTarget;
		_lastReconTargetChange = BWAPI::Broodwar->getFrameCount();
	}
}

// Choose an empty base location for the recon squad to check out.
// Called only by chooseReconTarget().
BWAPI::Position CombatCommander::getReconLocation(const Squad * squad)
{
	// Ground and air considerations.
	bool hasGround = true;
	bool hasAir = false;
	bool canAttackGround = true;
	bool canAttackAir = false;
	if (squad)
	{
		hasGround = squad->hasGround();
		hasAir = squad->hasAir();
		canAttackGround = squad->canAttackGround();
		canAttackAir = squad->canAttackAir();
	}

	// Get baselocations where our recon squad can go
	std::vector<const BWEM::Base *> choices;
	const BWEM::Base * mainbase = InformationManager::Instance().getMyMainBaseLocation();

	// Squads with no units
	if (squad && squad->getUnits().size() == 0)
	{
		return mainbase->Center();
	}

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (hasGround)
			{
				int distance = MapTools::Instance().getGroundTileDistance(mainbase->Location(), base.Location());
				if (distance < 0)
				{
					continue;
				}
			}
		
			if (InformationManager::Instance().getBaseOwner(&base) == BWAPI::Broodwar->neutral() && 
				!BWAPI::Broodwar->isVisible(BWAPI::TilePosition(base.Center())))
			{
				choices.push_back(&base);
			}
		}
	}

	// BWAPI::Broodwar->printf("%d recon squad choices", choices.size());

	// If there are none, return an invalid position.
	if (choices.empty())
	{
		return BWAPI::Positions::Invalid;
	}

	// Choose randomly.
	// We may choose the same target we already have. That's OK; if there's another choice,
	// we'll probably switch to it soon.
	const BWEM::Base * base = choices.at(Random::Instance().index(choices.size()));
	return base->Center();
}

void CombatCommander::updateAttackSquads()
{
	Squad & groundSquad = _squadData.getSquad("Ground");
	//Squad & groundAntiAirSquad = _squadData.getSquad("Ground #2");
	Squad & flyingSquad = _squadData.getSquad("Flying");

	bool groundSquadExists = groundSquad.hasCombatUnits();
/*
	bool groundAntiAirSquadExists = false;
	for (const auto unit : groundAntiAirSquad.getUnits())
	{
		if (isGroundAntiAirSquadUnit(unit->getType()))
		{
			groundAntiAirSquadExists = true;
			break;
		}
	}
*/
	bool flyingSquadExists = false;
	for (const auto unit : flyingSquad.getUnits())
	{
		if (isFlyingSquadUnit(unit->getType()))
		{
			flyingSquadExists = true;
			break;
		}
	}

	for (const auto unit : _combatUnits)
	{
		if (isFlyingSquadUnit(unit->getType()))
		{
			if (_squadData.canAssignUnitToSquad(unit, flyingSquad))
			{
				_squadData.assignUnitToSquad(unit, flyingSquad);
			}
		}

		// Certain flyers go into the flying squad only if it already exists.
		// Otherwise they go into the ground squad.
		else if (isOptionalFlyingSquadUnit(unit->getType()))
		{
			if (flyingSquadExists)
			{
				if (groundSquad.containsUnit(unit))
				{
					groundSquad.removeUnit(unit);
				}
				if (_squadData.canAssignUnitToSquad(unit, flyingSquad))
				{
					_squadData.assignUnitToSquad(unit, flyingSquad);
				}
			}
			else
			{
				if (flyingSquad.containsUnit(unit))
				{
					flyingSquad.removeUnit(unit);
					UAB_ASSERT(_squadData.canAssignUnitToSquad(unit, groundSquad), "can't go to ground");
				}
				if (_squadData.canAssignUnitToSquad(unit, groundSquad))
				{
					_squadData.assignUnitToSquad(unit, groundSquad);
				}
			}
		}
/*
		else if (isGroundAntiAirSquadUnit(unit->getType()))
		{
			if (_squadData.canAssignUnitToSquad(unit, groundAntiAirSquad))
			{
				_squadData.assignUnitToSquad(unit, groundAntiAirSquad);
			}
		}
*/
		// isGroundSquadUnit() is defined as a catchall, so it has to go last.
		else if (isGroundSquadUnit(unit->getType()))
		{
			if (_squadData.canAssignUnitToSquad(unit, groundSquad))
			{
				_squadData.assignUnitToSquad(unit, groundSquad);
			}
		}
	}

	// Add or remove detectors.
	bool wantDetector = wantSquadDetectors();
	maybeAssignDetector(groundSquad, wantDetector);
	//maybeAssignDetector(groundAntiAirSquad, wantDetector);
	maybeAssignDetector(flyingSquad, wantDetector);

	SquadOrder mainAttackOrder(getAttackOrder(&groundSquad));
	groundSquad.setSquadOrder(mainAttackOrder);

	//SquadOrder secondAttackOrder(getAttackOrder(&groundAntiAirSquad));
	//groundAntiAirSquad.setSquadOrder(secondAttackOrder);

	SquadOrder flyingAttackOrder(getAttackOrder(&flyingSquad));
	flyingSquad.setSquadOrder(flyingAttackOrder);
}

// Unit definitely belongs in the Flying squad.
bool CombatCommander::isFlyingSquadUnit(const BWAPI::UnitType type) const
{
	return
		type == BWAPI::UnitTypes::Zerg_Mutalisk ||
		type == BWAPI::UnitTypes::Terran_Wraith ||
		type == BWAPI::UnitTypes::Terran_Valkyrie ||
		type == BWAPI::UnitTypes::Terran_Battlecruiser ||
		type == BWAPI::UnitTypes::Protoss_Corsair ||
		type == BWAPI::UnitTypes::Protoss_Scout;
}

// Unit belongs in the Flying squad if the Flying squad exists, otherwise the Ground squad.
bool CombatCommander::isOptionalFlyingSquadUnit(const BWAPI::UnitType type) const
{
	return
		type == BWAPI::UnitTypes::Zerg_Scourge ||
		type == BWAPI::UnitTypes::Zerg_Devourer ||
		type == BWAPI::UnitTypes::Protoss_Carrier;
}

// Unit belongs in the ground squad and can shoot air
bool CombatCommander::isGroundAntiAirSquadUnit(const BWAPI::UnitType type) const
{
	return
		type == BWAPI::UnitTypes::Zerg_Hydralisk ||
		type == BWAPI::UnitTypes::Protoss_Dragoon ||
		type == BWAPI::UnitTypes::Protoss_Archon ||
		type == BWAPI::UnitTypes::Terran_Marine ||
		type == BWAPI::UnitTypes::Terran_Ghost ||
		type == BWAPI::UnitTypes::Terran_Goliath;
}

// Unit belongs in the ground squad.
// With the current definition, it includes everything except workers, so it captures
// everything that is not already taken: It should be the last condition checked.
bool CombatCommander::isGroundSquadUnit(const BWAPI::UnitType type) const
{
	return
		!type.isDetector() &&
		!type.isWorker();
}

void CombatCommander::updateDropSquads()
{
	// If we don't have a drop squad, then we don't want to drop.
	// It is created in initializeSquads().
	if (!_squadData.squadExists("Drop"))
	{
		return;
	}

    Squad & dropSquad = _squadData.getSquad("Drop");

    // figure out how many units the drop squad needs
    bool dropSquadHasTransport = false;
    int transportSpotsRemaining = 8;
    auto & dropUnits = dropSquad.getUnits();

    for (auto & unit : dropUnits)
    {
        if (unit->isFlying() && unit->getType().spaceProvided() > 0)
        {
            dropSquadHasTransport = true;
        }
        else
        {
            transportSpotsRemaining -= unit->getType().spaceRequired();
        }
    }

    // if there are still units to be added to the drop squad, do it
    if (transportSpotsRemaining > 0 || !dropSquadHasTransport)
    {
        // take our first amount of combat units that fill up a transport and add them to the drop squad
        for (auto & unit : _combatUnits)
        {
            // if this is a transport unit and we don't have one in the squad yet, add it
            if (!dropSquadHasTransport && (unit->getType().spaceProvided() > 0 && unit->isFlying()))
            {
                _squadData.assignUnitToSquad(unit, dropSquad);
                dropSquadHasTransport = true;
                continue;
            }

            if (unit->getType().spaceRequired() > transportSpotsRemaining)
            {
                continue;
            }

            // get every unit of a lower priority and put it into the attack squad
            if (!unit->getType().isWorker() && _squadData.canAssignUnitToSquad(unit, dropSquad))
            {
                _squadData.assignUnitToSquad(unit, dropSquad);
                transportSpotsRemaining -= unit->getType().spaceRequired();
            }
        }
    }
    // otherwise the drop squad is full, so execute the order
    else
    {
		SquadOrder dropOrder(SquadOrderTypes::Drop, getAttackLocation(&dropSquad), 300, "Go drop");
        dropSquad.setSquadOrder(dropOrder);
    }
}

void CombatCommander::updateScoutDefenseSquad() 
{
	if (Config::Micro::ScoutDefenseRadius == 0 || _combatUnits.empty())
	{
		return;
	}

    // if the current squad has units in it then we can ignore this
    Squad & scoutDefenseSquad = _squadData.getSquad("Scout Defense");
  
    // get the region that our base is located in
	const BWEM::Area * myRegion = InformationManager::Instance().getMyMainBaseLocation()->GetArea();
	if (!myRegion || !myRegion->Top().isValid())
    {
        return;
    }

    // get all of the enemy units in this region
	BWAPI::Unitset enemyUnitsInRegion;
    for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
    {
        if (BWEMmap.GetArea(BWAPI::TilePosition(unit->getPosition())) == myRegion)
        {
            enemyUnitsInRegion.insert(unit);
        }
    }

    // if there's an enemy worker in our region then assign someone to chase him
    bool assignScoutDefender = enemyUnitsInRegion.size() == 1 && (*enemyUnitsInRegion.begin())->getType().isWorker();

    // if our current squad is empty and we should assign a worker, do it
    if (scoutDefenseSquad.isEmpty() && assignScoutDefender)
    {
        // the enemy worker that is attacking us
        BWAPI::Unit enemyWorker = *enemyUnitsInRegion.begin();

        // get our worker unit that is mining that is closest to it
        BWAPI::Unit workerDefender = findClosestWorkerToTarget(_combatUnits, enemyWorker);

		if (enemyWorker && workerDefender)
		{
			// grab it from the worker manager and put it in the squad
			if (_squadData.canAssignUnitToSquad(workerDefender, scoutDefenseSquad))
			{
				_squadData.assignUnitToSquad(workerDefender, scoutDefenseSquad);
			}
		}
    }
    // if our squad is not empty and we shouldn't have a worker chasing then take him out of the squad
    else if (!scoutDefenseSquad.isEmpty() && !assignScoutDefender)
    {
        scoutDefenseSquad.clear();
    }
}

void CombatCommander::updateDefenseSquads() 
{
	if (_combatUnits.empty()) 
    { 
        return; 
    }
    
	const int baseDefenseRadius = 600;
	const int baseDefenseHysteresis = 300;

	const BWEM::Base * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();
	const BWEM::Area * enemyRegion = nullptr;
    if (enemyBaseLocation)
    {
        enemyRegion = enemyBaseLocation->GetArea();
    }

	// for each of our bases
	for (auto & base : InformationManager::Instance().getMyBases())
	{
        // don't defend inside the enemy region, this will end badly when we are stealing gas
        if (base->GetArea() == enemyRegion)
        {
            continue;
        }

		//BWAPI::Position regionCenter = BWAPI::Position(base->GetArea()->Top());
		BWAPI::Position baseCenter = BWAPI::Position(base->Center());
		if (!baseCenter.isValid())
		{
			continue;
		}

		// all of the enemy units in this base
		std::set<BWAPI::Unit> enemyUnitsInRegion;
        for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
        {
			// If it's a harmless air unit, don't worry about it for base defense.
			// TODO something more sensible
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord || 
				unit->getType() == BWAPI::UnitTypes::Protoss_Observer ||
				unit->isLifted())  // floating terran building
			{
				continue;
			}

			// If the enemy unit is not in the base, ignore it
			if (unit->getDistance(baseCenter) > baseDefenseRadius)
			{
				continue;
			}

			// Add it
			enemyUnitsInRegion.insert(unit);

			// Add any units close to it
			for (const auto closeUnit : BWAPI::Broodwar->enemy()->getUnits())
				if (unit != closeUnit && unit->getDistance(closeUnit) < 400)
					enemyUnitsInRegion.insert(closeUnit);
        }

        // we can ignore the first enemy worker in our base since we assume it is a scout
		// This is because we can't catch it early. Should skip this check when we can. 
		// TODO replace with something sensible
		for (auto & unit : enemyUnitsInRegion)
        {
            if (unit->getType().isWorker())
            {
                enemyUnitsInRegion.erase(unit);
                break;
            }
        }

        std::stringstream squadName;
        squadName << "Base Defense " << baseCenter.x << " " << baseCenter.y;
        
        // if there's nothing in this region to worry about
        if (enemyUnitsInRegion.empty())
        {
            // if a defense squad for this base exists, remove it
            if (_squadData.squadExists(squadName.str()))
            {
                _squadData.getSquad(squadName.str()).clear();
            }
            
            // and return, nothing to defend here
            continue;
        }
        else 
        {
            // if we don't have a squad assigned to this base already, create one
            if (!_squadData.squadExists(squadName.str()))
            {
                SquadOrder defendRegion(SquadOrderTypes::Defend, baseCenter, baseDefenseRadius, "Defend Base");
                _squadData.addSquad(Squad(squadName.str(), defendRegion, BaseDefensePriority));
            }
			// Update the squad order
			else
			{
				Squad & defendSquad = _squadData.getSquad(squadName.str());
				SquadOrder defendRegion(SquadOrderTypes::Defend, baseCenter, baseDefenseRadius, "Defend Base");
				defendSquad.setSquadOrder(defendRegion);
			}
        }

		int numEnemyFlyingInRegion = std::count_if(enemyUnitsInRegion.begin(), enemyUnitsInRegion.end(), [](BWAPI::Unit u) { return u->isFlying(); });
		int numEnemyGroundInRegion = std::count_if(enemyUnitsInRegion.begin(), enemyUnitsInRegion.end(), [](BWAPI::Unit u) { return !u->isFlying(); });
		//bool enemyHasAntiAir = std::any_of(enemyUnitsInRegion.begin(), enemyUnitsInRegion.end(), UnitUtil::CanAttackAir);

		// assign units to the squad
		UAB_ASSERT(_squadData.squadExists(squadName.str()), "Squad should exist: %s", squadName.str().c_str());
		Squad & defenseSquad = _squadData.getSquad(squadName.str());

		// start off assuming all enemy units in region are just workers
		int numDefendersPerEnemyUnit = 2;

        // figure out how many units we need on defense
	    int flyingDefendersNeeded = numDefendersPerEnemyUnit * numEnemyFlyingInRegion;
	    int groundDefendersNeeded = numDefendersPerEnemyUnit * numEnemyGroundInRegion;

		// Count static defense as air defenders.
		for (auto & unit : BWAPI::Broodwar->self()->getUnits()) {
			if ((unit->getType() == BWAPI::UnitTypes::Terran_Missile_Turret ||
				unit->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony) &&
				unit->isCompleted() && unit->isPowered() &&
				BWEMmap.GetArea(unit->getTilePosition()) == base->GetArea())
			{
				flyingDefendersNeeded -= 3;
			}
		}
		flyingDefendersNeeded = std::max(flyingDefendersNeeded, 2);

		// Count static defense as ground defenders.
		bool sunkenDefender = false;
		for (auto & unit : BWAPI::Broodwar->self()->getUnits()) {
			if ((unit->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
				unit->isCompleted() && unit->isPowered() &&
				BWEMmap.GetArea(unit->getTilePosition()) == base->GetArea())
			{
				sunkenDefender = true;
				groundDefendersNeeded -= 4;   // pretend a sunken can hold 2 zerglings
			}
		}
		groundDefendersNeeded = std::max(groundDefendersNeeded, 2);

		const bool wePulledWorkers =
			std::any_of(defenseSquad.getUnits().begin(), defenseSquad.getUnits().end(), BWAPI::Filter::IsWorker);

		// Pull workers only in narrow conditions.
		// Pulling workers (as implemented) can lead to big losses.
		bool pullWorkers =
			Config::Micro::WorkersDefendRush &&
			(numZerglingsInOurBase() > 0 || beingBuildingRushed());

		if (wePulledWorkers && !pullWorkers)
		{
			defenseSquad.releaseWorkers();
		}

		updateDefenseSquadUnits(defenseSquad, flyingDefendersNeeded, groundDefendersNeeded, pullWorkers);
	}

    // for each of our defense squads, if there aren't any enemy units near the position, remove the squad
	for (const auto & kv : _squadData.getSquads())
	{
		const Squad & squad = kv.second;
		const SquadOrder & order = squad.getSquadOrder();

		if (order.getType() != SquadOrderTypes::Defend || squad.isEmpty())
		{
			continue;
		}

		bool enemyUnitInRange = false;
		for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
		{
			if (unit->getPosition().getApproxDistance(order.getPosition()) < order.getRadius())
			{
				enemyUnitInRange = true;
				break;
			}
		}

		if (!enemyUnitInRange)
		{
			_squadData.getSquad(squad.getName()).clear();
		}
	}
}

void CombatCommander::updateDefenseSquadUnits(Squad & defenseSquad, const size_t & flyingDefendersNeeded, const size_t & groundDefendersNeeded, bool pullWorkers)
{
	// if there's nothing left to defend, clear the squad
	if (flyingDefendersNeeded == 0 && groundDefendersNeeded == 0 && pullWorkers ==  false)
	{
		defenseSquad.clear();
		return;
	}

	// NOTE Defenders can be double-counted as both air and ground defenders. It can be a mistake.
	const BWAPI::Unitset & squadUnits = defenseSquad.getUnits();
	size_t flyingDefendersInSquad = std::count_if(squadUnits.begin(), squadUnits.end(), UnitUtil::CanAttackAir);
	size_t groundDefendersInSquad = std::count_if(squadUnits.begin(), squadUnits.end(), UnitUtil::CanAttackGround);
	
	// add flying defenders if we still need them
	size_t flyingDefendersAdded = 0;
	while (flyingDefendersNeeded > flyingDefendersInSquad + flyingDefendersAdded)
	{
		BWAPI::Unit defenderToAdd = findClosestDefender(defenseSquad, defenseSquad.getSquadOrder().getPosition(), true, false);

		// if we find a valid flying defender, add it to the squad
		if (defenderToAdd)
		{
			_squadData.assignUnitToSquad(defenderToAdd, defenseSquad);
			++flyingDefendersAdded;
		}
		// otherwise we'll never find another one so break out of this loop
		else
		{
			break;
		}
	}

	// add ground defenders if we still need them
	size_t groundDefendersAdded = 0;
	while (groundDefendersNeeded > groundDefendersInSquad + groundDefendersAdded)
	{
		BWAPI::Unit defenderToAdd = findClosestDefender(defenseSquad, defenseSquad.getSquadOrder().getPosition(), false, pullWorkers);

		// if we find a valid ground defender add it
		if (defenderToAdd)
		{
			if (defenderToAdd->getType().isWorker())
			{
				WorkerManager::Instance().setCombatWorker(defenderToAdd);
			}
			_squadData.assignUnitToSquad(defenderToAdd, defenseSquad);
			++groundDefendersAdded;
		}
		// otherwise we'll never find another one so break out of this loop
		else
		{
			break;
		}
	}
}

// Choose a defender to join the base defense squad.
BWAPI::Unit CombatCommander::findClosestDefender(const Squad & defenseSquad, BWAPI::Position pos, bool flyingDefender, bool pullWorkers)
{
	BWAPI::Unit closestDefender = nullptr;
	int minDistance = 99999;

	for (auto & unit : _combatUnits)
	{
		if ((flyingDefender && !UnitUtil::CanAttackAir(unit)) ||
			(!flyingDefender && !UnitUtil::CanAttackGround(unit)))
		{
			continue;
		}

		if (!_squadData.canAssignUnitToSquad(unit, defenseSquad))
		{
			continue;
		}

		int dist = unit->getDistance(pos);

		// Pull workers only if requested, and not from distant bases.
		if (unit->getType().isWorker() && (!pullWorkers || dist > 25 * 32))
		{
			continue;
		}

		/*
		// If the enemy can't shoot up, prefer air units as defenders.
		if (!enemyHasAntiAir && unit->isFlying())
		{
			dist -= 12 * 32;     // may become negative - that's OK
		}
		*/

		if (dist < minDistance)
		{
			closestDefender = unit;
			minDistance = dist;
		}
	}

	return closestDefender;
}

// add overlords to the survey squad.
void CombatCommander::updateSurveySquad()
{
	if (!_squadData.squadExists("Survey"))
	{
		return;
	}

	Squad & surveySquad = _squadData.getSquad("Survey");
	for (const auto unit : _combatUnits)
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord && _squadData.canAssignUnitToSquad(unit, surveySquad))
		{
			_squadData.assignUnitToSquad(unit, surveySquad);
		}
	}
}

// Should squads have detectors assigned? Does not apply to all squads.
// Yes if the enemy has cloaked units. Also yes if the enemy is protoss and has observers
// and we have cloaked units--we want to shoot down those observers.
// Otherwise no if the detectors are in danger of dying.
bool CombatCommander::wantSquadDetectors() const
{
	if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Protoss &&
		InformationManager::Instance().enemyHasMobileDetection())
	{
		if (BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Cloaking_Field) ||
			BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Personnel_Cloaking) ||
			UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar) > 0 ||
			UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Arbiter) > 0 ||
			BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Burrowing) ||
			BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect))
		{
			return true;
		}
	}

	return
		BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Protoss ||      // observers should be safe-ish
		!InformationManager::Instance().enemyHasAntiAir() ||
		InformationManager::Instance().enemyCloakedUnitsSeen();
}

// Add or remove a given squad's detector, subject to availability.
// Because this checks the content of the squad, it should be called
// after any other units are added or removed.
void CombatCommander::maybeAssignDetector(Squad & squad, bool wantDetector)
{
	if (squad.hasDetector())
	{
		// If the detector is the only thing left in the squad, we don't want to keep it.
		if (!wantDetector || squad.getUnits().size() == 1)
		{
			for (BWAPI::Unit unit : squad.getUnits())
			{
				if (unit->getType().isDetector())
				{
					squad.removeUnit(unit);
					return;
				}
			}
		}
	}
	else
	{
		// Don't add a detector to an empty squad.
		if (wantDetector && !squad.getUnits().empty())
		{
			for (BWAPI::Unit unit : _combatUnits)
			{
				if (unit->getType().isDetector() && _squadData.canAssignUnitToSquad(unit, squad))
				{
					_squadData.assignUnitToSquad(unit, squad);
					return;
				}
			}
		}
	}
}

// Get our money back for stuff that is about to be destroyed.
// TODO does this work for protoss buildings?
void CombatCommander::cancelDyingBuildings()
{
	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		
		if (unit->getType().isBuilding() && 
			unit->getType() != BWAPI::UnitTypes::Zerg_Sunken_Colony &&
			unit->getType() != BWAPI::UnitTypes::Zerg_Spore_Colony &&
			unit->getType() != BWAPI::UnitTypes::Zerg_Creep_Colony &&
			!unit->isCompleted() &&
			unit->isUnderAttack() && 
			unit->getShields() + unit->getHitPoints() < 30)
		{
			if (unit->isMorphing() && unit->canCancelMorph()) {
				Log().Get() << "Cancelling dying " << unit->getType() << " @ " << unit->getTilePosition(); 
				unit->cancelMorph();
			}
			else if (unit->isBeingConstructed() && unit->canCancelConstruction()) {
				Log().Get() << "Cancelling dying " << unit->getType() << " @ " << unit->getTilePosition(); 
				unit->cancelConstruction();
			}
		}

		if ((unit->getType() == BWAPI::UnitTypes::Zerg_Egg ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Lurker_Egg ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Cocoon) &&
			!unit->isCompleted() &&
			unit->isUnderAttack() &&
			unit->getShields() + unit->getHitPoints() < 30)
		{
			if (unit->isMorphing() && unit->canCancelMorph()) {
				Log().Get() << "Cancelling dying " << unit->getType() << " @ " << unit->getTilePosition(); 
				unit->cancelMorph();
			}
		}
	}
}

void CombatCommander::drawSquadInformation(int x, int y)
{
	_squadData.drawSquadInformation(x, y);
}

// Create an attack order for the given squad (which may be null).
// For a squad with ground units, ignore targets which are not accessible by ground.
SquadOrder CombatCommander::getAttackOrder(const Squad * squad)
{
	// Ground and air considerations.
	bool hasGround = true;
	bool hasAir = false;
	bool canAttackGround = true;
	bool canAttackAir = false;
	if (squad)
	{
		hasGround = squad->hasGround();
		hasAir = squad->hasAir();
		canAttackGround = squad->canAttackGround();
		canAttackAir = squad->canAttackAir();
	}

	const BWEM::Base * mainbase = InformationManager::Instance().getMyMainBaseLocation();

	// 1. Clear any obstacles around our bases.
	// Most maps don't have any such thing, but see e.g. Benzene, Electric Circuit and Sparkle.
	// Only ground squads are sent to clear obstacles.
	if (squad && squad->getUnits().size() > 0 && hasGround && canAttackGround)
	{
		//BWAPI::Broodwar->printf("Destroy neutrals %s", squad->getName().c_str());

		// Find blockers, destructible neutral units that are very close to the base
		// and may interfere with its operation.
		// This does not include the minerals to mine!
		for (const auto unit : BWAPI::Broodwar->getStaticNeutralUnits())
		{
			if (unit &&
				unit->getPlayer() == BWAPI::Broodwar->neutral() &&
				unit->isTargetable() &&
				!unit->getInitialType().isInvincible() && // This will not include Khaydarin crystals which are not destructible
				!unit->getType().canMove() &&
				!unit->isFlying() &&
				unit->getType() != BWAPI::UnitTypes::Zerg_Egg &&
				unit->getType() != BWAPI::UnitTypes::Zerg_Lurker_Egg &&
				unit->getInitialType().getName().find("Khaydarin") == std::string::npos)
			{
				for (auto& area : BWEMmap.Areas())
				{
					for (const auto& base : area.Bases())
					{
						// This will not include refineries
						/*if(unit->getType() == BWAPI::UnitTypes::Zerg_Extractor ||
							unit->getType() == BWAPI::UnitTypes::Protoss_Assimilator ||
							unit->getType() == BWAPI::UnitTypes::Terran_Refinery)
						{
							continue;
						}*/

						// Only owned bases will be considered
						if (InformationManager::Instance().getBaseOwner(&base) != BWAPI::Broodwar->self())
						{
							continue;
						}
						/*
						// Ground squads ignore neutral units which they cannot reach.
						if (hasGround)
						{
							int dist = MapTools::Instance().getGroundTileDistance(base.Location(), unit->getTilePosition());
							//BWAPI::Broodwar->printf("neutral distance %d %d", unit->getID(), dist);
							if (dist < 0)
							{
								continue;
							}
						}
						*/
						// Use air distance to the neutral units from one of our bases
						int distance = unit->getDistance(base.Center());
						//BWAPI::Broodwar->printf("neutral distance %d %d", unit->getID(), distance);

						// Neutral units that are close to one of our bases
						if (distance >= 0 && distance <= 13 * 32)
						{
							//BWAPI::Broodwar->printf("Attack neutral units");
							BWAPI::Unit target = unit;
							return SquadOrder(SquadOrderTypes::DestroyNeutral, target->getInitialPosition(), 400, "Destroy neutrals");
						}
					}
					
				}

			}

		}

	}

	// 2. If we're defensive, look for a front line to hold. No attacks.
	bool _goAggressive = ProductionManager::Instance().getAggression();
	if (!_goAggressive)
	{
		//BWAPI::Broodwar->printf("Defend front");
		return SquadOrder(SquadOrderTypes::Attack, getDefenseLocation(), DefendFrontRadius, "Defend front");
	}

	// 3. Otherwise we are aggressive. Look for a spot to attack.
	return SquadOrder(SquadOrderTypes::Attack, getAttackLocation(squad), AttackRadius, "Attack enemy");
}

// Choose a point of attack for the given squad (which may be null).
BWAPI::Position CombatCommander::getAttackLocation(const Squad * squad)
{
	// Ground and air considerations.
	bool hasGround = true;
	bool hasAir = false;
	bool canAttackGround = true;
	bool canAttackAir = false;
	if (squad)
	{
		hasGround = squad->hasGround();
		hasAir = squad->hasAir();
		canAttackGround = squad->canAttackGround();
		canAttackAir = squad->canAttackAir();
	}
	
	const BWEM::Base * mainbase = InformationManager::Instance().getMyMainBaseLocation();
	const BWAPI::TilePosition squadLocation = mainbase->Location();
	if (hasAir && !hasGround) {
		const BWAPI::TilePosition squadLocation = BWAPI::TilePosition(squad->calcCenter());
	}

	// Squads with no units
	if (squad && squad->getUnits().size() == 0)
	{
		return mainbase->Center();
	}

	// 1. Attack the enemy base with the weakest defense and most drones.
	// Only if the squad can attack ground. Lift the command center and it is no longer counted as a base.
	if (canAttackGround)
	{
		int count = 0;
		int bestScore = -99999;
		const BWEM::Base * bestBase = nullptr;

		for (auto& area : BWEMmap.Areas())
		{
			for (const auto& base : area.Bases())
			{
				if (InformationManager::Instance().getBaseOwner(&base) == BWAPI::Broodwar->enemy())
				{
					// Ground squads ignore enemy bases which they cannot reach.
					if (hasGround)
					{
						int distance = MapTools::Instance().getGroundTileDistance(mainbase->Location(), base.Location());
						if (distance < 0)
						{
							continue;
						}
					}
					
					int score = 0;

					// The squad vanguard is the unit closest to the current attack location.
					// Prefer a new attack location which is close, preferably even closer.
					if (squad && squad->unitClosestToEnemy())
					{
						score -= squad->unitClosestToEnemy()->getDistance(base.Center()) / (16 * 32);
					}

					count++;
					std::vector<UnitInfo> enemies;
					InformationManager::Instance().getNearbyForce(enemies, base.Center(), BWAPI::Broodwar->enemy(), AttackRadius);

					for (const auto & enemy : enemies)
					{
						// Count enemies that are static defense or slow-moving units good for defense.
						if (enemy.type.isBuilding() ||
							enemy.type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
							enemy.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
							enemy.type == BWAPI::UnitTypes::Protoss_Reaver ||
							enemy.type == BWAPI::UnitTypes::Protoss_Arbiter ||
							enemy.type == BWAPI::UnitTypes::Protoss_High_Templar ||
							enemy.type == BWAPI::UnitTypes::Zerg_Lurker ||
							enemy.type == BWAPI::UnitTypes::Zerg_Guardian)
						{
							// If the unit can attack (some units of) the squad, count it.
							if (hasGround && UnitUtil::TypeCanAttackGround(enemy.type) ||		// doesn't recognize casters
								hasAir && UnitUtil::TypeCanAttackAir(enemy.type) ||				// doesn't recognize casters
								enemy.type == BWAPI::UnitTypes::Protoss_High_Templar)			// spellcaster
							{
								--score;
							}
						}

						// Count workers
						if (enemy.type.isWorker())
						{
							score++;
						}
					}
					//BWAPI::Broodwar->printf("Enemy base score %d", score);

					if (score > bestScore)
					{
						bestBase = &base;
						bestScore = score;
						//BWAPI::Broodwar->printf("Best base score %d", score);
					}
				}
			}
		}
		//BWAPI::Broodwar->printf("Enemy base count %d", count);

		if (bestBase)
		{
			//BWAPI::Broodwar->printf("Enemy weakest base");
			return bestBase->Center();
		}
	}
	
	
	// 2. Attack the enemy main unless we think it's empty.
	const BWEM::Base * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();
	if (enemyBaseLocation)
	{
		// Ground squads ignore enemy bases which they cannot reach.
		if (!hasGround || MapTools::Instance().getGroundTileDistance(mainbase->Location(), enemyBaseLocation->Location()) >= 0)
		{
			//BWAPI::Broodwar->printf("Enemy base");
			BWAPI::Position enemyBasePosition = enemyBaseLocation->Center();

			// If the enemy base hasn't been seen yet, go there.
			if (!BWAPI::Broodwar->isExplored(BWAPI::TilePosition(enemyBasePosition)))
			{
				//BWAPI::Broodwar->printf("Enemy base not seen");
				return enemyBasePosition;
			}

			// get all known enemy units in the area
			BWAPI::Unitset enemyUnitsInArea;
			MapGrid::Instance().GetUnits(enemyUnitsInArea, enemyBasePosition, 1000, false, true);

			for (const auto unit : enemyUnitsInArea)
			{
				if (unit->getType() != BWAPI::UnitTypes::Zerg_Larva &&
					(unit->isFlying() && canAttackAir || !unit->isFlying() && canAttackGround))
				{
					// Enemy base is not empty: Something interesting is in the enemy base area.
					return enemyBasePosition;
				}
			}
		}
	}
	
	// 3. Attack known enemy buildings.
	// We assume that a terran can lift the buildings; otherwise, the squad must be able to attack ground.
	//if (canAttackGround || BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Terran)
	//{
		for (const auto & kv : InformationManager::Instance().getUnitInfo(BWAPI::Broodwar->enemy()))
		{
			const UnitInfo & ui = kv.second;

			if (!ui.type.isBuilding() || 
				ui.type.isAddon() ||
				!ui.lastPosition.isValid() ||
				ui.goneFromLastPosition)
			{
				continue;
			}

			// Ground squads ignore enemy buildings which they cannot reach.
			if (hasGround)
			{
				int distance = MapTools::Instance().getGroundTileDistance(mainbase->Location(), BWAPI::TilePosition(ui.lastPosition));
				if (distance < 0)
				{
					continue;
				}
			}

			if (ui.unit->isFlying() && canAttackAir || !ui.unit->isFlying() && canAttackGround)
			{
				//BWAPI::Broodwar->printf("Visible enemy");
				return ui.lastPosition;
			}

			//if (ui.unit->isLifted())
			//{
			//	// The building is lifted. Only if the squad can hit it.
			//	if (canAttackAir)
			//	{
			//		//BWAPI::Broodwar->printf("Known buildings lifted");
			//		return ui.lastPosition;
			//	}
			//}
			//else
			//{
			//	// The building is not known for sure to be lifted.
			//	//BWAPI::Broodwar->printf("Known buildings");
			//	return ui.lastPosition;
			//}
		}
	//}

	// 4. Attack visible enemy
	for (const auto unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (!unit->exists() ||
			!unit->getPosition().isValid() ||
			!unit->isDetected() ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Larva)
		{
			continue;
		}

		// Ground squads ignore enemies which they cannot reach.
		if (hasGround)
		{
			int distance = MapTools::Instance().getGroundTileDistance(mainbase->Location(), unit->getTilePosition());
			if (distance < 0)
			{
				continue;
			}
		}

		if (unit->isFlying() && canAttackAir || !unit->isFlying() && canAttackGround)
		{
			//BWAPI::Broodwar->printf("Visible enemy");
			return unit->getPosition();
		}
	}

	// 5. We can't see anything so explore the map attacking along the way
	//BWAPI::Broodwar->printf("Least explored");
	return MapGrid::Instance().getLeastExplored(hasGround && !hasAir, BWAPI::Position(squadLocation));
}

// We're being defensive. Get the location to defend.
BWAPI::Position CombatCommander::getDefenseLocation()
{
	// Retreat to the main base, unless we change our mind below.
	//const BWEM::Base * base = InformationManager::Instance().getMyMainBaseLocation();
	auto pos = MapTools::Instance().getMainDefPosition();

	// If the natural has been taken, retreat there instead.
	const BWEM::Base * natural = InformationManager::Instance().getMyNaturalLocation();
	if (natural && BWAPI::Broodwar->self() == InformationManager::Instance().getBaseOwner(natural))
	{
		//base = natural;
		pos = BWAPI::Position(InformationManager::Instance().getNaturalChoke()->Center());
	}

	return pos;
}

BWAPI::Position CombatCommander::getSurveyLocation()
{
	//BWAPI::Broodwar->printf("getSurveyLocation");
	const BWEM::Base * ourBaseLocation = InformationManager::Instance().getMyMainBaseLocation();

	return ourBaseLocation->Center();
}

// Choose one worker to pull for scout defense.
BWAPI::Unit CombatCommander::findClosestWorkerToTarget(BWAPI::Unitset & unitsToAssign, BWAPI::Unit target)
{
	UAB_ASSERT(target != nullptr, "target was null");

	if (!target)
	{
		return nullptr;
	}

	BWAPI::Unit closestMineralWorker = nullptr;
	int closestDist = Config::Micro::ScoutDefenseRadius + 128;    // more distant workers do not get pulled

	for (auto & unit : unitsToAssign)
	{
		if (unit->getType().isWorker() && WorkerManager::Instance().isFree(unit))
		{
			int dist = unit->getDistance(target);
			if (unit->isCarryingMinerals())
			{
				dist += 96;
			}

			if (dist < closestDist)
			{
				closestMineralWorker = unit;
				dist = closestDist;
			}
		}
	}

	return closestMineralWorker;
}

// when do we want to defend with our workers?
int CombatCommander::defendWithWorkers()
{
	// defense radius
	int defenseRadius = 300;
	// enemy units near our workers
	int enemyUnitsNearWorkers = 0;

	// our home position
	const BWEM::Base * main = InformationManager::Instance().getMyMainBaseLocation();
	BWAPI::Position myBasePosition(main->Center());

	// fill the set with the types of units we're concerned about
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		// if it's a zergling or a worker we want to defend
		if ((unit->getType() == BWAPI::UnitTypes::Zerg_Zergling || unit->getType().isWorker()) &&
			unit->getDistance(myBasePosition) < defenseRadius)
		{
			enemyUnitsNearWorkers++;
		}
	}

	return enemyUnitsNearWorkers;
}

int CombatCommander::numZerglingsInOurBase()
{
    int concernRadius = 300;
    int zerglings = 0;

	const BWEM::Base * main = InformationManager::Instance().getMyMainBaseLocation();
	BWAPI::Position myBasePosition(main->Center());

	const BWEM::Base * natural = InformationManager::Instance().getMyNaturalLocation();
	BWAPI::Position myNaturalPosition(myBasePosition);
	if (natural)
	{
		BWAPI::Position myNaturalPosition(natural->Center());
	}

	// fill the set with the types of units we're concerned about
	for (auto unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		// if it's a zergling we want to defend (main)
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Zergling &&
			unit->getDistance(myBasePosition) < concernRadius)
		{
			++zerglings;
		}
		// if it's a zergling we want to defend (natural)
		else if (natural)
		{
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Zergling &&
				unit->getDistance(myNaturalPosition) < concernRadius)
			{
				++zerglings;
			}
		}
		

	}

    return zerglings;
}

// Is an enemy building near our base? If so, we may pull workers.
bool CombatCommander::beingBuildingRushed()
{
	// If we have units, there will be no need to pull workers.
	if (InformationManager::Instance().weHaveCombatUnits())
	{
		return false;
	}

	const BWEM::Base * main = InformationManager::Instance().getMyMainBaseLocation();
	BWAPI::Position myBasePosition(main->Center());

    // check to see if the enemy has buildings near our base
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->getType().isBuilding() && 
			unit->getDistance(myBasePosition) < 1200 &&
			!unit->isLifted())
		{
			// Completed static photon cannon is too dangerous
			if (unit->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon && unit->isCompleted())
			{
				return false;
			}
			// Completed static sunken colony is too dangerous
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony && unit->isCompleted())
			{
				return false;
			}

			return true;
		}
	}

    return false;
}

CombatCommander & CombatCommander::Instance()
{
	static CombatCommander instance;
	return instance;
}

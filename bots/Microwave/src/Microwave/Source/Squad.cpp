#include "Common.h"
#include "Squad.h"
#include "UnitUtil.h"
#include "ProductionManager.h"
#include "UAlbertaBotModule.h"
#include <random>

using namespace UAlbertaBot;

Squad::Squad()
	: _name("Default")
	, _combatSquad(false)
	, _combatSimRadius(Config::Micro::CombatRegroupRadius)
	, _fightVisibleOnly(false)
	, _enemyHasAir(false)
	, _enemyHasGround(false)
	, _enemyCanAttackAir(false)
	, _enemyCanAttackGround(false)
	, _hasAir(false)
	, _hasGround(false)
	, _canAttackAir(false)
	, _canAttackGround(false)
	, _attackAtMax(false)
    , _lastRetreatSwitch(0)
    , _lastRetreatSwitchVal(true)
	, _lastScoreVal(-1)
	, _lastAttack(BWAPI::Broodwar->getFrameCount())
	, _lastRetreat(BWAPI::Broodwar->getFrameCount())
    , _priority(0)
{
    int a = 10;
}

Squad::Squad(const std::string & name, SquadOrder order, size_t priority) 
	: _name(name)
	, _order(order)
	, _combatSquad(name != "Idle")
	, _combatSimRadius(Config::Micro::CombatRegroupRadius)
	, _fightVisibleOnly(false)
	, _enemyHasAir(false)
	, _enemyHasGround(false)
	, _enemyCanAttackAir(false)
	, _enemyCanAttackGround(false)
	, _hasAir(false)
	, _hasGround(false)
	, _canAttackAir(false)
	, _canAttackGround(false)
	, _attackAtMax(false)
    , _lastRetreatSwitch(0)
    , _lastRetreatSwitchVal(true)
	, _lastScoreVal(-1)
	, _lastAttack(BWAPI::Broodwar->getFrameCount())
	, _lastRetreat(BWAPI::Broodwar->getFrameCount())
    , _priority(priority)
{
	setSquadOrder(order);
}

Squad::~Squad()
{
    clear();
}

void Squad::update()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// update all necessary unit information within this squad
	updateUnits();

	if (_units.empty())
	{
		//_regroupStatus = std::string("\x04 No attackers available");
		return;
	}

	// If this squad does not have an order
	if (_order.getType() == SquadOrderTypes::None)
	{
		return;
	}

	// If this is a worker squad, there is nothing more to do.
	if (!_combatSquad)
	{
		return;
	}

	// This is a non-empty combat squad, so it may have a meaningful vanguard unit.
	_vanguard = unitClosestToTarget(_units);

	// dummy unit set
	BWAPI::Unitset dummyUnits;
	UnitCluster dummyCluster;

	// execute detector micro
	_detectorManager.setUnitClosestToEnemy(_vanguard);
	_detectorManager.setCenter(_center);
	_detectorManager.executeMicro(dummyUnits, dummyCluster);

	// execute surveyer micro
	_surveyerManager.executeMicro(dummyUnits, dummyCluster);

	// For now, we don't use this
	//_transportManager.update();

	/* CLUSTER START */

	// Choosing the units for clusters
	BWAPI::Unitset unitsToCluster;
	for (BWAPI::Unit unit : _units)
	{
		if (unit->getType().isDetector() ||
			unit->getType().spaceProvided() > 0 ||
			unit->getType() == BWAPI::UnitTypes::Protoss_High_Templar ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Queen)
		{
			// Don't cluster detectors, transports, high templar, queens.
			// They are handled separately above.
		}
		else
		{
			unitsToCluster.insert(unit);
		}
	}

	// First pass to set cluster status.
	// It also takes non-combat actions.
	Cluster::Instance().cluster(_self, unitsToCluster, _clusters);
	for (UnitCluster & cluster : _clusters)
	{
		setClusterStatus(cluster);
		drawCluster(cluster);
	}

	// Second pass to reconsider cluster status in light of the status of nearby clusters.
	/*
	bool statusReset;
	do
	{
		statusReset = false;
		for (UnitCluster & cluster : _clusters)
		{
			statusReset = statusReset || resetClusterStatus(cluster);
		}
	} while (statusReset);
	*/
	// Update _lastAttack and _lastRetreat based on the cluster status.
	setLastAttackRetreat();

	// Third pass to carry out combat or retreat actions for clusters that have them.
	// It can get slow in late game when there are many clusters, so cut down the update frequency.
	const int nPhases = std::max(1, std::min(4, int(_clusters.size() / 4)));
	int phase = BWAPI::Broodwar->getFrameCount() % nPhases;
	for (const UnitCluster & cluster : _clusters)
	{
		if (phase == 0)
		{
			clusterCombat(cluster);
		}
		phase = (phase + 1) % nPhases;
	}
	/* CLUSTER END */
}

bool Squad::isEmpty() const
{
    return _units.empty();
}

size_t Squad::getPriority() const
{
    return _priority;
}

void Squad::setPriority(const size_t & priority)
{
    _priority = priority;
}

void Squad::updateUnits()
{
	// Profile debug
	//PROFILE_FUNCTION();

	setAllUnits();
	setAllUnitsEnemy();
	setNearEnemyUnits();
	addUnitsToMicroManagers();

	//_vanguard = unitClosestToEnemy();
	_center = calcCenter();
}

// Clean up the _units vector.
// Also notice and remember a few facts about the members of the squad.
// Note: Some units may be loaded in a bunker or transport and cannot accept orders.
//       Check unit->isLoaded() before issuing orders.
void Squad::setAllUnits()
{
	_hasAir = false;
	_hasGround = false;
	_canAttackAir = false;
	_canAttackGround = false;

	BWAPI::Unitset goodUnits;
	for (const auto unit : _units)
	{
		if (UnitUtil::IsValidUnit(unit))
		{
			goodUnits.insert(unit);

			if (unit->isFlying())
			{
				if (!unit->getType().isDetector())    // mobile detectors don't count
				{
					_hasAir = true;
				}
			}
			else
			{
				_hasGround = true;
			}
			if (UnitUtil::CanAttackAir(unit))
			{
				_canAttackAir = true;
			}
			if (UnitUtil::CanAttackGround(unit))
			{
				_canAttackGround = true;
			}
		}
	}
	_units = goodUnits;
}

// Notice and remember a few facts about the enemies nearby the squad.
// Note: Some units may be loaded in a bunker or transport and cannot accept orders.
//       Check unit->isLoaded() before issuing orders.
void Squad::setAllUnitsEnemy()
{
	_enemyHasAir = false;
	_enemyHasGround = false;
	_enemyCanAttackAir = false;
	_enemyCanAttackGround = false;

	std::vector<UnitInfo> enemyCombatUnits;
	InformationManager::Instance().getNearbyForce(enemyCombatUnits, _center, BWAPI::Broodwar->enemy(), _combatSimRadius);

	for (const auto ui : enemyCombatUnits)
	{
		if (!ui.unit->exists() || ui.unit->isLoaded())
		{
			continue;
		}

		if (ui.unit->isFlying())
		{
			if (!ui.unit->getType().isDetector())    // mobile detectors don't count
			{
				_enemyHasAir = true;
			}
		}
		else
		{
			_enemyHasGround = true;
		}
		if (UnitUtil::CanAttackAir(ui.unit))
		{
			_enemyCanAttackAir = true;
		}
		if (UnitUtil::CanAttackGround(ui.unit))
		{
			_enemyCanAttackGround = true;
		}
	}
}

void Squad::setNearEnemyUnits()
{
	_nearEnemy.clear();

	for (const auto unit : _units)
	{
		if (!unit->getPosition().isValid())   // excludes loaded units
		{
			continue;
		}

		_nearEnemy[unit] = unitNearEnemy(unit);

		if (Config::Debug::DrawSquadInfo) {
			int left = unit->getType().dimensionLeft();
			int right = unit->getType().dimensionRight();
			int top = unit->getType().dimensionUp();
			int bottom = unit->getType().dimensionDown();

			int x = unit->getPosition().x;
			int y = unit->getPosition().y;

			BWAPI::Broodwar->drawBoxMap(x - left, y - top, x + right, y + bottom,
				(_nearEnemy[unit]) ? Config::Debug::ColorUnitNearEnemy : Config::Debug::ColorUnitNotNearEnemy);
		}
	}
}

void Squad::addUnitsToMicroManagers()
{
	BWAPI::Unitset meleeUnits;
	BWAPI::Unitset rangedUnits;
	BWAPI::Unitset detectorUnits;
	BWAPI::Unitset transportUnits;
	BWAPI::Unitset lurkerUnits;
    BWAPI::Unitset tankUnits;
    BWAPI::Unitset medicUnits;
	BWAPI::Unitset surveyerUnits;
	BWAPI::Unitset casterUnits;

	// add _units to micro managers
	for (auto & unit : _units)
	{
		if (unit->isCompleted() && unit->exists() && unit->getHitPoints() > 0 && !unit->isLoaded())
		{
			if (_name == "Survey" && unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
			{
				surveyerUnits.insert(unit);
			}
            else if (unit->getType() == BWAPI::UnitTypes::Terran_Medic)
            {
                medicUnits.insert(unit);
            }
			else if (unit->getType().isSpellcaster() ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Infested_Terran) {
				casterUnits.insert(unit);
			}
			else if (unit->getType() == BWAPI::UnitTypes::Zerg_Lurker)
			{
				lurkerUnits.insert(unit);
			}
			else if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
				unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
            {
                tankUnits.insert(unit);
            }   
			// select detector _units
			else if (unit->getType().isDetector() && !unit->getType().isBuilding())
			{
				detectorUnits.insert(unit);
			}
			// select transport _units
			else if (unit->getType() == BWAPI::UnitTypes::Protoss_Shuttle || 
				unit->getType() == BWAPI::UnitTypes::Terran_Dropship)
			{
				transportUnits.insert(unit);
			}
			// select ranged _units
			else if ((unit->getType().groundWeapon().maxRange() > 32) || 
				unit->getType() == BWAPI::UnitTypes::Zerg_Scourge ||
				unit->getType() == BWAPI::UnitTypes::Protoss_Reaver ||
				unit->getType() == BWAPI::UnitTypes::Protoss_Carrier)
			{
				rangedUnits.insert(unit);
			}
			// select melee _units
			else if (unit->getType().isWorker() && _combatSquad)
			{
				// If this is a combat squad, then workers are melee units like any other,
				// but we have to tell WorkerManager about them.
				// If it's not a combat squad, WorkerManager owns them; don't add them to a micromanager.
				WorkerManager::Instance().setCombatWorker(unit);
				meleeUnits.insert(unit);
			}
			// select melee _units
			else if (unit->getType().groundWeapon().maxRange() <= 32)
			{
				meleeUnits.insert(unit);
			}
		}
	}

	_meleeManager.setUnits(meleeUnits);
	_rangedManager.setUnits(rangedUnits);
	_lurkerManager.setUnits(lurkerUnits);
	_detectorManager.setUnits(detectorUnits);
	_transportManager.setUnits(transportUnits);
	_tankManager.setUnits(tankUnits);
    _medicManager.setUnits(medicUnits);
	_surveyerManager.setUnits(surveyerUnits);
	_casterManager.setUnits(casterUnits);
}

// calculates whether or not to regroup
bool Squad::needsToRegroup()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// if we have no units, never regroup
	if (_units.empty())
	{
		_regroupStatus = std::string("\x04 No attackers available");
		return false;
	}

	// Our order may not allow us to regroup.
	if (!_order.isRegroupableOrder())
	{
		_regroupStatus = std::string("\x04 Never retreat!");
		return false;
	}

	// If we're nearly maxed and have good income and cash, don't regroup.
	if (BWAPI::Broodwar->self()->supplyUsed() >= 380 &&
		BWAPI::Broodwar->self()->minerals() > 1000 && 
		BWAPI::Broodwar->self()->gas() > 1000)
	{
		_attackAtMax = true;
	}
	if (_attackAtMax)
	{
		if (BWAPI::Broodwar->self()->supplyUsed() < 320)
		{
			_attackAtMax = false;
		}
		else
		{
			_regroupStatus = std::string("\x04 Maxed. Banzai!");
			return false;
		}
	}
	
	// No vangaurd, don't regroup
	BWAPI::Unit vanguard = _vanguard;
	BWAPI::Position center = _center;
	if (!vanguard)
	{
		_regroupStatus = std::string("\x04 No vanguard");
		return false;
	}
	
	// Is there static defense nearby that we should take into account?
	// vanguard is known to be set thanks to the test immediately above.
	BWAPI::Unit nearestStaticDefense = nearbyImmobileDefense(vanguard->getPosition());
	const BWAPI::Position finalPosition = finalRegroupPosition();
	if (nearestStaticDefense)
	{
		// Don't retreat if we are in range of static defense that is attacking.
		if (nearestStaticDefense->getOrder() == BWAPI::Orders::AttackUnit && nearestStaticDefense->getDistance(vanguard) <= 3 * 32)
		{
			_regroupStatus = std::string("\x04 Go static defense!");
			return false;
		}

		// Don't retreat if we are behind static defense.
		// Assume that the static defense is between the final regroup position and the enemy.
		if (vanguard->getDistance(center) < 1 * 32 /*&&
			center.getApproxDistance(vanguard->getPosition()) - vanguard->getDistance(finalPosition) >= 32*/)
		{
			_regroupStatus = std::string("\x04 Behind static defense");
			return false;
		}
	}
	else
	{
		// There is no static defense to retreat to.
		if (vanguard->getPosition().getApproxDistance(finalPosition) < 3 * 32)
		{
			_regroupStatus = std::string("\x04 Back to the wall");
			return false;
		}
	}


	// if none of our units are in range of any enemy units, don't regroup
    std::vector<UnitInfo> enemyCombatUnits;
    const auto & enemyUnitInfo = InformationManager::Instance().getUnitInfo(BWAPI::Broodwar->enemy());

    bool anyInRange = false;
    for (const auto & eui : enemyUnitInfo)
    {
        for (const auto & u : _units)
        {
			if (!u->exists() || u->isLoaded())
			{
				continue;
			}

			// Max of weapon range and vision range. Vision range is as long or longer, except for tanks.
			// We assume that the tanks may siege, and check the siege range of unsieged tanks.
			if (UnitUtil::CanAttack(eui.second.type, u->getType()))
			{
				int range = 0;     // range of enemy unit

				if (eui.second.type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
					eui.second.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
				{
					range = (BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode).groundWeapon().maxRange() + 128;  // plus safety fudge
				}
				else
				{
					// Sight range is >= weapon range, so we can stop here.
					range = eui.second.type.sightRange();
				}

				range += 128;    // plus some to account for our squad spreading out

				if (range >= u->getPosition().getApproxDistance(eui.second.lastPosition))
				{
					anyInRange = true;
					break;   // break out of inner loop
				}
			}
        }

		if (anyInRange)
        {
            break;   // break out of outer loop
        }
    }

    if (!anyInRange)
    {
        _regroupStatus = std::string("\x04 No enemy units in range");
        return false;
    }
	
	// ---
	// All other checks are done. Finally do the expensive combat simulation.
	// ---

	// If we most recently retreated, don't attack again until retreatDuration frames have passed.
	const int retreatDuration = 5 * 24;
	bool retreat = _lastRetreatSwitchVal && (BWAPI::Broodwar->getFrameCount() - _lastRetreatSwitch < retreatDuration);

	if (!retreat)
	{
		// All checks are done. Finally do the expensive combat simulation.
		CombatSimulation sim;

		// FAP
		//sim.setCombatUnits(unitClosest->getPosition(), Config::Micro::CombatRegroupRadius);
		int score = sim.simulateCombatFAP(_units, vanguard->getPosition(), _combatSimRadius, _canAttackGround, _canAttackAir);

		retreat = score < 0;
		_lastRetreatSwitch = BWAPI::Broodwar->getFrameCount();
		_lastRetreatSwitchVal = retreat;
	}
	
	if (retreat)
	{
		_regroupStatus = std::string("\x04 Retreat");
	}
	else
	{
		_regroupStatus = std::string("\x04 Attack");
	}

	return retreat;
}

// calculates whether or not to regroup
bool Squad::needsToRegroupCluster(UnitCluster & cluster)
{
	// Profile debug
	//PROFILE_FUNCTION();

	cluster.setExtraText("");

	// if we have no units, never regroup
	if (_units.empty())
	{
		_regroupStatus = green + std::string("No attackers available");
		return false;
	}

	// Our order may not allow us to regroup.
	if (!_order.isRegroupableOrder())
	{
		_regroupStatus = green + std::string("Never retreat!");
		return false;
	}

	// If we're nearly maxed and have good income and cash, don't regroup.
	if (BWAPI::Broodwar->self()->supplyUsed() >= 380 &&
		(BWAPI::Broodwar->self()->minerals() > 1000 || WorkerManager::Instance().getNumMineralWorkers() > 12))
	{
		_attackAtMax = true;
	}
	if (_attackAtMax)
	{
		if (BWAPI::Broodwar->self()->supplyUsed() < 320)
		{
			_attackAtMax = false;
		}
		else
		{
			_regroupStatus = green + std::string("Banzai!");
			return false;
		}
	}

	// Cluster vanguard
	BWAPI::Unit vanguard = unitClosestToTarget(cluster.units);  // cluster vanguard (not squad vanguard)
	if (Config::Debug::DrawClusters) {
		BWAPI::Broodwar->drawCircleMap(vanguard->getPosition(), 5, BWAPI::Colors::White, true);
	}

	if (!vanguard)
	{
		// No combat units in the cluster.
		_regroupStatus = yellow + std::string("No vanguard");
		return true;
	}

	// Is there immobile defense nearby that we should take into account?
	// vanguard is known to be set thanks to the test immediately above.
	BWAPI::Unit staticDefense = nearbyImmobileDefense(vanguard->getPosition());
	const BWAPI::Position finalPosition = finalRegroupPosition();
	if (staticDefense)
	{
		// Don't retreat if we are in range of static defense that is attacking.
		if (staticDefense->getOrder() == BWAPI::Orders::AttackUnit && staticDefense->getDistance(vanguard) <= 5 * 32)
		{
			_regroupStatus = green + std::string("Go static defense!");
			return false;
		}

		// If there is defense to retreat to, we're fine if we're behind it wrt the enemy.
		const UnitCluster * enemyCluster = Cluster::Instance().getNearestEnemyClusterVs(cluster.center, !cluster.air, cluster.air);
		if (!enemyCluster)
		{
			_regroupStatus = green + std::string("Nothing to fear");
			return false;
		}

		// Don't retreat if we are behind static defense.
		// Assume that the static defense is between the final regroup position and the enemy.
		if (staticDefense->getDistance(cluster.center) < 5 * 32 &&
			cluster.center.getApproxDistance(enemyCluster->center) - staticDefense->getDistance(enemyCluster->center) >= 2 * 32)
		{
			_regroupStatus = green + std::string("Behind static defense");
			return false;
		}
	}
	else
	{
		// There is no static defense to retreat to.
		if (vanguard->getPosition().getApproxDistance(finalPosition) < 7 * 32)
		{
			_regroupStatus = green + std::string("Back to the wall");
			return false;
		}
	}

	// ---
	// All other checks are done. Finally do the expensive combat simulation.
	// ---

	// Get the average lastRetreatSwitchFrame and lastRetreatVal among units in cluster
	int totalRetreat = 0;
	int totalRetreatVal = 0;
	int lastRetreatSwitchFrame = 0;
	bool lastRetreatVal = false;
	for (BWAPI::Unit u : cluster.units)
	{
		UnitInfo ui = InformationManager::Instance().getUnit(BWAPI::Broodwar->self(), u);
		totalRetreat += ui.lastRetreatSwitchFrame;
		totalRetreatVal += ui.lastRetreatVal;
	}
	lastRetreatSwitchFrame = totalRetreat / cluster.units.size();
	lastRetreatVal = totalRetreatVal / cluster.units.size();

	// If we most recently retreated, don't attack again until retreatDuration frames have passed.
	const int retreatDuration = 5 * 24;
	bool retreatDurationNotReached = (BWAPI::Broodwar->getFrameCount() - lastRetreatSwitchFrame) < retreatDuration;
	bool retreat = lastRetreatVal && retreatDurationNotReached;
	int score = 0;

	Log().Debug() << "Sim lastRetreatSwitchFrame: " << lastRetreatSwitchFrame << "; lastRetreatVal: " << lastRetreatVal;
	Log().Debug() << "Sim retreatDurationNotReached: " << retreatDurationNotReached << " retreat: " << retreat;

	if (!retreat)
	{
		// Do the expensive combat simulation.
		CombatSimulation sim;
		
		// FAP
		score = sim.simulateCombatFAP(cluster.units, vanguard->getPosition(), _combatSimRadius, cluster.groundDPF, cluster.airDPF);
		retreat = score < 0;

		if(retreat)
		{
			for (BWAPI::Unit u : cluster.units)
			{
				InformationManager::Instance().setLastRetreatSwitchFrame(u, BWAPI::Broodwar->getFrameCount());
			}
		}

		Log().Debug() << "Sim new retreat: " << retreat << "; new score: " << score << "";
	}

	for (BWAPI::Unit u : cluster.units)
	{
		InformationManager::Instance().setLastRetreatVal(u, retreat);
	}

	if (Config::Debug::DrawClusters)
	{
		std::stringstream clusterText;
		clusterText << white << "sim: " << (retreat ? red : green) << score;
		clusterText << (retreat ? red : green) << (retreat ? " back" : " go");
		cluster.setExtraText(clusterText.str());
	}

	if (retreat)
	{		
		_regroupStatus = red + std::string("Retreat");
	}
	else
	{
		_regroupStatus = green + std::string("Attack");
	}

	return retreat;
}

void Squad::setSquadOrder(const SquadOrder & so)
{
	_order = so;
}

bool Squad::containsUnit(BWAPI::Unit u) const
{
    return _units.contains(u);
}

bool Squad::containsUnitType(BWAPI::UnitType t) const
{
	for (const auto u : _units)
	{
		if (u->getType() == t)
		{
			return true;
		}
	}
	return false;
}

void Squad::clear()
{
	// If game has ended WorkerManager could be deconstrcuted and result in crash
	// Also SC will quit, so no need
	if (gameEnded) return;

    for (const auto unit : getUnits())
    {
        if (unit->getType().isWorker())
        {
            WorkerManager::Instance().finishedWithWorker(unit);
        }
    }

    _units.clear();
}

bool Squad::unitNearEnemy(BWAPI::Unit unit)
{
	UAB_ASSERT(unit, "missing unit");

	int safeDistance = (!unit->isFlying() && InformationManager::Instance().enemyHasSiegeMode()) ? 15 * 32 : 11 * 32;

	// For each enemy unit, visible or not.
	for (const auto & kv : InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (UnitUtil::CanAttack(ui.type, unit->getType()) && ui.lastPosition.isValid() && !ui.goneFromLastPosition)
		{
			if (unit->getDistance(ui.lastPosition) <= safeDistance)
			{
				return true;
			}
		}
	}

	return false;
}

BWAPI::Position Squad::calcCenter() const
{
	// Profile debug
	//PROFILE_FUNCTION();

    if (_units.empty())
    {
		return finalRegroupPosition();
    }

	BWAPI::Position accum(0,0);
	for (auto & unit : _units)
	{
		if (unit->getPosition().isValid())
		{
			accum += unit->getPosition();
		}
	}
	return BWAPI::Position(accum.x / _units.size(), accum.y / _units.size());
}

BWAPI::Position Squad::calcRegroupPosition()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// 1. Retreat toward static defense, if any is near.
	BWAPI::Unit vanguard = _vanguard;  // squad vanguard
	if (vanguard)
	{
		BWAPI::Unit nearest = nearbyImmobileDefense(vanguard->getPosition());
		if (nearest)
		{
			BWAPI::Position behind = DistanceAndDirection(nearest->getPosition(), vanguard->getPosition(), -128);
			return behind;
		}
	}
		
	// 2. Retreat to the location of the unit not near the enemy which is
	// closest to the order position.
	// NOTE May retreat somewhere silly if the chosen unit was newly produced.
	//      Sometimes retreats back and forth through the enemy when new
	//      units are produced in bases on opposite sides.
	BWAPI::Position base_position = finalRegroupPosition();
	BWAPI::Position regroup(0, 0);

	if (vanguard)
	{
		BWAPI::Position order_position(_order.getPosition());
		BWAPI::TilePosition order_location(_order.getPosition());
		BWAPI::Position enemy_position(vanguard->getPosition());
		int minDistEnemy = 100000;

		for (const auto unit : _units)
		{
			// Don't return the position of a detector, which may be in a weird place.
			// That means science vessel, protoss observer, or overlord.
			// Bug fix thanks to AIL!
			if (unit->exists() && 
				!_nearEnemy[unit] &&
				!unit->getType().isDetector() &&
				unit->getType() != BWAPI::UnitTypes::Terran_Medic &&
				unit->getPosition().isValid())    // excludes loaded units
			{
				//int distOrder = unit->getPosition().getApproxDistance(order_position);
				int distBase = unit->getPosition().getApproxDistance(base_position);
				int distEnemy = unit->getPosition().getApproxDistance(enemy_position);
				int distBaseEnemy = base_position.getApproxDistance(enemy_position);

				if (distEnemy < minDistEnemy && distBase < distBaseEnemy)
				{
					// If the squad has any ground units, don't try to retreat to the position of an air unit
					// which is flying in a place that a ground unit cannot reach.
					if (!_hasGround || MapTools::Instance().getGroundTileDistance(unit->getTilePosition(), order_location) >= 0)
					{
						minDistEnemy = distEnemy;
						regroup = unit->getPosition();
					}
				}
			}
		}
	}
	if (regroup != BWAPI::Position(0, 0))
	{
		return regroup;
	}

	// 3. Failing that, retreat to a base we own.
	return base_position;
}

BWAPI::Position Squad::calcRegroupPositionCluster(const UnitCluster & cluster)
{
	// Profile debug
	//PROFILE_FUNCTION();

	const int riskRadius = 31 + (!cluster.air && InformationManager::Instance().enemyHasSiegeMode() ? 12 * 32 : 8 * 32);
	BWAPI::Unit closestEnemy = BWAPI::Broodwar->getClosestUnit(
		cluster.center,
		BWAPI::Filter::IsEnemy &&
		(cluster.air
			? BWAPI::Filter::AirWeapon != BWAPI::WeaponTypes::None || BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Bunker || BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Carrier
			: BWAPI::Filter::GroundWeapon != BWAPI::WeaponTypes::None || BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Bunker || BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Carrier || BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Reaver),
		riskRadius);

	// The closestEnemy may be null, though we're fleeing so it should be rare.
	bool fleeingAir = closestEnemy ? closestEnemy->isFlying() : false;

	// Cluster vanguard (not squad vanguard).
	BWAPI::Unit vanguard = unitClosestToTarget(cluster.units);

	// 1. Retreat toward immobile defense, if any is near.
	// Cluster vanguard (not squad vanguard).
	if (vanguard)
	{
		BWAPI::Unit nearest = nearbyImmobileDefense(vanguard->getPosition());
		if (nearest)
		{
			BWAPI::Position behind = DistanceAndDirection(nearest->getPosition(), vanguard->getPosition(), -128);
			return behind;
		}
	}
	
	// 2. Regroup toward another cluster.
	// Look for a cluster nearby, preferably closer to the enemy, preferably attacking.
	if (!closestEnemy)
	{
		const int distToOrder = _order.getPosition().getApproxDistance(cluster.center);       // pixels by air or ground
		const UnitCluster * bestCluster = nullptr;
		int bestScore = INT_MIN;
		for (const UnitCluster & neighbor : _clusters)
		{
			int distToNeighbor = cluster.center.getApproxDistance(neighbor.center);
			// An air cluster may join a ground cluster, but not vice versa.
			if (distToNeighbor > 0 && cluster.air >= neighbor.air)
			{
				int score = distToOrder - _order.getPosition().getApproxDistance(neighbor.center);
				if (neighbor.status == ClusterStatus::Attack)
				{
					score += 3 * 32;
				}
				else if (neighbor.status == ClusterStatus::Regroup)
				{
					if (neighbor.speed < 0.1)
					{
						// Wants to retreat but can't. Skip it.
						continue;
					}
					score -= 32;
				}
				if (score > bestScore)
				{
					bestCluster = &neighbor;
					bestScore = score;
				}
			}
		}
		if (bestCluster)
		{
			return bestCluster->center;
		}
	}

	// 3. Mutalisks fleeing ground units can probably find a way.
	//if (cluster.air && closestEnemy && !fleeingAir && cluster.includesType(BWAPI::UnitTypes::Zerg_Mutalisk))
	//{
	//	//BWAPI::Broodwar->printf("muta retreat to safe position");
	//	BWAPI::Position behind = DistanceAndDirection(cluster.center, vanguard->getPosition(), -128);
	//	return behind;
	//}

	// 4. Retreat to the location of the unit not near the enemy which is
	// closest to the order position.
	// We are using Squad units since clusters can be very small.
	// NOTE May retreat somewhere silly if the chosen unit was newly produced.
	//      Sometimes retreats back and forth through the enemy when new
	//      units are produced in bases on opposite sides.
	BWAPI::Position base_position = finalRegroupPosition();
	BWAPI::Position regroup(0, 0);

	if (vanguard)
	{
		BWAPI::Position order_position(_order.getPosition());
		BWAPI::TilePosition order_location(_order.getPosition());
		BWAPI::Position enemy_position(vanguard->getPosition());
		int minDistEnemy = 100000;

		for (const auto unit : _units)
		{
			// Don't return the position of a detector, which may be in a weird place.
			// That means science vessel, protoss observer, or overlord.
			// Bug fix thanks to AIL!
			if (unit->exists() &&
				!_nearEnemy[unit] &&
				!unit->getType().isDetector() &&
				unit->getType() != BWAPI::UnitTypes::Terran_Medic &&
				unit->getPosition().isValid())    // excludes loaded units
			{
				//int distOrder = unit->getPosition().getApproxDistance(order_position);
				int distBase = unit->getPosition().getApproxDistance(base_position);
				int distEnemy = unit->getPosition().getApproxDistance(enemy_position);
				int distBaseEnemy = base_position.getApproxDistance(enemy_position);

				if (distEnemy < minDistEnemy && distBase < distBaseEnemy)
				{
					// If the squad has any ground units, don't try to retreat to the position of an air unit
					// which is flying in a place that a ground unit cannot reach.
					if (!_hasGround || MapTools::Instance().getGroundTileDistance(unit->getTilePosition(), order_location) >= 0)
					{
						minDistEnemy = distEnemy;
						regroup = unit->getPosition();
					}
				}
			}
		}
	}
	if (regroup != BWAPI::Position(0, 0))
	{
		return regroup;
	}

	// 5. Failing that, retreat to a base we own.
	return base_position;
}

// Return the rearmost position we should retreat to, which puts our "back to the wall".
BWAPI::Position Squad::finalRegroupPosition() const
{
	// Retreat to the main base, unless we change our mind below.
	const BWEM::Base * base = InformationManager::Instance().getMyMainBaseLocation();

	// If the natural has been taken, retreat there instead.
	const BWEM::Base * natural = InformationManager::Instance().getMyNaturalLocation();
	if (natural && BWAPI::Broodwar->self() == InformationManager::Instance().getBaseOwner(natural))
	{
		base = natural;
	}

	return base->Center();
}

BWAPI::Unit Squad::nearbyImmobileDefense(const BWAPI::Position & pos) const
{
	BWAPI::Unit nearest = nullptr;

	// NOTE What matters is whether the enemy has ground or air units.
	if (_enemyHasGround)
	{
		nearest = InformationManager::Instance().nearestGroundImmobileDefense(pos);
	}
	else
	{
		nearest = InformationManager::Instance().nearestAirStaticDefense(pos);
	}
	if (nearest && nearest->getPosition().getApproxDistance(pos) < 800)
	{
		return nearest;
	}
	return nullptr;
}

// Return the unit closest to the order position (not actually closest to the enemy).
BWAPI::Unit Squad::unitClosestToEnemy() const
{
	// Profile debug
	//PROFILE_FUNCTION();

	BWAPI::Position order_position(_order.getPosition());
	BWAPI::TilePosition order_location(_order.getPosition());
	const BWEM::Base * enemyBase = InformationManager::Instance().getEnemyMainBaseLocation();
	BWAPI::Unit closest = nullptr;
	int closestDist = 100000;

	UAB_ASSERT(_order.getPosition().isValid(), "bad order position");

	for (const auto unit : _units)
	{
		// Non-combat units should be ignored for this calculation.
		if (!unit->getPosition().isValid() ||   // includes units loaded into bunkers or transports
			unit->getType().isDetector() ||
			unit->getType().isSpellcaster() ||  // ignore queens and defilers
			unit->getType() == BWAPI::UnitTypes::Terran_Medic || 
			unit->getType() == BWAPI::UnitTypes::Zerg_Infested_Terran ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Broodling)
		{
			continue;
		}

		int dist;
		if (_order.getType() == SquadOrderTypes::Regroup)
		{
			dist = unit->getPosition().getApproxDistance(_lastOrderPosition);
		}
		else
		{
			dist = unit->getPosition().getApproxDistance(order_position);
		}
		/*if (_hasGround)
		{
			// A ground or air-ground squad. Use ground distance.
			// It is -1 if no ground path exists.
			dist = MapTools::Instance().getGroundTileDistance(unit->getTilePosition(), order_location);
		}
		else
		{
			// An all-air squad. Use air distance (which is what unit->getPosition().getApproxDistance() gives).
			dist = unit->getPosition().getApproxDistance(order_position);
		}*/

		if (dist < closestDist && dist > 0)
		{
			closest = unit;
			closestDist = dist;
		}
	}

	if (closest && Config::Debug::DrawSquadInfo) {
		BWAPI::Broodwar->drawCircleMap(closest->getPosition(), 5, BWAPI::Colors::White, true);
	}

	return closest;
}

int Squad::squadUnitsNear(BWAPI::Position p)
{
	int numUnits = 0;

	for (auto & unit : _units)
	{
		if (unit->getPosition().getApproxDistance(p) < 600)
		{
			numUnits++;
		}
	}

	return numUnits;
}

const BWAPI::Unitset & Squad::getUnits() const	
{ 
	return _units; 
} 

const SquadOrder & Squad::getSquadOrder()	const			
{ 
	return _order; 
}

void Squad::addUnit(BWAPI::Unit u)
{
	_units.insert(u);
}

void Squad::removeUnit(BWAPI::Unit u)
{
	if (_combatSquad && u->getType().isWorker())
	{
		WorkerManager::Instance().finishedWithWorker(u);
	}
	_units.erase(u);
}

// Remove all workers from the squad, releasing them back to WorkerManager.
void Squad::releaseWorkers()
{
	for (auto it = _units.begin(); it != _units.end();)
	{
		if (_combatSquad && (*it)->getType().isWorker())
		{
			WorkerManager::Instance().finishedWithWorker(*it);
			it = _units.erase(it);
		}
		else
		{
			++it;
		}
	}
}

const std::string & Squad::getName() const
{
    return _name;
}

const bool Squad::hasCombatUnits() const
{
	// If the only units we have are detectors, then we have no combat units.
	return !(_units.empty() || _units.size() == _detectorManager.getUnits().size());
}

// Set cluster status and take non-combat cluster actions.
void Squad::setClusterStatus(UnitCluster & cluster)
{
	// Cases where the cluster can't get into a fight.
	if (noCombatUnits(cluster))
	{
		// The cluster has no combat units.
		cluster.status = ClusterStatus::FallBack;
		_regroupStatus = red + std::string("Fall back");
	}
	else if (notNearEnemy(cluster))
	{
		// Move toward the order position.
		cluster.status = ClusterStatus::Advance;
		_regroupStatus = yellow + std::string("Advance");
	}
	else
	{
		// Cases where the cluster might get into a fight.
		if (needsToRegroupCluster(cluster))
		{
			cluster.status = ClusterStatus::Regroup;
		}
		else
		{
			cluster.status = ClusterStatus::Attack;
		}
	}
}

// Update _lastAttack and _lastRetreat based on the cluster status.
void Squad::setLastAttackRetreat()
{
	for (UnitCluster & cluster : _clusters)
	{
		if (cluster.status == ClusterStatus::Attack)
		{
			_lastAttack = NOW;
			cluster.lastAttack = NOW;
		}
		else if (cluster.status == ClusterStatus::Regroup)
		{
			_lastRetreat = NOW;
			cluster.lastRetreat = NOW;
		}
	}
}

// Reconsider cluster status in light of the status of nearby clusters.
// Some clusters may be set to retreat, but they're near a fight they should join.
// Tell them to attack after all.
// Return true if a change was made.
bool Squad::resetClusterStatus(UnitCluster & cluster)
{
	if (cluster.status == ClusterStatus::Regroup)
	{
		for (const UnitCluster & cluster2 : _clusters)
		{
			if (cluster2.status == ClusterStatus::Attack &&
				cluster.center.getApproxDistance(cluster2.center) < 3 * 32)
			{
				cluster.status = ClusterStatus::Attack;
				return true;
			}
		}
	}

	return false;
}

// Special-case units which are clustered, but arrange their own movement
// instead of accepting the cluster's movement commands.
// Currently, this is medics and defilers.
// Queens are not clustered.
void Squad::microSpecialUnits(const UnitCluster & cluster)
{
	// Medics and defilers try to get near the front line.
	static int spellPhase = 0;
	spellPhase = (spellPhase + 1) % 6;
	if (spellPhase == 0)
	{
		// The vanguard is chosen among combat units only, so a non-combat unit sent toward
		// the vanguard may either advance or retreat--either way, that's probably what we want.
		BWAPI::Unit vanguard = unitClosestToTarget(cluster.units);	// cluster vanguard
		if (!vanguard)
		{
			vanguard = _vanguard;									// squad vanguard
		}

		//_microDefilers.updateMovement(cluster, vanguard);
		//_medicManager.update(cluster, vanguard);
	}
	else if (spellPhase == 2)
	{
		//_microDefilers.updateSwarm(cluster);
	}
	else if (spellPhase == 4)
	{
		//_microDefilers.updatePlague(cluster);
	}
}

// Take cluster combat actions.
void Squad::clusterCombat(const UnitCluster & cluster)
{
	// set vangaurd
	BWAPI::Unit vanguard = unitClosestToTarget(cluster.units);  // cluster vanguard (not squad vanguard)

	// set final position
	BWAPI::Position finalPosition = finalRegroupPosition();

	// set agression
	bool _goAggressive = ProductionManager::Instance().getAggression();

	if (cluster.status == ClusterStatus::Regroup)
	{
		// Regroup, aka retreat. Only fighting units care about regrouping.
		BWAPI::Position regroupPosition = calcRegroupPositionCluster(cluster);

		if (Config::Debug::DrawClusters)
		{
			BWAPI::Broodwar->drawLineMap(cluster.center, regroupPosition, BWAPI::Colors::Purple);
			BWAPI::Broodwar->drawCircleMap(regroupPosition, 3, BWAPI::Colors::Purple, true);
		}

		// Note: Medics, casters, detectors and transports do not regroup.
		_meleeManager.regroup(regroupPosition, cluster);
		_rangedManager.regroup(regroupPosition, cluster);
		_tankManager.regroup(regroupPosition, cluster);

		// if no lurkers are dead, execute the order
		if (BWAPI::Broodwar->self()->deadUnitCount(BWAPI::UnitTypes::Zerg_Lurker) == 0)
		{
			_lurkerManager.execute(_order, cluster);
		}
		else
		{
			_lurkerManager.regroup(regroupPosition, cluster);
		}
	}
	else
	{
		_meleeManager.execute(_order, cluster);
		_rangedManager.execute(_order, cluster);
		_lurkerManager.execute(_order, cluster);
		_tankManager.execute(_order, cluster);
	}

	if (cluster.status == ClusterStatus::FallBack)
	{
		// Fall back, non-combat units go back to base if they are the only ones in the cluster
		_casterManager.regroup(finalPosition, cluster);
		_medicManager.regroup(finalPosition, cluster);
	}
	else
	{
		// execute spell caster micro
		_casterManager.setUnitClosestToEnemy(vanguard);
		_casterManager.execute(_order, cluster);

		// execute medic micro
		_medicManager.setUnitClosestToEnemy(_vanguard);
		_medicManager.setCenter(_center);
		_medicManager.execute(_order, cluster);
	}
}

// The cluster has no units which can fight.
// It should try to join another cluster, or else retreat to base.
bool Squad::noCombatUnits(const UnitCluster & cluster) const
{
	for (BWAPI::Unit unit : cluster.units)
	{
		if (UnitUtil::TypeCanAttack(unit->getType()))
		{
			return false;
		}
	}
	return true;
}

// The cluster has no enemies nearby.
// It will try to join another cluster, or to advance toward the goal.
bool Squad::notNearEnemy(const UnitCluster & cluster)
{
	for (BWAPI::Unit unit : cluster.units)
	{
		if (_nearEnemy[unit])
		{
			return false;
		}
	}
	return true;
}

// The combat unit closest to the given position.
BWAPI::Unit Squad::unitClosestToPosition(const BWAPI::Position & pos, const BWAPI::Unitset & units) const
{
	if (!pos.isValid())
	{
		UAB_ASSERT(pos.isValid(), "bad position");
		return nullptr;
	}

	BWAPI::Unit closest = nullptr;
	int closestDist = MAX_DISTANCE;

	for (BWAPI::Unit unit : units)
	{
		// Non-combat units should be ignored for this calculation.
		// If the cluster contains only these units, we'll return null.
		if (unit->getType().isDetector() ||
			!unit->getPosition().isValid() ||       // includes units loaded into bunkers or transports
			unit->getType() == BWAPI::UnitTypes::Terran_Medic ||
			unit->getType() == BWAPI::UnitTypes::Protoss_High_Templar ||
			unit->getType() == BWAPI::UnitTypes::Protoss_Dark_Archon ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Defiler ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Queen)
		{
			continue;
		}

		int dist;
		if (_hasGround)
		{
			// A ground or air-ground group. Use ground distance.
			// It is -1 if no ground path exists.
			//dist = the.map.getGroundDistance(unit->getPosition(), pos);
			dist = unit->getPosition().getApproxDistance(pos);
		}
		else
		{
			// An all-air group. Use air distance (which is what unit->getDistance() gives).
			dist = unit->getPosition().getApproxDistance(pos);
		}

		if (dist < closestDist && dist != -1)
		{
			closest = unit;
			closestDist = dist;
		}
	}

	return closest;
}

// Return the combat unit closest to the order position. Spellcasters are excluded.
// The order position might be an enemy position, or a defense position in our base.
BWAPI::Unit Squad::unitClosestToTarget(const BWAPI::Unitset & units) const
{
	return unitClosestToPosition(_order.getPosition(), units);
}

void Squad::drawCluster(const UnitCluster & cluster) const
{
	if (Config::Debug::DrawClusters)
	{
		cluster.draw(BWAPI::Colors::Grey, white + _name + ' ' + _regroupStatus);
	}
}
#include "MicroManager.h"
#include "UnitUtil.h"
#include "ProductionManager.h"

using namespace UAlbertaBot;

MicroManager::MicroManager() 
{
}

void MicroManager::setUnits(const BWAPI::Unitset & u) 
{ 
	_units = u;
}

BWAPI::Position MicroManager::calcCenter() const
{
    if (_units.empty())
    {
        //if (Config::Debug::DrawSquadInfo)
        //{
        //    BWAPI::Broodwar->printf("calcCenter() called on empty squad");
        //}
        return BWAPI::Position(0,0);
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

void MicroManager::execute(const SquadOrder & inputOrder, const UnitCluster & cluster)
{
	// Profile debug
	//PROFILE_FUNCTION();

	// Nothing to do if we have no units
	if (_units.empty())
	{
		return;
	}

	order = inputOrder;
	drawOrderText();

	// No order
	if (!(order.getType() == SquadOrderTypes::Attack || 
		order.getType() == SquadOrderTypes::Defend || 
		order.getType() == SquadOrderTypes::Regroup ||
		order.getType() == SquadOrderTypes::Survey ||
		order.getType() == SquadOrderTypes::DestroyNeutral))
	{
		return;
	}

	// The targets that micro managers have available to shoot at
	BWAPI::Unitset nearbyEnemies;

	// Always include enemies in the radius of the order.
	MapGrid::Instance().GetUnits(nearbyEnemies, order.getPosition(), order.getRadius(), false, true);

	// If the order is to destroy neutral units at a given location.
	if (order.getType() == SquadOrderTypes::DestroyNeutral)
	{
		for (BWAPI::Unit unit : BWAPI::Broodwar->getStaticNeutralUnits())
		{
			if (unit &&
				unit->getPlayer() == BWAPI::Broodwar->neutral() &&
				!unit->getInitialType().isInvincible() &&
				!unit->getType().canMove() &&
				!unit->isFlying() &&
				order.getPosition().getApproxDistance(unit->getInitialPosition()) < 5 * 32)
			{
				nearbyEnemies.insert(unit);
			}
		}

		// Add enemy units
		for (auto & unit : cluster.units)
		{
			if (!unit->getType().isFlyer())
			{
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), unit->getType().sightRange(), false, true);
			}
		}

		// Allow micromanager to handle neutrals
		destroyNeutralTargets(nearbyEnemies);
		return;
	}

	// if the order is to survey
	else if (order.getType() == SquadOrderTypes::Survey)
	{
		// Add enemy units
		for (auto & unit : cluster.units)
		{
			MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), unit->getType().sightRange(), false, true);
		}
	}

	else
	{
		// For other orders: Units in sight of our cluster.
		// Don't be distracted by distant units; move toward the goal.
		bool _goAggressive = ProductionManager::Instance().getAggression();
		for (BWAPI::Unit unit : cluster.units)
		{
			if(cluster.status == ClusterStatus::Regroup)
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), 0, false, true);
			else if (!_goAggressive && unit->getDistance(order.getPosition()) > 400)
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), 2 * 32, false, true);
			else if (!_goAggressive)
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), unit->getType().sightRange(), false, true);
			else
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), 2 * unit->getType().sightRange(), false, true);
		}

		// Filter out targets that we definitely can't attack.
		for (auto it = nearbyEnemies.begin(); it != nearbyEnemies.end(); )
		{
			if ((*it)->isInvincible() ||
				(*it)->getType().isSpell() ||
				!(*it)->isVisible() ||
				!(*it)->isDetected())
			{
				it = nearbyEnemies.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	executeMicro(nearbyEnemies, cluster);
	return;
	/*
	// if the order is to attack
	else if (order.getType() == SquadOrderTypes::Attack)
	{
		// Add enemy units
		bool _goAggressive = ProductionManager::Instance().getAggression();
		for (auto & unit : cluster.units)
		{
			if (!_goAggressive && unit->getDistance(order.getPosition()) > 800)
			{
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), 2 * 32, false, true);
			}
			else if (!_goAggressive)
			{
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), unit->getType().sightRange(), false, true);
			}
			else
			{
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), 2 * unit->getType().sightRange(), false, true);
			}
		}

		// if this is an attack squad
		BWAPI::Unitset workersRemoved;

		for (auto & enemyUnit : nearbyEnemies)
		{
			// if it's not a worker, or if it is but we don't like it, add it to the targets
			if (!enemyUnit->getType().isWorker() ||
				enemyUnit->isRepairing() || enemyUnit->isConstructing() || unitNearChokepoint(enemyUnit) ||
				enemyUnit->isBraking() || !enemyUnit->isMoving() || enemyUnit->isGatheringMinerals() || enemyUnit->isGatheringGas())
			{
				workersRemoved.insert(enemyUnit);
			}
			// if it is a worker
			else
			{
				for (auto& enemyRegion : InformationManager::Instance().getOccupiedRegions(BWAPI::Broodwar->enemy()))
				{
					// only add it if it's in their region
					if (BWEMmap.GetArea(BWAPI::TilePosition(enemyUnit->getPosition())) == enemyRegion)
					{
						workersRemoved.insert(enemyUnit);
						break;
					}
				}
			}
		}
		
		// Allow micromanager to handle enemies
		executeMicro(workersRemoved);
		return;
	}

	// if the order is to regroup
	else if (order.getType() == SquadOrderTypes::Regroup)
	{
		// Add enemy units
		for (auto & unit : cluster.units)
		{
			if (unit->getDistance(order.getPosition()) > 800)
			{
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), 2 * 32, false, true);
			}
			else
			{
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), unit->getType().sightRange(), false, true);
			}
		}
	}

	// if the order is to defend
	else if (order.getType() == SquadOrderTypes::Defend)
	{
		// Add enemy units
		for (auto & unit : cluster.units)
		{
			if (unit->getDistance(order.getPosition()) > 800)
			{
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), 2 * 32, false, true);
			}
			else
			{
				MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), unit->getType().sightRange(), false, true);
			}
		}
	}
	*/
	/*
	// do survey micro
	if (order.getType() == SquadOrderTypes::Survey)
	{
		executeMicro(nearbyEnemies);
	}

	// the following block of code attacks all units on the way to the order position
	// we want to do this if the order is attack, defend, or harass
	if (order.getType() == SquadOrderTypes::Attack || 
		order.getType() == SquadOrderTypes::Defend ||
		order.getType() == SquadOrderTypes::Regroup)
	{
		// if this is a worker defense force
		if (_units.size() == 1 && (*_units.begin())->getType().isWorker())
		{
			executeMicro(nearbyEnemies);
		}
		// otherwise it is a normal attack force
		else
		{
			// if this is a defense squad then we care about all units in the area
			if (order.getType() == SquadOrderTypes::Defend)
			{
				executeMicro(nearbyEnemies);
			}
			// otherwise we only care about workers if they are in their own region
			// or are building something
			// Idea: Don't goose chase the enemy scout.
			// Unfortunately there are bad side effects.
			else
			{
				// if this is an attack squad
				BWAPI::Unitset workersRemoved;

				for (auto & enemyUnit : nearbyEnemies)
				{
					// if it's not a worker, or if it is but we don't like it, add it to the targets
					if (!enemyUnit->getType().isWorker() ||
						enemyUnit->isRepairing() || enemyUnit->isConstructing() || unitNearChokepoint(enemyUnit) ||
						enemyUnit->isBraking() || !enemyUnit->isMoving() || enemyUnit->isGatheringMinerals() || enemyUnit->isGatheringGas())
					{
						workersRemoved.insert(enemyUnit);
					}
					// if it is a worker
					else
					{
						for (BWTA::Region * enemyRegion : InformationManager::Instance().getOccupiedRegions(BWAPI::Broodwar->enemy()))
						{
							// only add it if it's in their region
							if (BWTA::getRegion(BWAPI::TilePosition(enemyUnit->getPosition())) == enemyRegion)
							{
								workersRemoved.insert(enemyUnit);
								break;
							}
						}
					}
				}

				// Allow micromanager to handle enemies
				executeMicro(workersRemoved);
			}
		}
	}
	*/
	
}

// The order is DestroyNeutral. Carry it out.
void MicroManager::destroyNeutralTargets(const BWAPI::Unitset & targets)
{
	// Profile debug
	//PROFILE_FUNCTION();

	// Is any target in sight? We only need one.
	BWAPI::Unitset visibleTargets;
	for (const auto target : targets)
	{
		if (target->exists() &&
			target->isTargetable() &&
			target->isDetected())			// not e.g. a neutral egg under a neutral arbiter
		{
			visibleTargets.insert(target);
		}
	}

	int count = 0;
	for (const auto unit : _units)
	{
		count++;

		// pick closest visible target
		auto visibleTarget = closestUnit(unit, visibleTargets);

		if (visibleTarget)
		{
			// We see a target, so we can issue attack orders to units that can attack.
			if (UnitUtil::CanAttackGround(unit) && unit->canAttack())
			{
				// There are targets. Attack it.
				Micro::SmartAttackUnit(unit, visibleTarget);
			}
		}
		else if (unit->getDistance(order.getPosition()) > 96)
		{
			// There are no targets. Move to the order position if not already close.
			Micro::SmartMove(unit, order.getPosition());
		}
	}
}

BWAPI::Unit MicroManager::closestUnit(BWAPI::Unit unit, const BWAPI::Unitset & targets)
{
	int minDistance = 0;
	BWAPI::Unit closest = nullptr;

	for (auto & target : targets)
	{
		//int distance = -1;
		//BWEMmap.GetPath(unit->getPosition(), target->getPosition(), &distance);
		int distance = target->getDistance(unit);
		if (distance >= 0 && (!closest || distance < minDistance))
		{
			minDistance = distance;
			closest = target;
		}
	}

	return closest;
}

BWAPI::Unit MicroManager::closestThreat(BWAPI::Unit unit, const BWAPI::Unitset & targets)
{
	int minDistance = 0;
	BWAPI::Unit closest = nullptr;

	for (auto & target : targets)
	{
		if (!UnitUtil::IsThreat(unit, target)){
			continue;
		}

		//int distance = -1;
		//BWEMmap.GetPath(unit->getPosition(), target->getPosition(), &distance);
		int distance = target->getDistance(unit);
		if (distance >= 0 && (!closest || distance < minDistance))
		{
			minDistance = distance;
			closest = target;
		}
	}

	return closest;
}

const BWAPI::Unitset & MicroManager::getUnits() const 
{ 
    return _units; 
}

void MicroManager::regroup(const BWAPI::Position & regroupPosition, const UnitCluster & cluster) const
{
	const int groundRegroupRadius = 96;
	const int airRegroupRadius = 8;

	BWAPI::Unitset units = Intersection(getUnits(), cluster.units);

	// for each of the units we have
	for (const auto unit : units)
	{
		// 1. A ground unit next to an enemy sieged tank should not move away.
		// 2. Other units will move to the regroup positions
		if (!unit->isFlying() &&
			BWAPI::Broodwar->getClosestUnit(unit->getPosition(),
			BWAPI::Filter::IsEnemy && BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode,64))
		{
			if (!mobilizeUnit(unit))
			{
				Micro::SmartAttackMove(unit, unit->getPosition());
				//Log().Debug() << "Ground unit " << unit->getType() << " " << unit->getID() << " near siege tank: attack move";
			}
		}
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Lurker && !UnitUtil::EnemyDetectorInRange(unit))
		{
			// Handle undetected lurkers as a special case.
			// Detected lurkers are handled in the regular cases below.
			if (unit->canBurrow())
			{
				(bool)immobilizeUnit(unit);
			}
			// Otherwise it is burrowed and undetected, or busy burrowing, so we can leave it.
		}
		else if (!unit->isFlying() && unit->getDistance(regroupPosition) > groundRegroupRadius)
		{
			// Ground units
			if (!mobilizeUnit(unit))
			{
				Micro::SmartMove(unit, regroupPosition);
				//Micro::SmartMovePath(unit, regroupPosition);
				//Log().Debug() << "Ground unit " << unit->getType() << " " << unit->getID() << " not reached regroup position: move";
			}
		}
		else if (unit->isFlying() && unit->getDistance(regroupPosition) > airRegroupRadius)
		{
			// Flying units
			Micro::SmartMove(unit, regroupPosition);
			//Log().Debug() << "Flying unit " << unit->getType() << " " << unit->getID() << " not reached regroup position: move";
		}
		else
		{
			// We have retreated to a good position.
			if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode || unit->getType() == BWAPI::UnitTypes::Zerg_Lurker)
			{
				(bool)immobilizeUnit(unit);
				// NOTE We don't want lurkers to hold position. Unlike other units, then they don't attack.
			}
			else
			{
				Micro::SmartHoldPosition(unit);
				//Log().Debug() << "Unit " << unit->getType() << " " << unit->getID() << " reached regroup position: hold position";
			}
		}
	}
}

bool MicroManager::unitNearEnemy(BWAPI::Unit unit) const
{
	assert(unit);

	BWAPI::Unitset enemyNear;

	MapGrid::Instance().GetUnits(enemyNear, unit->getPosition(), 800, false, true);

	return enemyNear.size() > 0;
}

// returns true if position:
// a) is walkable
// b) doesn't have buildings on it
// c) doesn't have a unit on it that can attack ground
bool MicroManager::checkPositionWalkable(BWAPI::Position pos) 
{
	// get x and y from the position
	int x(pos.x), y(pos.y);

	// walkable tiles exist every 8 pixels
	bool good = BWAPI::Broodwar->isWalkable(x/8, y/8);
	
	// if it's not walkable throw it out
	if (!good) return false;
	
	// for each of those units, if it's a building or an attacking enemy unit we don't want to go there
	for (auto & unit : BWAPI::Broodwar->getUnitsOnTile(x/32, y/32)) 
	{
		if	(unit->getType().isBuilding() || unit->getType().isResourceContainer() || 
			(unit->getPlayer() != BWAPI::Broodwar->self() && unit->getType().groundWeapon() != BWAPI::WeaponTypes::None)) 
		{		
				return false;
		}
	}

	// otherwise it's okay
	return true;
}

void MicroManager::trainSubUnits(BWAPI::Unit unit) const
{
	if (unit->getType() == BWAPI::UnitTypes::Protoss_Reaver)
	{
		if (!unit->isTraining() && unit->canTrain(BWAPI::UnitTypes::Protoss_Scarab))
		{
			unit->train(BWAPI::UnitTypes::Protoss_Scarab);
		}
	}
	else if (unit->getType() == BWAPI::UnitTypes::Protoss_Carrier)
	{
		if (!unit->isTraining() && unit->canTrain(BWAPI::UnitTypes::Protoss_Interceptor))
		{
			unit->train(BWAPI::UnitTypes::Protoss_Interceptor);
		}
	}
}

bool MicroManager::unitNearChokepoint(BWAPI::Unit unit) const
{
	for (const auto & area : BWEMmap.Areas())
	{
		for (const BWEM::ChokePoint * choke : area.ChokePoints())
		{
			if (unit->getDistance(BWAPI::Position(choke->Center())) < 5 * 32)
			{
				return true;
			}
		}
	}

	return false;
}

// Mobilize the unit if it is immobile: A sieged tank or a burrowed zerg unit.
// Return whether any action was taken.
bool MicroManager::mobilizeUnit(BWAPI::Unit unit) const
{
	if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode && unit->canUnsiege())
	{
		return unit->unsiege();
	}
	if (unit->isBurrowed() && unit->canUnburrow() &&
		!unit->isIrradiated() &&
		(double(unit->getHitPoints() / double(unit->getType().maxHitPoints())) > 0.25))  // very weak units stay burrowed
	{
		return unit->unburrow();
	}
	return false;
}

// Immobilize the unit: Siege a tank, burrow a lurker. Otherwise do nothing.
// Return whether any action was taken.
bool MicroManager::immobilizeUnit(BWAPI::Unit unit) const
{
	if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode && unit->canSiege())
	{
		return unit->siege();
	}
	if (!unit->isBurrowed() && unit->canBurrow() &&
		(unit->getType() == BWAPI::UnitTypes::Zerg_Lurker || unit->isIrradiated()))
	{
		return unit->burrow();
	}
	return false;
}

// Sometimes a unit on ground attack-move freezes in place.
// Luckily it's easy to recognize, though units may be on PlayerGuard for other reasons.
// Return whether any action was taken.
// This solves stuck zerglings, but doesn't always prevent other units from getting stuck.
bool MicroManager::unstickStuckUnit(BWAPI::Unit unit) const
{
	if (!unit->isMoving() && !unit->getType().isFlyer() && !unit->isBurrowed() &&
		unit->getOrder() == BWAPI::Orders::PlayerGuard &&
		BWAPI::Broodwar->getFrameCount() % 4 == 0)
	{
		Micro::SmartStop(unit);
		return true;
	}

	return false;
}

void MicroManager::initializeVectors() {
	kiteVector = BWAPI::Position(0, 0);
	tangentVector = BWAPI::Position(0, 0);
	accumulatedTangents = 0;
}

void MicroManager::updateVectors(BWAPI::Unit unit, BWAPI::Unit target, int inclusionScale, BWAPI::Position heading) {
	int distance = unit->getDistance(target);
	if (UnitUtil::IsThreat(unit, target, false)) {
		int range = UnitUtil::GetAttackRange(target, unit);
		if (distance < range + inclusionScale * (32 + 3 * (target->getType().topSpeed()))) {
			kiteVector += Micro::GetKiteVector(target, unit);
		}
		//difficult to say what to do with it right now until other things have been set up
		else if (heading.isValid() && distance < range + inclusionScale*(2 * 32) && target->getDistance(heading) < unit->getDistance(heading)) {
			accumulatedTangents++;
			//int scale = (target->getDistance(unit))/64; //let's just do short range for now...
			tangentVector += Micro::GetTangentVector(target, unit, heading);
		}

	}
}

BWAPI::Position	MicroManager::computeAttractionVector(BWAPI::Unit unit) {
	auto allies = BWAPI::Broodwar->getUnitsInRadius(unit->getPosition(), 48, BWAPI::Filter::GetType == unit->getType());
	return (allies.getPosition() - unit->getPosition());
}

void MicroManager::normalizeVectors() {
	if (kiteVector.getLength() >= 64) {  //normalize
		kiteVector = BWAPI::Position(int(64 * kiteVector.x / kiteVector.getLength()), int(64 * kiteVector.y / kiteVector.getLength()));
	}
	if (accumulatedTangents > 0) {
		tangentVector /= accumulatedTangents;
	}
}

// Choose a target from the set.
BWAPI::Unit MicroManager::getTargetSD(BWAPI::Unit unit, const BWAPI::Unitset & targets)
{
	int bestScore = INT_MIN;
	BWAPI::Unit bestTarget = nullptr;

	for (const auto target : targets)
	{
		const int priority = getTargetPrioritySD(target, unit->getType());	// 0..15
		const int range = unit->getDistance(target);						// 0..map size in pixels
		const bool isRanged = UnitUtil::IsRangedUnit(unit->getType());

		// Skip target we cannot attack, not detected, no hitpoints.
		if(target->getType() == BWAPI::UnitTypes::Zerg_Larva ||
			target->getType() == BWAPI::UnitTypes::Zerg_Egg ||
			!unit->isDetected() || unit->getHitPoints() <= 0 || 
			!UnitUtil::CanAttack(unit, target))
		{
			continue;
		}

		// Melee cannot hit targets under disruption web and don't want to attack targets under storm.
		if (!isRanged && (target->isUnderDisruptionWeb() || target->isUnderStorm()))
		{
			continue;
		}

		// Ranged and workers skip targets under dark swarm that we can't attack.
		if ((isRanged || unit->getType().isWorker()) && target->isUnderDarkSwarm() && 
			!target->getType().isBuilding() && !goodUnderDarkSwarm(unit->getType()))
		{
			continue;
		}

		// Skip targets that are too far away to worry about--outside tank range.
		if (range >= 16 * 32)
		{
			continue;
		}

		// Let's say that 1 priority step is worth 64 pixels (2 tiles).
		// We care about unit-target range and target-order position distance.
		int score = 2 * 32 * priority - range;

		// Adjust for special features.

		// This is what provides some focus fire behaviour, as we simulate previous attacker's hits
		double healthPercentage = (double)(target->getHitPoints() + target->getShields()) /
			(double)(target->getType().maxHitPoints() + target->getType().maxShields());
		score += (int)(160.0 * (1.0 - healthPercentage));

		// Avoid defensive matrix
		if (target->isDefenseMatrixed())
		{
			score -= 4 * 32;
		}

		// Give a bonus for enemies that are closer to our target position (usually the enemy base)
		if (target->getDistance(order.getPosition()) < unit->getDistance(order.getPosition()))
		{
			score += 2 * 32;
		}

		// Give bonus to units under dark swarm
		// Ranged units skip these targets earlier
		if (target->isUnderDarkSwarm())
		{
			score += 4 * 32;
		}

		// Adjust based on the threat level of the enemy unit to us
		if (target->canAttack(unit))
		{
			if (target->isInWeaponRange(unit))
			{
				score += 6 * 32;
			}
			else if (unit->isInWeaponRange(target))
			{
				score += 4 * 32;
			}
			else
			{
				score += 3 * 32;
			}
		}

		// Give a bonus to non-moving or braking targets, and a penalty to units that are faster than us
		if (!target->isMoving())
		{
			if (target->isSieged() ||
				target->getOrder() == BWAPI::Orders::Sieging ||
				target->getOrder() == BWAPI::Orders::Unsieging)
			{
				score += 48;
			}
			else
			{
				score += 24;
			}
		}
		else if (target->isBraking())
		{
			score += 16;
		}
		else if (target->getType().topSpeed() >= unit->getType().topSpeed())
		{
			score -= 4 * 32;
		}

		// Prefer to hit units that have acid spores on them from devourers.
		if (target->getAcidSporeCount() > 0)
		{
			// Especially if we're a mutalisk with a bounce attack.
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Mutalisk)
			{
				score += 16 * target->getAcidSporeCount();
			}
			else
			{
				score += 8 * target->getAcidSporeCount();
			}
		}

		// Take the damage type into account.
		BWAPI::DamageType damage = UnitUtil::GetWeapon(unit, target).damageType();
		if (damage == BWAPI::DamageTypes::Explosive)
		{
			if (target->getType().size() == BWAPI::UnitSizeTypes::Large)
			{
				score += 32;
			}
		}
		else if (damage == BWAPI::DamageTypes::Concussive)
		{
			if (target->getType().size() == BWAPI::UnitSizeTypes::Small)
			{
				score += 32;
			}
			else if (target->getType().size() == BWAPI::UnitSizeTypes::Large)
			{
				score -= 32;
			}
		}

		// Give a big bonus to SCVs repairing a bunker that we can attack without coming into range of the bunker
		if (target->isRepairing() &&
			target->getOrderTarget() &&
			target->getOrderTarget()->getType() == BWAPI::UnitTypes::Terran_Bunker &&
			unit->getDistance(target) <= range)
		{
			auto bunker = target->getOrderTarget();
			if (bunker && !bunker->isInWeaponRange(unit))
			{
				score += 256;
			}
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestTarget = target;
		}
	}

	return bestTarget;
}

// get the attack priority of a type
int MicroManager::getTargetPrioritySD(BWAPI::Unit target, BWAPI::UnitType type)
{
	const BWAPI::UnitType targetType = target->getType();
	auto closeToOurBase = [&target]()
	{
		auto myMain = InformationManager::Instance().getMyMainBaseLocation();
		auto ourBasePosition = BWAPI::Position(myMain->Location());
		return target->getDistance(ourBasePosition) < 1000;
	};

	// Scourge
	if (type == BWAPI::UnitTypes::Zerg_Scourge)
	{
		if (targetType == BWAPI::UnitTypes::Zerg_Overlord ||
			targetType == BWAPI::UnitTypes::Zerg_Scourge ||
			targetType == BWAPI::UnitTypes::Protoss_Interceptor ||
			targetType.isFlyingBuilding())
		{
			// Usually not worth scourge at all.
			return 0;
		}

		// Capital ships that are mostly vulnerable.
		if (targetType == BWAPI::UnitTypes::Terran_Science_Vessel ||
			targetType == BWAPI::UnitTypes::Terran_Valkyrie ||
			targetType == BWAPI::UnitTypes::Protoss_Carrier ||
			targetType == BWAPI::UnitTypes::Protoss_Arbiter ||
			targetType == BWAPI::UnitTypes::Zerg_Devourer ||
			targetType == BWAPI::UnitTypes::Zerg_Guardian ||
			targetType == BWAPI::UnitTypes::Zerg_Cocoon)
		{
			return 15;
		}

		// Everything else is the same.
		return 10;
	}

	// Devourers
	if (type == BWAPI::UnitTypes::Zerg_Devourer)
	{
		if (targetType.isBuilding())
		{
			// A lifted building is less important.
			return 1;
		}
		if (targetType == BWAPI::UnitTypes::Zerg_Scourge)
		{
			// Devourers are not good at attacking scourge.
			return 9;
		}

		// Everything else is the same.
		return 10;
	}

	// Always attack nearest unit if we are an ultralisk
	if (type == BWAPI::UnitTypes::Zerg_Ultralisk)
	{
		return 10;
	}

	if (targetType == BWAPI::UnitTypes::Zerg_Infested_Terran ||
		targetType == BWAPI::UnitTypes::Protoss_High_Templar ||
		targetType == BWAPI::UnitTypes::Protoss_Reaver ||
		(targetType == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine && !target->isBurrowed()) ||
		(targetType == BWAPI::UnitTypes::Protoss_Observer && UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar) > 0) ||
		(targetType == BWAPI::UnitTypes::Protoss_Observer && UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Lurker) > 0))
	{
		return 15;
	}

	if (targetType == BWAPI::UnitTypes::Protoss_Arbiter ||
		targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
	{
		return 14;
	}

	if (targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
		targetType == BWAPI::UnitTypes::Terran_Dropship ||
		targetType == BWAPI::UnitTypes::Terran_Science_Vessel ||
		targetType == BWAPI::UnitTypes::Protoss_Observer ||
		targetType == BWAPI::UnitTypes::Protoss_Shuttle ||
		targetType == BWAPI::UnitTypes::Zerg_Scourge ||
		targetType == BWAPI::UnitTypes::Zerg_Nydus_Canal)
	{
		return 13;
	}

	// Proxies
	if (target->getType().isBuilding() && closeToOurBase())
	{
		if (UnitUtil::CanAttackGround(target) || 
			UnitUtil::CanAttackAir(target) || 
			targetType == BWAPI::UnitTypes::Terran_Bunker)
		{
			return 12;
		}
		return 10;
	}

	if (targetType == BWAPI::UnitTypes::Terran_Bunker)
	{
		return 11;
	}

	// Workers
	if (targetType.isWorker())
	{
		if ((target->isConstructing() || target->isRepairing()) && closeToOurBase())
		{
			return 15;
		}

		// Blocking a narrow choke makes you critical.
		if (unitNearChokepoint(target) && !target->isFlying() && !target->isLifted())
		{
			return 14;
		}

		// Repairing
		if (target->isRepairing() && target->getOrderTarget())
		{
			// Something that can shoot
			if (target->getOrderTarget()->getType().groundWeapon() != BWAPI::WeaponTypes::None)
			{
				return 14;
			}

			// A bunker: only target the workers if we can't outrange the bunker
			if (target->getOrderTarget()->getType() == BWAPI::UnitTypes::Terran_Bunker &&
				UnitUtil::GetTrueGroundRange(BWAPI::UnitTypes::Terran_Marine, target->getPlayer()) > 128)
			{
				return 13;
			}
		}

		// Workers that have attacked in the last four seconds
		//if ((BWAPI::Broodwar->getFrameCount() - target->lastSeenAttacking) < 96)
		//{
		//	return 11;
		//}

		if (target->isConstructing())
		{
			return 10;
		}

		return 9;
	}

	if (UnitUtil::CanAttackGround(target) || UnitUtil::CanAttackAir(target))
	{
		return 11;
	}

	if (targetType.isSpellcaster())
	{
		return 10;
	}

	if (targetType.isResourceDepot())
	{
		return 7;
	}

	if (targetType == BWAPI::UnitTypes::Protoss_Pylon ||
		targetType == BWAPI::UnitTypes::Zerg_Spawning_Pool ||
		targetType == BWAPI::UnitTypes::Terran_Factory ||
		targetType == BWAPI::UnitTypes::Terran_Armory)
	{
		return 5;
	}

	if (targetType.isAddon())
	{
		return 1;
	}

	if (!target->isCompleted() || (targetType.requiresPsi() && !target->isPowered()))
	{
		return 2;
	}

	if (targetType.gasPrice() > 0)
	{
		return 4;
	}

	if (targetType.mineralPrice() > 0)
	{
		return 3;
	}

	return 1;
}

// The unit's ranged ground weapon does splash damage, so it works under dark swarm.
// Firebats are not here: They are melee units.
bool MicroManager::goodUnderDarkSwarm(BWAPI::UnitType type)
{
	return
		type == BWAPI::UnitTypes::Protoss_Archon ||
		type == BWAPI::UnitTypes::Protoss_Reaver ||
		type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
		type == BWAPI::UnitTypes::Zerg_Lurker;
}

void MicroManager::drawOrderText()
{
	if (Config::Debug::DrawUnitTargetInfo)
	{
		for (auto & unit : _units)
		{
			int textLength = order.getStatus().length() * 5;
			BWAPI::Broodwar->drawTextMap(unit->getPosition().x-(textLength/2), unit->getPosition().y, "%s", order.getStatus().c_str());
		}
	}
}

#include "RangedManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;
using namespace BWAPI;
using namespace UnitTypes;
using namespace UnitUtil;

RangedManager::RangedManager() 
{ 
}

void RangedManager::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
	BWAPI::Unitset units = Intersection(getUnits(), cluster.units);
	if (units.empty())
	{
		return;
	}
	assignTargets(units, targets);
}

void RangedManager::assignTargets(const BWAPI::Unitset & rangedUnits, const BWAPI::Unitset & targets)
{
	// The set of potential targets.
	BWAPI::Unitset rangedUnitTargets;
	std::copy_if(targets.begin(), targets.end(), std::inserter(rangedUnitTargets, rangedUnitTargets.end()),
		[](BWAPI::Unit u) {
		return
			u->isVisible() &&
			u->isDetected() &&
			u->getType() != BWAPI::UnitTypes::Zerg_Larva &&
			u->getType() != BWAPI::UnitTypes::Zerg_Egg &&
			!u->isStasised();
	});

	int count = 0;
    for (auto & rangedUnit : rangedUnits)
	{
		count++;

		// Special micro eg. storm dodge
		if (Micro::CheckSpecialCases(rangedUnit)) continue;

		// train sub units such as scarabs or interceptors
		trainSubUnits(rangedUnit);

		// Irradiated units attack move toward enemy position
		if (rangedUnit->isIrradiated()) 
		{
			auto enemyMain = InformationManager::Instance().getEnemyMainBaseLocation();
			auto orderPosition = order.getPosition();
			if (enemyMain)
			{
				orderPosition = enemyMain->Center();
			}
			if (rangedUnit->getDistance(order.getPosition()) < 300)
			{
				Micro::SmartAttackMove(rangedUnit, orderPosition);
			}
			else
			{
				Micro::SmartMove(rangedUnit, orderPosition);
			}
			continue;
		}
		/*
		// Unstick stucked units
		if (unstickStuckUnit(rangedUnit))
		{
			continue;
		}
		*/

		// Ignore targets when regrouping if we are not scourge
		if (order.getType() == SquadOrderTypes::Regroup && rangedUnit->getType() != BWAPI::UnitTypes::Zerg_Scourge)
		{
			if (rangedUnit->isFlying() && rangedUnit->getDistance(order.getPosition()) > 8)
			{
				//Micro::SmartMove(rangedUnit, order.getPosition());
				Micro::SmartMovePath(rangedUnit, order.getPosition());
			}
			else if (!rangedUnit->isFlying() && rangedUnit->getDistance(order.getPosition()) > 96)
			{
				//Micro::SmartMove(rangedUnit, order.getPosition());
				Micro::SmartMovePath(rangedUnit, order.getPosition());
			}
			else
			{
				Micro::SmartHoldPosition(rangedUnit);
			}
			continue;
		}

		// if the order is to attack or defend or regroup and we are scourge
		if (order.getType() == SquadOrderTypes::Attack || 
			order.getType() == SquadOrderTypes::Defend || 
			(order.getType() == SquadOrderTypes::Regroup && rangedUnit->getType() == BWAPI::UnitTypes::Zerg_Scourge))
		{
			// If a target can be found.
			BWAPI::Unit target = getTarget(rangedUnit, rangedUnitTargets);
			if (target)
			{
				// We have a target, attack it
				// Kite if target can attack ranged unit. Scourge do not kite.
				if (Config::Micro::KiteWithRangedUnits && 
					UnitUtil::CanAttack(target, rangedUnit) &&
					UnitUtil::CanAttack(rangedUnit, target) &&
					rangedUnit->getType() != BWAPI::UnitTypes::Zerg_Scourge &&
					rangedUnit->getDistance(target->getPosition()) < 192)
				{
					if (rangedUnit->getType() == BWAPI::UnitTypes::Zerg_Mutalisk || 
						rangedUnit->getType() == BWAPI::UnitTypes::Terran_Vulture)
					{
						Micro::MutaDanceTarget(rangedUnit, target);
						//Log().Debug() << "Unit " << rangedUnit->getType() << " " << rangedUnit->getID() << " MutaDance command";
					}
					else
					{
						Micro::SmartKiteTarget(rangedUnit, target);
						//Log().Debug() << "Unit " << rangedUnit->getType() << " " << rangedUnit->getID() << " Kite command";
					}
				}
				else if(rangedUnit->getDistance(target->getPosition()) < 192)
				{
					Micro::SmartAttackUnit(rangedUnit, target);
					//Log().Debug() << "Unit " << rangedUnit->getType() << " " << rangedUnit->getID() << " Attack command";
				}
				else
				{
					// Targets found. Move closer.
					if (rangedUnit->isFlying())
					{
						//Micro::SmartMove(rangedUnit, target->getPosition());
						Micro::SmartMovePath(rangedUnit, target->getPosition());
					}
					else if (!rangedUnit->isFlying())
					{
						//Micro::SmartMove(rangedUnit, target->getPosition());
						Micro::SmartMovePath(rangedUnit, target->getPosition());
					}
				}
			}
			else
			{
				// No target found. If we're not near the order position, go there.
				if (rangedUnit->isFlying() && rangedUnit->getDistance(order.getPosition()) > 8)
				{
					//Micro::SmartMove(rangedUnit, order.getPosition());
					Micro::SmartMovePath(rangedUnit, order.getPosition());
				}
				else if (!rangedUnit->isFlying() && rangedUnit->getDistance(order.getPosition()) > 96)
				{
					//Micro::SmartMove(rangedUnit, order.getPosition());
					Micro::SmartMovePath(rangedUnit, order.getPosition());
				}
				else
				{
					Micro::SmartHoldPosition(rangedUnit);
				}
			}
		}

		if (Config::Debug::DrawUnitTargetInfo)
		{
			BWAPI::Broodwar->drawLineMap(rangedUnit->getPosition(), rangedUnit->getTargetPosition(), BWAPI::Colors::Purple);
		}
	}
}

// get a target to attack
BWAPI::Unit RangedManager::getTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets)
{
	int highPriority = 0;
	int closestDist = 99999;
	BWAPI::Unit bestTarget = nullptr;

	for (const auto & target : targets)
	{
		int distance = rangedUnit->getDistance(target);
		int priority = getAttackPriority(rangedUnit, target);

		//BWAPI::Broodwar->drawTextMap(target->getPosition(), "%d", priority);

		if (priority > 0)
		{
			if (!bestTarget || (priority > highPriority) || (priority == highPriority && distance < closestDist))
			{
				closestDist = distance;
				highPriority = priority;
				bestTarget = target;
			}
		}
	}

	return bestTarget;
}

// get the attack priority of a target unit
int RangedManager::getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target) 
{
	BWAPI::UnitType rangedType = rangedUnit->getType();
	BWAPI::UnitType targetType = target->getType();

	if (target->isFlying() && !CanAttackAir(rangedUnit))
	{
		return 0;
	}
	else if (!target->isFlying() && !CanAttackGround(rangedUnit))
	{
		return 0;
	}

	auto distance = rangedUnit->getDistance(target);
	auto range = target->isFlying() ? rangedType.airWeapon().maxRange() : rangedType.groundWeapon().maxRange();

	// far units get low priority
	if (distance > range + 600)
	{
		return 1;
	}
	// Scourge
	if (rangedType == BWAPI::UnitTypes::Zerg_Scourge)
	{
		if (!targetType.isFlyer())
		{
			// Can't target it. Also, ignore lifted buildings.
			return 0;
		}
		if (targetType == BWAPI::UnitTypes::Zerg_Overlord ||
			targetType == BWAPI::UnitTypes::Zerg_Scourge ||
			targetType == BWAPI::UnitTypes::Protoss_Interceptor || 
			targetType.isFlyingBuilding())
		{
			// Usually not worth scourge at all.
			return 0;
		}

		// Arbiters first.
		if (targetType == BWAPI::UnitTypes::Protoss_Arbiter)
		{
			return 10;
		}

		// Everything else is the same. Hit whatever's closest.
		return 9;
	}
	// Devourers
	if (rangedType == BWAPI::UnitTypes::Zerg_Devourer)
	{
		if (!target->isFlying())
		{
			// Can't target it.
			return 0;
		}
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
	//Guardians
	if (rangedType == BWAPI::UnitTypes::Zerg_Guardian && target->isFlying())
	{
		// Can't target it.
		return 0;
	}
	// An addon other than a completed comsat is boring.
	// TODO should also check that it is attached
	if (targetType.isAddon() && !(targetType == BWAPI::UnitTypes::Terran_Comsat_Station && target->isCompleted()))
	{
		return 1;
	}
	// if the target is building something near our base something is fishy
	BWAPI::Position ourBasePosition(InformationManager::Instance().getMyMainBaseLocation()->Center());
	if (target->getDistance(ourBasePosition) < 1200) {
		if (target->getType().isWorker() && (target->isConstructing() || target->isRepairing()))
		{
			return 12;
		}
		if (target->getType().isBuilding())
		{
			// This includes proxy buildings, which deserve high priority.
			// But when bases are close together, it can include innocent buildings.
			// We also don't want to disrupt priorities in case of proxy buildings
			// supported by units; we may want to target the units first.
			if (UnitUtil::CanAttackGround(target) || UnitUtil::CanAttackAir(target))
			{
				return 12;
			}
			return 8;
		}
	}

	if (rangedType.isFlyer()) {
		// Exceptions if we're a flyer (other than scourge, which is handled above).
		if (targetType == BWAPI::UnitTypes::Zerg_Scourge)
		{
			return 12;
		}
	}
	else
	{
		// Exceptions if we're a ground unit.
		if (targetType == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine && !target->isBurrowed() ||
			targetType == BWAPI::UnitTypes::Zerg_Infested_Terran)
		{
			return 12;
		}
	}

	// Short circuit: Static defense
	if (UnitUtil::CanAttack(targetType, rangedType) &&
		(targetType == BWAPI::UnitTypes::Terran_Missile_Turret ||
		targetType == BWAPI::UnitTypes::Terran_Bunker ||
		targetType == BWAPI::UnitTypes::Protoss_Photon_Cannon || 
		targetType == BWAPI::UnitTypes::Zerg_Sunken_Colony || 
		targetType == BWAPI::UnitTypes::Zerg_Spore_Colony))
	{
		return 12;
	}
	if (!UnitUtil::CanAttack(targetType, rangedType) && 
		(targetType == BWAPI::UnitTypes::Terran_Missile_Turret ||
		targetType == BWAPI::UnitTypes::Terran_Bunker ||
		targetType == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
		targetType == BWAPI::UnitTypes::Zerg_Sunken_Colony || 
		targetType == BWAPI::UnitTypes::Zerg_Spore_Colony))
	{
		return 10;
	}
	// Without forge, P can not make cannons
	if (targetType == BWAPI::UnitTypes::Protoss_Forge)
	{
		return 10;
	}
	// Combat units with no waepons.
	if (targetType == BWAPI::UnitTypes::Protoss_High_Templar || 
		targetType == BWAPI::UnitTypes::Protoss_Reaver ||
		targetType == BWAPI::UnitTypes::Protoss_Arbiter)
	{
		return 12;
	}
	// Threats can attack us. Exception: Workers are not threats.
	if (UnitUtil::CanAttack(targetType, rangedType) && !targetType.isWorker())
	{
		// Enemy unit which is far enough outside its range is lower priority than a worker.
		if (rangedUnit->getDistance(target) > 64 + UnitUtil::GetAttackRange(target, rangedUnit))
		{
			return 8;
		}
		return 12;
	}
	// Droppers are as bad as threats. They may be loaded and are often isolated and safer to attack.
	if (targetType == BWAPI::UnitTypes::Terran_Dropship ||
		targetType == BWAPI::UnitTypes::Protoss_Shuttle)
	{
		return 12;
	}
	// Also as bad are other dangerous things.
	if (targetType == BWAPI::UnitTypes::Terran_Science_Vessel ||
		targetType == BWAPI::UnitTypes::Zerg_Scourge ||
		targetType == BWAPI::UnitTypes::Protoss_Observer)
	{
		return 12;
	}
	// Next are workers.
	if (targetType.isWorker())
	{
		if (rangedUnit->getType() == BWAPI::UnitTypes::Terran_Vulture)
		{
			return 12;
		}
		// Workers doing something
		if (target->isRepairing() || target->isConstructing() || unitNearChokepoint(target) ||
			target->isBraking() || !target->isMoving() || target->isGatheringMinerals() || target->isGatheringGas())
		{
			return 12;
		}
		return 12;
	}
	// Important combat units that we may not have targeted above (esp. if we're a flyer).
	if (targetType == BWAPI::UnitTypes::Protoss_Carrier ||
		targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
		targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
	{
		return 12;
	}
	// Nydus canal is the most important building to kill.
	if (targetType == BWAPI::UnitTypes::Zerg_Nydus_Canal)
	{
		return 8;
	}
	// Spell casters are as important as key buildings.
	// Also remember to target non-threat combat units.
	if (targetType.isSpellcaster() ||
		targetType.groundWeapon() != BWAPI::WeaponTypes::None ||
		targetType.airWeapon() != BWAPI::WeaponTypes::None)
	{
		return 8;
	}
	// Templar tech and spawning pool are more important.
	if (targetType == BWAPI::UnitTypes::Protoss_Templar_Archives)
	{
		return 7;
	}
	if (targetType == BWAPI::UnitTypes::Zerg_Spawning_Pool)
	{
		return 7;
	}
	// Don't forget the nexus/cc/hatchery.
	if (targetType.isResourceDepot())
	{
		return 6;
	}
	if (targetType == BWAPI::UnitTypes::Protoss_Pylon)
	{
		return 5;
	}
	if (targetType == BWAPI::UnitTypes::Terran_Factory || targetType == BWAPI::UnitTypes::Terran_Armory)
	{
		return 5;
	}
	// Downgrade unfinished/unpowered buildings, with exceptions.
	if (targetType.isBuilding() &&
		(!target->isCompleted() || !target->isPowered()) &&
		!(targetType.isResourceDepot() ||
		targetType.groundWeapon() != BWAPI::WeaponTypes::None ||
		targetType.airWeapon() != BWAPI::WeaponTypes::None ||
		targetType == BWAPI::UnitTypes::Terran_Bunker))
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
	// Finally everything else.
	return 1;
}

BWAPI::Unit RangedManager::closestrangedUnit(BWAPI::Unit target, std::set<BWAPI::Unit> & rangedUnitsToAssign)
{
	double minDistance = 0;
	BWAPI::Unit closest = nullptr;

	for (auto & rangedUnit : rangedUnitsToAssign)
	{
		int distance = rangedUnit->getDistance(target);
		if (!closest || distance < minDistance)
		{
			minDistance = distance;
			closest = rangedUnit;
		}
	}
	
	return closest;
}


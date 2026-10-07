#include "MeleeManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;
using namespace BWAPI;
using namespace UnitTypes;
using namespace UnitUtil;

// Note: Melee units are ground units only. Scourge is a "ranged" unit.

MeleeManager::MeleeManager() 
{ 
}

void MeleeManager::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
	BWAPI::Unitset units = Intersection(getUnits(), cluster.units);
	if (units.empty())
	{
		return;
	}
	assignTargets(units, targets);
}

void MeleeManager::assignTargets(const BWAPI::Unitset & meleeUnits, const BWAPI::Unitset & targets)
{
	// figure out targets
	BWAPI::Unitset meleeUnitTargets;
	for (auto & target : targets) 
	{
		// conditions for targeting
		if (target->isVisible() &&
			target->isDetected() &&
			!target->isFlying() &&
			!target->isLifted() &&
			target->getPosition().isValid() &&
			target->getType() != BWAPI::UnitTypes::Zerg_Larva && 
			target->getType() != BWAPI::UnitTypes::Zerg_Egg &&
			!target->isStasised() && 
			!target->isInvincible() &&
			!target->isUnderDisruptionWeb())             // melee unit can't attack under dweb
		{
			meleeUnitTargets.insert(target);
		}
	}

	int count = 0;
	for (auto & meleeUnit : meleeUnits)
	{
		count++;

		// Special micro eg. storm dodge
		if (Micro::CheckSpecialCases(meleeUnit)) continue;

		// Irradiated units attack move toward enemy position
		if (meleeUnit->isIrradiated())
		{
			auto enemyMain = InformationManager::Instance().getEnemyMainBaseLocation();
			auto orderPosition = order.getPosition();
			if (enemyMain)
			{
				orderPosition = enemyMain->Center();
			}
			if (meleeUnit->getDistance(order.getPosition()) < 300)
			{
				Micro::SmartAttackMove(meleeUnit, orderPosition);
			}
			else
			{
				Micro::SmartMove(meleeUnit, orderPosition);
			}
			continue;
		}
		/*
		// Unstick stucked units
		if (unstickStuckUnit(meleeUnit))
		{
			continue;
		}
		*/

		// Ignore targets when regrouping
		if (order.getType() == SquadOrderTypes::Regroup)
		{
			if (meleeUnit->getDistance(order.getPosition()) > 96)
			{	
				//Micro::SmartMove(meleeUnit, order.getPosition());
				Micro::SmartMovePath(meleeUnit, order.getPosition());
			}
			else
			{
				Micro::SmartHoldPosition(meleeUnit);
			}
			continue;
		}

		// if the order is to attack or defend
		if (order.getType() == SquadOrderTypes::Attack || 
			order.getType() == SquadOrderTypes::Defend)
		{
			// run away if we meet the retreat criterion
			if (meleeUnitShouldRetreat(meleeUnit, targets))
			{
				BWAPI::Position fleeTo(InformationManager::Instance().getMyMainBaseLocation()->Center());
				//Micro::SmartMove(meleeUnit, fleeTo);
				Micro::SmartMovePath(meleeUnit, fleeTo);
			}
			else
			{
				BWAPI::Unit target = getTarget(meleeUnit, meleeUnitTargets);
				if (target)
				{
					if (meleeUnit->getDistance(target->getPosition()) < 96)
					{
						// There are targets. Pick the best one and attack it.
						//Micro::CatchAndAttackUnit(meleeUnit, target);
						Micro::SmartAttackUnit(meleeUnit, target);
					}
					else
					{
						// There are targets. Move closer.
						//Micro::SmartMove(meleeUnit, target->getPosition());
						Micro::SmartMovePath(meleeUnit, target->getPosition());
					}
				}
				else if (meleeUnit->getDistance(order.getPosition()) > 96)
				{
					// There are no targets. Move to the order position if not already close.
					//Micro::SmartMove(meleeUnit, order.getPosition());
					Micro::SmartMovePath(meleeUnit, order.getPosition());
				}
				else
				{
					Micro::SmartHoldPosition(meleeUnit);
				}
			}
		}

		if (Config::Debug::DrawUnitTargetInfo)
		{
			BWAPI::Broodwar->drawLineMap(meleeUnit->getPosition(), meleeUnit->getTargetPosition(), BWAPI::Colors::Purple);
		}
	}
}


// get a target for the meleeUnit to attack
BWAPI::Unit MeleeManager::getTarget(BWAPI::Unit meleeUnit, const BWAPI::Unitset & targets)
{
	int highPriority = 0;
	int closestDist = 99999;
	BWAPI::Unit bestTarget = nullptr;

	// for each target possiblity
	for (auto & target : targets)
	{
		int priority = getAttackPriority(meleeUnit, target);
		int distance = meleeUnit->getDistance(target);

		//BWAPI::Broodwar->drawTextMap(target->getPosition(), "%d", priority);

		if (priority > 0)
		{
			// if it's a higher priority, or it's closer, set it
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

// get the attack priority of a type
int MeleeManager::getAttackPriority(BWAPI::Unit meleeUnit, BWAPI::Unit target) 
{
	BWAPI::UnitType meleeType = meleeUnit->getType();
	BWAPI::UnitType targetType = target->getType();

	// Short circuit: Do not attack flying units or buildings
	if (target->isFlying() ||  target->isLifted())
	{
		return 0;
	}

	// dont care about far units
	if (meleeUnit->getDistance(target) > 600)
	{
		return 1;
	}

	// always attack nearest unit if we are an ultralisk
	if (meleeUnit->getType() == BWAPI::UnitTypes::Zerg_Ultralisk)
	{
		return 10;
	}

	// faster units are less important
	if (targetType.topSpeed() >= 0.8 * meleeUnit->getType().topSpeed())
	{
		return 8;
	}

	// Short Circuit: Terran buildings near chokepoint (properly part of wall)
	if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Terran &&
		targetType.isBuilding() && !targetType.isFlyingBuilding() && unitNearChokepoint(target) &&
		!target->isFlying() && !target->isLifted())
	{
		return 12;
	}

	// Exceptions for dark templar.
	if (meleeUnit->getType() == BWAPI::UnitTypes::Protoss_Dark_Templar)
	{
		if (target->getType() == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine)
		{
			return 10;
		}
		if ((targetType == BWAPI::UnitTypes::Terran_Missile_Turret || targetType == BWAPI::UnitTypes::Terran_Comsat_Station) &&
			(BWAPI::Broodwar->self()->deadUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar) == 0))
		{
			return 9;
		}
		if (target->getType().isWorker())
		{
			return 8;
		}
	}

	// Short circuit: Enemy unit which is far enough outside its range is lower priority than a worker.
	int enemyRange = UnitUtil::GetAttackRange(target, meleeUnit);
	if (enemyRange && !targetType.isWorker() &&
		meleeUnit->getDistance(target) > 96 + enemyRange)
	{
		return 8;
	}
	if (targetType == BWAPI::UnitTypes::Terran_Missile_Turret ||
		targetType == BWAPI::UnitTypes::Zerg_Spore_Colony ||
		targetType == BWAPI::UnitTypes::Protoss_Forge)
	{
		return 10;
	}
	// Short circuit: Static defense
	if (targetType == BWAPI::UnitTypes::Terran_Bunker ||
		targetType == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
		targetType == BWAPI::UnitTypes::Zerg_Sunken_Colony)
	{
		return 12;
	}
	// Medics and dangerous combat units.
	if (targetType == BWAPI::UnitTypes::Terran_Medic ||
		targetType == BWAPI::UnitTypes::Protoss_High_Templar ||
		targetType == BWAPI::UnitTypes::Protoss_Reaver ||
		targetType == BWAPI::UnitTypes::Zerg_Lurker)
	{
		return 12;
	}
	// Threats can attack us. Exception: Workers are not threats.
	if (UnitUtil::CanAttack(targetType, meleeType) && !targetType.isWorker())
	{
		// Enemy unit which is far enough outside its range is lower priority than a worker.
		if (meleeUnit->getDistance(target) > 64 + UnitUtil::GetAttackRange(target, meleeUnit))
		{
			return 8;
		}
		return 12;
	}
	// next priority is worker
	if (targetType.isWorker())
	{
		if (target->isRepairing() || target->isConstructing() || unitNearChokepoint(target) ||
			target->isBraking() || !target->isMoving() || target->isGatheringMinerals() || target->isGatheringGas())
		{
			return 12;
		}
		return 12;
	}
	// Buildings come under attack during free time, so they can be split into more levels.
	if (targetType == BWAPI::UnitTypes::Zerg_Spire || targetType == BWAPI::UnitTypes::Zerg_Nydus_Canal)
	{
		return 6;
	}
	if (targetType == BWAPI::UnitTypes::Zerg_Spawning_Pool ||
		targetType.isResourceDepot() ||
		targetType == BWAPI::UnitTypes::Protoss_Templar_Archives ||
		targetType.isSpellcaster())
	{
		return 5;
	}
	// Short circuit: Addons other than a completed comsat are worth almost nothing.
	// TODO should also check that it is attached
	if (targetType.isAddon() && !(targetType == BWAPI::UnitTypes::Terran_Comsat_Station && target->isCompleted()))
	{
		return 1;
	}
	// anything with a cost
	if (targetType.gasPrice() > 0 || targetType.mineralPrice() > 0)
	{
		return 3;
	}

	// then everything else
	return 1;
}

BWAPI::Unit MeleeManager::closestMeleeUnit(BWAPI::Unit target, const BWAPI::Unitset & meleeUnitsToAssign)
{
	int minDistance = 0;
	BWAPI::Unit closest = nullptr;

	for (auto & meleeUnit : meleeUnitsToAssign)
	{
		int distance = meleeUnit->getDistance(target);
		if (!closest || distance < minDistance)
		{
			minDistance = distance;
			closest = meleeUnit;
		}
	}
	
	return closest;
}

bool MeleeManager::meleeUnitShouldRetreat(BWAPI::Unit meleeUnit, const BWAPI::Unitset & targets)
{
    // terran don't regen so it doesn't make any sense to retreat
    if (meleeUnit->getType().getRace() == BWAPI::Races::Terran)
    {
        return false;
    }

    // we don't want to retreat the melee unit if its shields or hit points are above the threshold set in the config file
    // set those values to zero if you never want the unit to retreat from combat individually
    if (meleeUnit->getShields() > Config::Micro::RetreatMeleeUnitShields || 
		meleeUnit->getHitPoints() > Config::Micro::RetreatMeleeUnitHP)
    {
        return false;
    }

    // if there is a ranged enemy unit within attack range of this melee unit then we shouldn't bother retreating since it could fire and kill it anyway
    for (auto & target : targets)
    {
		int groundWeaponRange = target->getType().groundWeapon().maxRange();
		if (groundWeaponRange >= 64 && target->getDistance(meleeUnit) < groundWeaponRange)
        {
            return false;
        }
    }

	// A broodling should not retreat since it is on a timer and regeneration does it no good.
	if (meleeUnit->getType() == BWAPI::UnitTypes::Zerg_Broodling)
	{
		return false;
	}

    return true;
}


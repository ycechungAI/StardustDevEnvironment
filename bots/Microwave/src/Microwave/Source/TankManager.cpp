#include "TankManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

TankManager::TankManager() 
{ 
}

void TankManager::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
	BWAPI::Unitset units = Intersection(getUnits(), cluster.units);
	if (units.empty())
	{
		return;
	}
	assignTargets(units, targets);
}

void TankManager::assignTargets(const BWAPI::Unitset & tanks, const BWAPI::Unitset & targets)
{
	// figure out targets
	BWAPI::Unitset tankTargets;
    std::copy_if(targets.begin(), targets.end(), std::inserter(tankTargets, tankTargets.end()), 
                 [](BWAPI::Unit u){ return u->isVisible() && !u->isFlying(); });
    
    int siegeTankRange = BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode.groundWeapon().maxRange() - 32;
    bool haveSiege = BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Tank_Siege_Mode);



	for (auto & tank : tanks)
	{
		// Check if tank is near choke
        bool tankNearChokepoint = false; 
		for (const auto & area : BWEMmap.Areas())
		{
			for (const BWEM::ChokePoint * choke : area.ChokePoints())
			{
				if (tank->getDistance(BWAPI::Position(choke->Center())) < 64)
				{
					tankNearChokepoint = true;
					break;
				}
			}
		}

		// if the order is to attack or defend
		if (order.getType() == SquadOrderTypes::Attack || 
			order.getType() == SquadOrderTypes::Defend ||
			order.getType() == SquadOrderTypes::Regroup)
        {
			// if there are targets
			if (!tankTargets.empty())
			{
				// find the best target for this tank
				BWAPI::Unit target = getTarget(tank, tankTargets);

                if (target && Config::Debug::DrawUnitTargetInfo) 
	            {
		            BWAPI::Broodwar->drawLineMap(tank->getPosition(), tank->getTargetPosition(), BWAPI::Colors::Purple);
	            }

                // if we are within siege range, siege up
                if (tank->getDistance(target) < siegeTankRange && tank->canSiege() && !tankNearChokepoint)
                {
                    tank->siege();
                }
                // otherwise unsiege and move in
                else if ((!target || tank->getDistance(target) > siegeTankRange) && tank->canUnsiege())
                {
					if (BWAPI::Broodwar->getFrameCount() % 200 == 0)
					{
						tank->unsiege();
					}
                }

                // if we're in siege mode just attack the target
                if (tank->isSieged())
                {
                    Micro::SmartAttackUnit(tank, target);
                }
                // if we're not in siege mode kite the target
                else
                {
                    Micro::SmartKiteTarget(tank, target);
                }
			}
			// if there are no targets
			else
			{
				// if we're not near the order position
				if (tank->getDistance(order.getPosition()) > 100)
				{
                    if (tank->canUnsiege())
                    {
						if (BWAPI::Broodwar->getFrameCount() % 200 == 0)
						{
							tank->unsiege();
						}                   
                    }
                    else
                    {
    					// move to it
    					Micro::SmartMove(tank, order.getPosition());
                    }
				}
			}
		}
	}
}

// get a target for the tank to attack
BWAPI::Unit TankManager::getTarget(BWAPI::Unit tank, const BWAPI::Unitset & targets)
{
	int highPriority = 0;
	int closestDist = 99999;
	BWAPI::Unit bestTarget = nullptr;

	int siegeTankRange = BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode.groundWeapon().maxRange() - 8;
	BWAPI::Unitset targetsInSiegeRange;
	for (const auto target : targets)
	{
		if (target->getDistance(tank) < siegeTankRange && !target->isFlying())
		{
			targetsInSiegeRange.insert(target);
		}
	}

	const BWAPI::Unitset & newTargets = targetsInSiegeRange.empty() ? targets : targetsInSiegeRange;

	// check first for units that are in range of our attack that can cause damage
	// choose the highest priority one from them at the lowest health
	for (const auto target : newTargets)
	{
		if (target->isFlying())
		{
			continue;
		}

		int distance = tank->getDistance(target);
		int priority = getAttackPriority(tank, target);

		if (!bestTarget || (priority > highPriority) || (priority == highPriority && distance < closestDist))
		{
			closestDist = distance;
			highPriority = priority;
			bestTarget = target;
		}
	}

	return bestTarget;
}

	// get the attack priority of a type in relation to a zergling
int TankManager::getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target) 
{
	BWAPI::UnitType rangedType = rangedUnit->getType();
	BWAPI::UnitType targetType = target->getType();

	if (target->getType() == BWAPI::UnitTypes::Zerg_Larva || target->getType() == BWAPI::UnitTypes::Zerg_Egg)
	{
		return 0;
	}

	// if the target is building something near our base something is fishy
	BWAPI::Position ourBasePosition = BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());
	if (target->getType().isWorker() && (target->isConstructing() || target->isRepairing()) && target->getDistance(ourBasePosition) < 1200)
	{
		return 12;
	}

	if (target->getType().isBuilding() && (target->isCompleted() || target->isBeingConstructed()) && target->getDistance(ourBasePosition) < 1200)
	{
		return 12;
	}

	bool isThreat = UnitUtil::TypeCanAttackGround(targetType);    // includes bunkers
	if (target->getType().isWorker())
	{
		isThreat = false;
	}

	// The most dangerous enemy units.
	if (targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
		targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
		targetType == BWAPI::UnitTypes::Protoss_High_Templar ||
		targetType == BWAPI::UnitTypes::Protoss_Reaver ||
		targetType == BWAPI::UnitTypes::Zerg_Infested_Terran)
	{
		return 12;
	}
	// something that can attack us or aid in combat
	if (isThreat ||
		targetType == BWAPI::UnitTypes::Protoss_Forge)
	{
		return 10;
	}
	// next priority is any unit on the ground
	if (!targetType.isBuilding())
	{
		return 9;
	}

	// next is special buildings
	if (targetType == BWAPI::UnitTypes::Zerg_Nydus_Canal)
	{
		return 6;
	}
	if (targetType == BWAPI::UnitTypes::Zerg_Spawning_Pool)
	{
		return 5;
	}
	if (targetType == BWAPI::UnitTypes::Protoss_Pylon || targetType == BWAPI::UnitTypes::Protoss_Templar_Archives)
	{
		return 5;
	}

	// any buildings that cost gas
	if (targetType.gasPrice() > 0)
	{
		return 4;
	}
	if (targetType.mineralPrice() > 0)
	{
		return 3;
	}

	// then everything else
	return 1;

}


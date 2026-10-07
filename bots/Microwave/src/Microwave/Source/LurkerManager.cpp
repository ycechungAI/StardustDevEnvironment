#include "LurkerManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

LurkerManager::LurkerManager()
{
}

void LurkerManager::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
	BWAPI::Unitset units = Intersection(getUnits(), cluster.units);
	if (units.empty())
	{
		return;
	}
	assignTargets(units, targets);
}

void LurkerManager::assignTargets(const BWAPI::Unitset & lurkers, const BWAPI::Unitset & targets)
{
	// figure out targets
	BWAPI::Unitset LurkerTargets;
	std::copy_if(targets.begin(), targets.end(), std::inserter(LurkerTargets, LurkerTargets.end()),
		[](BWAPI::Unit u)
	{ 
		return 
			u->isVisible() &&
			u->isDetected() &&
			!u->isFlying() &&
			!u->isLifted() &&
			u->getPosition().isValid() &&
			u->getType() != BWAPI::UnitTypes::Zerg_Larva &&
			u->getType() != BWAPI::UnitTypes::Zerg_Egg &&
			!u->isStasised();
	});

	for (auto & lurker : lurkers)
	{
		// Special micro eg. storm dodge
		if (Micro::CheckSpecialCases(lurker)) continue;

		// Check if lurker is near choke
		bool lurkerNearChokepoint = false;
		for (const auto & area : BWEMmap.Areas())
		{
			for (const BWEM::ChokePoint * choke : area.ChokePoints())
			{
				if (lurker->getDistance(BWAPI::Position(choke->Center())) < 64)
				{
					lurkerNearChokepoint = true;
					break;
				}
			}
		}
		/*
		// Ignore targets when regrouping, only if lurkers are dead
		if (order.getType() == SquadOrderTypes::Regroup &&
			BWAPI::Broodwar->self()->deadUnitCount(BWAPI::UnitTypes::Zerg_Lurker) > 0)
		{
			if (lurker->getDistance(order.getPosition()) > 96)
			{
				if (lurker->canUnburrow())
				{
					if (okToUnburrow(lurker))
					{
						lurker->unburrow();
					}
				}
				else
				{
					//Micro::SmartMove(lurker, order.getPosition());
					Micro::SmartMovePath(lurker, order.getPosition());
				}
			}
			else
			{
				if (lurker->canBurrow())
				{
					lurker->burrow();
				}
				
				if (lurker->isBurrowed())
				{
					Micro::SmartHoldPosition(lurker);
				}
			}
			continue;
		}
		*/
		// if the order is regroup and no lurkers are dead, move toward enemy position
		auto orderPosition = order.getPosition();
		if (order.getType() == SquadOrderTypes::Regroup &&
			BWAPI::Broodwar->self()->deadUnitCount(BWAPI::UnitTypes::Zerg_Lurker) == 0)
		{
			auto enemyMain = InformationManager::Instance().getEnemyMainBaseLocation();
			if (enemyMain)
			{
				orderPosition = enemyMain->Center();
			}
		}

		// if the order is to attack, defend or regroup
		if (order.getType() == SquadOrderTypes::Attack || 
			order.getType() == SquadOrderTypes::Defend ||
			order.getType() == SquadOrderTypes::Regroup)
		{

			// find the best target for this lurker
			BWAPI::Unit target = getTarget(lurker, LurkerTargets);

			// if there are targets
			if (target)
			{
				// If our target is a threat, burrow at max range.
				// If our target is safe, get close first.
				// Count a turret as a threat, but not a spore colony. Zerg has overlords.
				const bool isThreat =
					target->getType() == BWAPI::UnitTypes::Terran_Missile_Turret ||
					UnitUtil::CanAttackGround(target) && !target->getType().isWorker();

				int dist = lurker->getDistance(target);

				int lurkerRange = BWAPI::UnitTypes::Zerg_Lurker.groundWeapon().maxRange();
				
				if ((isThreat && dist <= lurkerRange) || dist <= 65)
				{
					// Burrow or stay burrowed.
					if (lurker->canBurrow())
					{
						lurker->burrow();
					}
					else if (lurker->isBurrowed())
					{
						Micro::SmartAttackUnit(lurker, target);
					}
				} 
				else if (!isThreat && dist > lurkerRange ||
					isThreat && dist > std::max(lurkerRange, 32 + UnitUtil::GetAttackRange(target, lurker)))
				{
					// Possibly unburrow and move.
					if (lurker->canUnburrow() &&
						/*lurker->getDistance(orderPosition) > 96 &&*/
						okToUnburrow(lurker))
					{
						lurker->unburrow();
					}
					else if (lurker->isBurrowed())
					{
						if (dist <= lurkerRange)
						{
							Micro::SmartAttackUnit(lurker, target);
						}
					}
					else
					{
						//Micro::SmartMove(lurker, target->getPosition());
						Micro::SmartMovePath(lurker, target->getPosition());
					}
				}
				else 
				{
					// In between "close enough to burrow" and "far enough to unburrow".
					// Keep doing whatever we were doing.
					if (lurker->isBurrowed())
					{
						if (dist <= lurkerRange)
						{
							Micro::SmartAttackUnit(lurker, target);
						}
					}
					else
					{
						//Micro::SmartMove(lurker, target->getPosition());
						Micro::SmartMovePath(lurker, target->getPosition());
					}
				}
			}
			// if there are no targets
			else
			{
				// if we're not near the order position
				if (lurker->getDistance(orderPosition) > 96)
				{
					if (lurker->canUnburrow()) 
					{
						if (okToUnburrow(lurker))
						{
							lurker->unburrow();
						}
					} 
					else 
					{
						//Micro::SmartMove(lurker, orderPosition);
						Micro::SmartMovePath(lurker, orderPosition);
					}
				}
				else
				{
					if (lurker->canBurrow())
					{
						lurker->burrow();
					}				
				}
			}
		}

		if (Config::Debug::DrawUnitTargetInfo)
		{
			BWAPI::Broodwar->drawLineMap(lurker->getPosition(), lurker->getTargetPosition(), BWAPI::Colors::Purple);
		}
	}
}

BWAPI::Unit LurkerManager::getTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets)
{
	if (targets.empty())
	{
		return nullptr;
	}

	int lurkerRange = BWAPI::UnitTypes::Zerg_Lurker.groundWeapon().maxRange();

	BWAPI::Unitset targetsInRange;
	for (auto & target : targets)
	{
		if (target->getDistance(lurker) <= lurkerRange)
		{
			targetsInRange.insert(target);
		}
	}

	// If we can shoot, choose the most distant target because it's more effective.
	if (lurker->isBurrowed() && !targetsInRange.empty())
	{
		return getFarthestTarget(lurker, targetsInRange);
	}

	// If any targets are in lurker range, then always return one of the targets in range.
	// The nearest one is the one we should approach.
	const BWAPI::Unitset & newTargets = targetsInRange.empty() ? targets : targetsInRange;
	return getNearestTarget(lurker, newTargets);
}

// get the attack priority of a type in relation to a zergling
int LurkerManager::getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target) const
{
	BWAPI::UnitType rangedType = rangedUnit->getType();
	BWAPI::UnitType targetType = target->getType();

	bool isThreat = rangedType.isFlyer() ? targetType.airWeapon() != BWAPI::WeaponTypes::None : targetType.groundWeapon() != BWAPI::WeaponTypes::None;

	if (target->getType().isWorker())
	{
		isThreat = false;
	}

	// Short circuit: Do not attack flying units or buildings
	if (target->isFlying() || target->isLifted())
	{
		return 0;
	}

	if (target->getType() == BWAPI::UnitTypes::Zerg_Larva || 
		target->getType() == BWAPI::UnitTypes::Zerg_Egg || 
		target->getType().isAddon())
	{
		return 0;
	}

	// if the target is building something near our base something is fishy
	BWAPI::Position ourBasePosition = BWAPI::Position(InformationManager::Instance().getMyMainBaseLocation()->Center());
	if (target->getType().isWorker() && 
		(target->isConstructing() || target->isRepairing()) && 
		target->getDistance(ourBasePosition) < 1200)
	{
		return 12;
	}
	if (target->getType().isBuilding() && 
		(target->isCompleted() || target->isBeingConstructed()) && 
		target->getDistance(ourBasePosition) < 1200)
	{
		return 12;
	}

	// highest priority is something that can attack us or aid in combat
	if (targetType == BWAPI::UnitTypes::Terran_Bunker || 
		targetType == BWAPI::UnitTypes::Protoss_Reaver ||
		isThreat)
	{
		return 12;
	}
	// next priority is turrets, spore and forge
	else if (targetType == BWAPI::UnitTypes::Terran_Missile_Turret ||
		targetType == BWAPI::UnitTypes::Zerg_Spore_Colony ||
		targetType == BWAPI::UnitTypes::Protoss_Forge)
	{
		return 10;
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
	// next is special buildings
	else if (targetType == BWAPI::UnitTypes::Zerg_Spawning_Pool)
	{
		return 5;
	}
	// next is special buildings
	else if (targetType == BWAPI::UnitTypes::Protoss_Pylon)
	{
		return 5;
	}
	// next is buildings that cost gas
	else if (targetType.gasPrice() > 0)
	{
		return 4;
	}
	else if (targetType.mineralPrice() > 0)
	{
		return 3;
	}
	// then everything else
	else
	{
		return 1;
	}
}

// Prefer the nearest target, as most units do.
BWAPI::Unit LurkerManager::getNearestTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets) const
{
	int highPriority = 0;
	int closestDist = INT_MAX;
	BWAPI::Unit bestTarget = nullptr;

	for (BWAPI::Unit target : targets)
	{
		int distance = lurker->getDistance(target);
		int priority = getAttackPriority(lurker, target);

		// BWAPI::Broodwar->drawTextMap(target->getPosition() + BWAPI::Position(20, -10), "%c%d", yellow, priority);

		if (priority > highPriority || priority == highPriority && distance < closestDist)
		{
			closestDist = distance;
			highPriority = priority;
			bestTarget = target;
		}
	}

	return bestTarget;
}

// Prefer the farthest target with the highest priority.
// The caller promises that all targets are in range, so all targets can be attacked.
// Choosing a distant target gives better odds of accidentally also hitting nearer targets,
// since nearby targets subtend a larger angle from the point of view of the lurker.
// It's a way to slightly improve lurker targeting without doing a full analysis.
BWAPI::Unit LurkerManager::getFarthestTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets) const
{
	int highPriority = 0;
	int farthestDist = -1;
	BWAPI::Unit bestTarget = nullptr;

	for (BWAPI::Unit target : targets)
	{
		int distance = lurker->getDistance(target);
		int priority = getAttackPriority(lurker, target);

		// BWAPI::Broodwar->drawTextMap(target->getPosition() + BWAPI::Position(20, -10), "%c%d", yellow, priority);

		if (priority > highPriority || priority == highPriority && distance > farthestDist)
		{
			farthestDist = distance;
			highPriority = priority;
			bestTarget = target;
		}
	}

	return bestTarget;
}

// An unseen enemy could kill the lurker if it unburrows now. That means either
// 1. An undetected cloaked unit like a dark templar, or
// 2. A known enemy in range but out of sight on high ground.
bool LurkerManager::hiddenEnemyInRange(BWAPI::Unit lurker) const
{
	if (UnitUtil::EnemyDetectorInRange(lurker))
	{
		// We're detected anyway, unburrowing won't make us more detected.
		return false;
	}

	for (const auto & kv : InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.goneFromLastPosition)
		{
			if (ui.unit && ui.unit->isVisible())
			{
				// The enemy unit is in sight.
				if (!ui.unit->isDetected() &&
					lurker->getDistance(ui.lastPosition) < 8 * 32)
				{
					//BWAPI::Broodwar->printf("lurker %d near cloaked %s", lurker->getID(), UnitTypeName(ui.type).c_str());
					return true;
				}
			}
			else
			{
				// The enemy unit is out of sight.
				int enemyRange = UnitUtil::GetAttackRangeAssumingUpgrades(ui.type, BWAPI::UnitTypes::Zerg_Lurker);
				if (enemyRange > 0 && lurker->getDistance(ui.lastPosition) <= enemyRange + 31)
				{
					//BWAPI::Broodwar->printf("lurker %d near unseen %s", lurker->getID(), UnitTypeName(ui.type).c_str());
					return true;
				}
			}
		}
	}

	return false;
}

// The lurker can probably unburrow without dying immediately or irradiating its friends.
// Though the answer will be wrong if we're surrounded by dragoons....
// Unburrow only at set intervals, to reduce the burrow-unburrow frenzy.
bool LurkerManager::okToUnburrow(BWAPI::Unit lurker) const
{
	return
		BWAPI::Broodwar->getFrameCount() % 200 == 0 &&
		lurker->getHitPoints() > 31 &&
		!lurker->isIrradiated() &&
		!hiddenEnemyInRange(lurker);
}
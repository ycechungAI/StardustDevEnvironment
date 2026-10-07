#include "MicroTanks.h"

#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// A target is "threatening" if it can attack tanks.
// NOTE All given targets are necessarily ground units.
int MicroTanks::nThreats(const BWAPI::Unitset & targets) const
{
    int n = 0;
    for (const BWAPI::Unit target : targets)
    {
        if (UnitUtil::CanAttackGround(target))
        {
            ++n;
        }
    }

    return n;
}

// A unit we should siege for even if there is only one of them.
bool MicroTanks::anySiegeUnits(const BWAPI::Unitset & targets) const
{
    for (const BWAPI::Unit target : targets)
    {
        if (target->getType() == BWAPI::UnitTypes::Terran_Bunker ||
            target->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
            target->getType() == BWAPI::UnitTypes::Protoss_Reaver ||
            target->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony)
        {
            return true;
        }
    }

    return false;
}

// Melee units or vultures or lurkers.
// Units you'd rather stay mobile against.
bool MicroTanks::allMeleeAndSameHeight(const BWAPI::Unitset & targets, BWAPI::Unit tank) const
{
    int height = GroundHeight(tank->getTilePosition());

    for (const BWAPI::Unit target : targets)
    {
        if (height != GroundHeight(target->getTilePosition()) ||
			UnitUtil::GetAttackRange(target, tank) > 32 &&
				target->getType() != BWAPI::UnitTypes::Terran_Vulture &&
				target->getType() != BWAPI::UnitTypes::Zerg_Lurker)
        {
            return false;
        }
    }

    return true;
}

bool MicroTanks::enemyTooClose(const BWAPI::Unitset & targets, BWAPI::Unit tank) const
{
	for (const BWAPI::Unit target : targets)
	{
		if (UnitUtil::CanAttackGround(target) && tank->getDistance(target) < SiegeTankMinRange)
		{
			return true;
		}
	}

	return false;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

MicroTanks::MicroTanks() 
{ 
}

void MicroTanks::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
    const BWAPI::Unitset tanks = Intersection(getUnits(), cluster.units);
    if (tanks.empty())
    {
        return;
    }

    if (!order->isCombatOrder())
    {
        return;
    }

    BWAPI::Unitset groundTargets;
    std::copy_if(targets.begin(), targets.end(), std::inserter(groundTargets, groundTargets.end()),
                 [](BWAPI::Unit u){ return !u->isFlying(); });
    
    // If there is 1 static defense building or reaver, we may want to siege.
    // Otherwise, if there are > 1 targets that can attack a tank, we may want to siege.
    const bool threatsExist = anySiegeUnits(targets) || nThreats(targets) > 1;

    for (BWAPI::Unit tank : tanks)
    {
        if (tank->getOrder() == BWAPI::Orders::Sieging || tank->getOrder() == BWAPI::Orders::Unsieging)
        {
            // The tank is busy and can't do anything.
            continue;
        }

        if (groundTargets.empty())
        {
            // There are no targets in sight.
            // Move toward the order position.
            if (tank->getDistance(order->getPosition()) > 100)
            {
                if (tank->canUnsiege())
                {
                    the.micro.Unsiege(tank);
                }
                else
                {
                    the.micro.Move(tank, order->getPosition());
                }
            }
        }
        else
        {
			// There are targets.
			// Find out which are in range if sieged or unsieged.
            BWAPI::Unit siegeTarget = getSiegeTarget(tank, targets);
			BWAPI::Unit tankTarget = getTankTarget(tank, targets);

            // Don't siege for single enemy units; this is included in threatsExist.
            // An unsieged tank will do nearly as much damage and can kite away.
            bool shouldSiege =
				siegeTarget && threatsExist;
            if (shouldSiege)
            {
                if (
                    // Don't siege to fight buildings, unless they can shoot back.
					siegeTarget && siegeTarget->getType().isBuilding() && !UnitUtil::CanAttackGround(siegeTarget)

					||

					// Don't siege for spider mines.
					siegeTarget && siegeTarget->getType() == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine

                    ||

                    // Don't siege if all targets are melee (or vultures or lurkers) and are at the same ground height.
                    allMeleeAndSameHeight(targets, tank)

					||

					// Don't siege if an enemy attacker is closer than our firing range.
					enemyTooClose(targets, tank)

					||

					// Don't siege in range of an enemy immobile unit (unless it's terran; have to fight enemy tanks).
					the.enemyRace() != BWAPI::Races::Terran && the.groundHitsFixed.at(tank->getTilePosition()) > 0

					||

					// Don't siege if we expect to be dead before we can fire a shot.
					// NOTE This assume a cooldown after sieging and ignores turret turning time.
					UnitUtil::ExpectedSurvivalTime(tank) <= SiegeTime + SiegedCooldown
                    )
                {
                    shouldSiege = false;
                }
            }

            // The targeting tries to assign a threat that is inside max and outside min range.
			bool shouldUnsiege;
			if (tank->isUnderDisruptionWeb())
			{
				shouldUnsiege = true;
			}
			else
			{
				// Be reluctant to unsiege. Having no target is not enough.
				// Don't unsiege if we expect to be dead before we can fire a shot.
				// Stay sieged anyway if we have plenty of comrades to fight with us.
				shouldUnsiege =
					!siegeTarget &&
					UnitUtil::ExpectedSurvivalTime(tank) > UnsiegeTime + UnsiegedCooldown &&
					cluster.groundDPF < 3.5;
			}

			if (tank->canSiege() && shouldSiege && !shouldUnsiege)
            {
                the.micro.Siege(tank);
            }
            else if (tank->canUnsiege() && shouldUnsiege)
            {
                the.micro.Unsiege(tank);
            }
			else if (siegeTarget && tank->isSieged())
			{
				the.micro.AttackUnit(tank, siegeTarget);
				if (Config::Debug::DrawUnitTargets)
				{
					BWAPI::Broodwar->drawLineMap(tank->getPosition(), siegeTarget->getPosition(), BWAPI::Colors::Purple);
				}
			}
			else if (tankTarget && !tank->isSieged())
			{
				the.micro.KiteTarget(tank, tankTarget);
				if (Config::Debug::DrawUnitTargets)
				{
					BWAPI::Broodwar->drawLineMap(tank->getPosition(), tankTarget->getPosition(), BWAPI::Colors::Purple);
				}
			}
        }
    }
}

BWAPI::Unit MicroTanks::getSiegeTarget(BWAPI::Unit tank, const BWAPI::Unitset & targets)
{
	int highPriority = 0;
	int closestDist = MAX_DISTANCE;
	BWAPI::Unit closestTarget = nullptr;

	// Choose, among the highest priority targets, the one which is the closest.
	for (BWAPI::Unit target : targets)
	{
		const int distance = tank->getDistance(target);
		const int priority = getAttackPriority(target, true);

		if (distance <= SiegeTankRange && distance >= SiegeTankMinRange)
		{
			if (priority > highPriority ||
				priority == highPriority && distance < closestDist)
			{
				closestDist = distance;
				highPriority = priority;
				closestTarget = target;
			}
		}
	}

    return closestTarget;
}

BWAPI::Unit MicroTanks::getTankTarget(BWAPI::Unit tank, const BWAPI::Unitset & targets)
{
	int highPriority = 0;
	int closestDist = MAX_DISTANCE;
	BWAPI::Unit closestTarget = nullptr;

	// Choose, among the highest priority targets, the one which is the closest.
	for (BWAPI::Unit target : targets)
	{
		const int distance = tank->getDistance(target);
		const int priority = getAttackPriority(target, false);

		if (distance <= TankRange)
		{
			if (priority > highPriority ||
				priority == highPriority && distance < closestDist)
			{
				closestDist = distance;
				highPriority = priority;
				closestTarget = target;
			}
		}
	}

	return closestTarget;
}

// Only targets that the tank can potentially attack go into the target set.
// toBeSieged is whether the tank is or will be sieged when the target is selected for firing on.
int MicroTanks::getAttackPriority(BWAPI::Unit target, bool toBeSieged)
{
	int key = getKeyTargetsPriority(target);
	if (key)
	{
		return key;
	}

	BWAPI::UnitType targetType = target->getType();

	if (target->getType() == BWAPI::UnitTypes::Zerg_Larva)
    {
        return 0;
    }
	if (target->getType() == BWAPI::UnitTypes::Zerg_Egg)
	{
		return 1;
	}

    // If it's under dark swarm, we can't hurt it unless we're sieged. And even then not if it's burrowed.
    if (target->isUnderDarkSwarm() &&
		!target->getType().isBuilding() &&
		(!toBeSieged || target->isBurrowed()))
    {
        return 0;
    }

    // if the target is building something near our base something is fishy
    BWAPI::Position ourBasePosition = BWAPI::Position(the.self()->getStartLocation());
    if (target->getType().isWorker() && (target->isConstructing() || target->isRepairing()) && target->getDistance(ourBasePosition) < 1200)
    {
        return 12;
    }

    if (target->getType().isBuilding() && target->getDistance(ourBasePosition) < 1200)
    {
        return 12;
    }

    // The most dangerous enemy units.
    if (targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
        targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
        targetType == BWAPI::UnitTypes::Protoss_High_Templar ||
        targetType == BWAPI::UnitTypes::Protoss_Reaver ||
        targetType == BWAPI::UnitTypes::Zerg_Infested_Terran ||
        targetType == BWAPI::UnitTypes::Zerg_Defiler ||
		targetType == BWAPI::UnitTypes::Zerg_Nydus_Canal)
    {
        return 12;
    }

	const bool isThreat = UnitUtil::TypeCanAttackGround(targetType) && !target->getType().isWorker();    // includes bunkers
    if (isThreat)
    {
        if (targetType.size() == BWAPI::UnitSizeTypes::Large)
        {
            return 11;
        }
        return 10;
    }

    return getBackstopAttackPriority(target);
}

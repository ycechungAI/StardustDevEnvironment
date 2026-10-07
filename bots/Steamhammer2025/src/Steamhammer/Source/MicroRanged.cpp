#include "MicroRanged.h"


#include "Bases.h"
#include "InformationManager.h"
#include "SpiderMineData.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// The unit's ranged ground weapon does splash damage, so it works under dark swarm.
// Firebats are not here: They are not ranged units.
// Tanks and lurkers are not here: They have their own micro managers.
bool MicroRanged::goodUnderDarkSwarm(BWAPI::UnitType type)
{
    return
        type == BWAPI::UnitTypes::Protoss_Archon ||
        type == BWAPI::UnitTypes::Protoss_Reaver;
}

// Cloaking costs 25 energy plus an ongoing drain. Allow enough energy over 25
// that it can stay cloaked for a useful duration.
// Don't uncloak. Too complicated to get right; just assume more fighting will happen.
void MicroRanged::maybeCloakWraiths(const BWAPI::Unitset & rangedUnits)
{
	if (!the.self()->hasResearched(BWAPI::TechTypes::Cloaking_Field))
	{
		return;
	}

	for (BWAPI::Unit u : rangedUnits)
	{
		if (u->getType() == BWAPI::UnitTypes::Terran_Wraith &&
			u->getEnergy() >= 50 &&
			!u->isCloaked() &&
			!the.info.getEnemyFireteam(u).empty() &&
			!UnitUtil::EnemyDetectorInRange(u) &&
			!u->isEnsnared() &&
			!u->isPlagued())
		{
			u->cloak();
		}
	}
}

// Vultures may want to lay mines.
void MicroRanged::maybeLayMines(const BWAPI::Unitset & rangedUnits)
{
	if (!the.self()->hasResearched(BWAPI::TechTypes::Spider_Mines))
	{
		return;
	}

	// Allow time for lay-mine commands to be processed before trying more.
	if (the.now() % (3 * (1 + BWAPI::Broodwar->getLatencyFrames())) != 0)
	{
		return;
	}

	// Clean out the _vulturesLaying set.
	for (auto it = _vulturesLaying.begin(); it != _vulturesLaying.end(); )
	{
		BWAPI::Unit v = *it;
		if (!v->exists() ||
			v->getSpiderMineCount() == 0 ||
			!rangedUnits.contains(v) ||
			// Has the mine been laid successfully?
			BWAPI::Broodwar->getClosestUnit(
				v->getPosition(),
				BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine && BWAPI::Filter::IsOwned,
				15)
			)
		{
			it = _vulturesLaying.erase(it);
		}
		else
		{
			++it;
		}
	}

	for (BWAPI::Unit u : rangedUnits)
	{
		if (u->getSpiderMineCount() > 0 &&
			!_vulturesLaying.contains(u) &&
			the.airHitsFixed.at(u->getTilePosition()) == 0)	// not in e.g. turret range
		{
			// Look in the immediate neighborhood for a good place to lay a mine.
			BWAPI::Position pos = BWAPI::Positions::None;
			SpiderMineData::MineLocation location = SpiderMineData::MineLocation::None;

			BWAPI::Unit myMine = BWAPI::Broodwar->getClosestUnit(
				u->getPosition(),
				BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine && BWAPI::Filter::IsOwned,
				2 * 32);
			BWAPI::Unit closestEnemy = BWAPI::Broodwar->getClosestUnit(
				u->getPosition(),
				BWAPI::Filter::IsEnemy && !BWAPI::Filter::IsWorker && !BWAPI::Filter::IsFlying &&
					BWAPI::Filter::GetType != BWAPI::UnitTypes::Terran_Vulture_Spider_Mine,
				8 * 32);
			if (closestEnemy &&
				(closestEnemy->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
				closestEnemy->getType() == BWAPI::UnitTypes::Protoss_Dragoon && u->getDistance(closestEnemy) < 4 * 32 ||
				closestEnemy->getType() == BWAPI::UnitTypes::Zerg_Lurker_Egg))
			{
				// If there are not already mines next to the enemy, lay one.
				BWAPI::Unitset mines = BWAPI::Broodwar->getUnitsInRadius(
					closestEnemy->getPosition(),
					2 * 32 + 16,
					BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine && BWAPI::Filter::IsOwned);
				if (mines.size() < 2)
				{
					pos = RawDistanceAndDirection(closestEnemy->getPosition(), u->getPosition(), 2 * 32);
					location = SpiderMineData::MineLocation::Combat;
				}
			}
			else if (!myMine && nullptr == BWAPI::Broodwar->getClosestUnit(
				u->getPosition(),
				BWAPI::Filter::IsBuilding && BWAPI::Filter::IsOwned,
				6 * 32))
			{
				// Lay a mine where ever we are, as long as it's not next to one of our own buildings.
				pos = u->getPosition();
				location = SpiderMineData::MineLocation::OnPath;
			}

			if (pos.isValid())
			{
				the.micro.LaySpiderMine(u, pos);
				_vulturesLaying.insert(u);
				the.mines.report(location, pos);
				//BWAPI::Broodwar->printf("dynamic mine vulture %d @ %d,%d", u->getID(), pos.x, pos.y);
			}
		}
	}
}

// -----------------------------------------------------------------------------------------

MicroRanged::MicroRanged()
{ 
}

void MicroRanged::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
    BWAPI::Unitset units = Intersection(getUnits(), cluster.units);
    if (units.empty())
    {
        return;
    }
	maybeCloakWraiths(units);
	maybeLayMines(units);
    assignTargets(units, targets);
}

void MicroRanged::assignTargets(const BWAPI::Unitset & rangedUnits, const BWAPI::Unitset & targets)
{
    // The set of potential targets.
    BWAPI::Unitset rangedUnitTargets;
    std::copy_if(targets.begin(), targets.end(), std::inserter(rangedUnitTargets, rangedUnitTargets.end()),
        [=](BWAPI::Unit u) {
        return
            u->getType() != BWAPI::UnitTypes::Zerg_Larva &&
            u->getType() != BWAPI::UnitTypes::Zerg_Egg &&
            !infestable(u);
    });

    // Figure out if the enemy is ready to attack ground or air.
    bool enemyHasAntiGround = false;
    bool enemyHasAntiAir = false;
    for (BWAPI::Unit target : rangedUnitTargets)
    {
        // If the enemy unit is retreating or whatever, it won't attack.
        if (UnitUtil::AttackOrder(target))
        {
            if (UnitUtil::CanAttackGround(target))
            {
                enemyHasAntiGround = true;
            }
            if (UnitUtil::CanAttackAir(target))
            {
                enemyHasAntiAir = true;
            }
        }
    }
    
    // Are any enemies in range to shoot at the ranged units?
    bool underThreat = order->isCombatOrder() && anyUnderThreat(rangedUnits);

    for (BWAPI::Unit rangedUnit : rangedUnits)
    {
        if (rangedUnit->isBurrowed())
        {
            // For now, it would burrow only if irradiated. Leave it.
            // Lurkers are controlled elsewhere.
            continue;
        }

		if (_vulturesLaying.contains(rangedUnit))
		{
			// The vulture is busy laying a mine.
			continue;
		}

		if (the.micro.fleeDT(rangedUnit))
        {
            // We fled from an undetected dark templar.
            continue;
        }

        if (order->isCombatOrder())
        {
            BWAPI::Unit target = getTarget(rangedUnit, rangedUnitTargets, underThreat);
            if (target)
            {
                if (Config::Debug::DrawUnitTargets)
                {
                    BWAPI::Broodwar->drawLineMap(rangedUnit->getPosition(), rangedUnit->getTargetPosition(), BWAPI::Colors::Purple);
                }

                bool kite = rangedUnit->isFlying() ? enemyHasAntiAir : enemyHasAntiGround;
                if (kite)
                {
                    the.micro.KiteTarget(rangedUnit, target);
                }
                else
                {
                    the.micro.CatchAndAttackUnit(rangedUnit, target);
                }
            }
            else
            {
                // No target found. If we're not near the order position, go there.
                if (rangedUnit->getDistance(order->getPosition()) > 100)
                {
                    the.micro.MoveNear(rangedUnit, order->getPosition());
                }
            }
        }
    }
}

// This can return null if no target is worth attacking.
// underThreat is true if any of the melee units is under immediate threat of attack.
BWAPI::Unit MicroRanged::getTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets, bool underThreat)
{
    int bestScore = INT_MIN;
    BWAPI::Unit bestTarget = nullptr;

    for (BWAPI::Unit target : targets)
    {
        // Skip targets under dark swarm that we can't hit.
        if (target->isUnderDarkSwarm() && !target->getType().isBuilding() && !goodUnderDarkSwarm(rangedUnit->getType()))
        {
            continue;
        }

        const int priority = getAttackPriority(rangedUnit, target);		// 0..12
        const int range = rangedUnit->getDistance(target);				// 0..map diameter in pixels
        const int closerToGoal =										// positive if target is closer than us to the goal
            rangedUnit->getDistance(order->getPosition()) - target->getDistance(order->getPosition());
        
        // Skip targets that are too far away to worry about--outside tank range.
        if (range >= 13 * 32)
        {
            continue;
        }

        // TODO disabled - seems to be wrong, skips targets it should not
        // Don't chase targets that we can't catch.
        //if (!CanCatchUnit(meleeUnit, target))
        //{
        //	continue;
        //}

        // Let's say that 1 priority step is worth 160 pixels (5 tiles).
        // We care about unit-target range and target-order position distance.
        int score = 5 * 32 * priority - range;

        // Adjust for special features.
        // A bonus for attacking enemies that are "in front".
        // It helps reduce distractions from moving toward the goal, the order position.
        if (closerToGoal > 0)
        {
            score += 2 * 32;
        }

        if (!underThreat)
        {
            // We're not under threat. Prefer to attack stuff outside enemy static defense range.
            if (rangedUnit->isFlying() ? !the.airHitsFixed.inRange(target) : !the.groundHitsFixed.inRange(target))
            {
                score += 4 * 32;
            }
        }

        const bool isThreat = UnitUtil::CanAttack(target, rangedUnit);   // may include workers as threats
        const bool canShootBack = isThreat && range <= 32 + UnitUtil::GetAttackRange(target, rangedUnit);

        if (isThreat)
        {
            if (canShootBack)
            {
                score += 7 * 32;
            }
            else if (rangedUnit->isInWeaponRange(target))
            {
                score += 5 * 32;
            }
            else
            {
                score += 5 * 32;
            }
        }
        else if (!target->isMoving())
        {
            if (target->isSieged() ||
                target->getOrder() == BWAPI::Orders::Sieging ||
                target->getOrder() == BWAPI::Orders::Unsieging ||
                target->isBurrowed())
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
        else if (target->getPlayer()->topSpeed(target->getType()) >= rangedUnit->getPlayer()->topSpeed(rangedUnit->getType()))
        {
            score -= 4 * 32;
        }
        
        // Prefer targets that are already hurt.
        if (target->getType().getRace() == BWAPI::Races::Protoss && target->getShields() <= 5)
        {
            score += 32;
        }
        if (target->getHitPoints() < target->getType().maxHitPoints())
        {
            score += 24;
        }

        // Prefer to hit air units that have acid spores on them from devourers.
        if (target->getAcidSporeCount() > 0)
        {
            // Especially if we're a mutalisk with a bounce attack.
            if (rangedUnit->getType() == BWAPI::UnitTypes::Zerg_Mutalisk)
            {
                score += 16 * target->getAcidSporeCount();
            }
            else
            {
                score += 8 * target->getAcidSporeCount();
            }
        }

        // Take the damage type into account.
        BWAPI::DamageType damage = UnitUtil::GetWeapon(rangedUnit, target).damageType();
        if (damage == BWAPI::DamageTypes::Explosive)
        {
            if (target->getType().size() == BWAPI::UnitSizeTypes::Large)
            {
                score += 48;
            }
        }
        else if (damage == BWAPI::DamageTypes::Concussive)
        {
            if (target->getType().size() == BWAPI::UnitSizeTypes::Small)
            {
                score += 48;
            }
            else if (target->getType().size() == BWAPI::UnitSizeTypes::Large)
            {
                score -= 48;
            }
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestTarget = target;
        }
    }

    return bestScore > 0 ? bestTarget : nullptr;
}

// How much do we want to attack this enenmy target?
int MicroRanged::getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target) 
{
	const BWAPI::UnitType rangedType = rangedUnit->getType();
	const BWAPI::UnitType targetType = target->getType();

	// Wraiths refuse to attack buildings except in special cases.
	if (rangedType == BWAPI::UnitTypes::Terran_Wraith &&
		!the.ops.wraithsAttackEnemyBuildings() &&
		targetType.isBuilding() &&
		targetType != BWAPI::UnitTypes::Terran_Missile_Turret &&
		(targetType != BWAPI::UnitTypes::Terran_Bunker || !the.info.isEnemyBunkerLoaded(target)) &&
		targetType != BWAPI::UnitTypes::Protoss_Photon_Cannon)
	{
		return 0;
	}

	int key = getKeyTargetsPriority(target);
	if (key)
	{
		return key;
	}

    if (rangedType == BWAPI::UnitTypes::Zerg_Guardian && target->isFlying())
    {
        // Can't target it.
        return 0;
    }

    // A carrier should not target an enemy interceptor.
    if (rangedType == BWAPI::UnitTypes::Protoss_Carrier && targetType == BWAPI::UnitTypes::Protoss_Interceptor)
    {
        return 0;
    }

    // if the target is building something near our base, something is fishy
    BWAPI::Position ourBasePosition = BWAPI::Position(the.bases.myMain()->getPosition());
    if (target->getDistance(ourBasePosition) < 1000) {
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
                return 10;
            }
            return 8;
        }
    }
    
    if (rangedType.isFlyer()) {
        // Exceptions if we're a flyer.
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

	// Short circuit: Give loaded bunkers a lower priority to reduce bunker obsession.
	if (targetType == BWAPI::UnitTypes::Terran_Bunker && the.info.isEnemyBunkerLoaded(target))
	{
		if (rangedType == BWAPI::UnitTypes::Terran_Wraith)
		{
			return 6;		// wraiths are too fragile to shoot at bunkers
		}
		return 9;
	}

	// Wraiths, scouts, and goliaths strongly prefer air targets because they do more damage to air units.
    if (rangedType == BWAPI::UnitTypes::Terran_Wraith ||
        rangedType == BWAPI::UnitTypes::Protoss_Scout ||
		rangedType == BWAPI::UnitTypes::Terran_Goliath)
    {
		if (targetType == BWAPI::UnitTypes::Protoss_Carrier)
		{
			return 12;
		}
		else if (BWAPI::UnitTypes::Protoss_Interceptor)
		{
			return 9;
		}
		else if (targetType.isFlyer())    // air units, not floating buildings
		{
			return 11;
		}
	}
	else if (rangedType == BWAPI::UnitTypes::Protoss_Dragoon)
	{
		if (targetType == BWAPI::UnitTypes::Protoss_Carrier)
		{
			return 12;
		}
		else if (BWAPI::UnitTypes::Protoss_Interceptor)
		{
			return 8;
		}
	}

    if (targetType == BWAPI::UnitTypes::Protoss_High_Templar ||
        targetType == BWAPI::UnitTypes::Zerg_Defiler)
    {
        return 12;
    }

    if (targetType == BWAPI::UnitTypes::Protoss_Reaver ||
        targetType == BWAPI::UnitTypes::Protoss_Arbiter ||
        targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
        targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
    {
        return 11;
    }

    // Threats can attack us. Exception: Workers are not threats.
    if (UnitUtil::CanAttack(targetType, rangedType) && !targetType.isWorker())
    {
        // Enemy unit which is far enough outside its range is lower priority than a worker.
        if (rangedUnit->getDistance(target) > 48 + UnitUtil::GetAttackRange(target, rangedUnit))
        {
            return 8;
        }
        return 10;
    }
    // Droppers are as bad as threats. They may be loaded and are often isolated and safer to attack.
    if (targetType == BWAPI::UnitTypes::Terran_Dropship ||
        targetType == BWAPI::UnitTypes::Protoss_Shuttle)
    {
        return 10;
    }
    // Also as bad are other dangerous things.
    if (targetType == BWAPI::UnitTypes::Zerg_Scourge)
    {
        return 10;
    }
    if (targetType == BWAPI::UnitTypes::Terran_Science_Vessel || 
		targetType == BWAPI::UnitTypes::Protoss_Observer)
    {
        // If we have cloaked units, mobile detectors are worse than threats.
        if (InformationManager::Instance().weHaveCloakTech())
        {
            return 11;
        }
        // Otherwise, they are equal.
        return 10;
    }
    // Next are workers.
    if (targetType.isWorker()) 
    {
        if (rangedUnit->getType() == BWAPI::UnitTypes::Terran_Vulture)
        {
            return 11;
        }
        // Repairing or blocking a choke makes you critical.
        if (target->isRepairing() || unitNearChokepoint(target))
        {
            return 11;
        }
        // SCVs constructing are also important.
        if (target->isConstructing())
        {
            return 10;
        }

        return 9;
    }

    // Important combat units that we may not have targeted above.
    if (targetType == BWAPI::UnitTypes::Protoss_Carrier)
    {
        return 8;
    }

    return getBackstopAttackPriority(target);
}

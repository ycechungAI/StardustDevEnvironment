#include "MicroManager.h"

#include "InformationManager.h"
#include "MapGrid.h"
#include "MapTools.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

CasterState::CasterState()
    : spell(CasterSpell::None)
    , lastEnergy(0)
    , lastCastFrame(0)
{
}

CasterState::CasterState(BWAPI::Unit caster)
    : spell(CasterSpell::None)
    , lastEnergy(caster->getEnergy())
    , lastCastFrame(0)
{
}

void CasterState::update(BWAPI::Unit caster)
{
    if (caster->getEnergy() < lastEnergy)
    {
        // We either cast the spell, or we were hit by EMP or feedback.
        // Whatever the case, we're not going to cast now.
        spell = CasterSpell::None;
        lastCastFrame = the.now();
        // BWAPI::Broodwar->printf("... spell complete");
    }
    lastEnergy = caster->getEnergy();
}

// Not enough time since the last spell.
bool CasterState::waitToCast() const
{
    return the.now() - lastCastFrame < framesBetweenCasts;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

MicroManager::MicroManager()
    : order(nullptr)
{
}

void MicroManager::setUnits(const BWAPI::Unitset & u) 
{ 
    _units = u;
}

void MicroManager::setOrder(const SquadOrder & inputOrder)
{
    order = &inputOrder;
}

void MicroManager::execute(const UnitCluster & cluster)
{
    // Nothing to do if we have no units.
    if (_units.empty())
    {
        return;
    }

    drawOrderText();

    // If we have no combat order (attack or defend), we're done.
    if (!order->isCombatOrder())
    {
        return;
    }

    // What the micro managers have available to shoot at.
    BWAPI::Unitset targets;

    if (order->getType() == SquadOrderTypes::DestroyNeutral)
    {
        // An order to destroy neutral ground units at a given location.
        for (BWAPI::Unit unit : BWAPI::Broodwar->getStaticNeutralUnits())
        {
            if (!unit->getType().canMove() &&
                !unit->isInvincible() &&
                !unit->isFlying() &&
                !unit->getType().isSpell() &&
                order->getPosition().getDistance(unit->getInitialPosition()) < 4.5 * 32)
            {
                targets.insert(unit);
            }
        }
        destroyNeutralTargets(targets);
    }
    else
    {
        // An order to fight enemies.
        // Units with different orders choose different targets.

        if (order->getType() == SquadOrderTypes::Hold ||
            order->getType() == SquadOrderTypes::Drop)
        {
            // Units near the order position.
            MapGrid::Instance().getUnits(targets, order->getPosition(), order->getRadius(), false, true);
        }
        else if (order->getType() == SquadOrderTypes::OmniAttack)
        {
            // All visible enemy units.
            // This is for when units are the goal, not a location.
            targets = BWAPI::Broodwar->enemy()->getUnits();
        }
        else
        {
            // For other orders: Units in sight of our cluster, plus sieged tanks in range.
            // Don't be distracted by distant units; move toward the goal.
			if (the.info.enemyHasSiegeMode())
			{
				targets = BWAPI::Broodwar->getUnitsInRadius(
					cluster.center,
					cluster.radius + 12 * 32 + 16,
					BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode &&
					BWAPI::Filter::GetPlayer == the.enemy()
				);
			}
			for (BWAPI::Unit unit : cluster.units)
            {
                // NOTE Ignores possible sight range upgrades. It's fine.
                MapGrid::Instance().getUnits(targets, unit->getPosition(), unit->getType().sightRange(), false, true);
            }

			// For zerg hydras, filter out valkyries and corsairs that do not threaten overlords.
			// If there are a lot of them, they threaten even protected overlords.
			if (the.selfRace() == BWAPI::Races::Zerg && !cluster.air && cluster.airDPF > 0 &&
				the.your.seen.count(BWAPI::UnitTypes::Terran_Valkyrie) <= 3 &&
				the.your.seen.count(BWAPI::UnitTypes::Protoss_Corsair) <= 6 &&
				the.info.allOverlordsProtected())
			{
				for (auto it = targets.begin(); it != targets.end(); )
				{
					if ((*it)->getType() == BWAPI::UnitTypes::Terran_Valkyrie ||
						(*it)->getType() == BWAPI::UnitTypes::Protoss_Corsair)
					{
						it = targets.erase(it);
					}
					else
					{
						++it;
					}
				}
			}
        }

        // Filter out targets that we definitely can't attack.
        for (auto it = targets.begin(); it != targets.end(); )
        {
            if ((*it)->isInvincible() ||
                (*it)->getType().isSpell() ||
                !(*it)->isVisible() ||
                !(*it)->isDetected())
            {
                it = targets.erase(it);
            }
            else
            {
                ++it;
            }
        }

        executeMicro(targets, cluster);
    }
}

// Attack priorities shared across micromanager subclasses.
// Return 0 to mean "no target recognized, continue with your regular checks."
int MicroManager::getKeyTargetsPriority(BWAPI::Unit target) const
{
	BWAPI::UnitType targetType = target->getType();

	// A ghost which is nuking is the highest priority by a mile.
	if (targetType == BWAPI::UnitTypes::Terran_Ghost &&
		(target->getOrder() == BWAPI::Orders::NukePaint || target->getOrder() == BWAPI::Orders::NukeTrack))
	{
		return 15;
	}

	// Special case for refineries.
	// A refinery in my base is a gas steal and is critical.
	// A refinery in an enemy base is important.
	// A refinery in a neutral base is a dead enemy base and does not matter at all.
	if (targetType == the.enemyRace().getRefinery())
	{
		BWAPI::Unit depot = BWAPI::Broodwar->getClosestUnit(
			target->getPosition(),
			BWAPI::Filter::IsResourceDepot,
			5 * 32
		);
		if (depot && depot->getPlayer() == the.enemy())
		{
			return 2;
		}
		if (depot && depot->getPlayer() == the.self())
		{
			return 10;
		}
		return 1;
	}

	return 0;
}

// Attack priorities for buildings and other mostly low-priority stuff are shared among some micromanagers.
int MicroManager::getBackstopAttackPriority(BWAPI::Unit target) const
{
    BWAPI::UnitType targetType = target->getType();

    // Nydus canal is critical.
    if (targetType == BWAPI::UnitTypes::Zerg_Nydus_Canal)
    {
        return 10;
    }

    // Spellcasters not previously mentioned are more important than key buildings.
    // Also remember to target other non-threat combat units.
    if (targetType.isSpellcaster() ||
        targetType.groundWeapon() != BWAPI::WeaponTypes::None ||
        targetType.airWeapon() != BWAPI::WeaponTypes::None)
    {
        return 7;
    }

    // Short circuit: Addons other than a completed comsat or silo are worth almost nothing.
    if (targetType.isAddon())
    {
        return 0;
    }

    // Short circuit: Downgrade unpowered production buildings and unfinished buildings, with exceptions.
	// Unpowered protoss tech buildings keep their higher priority.
    if (targetType.isBuilding() &&
        (!target->isCompleted() || (!target->isPowered() && UnitUtil::IsProductionBuildingType(targetType))) &&
        !targetType.isResourceDepot() &&
		!UnitUtil::IsStaticDefense(targetType))
    {
        return 2;
    }

    // Buildings come under attack during free time, so they can be split into more levels.
    if (targetType == BWAPI::UnitTypes::Protoss_Templar_Archives ||
		targetType == BWAPI::UnitTypes::Zerg_Spawning_Pool ||
        targetType == BWAPI::UnitTypes::Zerg_Spire ||
        targetType == BWAPI::UnitTypes::Zerg_Greater_Spire)
    {
        return 6;
    }
    if (targetType.isResourceDepot())
    {
        return 5;
    }
    if (targetType == BWAPI::UnitTypes::Protoss_Pylon)
    {
        return 4;
    }

    // Anything with a cost.
    if (targetType.gasPrice() > 0)
    {
        return 3;
    }
    if (targetType.mineralPrice() > 0)
    {
        return 2;
    }

    // Anything else that may exist.
    return 1;

}

// The order is DestroyNeutral. Carry it out.
void MicroManager::destroyNeutralTargets(const BWAPI::Unitset & targets)
{
    // Is any target in sight? We only need one.
    BWAPI::Unit visibleTarget = nullptr;
    for (BWAPI::Unit target : targets)
    {
        if (target->exists() &&
            target->isTargetable() &&
            target->isDetected())			// not e.g. a neutral egg under a neutral arbiter
        {
            visibleTarget = target;
            break;
        }
    }

    for (BWAPI::Unit unit : _units)
    {
        if (visibleTarget)
        {
            // We see a target, so we can issue attack orders to units that can attack.
            if (UnitUtil::CanAttackGround(unit) && unit->canAttack())
            {
                the.micro.CatchAndAttackUnit(unit, visibleTarget);
            }
            else if (unit->canMove())
            {
                the.micro.Move(unit, order->getPosition());
            }
        }
        else
        {
            // No visible targets. Move units toward the order position.
            if (unit->canMove())
            {
                the.micro.Move(unit, order->getPosition());
            }
        }
    }
}

const BWAPI::Unitset & MicroManager::getUnits() const
{ 
    return _units; 
}

// Unused but potentially useful.
bool MicroManager::containsType(BWAPI::UnitType type) const
{
    for (BWAPI::Unit unit : _units)
    {
        if (unit->getType() == type)
        {
            return true;
        }
    }
    return false;
}

void MicroManager::regroup(const BWAPI::Position & regroupPosition, const UnitCluster & cluster) const
{
    const int groundRegroupRadius = 96;
    const int airRegroupRadius = 8;			// air units stack and can be kept close together

    BWAPI::Unitset units = Intersection(getUnits(), cluster.units);

    for (BWAPI::Unit unit : units)
    {
		// 1. A reaver or guardian that is ready to fire and has a target should always fire. It's too slow to get away.
        // 2. A ground unit next to an undetected dark templar should try to flee the DT.
        // 3. A broodling should never retreat, but attack as long as it lives (not long).
        // 4. If none of its kind has died yet, a dark templar or lurker should not retreat.
        // 5. A ground unit next to an enemy sieged tank should not move away.
		BWAPI::Unit target = nullptr;
		if (unit->getType() == BWAPI::UnitTypes::Protoss_Reaver &&
			unit->getScarabCount() > 0 &&
			unit->getGroundWeaponCooldown() == 0)
		{
			target = unit->getClosestUnit(BWAPI::Filter::IsEnemy && !BWAPI::Filter::IsFlying, 8 * 32);
		}
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Guardian &&
			unit->getGroundWeaponCooldown() == 0)
		{
			target = unit->getClosestUnit(BWAPI::Filter::IsEnemy && !BWAPI::Filter::IsFlying, 7 * 32);
		}
		if (target)
		{
			the.micro.AttackUnit(unit, target);
		}
        else if (the.micro.fleeDT(unit))
        {
            // We're done for this frame.
        }
        else if (unit->getType() == BWAPI::UnitTypes::Zerg_Lurker && !UnitUtil::EnemyDetectorInRange(unit))
        {
            // Handle undetected lurkers as a special case.
            // Detected lurkers are handled in the regular cases below.
            if (unit->canBurrow())
            {
                the.micro.Burrow(unit);
            }
            // Otherwise it is burrowed and undetected, or busy burrowing, so we can leave it.
        }
        else if (unit->getType() == BWAPI::UnitTypes::Zerg_Broodling ||
				unit->getType() == BWAPI::UnitTypes::Protoss_Dark_Templar && safeDTAttack(unit) ||
				nextToSiegedTank(unit))
        {
            // In these special cases, the unit should not retreat.
            if (unit->getOrder() != BWAPI::Orders::AttackUnit)
            {
                the.micro.AttackMove(unit, order->getPosition());
            }
            // Otherwise keep the original order.
        }
        else if (!unit->isFlying() && unit->getDistance(regroupPosition) > groundRegroupRadius)   // air distance; can cause mistakes
        {
            // A ground unit away from the retreat point.
            // For ground units, figure out whether we have to fight our way to the retreat point.
            // Actually only handle the case of immobile units that will take time to retreat.
            bool mustFight = false;
            if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
                unit->getType() == BWAPI::UnitTypes::Zerg_Lurker && unit->isBurrowed())
            {
                if (unit->getOrder() == BWAPI::Orders::AttackUnit)
                {
                    int mobilizeTime = unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode
                        ? 63 + 12       // https://makingcomputerdothings.com/brood-war-api-the-comprehensive-guide-terran-units-continued-and-some-zerg-ones/
                        : 9;            // maximum standard unburrow time

                    mustFight = UnitUtil::ExpectedSurvivalTime(unit) < mobilizeTime + 12;
                }
            }
            if (!mustFight && unit->isUnderDarkSwarm())
            {
                BWAPI::Unitset enemies = BWAPI::Broodwar->getUnitsInRadius(
                    unit->getPosition(),
                    the.info.enemyHasSiegeMode() ? 12 * 32 : 8 * 32,
                    BWAPI::Filter::IsEnemy && !BWAPI::Filter::IsFlyer
                );
                // If no enemy can hit us under dark swarm, then stay there.
                // If there are no enemies near at all, mustFight remains false and we regroup. Should be rare.
                for (BWAPI::Unit enemy : enemies)
                {
                    mustFight = true;       // until proven otherwise
                    if (UnitUtil::HitsUnderSwarm(enemy))
                    {
                        mustFight = false;  // run away, we're not safe after all
                        break;
                    }
                }
            }

            // Handle all ground unit types, just as if the checks above were complete.
            if (mustFight)
            {
                if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
                {
                    // A sieged tank cannot attack-move. Leave it as is.
                }
                else if (unit->getType() == BWAPI::UnitTypes::Zerg_Lurker)
                {
                    // A lurker cannot attack-move. Burrow it, or leave it underground (micro.Burrow() does the check).
                    the.micro.Burrow(unit);
                }
                else
                {
                    the.micro.AttackMove(unit, regroupPosition);
                }
            }
            else if (!UnitUtil::MobilizeUnit(unit))
            {
                the.micro.Move(unit, regroupPosition);
            }
        }
        else if (unit->getType() == BWAPI::UnitTypes::Zerg_Scourge && unit->getDistance(regroupPosition) > groundRegroupRadius)
        {
            // Scourge away from the retreat point.
            // If the scourge is next to an enemy, attack anyway.
			BWAPI::Unit scourgeTarget = BWAPI::Broodwar->getClosestUnit(
				unit->getPosition(),
					BWAPI::Filter::IsEnemy && BWAPI::Filter::IsFlying && !BWAPI::Filter::IsBuilding && BWAPI::Filter::IsDetected &&
					BWAPI::Filter::GetType != BWAPI::UnitTypes::Protoss_Interceptor &&
					BWAPI::Filter::GetType != BWAPI::UnitTypes::Zerg_Overlord,
                3 * 32
            );
            if (scourgeTarget)
            {
                the.micro.AttackUnit(unit, scourgeTarget);
            }
            else
            {
                // Scourge is allowed to spread out more.
                the.micro.Move(unit, regroupPosition);
            }
        }
        else if (unit->isFlying() && unit->getDistance(regroupPosition) > airRegroupRadius)
        {
            // Other air units away from the retreat point.
            // 1. Flyers stack, so keep close. 2. Flyers are always mobile, no need to mobilize.
            the.micro.Move(unit, regroupPosition);
        }
        else
        {
            // We have retreated to a good position. Stay put.
            if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode || unit->getType() == BWAPI::UnitTypes::Zerg_Lurker)
            {
                (void) UnitUtil::ImmobilizeUnit(unit);
                // NOTE We don't want lurkers to hold position. Unlike other units, then they don't attack.
            }
            else
            {
                the.micro.HoldPosition(unit);
            }
        }
    }
}

// returns true if position:
// a) is walkable
// b) doesn't have buildings on it
// c) isn't blocked by an enemy unit that can attack ground
// NOTE Unused code, a candidate for throwing out.
bool MicroManager::checkPositionWalkable(BWAPI::Position pos) 
{
    // get x and y from the position
    int x(pos.x), y(pos.y);

    // If it's not walkable, throw it out.
    if (!BWAPI::Broodwar->isWalkable(x / 8, y / 8))
    {
        return false;
    }

    // for each of those units, if it's a building or an attacking enemy unit we don't want to go there
    for (BWAPI::Unit unit : BWAPI::Broodwar->getUnitsOnTile(x/32, y/32)) 
    {
        if	(unit->getType().isBuilding() ||
            unit->getType().isResourceContainer() || 
            !unit->isFlying() && unit->getPlayer() != BWAPI::Broodwar->self() && UnitUtil::CanAttackGround(unit)) 
        {		
            return false;
        }
    }

    // otherwise it's okay
    return true;
}

bool MicroManager::unitNearChokepoint(BWAPI::Unit unit) const
{
    UAB_ASSERT(unit, "bad unit");

    return the.tileRoom.at(unit->getTilePosition()) <= 12;
}

// Dodge any incoming spider mine.
// Return true if we took action.
bool MicroManager::dodgeMine(BWAPI::Unit u) const
{
    // TODO DISABLED - not good enough
    return false;

    const BWAPI::Unitset & attackers = InformationManager::Instance().getEnemyFireteam(u);

    // Find the closest kaboom. We react to that one and ignore any others.
    BWAPI::Unit closestMine = nullptr;
    int closestDist = MAX_DISTANCE;
    for (BWAPI::Unit attacker: attackers)
    {
        if (attacker->getType() == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine)
        {
            int dist = u->getDistance(attacker);
            if (dist < closestDist)
            {
                closestMine = attacker;
                closestDist = dist;
            }
        }
    }

    if (closestMine)
    {
        // First, try to drag the mine into an enemy.
        BWAPI::Unitset enemies = u->getUnitsInRadius(5 * 32, BWAPI::Filter::IsEnemy);
        BWAPI::Unit bestEnemy = nullptr;
        int bestEnemyScore = INT_MIN;
        for (BWAPI::Unit enemy : enemies)
        {
            int score = -u->getDistance(enemy);
            if (enemy->getType().isBuilding())
            {
                score -= 32;
            }
            if (enemy->getType() != BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
            {
                score -= 5;
            }
            if (score > bestEnemyScore)
            {
                bestEnemy = enemy;
                bestEnemyScore = score;
            }
        }
        if (bestEnemy)
        {
            BWAPI::Broodwar->printf("drag mine to enemy @ %d,%d", bestEnemy->getPosition().x, bestEnemy->getPosition().y);
            the.micro.Move(u, bestEnemy->getPosition());
            return true;
        }

        // Second, try to move away from our own units.
        BWAPI::Unit nearestFriend = u->getClosestUnit(BWAPI::Filter::IsOwned && !BWAPI::Filter::IsBuilding, 4 * 32);
        if (nearestFriend)
        {
            BWAPI::Position destination = DistanceAndDirection(u->getPosition(), nearestFriend->getPosition(), -4 * 32);
            BWAPI::Broodwar->printf("drag mine to %d,%d away from friends", destination.x, destination.y);
            the.micro.Move(u, destination);
            return true;
        }

        // Third, move directly away from the mine.
        BWAPI::Position destination = DistanceAndDirection(u->getPosition(), closestMine->getPosition(), -8 * 32);
        BWAPI::Broodwar->printf("move to %d,%d away from mine", destination.x, destination.y);
        the.micro.Move(u, destination);
        return true;
    }

    return false;
}

// Send the protoss unit to the shield battery and recharge its shields.
// The caller should have already checked all conditions.
// TODO shield batteries are not quite working
void MicroManager::useShieldBattery(BWAPI::Unit unit, BWAPI::Unit shieldBattery)
{
    if (unit->getDistance(shieldBattery) >= 32)
    {
        // BWAPI::Broodwar->printf("move to battery %d at %d", unit->getID(), shieldBattery->getID());
        the.micro.Move(unit, shieldBattery->getPosition());
    }
    else
    {
        // BWAPI::Broodwar->printf("recharge shields %d at %d", unit->getID(), shieldBattery->getID());
        the.micro.RightClick(unit, shieldBattery);
    }
}

// The decision is made. Move closer if necessary, then cast the spell.
// The target is a map position.
bool MicroManager::spell(BWAPI::Unit caster, BWAPI::TechType techType, BWAPI::Position target) const
{
    UAB_ASSERT(techType.targetsPosition() && target.isValid(), "can't target that");

    // Enough time since the last spell?
    // Forcing a delay prevents double-casting on the same target.
    auto it = _casterState.find(caster);
    if (it == _casterState.end() || (*it).second.waitToCast())
    {
        return false;
    }

    if (caster->getDistance(target) > techType.getWeapon().maxRange())
    {
        // We're out of range. Move closer.
        // BWAPI::Broodwar->printf("%s moving in...", UnitTypeName(caster).c_str());
        the.micro.Move(caster, target);
        return true;
    }
    else if (caster->canUseTech(techType, target))
    {
        // BWAPI::Broodwar->printf("%s!", techType.getName().c_str());
        return the.micro.UseTech(caster, techType, target);
    }

    return false;
}

// The decision is made. Move closer if necessary, then cast the spell.
// The target is a unit.
bool MicroManager::spell(BWAPI::Unit caster, BWAPI::TechType techType, BWAPI::Unit target) const
{
    UAB_ASSERT(techType.targetsUnit() && target->exists() && target->getPosition().isValid(), "can't target that");

    // Enough time since the last spell?
    // Forcing a delay prevents double-casting on the same target.
    auto it = _casterState.find(caster);
    if (it == _casterState.end() || (*it).second.waitToCast())
    {
        return false;
    }

    if (caster->getDistance(target) > techType.getWeapon().maxRange())
    {
        // We're out of range. Move closer.
        // BWAPI::Broodwar->printf("%s moving in...", UnitTypeName(caster).c_str());
        the.micro.Move(caster, target->getPosition());
        return true;
    }
    else if (caster->canUseTech(techType, target))
    {
        // BWAPI::Broodwar->printf("%s!", techType.getName().c_str());
        return the.micro.UseTech(caster, techType, target);
    }

    return false;
}

// A spell caster declares that it is ready to cast.
void MicroManager::setReadyToCast(BWAPI::Unit caster, CasterSpell spell)
{
    _casterState.at(caster).setSpell(spell);
}

// A spell caster declares that it is ready to cast.
void MicroManager::clearReadyToCast(BWAPI::Unit caster)
{
    _casterState.at(caster).setSpell(CasterSpell::None);
}

// Is it ready to cast? If so, don't interrupt it with another action.
bool MicroManager::isReadyToCast(BWAPI::Unit caster)
{
    return _casterState.at(caster).getSpell() != CasterSpell::None;
}

// Is it ready to cast a spell other than the given one? If so, don't interrupt it with another action.
bool MicroManager::isReadyToCastOtherThan(BWAPI::Unit caster, CasterSpell spellToAvoid)
{
    CasterSpell spell = _casterState.at(caster).getSpell();
    return spell != CasterSpell::None && spell != spellToAvoid;
}

// Update records for spells that have finished casting, and delete obsolete records.
// Called only by micro managers which control casters.
void MicroManager::updateCasters(const BWAPI::Unitset & casters)
{
    // Update the known casters.
    for (auto it = _casterState.begin(); it != _casterState.end();)
    {
        BWAPI::Unit caster = (*it).first;
        CasterState & state = (*it).second;

        if (caster->exists())
        {
            if (casters.contains(caster))
            {
                state.update(caster);
            }
            ++it;
        }
        else
        {
            // Delete records for units which are gone.
            it = _casterState.erase(it);
        }
    }

    // Add any new casters.
    for (BWAPI::Unit caster : casters)
    {
        auto it = _casterState.find(caster);
        if (it == _casterState.end())
        {
            _casterState.insert(std::pair<BWAPI::Unit, CasterState>(caster, CasterState(caster)));
        }
    }
}

// Is the potential target a command center that we have a queen close to?
// Some unit controllers decline to attack targets that could be infested instead.
bool MicroManager::infestable(BWAPI::Unit target) const
{
    return
        target->getType() == BWAPI::UnitTypes::Terran_Command_Center &&
        target->getHitPoints() < 750 &&
		target->isCompleted() &&
        BWAPI::Broodwar->getClosestUnit(
            target->getPosition(),
            BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Queen && BWAPI::Filter::IsOwned,
            10 * 32
        );
}

// The dark templar is not detected, and it has a target that is not detected.
// It should not retreat with the rest of its cluster.
bool MicroManager::safeDTAttack(BWAPI::Unit dt) const
{
	return
		!UnitUtil::EnemyDetectorInRange(dt) &&

		(!dt->getTarget() ||
		 !dt->getTarget()->getPosition().isValid() ||
		 !UnitUtil::EnemyDetectorInRange(dt->getTarget()->getPosition()));
}

// The attacker is right next to a sieged tank--too close for the tank to attack.
// It should not retreat with the rest of its cluster.
bool MicroManager::nextToSiegedTank(BWAPI::Unit attacker) const
{
	return
		the.enemyRace() == BWAPI::Races::Terran &&

		!attacker->isFlying() &&

		BWAPI::Broodwar->getClosestUnit(attacker->getPosition(),
			BWAPI::Filter::IsEnemy &&
			(BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
			 BWAPI::Filter::CurrentOrder == BWAPI::Orders::Sieging ||
			 BWAPI::Filter::CurrentOrder == BWAPI::Orders::Unsieging),
			 2 * 32);
}

void MicroManager::drawOrderText()
{
    if (Config::Debug::DrawUnitTargets)
    {
        for (BWAPI::Unit unit : _units)
        {
            BWAPI::Broodwar->drawTextMap(unit->getPosition().x, unit->getPosition().y, "%s", order->getStatus().c_str());
        }
    }
}

// Is the enemy threatening to shoot at any of our units in the set?
bool MicroManager::anyUnderThreat(const BWAPI::Unitset & units) const
{
    for (const BWAPI::Unit unit : units)
    {
        // Is static defense in range?
        if (unit->isFlying() ? the.airHitsFixed.inRange(unit) : the.groundHitsFixed.inRange(unit))
        {
            return true;
        }

        // Are enemy mobile units close and intending to shoot?
        for (BWAPI::Unit enemy : InformationManager::Instance().getEnemyFireteam(unit))
        {
            if (UnitUtil::IsSuicideUnit(enemy) ||
                unit->getDistance(enemy) < 32 + UnitUtil::GetAttackRange(enemy, unit))
            {
                return true;
            }
        }
    }
    return false;
}

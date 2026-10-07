#include "MicroIrradiated.h"

#include "Bases.h"
#include "The.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// If our lurker burrows here, can it hit an enemy?
bool MicroIrradiated::enemyInLurkerRange(BWAPI::Unit lurker) const
{
    BWAPI::Unit enemy = BWAPI::Broodwar->getClosestUnit(
        lurker->getPosition(),
        BWAPI::Filter::IsEnemy && !BWAPI::Filter::IsFlying,
        6 * 32
    );

    return enemy != nullptr;
}

// The nearest enemy that we can splash with our radiation.
// Visible enemies only. Unseen enemies may have moved.
BWAPI::Unit MicroIrradiated::nearestEnemy(BWAPI::Unit unit) const
{
    // Look farther if we are flying. Flyers are more likely to be able to get there.
    int dist = unit->isFlying() ? 14 * 32 : 10 * 32;

    // NOTE The enemy is terran, so we don't have to check all details.
	//      They have no burrowed organic units.
    //      A zerg building is organic but is unaffected by radiation. But if the enemy
    //      has a zerg building, then the enemy can't irradiate us.
    BWAPI::Unit enemy = BWAPI::Broodwar->getClosestUnit(
        unit->getPosition(),
        BWAPI::Filter::IsEnemy && BWAPI::Filter::IsOrganic,
        dist
    );

    return enemy;           // may be null
}

// Are we putting a nearby friendly unit at risk?
BWAPI::Unit MicroIrradiated::friendNearby(BWAPI::Unit unit) const
{
    BWAPI::Unit friendly = BWAPI::Broodwar->getClosestUnit(
        unit->getPosition(),
        BWAPI::Filter::IsOwned && BWAPI::Filter::IsOrganic && !BWAPI::Filter::IsBuilding && !BWAPI::Filter::IsBurrowed,
		DangerRange
    );

    return friendly;        // may be null
}

void MicroIrradiated::burrow(BWAPI::Unit unit)
{
    (void) the.micro.Burrow(unit);
}

// Try to expose enemy units to our irradiation splash. And also attack them, if possible.
// The enemy unit is never null.
// NOTE Better would be to stay near as many vulnerable enemies as possible.
void MicroIrradiated::runToEnemy(BWAPI::Unit unit, BWAPI::Unit enemy)
{
    if (unit->getDistance(enemy) < 2 * 32 && unit->canAttack(enemy))
    {
        // We're close enough to cause damage by irradiate splash. Attack.
		//UAB_MESSAGE("  run to enemy and attack");
		the.micro.AttackMove(unit, enemy->getPosition());
    }
    else
    {
		//UAB_MESSAGE("  run to enemy");
		the.micro.MoveNear(unit, enemy->getPosition());
    }
}

// Run away from any vulnerable friendly units. Burrow if possible.
// The friendly unit may be null.
void MicroIrradiated::runAway(BWAPI::Unit unit, BWAPI::Unit friendly)
{
    if (friendly)
    {
		if (unit->canBurrow())
        {
            // The fastest and most reliable escape, when available.
			//UAB_MESSAGE("  run away burrow");
			burrow(unit);
        }
        else
        {
            // We're in danger range of a friendly unit. Treat the friend as an enemy and flee.
			//UAB_MESSAGE("  run away flee friendly");
			the.micro.fleeEnemy(unit, friendly, DangerRange);
        }
    }
    else if (the.bases.enemyStart())
    {
		//UAB_MESSAGE("  run away to enemy base");
		// There is neither opportunity nor danger, so try to scout a little.
        the.micro.AttackMove(unit, the.bases.enemyStart()->getPosition());
    }
    else
    {
		//UAB_MESSAGE("  run away to the corner");
		// We got nothing. Just run for the corner. It should be rare.
        the.micro.AttackMove(unit, BWAPI::Positions::Origin);
    }
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

MicroIrradiated::MicroIrradiated()
{
}

void MicroIrradiated::update()
{
    for (BWAPI::Unit unit : getUnits())
    {
		//UAB_MESSAGE("handling irradiated %s", unit->getType().getName().c_str());
		
		if (unit->isBurrowed() || unit->getOrder() == BWAPI::Orders::Burrowing)
        {
            // Units that can burrow will die from irradiate. Stay underground.
            // NOTE This is safe, but slightly better play is possible.
			//UAB_MESSAGE("stay burrowed");
		}
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord && !unit->getLoadedUnits().empty())
		{
			// It's a loaded overlord. Unload before it dies.
			// TODO move to a place where unloading is possible!
			unit->unloadAll();
		}
        else if (unit->getType() == BWAPI::UnitTypes::Zerg_Lurker && enemyInLurkerRange(unit))
        {
			//UAB_MESSAGE("burrow 1");
			burrow(unit);
        }
        else if (BWAPI::Unit friendly = friendNearby(unit))
        {
            if (unit->canBurrow())
            {
				//UAB_MESSAGE("burrow 2");
				burrow(unit);
            }
            else if (BWAPI::Unit enemy = nearestEnemy(unit))
            {
				//UAB_MESSAGE("friend: run to enemy");
				runToEnemy(unit, enemy);
            }
            else
            {
				//UAB_MESSAGE("run away 1");
				runAway(unit, friendly);
            }
        }
        else if (BWAPI::Unit enemy = nearestEnemy(unit))
        {
			//UAB_MESSAGE("no friend: run to enemy");
			runToEnemy(unit, enemy);
        }
        else
        {
			//UAB_MESSAGE("run away 2");
			runAway(unit, nullptr);
        }
    }
}

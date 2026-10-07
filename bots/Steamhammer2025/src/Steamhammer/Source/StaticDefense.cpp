#include "StaticDefense.h"

#include "Bases.h"
#include "BuildingManager.h"
#include "Forecast.h"
#include "MacroAct.h"
#include "Maker.h"
#include "OpponentModel.h"
#include "ProductionManager.h"
#include "Random.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

StaticDefensePlan::StaticDefensePlan()
    : atInnerBases(0)
    , atOuterBases(0)
    , atFront(0)
	, turretBehindBunker(false)
	, turretBesideBunker(false)
	//, atProxy(0)
    , airIsPerBase(true)
	, airStrongpoint(true)
    , antiAir(0)
{
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// -- -- -- -- -- -- -- -- -- -- --
// Private methods.

// The defense plan has been filled in. Make sure it doesn't call for excess defense.
// This tries to limit defenses to what the economy can support, as a separate task
// to simplify the overall code.
void StaticDefense::limitZergDefenses()
{
    const int nBases = the.bases.completedBaseCount(the.self());
    if (nBases == 0)
    {
        // Save the effort. Also, don't carelessly divide by zero.
        return;
    }

    int nDrones = WorkerManager::Instance().getNumMineralWorkers();
    int limit;

    // Sunkens.
    _plan.atInnerBases = std::min(_plan.atInnerBases, nDrones / 12);

    if (nDrones >= 21)
    {
        limit = nDrones / 6;
    }
    else if (nDrones >= 10)
    {
        limit = 3;
    }
    else if (nDrones >= 7)
    {
        limit = 1;
    }
    else
    {
        limit = 0;
    }

    if (the.enemyRace() == BWAPI::Races::Terran)
    {
        if (the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) +
            the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) > 4 &&
            the.info.enemyHasSiegeMode())
        {
            // There are a lot of tanks. Sunkens won't help much.
            limit = std::min(limit, 4);
        }
        else
        {
            // Otherwise, limit sunkens versus terran to 6.
            limit = std::min(limit, 6);
        }
    }

	// Further limit: If the front base is the main and the natural is morphing,
	// and the enemy is not in the natural already, then make no sunken in the main.
	// NOTE This is not usually strict enough to prevent a useless sunken in the main.
	if (the.bases.myFront() == the.bases.myStart() &&
		the.bases.myNatural() &&
		the.bases.myNatural()->getDepot() &&
		!the.bases.myNatural()->getDepot()->isCompleted() &&
		!the.bases.myNatural()->bewareEnemyGroundAttackers() &&
		!the.info.enemyHasTransport()
		)
	{
		_plan.atFront = 0;
	}

    _plan.atOuterBases = std::min(_plan.atOuterBases, std::max(0, limit-1));
    _plan.atFront = std::min(_plan.atFront, limit);

    // Spores.
	// Always allow at least one, if the plan calls for any.
	// In ZvZ, always allow two if the drone count allows. Mutas are dangerous.
    if (_plan.airIsPerBase && _plan.antiAir > 0)
    {
        limit = std::max(1, nDrones / (7 * nBases));     // safe because nBases > 0
        if (the.enemyRace() == BWAPI::Races::Zerg)
        {
			if (nDrones >= 6)
			{
				limit = std::max(limit, 2);
			}
            limit = std::min(limit, 3);
        }
        _plan.antiAir = std::min(_plan.antiAir, limit);
    }
}

// Analyze static defense needs for TvX and PvX.
void StaticDefense::planTP()
{
	// -- ground defenses --

    _plan.atInnerBases = 0;
    _plan.atOuterBases = 0;
    _plan.atFront = 0;
    //_plan.atProxy = 0;

	// -- ground defenses --

	if (the.selfRace() == BWAPI::Races::Terran)
	{
		if (the.maker.isMaking(BWAPI::UnitTypes::Terran_Marine))
		{
			if (the.bases.myStart()->getNatural() && the.bases.myStart()->getNatural()->isMyBase() ||
				the.your.seen.count(BWAPI::UnitTypes::Terran_Vulture) > 0 ||
				the.your.seen.count(BWAPI::UnitTypes::Terran_Vulture_Spider_Mine) > 0 ||
				the.your.seen.count(BWAPI::UnitTypes::Zerg_Lurker) > 0 ||
				the.your.seen.count(BWAPI::UnitTypes::Zerg_Lurker_Egg) > 0 ||
				the.your.seen.count(BWAPI::UnitTypes::Zerg_Zergling) > the.my.completed.count(BWAPI::UnitTypes::Terran_Marine) + 4 * the.my.completed.count(BWAPI::UnitTypes::Terran_Vulture))
			{
				_plan.atFront = 1;

				// Turret build time plus a margin.
				const int t = the.now() + 450 + 5 * 24;

				if (the.enemyRace() == BWAPI::Races::Protoss)
				{
					if (the.your.ever.count(BWAPI::UnitTypes::Protoss_Dark_Templar) > 0 ||
						the.forecast.getEnemyUnitFrame(BWAPI::UnitTypes::Protoss_Dark_Templar) < t)
					{
						_plan.turretBehindBunker = true;
					}
				}
				else if (the.enemyRace() == BWAPI::Races::Zerg)
				{
					if (the.your.ever.count(BWAPI::UnitTypes::Zerg_Lurker) > 0 ||
						the.forecast.getEnemyUnitFrame(BWAPI::UnitTypes::Zerg_Lurker) < t)
					{
						_plan.turretBesideBunker = true;
					}
				}
			}
		}
	}
	else // protoss
    {
		if (the.your.seen.count(BWAPI::UnitTypes::Terran_Vulture) >= 6 ||
			the.your.seen.count(BWAPI::UnitTypes::Zerg_Lurker) > 0)
        {
            _plan.atOuterBases = 2;
        }
        else if (the.your.seen.count(BWAPI::UnitTypes::Terran_Vulture) > 0)
        {
            _plan.atOuterBases = 1;
        }
		if (the.info.enemyHasTransport())
		{
			_plan.atInnerBases = _plan.atOuterBases;
		}
    }

    // -- air defenses --

    _plan.airIsPerBase = true;

    // Count enemy air to ground power.
	int nWraiths = the.your.seen.count(BWAPI::UnitTypes::Terran_Wraith);

	// 5 wraiths can kill a turret with little trouble.
	// But bots don't do that, so don't worry until there are 6.
	int enemySpreadAir =
		(nWraiths <= 6 ? nWraiths : 0) +
		2 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Arbiter);

	int enemyAir =
		nWraiths +
		6 * the.your.seen.count(BWAPI::UnitTypes::Terran_Battlecruiser) +

        2 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Scout) +
        6 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Carrier) +
		2 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Arbiter) +

        2 * the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk) +
        4 * the.your.seen.count(BWAPI::UnitTypes::Zerg_Guardian);

    // Count my mobile anti-air power. Skip spellcasters.
    int myAntiAir =
            the.my.all.count(BWAPI::UnitTypes::Terran_Marine) +
        6 * the.my.all.count(BWAPI::UnitTypes::Terran_Goliath) +
        2 * the.my.all.count(BWAPI::UnitTypes::Terran_Wraith) +
        3 * the.my.all.count(BWAPI::UnitTypes::Terran_Valkyrie) +
        3 * the.my.all.count(BWAPI::UnitTypes::Terran_Battlecruiser) +		// lower value because it is slow
        
        2 * the.my.all.count(BWAPI::UnitTypes::Protoss_Dragoon) +
        4 * the.my.all.count(BWAPI::UnitTypes::Protoss_Archon) +
        2 * the.my.all.count(BWAPI::UnitTypes::Protoss_Corsair) +
        3 * the.my.all.count(BWAPI::UnitTypes::Protoss_Scout) +
        3 * the.my.all.count(BWAPI::UnitTypes::Protoss_Carrier);			// lower value because it is slow

	int minAir = 0;		// minimum air defense
	if (the.info.enemyHasTransport())
	{
		enemySpreadAir += 1;
		minAir = 2;
	}
	else if (enemyAir > 0 ||
		the.info.enemyHasMobileCloakTech() ||
		the.now() + 1200 > the.forecast.getStaticAirDefenseFrame())
	{
		minAir = 1;
	}

	// My mobile anti-air may be away from home, so count half of it.
    int antiAir = std::max(minAir, (enemyAir - myAntiAir/2) / 4);

    if (the.selfRace() == BWAPI::Races::Terran)
    {
		_plan.antiAir = std::min(4, antiAir);		// not more than this many turrets per base

		// Whether to spread out turrets or concentrate them.
		_plan.airStrongpoint = enemySpreadAir < 2 * enemyAir / 3;
	}
    else // protoss
    {
		_plan.antiAir = 0;

		antiAir = std::min(5, antiAir);				// not more than this many antiair cannons per base

		// Copy needed air defense into the ground plan.
		_plan.atInnerBases = std::max(antiAir, _plan.atInnerBases);
		_plan.atOuterBases = std::max(antiAir, _plan.atOuterBases);
		_plan.atFront = std::max(antiAir, _plan.atFront);
	}
}

// How many sunks do we need in total?
int StaticDefense::analyzeGroundDefensesZvT() const
{
    const int nLurkers = the.my.completed.count(BWAPI::UnitTypes::Zerg_Lurker);

    // Lurkers multiply, up to a limit.
    const int efficientLurkers = std::min(8, nLurkers);
    int myPower = 2 * (efficientLurkers * efficientLurkers + nLurkers - efficientLurkers);

    // Other units add. Let's pretend they do, anyway.
    for (BWAPI::Unit u : the.self()->getUnits())
    {
        if (!u->getType().isBuilding() && !u->getType().isWorker() &&
            UnitUtil::TypeCanAttackGround(u->getType()) &&
            u->getType() != BWAPI::UnitTypes::Zerg_Lurker)
        {
            myPower += u->getType().supplyRequired();
        }
    }

    // Units that are weak against sunkens. They add.
    int yourPower =
        the.your.seen.count(BWAPI::UnitTypes::Terran_Firebat) +
        the.your.seen.count(BWAPI::UnitTypes::Terran_Vulture) +
        the.your.seen.count(BWAPI::UnitTypes::Terran_Ghost);

    // Other units multiply, up to a limit.

    // No point trying to stop mass tanks with sunkens.
    const int nTanks = std::min(4,
        the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) +
        the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode));
    yourPower += 4 * nTanks * nTanks;

    // Goliaths.
    const int nGoliaths = the.your.seen.count(BWAPI::UnitTypes::Terran_Goliath);
    const int efficientGoliaths = std::min(8, nGoliaths);
    yourPower += efficientGoliaths * efficientGoliaths + nGoliaths - efficientGoliaths;

    // Marines and medics.
    const int nMarines = the.your.seen.count(BWAPI::UnitTypes::Terran_Marine);
    const int nMedics = the.your.seen.count(BWAPI::UnitTypes::Terran_Medic);
    const int efficientMedics = std::min(nMedics, nMarines / 4);
    yourPower += efficientMedics * efficientMedics + nMarines - efficientMedics;

	// The minimum provides a little protection against surprise attacks.
	int minimum = (yourPower >= 8 && the.bases.myNatural() && the.bases.myNatural()->getOwner() == the.self()) ? 1 : 0;
    return  std::max(minimum, (yourPower - myPower) / 4);
}

// How many sunks do we need in total?
int StaticDefense::analyzeGroundDefensesZvP() const
{
    int myPower = 0;
    for (BWAPI::Unit u : the.self()->getUnits())
    {
        if (!u->getType().isBuilding() && !u->getType().isWorker() &&
            UnitUtil::TypeCanAttackGround(u->getType()))
        {
            myPower += u->getType().supplyRequired();
        }
    }

    // Protoss ground units with physical attacks.
    int yourPower =
        the.your.seen.count(BWAPI::UnitTypes::Protoss_Zealot) * 4 +
        the.your.seen.count(BWAPI::UnitTypes::Protoss_Dragoon) * 5 +                // can kite out of range
        the.your.seen.count(BWAPI::UnitTypes::Protoss_Archon) * 6 +                 // weak vs sunkens
        the.your.seen.count(BWAPI::UnitTypes::Protoss_Dark_Templar) * 4 +
        std::min(2, the.your.seen.count(BWAPI::UnitTypes::Protoss_Reaver)) * 8;     // sunks don't stop mass reavers

    return std::max(0, (yourPower - myPower) / 10);
}

// How many sunks do we need at outer bases?
// Account for likely midgame units only. Other stuff can take care of itself.
int StaticDefense::analyzeGroundDefensesZvZ() const
{
	const int hatches =
		the.your.seen.count(BWAPI::UnitTypes::Zerg_Hatchery) -
		the.my.all.count(BWAPI::UnitTypes::Zerg_Hatchery);

    const int myPower =
        the.my.all.count(BWAPI::UnitTypes::Zerg_Zergling) +
        the.my.all.count(BWAPI::UnitTypes::Zerg_Mutalisk) / 2 - the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk) / 4;

    const int yourPower =
        the.your.seen.count(BWAPI::UnitTypes::Zerg_Zergling) +
        2 * the.your.seen.count(BWAPI::UnitTypes::Zerg_Hydralisk);

    return std::max(0, hatches + (yourPower - myPower) / 6);
}

// Analyze static defense needs for ZvT ZvP ZvZ.
void StaticDefense::planZ()
{
    int nBases = the.bases.completedBaseCount(the.self());

    if (the.enemyRace() == BWAPI::Races::Terran)
    {
		// ZvT
        int enemyAirToAir =
            the.your.seen.count(BWAPI::UnitTypes::Terran_Wraith) +
            the.your.seen.count(BWAPI::UnitTypes::Terran_Valkyrie);
        const int enemyAirToGround =
            the.your.seen.count(BWAPI::UnitTypes::Terran_Wraith) +
            4 * the.your.seen.count(BWAPI::UnitTypes::Terran_Battlecruiser);

		// If we've seen no surprising air units yet but have seen two starports,
		// fear two port wraith.
		if (enemyAirToAir == 0 && enemyAirToGround == 0 &&
			the.your.seen.count(BWAPI::UnitTypes::Terran_Science_Vessel) == 0 &&
			the.your.seen.count(BWAPI::UnitTypes::Terran_Starport) >= 2)
		{
			enemyAirToAir = 1;
		}

        // Assume vultures will keep coming.
        const int vultureDefense =
            (the.your.ever.count(BWAPI::UnitTypes::Terran_Vulture) > 0) ? 1 : 0;

         // Assume dropships may give up.
        _plan.atInnerBases =
            (the.your.seen.count(BWAPI::UnitTypes::Terran_Dropship) > 0) ? 1 : 0;
        if (Config::Skills::HumanOpponent)
        {
            // Humans attack weak bases first, so be strong everywhere.
            _plan.atOuterBases =        // includes the front base
                std::max(vultureDefense, analyzeGroundDefensesZvT());
            _plan.atFront = _plan.atOuterBases;
        }
        else
        {
            // Bots usually attack the front line and ignore other bases.
            _plan.atOuterBases = vultureDefense;
            _plan.atFront = std::max(vultureDefense, analyzeGroundDefensesZvT());
        }

		if (enemyAirToAir == 0)
		{
			// Predict the appearance of enemy air-to-air power in time to prepare for it.
			// "No prediction" comes out the same as "predicted to never happen". Both are MAX_FRAME.

			// Build times: Evo 600, creep 300, spore 300. It adds up to 1200.
			if (the.now() + 1200 > the.forecast.getStaticAirDefenseFrame())
			{
				enemyAirToAir = 1;
			}
		}
		
		_plan.airIsPerBase = false;
        _plan.antiAir = 0;
        if (enemyAirToGround >= 7)
        {
            // Mass wraiths or battlecruisers.
            _plan.airIsPerBase = true;
            _plan.antiAir = std::min(3, enemyAirToGround / 3);
        }
        else if (nBases > 1 && enemyAirToAir >= 3 && (the.my.completed.count(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0 || enemyAirToAir >= 6))
        {
            _plan.antiAir = 2;
        }
        else if (enemyAirToAir > 0 && (the.my.completed.count(BWAPI::UnitTypes::Zerg_Spire) == 0 || enemyAirToAir >= 3))
        {
            _plan.antiAir = 1;
        }
    }
    else if (the.enemyRace() == BWAPI::Races::Protoss)
    {
		// ZvP
        _plan.atInnerBases =
            // Vs human: Shuttle means drop on bases.
            // Vs bot: If there was once a shuttle and reaver and there's still a shuttle, expect another reaver.
            (the.your.seen.count(BWAPI::UnitTypes::Protoss_Shuttle) > 0 &&
            (Config::Skills::HumanOpponent || the.your.ever.count(BWAPI::UnitTypes::Protoss_Reaver) > 0))
            ? 1 : 0;
        if (Config::Skills::HumanOpponent)
        {
            // Humans attack weak bases first, so be strong everywhere.
            _plan.atOuterBases =
                std::max(the.your.ever.count(BWAPI::UnitTypes::Protoss_Dark_Templar) > 0 ? 1 : 0, analyzeGroundDefensesZvP());
            _plan.atFront = _plan.atOuterBases;
        }
        else
        {
            // Bots usually attack the front line and ignore other bases.
            _plan.atFront = analyzeGroundDefensesZvP();
            _plan.atOuterBases =
                (_plan.atFront > 0 || the.your.ever.count(BWAPI::UnitTypes::Protoss_Dark_Templar) > 0) ? 1 : 0;
        }

		// Don't count carriers or arbiters.
        int enemyAirToAir =
            the.your.seen.count(BWAPI::UnitTypes::Protoss_Corsair) +
            3 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Scout);
        int enemyAirToGround =
            the.your.seen.count(BWAPI::UnitTypes::Protoss_Scout);

		if (enemyAirToAir == 0)
		{
			// Predict the appearance of enemy air-to-air power in time to prepare for it.
			// "No prediction" comes out the same as "predicted to never happen". Both are MAX_FRAME.

			// Build times: Evo 600, creep 300, spore 300. It adds up to 1200.
			if (the.your.seen.count(BWAPI::UnitTypes::Protoss_Stargate) > 0 ||
				the.now() + 1200 > the.forecast.getStaticAirDefenseFrame())
			{
				enemyAirToAir = 1;
			}
		}

        _plan.airIsPerBase = false;
        _plan.antiAir = 0;
        if (enemyAirToGround >= 5)
        {
            // Mass scouts. Ouch.
            _plan.airIsPerBase = true;
            _plan.antiAir = enemyAirToGround / 4;
        }
        else if (nBases >= 3 && enemyAirToAir >= 10)
        {
            _plan.antiAir = 3;
        }
        else if (nBases >= 2 && (enemyAirToAir >= 3 && the.my.completed.count(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0 || enemyAirToAir >= 6))
        {
            _plan.antiAir = 2;
        }
        else if (enemyAirToAir > 0 && the.my.completed.count(BWAPI::UnitTypes::Zerg_Spire) == 0 || enemyAirToAir >= 3)
        {
            _plan.antiAir = 1;
        }
    }
    else
    {
        // ZvZ

        _plan.atInnerBases = 0;
        _plan.atOuterBases = analyzeGroundDefensesZvZ();
        _plan.atFront = _plan.atOuterBases;

        _plan.airIsPerBase = true;
        _plan.antiAir = 0;

		if (the.info.enemyGasTiming() > 0 || !the.enemyBaseUnderSurveillance())       // enemy has been seen to use gas, or we don't know
        {
			int nMutas = the.my.all.count(BWAPI::UnitTypes::Zerg_Mutalisk);
			int enemyMutas = the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk);

			if (enemyMutas > nMutas + 6)
            {
                if (the.my.completed.count(BWAPI::UnitTypes::Zerg_Spire) > 0)
                {
                    _plan.antiAir = 2;
                }
                else
                {
                    // Spire destroyed or never made.
                    _plan.antiAir = 2 + (enemyMutas - nMutas) / 6;
                }
            }
			else
			{
				// Build times: Evo 600, creep 300, spore 300. It adds up to 1200.
				if (enemyMutas == 0 && the.my.all.count(BWAPI::UnitTypes::Zerg_Spire) == 0 && !the.enemyBaseUnderSurveillance())
				{
					// Predict the appearance of enemy air power in time to prepare for it.
					// "No prediction" comes out the same as "predicted to never happen". Both are MAX_FRAME.

					if (the.now() + 1200 > the.forecast.getStaticAirDefenseFrame())
					{
						enemyMutas = 1;
					}
				}

				// When did, or will, the enemy spire finish? If before now, pretend it is now.
				// If we have seen no enemy spire (completed or not), then the enemy timing will be MAX_FRAME.
				const int enemySpireTime = std::max(the.now(), the.info.getEnemyBuildingTiming(BWAPI::UnitTypes::Zerg_Spire));

				if (enemyMutas > nMutas || the.info.getMySpireTiming() - 30 * 24 > enemySpireTime)
				{
					//BWAPI::Broodwar->printf("our spire %d versus theirs %d", the.info.getMySpireTiming(), enemySpireTime);
					_plan.antiAir = enemyMutas > 1 ? 2 : 1;
				}
			}
        }
    }
}

// Analyze the situation and decide what static defense is needed where.
void StaticDefense::plan()
{
    if (the.enemyRace() == BWAPI::Races::Unknown)
    {
        // No defense needed until we've seen some enemy units.
        return;
    }

    if (the.selfRace() == BWAPI::Races::Zerg)
    {
        planZ();
        limitZergDefenses();
    }
    else
    {
        planTP();
    }
}

void StaticDefense::startBuilding(BWAPI::UnitType building, const BWAPI::TilePosition & tile) const
{
    BuildOrderQueue & queue = the.production.getQueue();
	BWAPI::UnitType workerType = building.getRace().getWorker();

	// If we need more workers, according to the hardcoded limit, add one as the second queue item.
	// NOTE Zerg will use up a drone, so that its worker count stays the same.
	if (the.my.all.count(workerType) < _MinWorkerLimit &&
		queue.getNextUnit() != workerType)
	{
		queue.queueAsHighestPriority(workerType);
	}

	// The building becomes the first queue item.
    queue.queueAsHighestPriority(MacroAct(building, tile));
}

// The building manager sometimes forgets about sunkens and spores while building them, and
// leaves unmorphed creep colonies. If we still want the sunk or spore, morph it.
// For spores, this only works when _plan.airIsPerBase is true.
bool StaticDefense::morphStrandedCreeps(BWAPI::UnitType type)
{
    if (the.selfRace() != BWAPI::Races::Zerg)
    {
        return false;
    }

    // Ensure that no creep colonies are under the building manager's control. We don't want to steal them.
    if (BuildingManager::Instance().isBeingBuilt(_ground) || BuildingManager::Instance().isBeingBuilt(_air))
    {
        return false;
    }

    bool any = false;

    // We simultaneously morph up to one creep per base.
    for (Base * base : the.bases.getAll())
    {
        int nCreeps = base->getNumUnits(BWAPI::UnitTypes::Zerg_Creep_Colony);
        if (base->isMyCompletedBase() && nCreeps > 0)
        {
            int nUnits = base->getNumUnits(type);
            bool gotOne;
            if (type == _ground)
            {
                gotOne = 
                    base == the.bases.myFront() && nUnits < _plan.atFront ||
                    !base->isInnerBase() && nUnits < _plan.atOuterBases ||
                    base->isInnerBase() && nUnits < _plan.atInnerBases;
            }
            else // type == _air
            {
                gotOne = _plan.airIsPerBase && nUnits < _plan.antiAir;
            }

            if (gotOne)
            {
                // We want to morph a creep. Is there one to morph?
                BWAPI::Unit creep = BWAPI::Broodwar->getClosestUnit(
                    base->getCenter(),
                    BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Creep_Colony && BWAPI::Filter::IsOwned && BWAPI::Filter::IsCompleted,
                    8 * 32
                );
                if (creep)
                {
                    the.micro.Make(creep, type);
                }
            }
        }
    }

    return any;
}

// Can we build at this base without wasting our resources?
bool StaticDefense::isSafelyBuildable(Base * base) const
{
	return
		base->isMyCompletedBase() &&
		!base->isDoomed() &&
		(the.selfRace() != BWAPI::Races::Protoss || base->getPylon() && base->getPylon()->isCompleted());
}

// Urgently start all ground defenses needed at the front base.
bool StaticDefense::startFrontGroundBuildings()
{
    Base * front = the.bases.myFront();

	// If the front base is the natural and it's doomed, make it the main.
	if (the.bases.myStart()->getOwner() == the.self() &&
		front == the.bases.myStart()->getNatural() &&
		front->isDoomed())
	{
		front = the.bases.myStart();
	}

    if (!front || !isSafelyBuildable(front))
    {
        return false;
    }

    int nActual = front->getNumUnits(_ground);
    if (_ground == BWAPI::UnitTypes::Zerg_Sunken_Colony)
    {
        // For sunkens, also count creeps, which will most often turn into sunkens.
        // NOTE We do this only for front base defenses. Others are OK because they are built more slowly.
        nActual += front->getNumUnits(BWAPI::UnitTypes::Zerg_Creep_Colony);
    }
    // Also count queued buildings, without checking where they are set to be built.
    nActual += the.production.getQueue().numInNextN(_ground, 2 * _plan.atFront);

    int nNeeded = _plan.atFront - nActual;      // new buildings to start
    if (nNeeded <= 0)
    {
        return false;
    }

    //BWAPI::Broodwar->printf("%d needed at front: plan %d - existing %d", nNeeded, _plan.atFront, nActual);

    while (nNeeded > 0)
    {
        startBuilding(_ground, front->getFrontTile());
        --nNeeded;
    }

    return true;
}

bool StaticDefense::bunkerNeedsTurret()
{
	if (the.selfRace() != BWAPI::Races::Terran ||
		!_plan.turretBehindBunker && !_plan.turretBesideBunker)
	{
		return false;
	}

	// A turret is already set for production.
	if (the.production.getQueue().getNextUnit() == BWAPI::UnitTypes::Terran_Missile_Turret ||
		BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Missile_Turret))
	{
		return false;
	}

	// Locate the bunkers that need turrets.
	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (u->getType() == BWAPI::UnitTypes::Terran_Bunker &&
			nullptr == BWAPI::Broodwar->getClosestUnit(
				u->getPosition(),
				BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Missile_Turret && BWAPI::Filter::IsOwned,
				3 * 32
			))
		{
			startBuilding(BWAPI::UnitTypes::Terran_Missile_Turret, u->getTilePosition());
		}
	}

	return false;
}

// Start ground defenses needed at bases other than the front base.
// As zerg, build defenses one at a time. They're powerful and expensive.
// As other races, build one at each relevant base in this round.
// Don't build more at a base than there are workers there, so that new bases don't overdo it.
bool StaticDefense::startOtherGroundBuildings()
{
	bool isZerg = the.selfRace() == BWAPI::Races::Zerg;
	bool any = false;

    for (Base * base : the.bases.getAll())
    {
        if (isSafelyBuildable(base) &&
            (!isZerg || base->getNumUnits(BWAPI::UnitTypes::Zerg_Creep_Colony) == 0))
        {
			int nUnits = base->getNumUnits(_ground) +
				the.production.getQueue().numInNextN(_ground, base->isInnerBase() ? _plan.atInnerBases : _plan.atOuterBases);
			if (base == the.bases.myFront())
            {
                // Handled by startFrontGroundBuildings().
            }
            else if (!base->isInnerBase())  // outer base
            {
                if (nUnits < _plan.atOuterBases && nUnits < base->getNumWorkers())
                {
                    startBuilding(_ground, base->getTilePosition());
                    any = true;
					if (isZerg)
					{
						break;
					}
                }
            }
            else if (base->isInnerBase())
            {
                if (nUnits < _plan.atInnerBases && nUnits < base->getNumWorkers())
                {
                    startBuilding(_ground, base->getTilePosition());
					any = true;
					if (isZerg)
					{
						break;
					}
				}
            }
        }
    }

    return any;
}

bool StaticDefense::buildGround()
{
	if (_plan.atInnerBases + _plan.atOuterBases + _plan.atFront == 0)
    {
        return false;
    }

    // Build barracks/forge/pool if needed.
    // Protoss normally already has a pylon, created by a different mechanism.
    if (needPrerequisite(_groundPrereq)) 
    {
		return true;
    }

    // Morph creep colonies that the building manager forgot about, if needed.
    if (morphStrandedCreeps(_ground))
    {
        return true;
    }

    // Build defenses on the front line as fast as possible.
    if (startFrontGroundBuildings())
    {
        return true;
    }

    // Build defenses elsewhere more slowly.
    if (alreadyBuilding(_ground))
    {
        return true;
    }

	if (bunkerNeedsTurret())
	{
		return true;
	}

    return startOtherGroundBuildings();
}

// We're placing one air defense building at a each of limited number of bases. Choose them.
// In practice, only zerg does this, to provide safe havens for overlords.
// First anti-air: vT and vZ, closest base to enemy; vP, natural.
// Second anti-air in the remaining base of main/natural.
// Any remaining in remaining bases.
void StaticDefense::chooseAirBases()
{
    // Figure out the first two bases.
    const bool hasNatural =
        the.bases.myNatural() &&
        the.bases.myNatural()->isMyCompletedBase();
    Base * base1;
    Base * base2;
    if (the.enemyRace() == BWAPI::Races::Protoss)
    {
        // Natural first vP, to help defend against dark templar.
        base1 = hasNatural ? the.bases.myNatural() : the.bases.myMain();
        base2 =  hasNatural ? the.bases.myMain() : nullptr;
    }
    else // vT or vZ
    {
        base1 = the.bases.myMain();
        base2 = hasNatural ? the.bases.myNatural() : nullptr;

        Base * enemyStart = the.bases.enemyStart();
        if (hasNatural && enemyStart)
        {
            // We know where the enemy is. Put the first in the natural if it is significantly closer.
            int mainDist = the.bases.myMain()->getCenter().getApproxDistance(enemyStart->getPosition());
            int natDist = the.bases.myNatural()->getCenter().getApproxDistance(enemyStart->getPosition());
            if (natDist + 8 * 32 < mainDist)
            {
                base1 = the.bases.myNatural();
                base2 = the.bases.myMain();
            }
        }
    }

    // Fill in _airBases.
    _airBases.clear();
    _airBases.push_back(base1);
    if (base2 && _plan.antiAir >= 2)
    {
        _airBases.push_back(base2);
    }

    if (_plan.antiAir > 2)
    {
        int remaining = _plan.antiAir - 2;
        for (Base * base : the.bases.getAll())
        {
            if (base->isMyCompletedBase() && base != base1 && base != base2)
            {
                _airBases.push_back(base);
                --remaining;
                if (remaining <= 0)
                {
                    break;
                }
            }
        }
    }
}

// TODO This doesn't work well. So it's not used. But the idea should be OK.
BWAPI::TilePosition StaticDefense::terranSpreadTurretPosition(Base * base) const
{
	int minDist = 7;
	if (base->getNatural())
	{
		// It's a main base. There should be a lot of room and possibly a lot of buildings.
		minDist = 10;
	}

	const int limit = 17;

	// Randomly choose a grid spacing, so that we get varied placements.
	const int gridSpacing = the.random.flag(0.5) ? 5 : 4;

	int bestDistance = MAX_DISTANCE;
	BWAPI::TilePosition bestLocation = base->getAntiMineralLineTile();

	for (int tileX = std::max(4, base->getTilePosition().x - limit); tileX <= std::min(BWAPI::Broodwar->mapWidth() - 4, base->getTilePosition().x + limit); tileX += gridSpacing)
	{
		for (int tileY = std::max(4, base->getTilePosition().y - limit); tileY <= std::min(BWAPI::Broodwar->mapHeight() - 4, base->getTilePosition().y + limit); tileY += gridSpacing)
		{
			BWAPI::TilePosition tile(tileX, tileY);
			if (base->isInBase(tile) &&
				!BWAPI::Broodwar->getClosestUnit(
					TileCenter(tile),
					BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Missile_Turret && BWAPI::Filter::IsOwned,
					32 * minDist
				))
			{
				Building b(BWAPI::UnitTypes::Terran_Missile_Turret, tile);
				BWAPI::TilePosition location = the.placer.getBuildLocationNear(b, 1);
				int distance = base->getCenter().getApproxDistance(TileCenter(location));
				if (location.isValid() && base->isInBase(location) && distance < bestDistance)
				{
					bestDistance = distance;
					bestLocation = location;
				}
			}
		}
	}
	return bestLocation;
}

// All checks passed.
// For zerg, one building at a time. For terran, make one at each base this round.
// Protoss only makes "ground" defense, because cannons are general-purpose.
void StaticDefense::startAirBuilding()
{
    const std::vector<Base *> & bases =
        _plan.airIsPerBase ? the.bases.getAll() : _airBases;
    
	bool isZerg = the.selfRace() == BWAPI::Races::Zerg;

    for (Base * base : bases)
    {
		int nUnits = base->getNumUnits(_air) + the.production.getQueue().numInNextN(_air, _plan.antiAir);
		if (base->isMyBase() && nUnits < (_plan.airIsPerBase ? _plan.antiAir : 1))
        {
			if (!isZerg ||
				base->isCompleted() && base->getNumUnits(BWAPI::UnitTypes::Zerg_Creep_Colony) == 0)
            {
				BWAPI::TilePosition location = base->getMineralLineTile();
				if (!isZerg && nUnits > 0 && !_plan.airStrongpoint)
				{
					location = base->getAntiMineralLineTile();
					//location = terranSpreadTurretPosition(base);
				}
				else if (isZerg && nUnits % 2 == 1)
				{
					location = base->getAntiMineralLineTile();
				}
                startBuilding(_air, location);
				if (isZerg)
				{
					return;
				}
            }
        }
    }
}

// Order terran turrets or zerg spore colonies.
// Protoss doesn't use this, because cannons are general-purpose.
void StaticDefense::buildAir()
{
	if (the.selfRace() == BWAPI::Races::Protoss || _plan.antiAir <= 0)
    {
        return;
    }

	// Build ebay/evo if needed.
    if (_plan.antiAir > 0 && needPrerequisite(_airPrereq))
    {
        return;
    }

    // Morph creep colonies that the building manager forgot about, if needed.
    if (morphStrandedCreeps(_air))
    {
        return;
    }

    // Zerg is not in a hurry. Wait for the last one to finish.
    if (the.selfRace() == BWAPI::Races::Zerg && alreadyBuilding(_air))
    {
        return;
    }

    if (!_plan.airIsPerBase)
    {
        chooseAirBases();
    }

    startAirBuilding();
}

// Only zerg waits for the last building to finish before starting the next.
bool StaticDefense::alreadyBuilding(BWAPI::UnitType type)
{
	return
		UnitUtil::GetUncompletedUnitCount(type) > 0 ||
		BuildingManager::Instance().isBeingBuilt(type) ||
		UnitUtil::GetUncompletedUnitCount(BWAPI::UnitTypes::Zerg_Creep_Colony) > 0 ||
		the.production.getQueue().anyInNextN(type, 2);
}

// Add a preprequisite tech building, if necessary.
// Return true iff the prerequisite is not available.
bool StaticDefense::needPrerequisite(BWAPI::UnitType prereq)
{
	if (!prereq.isValid())
    {
		return false;
    }

    if (the.my.completed.count(prereq) > 0)
    {
		return false;
    }

    // If we're very low on workers, defense tech will not save us no matter how much we need it.
    if (the.my.completed.countWorkers() <= 5)
    {
		return true;
    }

    if (the.my.all.count(prereq) > 0 ||
        BuildingManager::Instance().isBeingBuilt(prereq) ||
        the.production.getQueue().anyInNextN(prereq, 2))
    {
        // It's building.
        // Build time of a creep colony is 300 frames. If the prereq will finish by then, it's OK.
        if (the.selfRace() == BWAPI::Races::Zerg && the.info.remainingBuildTime(prereq) <= 300)
        {
			return false;
        }

        return true;
    }

	the.production.getQueue().queueAsHighestPriority(prereq);
    return true;
}

// Possibly add static defenses at various bases.
void StaticDefense::build()
{
    if (!buildGround())
    {
        buildAir();
    }
}

void StaticDefense::drawGroundCount(Base * base, int count) const
{
    if (count == 0)
    {
        return;
    }

    std::string building;
    if (the.selfRace() == BWAPI::Races::Terran)
    {
        building = "bunker";
    }
    else if (the.selfRace() == BWAPI::Races::Protoss)
    {
        building = "cannon";
    }
    else // zerg
    {
        building = "sunken";
    }

    BWAPI::Position xy = base->getFront();
    BWAPI::Broodwar->drawTextMap(xy+BWAPI::Position(-26, -6), "%c%d x %s", yellow, count, building.c_str());
    BWAPI::Broodwar->drawCircleMap(xy, 32, BWAPI::Colors::Orange);
}

void StaticDefense::drawAirCount(Base * base, int count) const
{
    if (count == 0)
    {
        return;
    }

    std::string building;
    if (the.selfRace() == BWAPI::Races::Terran)
    {
        building = "turret";
    }
    else if (the.selfRace() == BWAPI::Races::Protoss)
    {
        building = "(none)";
    }
    else // zerg
    {
        building = "spore";
    }

	std::string formation = "spread out";
	if (_plan.airStrongpoint)
	{
		formation = "strongpoint";
	}

    BWAPI::Position xy = base->getPosition();
    BWAPI::Broodwar->drawTextMap(xy+BWAPI::Position(-26, -6), "%c%d x %s", yellow, count, building.c_str());
	BWAPI::Broodwar->drawTextMap(xy + BWAPI::Position(-26, 4), "%c%s", orange, formation.c_str());
	BWAPI::Broodwar->drawCircleMap(xy, 32, BWAPI::Colors::Orange);
}

void StaticDefense::draw() const
{
    if (!Config::Debug::DrawStaticDefensePlan)
    {
        return;
    }

    for (Base * base : the.bases.getAll())
    {
        if (base->getOwner() == the.self())
        {
            if (base == the.bases.myFront() && _plan.atFront > _plan.atOuterBases)
            {
                drawGroundCount(base, _plan.atFront);
            }
            else if (base->isInnerBase())
            {
                drawGroundCount(base, _plan.atInnerBases);
            }
            else // outer base
            {
                drawGroundCount(base, _plan.atOuterBases);
            }

            if (_plan.airIsPerBase)
            {
                drawAirCount(base, _plan.antiAir);
            }
        }
    }

    if (!_plan.airIsPerBase)
    {
        for (Base * base : _airBases)
        {
            if (base->getOwner() == the.self())
            {
                drawAirCount(base, _plan.antiAir);
            }
        }
    }
}

// -- -- -- -- -- -- -- -- -- -- --
// Public methods.

// When this runs, the.selfRace() and enemyRace() are not yet initialized.
StaticDefense::StaticDefense()
    : _MinWorkerLimit(BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg && BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Zerg
		? 10		// ZvZ
		: 18)		// all other matchups
{
    if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Terran)
    {
        _ground = BWAPI::UnitTypes::Terran_Bunker;
        _groundPrereq =  BWAPI::UnitTypes::Terran_Barracks;
        _air = BWAPI::UnitTypes::Terran_Missile_Turret;
        _airPrereq = BWAPI::UnitTypes::Terran_Engineering_Bay;
    }
    else if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Protoss)
    {
        _ground = BWAPI::UnitTypes::Protoss_Photon_Cannon;
        _groundPrereq =  BWAPI::UnitTypes::Protoss_Forge;
        _air = BWAPI::UnitTypes::None;
        _airPrereq = BWAPI::UnitTypes::None;
    }
    else // zerg
    {
        _ground = BWAPI::UnitTypes::Zerg_Sunken_Colony;
        _groundPrereq =  BWAPI::UnitTypes::Zerg_Spawning_Pool;
        _air = BWAPI::UnitTypes::Zerg_Spore_Colony;
        _airPrereq = BWAPI::UnitTypes::Zerg_Evolution_Chamber;
    }
}

// Called every frame. Does nothing on most frames.
void StaticDefense::update()
{
    if (!Config::Skills::StaticDefense)
    {
        return;
    }

    int phase = the.now() % 19;

    if (phase == 0)
    {
        plan();
    }
    else if (phase == 1)
    {
		build();
    }

    draw();
}

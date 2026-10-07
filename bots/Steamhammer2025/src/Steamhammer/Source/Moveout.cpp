#include "Moveout.h"

#include "OpsStrategy.h"
#include "The.h"

using namespace UAlbertaBot;

// Decide, based on heuristics, when the given squad should
// move out. Otherwise it stays home.

// Scouting and harassment squads are not included.
// They naturally want to be out and about.

// It's essential for terran, and for any units that benefit from mass
// and need to hold back until they have enough:

// Terran: Any main army.
// Terran: Valkyries versus zerg, get a minimum number.
// Protoss: Corsairs versus zerg, get a minimum number.

// the.opsStrategy.getAggression() controls ground attack squads;
// they do not move out when Defensive. The opening book can set the
// aggression level, and so can other code.
// The opening book can also set _rushBuild for terran, so that
// squads rush instead of being held back to build up. So can other
// code, in principle.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

// Is this an air squad of valkyries?
bool Moveout::isValkyrieSquad(const PlayerSnapshot & snap, int count) const
{
	return
		snap.count(BWAPI::UnitTypes::Terran_Science_Vessel) +
		snap.count(BWAPI::UnitTypes::Terran_Valkyrie)
		== count;
}

// Is this an air squad of corsairs and/or scouts?
bool Moveout::isCorsairScoutSquad(const PlayerSnapshot & snap, int count) const
{
	return
		snap.count(BWAPI::UnitTypes::Protoss_Observer) +
		snap.count(BWAPI::UnitTypes::Protoss_Scout) +
		snap.count(BWAPI::UnitTypes::Protoss_Corsair)
		== count;
}

// For a terran ground squad.
// True for infantry, false for factory units.
// Infantry-tank mix counts as infantry; there usually aren't that many tanks.
bool Moveout::isInfantry(const PlayerSnapshot & snap, int count) const
{
	return
		the.selfRace() == BWAPI::Races::Terran &&
		(snap.count(BWAPI::UnitTypes::Terran_Marine) +
		snap.count(BWAPI::UnitTypes::Terran_Firebat) +
		snap.count(BWAPI::UnitTypes::Terran_Medic)
		> count / 2);
}

// For a terran ground squad.
bool Moveout::isMostlyVultures(const PlayerSnapshot & snap, int count) const
{
	return
		snap.count(BWAPI::UnitTypes::Terran_Science_Vessel) +
		snap.count(BWAPI::UnitTypes::Terran_Vulture)
		>= 4 * count / 5;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

Moveout::Moveout()
	: _rushBuild(false)
{
}

bool Moveout::go(const Squad * squad, SquadType type)
{
	if (type == SquadType::GroundAttack &&
		the.opsStrategy.getAggression() == AggressionLevel::Defensive)
	{
		return false;
	}

	const BWAPI::Unitset & units = squad->getUnits();
	if (units.empty())
	{
		return false;
	}

	PlayerSnapshot snap(units);
	int count = int(units.size());

	const bool infantry = isInfantry(snap, count);

	if (_rushBuild)
	{
		// The opening turns on _rushBuild for terran rushes that must move out immediately.
		// Other races normally move out ground squads immediately anyway.
		// Turn it off (and return to defense) if moving out has become dangerous.
		if (the.selfRace() == BWAPI::Races::Terran &&
			the.enemyRace() == BWAPI::Races::Zerg &&
			(	the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk) >= 3 ||
				the.info.enemyCloakedUnitsSeen() ||
				the.info.enemyHasLingSpeed() && infantry && the.your.seen.count(BWAPI::UnitTypes::Zerg_Zergling) >= 2))
		{
			_rushBuild = false;
		}
		else
		{
			return true;
		}
	}

	if (the.selfRace() == BWAPI::Races::Terran)
	{
		// Terran ground army.
		if (type == SquadType::GroundAttack)
		{
			if (infantry)
			{
				return
					snap.count(BWAPI::UnitTypes::Terran_Marine) >= 16 &&
					snap.count(BWAPI::UnitTypes::Terran_Medic) >= 2;
			}
			// Factory mix.
			// Vulture squads move out, other factory squads wait for +1.
			if (isMostlyVultures(snap, count))
			{
				return true;
			}
			return the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Terran_Vehicle_Weapons) > 0;
		}

		// Valkyries.
		if (type == SquadType::Air &&
			the.enemyRace() == BWAPI::Races::Zerg &&
			isValkyrieSquad(snap, count))
		{
			return
				count >= 3 ||
				count >= the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk);
		}

		// All other terran squads should move out immediately.
		return true;
	}

	// Corsairs vs mutalisks.
	if (the.selfRace() == BWAPI::Races::Protoss &&
		the.enemyRace() == BWAPI::Races::Zerg &&
		isCorsairScoutSquad(snap, count))
	{
		int hasScourge = the.your.seen.count(BWAPI::UnitTypes::Zerg_Scourge) > 0 ? 1 : 0;
		return
			count >= 3 + hasScourge ||
			count >= the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk);
	}

	// All other squads should move out immediately.
	return true;
}

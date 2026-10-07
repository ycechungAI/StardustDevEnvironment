#include "StrategyBossProtoss.h"

#include "Bases.h"
#include "Forecast.h"
#include "Maker.h"
#include "ProductionManager.h"
#include "The.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Protected.

std::string StrategyBossProtoss::getMixString() const
{
	if (_mix == Mix::None)
	{
		return "None";
	}
	if (_mix == Mix::Zealots)
	{
		return "Zealots";
	}
	if (_mix == Mix::Dragoons)
	{
		return "Dragoons";
	}
	if (_mix == Mix::DragoonZealot)
	{
		return "Dragoon-Zealot";
	}
	if (_mix == Mix::DragoonReaver)
	{
		return "Dragoon-Reaver";
	}
	if (_mix == Mix::ZealotArchon)
	{
		return "Zealot-Archon";
	}
	if (_mix == Mix::MassScouts)
	{
		return "Scouts";
	}
	if (_mix == Mix::Carriers)
	{
		return "Carriers";
	}
	return "ERROR";
}

std::string StrategyBossProtoss::getAuxString() const
{
	if (_auxes.empty())
	{
		return "";
	}

	std::stringstream ss;
	ss << " +";
	for (auto it = _auxes.begin(); it != _auxes.end(); ++it)
	{
		if (*it == Aux::Observers)
		{
			ss << " Obs";
		}
		else if (*it == Aux::Reavers)
		{
			ss << " Reaver";
		}
		else if (*it == Aux::DTs)
		{
			ss << " DT";
		}
		else if (*it == Aux::Corsairs)
		{
			ss << " Corsair";
		}
		else if (*it == Aux::Scouts)
		{
			ss << " Scout";
		}
	}
	return ss.str();
}

bool StrategyBossProtoss::isAirMix() const
{
	return _mix == Mix::Carriers || _mix == Mix::MassScouts;
}

// 1. Build assimilators.
// 2. Expand when appropriate.
// 3. Ensure every nexus has a pylon so we can build there.
// 4. Add pylons to fix any building stall.
void StrategyBossProtoss::manageBases()
{
	// 1. Build assimilators.
	// Add them willy-nilly at every free geyser in our bases, if we have enough workers.
	// Usually, the opening book will build a refinery. If not, we start one at a common timing.
	if (_nFreeGas > 0 && _mineralWorkers >= 11)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Assimilator, _nGas + _nFreeGas));
	}

	// 2. Expand.
	// Expand, ideally until we can fully employ AbsoluteMaxWorkers.

	// We have enough probes for the bases we already own, and
	// there is room to employ more workers by taking another base, and
	// at least one free base exists, and
	// no nexus is currently under construction.
	if (!_emergency &&
		(the.my.all.count(BWAPI::UnitTypes::Protoss_Probe) >= _maxWorkers || baseRunningLow()) &&
		the.map.getNextExpansion(false, true, false) != BWAPI::TilePositions::None &&
		the.my.all.count(BWAPI::UnitTypes::Protoss_Nexus) == the.my.completed.count(BWAPI::UnitTypes::Protoss_Nexus) &&
		BuildingManager::Instance().getNumUnstarted(BWAPI::UnitTypes::Protoss_Nexus) == 0)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Nexus, 1 + _nBases));
	}

	// 3. Ensure every nexus has a pylon.
	for (Base * base : the.bases.getAll())
	{
		if (base->isMyCompletedBase() &&
			!base->inWorkerDanger() &&
			!base->isDoomed() &&
			!base->getPylon() &&
			!_emergency &&
			the.self()->minerals() >= 100 &&
			the.my.all.count(BWAPI::UnitTypes::Protoss_Pylon) == the.my.completed.count(BWAPI::UnitTypes::Protoss_Pylon) &&
			BuildingManager::Instance().getNumUnstarted(BWAPI::UnitTypes::Protoss_Pylon) == 0)
		{
			the.production.getQueue().queueAsHighestPriority(
				MacroAct(BWAPI::UnitTypes::Protoss_Pylon, base->getFrontTile())
			);
		}
	}

	// 4. If we are stalled for lack of pylon power (because the available space is used up),
	//    make a pylon.
	//    It could theoretically happen to other races, but I've never seen it.
	if (BuildingManager::Instance().getStalledForLackOfSpace() &&
		the.my.all.count(BWAPI::UnitTypes::Protoss_Pylon) == the.my.completed.count(BWAPI::UnitTypes::Protoss_Pylon) &&
		BuildingManager::Instance().getNumUnstarted(BWAPI::UnitTypes::Protoss_Pylon) == 0)
	{
		//BWAPI::Broodwar->printf("stall pylon");
		the.production.getQueue().queueAsHighestPriority(BWAPI::UnitTypes::Protoss_Pylon);
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

// -- -- -- -- -- --
// High-priority rules.

// Fallback rules in case minerals accumulate because of a gas shortage.
// The rules tell how to spend the extra minerals.
void StrategyBossProtoss::setBackstopRules()
{
	ADD(RuleLowGas(BWAPI::UnitTypes::Protoss_Zealot));
	ADD(RuleLowGas(BWAPI::UnitTypes::Protoss_Gateway, 22));		// highest number that makes sense
}

// If we predict something important before it happens, start preparations.
// Don't make expensive reactions because the forecast might be wrong.
// StaticDefense will build defenses and the regular unit mix will react if necessary,
// we mostly want to have the tech at hand.
void StrategyBossProtoss::setForecastRules()
{
	int t = the.now()
		+ BWAPI::UnitTypes::Protoss_Forge.buildTime()
		+ BWAPI::UnitTypes::Protoss_Photon_Cannon.buildTime();
	if (the.forecast.getDetectionFrame() <= t)
	{
		if (!gettingUnit(BWAPI::UnitTypes::Protoss_Forge))
		{
			ADD(Rule(BWAPI::UnitTypes::Protoss_Forge));
		}
		else if (!gettingUnit(BWAPI::UnitTypes::Protoss_Observatory))
		{
			ADD(Rule(BWAPI::UnitTypes::Protoss_Observatory));
		}
	}

	if (the.forecast.getAirDefenseFrame() <= t)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Forge));
	}

	t = the.now()
		+ BWAPI::UnitTypes::Protoss_Stargate.buildTime()
		+ BWAPI::UnitTypes::Protoss_Scout.buildTime();
	if (the.forecast.getEnemyUnitFrame(BWAPI::UnitTypes::Terran_Battlecruiser) <= t ||
		the.forecast.getEnemyUnitFrame(BWAPI::UnitTypes::Protoss_Carrier) <= t ||
		the.forecast.getEnemyUnitFrame(BWAPI::UnitTypes::Zerg_Guardian) <= t)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Scout));
	}
}

// -- -- -- -- -- --
// Upgrade rules.

void StrategyBossProtoss::setGroundUpgradeRules()
{
	if (hasUnit(BWAPI::UnitTypes::Protoss_Cybernetics_Core) &&
		_mix != Mix::Zealots)
	{
		ADD(Rule(BWAPI::UpgradeTypes::Singularity_Charge));
	}

	// If zealots are in the main mix, get legs.
	// Or if the enemy has many tanks, legs are a must.
	if ((_mix == Mix::Zealots || _mix == Mix::DragoonZealot) ||
		tank > 0.3 && _mineralWorkers >= 13 && _nGas > 0)
	{
		ADD(Rule(BWAPI::UpgradeTypes::Leg_Enhancements));
	}
	else if (hasUnit(BWAPI::UnitTypes::Protoss_Citadel_of_Adun))
	{
		if ((_mix == Mix::Zealots || _mix == Mix::DragoonZealot) &&
			the.my.completed.count(BWAPI::UnitTypes::Protoss_Zealot) >= 2
				||
			the.my.completed.count(BWAPI::UnitTypes::Protoss_Zealot) >= 4 && _nGas >= 2)
		{
			ADD(Rule(BWAPI::UpgradeTypes::Leg_Enhancements));
		}
	}

	// Simplified, but go with this for now.
	if (hasUnit(BWAPI::UnitTypes::Protoss_Forge) &&
		!_emergencyEnemyDead)
	{
		ADD(Rule(BWAPI::UpgradeTypes::Protoss_Ground_Weapons, 3));
		// Try to finish the prerequisites just before we need to upgrade past level 1.
		// Time to make citadel or archives is 900 each, 1800 total.
		// Time to upgrade weapons or armor +1 is 4000 (ground or air).
		if (hasUnit(BWAPI::UnitTypes::Protoss_Citadel_of_Adun) &&
			upgradeTimeRemaining(BWAPI::UpgradeTypes::Protoss_Ground_Weapons, 1) < 1000 ||		// need archives only
			upgradeTimeRemaining(BWAPI::UpgradeTypes::Protoss_Ground_Weapons, 1) < 2000)		// need both
		{
			ADD(Rule(BWAPI::UnitTypes::Protoss_Templar_Archives));
		}
		ADD(Rule(BWAPI::UpgradeTypes::Protoss_Ground_Armor, 3));
		ADD(Rule(BWAPI::UpgradeTypes::Protoss_Plasma_Shields, 3));
	}

	if (_mix == Mix::DragoonReaver && the.my.completed.count(BWAPI::UnitTypes::Protoss_Reaver) >= 2)
	{
		ADD(Rule(BWAPI::UpgradeTypes::Scarab_Damage));
	}
}

// For when we're going heavy air.
// For now, we assume we only have one cyber core to upgrade in.
void StrategyBossProtoss::setAirUpgradeRules()
{
	// The air mixes make dragoons.
	ADD(Rule(BWAPI::UpgradeTypes::Singularity_Charge));

	// Double cyber for double upgrades.
	if ((_nBases >= 3 && _nGas >= 3 || corsair > 0.2 && enemySupply > 40 && _nGas >= 2) &&
		!_emergencyEnemyDead)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Cybernetics_Core, 2));
	}

	// Try to make the upgrade just before we need it.
	// Upgrading carrier capacity takes 1500 frames.
	// Building 4 interceptors takes 1200 frames.
	if (hasUnit(BWAPI::UnitTypes::Protoss_Fleet_Beacon) &&
		trainTimeRemaining(BWAPI::UnitTypes::Protoss_Carrier) < 350)
	{
		ADD(Rule(BWAPI::UpgradeTypes::Carrier_Capacity));
	}

	if (_mix == Mix::MassScouts)
	{
		ADD(Rule(BWAPI::UpgradeTypes::Gravitic_Thrusters));
	}

	// Get a stargate before starting air upgrades!
	if (the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Protoss_Air_Weapons) < 3 &&
		hasUnit(BWAPI::UnitTypes::Protoss_Stargate) &&
		!_emergencyEnemyDead)
	{
		// Upgrades >= 2 require fleet beacon, which we make for air mixes anyway.
		ADD(Rule(BWAPI::UpgradeTypes::Protoss_Air_Weapons, 3));
	}
	if (the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Protoss_Air_Armor) < 3 &&
		hasUnit(BWAPI::UnitTypes::Protoss_Stargate) &&
		!_emergencyEnemyDead)
	{
		ADD(Rule(BWAPI::UpgradeTypes::Protoss_Air_Armor, 3));
	}

	if (the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Protoss_Air_Weapons) >= 2 &&
		hasUnit(BWAPI::UnitTypes::Protoss_Forge) &&
		!_emergencyEnemyDead)
	{
		// Upgrades >= 2 require cyber core, which we already made.
		ADD(Rule(BWAPI::UpgradeTypes::Protoss_Plasma_Shields, 3));
	}
}

// Choose the main upgrades depending on whether the mix is air or ground.
void StrategyBossProtoss::setMixUpgradeRules()
{
	if (_mix == Mix::None)
	{
	}
	else if (isAirMix())
	{
		setAirUpgradeRules();
	}
	else
	{
		setGroundUpgradeRules();
	}
}

void StrategyBossProtoss::setAuxUpgradeRules()
{
	if (_auxes.find(Aux::Corsairs) != _auxes.end())
	{
		if (valkyrie > 0.0 || corsair > 0.0)
		{
			ADD(Rule(BWAPI::UpgradeTypes::Protoss_Air_Armor, 1));
		}
		ADD(Rule(BWAPI::UpgradeTypes::Protoss_Air_Weapons, 1));
	}

	if (_auxes.find(Aux::Observers) != _auxes.end() &&
		hasUnit(BWAPI::UnitTypes::Protoss_Observer) &&
		(
			the.my.completed.count(BWAPI::UnitTypes::Protoss_Probe) > 48 ||
			the.my.completed.count(BWAPI::UnitTypes::Protoss_Probe) > 24 && the.info.enemyCloakedUnitsSeen())
		)
	{
		ADD(Rule(BWAPI::UpgradeTypes::Gravitic_Boosters));
	}
}

void StrategyBossProtoss::setSpecialMatchupRules()
{
	if (the.enemyRace() == BWAPI::Races::Terran)
	{
		// Make zealots to deal with tanks, dragoons to deal with other units.
		if (_mix == Mix::DragoonZealot && mech > 0.01)
		{
			// This rule is particularly crude.
			ADD(RuleRatio(BWAPI::UnitTypes::Protoss_Zealot, BWAPI::UnitTypes::Protoss_Dragoon, tank / mech));
		}
	}

	else if (the.enemyRace() == BWAPI::Races::Protoss)
	{
		// Make a zealot or two along with the dragoons to absorb fire.
		// If enemy is all zealot, make more zealots for safety.
		if (_mix == Mix::Dragoons)
		{
			int n = 1;
			if (the.your.seen.count(BWAPI::UnitTypes::Protoss_Zealot) >= 7)
			{
				n = 3;
			}
			else if (the.my.completed.count(BWAPI::UnitTypes::Protoss_Dragoon) >= 8 && reaver == 0.0)
			{
				n = 2;
			}
			ADD(Rule(BWAPI::UnitTypes::Protoss_Zealot, n));
		}
	}

	else if (the.enemyRace() == BWAPI::Races::Zerg)
	{
		// Early on make zealots to deal with lings, dragoons to deal with other units.
		// Or if the zealots are getting speed, and the enemy doesn't have much air or lurkers,
		// mix in zealots regularly.
		if (hasUnit(BWAPI::UnitTypes::Protoss_Citadel_of_Adun) && enemyAir < 0.2 && lurker < 0.1)
		{
			ADD(RuleRatio(BWAPI::UnitTypes::Protoss_Zealot, BWAPI::UnitTypes::Protoss_Dragoon, 1.0));
		}
		else if (hasUnit(BWAPI::UnitTypes::Protoss_Citadel_of_Adun) && enemyAir < 0.3 && lurker < 0.2)
		{
			ADD(RuleRatio(BWAPI::UnitTypes::Protoss_Zealot, BWAPI::UnitTypes::Protoss_Dragoon, 0.5));
		}
		else if (_mix == Mix::DragoonZealot && zergling > 0.1 && enemyAir < 0.4)
		{
			ADD(RuleRatio(BWAPI::UnitTypes::Protoss_Zealot, BWAPI::UnitTypes::Protoss_Dragoon, zergling / (1.0 - enemyAir)));
		}
	}
}

void StrategyBossProtoss::setMixRules()
{
	if (_mix == Mix::Zealots)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Zealot));
		ADD(Rule(BWAPI::UnitTypes::Protoss_Gateway, 1 + 3 * _nBases));
	}
	else if (_mix == Mix::Dragoons)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Dragoon));
		ADD(Rule(BWAPI::UnitTypes::Protoss_Gateway, 1 + 2 * _nBases));
	}
	else if (_mix == Mix::DragoonZealot)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Dragoon));
		ADD(Rule(BWAPI::UnitTypes::Protoss_Gateway, 1 + 2 * _nBases));
	}
	else if (_mix == Mix::DragoonReaver)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Reaver));
		ADD(Rule(BWAPI::UnitTypes::Protoss_Dragoon));
		ADD(Rule(BWAPI::UnitTypes::Protoss_Gateway, 1 + 2 * _nBases));
		if (_nBases >= 3)
		{
			// Double robo if we have enough bases.
			ADD(Rule(BWAPI::UnitTypes::Protoss_Robotics_Facility, 2));
		}
	}
	else if (_mix == Mix::ZealotArchon)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_High_Templar, 2));
		ADD(Rule(BWAPI::UnitTypes::Protoss_Zealot));
		ADD(Rule(BWAPI::UnitTypes::Protoss_Gateway, 1 + 2 * _nBases));
	}
	else if (_mix == Mix::MassScouts)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Scout));
		ADD(Rule(BWAPI::UnitTypes::Protoss_Stargate, 2 + _nBases / 2));
		ADD(Rule(BWAPI::UnitTypes::Protoss_Dragoon));
		ADD(Rule(BWAPI::UnitTypes::Protoss_Gateway, 1 + _nBases));
	}
	else if (_mix == Mix::Carriers)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Carrier));
		if (hasUnit(BWAPI::UnitTypes::Protoss_Fleet_Beacon))
		{
			ADD(Rule(BWAPI::UnitTypes::Protoss_Stargate, 2 + _nBases / 2));
		}
		// If the enemy is dead, make no ground units at all.
		if (!_emergencyEnemyDead)
		{
			ADD(Rule(BWAPI::UnitTypes::Protoss_Dragoon));
			ADD(Rule(BWAPI::UnitTypes::Protoss_Gateway, 2 + _nBases / 2));
		}
	}

	// For all mixes, get a forge before too long, just to be ready.
	if (_nBases >= 2)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Forge));
	}
}

void StrategyBossProtoss::setAuxRules(Aux aux, bool bothObsAndReaver)
{
	if (aux == Aux::Observers)
	{
		if (_mix == Mix::DragoonReaver && the.my.completed.count(BWAPI::UnitTypes::Protoss_Robotics_Facility) < 2)
		{
			// Reavers get priority.
			ADD(RuleRatio(BWAPI::UnitTypes::Protoss_Observer, BWAPI::UnitTypes::Protoss_Reaver, 1.0, MaxObservers));
		}
		else
		{
			ADD(Rule(BWAPI::UnitTypes::Protoss_Observer, MaxObservers));
		}
	}
	else if (aux == Aux::Reavers)
	{
		int n = 0;
		if (the.enemyRace() == BWAPI::Races::Terran)
		{
			n = (infantry > 0.33) ? 3 : (tank > 0.33 ? 1 : 2);
		}
		else if (the.enemyRace() == BWAPI::Races::Protoss)
		{
			n = the.info.enemyHasStorm() ? 1 : 3;
		}
		else if (the.enemyRace() == BWAPI::Races::Zerg)
		{
			n = (mutalisk + guardian == 0.0) ? 3 : 1;
		}
		if (bothObsAndReaver && the.my.completed.count(BWAPI::UnitTypes::Protoss_Robotics_Facility) < 2)
		{
			// Observers get priority. 
			if (hasUnit(BWAPI::UnitTypes::Protoss_Observer))
			{
				ADD(RuleRatio(BWAPI::UnitTypes::Protoss_Reaver, BWAPI::UnitTypes::Protoss_Observer, 1.0, n));
			}
		}
		else
		{
			ADD(Rule(BWAPI::UnitTypes::Protoss_Reaver, n));
		}
	}
	else if (aux == Aux::DTs)
	{
		int n = the.info.enemyHasMobileDetection() ? 2 : 4;
		ADD(Rule(BWAPI::UnitTypes::Protoss_Dark_Templar,
			std::min(n, 1 + the.my.completed.count(BWAPI::UnitTypes::Protoss_Dark_Templar))));
	}
	else if (aux == Aux::Archons)
	{
		// An archon costs 2 x 150 = 300 gas.
		if (the.self()->gas() > 225 && _nGas >= 2)
		{
			ADD(Rule(BWAPI::UnitTypes::Protoss_High_Templar, 2));
		}
	}
	else if (aux == Aux::Corsairs)
	{
		int n = 0;
		if (the.enemyRace() == BWAPI::Races::Terran)
		{
			n = 4 + int(10 * wraith);
		}
		else if (the.enemyRace() == BWAPI::Races::Protoss)
		{
			n = 6 + int(10 * scout + 5 * corsair);
		}
		else if (the.enemyRace() == BWAPI::Races::Zerg)
		{
			n = 4 + (scourge > 0 ? 2 : 0) + the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk) / 4;
		}
		ADD(Rule(BWAPI::UnitTypes::Protoss_Corsair, n));
		if (n > 8)
		{
			ADD(Rule(BWAPI::UnitTypes::Protoss_Stargate, 2));
		}
	}
}

void StrategyBossProtoss::setAuxesRules()
{
	bool bothObsAndReaver =
		_auxes.find(Aux::Observers) != _auxes.end() &&
		_auxes.find(Aux::Reavers) != _auxes.end() &&
		hasUnit(BWAPI::UnitTypes::Protoss_Robotics_Facility) == 1;

	for (int i = int(Aux::FIRST); i < int(Aux::LAST); ++i)
	{
		Aux aux = Aux(i);
		if (_auxes.find(aux) != _auxes.end())
		{
			setAuxRules(aux, bothObsAndReaver);
		}
	}
}

void StrategyBossProtoss::setRegularRules()
{
	setBackstopRules();
	if (!the.ops.allHands())
	{
		setForecastRules();
		setMixUpgradeRules();
		setAuxUpgradeRules();
	}
	setAuxesRules();
	setSpecialMatchupRules();
	setMixRules();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

void StrategyBossProtoss::setProbeRules()
{
	// Give priority to economy except when emergency production is needed,
	// and even then if the probe count is low.
	if (_emergencyEnemyDead)
	{
		// Lower worker limit if the enemy seems dead.
		ADD(Rule(BWAPI::UnitTypes::Protoss_Probe, std::min(50, _maxWorkers)));
	}
	else if (!_emergencyDefense || the.my.completed.count(BWAPI::UnitTypes::Protoss_Probe) < 12)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Probe, _maxWorkers));
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// PvT

void StrategyBossProtoss::updateMixPvT()
{
	if (!hasUnit(BWAPI::UnitTypes::Protoss_Cybernetics_Core))
	{
		_mix = Mix::Zealots;
	}
	else if (battlecruiser > 0.25 && goliath < 0.2 && valkyrie == 0.0)
	{
		_mix = Mix::MassScouts;
	}
	else if (battlecruiser > 0.25 && tank < 0.25)
	{
		_mix = Mix::Dragoons;
	}
	else if (the.self()->supplyUsed() > 160 && _nBases >= 3 && _nGas >= 3 && !the.info.enemyHasLockdown() &&
		(isAirMix() || mech > 0.65 && goliath == 0.0 && ghost == 0 || mech > 0.5 && goliath < 0.1 && the.forecast.neverSeen(BWAPI::UnitTypes::Terran_Ghost)))
	{
		// Once we've started carriers, we try to stick with them.
		// They grow more powerful with numbers.
		_mix = Mix::Carriers;
	}
	else if (tank > 0.5)
	{
		_mix = Mix::Zealots;
	}
	else if (infantry + vulture + goliath > 0.5)
	{
		if (the.your.seen.count(BWAPI::UnitTypes::Terran_Academy) == 0 &&
			the.your.inferred.count(BWAPI::UnitTypes::Terran_Academy) == 0 &&
			enemySupply < 80)
		{
			_mix = Mix::Dragoons;
		}
		else
		{
			_mix = Mix::DragoonReaver;
		}
	}
	else
	{
		_mix = Mix::DragoonZealot;
	}
}

void StrategyBossProtoss::updateAuxPvT()
{
	_auxes.clear();

	if (_mix == Mix::Zealots && wraith > 0.05 || wraith > 0.2)
	{
		_auxes.insert(Aux::Corsairs);
	}

	if (hasUnit(BWAPI::UnitTypes::Protoss_Robotics_Facility) || the.info.enemyHasCloakTech())
	{
		_auxes.insert(Aux::Observers);
	}

	bool goingReaver = false;
	if (hasUnit(BWAPI::UnitTypes::Protoss_Robotics_Facility) &&
		_mix != Mix::DragoonReaver &&
		infantry + vulture > 0.5 &&
		!the.info.enemyHasLockdown() &&
		ghost <= 6 &&
		// Not along with an air mix unless we have reasonable gas.
		(_nGas >= 3 || _mix != Mix::Carriers && _mix != Mix::MassScouts))
	{
		goingReaver = true;
		_auxes.insert(Aux::Reavers);
	}

	if (hasUnit(BWAPI::UnitTypes::Protoss_Templar_Archives))
	{
		// Check for ample gas before choosing multiple gas units.
		if (isAirMix())
		{
			if (_nGas >= 5 || !goingReaver && _nGas >= 4)
			{
				_auxes.insert(Aux::DTs);
			}
		}
		else
		{
			_auxes.insert(Aux::DTs);
		}
	}
}

void StrategyBossProtoss::setRulesXvT()
{
	// Update the primary unit mix less often. Even though it's quick.
	if (_emergencyEnemyDead)
	{
		_mix = Mix::Carriers;
	}
	else if (_mix == Mix::None || the.now() % UpdateInterval * 4 == 0)
	{
		updateMixPvT();
	}

	updateAuxPvT();

	setProbeRules();
	setRegularRules();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// PvP

void StrategyBossProtoss::updateMixPvP()
{
	if (!hasUnit(BWAPI::UnitTypes::Protoss_Cybernetics_Core))
	{
		_mix = Mix::Zealots;
	}
	else if (carrier >= 0.4)
	{
		_mix = Mix::MassScouts;
	}
	else if (the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Leg_Enhancements) ||
		the.self()->isUpgrading(BWAPI::UpgradeTypes::Leg_Enhancements))
	{
		_mix = Mix::DragoonZealot;
	}
	else if (hasUnit(BWAPI::UnitTypes::Protoss_Robotics_Support_Bay) &&
		hasUnit(BWAPI::UnitTypes::Protoss_Robotics_Facility))
	{
		_mix = Mix::DragoonReaver;
	}
	else
	{
		_mix = Mix::Dragoons;
	}
}

void StrategyBossProtoss::updateAuxPvP()
{
	_auxes.clear();

	if (_mix == Mix::Zealots && scout > 0.1)
	{
		_auxes.insert(Aux::Corsairs);
	}

	if (hasUnit(BWAPI::UnitTypes::Protoss_Robotics_Facility) || the.info.enemyHasCloakTech())
	{
		_auxes.insert(Aux::Observers);
	}

	bool goingReaver = false;
	if (hasUnit(BWAPI::UnitTypes::Protoss_Robotics_Facility) &&
		_mix != Mix::DragoonReaver &&
		enemyAir - corsair < 0.25 &&
		// Not along with an air mix unless we have reasonable gas.
		// NOTE We don't go carriers PvP.
		(_nGas >= 3 || !isAirMix()))
	{
		goingReaver = true;
		_auxes.insert(Aux::Reavers);
	}

	if (hasUnit(BWAPI::UnitTypes::Protoss_Templar_Archives))
	{
		// Check for ample gas before choosing multiple gas units.
		bool goingDT = false;
		if (_mix == Mix::MassScouts)
		{
			if (_nGas >= 5 || !goingReaver && _nGas >= 4)
			{
				goingDT = true;
				_auxes.insert(Aux::DTs);
			}
		}
		else if (!goingReaver || _nGas >= 2)
		{
			goingDT = true;
			_auxes.insert(Aux::DTs);
		}

		if (_mix == Mix::Zealots && dragoon < 0.1 && reaver < 0.05 && !goingReaver && !goingDT)
		{
			_auxes.insert(Aux::Archons);
		}
	}
}

void StrategyBossProtoss::setRulesXvP()
{
	// Update the primary unit mix less often. Even though it's quick.
	if (_emergencyEnemyDead)
	{
		_mix = Mix::Carriers;
	}
	else if (_mix == Mix::None || the.now() % UpdateInterval * 4 == 0)
	{
		updateMixPvP();
	}

	updateAuxPvP();

	setProbeRules();
	setRegularRules();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// PvZ

void StrategyBossProtoss::updateMixPvZ()
{
	if (!hasUnit(BWAPI::UnitTypes::Protoss_Cybernetics_Core))
	{
		_mix = Mix::Zealots;
	}
	else if (guardian >= 0.3 && enemyAir > 0.6 && devourer == 0.0)
	{
		_mix = Mix::MassScouts;
	}
	else if (hydralisk < 0.35 && _nBases >= 3 && _nGas >= 3 &&
		the.forecast.neverSeen(BWAPI::UnitTypes::Zerg_Scourge) &&
		the.forecast.neverSeen(BWAPI::UnitTypes::Zerg_Defiler) &&
		the.forecast.neverSeen(BWAPI::UnitTypes::Zerg_Devourer))
	{
		// Risky against zerg--because of units that this opponent has never shown.
		_mix = Mix::Carriers;
	}
	else if (hasUnit(BWAPI::UnitTypes::Protoss_Robotics_Facility) &&
		(lurker > 0.2 || airToGroundZ() < 0.2 || the.your.seen.count(BWAPI::UnitTypes::Zerg_Defiler) > 0))
	{
		_mix = Mix::DragoonReaver;
	}
	else if (lurker > 0.15)
	{
		// Observers and reavers will take time. Get dragoons out first.
		_mix = Mix::DragoonZealot;
	}
	else if (airToGroundZ() < 0.4 &&
		(the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Leg_Enhancements) > 0 ||
		the.self()->isUpgrading(BWAPI::UpgradeTypes::Leg_Enhancements)))
	{
		_mix = Mix::DragoonZealot;
	}
	else
	{
		_mix = Mix::Dragoons;
	}
}

void StrategyBossProtoss::updateAuxPvZ()
{
	_auxes.clear();

	if (_mix == Mix::Zealots && mutalisk > 0.0 ||
		mutalisk > 0.05 ||
		hasUnit(BWAPI::UnitTypes::Protoss_Stargate) && hydralisk < 0.2 && scourge < 10 && devourer == 0.0 ||
		hydralisk < 0.1 && scourge < 8 && devourer == 0.0)
	{
		_auxes.insert(Aux::Corsairs);
	}

	if (hasUnit(BWAPI::UnitTypes::Protoss_Robotics_Facility) || the.info.enemyHasCloakTech())
	{
		_auxes.insert(Aux::Observers);
	}

	// Favor reavers more if the enemy has broodling.
	bool goingReaver = false;
	if (hasUnit(BWAPI::UnitTypes::Protoss_Robotics_Facility) &&
		_mix != Mix::DragoonReaver &&
		(the.info.enemyHasBroodling() && airToGroundZ() < 0.35 || airToGroundZ() < 0.25) &&
		// Not along with an air mix unless we have reasonable gas.
		// NOTE We don't go carriers PvZ. They die to scourge and fail versus hydras.
		(!isAirMix() || _nGas >= 3))
	{
		goingReaver = true;
		_auxes.insert(Aux::Reavers);
	}

	if (hasUnit(BWAPI::UnitTypes::Protoss_Templar_Archives))
	{
		// Favor archons more if the enemy has broodling.
		bool goingArchon = false;
		if (_mix != Mix::ZealotArchon &&
			the.info.enemyHasBroodling() && zergling + mutalisk > 0.4 && hydralisk < 0.2 ||
			zergling + mutalisk > 0.5 && hydralisk < 0.2)
		{
			goingArchon = true;
			_auxes.insert(Aux::Archons);
		}

		// Check for ample gas before choosing multiple gas units.
		if (isAirMix() && !goingArchon)
		{
			if (_nGas >= 5 || !goingReaver && _nGas >= 4)
			{
				_auxes.insert(Aux::DTs);
			}
		}
		else if (!goingReaver && !goingArchon || _nGas >= 3)
		{
			_auxes.insert(Aux::DTs);
		}
	}
}

void StrategyBossProtoss::setRulesXvZ()
{
	// Update the primary unit mix less often. Even though it's quick.
	if (_emergencyEnemyDead)
	{
		_mix = Mix::Carriers;
	}
	else if (_mix == Mix::None || the.now() % UpdateInterval * 4 == 0)
	{
		updateMixPvZ();
	}

	updateAuxPvZ();

	setProbeRules();
	setRegularRules();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Emergencies.

void StrategyBossProtoss::setBasicsRules()
{
	// Get a gateway if we don't have it.
	if (the.my.completed.count(BWAPI::UnitTypes::Protoss_Probe) >= 10 &&
		!hasUnit(BWAPI::UnitTypes::Protoss_Gateway))
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Cybernetics_Core));
	}

	// Get gas if we don't have it.
	if (the.self()->gas() < 50 &&
		!hasUnit(BWAPI::UnitTypes::Protoss_Assimilator) &&
		the.my.completed.count(BWAPI::UnitTypes::Protoss_Probe) >= 12)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Assimilator));
	}

	// Get a cyber core if we don't have it.
	if (the.self()->gas() >= 50 &&
		the.my.completed.count(BWAPI::UnitTypes::Protoss_Probe) >= 15 &&
		!hasUnit(BWAPI::UnitTypes::Protoss_Cybernetics_Core))
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Cybernetics_Core));
	}

	// Get a forge if the enemy has cloaked units or air units.
	if ((the.info.enemyCloakedUnitsSeen() || enemyAir > 0.0 || droppers > 0) &&
		the.my.completed.count(BWAPI::UnitTypes::Protoss_Probe) >= 15 &&
		!hasUnit(BWAPI::UnitTypes::Protoss_Forge))
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Forge));
	}
}

void StrategyBossProtoss::recognizeEmergency()
{
	_emergencyNoBase = the.bases.baseCount(the.self()) == 0;

	_emergencyEnemyAttack = false;
	for (Base * base : the.bases.getAll())
	{
		if (base->getOwner() == the.self() && base->inWorkerDanger())
		{
			_emergencyEnemyAttack = true;
			break;
		}
	}

	_emergencyNeedDetection =
		the.info.enemyHasCloakTech() &&
		the.my.all.count(BWAPI::UnitTypes::Protoss_Observer) == 0;

	_emergencyNeedAirDefense = 
		the.info.enemyHasAirTech() &&
		the.my.all.count(BWAPI::UnitTypes::Protoss_Cybernetics_Core) == 0;

	_emergencyNaturalContain =
		the.bases.myNatural() && the.bases.myNatural()->hasEnemyDefense();

	_emergency =
		_emergencyNoBase ||
		_emergencyEnemyAttack ||
		_emergencyNeedDetection ||
		_emergencyNeedAirDefense ||
		_emergencyNaturalContain;

	// If there's another emergency, then the enemy is not dead yet.
	if (!_emergency)
	{
		_emergencyEnemyDead = the.ops.enemySeemsDead();
		_emergency = _emergencyEnemyDead;
	}

	_emergencyDefense =
		_emergencyEnemyAttack ||
		_emergencyNeedDetection && the.my.all.count(BWAPI::UnitTypes::Protoss_Forge) == 0;

	_allHands = _emergencyEnemyAttack;
}

void StrategyBossProtoss::setEmergencyRules()
{
	// There may be multiple emergencies at the same time.
	// Try to handle them in priority order.

	if (_emergencyNoBase)
	{
		if (!gettingUnit(BWAPI::UnitTypes::Protoss_Nexus))
		{
			ADD_IN_FRONT(Rule(BWAPI::UnitTypes::Protoss_Nexus, 1, Priority::Emergency));
		}
	}

	if (_emergencyNeedDetection)
	{
		// Static defense will usually add cannons. That's the first line of defense.
		// When those have started to come up, get observers.
		ADD(Rule(BWAPI::UnitTypes::Protoss_Forge));
		if (gettingUnit(BWAPI::UnitTypes::Protoss_Photon_Cannon))
		{
			ADD(Rule(BWAPI::UnitTypes::Protoss_Observer));
		}
	}

	if (_emergencyNeedAirDefense)
	{
		// Ensure we can make dragoons.
		// Beyond that, don't count it as an emergency.
		// Static defense will make forge and cannons if needed.
		ADD(Rule(BWAPI::UnitTypes::Protoss_Cybernetics_Core));
	}

	// if (_emergencyEnemyAttack)
	// Do nothing. Rely on the regular unit mix to make defenders.
	// NOTE Probes are skipped in an attack emergency unless we are short of them.

	if (_emergencyNaturalContain)
	{
		ADD(Rule(BWAPI::UnitTypes::Protoss_Reaver, 2));
	}

	// if (_emergencyEnemyDead)
	// No rules here. This sets the unit mix instead.
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

StrategyBossProtoss::StrategyBossProtoss()
	: _mix(Mix::None)
	, _emergencyNoBase(false)
	, _emergencyEnemyAttack(false)
	, _emergencyNeedDetection(false)
	, _emergencyNeedAirDefense(false)
	, _emergencyNaturalContain(false)
	, _emergencyEnemyDead(false)
{
}

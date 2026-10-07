#include "StrategyBossTerran.h"

#include "Bases.h"
#include "BuildingPilot.h"
#include "Forecast.h"
#include "ProductionManager.h"
#include "Maker.h"
#include "Random.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Protected.

std::string StrategyBossTerran::getMixString(Mix mix) const
{
	if (mix == Mix::None)
	{
		return "None";
	}
	if (mix == Mix::Marines)
	{
		return "Marines";
	}
	if (mix == Mix::Infantry)
	{
		return "Infantry";
	}
	if (mix == Mix::TankInfantry)
	{
		return "Tank-Infantry";
	}
	if (mix == Mix::TankVulture)
	{
		return "Tank-Vulture";
	}
	if (mix == Mix::TankVultureGoliath)
	{
		return "Tank-Vulture-Goliath";
	}
	if (mix == Mix::Goliath)
	{
		return "Goliath";
	}
	if (mix == Mix::BattlecruiserInfantry)
	{
		return "Battlecruiser-Infantry";
	}
	if (mix == Mix::BattlecruiserVulture)
	{
		return "Battlecruiser-Vulture";
	}
	return "ERROR";
}

std::string StrategyBossTerran::getMixString() const
{
	return getMixString(_mix);
}

std::string StrategyBossTerran::getAuxString() const
{
	if (_auxes.empty())
	{
		return "";
	}

	std::stringstream ss;
	ss << " +";
	for (auto it = _auxes.begin(); it != _auxes.end(); ++it)
	{
		if (*it == Aux::Vessels)
		{
			ss << " Vessel";
		}
		else if (*it == Aux::Wraiths)
		{
			ss << " Wraith";
		}
	}
	return ss.str();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

bool StrategyBossTerran::isBarracksMix() const
{
	return
		_mix == Mix::Marines ||
		_mix == Mix::Infantry ||
		_mix == Mix::TankInfantry ||
		_mix == Mix::BattlecruiserInfantry;
}

// NOTE TankInfantry is both a barracks mix and a factory mix.
bool StrategyBossTerran::isFactoryMix() const
{
	return
		_mix == Mix::TankInfantry ||
		_mix == Mix::TankVulture ||
		_mix == Mix::TankVultureGoliath ||
		_mix == Mix::Goliath ||
		_mix == Mix::BattlecruiserVulture;
}

// Really "is battlecruiser mix".
// NOTE Aux air units don't make it an air mix.
bool StrategyBossTerran::isAirMix() const
{
	return
		_mix == Mix::BattlecruiserInfantry ||
		_mix == Mix::BattlecruiserVulture;
}

// -- -- -- -- --
// Manage bases.

// 1. Build refineries.
// 2. Expand when appropriate.
void StrategyBossTerran::manageBases()
{
	// 1. Build refineries.
	// Add them willy-nilly at every free geyser in our bases, if we have enough workers.
	// Usually, the opening book will build a refinery. If not, we start one at a common timing.
	if (_nFreeGas > 0 && _mineralWorkers >= 12)
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Refinery, _nGas + _nFreeGas));
	}

	// 2. Expand.
	// Expand, ideally until we can fully employ AbsoluteMaxWorkers.
	// This version expands whenever existing bases are saturated.

	// We have enough SCVs for the bases we already own, and
	// there is room to employ more workers by taking another base, and
	// at least one free base exists, and
	// no CC is currently under construction.
	if (!_emergency &&
		(the.my.all.count(BWAPI::UnitTypes::Terran_SCV) >= _maxWorkers || baseRunningLow()) &&
		the.map.getNextExpansion(false, true, false) != BWAPI::TilePositions::None &&
		the.my.all.count(BWAPI::UnitTypes::Terran_Command_Center) == the.my.completed.count(BWAPI::UnitTypes::Terran_Command_Center) &&
		BuildingManager::Instance().getNumUnstarted(BWAPI::UnitTypes::Terran_Command_Center) == 0)
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Command_Center, 1 + _nBases));
	}
}

// -- -- -- -- -- --
// High-priority rules.

// Fallback rules in case minerals accumulate because of a gas shortage.
// The rules tell how to spend the extra minerals.
void StrategyBossTerran::setBackstopRules()
{
	if (isBarracksMix())
	{
		ADD(RuleLowGas(BWAPI::UnitTypes::Terran_Marine));
		ADD(RuleLowGas(BWAPI::UnitTypes::Terran_Barracks, std::min(_mineralWorkers / 3 - 1, 22)));
	}
	else if (hasUnit(BWAPI::UnitTypes::Terran_Factory))		// regardless of the mix
	{
		ADD(RuleLowGas(BWAPI::UnitTypes::Terran_Vulture));
		ADD(RuleLowGas(BWAPI::UnitTypes::Terran_Factory, std::min(_mineralWorkers / 4 - 1, 11)));
	}
}

// If we predict something important before it happens, start preparations.
// Don't make expensive reactions because the forecast might be wrong.
// StaticDefense will build defenses and the regular unit mix will react if necessary,
// we mostly want to have the tech at hand.
void StrategyBossTerran::setForecastRules()
{
	int t = the.now()
		+ BWAPI::UnitTypes::Terran_Engineering_Bay.buildTime()
		+ BWAPI::UnitTypes::Terran_Missile_Turret.buildTime();
	if (the.forecast.getDetectionFrame() <= t)
	{
		if (!gettingUnit(BWAPI::UnitTypes::Terran_Engineering_Bay))
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Engineering_Bay));
		}
		else if (!gettingUnit(BWAPI::UnitTypes::Terran_Academy))
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Academy));
		}
		else if (!gettingUnit(BWAPI::UnitTypes::Terran_Science_Facility))
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Science_Facility));
		}
	}

	if (the.forecast.getAirDefenseFrame() <= t)
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Engineering_Bay));
	}

	int guardianT = the.forecast.getEnemyUnitFrame(BWAPI::UnitTypes::Zerg_Guardian);

	t = the.now() + BWAPI::UpgradeTypes::Charon_Boosters.upgradeTime();
	if (the.forecast.getEnemyUnitFrame(BWAPI::UnitTypes::Terran_Battlecruiser) <= t ||
		the.forecast.getEnemyUnitFrame(BWAPI::UnitTypes::Protoss_Carrier) <= t ||
		isFactoryMix() && guardianT <= t)
	{
		ADD(Rule(BWAPI::UpgradeTypes::Charon_Boosters));
	}

	t = the.now() + BWAPI::UnitTypes::Terran_Wraith.buildTime();
	if (guardianT <= t)
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Wraith));
	}
}

// -- -- -- -- -- --
// Upgrade rules.

// Choose the main upgrades depending on whether the mix is air or ground.
void StrategyBossTerran::setMixUpgradeRules()
{
	if (_mix == Mix::None)
	{
		return;
	}

	if (isBarracksMix())
	{
		ADD(Rule(BWAPI::TechTypes::Stim_Packs));
		ADD(Rule(BWAPI::UpgradeTypes::U_238_Shells));
		if (the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::U_238_Shells) > 0)
		{
			ADD(Rule(BWAPI::UpgradeTypes::Terran_Infantry_Weapons, 3));
			ADD(Rule(BWAPI::UpgradeTypes::Terran_Infantry_Armor, 3));
		}
		if (_nBases >= 2 &&
			the.my.completed.count(BWAPI::UnitTypes::Terran_Barracks) >= 3 &&
			gettingUnit(BWAPI::UnitTypes::Terran_Science_Facility))
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Engineering_Bay, 2));
		}
	}

	if (isFactoryMix())
	{
		if (_mix != Mix::Goliath &&
			the.my.completed.count(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) > 0)
		{
			ADD(Rule(BWAPI::TechTypes::Tank_Siege_Mode));
		}

		// Vultures:
		// If the enemy is mostly vulture/zealot/zergling, get speed first.
		// Otherwise get mines first.
		if ((_mix == Mix::TankVulture ||
			_mix == Mix::TankVultureGoliath ||
			_mix == Mix::BattlecruiserVulture) &&
			the.my.completed.count(BWAPI::UnitTypes::Terran_Vulture) > 0 ||
			the.my.completed.count(BWAPI::UnitTypes::Terran_Vulture) >= 4)
		{
			// Vulture speed takes 1500 frames. Spider mines take 1200 frames.
			// 3000 frames is enough time to get both, with a buffer.
			const int timeEnough = the.now() + 3000;
			bool speedFirst = false;
			if (the.enemyRace() == BWAPI::Races::Terran)
			{
				speedFirst = vulture > 0.9;
			}
			else if (the.enemyRace() == BWAPI::Races::Protoss)
			{
				speedFirst = zealot > 0.9 && dt == 0.0 &&
					the.forecast.getEnemyUnitFrame(BWAPI::UnitTypes::Protoss_Dark_Templar) > timeEnough;
			}
			else if (the.enemyRace() == BWAPI::Races::Zerg)
			{
				speedFirst = zergling > 0.9 && lurker == 0.0 &&
					the.forecast.getEnemyUnitFrame(BWAPI::UnitTypes::Zerg_Lurker) > timeEnough;
			}
			if (speedFirst)
			{
				ADD(Rule(BWAPI::UpgradeTypes::Ion_Thrusters));
				ADD(Rule(BWAPI::TechTypes::Spider_Mines));
			}
			else
			{
				ADD(Rule(BWAPI::TechTypes::Spider_Mines));
				ADD(Rule(BWAPI::UpgradeTypes::Ion_Thrusters));
			}
		}

		if ((_mix == Mix::Goliath || _mix == Mix::TankVultureGoliath) &&
			(enemyAir > 0.0 || droppers > 0 || detectors > 0 && _nGas > 1))
		{
			ADD(Rule(BWAPI::UpgradeTypes::Charon_Boosters));
		}

		// Don't get armory too early if we're getting infantry upgrades too.
		if (!_emergency && (_mix != Mix::TankInfantry || _nBases >= 2 && _nGas >= 2))
		{
			// If we're going tank-infantry, get a tank before we start upgrades.
			if (_mix != Mix::TankInfantry ||
				hasUnit(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) ||
				hasUnit(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode))
			{
				ADD(Rule(BWAPI::UpgradeTypes::Terran_Vehicle_Weapons, 3));
				if (!isAirMix())
				{
					ADD(Rule(BWAPI::UpgradeTypes::Terran_Vehicle_Plating, 3));
				}
			}
			if (_nBases >= 2 && the.my.completed.count(BWAPI::UnitTypes::Terran_Factory) >= 3)
			{
				ADD(Rule(BWAPI::UnitTypes::Terran_Armory, 2));
			}
		}
	}

	if (isAirMix() && hasUnit(BWAPI::UnitTypes::Terran_Battlecruiser) && !_emergency)
	{
		ADD(Rule(BWAPI::UpgradeTypes::Terran_Ship_Weapons, 3));
		ADD(Rule(BWAPI::UnitTypes::Terran_Armory, 2));
		ADD(Rule(BWAPI::UpgradeTypes::Terran_Ship_Plating, 3));
	}
}

void StrategyBossTerran::setAuxUpgradeRules()
{
	// Wraiths might be made in the opening or by an aux rule.
	if (the.my.all.count(BWAPI::UnitTypes::Terran_Wraith) >= 4)
	{
		ADD(Rule(BWAPI::TechTypes::Cloaking_Field));
	}

	// If it's an air mix, rely on the air mix upgrade rules.
	if (the.my.all.count(BWAPI::UnitTypes::Terran_Valkyrie) >= 2 && !isAirMix() && !_emergency)
	{
		ADD(Rule(BWAPI::UpgradeTypes::Terran_Ship_Weapons));
	}
}

// -- -- -- -- -- --
// Production rules.

void StrategyBossTerran::setSpecialMatchupRules()
{
	if (the.enemyRace() != BWAPI::Races::Zerg)
	{
		if (_nGas > 0 && hasUnit(BWAPI::UnitTypes::Terran_Barracks))
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Factory));
		}
	}
}

// No firebats in TvT.
// Some in TvP if facing zealots.
// Many in TvZ if facing zerglings or dark swarm.
void StrategyBossTerran::setFirebatRules()
{
	if (hasUnit(BWAPI::UnitTypes::Terran_Academy))
	{
		if (the.enemyRace() == BWAPI::Races::Protoss)
		{
			if (zealot > 0.6)
			{
				ADD(RuleRatio(BWAPI::UnitTypes::Terran_Firebat, BWAPI::UnitTypes::Terran_Marine, zealot / 4));
			}
		}
		else if (the.enemyRace() == BWAPI::Races::Zerg)
		{
			if (zergling > 0.20)
			{
				ADD(RuleRatio(BWAPI::UnitTypes::Terran_Firebat, BWAPI::UnitTypes::Terran_Marine, zergling / 2));
			}
			else if (the.your.ever.count(BWAPI::UnitTypes::Zerg_Defiler) > 0)
			{
				ADD(RuleRatio(BWAPI::UnitTypes::Terran_Firebat, BWAPI::UnitTypes::Terran_Marine, zergling / 4));
			}
		}
	}
}

void StrategyBossTerran::setBattlecruiserRules()
{
	ADD(Rule(BWAPI::UnitTypes::Terran_Battlecruiser));
	if (hasUnit(BWAPI::UnitTypes::Terran_Physics_Lab))
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Starport, _nGas));
		ADD(Rule(BWAPI::UnitTypes::Terran_Control_Tower, _nGas));
	}
	else
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Starport));
		ADD(Rule(BWAPI::UnitTypes::Terran_Control_Tower));
	}
	ADD(Rule(BWAPI::UnitTypes::Terran_Physics_Lab));
}

void StrategyBossTerran::setMixRules()
{
	if (_mix == Mix::Marines)
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Marine));
	}
	else if (_mix == Mix::Infantry)
	{
		if (the.my.all.count(BWAPI::UnitTypes::Terran_Barracks) >= 2 &&
			!gettingUnit(BWAPI::UnitTypes::Terran_Refinery))
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Refinery));
		}
		if (gettingUnit(BWAPI::UnitTypes::Terran_Refinery))
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Academy));
		}
		ADD(RuleRatio(BWAPI::UnitTypes::Terran_Medic, BWAPI::UnitTypes::Terran_Marine, 1.0 / 6));
		setFirebatRules();
		ADD(Rule(BWAPI::UnitTypes::Terran_Marine));
		ADD(Rule(BWAPI::UnitTypes::Terran_Barracks, 1 + 2 * _nBases));
	}
	else if (_mix == Mix::TankInfantry)
	{
		int n = std::max(1, _nGas);
		ADD(Rule(BWAPI::UnitTypes::Terran_Factory, n));
		ADD(Rule(BWAPI::UnitTypes::Terran_Machine_Shop, n));
		if (gettingUnit(BWAPI::UnitTypes::Terran_Refinery))
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Academy));
		}
		ADD(RuleRatio(BWAPI::UnitTypes::Terran_Medic, BWAPI::UnitTypes::Terran_Marine, 1.0 / 6));
		setFirebatRules();
		if (the.enemyRace() == BWAPI::Races::Zerg)
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Marine));
			bool substituteVulture = zergling >= 0.95;
			ADD(Rule(substituteVulture ? BWAPI::UnitTypes::Terran_Vulture : BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode));
		}
		else
		{
			// NOTE No need to substitute vultures versus terran.
			bool substituteVulture = zealot + dt >= 0.95;
			ADD(Rule(substituteVulture ? BWAPI::UnitTypes::Terran_Vulture : BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode));
			ADD(Rule(BWAPI::UnitTypes::Terran_Marine));
		}
		ADD(Rule(BWAPI::UnitTypes::Terran_Barracks, 1 + 2 * _nBases));
	}
	else if (_mix == Mix::TankVulture)
	{
		// If we're building up mass, make tanks.
		// If the enemy is nearly all zealot/DT or zerglings, only make vultures.
		if (inBuildupMode() ||
			the.enemyRace() == BWAPI::Races::Terran ||
			the.enemyRace() == BWAPI::Races::Protoss && zealot + dt < 0.95 ||
			the.enemyRace() == BWAPI::Races::Zerg && zergling < 0.95)
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode));
		}
		else
		{
			// Exception: Some tanks if the enemy has ground static defense buildings.
			int n = the.your.seen.count(UnitUtil::GetGroundStaticDefenseType(the.enemyRace()));
			if (n > 0)
			{
				ADD(Rule(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, n >= 3 ? 4 : 1));
			}
		}
		ADD(Rule(BWAPI::UnitTypes::Terran_Vulture));
		ADD(Rule(BWAPI::UnitTypes::Terran_Factory, 1 + 2 * _nBases));
		ADD(Rule(BWAPI::UnitTypes::Terran_Machine_Shop, _nGas));
	}
	else if (_mix == Mix::TankVultureGoliath)
	{
		// Rely on low-gas vultures to spend any excess minerals.
		if (!hasUnit(BWAPI::UnitTypes::Terran_Armory))
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Vulture));	// vultures until goliaths are possible
		}
		if (enemyAir > enemySupply / 2)
		{
			ADD(RuleRatio(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, BWAPI::UnitTypes::Terran_Goliath, 0.33));
			ADD(Rule(BWAPI::UnitTypes::Terran_Goliath));
		}
		else
		{
			ADD(RuleRatio(BWAPI::UnitTypes::Terran_Goliath, BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, 2.0));
			ADD(Rule(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode));
		}
		ADD(Rule(BWAPI::UnitTypes::Terran_Factory, 1 + 2 * _nBases));
		ADD(Rule(BWAPI::UnitTypes::Terran_Machine_Shop, _nGas));
	}
	else if (_mix == Mix::Goliath)
	{
		// Rely on low-gas vultures to spend any excess minerals.
		if (!hasUnit(BWAPI::UnitTypes::Terran_Armory))
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Vulture));	// vultures until goliaths are possible
		}
		ADD(RuleRatio(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, BWAPI::UnitTypes::Terran_Goliath, 1.0 / 12));
		ADD(Rule(BWAPI::UnitTypes::Terran_Goliath));
		ADD(Rule(BWAPI::UnitTypes::Terran_Factory, 1 + 3 * _nBases));
	}
	else if (_mix == Mix::BattlecruiserInfantry)
	{
		setBattlecruiserRules();
		if (!_emergencyEnemyDead)
		{
			// Fewer medics to save a little gas for BCs.
			ADD(RuleRatio(BWAPI::UnitTypes::Terran_Medic, BWAPI::UnitTypes::Terran_Marine, 1.0 / 7));
			ADD(Rule(BWAPI::UnitTypes::Terran_Marine));
		}
	}
	else if (_mix == Mix::BattlecruiserVulture)
	{
		setBattlecruiserRules();
		if (!_emergencyEnemyDead)
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Vulture));
		}
	}

	// For all mixes, get an ebay before too long, just to be ready for turrets.
	// NOTE Barracks mixes get an ebay on their own.
	if (_nBases >= 2 && !isBarracksMix())
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Engineering_Bay));
	}
}

void StrategyBossTerran::setVesselRules()
{
	if (_auxes.find(Aux::Vessels) != _auxes.end())
	{
		// Only 1 vessel in gas-heavy mixes, unless we need more for safety.
		int n = 1;
		if (!isAirMix() &&
			(isBarracksMix() || the.info.enemyHasCloakTech()))
		{
			n = 3;
		}
		ADD(Rule(BWAPI::UnitTypes::Terran_Science_Vessel, n));
	}
}

void StrategyBossTerran::setAuxesRules()
{
	bool vesselsFirst =
		the.your.seen.count(BWAPI::UnitTypes::Protoss_Dark_Templar) > 0 ||
		the.your.seen.count(BWAPI::UnitTypes::Zerg_Lurker) > 0;

	if (vesselsFirst)
	{
		setVesselRules();
	}

	if (_auxes.find(Aux::Wraiths) != _auxes.end())
	{
		// A small number of wraiths for light harassment.
		int n = 3;
		if (the.enemyRace() == BWAPI::Races::Terran)
		{
			// Any number of wraiths to counter a specific situation.
			int bc = the.your.seen.count(BWAPI::UnitTypes::Terran_Battlecruiser);
			if (bc >= 4)
			{
				n = std::max(n, 2 * bc);
			}
			n += the.your.seen.count(BWAPI::UnitTypes::Terran_Dropship);
		}
		else if (the.enemyRace() == BWAPI::Races::Zerg)
		{
			n = 2;
			if (antiAirZ() == 0.0 || droppers > 0)
			{
				n = 5;
			}
			if (guardian > 0.0)
			{
				n = std::max(n, the.your.seen.count(BWAPI::UnitTypes::Zerg_Guardian) / 4);
			}
		}
		else
		{
			// Protoss or Unknown.
			n = std::max(n, droppers);
			// Rare: If the enemy does not have corsairs or observers, counter carriers and arbiters.
			if (the.your.seen.count(BWAPI::UnitTypes::Protoss_Observer) == 0 &&
				the.your.seen.count(BWAPI::UnitTypes::Protoss_Corsair) == 0)
			{
				n = std::max(n,
					4 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Carrier) +
					3 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Arbiter));
			}
		}
		ADD(Rule(BWAPI::UnitTypes::Terran_Wraith, n));
		if (n >= 6)
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Starport, (n + 2) / 4));
		}
	}

	if (_auxes.find(Aux::Valkyries) != _auxes.end())
	{
		// 3 valks are devastating against mutalisks, with cautious micro.
		// Versus wraiths, corsairs, or scouts we make only 1 to reinforce the
		// regular anti-air.
		// If you make many valks, you are likely to suffer the sprite bug where
		// they refuse to fire.
		int n = 3;
		if (the.enemyRace() == BWAPI::Races::Terran ||
			the.enemyRace() == BWAPI::Races::Protoss)
		{
			n = 1;
		}
		ADD(Rule(BWAPI::UnitTypes::Terran_Valkyrie, n));
	}

	if (_auxes.find(Aux::Goliaths) != _auxes.end())
	{
		// A small number to fend off air harass, including drops.
		int transports = droppers;
		if (the.enemyRace() == BWAPI::Races::Zerg)
		{
			transports = std::min(transports, 3);
		}
		int n =
			(2 +
			the.your.seen.count(BWAPI::UnitTypes::Terran_Wraith) +
			the.your.seen.count(BWAPI::UnitTypes::Protoss_Scout) +
			transports) / 3;
		ADD(Rule(BWAPI::UnitTypes::Terran_Goliath, n));
	}

	if (!vesselsFirst)
	{
		setVesselRules();
	}
}

void StrategyBossTerran::setRegularRules()
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

void StrategyBossTerran::setBattlecruiserMix()
{
	if (the.my.all.count(BWAPI::UnitTypes::Terran_Barracks) > the.my.all.count(BWAPI::UnitTypes::Terran_Factory))
	{
		_mix = Mix::BattlecruiserInfantry;
	}
	else
	{
		_mix = Mix::BattlecruiserVulture;
	}
}

void StrategyBossTerran::setSCVrules()
{
	// Give priority to economy except when emergency production is needed,
	// and even then if the SCV count is low.
	if (_emergencyEnemyDead)
	{
		// Lower worker limit if the enemy seems dead.
		ADD(Rule(BWAPI::UnitTypes::Terran_SCV, std::min(50, _maxWorkers)));
	}
	else if (!_emergencyDefense || the.my.completed.count(BWAPI::UnitTypes::Terran_SCV) < 12)
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_SCV, _maxWorkers));
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// TvT

void StrategyBossTerran::updateMixTvT()
{
	if (!hasUnit(BWAPI::UnitTypes::Terran_Factory))
	{
		_mix = _preferInfantry ? Mix::TankInfantry : Mix::TankVulture;
	}
	else if (battlecruiser > 0.5)
	{
		// Marines are little use.
		_mix = Mix::Goliath;
	}
	else if (battlecruiser > 0.0 || enemyAir > 0.0 && !_preferInfantry)
	{
		_mix = Mix::TankVultureGoliath;
	}
	else if (the.self()->supplyUsed() > 200 && _nBases >= 3 && _nGas >= 3 &&
		hasUnit(BWAPI::UnitTypes::Terran_Armory) &&
		enemySupply >= 100 &&
		!the.info.enemyHasLockdown() &&
		(goliath < 0.2 && wraith < 0.1 && ghost == 0 || goliath < 0.4 && wraith < 0.2 && the.forecast.neverSeen(BWAPI::UnitTypes::Terran_Ghost)))
	{
		if (the.my.all.count(BWAPI::UnitTypes::Terran_Barracks) > the.my.all.count(BWAPI::UnitTypes::Terran_Factory))
		{
			_mix = Mix::BattlecruiserInfantry;
		}
		else
		{
			_mix = Mix::BattlecruiserVulture;
		}
	}
	else
	{
		_mix = _preferInfantry ? Mix::TankInfantry : Mix::TankVulture;
	}
}

void StrategyBossTerran::updateAuxTvT()
{
	_auxes.clear();
	
	if (!isAirMix())
	{
		if (the.info.enemyCloakedUnitsSeen() &&
			hasUnit(BWAPI::UnitTypes::Terran_Missile_Turret) &&
			hasUnit(BWAPI::UnitTypes::Terran_Comsat_Station) ||
			_nGas >= 2 && the.my.completed.count(BWAPI::UnitTypes::Terran_SCV) >= 28)
		{
			_auxes.insert(Aux::Vessels);
		}

		if (wraith > 0.2 && enemySupply >= 100)
		{
			_auxes.insert(Aux::Valkyries);
		}
		else if ((wraith > 0 || droppers > 0) &&
			isFactoryMix() && _mix != Mix::TankVultureGoliath && _mix != Mix::Goliath)
		{
			_auxes.insert(Aux::Goliaths);
		}
	}

	if (droppers > 0 ||
		battlecruiser > 0.0 ||
		_wraithHarass &&
		goliath + marine < 0.1 && valkyrie == 0.0 &&
		_nGas > 0 && hasUnit(BWAPI::UnitTypes::Terran_Factory))
	{
		_auxes.insert(Aux::Wraiths);
	}
}

void StrategyBossTerran::setRulesXvT()
{
	// Update the primary unit mix less often. Even though it's quick.
	if (_emergencyEnemyDead)
	{
		setBattlecruiserMix();
	}
	else if (_mix == Mix::None || the.now() % UpdateInterval * 4 == 0)
	{
		updateMixTvT();
	}

	updateAuxTvT();

	setSCVrules();
	setRegularRules();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// TvP

void StrategyBossTerran::updateMixTvP()
{
	if (!hasUnit(BWAPI::UnitTypes::Terran_Factory))
	{
		_mix = _preferInfantry ? Mix::TankInfantry : Mix::TankVulture;
	}
	else if (carrier > 0.4)
	{
		_mix = Mix::Goliath;
	}
	else if ((arbiter > 0 || enemyAir > 0.0) && !isBarracksMix())
	{
		_mix = Mix::TankVultureGoliath;
	}
	else if (the.self()->supplyUsed() > 200 && _nBases >= 3 && _nGas >= 3 &&
		hasUnit(BWAPI::UnitTypes::Terran_Armory) &&
		enemySupply >= 100 &&
		dragoon < 0.4 && scout == 0 && arbiter == 0 && darkArchon == 0)
	{
		setBattlecruiserMix();
	}
	else
	{
		_mix = _preferInfantry ? Mix::TankInfantry : Mix::TankVulture;
	}
}

void StrategyBossTerran::updateAuxTvP()
{
	_auxes.clear();

	if (!isAirMix())
	{
		// Dark templar are not unlikely. Get vessels earlier than in TvT.
		if (darkArchon == 0 &&
			the.info.enemyHasCloakTech() &&
			hasUnit(BWAPI::UnitTypes::Terran_Missile_Turret) &&
			hasUnit(BWAPI::UnitTypes::Terran_Comsat_Station) ||
			_nGas >= 2 && the.my.completed.count(BWAPI::UnitTypes::Terran_SCV) >= 21)
		{
			_auxes.insert(Aux::Vessels);
		}

		if (corsair + scout > 0.2 && enemySupply >= 100)
		{
			_auxes.insert(Aux::Valkyries);
		}
		else if ((scout > 0.0 || droppers > 0) &&
			isFactoryMix() && _mix != Mix::TankVultureGoliath && _mix != Mix::Goliath)
		{
			_auxes.insert(Aux::Goliaths);
		}
	}

	// Shuttles, or little enemy anti-air. Archons can be avoided.
	if (droppers > 0 ||
		_wraithHarass &&
		dragoon + scout < 0.1 && corsair == 0.0 &&
		_nGas > 0 && hasUnit(BWAPI::UnitTypes::Terran_Factory))
	{
		_auxes.insert(Aux::Wraiths);
	}
}

void StrategyBossTerran::setRulesXvP()
{
	// Update the primary unit mix less often. Even though it's quick.
	if (_emergencyEnemyDead)
	{
		setBattlecruiserMix();
	}
	else if (_mix == Mix::None || the.now() % UpdateInterval * 4 == 0)
	{
		updateMixTvP();
	}

	updateAuxTvP();

	setSCVrules();
	setRegularRules();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// TvZ

void StrategyBossTerran::updateMixTvZ()
{
	if (!hasUnit(BWAPI::UnitTypes::Terran_Factory))
	{
		_mix = _preferInfantry ? Mix::Infantry : Mix::Goliath;
	}
	else if (guardian > 0.5 ||
		!_preferInfantry && mutalisk > 0.3 && zergling + mutalisk > 0.6 && hydralisk < 0.2)
	{
		_mix = Mix::Goliath;
	}
	else if (guardian > 0.1 && !_preferInfantry)
	{
		_mix = Mix::TankVultureGoliath;
	}
	else if (lurker > 0.0 || expectLurkers())
	{
		if (_preferInfantry)
		{
			_mix = Mix::TankInfantry;
		}
		else
		{
			_mix = (mutalisk > 0.0 || expectMutalisks()) ? Mix::TankVultureGoliath : Mix::TankVulture;
		}
	}
	else if (the.self()->supplyUsed() > 200 && _nBases >= 3 && _nGas >= 3 &&
		hasUnit(BWAPI::UnitTypes::Terran_Armory) &&
		enemySupply >= 100 &&
		hydralisk < 0.4 && devourer == 0.0 && defiler == 0)
	{
		if (the.my.all.count(BWAPI::UnitTypes::Terran_Barracks) > the.my.all.count(BWAPI::UnitTypes::Terran_Factory))
		{
			_mix = Mix::BattlecruiserInfantry;
		}
		else
		{
			_mix = Mix::BattlecruiserVulture;
		}
	}
	else
	{
		_mix = _preferInfantry ? Mix::Infantry : Mix::Goliath;
	}
}

void StrategyBossTerran::updateAuxTvZ()
{
	_auxes.clear();

	if (!isAirMix())
	{
		if (the.info.enemyHasCloakTech() &&
			hasUnit(BWAPI::UnitTypes::Terran_Missile_Turret) &&
			hasUnit(BWAPI::UnitTypes::Terran_Comsat_Station) ||
			_nGas >= 2 && the.my.completed.count(BWAPI::UnitTypes::Terran_SCV) >= 24)
		{
			_auxes.insert(Aux::Vessels);
		}

		if (mutalisk > 0.2 && enemySupply >= 100 || queen >= 12)
		{
			_auxes.insert(Aux::Valkyries);
		}
	}

	if (droppers > 0 ||
		guardian > 0.0 ||
		_wraithHarass &&
		hydralisk < 0.33 && devourer == 0.0 &&
		_nGas > 0 && hasUnit(BWAPI::UnitTypes::Terran_Factory))
	{
		_auxes.insert(Aux::Wraiths);
	}
}

void StrategyBossTerran::setRulesXvZ()
{
	// Update the primary unit mix less often. Even though it's quick.
	if (_emergencyEnemyDead)
	{
		setBattlecruiserMix();
	}
	else if (_mix == Mix::None || the.now() % UpdateInterval * 4 == 0)
	{
		updateMixTvZ();
	}

	updateAuxTvZ();

	setSCVrules();
	setRegularRules();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Called once when we go out of the opening book to make initial decisions
// based on the infrastructure inherited from the opening build.
void StrategyBossTerran::strategyDecisions()
{
	int nRax = the.my.completed.count(BWAPI::UnitTypes::Terran_Barracks);
	bool hasEbay = hasUnit(BWAPI::UnitTypes::Terran_Engineering_Bay);
	bool hasAcad = hasUnit(BWAPI::UnitTypes::Terran_Academy);

	int nFactory = the.my.completed.count(BWAPI::UnitTypes::Terran_Factory);
	int nShop = the.my.all.count(BWAPI::UnitTypes::Terran_Machine_Shop);
	bool hasArmory = hasUnit(BWAPI::UnitTypes::Terran_Armory);

	// Infantry or mech preference.
	int infantryScore = nRax + (hasEbay ? 1 : 0) + (hasAcad ? 1 : 0);
	int mechScore = nFactory + (nFactory > 1 ? nShop : 0) + (hasArmory ? 2 : 0);

	if (the.enemyRace() == BWAPI::Races::Zerg)
	{
		_preferInfantry = nRax > 1 || infantryScore >= mechScore;
		_wraithHarass = the.random.flag(0.33);
	}
	else
	{
		// Terran, protoss, or (unlikely) Unknown.
		if (the.info.enemyHasCloakTech())
		{
			mechScore += 1;
		}
		_preferInfantry = nRax > 1 || infantryScore > mechScore;
		_wraithHarass = the.random.flag(0.125);
	}

	// Cancel unfinished stuff that doesn't support the decision.
	// This is right out of the opening, so we may rebuild it later.
	// But for now, resources are at a premium.
	// NOTE TankInfantry is both an infantry mix and a factory mix. _preferInfantry allows it.
	if (_preferInfantry)
	{
		MacroAct(BWAPI::UpgradeTypes::Terran_Vehicle_Plating).cancelUnfinished();
		MacroAct(BWAPI::UpgradeTypes::Terran_Vehicle_Weapons).cancelUnfinished();
	}
	else
	{
		// Prefer mech.
		if (!the.info.enemyHasCloakTech() &&
			!the.info.enemyHasAirTech() &&
			the.forecast.getStaticAirDefenseFrame() > the.now() + BWAPI::UnitTypes::Terran_Engineering_Bay.buildTime() + 240)
		{
			MacroAct(BWAPI::UnitTypes::Terran_Engineering_Bay).cancelUnfinished();
		}
		MacroAct(BWAPI::UpgradeTypes::Terran_Infantry_Weapons).cancelUnfinished();
		MacroAct(BWAPI::UpgradeTypes::Terran_Infantry_Armor).cancelUnfinished();
		MacroAct(BWAPI::TechTypes::Stim_Packs).cancelUnfinished();
		MacroAct(BWAPI::UpgradeTypes::U_238_Shells).cancelUnfinished();
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Emergencies and contingencies, major and minor.

// Building types that the strategy boss is willing to lift when appropriate.
bool StrategyBossTerran::isLiftable(BWAPI::Unit unit)
{
	BWAPI::UnitType type = unit->getType();

	return
		unit->isCompleted() &&
		(type == BWAPI::UnitTypes::Terran_Barracks ||
		type == BWAPI::UnitTypes::Terran_Command_Center ||
		type == BWAPI::UnitTypes::Terran_Engineering_Bay ||
		type == BWAPI::UnitTypes::Terran_Factory ||
		type == BWAPI::UnitTypes::Terran_Starport ||
		type == BWAPI::UnitTypes::Terran_Science_Facility);
}

// If there are any floating command centers, consider when to land them.
// This version does not move them; they attempt to land where they lifted from.
void StrategyBossTerran::landCCs()
{
	if (the.now() % 9 == 0)
	{
		for (BWAPI::Unit u : the.self()->getUnits())
		{
			if (u->getType() == BWAPI::UnitTypes::Terran_Command_Center && u->isLifted())
			{
				// Try to land, or wait?
				// If there are enemies in range to shoot, do not land.
				if (the.groundHits(u->getTilePosition()) > 0)
				{
					break;
				}
				// If not, land with a small probability each check.
				// Something near half the time, we land within 10 seconds.
				// It's a crude method to wait for the enemy to go away
				// without maintaining any state.
				// if (the.random.flag(0.025))		// TODO testing
				{
					the.buildingPilot.landInPlace(u);
				}
			}
		}
	}
}

// Lift buildings that are in danger of dying from enemy attack.
// Lift buildings that are not needed, land them when they are needed.
// Cancel stuff started earlier that the current unit mix does not want.
void StrategyBossTerran::manageUrgentIssues()
{
	// -- --
	// Lift off buildings in danger of dying from enemy attack.
	// A lifted building will land later if it is safe and useful.
	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (!u->isLifted() && isLiftable(u))
		{
			// TODO testing
			//if (u->getType() == BWAPI::UnitTypes::Terran_Command_Center &&
			//	(u->getHitPoints() < u->getType().maxHitPoints() && !the.info.getEnemyFireteam(u).empty() ||
			//		the.bases.getBaseAtTilePosition(u->getTilePosition()) && the.bases.getBaseAtTilePosition(u->getTilePosition())->isDoomed()))
			//{
			//	the.buildingPilot.lift(u);
			//	continue;
			//}
			//double healthLimit =
			//	(u->getType() == BWAPI::UnitTypes::Terran_Command_Center && the.your.ever.count(BWAPI::UnitTypes::Zerg_Queen) > 0)
			//	? 0.65											// don't let it become infestable
			//	: (UnitUtil::BuildingIsBusy(u) ? 0.4 : 0.5);	// if busy, hold on longer
			// END TODO

			double healthLimit = 0.45;
			if (u->getType() != BWAPI::UnitTypes::Terran_Command_Center &&
				u->getHitPoints() <= healthLimit * u->getType().maxHitPoints() &&
				!the.info.getEnemyFireteam(u).empty())
			{
				the.buildingPilot.lift(u);
			}
		}
	}

	// When it becomes safe-ish.
	//landCCs();

	// -- --
	// Stuff that happens less often.

	static Mix oldMix = Mix::None;

	// Continue if the mix has changed or we haven't run this in a while.
	if (oldMix == _mix && the.now() % 53 != 0)
	{
		return;
	}
	oldMix = _mix;

	// -- --
	// Land buildings that the mix can make use of.

	if (isBarracksMix())
	{
		the.buildingPilot.land(BWAPI::UnitTypes::Terran_Barracks);
		the.buildingPilot.land(BWAPI::UnitTypes::Terran_Engineering_Bay);
	}

	if (isFactoryMix() ||
		_auxes.find(Aux::Goliaths) != _auxes.end())
	{
		the.buildingPilot.land(BWAPI::UnitTypes::Terran_Factory);
	}

	if (isAirMix() ||
		_auxes.find(Aux::Vessels) != _auxes.end() ||
		_auxes.find(Aux::Wraiths) != _auxes.end())
	{
		the.buildingPilot.land(BWAPI::UnitTypes::Terran_Starport);
	}

	// -- --
	// Cancel stuff that we don't expect to need.

	if (!isBarracksMix())
	{
		MacroAct(BWAPI::UpgradeTypes::Terran_Infantry_Weapons).cancelUnfinished();
		MacroAct(BWAPI::UpgradeTypes::Terran_Infantry_Armor).cancelUnfinished();
	}

	if (!isFactoryMix())
	{
		MacroAct(BWAPI::UpgradeTypes::Terran_Vehicle_Weapons).cancelUnfinished();
		MacroAct(BWAPI::UpgradeTypes::Terran_Vehicle_Plating).cancelUnfinished();
	}

	// Try to save a little gas for more BCs.
	if (_mix == Mix::BattlecruiserVulture)
	{
		MacroAct(BWAPI::UpgradeTypes::Terran_Vehicle_Plating).cancelUnfinished();
	}

	// -- --
	// Lift buildings that don't need to be on the ground.
	// Don't lift if it's an emergency; we might need emergency production.
	// Cancel upgrades that we don't expect to need (the mix changed).

	if (_emergency)
	{
		return;
	}

	if (!isBarracksMix())
	{
		if (hasUnit(BWAPI::UnitTypes::Terran_Factory))
		{
			the.buildingPilot.lift(BWAPI::UnitTypes::Terran_Barracks);
		}
		the.buildingPilot.lift(BWAPI::UnitTypes::Terran_Engineering_Bay);
	}

	if (!isFactoryMix() &&
		_auxes.find(Aux::Goliaths) == _auxes.end())
	{
		if (hasUnit(BWAPI::UnitTypes::Terran_Barracks))
		{
			the.buildingPilot.lift(BWAPI::UnitTypes::Terran_Factory);
		}
	}
}

void StrategyBossTerran::setBasicsRules()
{
	// -- --
	// Get basic buildings.

	// Get a barracks if we don't have it.
	if (!hasUnit(BWAPI::UnitTypes::Terran_Barracks) &&
		(the.my.completed.count(BWAPI::UnitTypes::Terran_SCV) >= 11 || the.self()->minerals() >= 200))
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Barracks));
	}

	// Get gas if we don't have it.
	int nSCV = the.my.completed.count(BWAPI::UnitTypes::Terran_SCV);
	if ((the.self()->gas() < 100 || _nBases > 1 || nSCV >= 20) &&
		!gettingUnit(BWAPI::UnitTypes::Terran_Refinery) &&
		nSCV >= 13)
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Refinery));
	}

	// Get an ebay if the enemy has cloaked units or flying units.
	if ((the.info.enemyCloakedUnitsSeen() || enemyAir > 0.0 || droppers > 0) &&
		the.my.completed.count(BWAPI::UnitTypes::Terran_SCV) >= 12 &&
		!gettingUnit(BWAPI::UnitTypes::Terran_Engineering_Bay))
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Engineering_Bay, 1, Priority::Emergency));
	}

	// Get the academy if it's past time.
	if (!gettingUnit(BWAPI::UnitTypes::Terran_Academy) && _nGas > 0 && _nBases >= 2)
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Academy));
	}

	// Get scanners if we have gas and academy.
	if (hasUnit(BWAPI::UnitTypes::Terran_Academy) && _nGas > 0 && _nBases > 0)
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Comsat_Station, _nBases));
	}
}

// NOTE The enemy appearing dead counts as an emergency so that we can make mop-up units.
void StrategyBossTerran::recognizeEmergency()
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
		the.my.all.count(BWAPI::UnitTypes::Terran_Comsat_Station) == 0;

	_emergencyNeedAirDefense =
		the.info.enemyHasAirTech() &&
		the.my.all.count(BWAPI::UnitTypes::Terran_Missile_Turret) == 0 &&
		the.my.all.count(BWAPI::UnitTypes::Terran_Marine) < 4 &&
		the.my.all.count(BWAPI::UnitTypes::Terran_Goliath) == 0;

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
		_emergencyNeedDetection && !gettingUnit(BWAPI::UnitTypes::Terran_Engineering_Bay);

	_allHands = _emergencyEnemyAttack;
}

void StrategyBossTerran::setEmergencyRules()
{
	// There may be multiple emergencies at the same time.
	// Try to handle them in priority order.

	if (_emergencyNoBase)
	{
		if (!gettingUnit(BWAPI::UnitTypes::Terran_Command_Center))
		{
			ADD_IN_FRONT(Rule(BWAPI::UnitTypes::Terran_Command_Center, 1, Priority::Emergency));
		}
	}

	if (_emergencyNeedDetection)
	{
		// Static defense will usually add turrets. That's the first line of defense.
		// When those have started to come up, get scan.
		// The regular rules will work toward vessels in their own time.
		ADD(Rule(BWAPI::UnitTypes::Terran_Engineering_Bay, 1, Priority::Urgent));
		if (gettingUnit(BWAPI::UnitTypes::Terran_Missile_Turret) &&
			gettingUnit(BWAPI::UnitTypes::Terran_Refinery))
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Academy, 1, Priority::Urgent));
		}
	}

	if (_emergencyNeedAirDefense)
	{
		// Ensure we can make turrets. StaticDefense will build them.
		// If we going mech with no anti-air, make goliaths.
		ADD(Rule(BWAPI::UnitTypes::Terran_Engineering_Bay, 1, Priority::Urgent));
		if (isBarracksMix())
		{
			ADD(Rule(BWAPI::UnitTypes::Terran_Marine, 4, Priority::Urgent));
		}
		if (isFactoryMix())
		{
			if (gettingUnit(BWAPI::UnitTypes::Terran_Refinery))
			{
				ADD(Rule(BWAPI::UnitTypes::Terran_Goliath, 0, Priority::Urgent));
			}
		}
	}

	// if (_emergencyEnemyAttack)
	// Do nothing. Rely on the regular unit mix to make defenders.
	// NOTE SCVs are skipped in an attack emergency unless we are short of them.

	if (_emergencyNaturalContain)
	{
		ADD(Rule(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, 2));
		the.buildingPilot.land(BWAPI::UnitTypes::Terran_Factory);
	}

	// if (_emergencyEnemyDead)
	// No rules here. This sets the unit mix instead.
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

StrategyBossTerran::StrategyBossTerran()
	: _mix(Mix::None)
	, _preferInfantry(false)					// may be set in strategyDecisions()
	, _wraithHarass(false)						// may be set in strategyDecisions()
	, _emergencyNoBase(false)
	, _emergencyEnemyAttack(false)
	, _emergencyNeedDetection(false)
	, _emergencyNeedAirDefense(false)
	, _emergencyNaturalContain(false)
	, _emergencyEnemyDead(false)
{
}

#include "CombatSimulation.h"
#include "UnitUtil.h"
#include "FAP.h"

using namespace UAlbertaBot;
using namespace BWAPI;

CombatSimulation::CombatSimulation()
{
}

// sets the starting states based on the combat units within a radius of a given position
// this center will most likely be the position of the forwardmost combat unit we control
int CombatSimulation::simulateCombatFAP(const BWAPI::Unitset & ourCombatUnits, const BWAPI::Position & ourCenter, const int radius, bool canAttackGround, bool canAttackAir)
{
	// Profile debug
	//PROFILE_FUNCTION();

	fap.clearState();

	// Center the circle of interest on the nearest enemy unit, not on one of our own units.
	// It reduces indecision: Enemy actions, not our own, induce us to move.
	BWAPI::Position enemyCenter = getClosestEnemyCombatUnit(ourCenter, radius);
	if (!enemyCenter.isValid())
	{
		// Do no combat sim, leave the state empty. It's fairly common.
		// The score will be 0, which counts as a win.
		// BWAPI::Broodwar->printf("no enemy near");
		return 0;
	}

	if (Config::Debug::DrawCombatSimulationInfo)
	{
		BWAPI::Broodwar->drawCircleMap(enemyCenter.x, enemyCenter.y, 6, BWAPI::Colors::Red, true);
		BWAPI::Broodwar->drawCircleMap(enemyCenter.x, enemyCenter.y, radius, BWAPI::Colors::Red);
	}


	//Our units
	BWAPI::Unitset ourCombatUnits2;
	MapGrid::Instance().GetUnits(ourCombatUnits2, ourCenter, radius, true, false);

	for (auto & unit : ourCombatUnits2)
	{
		// Exclude workers and buildings (and static defense)
		if (unit->getType().isWorker() || unit->getType().isBuilding())
		{
			continue;
		}

		if (UnitUtil::IsCombatSimUnit(unit))
		{
			fap.addIfCombatUnitPlayer1(unit);
			if (Config::Debug::DrawCombatSimulationInfo)
			{
				BWAPI::Broodwar->drawCircleMap(unit->getPosition(), 3, BWAPI::Colors::Green, true);
			}
		}
	}


	//Enemy units
	std::vector<UnitInfo> enemyCombatUnits;
	InformationManager::Instance().getNearbyForce(enemyCombatUnits, enemyCenter, BWAPI::Broodwar->enemy(), radius);

	for (UnitInfo & ui : enemyCombatUnits)
	{
		// if unit is flying, skip it if we cannot attack it
		if (ui.type.isFlyer() && !canAttackAir)
		{
			continue;
		}

		// The check is careful about seen units and assumes that unseen units are completed and powered.
		if (ui.lastHealth > 0 &&
			(ui.unit->exists() || (ui.lastPosition.isValid() && !ui.goneFromLastPosition)) &&
			(ui.unit->exists() ? UnitUtil::IsCombatSimUnit(ui.unit) : UnitUtil::IsCombatSimUnit(ui.type)))
		{
			fap.addIfCombatUnitPlayer2(ui);
			if (Config::Debug::DrawCombatSimulationInfo)
			{
				BWAPI::Broodwar->drawCircleMap(ui.lastPosition, 3, BWAPI::Colors::Red, true);
			}
		}
	}


	//pre sim scores
	std::pair<int, int> initScore = fap.playerScores();
	std::pair<int, int> initUnitcount = { (int)fap.getState().first->size(), (int)fap.getState().second->size() };

	//short sim
	fap.simulate(24 * 4);

	//short sim scores
	std::pair<int, int> shortScore = fap.playerScores();
	std::pair<int, int> shortUnitcount = { (int)fap.getState().first->size(), (int)fap.getState().second->size() };

	//get result (my score - enemy score)
	int resultShort1 = shortScore.first - shortScore.second;

	//get result (enemy loss - my loss)
	int resultShort2 = (initScore.second - shortScore.second) - (initScore.first - shortScore.first);

	std::string simtype = "";
	std::pair<int, int> score;
	std::pair<int, int> unitcount;
	int result = -1;
	
	if (initScore.first > 1.667 * initScore.second)
	{
		simtype = "Short1";
		result = resultShort1;
		score = shortScore;
		unitcount = shortUnitcount;
	}
	else
	{
		simtype = "Short2";
		result = resultShort2;
		score = shortScore;
		unitcount = shortUnitcount;
	}

	if (Config::Debug::DrawCombatSimulationInfo)
	{
		std::stringstream ss1;
		ss1 << "Initial score: " << initScore.first << " : " << initScore.second << "\n";
		ss1 << "P1: " << initUnitcount.first << "\n";
		ss1 << "P2: " << initUnitcount.second;

		std::stringstream ss2;
		ss2 << "Sim score: " << score.first << " : " << score.second << " (" << simtype.c_str() << ")\n";
		ss2 << "P1: " << unitcount.first << "\n";
		ss2 << "P2: " << unitcount.second;

		BWAPI::Broodwar->drawTextScreen(150, 200, "%s", ss1.str().c_str());
		BWAPI::Broodwar->drawTextScreen(300, 200, "%s", ss2.str().c_str());

		std::string prefix = result < 0 ? "\x06" : "\x07";
		BWAPI::Broodwar->drawTextScreen(240, 240, "Combat Sim: %s%d", prefix.c_str(), result);
	}

	return result;
}

// Return the position of the closest enemy combat unit.
BWAPI::Position CombatSimulation::getClosestEnemyCombatUnit(const BWAPI::Position & center, int radius) const
{
	// NOTE The numbers match with Squad::unitNearEnemy().
	int closestDistance = radius + (InformationManager::Instance().enemyHasSiegeMode() ? 15 * 32 : 11 * 32);		// nothing farther than this

	BWAPI::Position closestEnemyPosition = BWAPI::Positions::Invalid;
	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		const int dist = center.getApproxDistance(ui.lastPosition);
		if (dist < closestDistance &&
			!ui.goneFromLastPosition &&
			ui.completed &&
			ui.unit->isPowered() &&
			UnitUtil::IsCombatSimUnit(ui))
		{
			closestEnemyPosition = ui.lastPosition;
			closestDistance = dist;
		}
	}

	return closestEnemyPosition;
}

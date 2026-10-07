#pragma once

#include "Common.h"
#include "MapGrid.h"
#include "InformationManager.h"

#ifdef USING_VISUALIZATION_LIBRARIES
	#include "Visualizer.h"
#endif


namespace UAlbertaBot
{
class CombatSimulation
{

	BWAPI::Player		_self = BWAPI::Broodwar->self();
	BWAPI::Player		_enemy = BWAPI::Broodwar->enemy();

public:

	CombatSimulation();

	//void setCombatUnitsFAP(const BWAPI::Position & center, const int radius);
	int simulateCombatFAP(const BWAPI::Unitset & myUnits, const BWAPI::Position & center, const int radius, bool canAttackGround, bool canAttackAir);
	BWAPI::Position getClosestEnemyCombatUnit(const BWAPI::Position & center, int radius) const;
};
}
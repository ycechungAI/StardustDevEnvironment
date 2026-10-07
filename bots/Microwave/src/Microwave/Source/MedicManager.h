#pragma once;

#include <Common.h>
#include "MicroManager.h"

namespace UAlbertaBot
{
class MedicManager : public MicroManager
{
	BWAPI::Position center = BWAPI::Position(0, 0);
	BWAPI::Unit unitClosestToEnemy;

public:

	MedicManager();
	void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);
	void setCenter(const BWAPI::Position position) { center = position; }
	void setUnitClosestToEnemy(BWAPI::Unit unit) { unitClosestToEnemy = unit; }
	void assignTargets(const BWAPI::Unitset & units, const BWAPI::Unitset & targets);
};
}
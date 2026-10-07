#pragma once;

#include <Common.h>
#include "MicroManager.h"

namespace UAlbertaBot
{
class MicroManager;

class DetectorManager : public MicroManager
{
	// For each enemy cloaked unit, it keeps a flag "have we assigned a detector to watch it?"
	std::map<BWAPI::Unit, bool>	cloakedUnitMap;

	bool isAssigned(BWAPI::Unit unit);
	BWAPI::Unit unitClosestToEnemy;
	BWAPI::Position center = BWAPI::Position(0, 0);

public:

	DetectorManager();
	~DetectorManager() {}

	void setUnitClosestToEnemy(BWAPI::Unit unit) { unitClosestToEnemy = unit; }
	void setCenter(const BWAPI::Position position) { center = position; }
	void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);

	BWAPI::Unit closestCloakedUnit(const BWAPI::Unitset & cloakedUnits, BWAPI::Unit detectorUnit);
};
}
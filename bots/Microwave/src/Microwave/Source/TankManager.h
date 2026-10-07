#pragma once;

#include <Common.h>
#include "MicroManager.h"

namespace UAlbertaBot
{
class TankManager : public MicroManager
{
public:

	TankManager();
	void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);

	BWAPI::Unit chooseTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets, std::map<BWAPI::Unit, int> & numTargeting);

	int getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target);
	BWAPI::Unit getTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets);

	void assignTargets(const BWAPI::Unitset & rangedUnits, const BWAPI::Unitset & targets);
};
}
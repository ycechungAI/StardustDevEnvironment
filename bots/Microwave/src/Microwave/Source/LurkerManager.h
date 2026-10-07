#pragma once;

#include <Common.h>
#include "MicroManager.h"

namespace UAlbertaBot
{
	class LurkerManager : public MicroManager
	{
	public:

		LurkerManager();
		void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);

		int getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target) const;
		BWAPI::Unit getTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets);

		BWAPI::Unit getNearestTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets) const;
		BWAPI::Unit getFarthestTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets) const;

		bool hiddenEnemyInRange(BWAPI::Unit lurker) const;
		bool okToUnburrow(BWAPI::Unit lurker) const;

		void assignTargets(const BWAPI::Unitset & rangedUnits, const BWAPI::Unitset & targets);
	};
}
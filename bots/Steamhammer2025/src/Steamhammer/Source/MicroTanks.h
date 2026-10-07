#pragma once

#include "MicroManager.h"

namespace UAlbertaBot
{
class MicroTanks : public MicroManager
{
private:
	const int SiegeTankRange = 12 * 32;			// pixels
	const int SiegeTankMinRange = 2 * 32;
	const int TankRange = 7 * 32;

	const int SiegeTime = 79;					// frames
	const int UnsiegeTime = 75;

	const int UnsiegedCooldown = 37;
	const int SiegedCooldown = 75;

    int nThreats(const BWAPI::Unitset & targets) const;
    bool anySiegeUnits(const BWAPI::Unitset & targets) const;
    bool allMeleeAndSameHeight(const BWAPI::Unitset & targets, BWAPI::Unit tank) const;
	bool enemyTooClose(const BWAPI::Unitset & targets, BWAPI::Unit tank) const;

public:
    MicroTanks();
    void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);

	BWAPI::Unit getSiegeTarget(BWAPI::Unit tank, const BWAPI::Unitset & targets);
	BWAPI::Unit getTankTarget(BWAPI::Unit tank, const BWAPI::Unitset & targets);
	int getAttackPriority(BWAPI::Unit target, bool toBeSieged);
};
}
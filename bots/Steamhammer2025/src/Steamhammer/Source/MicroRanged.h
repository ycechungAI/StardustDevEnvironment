#pragma once

#include "MicroManager.h"

namespace UAlbertaBot
{
class MicroRanged : public MicroManager
{
private:

	BWAPI::Unitset _vulturesLaying;		// a vulture laying a mine can't do anything else at the same time

    // Ranged ground weapon does splash damage, so it works under dark swarm.
    bool goodUnderDarkSwarm(BWAPI::UnitType type);

	void maybeCloakWraiths(const BWAPI::Unitset & rangedUnits);

	void maybeLayMines(const BWAPI::Unitset & rangedUnits);

public:

    MicroRanged();

    void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);
    void assignTargets(const BWAPI::Unitset & rangedUnits, const BWAPI::Unitset & targets);

    int getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target);
    BWAPI::Unit getTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets, bool underThreat);
};
}
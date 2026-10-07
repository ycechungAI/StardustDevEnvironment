#pragma once

#include <BWAPI.h>

namespace UAlbertaBot
{

class MicroTargetRecord
{
private:
	int findHitsToKill(BWAPI::Unit t) const;

public:
	int lastUpdateFrame;
	int hitsToKill;

	MicroTargetRecord();
	MicroTargetRecord(BWAPI::Unit t);

	void update(BWAPI::Unit t);
};

class MicroStaticDefense
{
private:
	const int MaxRange = 7 * 32;								// all controllable static defense has the same range

	BWAPI::Unitset _defenders;
	std::map<BWAPI::Unit, MicroTargetRecord> _targets;
	std::map<BWAPI::Unit, BWAPI::Unit> _attacksOut;				// defender -> target

	int pendingHits(BWAPI::Unit defender, BWAPI::Unit enemy) const;	// count other defenders that intend attacks
	BWAPI::Unit findTarget(BWAPI::Unit defender) const;

	void updateDefenders();
	void updateEnemies();

	void assignTargetsAndFire();

public:
	MicroStaticDefense();

    void update();
	void draw();
};
}
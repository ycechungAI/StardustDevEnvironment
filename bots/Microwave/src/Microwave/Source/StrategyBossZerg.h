#pragma once

#include "BuildOrderQueue.h"

namespace UAlbertaBot
{

class StrategyBossZerg
{
	StrategyBossZerg();

	// Values that cannot or should not be exceeded.
	const int absoluteMaxSupply = 400;
	const int absoluteMaxDrones = 75;

	BWAPI::Player _self;
	BWAPI::Player _enemy;
	BWAPI::Race _selfRace;
	BWAPI::Race _enemyRace;

	int _existingSupply;
	int _pendingSupply;
	int _supplyUsed;

	int _lastUpdateFrame;
	bool _emergencyGroundDefense;
	int _emergencyStartFrame;
	int _ourForceGround;
	int _enemyForceGround;
	int _enemyForceGroundIncoming;
	int _nScourgeNeeded;
	int _queueMinerals;
	int _queueGas;
	int _nDrones;
	int _nGasDrones;
	int _nMineralDrones;
	int _nMineralPatches;
	int _nGas;
	int _nHatches;
	int _nMaxDrones;
	int _nMinDrones;
	int _nLarvas;
	bool _giveUp;

	void figureSupply();
	bool vProtossDenOrSpire();
	bool vTerranDenOrSpire();

public:
	static StrategyBossZerg & Instance();

	// Called once per frame for emergencies and other urgent needs.
	void handleUrgentProductionIssues(BuildOrderQueue & queue);

	void drawStrategyInformation(int x, int y);
	bool emergencyGroundDefense();
	int numInEgg(BWAPI::UnitType) const;
};

};

#pragma once

#include "Common.h"
#include "CombatCommander.h"
#include "InformationManager.h"
#include "Cluster.h"
#include "MapGrid.h"
#include "WorkerManager.h"
#include "ProductionManager.h"
#include "BuildingManager.h"
#include "ScoutManager.h"
#include "StrategyManager.h"
#include "StrategyBOSSZerg.h"
#include "TimerManager.h"

namespace UAlbertaBot
{

class UnitToAssign
{
public:

	BWAPI::Unit unit;
	bool isAssigned;

	UnitToAssign(BWAPI::Unit u)
	{
		unit = u;
		isAssigned = false;
	}
};

class GameCommander 
{
	CombatCommander		    _combatCommander;
	TimerManager		    _timerManager;

	BWAPI::Unitset          _validUnits;
	BWAPI::Unitset          _combatUnits;
	BWAPI::Unitset          _scoutUnits;

	int						_surrenderTime;		// for giving up early
	int						_minRemainingLatencyFrames;
	int						_countFrameLong;

	bool                    _initialScoutSet;	// 0 until a scouting worker is assigned
	bool					_scoutAlways;
	bool					_scoutIfNeeded;
	int						_scoutOnMinerals;

    void                    assignUnit(BWAPI::Unit unit, BWAPI::Unitset & set);
	bool                    isAssigned(BWAPI::Unit unit) const;

	bool					surrenderMonkey();

public:

	GameCommander();
	~GameCommander() {};

	void update();

	void handleUnitAssignments();
	void setValidUnits();
	void setScoutUnits();
	void setCombatUnits();
	void setScoutOnMinerals(int amount);

	void releaseOverlord(BWAPI::Unit overlord);     // zerg scouting overlord

	void goScoutAlways();
	void goScoutIfNeeded();

	void drawDebugInterface();
    void drawGameInformation(int x, int y);
	void drawUnitOrders();

	BWAPI::Unit getFirstSupplyProvider();
	// BWAPI::Unit getClosestUnitToTarget(BWAPI::UnitType type, BWAPI::Position target);
	BWAPI::Unit getAnyFreeWorker();

	void onUnitShow(BWAPI::Unit unit);
	void onUnitHide(BWAPI::Unit unit);
	void onUnitCreate(BWAPI::Unit unit);
	void onUnitComplete(BWAPI::Unit unit);
	void onUnitRenegade(BWAPI::Unit unit);
	void onUnitDestroy(BWAPI::Unit unit);
	void onUnitMorph(BWAPI::Unit unit);

	static GameCommander & Instance();
};

}
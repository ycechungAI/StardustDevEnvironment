#pragma once

#include <Common.h>
#include "BuildingManager.h"
#include "WorkerData.h"

namespace UAlbertaBot
{
class Building;

class WorkerManager
{
    WorkerData  workerData;
    BWAPI::Unit previousClosestWorker;
	BWAPI::Unit previousClosestBlockingMinWorker;
	bool		_collectGas;
	BWAPI::Unitset busy;    // units with special orders this frame

	bool        isBusy(BWAPI::Unit worker) const;
	void        makeBusy(BWAPI::Unit worker);

	void        setMineralWorker(BWAPI::Unit unit);
	void        setReturnCargoWorker(BWAPI::Unit unit);
	bool		refineryHasDepot(BWAPI::Unit refinery);
	bool        isGasStealRefinery(BWAPI::Unit unit);

	void        handleGasWorkers();
	void        handleIdleWorkers();
	void		handleReturnCargoWorkers();
	void        handleRepairWorkers();
    void        handleMoveWorkers();
	void		handleMineralLocking();

	BWAPI::Unit findEnemyTargetForWorker(BWAPI::Unit worker) const;
	BWAPI::Unit findEscapeMinerals(BWAPI::Unit worker) const;
	bool		defendSelf(BWAPI::Unit worker, BWAPI::Unit resource);

    WorkerManager();

public:

    void        update();
    void        onUnitDestroy(BWAPI::Unit unit);
    void        onUnitMorph(BWAPI::Unit unit);
    void        onUnitShow(BWAPI::Unit unit);
    void        onUnitRenegade(BWAPI::Unit unit);

    void        finishedWithWorker(BWAPI::Unit unit);
   // void        finishedWithCombatWorkers();

    void        drawResourceDebugInfo();
    void        updateWorkerStatus();
    void        drawWorkerInformation(int x,int y);

    int         getNumMineralWorkers() const;
    int         getNumGasWorkers() const;
    int         getNumIdleWorkers() const;
	int         getNumReturnCargoWorkers() const;
	int			getNumCombatWorkers() const;
	int			getMaxWorkers() const;

	bool		isCollectingGas()              { return _collectGas; };
	void		setCollectGas(bool collectGas) { _collectGas = collectGas; };

    bool        isWorkerScout(BWAPI::Unit worker);
	bool		isCombatWorker(BWAPI::Unit worker);
    bool        isFree(BWAPI::Unit worker);
    bool        isBuilder(BWAPI::Unit worker);

    BWAPI::Unit getBuilder(const Building & b);
    BWAPI::Unit getMoveWorker(BWAPI::Position p);
	BWAPI::Unit getAnyClosestDepot(BWAPI::Unit worker);
    BWAPI::Unit getNonFullClosestDepot(BWAPI::Unit worker);
    BWAPI::Unit getGasWorker(BWAPI::Unit refinery);
    BWAPI::Unit getClosestEnemyUnit(BWAPI::Unit worker);
	BWAPI::Unit getBestEnemyUnit(BWAPI::Unit worker);
    BWAPI::Unit getClosestMineralWorkerTo(BWAPI::Unit enemyUnit);
	BWAPI::Unit getClosestBlockingMinWorkerTo(BWAPI::Unit enemyUnit);
    BWAPI::Unit getWorkerScout();

	void        setScoutWorker(BWAPI::Unit worker);
    void        setBuildingWorker(BWAPI::Unit worker, BWAPI::UnitType buildingType);
    void        setRepairWorker(BWAPI::Unit worker,BWAPI::Unit unitToRepair);
    void        stopRepairing(BWAPI::Unit worker);
	void        setMoveWorker(BWAPI::Unit worker, int mineralsNeeded, int gasNeeded, BWAPI::Position & p);
    void        setCombatWorker(BWAPI::Unit worker);

    bool        willHaveResources(int mineralsRequired,int gasRequired,double distance);
    void        rebalanceWorkers();
	bool		maybeMineMineralBlocks(BWAPI::Unit worker);

	void		handleBlockingMineralsWorkers();
	bool		handleBlockingMineralsBuilders(BWAPI::Unit worker);

    static WorkerManager &  Instance();
};
}
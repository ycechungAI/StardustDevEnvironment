#pragma once

#include "Common.h"

namespace UAlbertaBot
{
class Base;					// forward declaration
enum class MacroLocation;

class WorkerData
{
public:

// Keep track of where workers are posted.
// The map position of a given macro location may change, so remember the original postion too.
class WorkerPost
{
public:
    WorkerPost();
    WorkerPost(MacroLocation loc);

    MacroLocation location;
    BWAPI::Position position;
};

enum WorkerJob
    { Minerals
    , Gas
    , Build
    , Combat
    , Idle          // default job, job when burrowed, etc.
	, Flee			// run away from an enemy
    , Repair
    , Scout
    , ReturnCargo
	, Transfer		// currently unused
    , Unblock       // mine out blocking minerals
    , Posted        // posted to a location
    , PostedBuild   // posted to a location, assigned to construct there
};

private:

    BWAPI::Unitset workers;

    std::map<BWAPI::Unit, enum WorkerJob>	workerJobMap;           // worker -> job
    std::map<BWAPI::Unit, BWAPI::Unit>		workerDepotMap;         // worker -> resource depot (hatchery, etc.)
    std::map<BWAPI::Unit, BWAPI::Unit>		workerRefineryMap;      // worker -> refinery
    std::map<BWAPI::Unit, BWAPI::Unit>		workerRepairMap;        // worker -> unit to repair
    std::map<BWAPI::Unit, BWAPI::TilePosition> workerUnblockMap;    // worker -> blocking mineral to mine out
    std::map<BWAPI::Unit, WorkerPost>       workerPostMap;          // worker -> assigned post

    std::map<BWAPI::Unit, int>				depotWorkerCount;       // mineral workers per depot
    std::map<BWAPI::Unit, int>				refineryWorkerCount;    // gas workers per refinery

    std::map<BWAPI::Unit, int>				workersOnMineralPatch;  // count workers per mineral patch
    std::map<BWAPI::Unit, BWAPI::Unit>		workerMineralAssignment;// worker -> mineral patch

    void			clearPreviousJob(BWAPI::Unit unit);
    BWAPI::Unit		getMineralToMine(BWAPI::Unit worker, Base * base);

public:

    WorkerData();

    void			workerDestroyed(BWAPI::Unit unit);
    void			removeDepot(BWAPI::Unit unit);
    void			addWorker(BWAPI::Unit unit);
    void			addWorker(BWAPI::Unit unit, WorkerJob job, BWAPI::Unit jobUnit);
    void			setWorkerJob(BWAPI::Unit unit, WorkerJob job, BWAPI::Unit jobUnit);
    void			setWorkerJob(BWAPI::Unit unit, WorkerJob job);
    void			setWorkerJob(BWAPI::Unit unit, const BWAPI::TilePosition & tile);
	void			setWorkerJobMinerals(BWAPI::Unit unit, Base * base);

    void			setWorkerPost(BWAPI::Unit unit, MacroLocation loc);
    void            resetWorkerPost(BWAPI::Unit unit, WorkerJob job);

    int				getNumWorkers() const;
	int				getNumWorkersWithJob(WorkerJob job) const;
    int				getNumMineralWorkers() const;
    int				getNumGasWorkers() const;
    int				getNumReturnCargoWorkers() const;
    int				getNumCombatWorkers() const;
    int             getNumRepairWorkers() const;
    int				getNumIdleWorkers() const;
    int				getNumPostedWorkers() const;
    bool            anyUnblocker() const;

    //void			getMineralWorkers(std::set<BWAPI::Unit> & mw);
    void			getGasWorkers(std::set<BWAPI::Unit> & mw);
    //void			getBuildingWorkers(std::set<BWAPI::Unit> & mw);
    //void			getRepairWorkers(std::set<BWAPI::Unit> & mw);
    
	bool			baseIsFull(Base * base) const;
    int				getMineralsNearDepot(BWAPI::Unit depot);

    int				getNumAssignedWorkers(BWAPI::Unit unit) const;

    WorkerJob		getWorkerJob(BWAPI::Unit unit) const;
    BWAPI::Unit		getWorkerResource(BWAPI::Unit unit);
    BWAPI::Unit		getWorkerDepot(BWAPI::Unit unit);
    BWAPI::Unit		getWorkerRepairUnit(BWAPI::Unit unit);
    BWAPI::TilePosition getWorkerTile(BWAPI::Unit unit);

    MacroLocation   getWorkerPostLocation(BWAPI::Unit unit);
    BWAPI::Position getWorkerPostPosition(BWAPI::Unit unit);

    BWAPI::Unitset  getMineralPatchesNearDepot(BWAPI::Unit depot);
    void            addToMineralPatch(BWAPI::Unit unit, int num);

    char			getJobCode(BWAPI::Unit unit) const;
    char			getJobCode(WorkerJob job) const;
    void			drawDepotDebugInfo();

    const BWAPI::Unitset &
                    getWorkers() const { return workers; }

};
}
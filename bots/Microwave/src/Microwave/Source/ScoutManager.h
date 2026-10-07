#pragma once

#include "Common.h"
#include "MicroManager.h"
#include "InformationManager.h"

namespace UAlbertaBot
{
class ScoutManager 
{
	BWAPI::Unit						_workerScout;
	BWAPI::Unit						_overlordScout;
	BWAPI::TilePosition				_overlordScoutTarget;
	BWAPI::Position					_overlordAtBaseTarget;
	BWAPI::TilePosition				_workerScoutTarget;    // only while still seeking the enemy base
    std::string                     _scoutStatus;
    std::string                     _gasStealStatus;
	int				                _numWorkerScouts;
	bool							_scoutLocationOnly;
	bool							_scoutOnceOnly;
	bool			                _scoutUnderAttack;
	bool							_overlordAtEnemyBase;

	bool							_tryGasSteal;
	bool							_startedGasSteal;
    bool                            _didGasSteal;
    bool                            _gasStealFinished;

    int                             _currentRegionVertexIndex;
    int                             _previousScoutHP;
	std::vector<BWAPI::Position>    _enemyRegionVertices;
	bool							_overlordClosestBase;

	void							setScoutTargets();
	bool                            enemyWorkerInRadius();
    bool			                immediateThreat();
    bool                            gasSteal();
    int                             getClosestVertexIndex(BWAPI::Unit unit);
    BWAPI::Position                 getFleePosition();
	BWAPI::Unit						getAnyEnemyGeyser() const;
	BWAPI::Unit						getEnemyGeyser();
	BWAPI::Unit						enemyWorkerToHarass();
    void                            followPerimeter();
	void                            moveGroundScout();
    void                            calculateEnemyRegionVertices();
	void                            moveAirScout();
	void							moveAirScoutAroundEnemyBase();
	void                            releaseOverlordScout();
	void                            drawScoutInformation(int x, int y);

	ScoutManager();

public:

    static ScoutManager & Instance();

	void update();

    void setWorkerScout(BWAPI::Unit unit);
	void setOverlordScout(BWAPI::Unit unit);
	BWAPI::Unit getWorkerScout() const { return _workerScout; };
	void releaseWorkerScout();
	void setScoutLocationOnly();
	void setScoutOnceOnly();
	void setGasSteal();
	void gasStealOver() { _gasStealFinished = true; };    // called by BuildingManager when releasing the worker
	void goOverlordClosestBase();
};
}
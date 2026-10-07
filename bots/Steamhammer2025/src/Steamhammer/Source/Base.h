#pragma once

#include "Common.h"
#include "GridAirDistances.h"
#include "GridDistances.h"
#include "GridSafePath.h"

namespace UAlbertaBot
{
class Base
{
private:

    // Resources within this ground distance (in tiles) are considered to belong to this base.
    static const int BaseResourceRange = 13;

    int					id;					// ID number for drawing base info

    BWAPI::TilePosition	tilePosition;		// upper left corner of the resource depot spot
    BWAPI::Unitset		minerals;			// the base's mineral patches (some may be mined out)
    BWAPI::Unitset		initialGeysers;		// initial units of the geysers (taking one changes the unit)
    BWAPI::Unitset		geysers;			// the base's current associated geysers
    BWAPI::Unitset		blockers;			// destructible neutral units that may be in the way
    const GridDistances distances;			// ground distances from tilePosition
	GridSafePathGround  groundSafePaths;    // safe ground paths from tilePosition
	GridSafePathAir     airSafePaths;       // safe air paths from tilePosition
	bool				startingBase;		// is this one of the map's starting bases?
    Base *              naturalBase;        // if a starting base, the base's natural if any; else null
    Base *              mainBase;           // if the natural of a starting base, the corresponding main; else null
    BWAPI::TilePosition front;              // the front line: place approach defenses near here
    BWAPI::Position     mineralOffset;      // mean offset of minerals from the center of the depot

	bool				notTheEnemyStart;	// code somewhere verified that the enemy did not start here
	int					enemyStartEvidence;	// add up signs that this may be the enemy starting base
	bool				reserved;			// if this is a planned expansion
    int					failedPlacements;	// count building placements that failed
	int					buildingsCost;		// sum of minerals+gas for all our buildings

	bool				underAttack;		// for our own bases only; false for others
	bool                overlordDanger;     // for our own bases only; false for others
	bool				workerDanger;		// for our own bases only; false for others
	bool                doomed;             // for our own bases only; false for others

    bool                findIsStartingBase() const;	// to initialize the startingBase flag
    BWAPI::TilePosition findFront() const;
    BWAPI::Position     findMineralOffset() const;

public:

    // The resourceDepot pointer is set for a base if the depot has been seen.
    // It is possible to infer a base location without seeing the depot.
    BWAPI::Unit		resourceDepot;			// hatchery, etc., or null if none
    BWAPI::Player	owner;					// self, enemy, neutral

    Base(BWAPI::TilePosition pos, const BWAPI::Unitset & availableResources);
    void setID(int baseID);                 // called exactly once at startup

    void initializeNatural(const std::vector<Base *> & bases);
    void initializeFront();

    int				getID()           const { return id; };
    BWAPI::Unit		getDepot()        const { return resourceDepot; };
    BWAPI::Player	getOwner()        const;
    bool            isAStartingBase() const { return startingBase; };
    bool            isIsland()        const;
    Base *          getNatural()      const { return naturalBase; };
    Base *          getMain()         const { return mainBase; };
    bool            isCompleted()     const;
	bool			isMyBase()        const;		// possibly under construction
    bool            isMyCompletedBase() const;
    bool            isInnerBase()     const;
	bool			isBuildable()     const;		// we can build there: has pylon or creep
	bool			hasEnemyDefense() const;		// enemy bunker, cannon, sunken

    BWAPI::Position getFront()         const { return TileCenter(front); };
    BWAPI::TilePosition getFrontTile() const { return front; };

    void updateGeysers();

    const BWAPI::TilePosition & getTilePosition() const { return tilePosition; };
    BWAPI::Position getPosition() const { return BWAPI::Position(tilePosition); };
    BWAPI::TilePosition getCenterTile() const;
    BWAPI::Position getCenter() const;
    BWAPI::TilePosition getMineralLineTile() const;
	BWAPI::TilePosition getAntiMineralLineTile() const;

    // Various distances.
    const GridDistances & getDistances() { return distances; };
	int getTileDistance(const BWAPI::TilePosition & pos) const { return distances.at(pos); };
	int getTileDistance(const BWAPI::Position & pos) const { return distances.at(pos); };
    int getDistance(const BWAPI::TilePosition & pos) const { return 32 * getTileDistance(pos); };
    int getDistance(const BWAPI::Position & pos) const { return 32 * getTileDistance(pos); };

	int getAirTileDistance(const BWAPI::TilePosition & pos) const;
	int getAirTileDistance(const BWAPI::Position & pos) const;
	int getAirDistance(const BWAPI::TilePosition & pos) const;
	int getAirDistance(const BWAPI::Position & pos) const;

	GridSafePathGround & getGroundSafePaths() { return groundSafePaths; };
	int getSafeTileDistance(const BWAPI::TilePosition & pos) { return getGroundSafePaths().at(pos); };
	int getSafeTileDistance(const BWAPI::Position & pos) { return getGroundSafePaths().at(pos); };
	int getSafeDistance(const BWAPI::TilePosition & pos) { return 32 * getSafeTileDistance(pos); };
	int getSafeDistance(const BWAPI::Position & pos) { return 32 * getSafeTileDistance(pos); };

	GridSafePathAir & getAirSafePaths() { return airSafePaths; };
	int getAirSafeTileDistance(const BWAPI::TilePosition & pos) { return getAirSafePaths().at(pos); };
	int getAirSafeTileDistance(const BWAPI::Position & pos) { return getAirSafePaths().at(pos); };
	int getAirSafeDistance(const BWAPI::TilePosition & pos) { return 32 * getAirSafeTileDistance(pos); };
	int getAirSafeDistance(const BWAPI::Position & pos) { return 32 * getAirSafeTileDistance(pos); };

	int getSafeTileDistance(bool air, const BWAPI::TilePosition & pos)
		{ return air ? getAirSafeTileDistance(pos) : getSafeTileDistance(pos); };
	int getSafeTileDistance(bool air, const BWAPI::Position & pos)
		{ return air ? getAirSafeTileDistance(pos) : getSafeTileDistance(pos); };

	void setOwner(BWAPI::Unit depot, BWAPI::Player player);
    void setInferredEnemyBase();
    void placementFailed() { ++failedPlacements; };
    int  getFailedPlacements() const { return failedPlacements; };

    // The mineral patch units and geyser units. Blockers prevent the base from being used efficiently.
    const BWAPI::Unitset & getMinerals() const { return minerals; };	// mined out or not
    const BWAPI::Unitset   getRemainingMinerals() const;				// only those with minerals left
	const BWAPI::Unitset   getRemainingSafeMinerals() const;			// excluding those in danger from the enemy
    const BWAPI::Unitset & getInitialGeysers() const { return initialGeysers; };
    const BWAPI::Unitset & getGeysers() const { return geysers; };
    const BWAPI::Unitset & getBlockers() const { return blockers; }

    // The latest reported total of resources available.
    int getLastKnownMinerals() const;
	int getLastKnownGas() const;

    // The total initial resources available.
    int getInitialMinerals() const;
    int getInitialGas() const;

    // Workers assigned to mine minerals or gas.
    int getMaxMineralWorkers() const;
    int getMaxWorkers() const;
    int getNumMineralWorkers() const;
    int getNumWorkers() const;

    int getNumUnits(BWAPI::UnitType type) const;
	int getNumEnemyNondefenseBuildings() const;
	bool isInBase(const BWAPI::TilePosition & tile) const;
	bool isInBase(const BWAPI::Position & pos) const { return isInBase(BWAPI::TilePosition(pos)); };
	bool isInBase(BWAPI::Unit u) const { return isInBase(u->getTilePosition()); };

    const BWAPI::Position & getMineralOffset() const { return mineralOffset; };

    BWAPI::Unit getPylon() const;

	void setNotTheEnemyStart() { notTheEnemyStart = true; };
	void addEnemyStartEvidence(int e) { enemyStartEvidence += e; };
	int getEnemyStartEvidence() const { return enemyStartEvidence; };
    bool isExplored() const;
    bool isVisible() const;

    void reserve() { reserved = true; };
    void unreserve() { reserved = false; };
    bool isReserved() const { return reserved; };

    // Outside tactical analysis decides this. See CombatCommander::updateBaseDefenseSquads().
	void setUnderAttack(bool attack) { underAttack = attack; };
	bool isUnderAttack() const { return underAttack; };
    void setOverlordDanger(bool attack) { overlordDanger = attack; };
    bool inOverlordDanger() const { return overlordDanger; };
    void setWorkerDanger(bool attack) { workerDanger = attack; };
    bool inWorkerDanger() const { return workerDanger; };
    void setDoomed(bool bad) { doomed = bad; };
    bool isDoomed() const { return doomed; };
	bool bewareEnemyGroundAttackers() const;

    void clearBlocker(BWAPI::Unit blocker);

	void updateBuildingsCost();
	int getBuildingsCost() const { return buildingsCost; };

    void drawBaseInfo() const;
};

}
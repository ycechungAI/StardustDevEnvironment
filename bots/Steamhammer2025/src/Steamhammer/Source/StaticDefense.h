#pragma once

// Plan and construct static defense buildings for any race.

#include <BWAPI.h>
#include <vector>

namespace UAlbertaBot
{

// Forward declaration.
class Base;

// NOTE The plan doesn't include shield batteries. Not supported yet.
struct StaticDefensePlan
{
	// Our ground defenses desired.
    // Count of bunkers, cannons, or sunken colonies at different sets of places.
    // An "inner base" is one the enemy cannot reach by ground without passing an outer base.
    int atInnerBases;
    int atOuterBases;
    int atFront;			// front line only (a specific outer base)
	bool turretBehindBunker;	// for detection versus dark templar - behind to protect the turret
	bool turretBesideBunker;	// for detection versus lurkers - beside because lurkers have range
    //int atProxy;			// proxy location NOT IMPLEMENTED

	// Our air defenses desired.
	// Count of turrets or spore colonies. Not used for protoss (cannons are dual-purpose).
    // If airPerBase is true, then antiAir gives the count wanted for each base.
    // If false, the total count, one at each of this many bases.
	// Currently, only terran turrets pay attention to airStrongPoint.
    bool airIsPerBase;
	bool airStrongpoint;		// one strongpoint or spread-out defenses
    int antiAir;

    StaticDefensePlan();
};

class StaticDefense
{
    StaticDefensePlan _plan;
    BWAPI::UnitType _ground;
    BWAPI::UnitType _groundPrereq;
    BWAPI::UnitType _air;
    BWAPI::UnitType _airPrereq;
    std::vector<Base *> _airBases;

	const int _MinWorkerLimit;					// add additional workers below this limit

    void limitZergDefenses();

    void planTP();

    int analyzeGroundDefensesZvT() const;
    int analyzeGroundDefensesZvP() const;
    int analyzeGroundDefensesZvZ() const;
    void planZ();

    void plan();

    void startBuilding(BWAPI::UnitType building, const BWAPI::TilePosition & tile) const;
    bool morphStrandedCreeps(BWAPI::UnitType type);
	bool isSafelyBuildable(Base * base) const;
    bool startFrontGroundBuildings();
	bool bunkerNeedsTurret();
    bool startOtherGroundBuildings();
    bool buildGround();

    void chooseAirBases();
	BWAPI::TilePosition terranSpreadTurretPosition(Base * base) const;
    void startAirBuilding();
    void buildAir();

    bool alreadyBuilding(BWAPI::UnitType type);
    bool needPrerequisite(BWAPI::UnitType prereq);
    void build();

    void drawGroundCount(Base * base, int count) const;
    void drawAirCount(Base * base, int count) const;
    void draw() const;

public:

    StaticDefense();

    void update();
};

};

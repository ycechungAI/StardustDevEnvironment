#pragma once

#include "Common.h"

namespace UAlbertaBot
{
struct UnitInfo
{
    // Keep track of units which are out of sight.

    int             unitID;
    int				updateFrame;
    int             lastHP;
    int             lastShields;
    BWAPI::Player   player;
    BWAPI::Unit     unit;
    BWAPI::Position lastPosition;
	v2				lastVelocity;
    bool			goneFromLastPosition;   // last position was seen, and it wasn't there
    bool			burrowed;               // believed to be burrowed (or burrowing) at this position
    bool            lifted;                 // lifted terran building when last seen
    bool            powered;                // powered when last seen
    BWAPI::UnitType type;

    // NOTE completeBy controls isCompleted(), which predicts whether a unit is complete
    //      by now. It's usually what you want to use instead of completed. When the code
    //      can't estimate completeBy accurately, it deliberately uses an overestimate.
    //      So when isCompleted() returns true, it's almost always right.
    int				completeBy;				// past frame known or future frame predicted
    bool            completed;              // actually seen in a completed state

    UnitInfo();
    UnitInfo(BWAPI::Unit unit);

    bool operator == (BWAPI::Unit u) const;
    bool operator == (const UnitInfo & rhs) const;
    bool operator < (const UnitInfo & rhs) const;

    int estimateHP() const;
    int estimateShields() const;
    int estimateHealth() const;

    // Predicted to be completed by now. Prefer this over .completed for most purposes.
    bool isCompleted() const { return completeBy <= BWAPI::Broodwar->getFrameCount(); };

    int predictCompletion() const;

	bool isImmobile() const { return type.isBuilding() && !lifted || burrowed || type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode; };

	// Calculate where to send my unit u to intercept this enemy unit.
	BWAPI::Position intercept(BWAPI::Unit u) const;
	void interceptInfo(BWAPI::Unit u, BWAPI::Position & pos, double & t) const;
};

typedef std::vector<UnitInfo> UnitInfoVector;
typedef std::map<BWAPI::Unit, UnitInfo> UIMap;

class UnitData
{
    UIMap unitMap;

    const bool			badUnitInfo(const UnitInfo & ui) const;

    std::vector<int>	numDeadUnits;   // how many lost

    int					mineralsLost;
    int					gasLost;

public:

    UnitData();

    void				updateGoneFromLastPosition();

    void				updateUnit(BWAPI::Unit unit);
    void				removeUnit(BWAPI::Unit unit);
    void				removeBadUnits();

    int					getGasLost()                                const;
    int					getMineralsLost()                           const;
    int					getNumDeadUnits(BWAPI::UnitType t)          const;
    const std::map<BWAPI::Unit,UnitInfo> & getUnits()				const;
	const UnitInfo *	getUnitInfo(BWAPI::Unit)					const;
};
}
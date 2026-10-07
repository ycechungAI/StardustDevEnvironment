#pragma once

#include "Common.h"
#include "UnitUtil.h"

namespace UAlbertaBot
{
struct UnitInfo
{
    // we need to store all of this data because if the unit is not visible, we
    // can't reference it from the unit pointer

    int             unitID;
	int				updateFrame;
    int             lastHealth;
    int             lastShields;
	int             lastEnergy;
    BWAPI::Player   player;
    BWAPI::Unit     unit;
    BWAPI::Position lastPosition;
	int             lastRangeAir;
	int             lastRangeGround;
	bool			goneFromLastPosition;   // last position was seen, and it wasn't there
	bool			burrowed;               // believed to be burrowed (or burrowing) at this position
	bool            lifted;                 // lifted terran building when last seen
    BWAPI::UnitType type;
	int				distance;
    bool            completed;
	int				completedFrame;
	int             lastMoveFrame;
	BWAPI::Position lastOrderPosition;
	BWAPI::Position lastMovePosition;
	BWEB::Path		lastOrderPath;
	BWEB::Path		lastMovePath;
	BWAPI::Position lastPathPosition;
	BWEM::CPPath	lastOrderPathBWEM;
	int				lastRetreatSwitchFrame;
	bool			lastRetreatVal;

	UnitInfo()
		: unitID(0)
		, updateFrame(0)
		, lastHealth(0)
		, lastShields(0)
		, lastEnergy(0)
		, player(nullptr)
		, unit(nullptr)
		, lastPosition(BWAPI::Positions::None)
		, lastRangeAir(0)
		, lastRangeGround(0)
		, burrowed(false)
		, lifted(false)
		, goneFromLastPosition(false)
		, type(BWAPI::UnitTypes::None)
		, completed(false)
		, completedFrame(0)
		, lastMoveFrame(0)
		, lastOrderPosition(BWAPI::Positions::None)
		, lastMovePosition(BWAPI::Positions::None)
		, lastPathPosition(BWAPI::Positions::None)
		, lastRetreatSwitchFrame(0)
		, lastRetreatVal(0)
	{
	}

	UnitInfo(BWAPI::Unit unit)
		: unitID(unit->getID())
		, updateFrame(BWAPI::Broodwar->getFrameCount())
		, lastHealth(unit->getHitPoints())
		, lastShields(unit->getShields())
		, lastEnergy(unit->getEnergy())
		, player(unit->getPlayer())
		, unit(unit)
		, lastPosition(unit->getPosition())
		, lastRangeAir(UnitUtil::GetAttackRangeAssumingUpgrades(unit->getType(), BWAPI::UnitTypes::Zerg_Mutalisk))
		, lastRangeGround(UnitUtil::GetAttackRangeAssumingUpgrades(unit->getType(), BWAPI::UnitTypes::Zerg_Zergling))
		, goneFromLastPosition(false)
		, burrowed(unit->isBurrowed() || unit->getOrder() == BWAPI::Orders::Burrowing)
		, lifted(unit->isLifted() || unit->getOrder() == BWAPI::Orders::LiftingOff)
		, type(unit->getType())
		, completed(unit->isCompleted())
		, completedFrame(0)
		, lastMoveFrame(0)
		, lastOrderPosition(BWAPI::Positions::None)
		, lastMovePosition(BWAPI::Positions::None)
		, lastPathPosition(BWAPI::Positions::None)
		, lastRetreatSwitchFrame(BWAPI::Broodwar->getFrameCount())
		, lastRetreatVal(0)
	{
	}

    const bool operator == (BWAPI::Unit unit) const
    {
        return unitID == unit->getID();
    }

    const bool operator == (const UnitInfo & rhs) const
    {
        return (unitID == rhs.unitID);
    }

    const bool operator < (const UnitInfo & rhs) const
    {
        return (unitID < rhs.unitID);
    }
};

typedef std::vector<UnitInfo> UnitInfoVector;
typedef std::map<BWAPI::Unit,UnitInfo> UIMap;

class UnitData
{
    UIMap unitMap;

    const bool			badUnitInfo(const UnitInfo & ui) const;

    std::vector<int>	numDeadUnits;
    std::vector<int>	numUnits;

    int					mineralsLost;
    int					gasLost;

public:

    UnitData();

	void	updateGoneFromLastPosition();

    void	updateUnit(BWAPI::Unit unit);
    void	removeUnit(BWAPI::Unit unit);
    void	removeBadUnits();

    int		getGasLost()                                const;
    int		getMineralsLost()                           const;
    int		getNumUnits(BWAPI::UnitType t)              const;
    int		getNumDeadUnits(BWAPI::UnitType t)          const;
    const	std::map<BWAPI::Unit,UnitInfo> & getUnits() const;

	void	setLastOrderPath(BWAPI::Unit unit, BWEB::Path path);
	void	setLastOrderPosition(BWAPI::Unit unit, BWAPI::Position position);
	void	setLastMovePath(BWAPI::Unit unit, BWEB::Path path);
	void	setLastMovePosition(BWAPI::Unit unit, BWAPI::Position position);
	void	setLastPathPosition(BWAPI::Unit unit, BWAPI::Position position);
	void	setLastOrderPathBWEM(BWAPI::Unit unit, BWEM::CPPath path);
	void	setLastMoveFrame(BWAPI::Unit unit, int frame);
	void	setLastRetreatSwitchFrame(BWAPI::Unit unit, int frame);
	void	setLastRetreatVal(BWAPI::Unit unit, bool retreatVal);
};
}
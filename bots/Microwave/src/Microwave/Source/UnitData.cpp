#include "Common.h"
#include "UnitData.h"

using namespace UAlbertaBot;

UnitData::UnitData() 
	: mineralsLost(0)
	, gasLost(0)
{
	int maxTypeID(0);
	for (const BWAPI::UnitType & t : BWAPI::UnitTypes::allUnitTypes())
	{
		maxTypeID = maxTypeID > t.getID() ? maxTypeID : t.getID();
	}

	numDeadUnits	    = std::vector<int>(maxTypeID + 1, 0);
	numUnits		    = std::vector<int>(maxTypeID + 1, 0);
}

// An enemy unit which is not visible, but whose lastPosition can be seen, is known
// not to be at its lastPosition. Flag it.
// Called from InformationManager with the enemy UnitData.
void UnitData::updateGoneFromLastPosition()
{
	for (auto & kv : unitMap)
	{
		UnitInfo & ui(kv.second);

		if (!ui.goneFromLastPosition &&
			ui.lastPosition.isValid() &&   // should be always true
			ui.unit &&                     // should be always true
			!ui.unit->isVisible() &&
			BWAPI::Broodwar->isVisible(BWAPI::TilePosition(ui.lastPosition)))
		{
			ui.goneFromLastPosition = true;
		}
	}
}

void UnitData::updateUnit(BWAPI::Unit unit)
{
	if (!unit) { return; }
	if (!unit->isVisible()) { return; }

	if (unitMap.find(unit) == unitMap.end())
	{
		++numUnits[unit->getType().getID()];
		unitMap[unit] = UnitInfo();
	}

	UnitInfo & ui = unitMap[unit];

	ui.unitID = unit->getID();
	ui.updateFrame = BWAPI::Broodwar->getFrameCount();
	ui.lastHealth = unit->getHitPoints();
	ui.lastShields = unit->getShields();
	ui.lastEnergy = unit->getEnergy();
	ui.player = unit->getPlayer();
	ui.unit = unit;
	ui.lastPosition = unit->getPosition();
	ui.goneFromLastPosition = false;
	ui.burrowed = unit->isBurrowed() || unit->getOrder() == BWAPI::Orders::Burrowing;
	ui.lifted = unit->isLifted() || unit->getOrder() == BWAPI::Orders::LiftingOff;
	ui.type = unit->getType();

	int rangeAir = UnitUtil::GetAttackRangeAssumingUpgrades(unit->getType(), BWAPI::UnitTypes::Zerg_Mutalisk);
	int rangeGround = UnitUtil::GetAttackRangeAssumingUpgrades(unit->getType(), BWAPI::UnitTypes::Zerg_Zergling);
	if (rangeAir > ui.lastRangeAir) ui.lastRangeAir = rangeAir;
	if (rangeGround > ui.lastRangeGround) ui.lastRangeGround = rangeGround;

	if (!ui.completed && unit->isCompleted())
	{
		ui.completedFrame = BWAPI::Broodwar->getFrameCount();
	}
	ui.completed = unit->isCompleted();
}

void UnitData::removeUnit(BWAPI::Unit unit)
{
	if (!unit) { return; }

	mineralsLost += unit->getType().mineralPrice();
	gasLost += unit->getType().gasPrice();
	numUnits[unit->getType().getID()]--;
	numDeadUnits[unit->getType().getID()]++;
		
	unitMap.erase(unit);
}

void UnitData::removeBadUnits()
{
	for (auto iter(unitMap.begin()); iter != unitMap.end();)
	{
		if (badUnitInfo(iter->second))
		{
			numUnits[iter->second.type.getID()]--;
			iter = unitMap.erase(iter);
		}
		else
		{
			iter++;
		}
	}
}

const bool UnitData::badUnitInfo(const UnitInfo & ui) const
{
    if (!ui.unit)
    {
        return false;
    }

	// Cull away any refineries/assimilators/extractors that were destroyed and reverted to vespene geysers
	if (ui.unit->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser)
	{ 
		return true;
	}

	// The owner can change in some situations:
	// - The unit is a refinery building and was destroyed, reverting to a neutral vespene geyser.
	// - The unit was mind controlled.
	// - The unit is a command center and was infested.
	if (ui.unit->isVisible() && ui.unit->getPlayer() != ui.player)
	{
		return true;
	}

	// The unit is a building and we can currently see its position and it is not there.
	// It may have burned down, or the enemy may have chosen to destroy it.
	// Or it may have been destroyed by splash damage while out of our sight.
	// NOTE A terran building could have lifted off and moved away while out of our vision.
	//      In that case, we mistakenly drop it.
	//      Not a fatal problem; we'll re-add it when we see it again. Loss counting will be wrong.	
	if (ui.type.isBuilding() && BWAPI::Broodwar->isVisible(BWAPI::TilePosition(ui.lastPosition)) && !ui.unit->isVisible() && !ui.lifted)
	{
		return true;
	}

	return false;
}

int UnitData::getGasLost() const 
{ 
    return gasLost; 
}

int UnitData::getMineralsLost() const 
{ 
    return mineralsLost; 
}

int UnitData::getNumUnits(BWAPI::UnitType t) const 
{ 
    return numUnits[t.getID()]; 
}

int UnitData::getNumDeadUnits(BWAPI::UnitType t) const 
{ 
    return numDeadUnits[t.getID()]; 
}

const std::map<BWAPI::Unit,UnitInfo> & UnitData::getUnits() const 
{ 
    return unitMap; 
}

void UnitData::setLastOrderPath(BWAPI::Unit unit, BWEB::Path path)
{
	if (!unit) { return; }

	UnitInfo & ui = unitMap[unit];
	ui.lastOrderPath = path;
}

void UnitData::setLastOrderPosition(BWAPI::Unit unit, BWAPI::Position position)
{
	if (!unit) { return; }

	UnitInfo & ui = unitMap[unit];
	ui.lastOrderPosition = position;
}

void UnitData::setLastMovePath(BWAPI::Unit unit, BWEB::Path path)
{
	if (!unit) { return; }

	UnitInfo & ui = unitMap[unit];
	ui.lastMovePath = path;
}

void UnitData::setLastMovePosition(BWAPI::Unit unit, BWAPI::Position position)
{
	if (!unit) { return; }

	UnitInfo & ui = unitMap[unit];
	ui.lastMovePosition = position;
}

void UnitData::setLastPathPosition(BWAPI::Unit unit, BWAPI::Position position)
{
	if (!unit) { return; }

	UnitInfo & ui = unitMap[unit];
	ui.lastPathPosition = position;
}

void UnitData::setLastOrderPathBWEM(BWAPI::Unit unit, BWEM::CPPath path)
{
	if (!unit) { return; }

	UnitInfo & ui = unitMap[unit];
	ui.lastOrderPathBWEM = path;
}

void UnitData::setLastMoveFrame(BWAPI::Unit unit, int frame)
{
	if (!unit) { return; }

	UnitInfo & ui = unitMap[unit];
	ui.lastMoveFrame = frame;
}

void UnitData::setLastRetreatSwitchFrame(BWAPI::Unit unit, int frame)
{
	if (!unit) { return; }

	UnitInfo & ui = unitMap[unit];
	ui.lastRetreatSwitchFrame = frame;
}

void UnitData::setLastRetreatVal(BWAPI::Unit unit, bool retreatVal)
{
	if (!unit) { return; }

	UnitInfo & ui = unitMap[unit];
	ui.lastRetreatVal = retreatVal;
}
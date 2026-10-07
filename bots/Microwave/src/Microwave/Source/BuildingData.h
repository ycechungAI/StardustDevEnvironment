#pragma once

#include "Common.h"
#include "MacroAct.h"

namespace UAlbertaBot
{

namespace BuildingStatus
{
    enum { Unassigned = 0, Assigned = 1, UnderConstruction = 2, Size = 3 };
}

namespace DefenseLocation
{
	enum { Chokepoint = 0, Normal = 1, Minerals = 2, Random = 3 };
}

class Building 
{
public:
    
	MacroAct				macroAct;
	MacroLocation			macroLocation;
	BWAPI::TilePosition     desiredPosition;
	BWAPI::TilePosition     finalPosition;
	BWAPI::UnitType         type;
	BWAPI::Unit             buildingUnit;
	BWAPI::Unit             builderUnit;
    size_t                  status;
    bool                    isGasSteal;
	bool                    buildCommandGiven;
	bool                    underConstruction;
	size_t					defenseLocation;
	int						startFrame;			// when this building record was first created
	int						buildFrame;			// when the build command for this building was first issued
	int						buildersSent;		// count workers lost in construction
	int						placeBuildingDeadline;

	Building() 
		: macroLocation		(MacroLocation::Anywhere)
		, desiredPosition	(0, 0)
        , finalPosition     (BWAPI::TilePositions::None)
        , type              (BWAPI::UnitTypes::Unknown)
        , buildingUnit      (nullptr)
        , builderUnit       (nullptr)
        , status            (BuildingStatus::Unassigned)
        , buildCommandGiven (false)
        , underConstruction (false) 
        , isGasSteal        (false)
		, defenseLocation	(DefenseLocation::Chokepoint)
		, startFrame(BWAPI::Broodwar->getFrameCount())
		, buildFrame		(0)
		, buildersSent		(0)
		, placeBuildingDeadline(0)
    {} 

	// constructor we use most often
	Building(BWAPI::UnitType t, BWAPI::TilePosition desired)
		: macroLocation		(MacroLocation::Anywhere)
		, desiredPosition	(desired)
		, finalPosition		(desired)
        , type              (t)
        , buildingUnit      (nullptr)
        , builderUnit       (nullptr)
        , status            (BuildingStatus::Unassigned)
        , buildCommandGiven (false)
        , underConstruction (false) 
        , isGasSteal        (false)
		, defenseLocation	(DefenseLocation::Chokepoint)
		, startFrame(BWAPI::Broodwar->getFrameCount())
		, buildFrame		(0)
		, buildersSent		(0)
		, placeBuildingDeadline(0)
    {}

	// equals operator
	bool operator==(const Building & b) 
    {
		// buildings are equal if their worker unit or building unit are equal
		return (b.buildingUnit == buildingUnit) || (b.builderUnit == builderUnit);
	}

	// Return the center of the planned building, under the assumption that the finalPosition is valid.
	// This is used for moving a worker toward the intended building location.
	BWAPI::Position getCenter()
	{
		return
			BWAPI::Position(
				BWAPI::TilePosition(finalPosition.x + (type.tileWidth() / 2), finalPosition.y + (type.tileHeight() / 2))
			);
	}
};

class BuildingData 
{
    std::vector<Building> _buildings;

public:

	BuildingData();
	
    std::vector<Building> & getBuildings();

	void        addBuilding(const Building & b);
	void        removeBuilding(const Building & b);
    void        removeBuildings(const std::vector<Building> & buildings);
	bool        isBeingBuilt(BWAPI::UnitType type);
};
}
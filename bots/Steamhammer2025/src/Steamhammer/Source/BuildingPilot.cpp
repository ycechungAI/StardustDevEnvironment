#include "BuildingPilot.h"

#include "Bases.h"
#include "BuildingPlacer.h"
#include "The.h"

// Lift and land terran flying buildings.

// The buildings do not float around the map to scout or provide vision.
// The unit mix might change so that we need them again, and then we want
// them near where they started so they can land quickly.

// But once a ScoutBoss exists, it would make sense to assign some
// buildings to it.

// Also, proxy buildings that are forced to lift could be used for scouting,
// or could slowly return toward the main to be ready to land.

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

// Cancel anything the building is doing, so that it can be lifted.
// Return true if the command was executed, or is in the process of executing.
// The building cannot lift until the activity is canceled.
// TODO with more checks and a rename, this could be moved to Micro
bool BuildingPilot::cancelActivities(BWAPI::Unit building)
{
	if (building->getLastCommandFrame() >= the.now() - 2 * (1 + BWAPI::Broodwar->getRemainingLatencyFrames()))
	{
		// We already issued a command very recently.
		//BWAPI::Broodwar->printf("waiting... ", building->getType().getName().c_str(), building->getID());
		return true;
	}

	if (building->isTraining())
	{
		//if (building->getLastCommand().getType() != BWAPI::UnitCommandTypes::Cancel_Train)
		{
			//BWAPI::Broodwar->printf("canceling %s %d", building->getType().getName().c_str(), building->getID());
			building->cancelTrain();
		}
		//else
		{
			//BWAPI::Broodwar->printf("canceled %s %d", building->getType().getName().c_str(), building->getID());
		}
		return true;
	}

	if (building->isUpgrading())
	{
		//if (building->getLastCommand().getType() != BWAPI::UnitCommandTypes::Cancel_Upgrade)
		{
			building->cancelUpgrade();
		}
		return true;
	}

	if (building->isResearching())
	{
		//if (building->getLastCommand().getType() != BWAPI::UnitCommandTypes::Cancel_Research)
		{
			building->cancelResearch();
		}
		return true;
	}

	if (building->isConstructing())
	{
		//if (building->getLastCommand().getType() != BWAPI::UnitCommandTypes::Cancel_Addon)
		{
			building->cancelAddon();
		}
		return true;
	}

	return false;
}

// Find the landing position and reserve the tiles there.
// Return BWAPI::TilePositions::None on failure.
BWAPI::TilePosition BuildingPilot::findLandingTile(BWAPI::Unit building)
{
	Building b(building->getType(), building->getTilePosition());
	BWAPI::TilePosition tile;
	if (building->getType() == BWAPI::UnitTypes::Terran_Command_Center)
	{
		int nGas, nFreeGas;
		the.bases.gasCounts(nGas, nFreeGas);
		tile = the.map.getNextExpansion(false, true, nGas == 0, building->getTilePosition());
		//BWAPI::Broodwar->printf("find CC %d to %d,%d", building->getID(), tile.x, tile.y);
	}
	else
	{
		tile = the.placer.getBuildLocationNear(b, Config::Macro::BuildingSpacing);
		//BWAPI::Broodwar->printf("find %s %d to %d,%d", building->getType().getName().c_str(), building->getID(), tile.x, tile.y);
	}
	if (tile.isValid())
	{
		b.finalPosition = tile;
		the.placer.reserveTiles(b);
	}
	return tile;
}

bool BuildingPilot::isExplored(BWAPI::Unit building, const BWAPI::TilePosition & landingPoint) const
{
	Building b(building->getType(), landingPoint);
	return the.placer.isExplored(b);
}

// Land a building.
// To issue a land order, all tiles the building will cover must be explored.
// If they're not, issue a move order instead and the building will explore them.
void BuildingPilot::goLand(BWAPI::Unit building, const BWAPI::TilePosition & landingPoint)
{
	// Also move if the landing command fails for any reason.
	if (!isExplored(building, landingPoint) || !the.micro.Land(building, landingPoint))
	{
		(void)the.micro.Move(building, BWAPI::Position(landingPoint));
	}
}

void BuildingPilot::unreserveTiles(BWAPI::Unit building, const BWAPI::TilePosition & landingPoint)
{
	if (landingPoint.isValid())
	{
		Building b(building->getType(), landingPoint);
		b.finalPosition = landingPoint;
		the.placer.freeTiles(b);
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

// Lift a single grounded building no matter what it may be doing.
// We don't check that it's a liftable building. The caller has to get it right.
// TODO it can still fail to lift sometimes
void BuildingPilot::lift(BWAPI::Unit building)
{
	//BWAPI::Broodwar->printf("lifting 1 %s %d", building->getType().getName().c_str(), building->getID());
	if (!cancelActivities(building))
	{
		//BWAPI::Broodwar->printf("lifting 2 %s %d", building->getType().getName().c_str(), building->getID());
		(void)the.micro.Lift(building);
	}
}

// Lift buildings of a given type.
// If a building is busy doing something, it won't lift. We don't force it.
void BuildingPilot::lift(BWAPI::UnitType type)
{
	//BWAPI::Broodwar->printf("lifting all %s", type.getName().c_str());
	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (u->getType() == type && !u->isLifted() && u->canLift())
		{
			(void)the.micro.Lift(u);
		}
	}
}

// Land a given floating building at a given location.
// If the location is not a valid tile, or if landing fails, the pilot will
// find a new location and try again.
void BuildingPilot::land(BWAPI::Unit building, BWAPI::TilePosition tile)
{
	//BWAPI::Broodwar->printf("landing %s %d", building->getType().getName().c_str(), building->getID());
	_landingPoints[building] = tile;
}

// Land the building exactly where it is, if possible. Otherwise silently do nothing.
void BuildingPilot::landInPlace(BWAPI::Unit building)
{
	if (building->canLand(building->getTilePosition()))
	{
		(void)the.micro.Land(building, building->getTilePosition());
	}
}

// Land any floating buildings of a given type.
void BuildingPilot::land(BWAPI::UnitType type)
{
	// BWAPI::Broodwar->printf("landing all %s", type.getName().c_str());
	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (u->getType() == type && u->isLifted() && _landingPoints.find(u) == _landingPoints.end())
		{
			_landingPoints[u] = BWAPI::TilePositions::None;
		}
	}
}

// The given building is in the process of landing. It has a destination.
bool BuildingPilot::isLanding(BWAPI::Unit building) const
{
	auto it = _landingPoints.find(building);
	return it != _landingPoints.end() && it->second.isValid();
}

// Landing a building is a process that can fail unexpectedly.
// Called once per frame.
// The building placer takes care to ensure that the landing place is safe (for the moment).
// landingPoint == None means we haven't found one. We may have not looked yet, or failed a landing.
// landingPoint == Invalid means we found no landing point. Delay and try again.
void BuildingPilot::update()
{
	if (_landingPoints.empty())
	{
		return;
	}

	// TODO testing
	//for (auto it = _landingPoints.begin(); it != _landingPoints.end(); )
	//{
	//	if (it->first->getType() == BWAPI::UnitTypes::Terran_Command_Center &&
	//		it->first->getTargetPosition().isValid() &&
	//		it->first->getPosition().isValid())
	//	{
	//		BWAPI::Broodwar->drawLineMap(it->first->getPosition(), it->first->getTargetPosition(), BWAPI::Colors::Yellow);
	//	}
	//}
	// END TESTING

	// Responsive enough.
	const int cadence = BWAPI::Broodwar->getLatencyFrames() + 1;
	if (the.now() % cadence != 0)
	{
		return;
	}

	for (auto it = _landingPoints.begin(); it != _landingPoints.end(); )
	{
		BWAPI::Unit building = it->first;
		BWAPI::TilePosition & landingPoint = it->second;

		// Delay handling buildings that failed placement last time.
		if (landingPoint == BWAPI::TilePositions::Invalid)
		{
			if (the.now() % (8 * cadence) != 0)
			{
				continue;
			}
			else
			{
				// BWAPI::Broodwar->printf("%d delayed", building->getID());
			}
		}

		// Drop buildings that have been destroyed or have landed.
		if (!building->exists() || !building->isLifted())
		{
			unreserveTiles(building, landingPoint);
			it = _landingPoints.erase(it);
			// BWAPI::Broodwar->printf("%d landed/destroyed", building->getID());
			continue;
		}

		// Buildings may be moving to explore their landing point.
		// The landing zone must be fully explored before you can issue a land order.
		if (building->getOrder() == BWAPI::Orders::Move)
		{
			if (isExplored(building, landingPoint))
			{
				// BWAPI::Broodwar->printf("%d move -> land", building->getID());
				goLand(building, landingPoint);
			}
		}

		// Recognize buildings that have failed to land.
		else if (building->getOrder() != BWAPI::Orders::BuildingLand)
		{
			unreserveTiles(building, landingPoint);
			// BWAPI::Broodwar->printf("%d landing failed", building->getID());
			landingPoint = BWAPI::TilePositions::None;
		}

		// Choose landing points for buildings that have none.
		if (!landingPoint.isValid())
		{
			BWAPI::TilePosition tile = findLandingTile(building);
			if (tile.isValid())
			{
				// BWAPI::Broodwar->printf("pilot %s %d to %d,%d", building->getType().getName().c_str(), building->getID(), tile.x, tile.y);
				landingPoint = tile;
				goLand(building, tile);
			}
			else
			{
				// The Invalid landing point is treated specially. It causes a delay before
				// we try again to place the building.
				// BWAPI::Broodwar->printf("landing failed %s %d", building->getType().getName().c_str(), building->getID());
				landingPoint = BWAPI::TilePositions::Invalid;
			}
		}

		++it;
	}
}
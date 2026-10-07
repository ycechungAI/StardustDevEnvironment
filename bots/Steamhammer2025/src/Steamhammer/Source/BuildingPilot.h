#pragma once

#include <BWAPI.h>

namespace UAlbertaBot
{
	class Building;		// forward declaration

	class BuildingPilot
	{
	private:
		// Where we intend to land each floating building.
		// This is only filled in once we're asked to land it.
		// The TilePosition may change before the building lands.
		std::map<BWAPI::Unit, BWAPI::TilePosition> _landingPoints;

		bool cancelActivities(BWAPI::Unit u);

		BWAPI::TilePosition findLandingTile(BWAPI::Unit building);
		bool isExplored(BWAPI::Unit building, const BWAPI::TilePosition & landingPoint) const;
		void goLand(BWAPI::Unit building, const BWAPI::TilePosition & landingPoint);
		void unreserveTiles(BWAPI::Unit building, const BWAPI::TilePosition & landingPoint);

	public:
		void lift(BWAPI::Unit building);
		void lift(BWAPI::UnitType type);
		void land(BWAPI::Unit building, BWAPI::TilePosition tile = BWAPI::TilePositions::None);
		void landInPlace(BWAPI::Unit building);
		void land(BWAPI::UnitType type);
		bool isLanding(BWAPI::Unit building) const;

		void update();
	};
}

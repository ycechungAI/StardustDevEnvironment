#pragma once

#include "Common.h"
#include <vector>
#include "BWAPI.h"
#include "GridDistances.h"
#include "MacroAct.h"

// Keep track of map information, like what tiles are walkable or buildable.

namespace UAlbertaBot
{
	struct ChokeData
	{
		int width;
		bool blocked;

		bool isRamp;
		BWAPI::TilePosition highElevationTile;

		bool requiresMineralWalk;
		BWAPI::Unit firstMineralPatch;
		BWAPI::Unit secondMineralPatch;

		ChokeData(const BWEM::ChokePoint* choke)
			: width(0)
			, blocked(false)
			, isRamp(false)
			, highElevationTile(BWAPI::TilePosition(choke->Center()))
			, requiresMineralWalk(false)
			, firstMineralPatch(nullptr)
			, secondMineralPatch(nullptr)
		{};
	};

	// provides useful tools for analyzing the starcraft map
	// calculates connectivity and distances using flood fills
	class MapTools
	{
		const size_t allMapsSize = 40;				// store this many distance maps in _allMaps
		std::map<BWAPI::TilePosition, GridDistances>
								_allMaps;			// a cache of already computed distance maps
		std::vector< std::vector<bool> >
								_terrainWalkable;	// walkable considering terrain only
		std::vector< std::vector<bool> >
								_walkable;			// walkable considering terrain and neutral units
		std::vector< std::vector<bool> >
								_buildable;
		std::vector< std::vector<bool> >
								_depotBuildable;
		std::vector< std::vector<int> >
								_distanceFromHome;
		bool					_distanceFromHomeCalc = false;

		bool					_hasIslandBases;
		int						_minChokeWidth;
		BWAPI::Unitset			_staticNeutrals;

		void					setBWAPIMapData();				// reads in the map data from bwapi and stores it in our map format
		void					setChokepointData();

		const BWEM::Base *		nextExpansion(bool hidden, bool wantMinerals);

	public:

		MapTools();

		static MapTools &	Instance();

		int		getGroundTileDistance(BWAPI::TilePosition from, BWAPI::TilePosition to);
		int		getGroundTileDistance(BWAPI::Position from, BWAPI::Position to);
		int		getGroundDistance(BWAPI::Position from, BWAPI::Position to);

		int		getGroundDistanceBWEB(BWAPI::Position from, BWAPI::Position to, bool neutralblocks = true);
		int		getGroundTileDistanceBWEB(BWAPI::TilePosition from, BWAPI::TilePosition to, bool neutralblocks = true);

		// Pass only valid tiles to these routines!
		bool	isTerrainWalkable(BWAPI::TilePosition tile) const { return _terrainWalkable[tile.x][tile.y]; };
		bool	isWalkable(BWAPI::TilePosition tile) const { return _walkable[tile.x][tile.y]; };
		bool	isBuildable(BWAPI::TilePosition tile) const { return _buildable[tile.x][tile.y]; };
		bool	isDepotBuildable(BWAPI::TilePosition tile) const { return _depotBuildable[tile.x][tile.y]; };

		bool	isBuildable(BWAPI::TilePosition tile, BWAPI::UnitType type) const;

		const std::vector<BWAPI::TilePosition> & getClosestTilesTo(BWAPI::TilePosition pos);
		const std::vector<BWAPI::TilePosition> & getClosestTilesTo(BWAPI::Position pos);

		void	drawHomeDistanceMap();

		BWAPI::TilePosition getNextExpansion(bool hidden = false, bool minOnlyOK = false);
		BWAPI::TilePosition reserveNextExpansion(bool hidden = false, bool minOnlyOK = false);

		bool	hasIslandBases(){ return _hasIslandBases; }
		int		minChokeWidth() { return _minChokeWidth; }

		/* BWEB functionality*/

		/// <summary> Returns the closest buildable BWAPI::TilePosition for any type of structure. </summary>
		/// <param name="type"> The BWAPI::UnitType of the structure you want to build. </param>
		/// <param name="tile"> The BWAPI::TilePosition you want to build closest to. </param>
		BWAPI::TilePosition getBuildPosition(BWAPI::UnitType type, BWAPI::TilePosition tile = BWAPI::Broodwar->self()->getStartLocation(), BWAPI::Unit builderUnit = nullptr);

		/// <summary> Returns the closest buildable BWAPI::TilePosition for a defensive structure. </summary>
		/// <param name="type"> The BWAPI::UnitType of the structure you want to build. </param>
		/// <param name="tile"> The BWAPI::TilePosition you want to build closest to. </param>
		BWAPI::TilePosition getDefBuildPosition(BWAPI::UnitType type, BWAPI::TilePosition tile = BWAPI::Broodwar->self()->getStartLocation(), BWAPI::Unit builderUnit = nullptr);
		BWAPI::TilePosition getStationBuildPosition(BWAPI::UnitType type, BWAPI::TilePosition tile = BWAPI::Broodwar->self()->getStartLocation(), BWAPI::Unit builderUnit = nullptr);

		bool haveWallPosition(BWAPI::UnitType type);

		bool creepCheck(BWAPI::UnitType type, BWAPI::TilePosition tile);
		bool psiCheck(BWAPI::UnitType type, BWAPI::TilePosition tile);
		bool isPlaceable(BWAPI::UnitType type, const BWAPI::TilePosition location);

		std::string	getMacroLocationString(MacroLocation MacroLocation) const;

		BWAPI::Position getNaturalDefPosition();
		BWAPI::Position getMainDefPosition();
		BWAPI::Position getBaseDefPosition(BWAPI::Position dp);
		BWAPI::Position getMineralDefPosition(BWAPI::Position dp);
		BWAPI::Position getRandomDefPosition(BWAPI::Position dp);
	};

}
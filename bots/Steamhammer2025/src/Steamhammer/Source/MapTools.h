#pragma once

#include <map>
#include <vector>
#include "GridDistances.h"

// Keep track of map information, like what tiles are walkable or buildable.

namespace UAlbertaBot
{
class Base;
class GridDistances;

class MapTools
{
    const size_t allMapsSize = 40;			// store this many distance maps in _allMaps

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

    void				setBWAPIMapData();					// reads in the map data from bwapi and stores it in our map format

public:

    MapTools();
    void initialize();

    // Pass only valid tiles to these routines!
    bool	isTerrainWalkable(BWAPI::TilePosition tile) const { return _terrainWalkable[tile.x][tile.y]; };
    bool	isWalkable(BWAPI::TilePosition tile) const { return _walkable[tile.x][tile.y]; };
    bool	isBuildable(BWAPI::TilePosition tile) const { return _buildable[tile.x][tile.y]; };
    bool	isDepotBuildable(BWAPI::TilePosition tile) const { return _depotBuildable[tile.x][tile.y]; };

    // TODO deprecated method, used only in Bases
    bool	isBuildable(BWAPI::TilePosition tile, BWAPI::UnitType type) const;

	int		getGroundTileDistance(BWAPI::TilePosition from, BWAPI::TilePosition to);
	int		getGroundTileDistance(BWAPI::Position from, BWAPI::Position to);
	int		getGroundDistance(BWAPI::Position from, BWAPI::Position to);

	const std::vector<BWAPI::TilePosition> & getClosestTilesTo(BWAPI::TilePosition pos);
    const std::vector<BWAPI::TilePosition> & getClosestTilesTo(BWAPI::Position pos);

	BWAPI::Position getCenter() const;

	bool inNukeRange(const BWAPI::Position & pos) const;

    void	drawHomeDistances();
    void    drawExpoScores() const;

	int distanceToNearestBaseOwnedBy(const BWAPI::TilePosition & pos, BWAPI::Player side) const;

	// If you specify closeTo, it prefers bases close to that location.
    Base *				nextExpansion(bool hidden, bool wantMinerals, bool wantGas, const BWAPI::TilePosition & closeTo = BWAPI::TilePositions::None) const;
    BWAPI::TilePosition	getNextExpansion(bool hidden, bool wantMinerals, bool wantGas, const BWAPI::TilePosition & closeTo = BWAPI::TilePositions::None) const;
};

}
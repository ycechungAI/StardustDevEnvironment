#pragma once

#include <vector>
#include "BWAPI.h"
#include "Grid.h"

namespace UAlbertaBot
{
class GridDistances : public Grid
{
	int _width;
	int _height;
	BWAPI::TilePosition _startTile;

	std::vector< std::vector<short> > _dist;
    std::vector<BWAPI::TilePosition> _sortedTilePositions;

	void computeDistanceMap(const BWAPI::TilePosition & start, int limit, bool neutralBlocks);

public:
	GridDistances();
	GridDistances(const BWAPI::TilePosition & start, bool neutralBlocks = true);
	GridDistances(const BWAPI::TilePosition & start, int limit, bool neutralBlocks = true);

	int getDistance(int tileX, int tileY) const;
	int getDistance(const BWAPI::TilePosition & tile) const;
	int getDistance(const BWAPI::Position & pos) const;
	int getDistance(const BWAPI::Unit unit) const;

	int getStaticUnitDistance(const BWAPI::Unit unit) const;

    // given a position, get the position we should move to to minimize distance
    const std::vector<BWAPI::TilePosition> & getSortedTiles() const;
};
}
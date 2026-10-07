#include "BananaBrain.h"

FastPosition::FastPosition(const FastWalkPosition& walk_position) : x(walk_position.x * 8), y(walk_position.y * 8)
{
}

FastPosition::FastPosition(const FastTilePosition& tile_position) : x(tile_position.x * 32), y(tile_position.y * 32)
{
}

bool FastPosition::isValid() const
{
	return x >= 0 && y >= 0 && x < Broodwar->mapWidth() * 32 && y < Broodwar->mapHeight() * 32;
}

FastPosition& FastPosition::makeValid()
{
	if (x < 0) x = 0;
	else if (x >= Broodwar->mapWidth() * 32) x = Broodwar->mapWidth() * 32 - 1;
	
	if (y < 0) y = 0;
	else if (y >= Broodwar->mapHeight() * 32) y = Broodwar->mapHeight() * 32 - 1;
	
	return *this;
}

int FastPosition::getApproxDistance(const FastPosition& position) const
{
	Position p1{x, y};
	Position p2{position.x, position.y };
	return p1.getApproxDistance(p2);
}

double FastPosition::getDistance(const FastPosition& position) const
{
	return ((*this) - position).getLength();
}

double FastPosition::getLength() const
{
	double xd = x;
	double yd = y;
	return std::sqrt(xd * xd + yd * yd);
}

FastWalkPosition::FastWalkPosition(const FastPosition& position) : x(position.x / 8), y(position.y / 8)
{
}

FastWalkPosition::FastWalkPosition(const FastTilePosition& tile_position) : x(tile_position.x * 4), y(tile_position.y * 4)
{
}

bool FastWalkPosition::isValid() const
{
	return x >= 0 && y >= 0 && x < Broodwar->mapWidth() * 4 && y < Broodwar->mapHeight() * 4;
}

FastWalkPosition& FastWalkPosition::makeValid()
{
	if (x < 0) x = 0;
	else if (x >= Broodwar->mapWidth() * 4) x = Broodwar->mapWidth() * 4 - 1;
	
	if (y < 0) y = 0;
	else if (y >= Broodwar->mapHeight() * 4) y = Broodwar->mapHeight() * 4 - 1;
	
	return *this;
}

int FastWalkPosition::getApproxDistance(const FastWalkPosition& walk_position) const
{
	WalkPosition p1{x, y};
	WalkPosition p2{walk_position.x, walk_position.y };
	return p1.getApproxDistance(p2);
}

FastTilePosition::FastTilePosition(const FastPosition& position) : x(position.x / 32), y(position.y / 32)
{
}

FastTilePosition::FastTilePosition(const FastWalkPosition& walk_position) : x(walk_position.x / 4), y(walk_position.y / 4)
{
}

bool FastTilePosition::isValid() const
{
	return x >= 0 && y >= 0 && x < Broodwar->mapWidth() && y < Broodwar->mapHeight();
}

FastTilePosition& FastTilePosition::makeValid()
{
	if (x < 0) x = 0;
	else if (x >= Broodwar->mapWidth()) x = Broodwar->mapWidth() - 1;
	
	if (y < 0) y = 0;
	else if (y >= Broodwar->mapHeight()) y = Broodwar->mapHeight() - 1;
	
	return *this;
}

int FastTilePosition::getApproxDistance(const FastTilePosition& tile_position) const
{
	TilePosition p1{x, y};
	TilePosition p2{tile_position.x, tile_position.y };
	return p1.getApproxDistance(p2);
}

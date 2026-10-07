#include "MapTools.h"
#include "BuildingPlacer.h"
#include "InformationManager.h"

const double pi = 3.14159265358979323846;

using namespace UAlbertaBot;

MapTools & MapTools::Instance()
{
	static MapTools instance;
	return instance;
}

MapTools::MapTools()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// Figure out which tiles are walkable and buildable.
	setBWAPIMapData();
	setChokepointData();

	// Figure out if start locations are connected
	_hasIslandBases = false;
	BWAPI::TilePosition startLocation = BWAPI::Broodwar->self()->getStartLocation();

	for (auto& area : BWEMmap.Areas())
	{
		for (auto& base : area.Bases())
		{		
			if (base.Starting() && base.Location() != startLocation)
			{
				int dist = getGroundTileDistanceBWEB(startLocation, base.Location());
				if (dist < 0)
				{
					//BWAPI::Broodwar->printf("We have islands!");
					_hasIslandBases = true;
					break;
				}
			}
		}
	}
}

//From Locutus
void MapTools::setChokepointData()
{
	// Profile debug
	//PROFILE_FUNCTION();
	
	_minChokeWidth = 4000;

	// Get all of the BWEM chokepoints
	std::set<const BWEM::ChokePoint*> chokes;
	for (const auto & area : BWEMmap.Areas())
		for (const BWEM::ChokePoint * choke : area.ChokePoints())
			chokes.insert(choke);

	// Store a ChokeData object for each choke
	for (const BWEM::ChokePoint * choke : chokes)
	{
		choke->SetExt(new ChokeData(choke));
		ChokeData & chokeData = *((ChokeData*)choke->Ext());
		BWAPI::Position chokeCenter(choke->Center());

		// Compute the choke width
		// Because the ends are themselves walkable tiles, we need to add a bit of padding to estimate the actual walkable width of the choke
		int width = BWAPI::Position(choke->Pos(choke->end1)).getApproxDistance(BWAPI::Position(choke->Pos(choke->end2))) + 16;
		if (width == 16) width = 0;
		chokeData.width = width;

		// Set minimum map choke width
		if (width < _minChokeWidth)
		{
			_minChokeWidth = width;
		}

		// Determine if the choke is blocked by neutrals
		bool blockedByNeutral = false;
		for (const auto staticNeutral : BWAPI::Broodwar->getStaticNeutralUnits())
		{
			if (!blockedByNeutral && 
				staticNeutral->getType().isSpecialBuilding() &&
				(staticNeutral->getType().tileHeight() * 32 > chokeData.width ||
				staticNeutral->getType().tileWidth() * 32 > chokeData.width) &&
				staticNeutral->getDistance(chokeCenter) < 100)
			{
				blockedByNeutral = true;
			}
		}
		chokeData.blocked = blockedByNeutral;
				
		// Determine if the choke is a ramp
		int firstAreaElevation = BWAPI::Broodwar->getGroundHeight(BWAPI::TilePosition(choke->GetAreas().first->Top()));
		int secondAreaElevation = BWAPI::Broodwar->getGroundHeight(BWAPI::TilePosition(choke->GetAreas().second->Top()));
		if (firstAreaElevation != secondAreaElevation)
		{
			chokeData.isRamp = true;

			// For narrow ramps with a difference in elevation, compute a tile at high elevation close to the choke
			// We will use this for pathfinding
			if (chokeData.width < 96)
			{
				// Start by computing the angle of the choke
				BWAPI::Position chokeDelta(choke->Pos(choke->end1) - choke->Pos(choke->end2));
				double chokeAngle = atan2(chokeDelta.y, chokeDelta.x);

				// Now find a tile a bit away from the middle of the choke that is at high elevation
				int highestElevation = std::max(firstAreaElevation, secondAreaElevation);
				BWAPI::Position center(choke->Center());
				BWAPI::TilePosition closestToCenter = BWAPI::TilePositions::Invalid;
				for (int step = 0; step <= 6; step++)
					for (int direction = -1; direction <= 1; direction += 2)
					{
						BWAPI::TilePosition tile(BWAPI::Position(
							center.x - (int)std::round(16 * step * std::cos(chokeAngle + direction * (pi / 2.0))),
							center.y - (int)std::round(16 * step * std::sin(chokeAngle + direction * (pi / 2.0)))));

						if (!tile.isValid()) continue;
						if (!BWEB::Map::isWalkable(tile)) continue;

						if (BWAPI::Broodwar->getGroundHeight(tile) == highestElevation)
						{
							chokeData.highElevationTile = tile;
						}
					}
			}
		}

		// Map override: (2)CarthageIII_3.0.scx 
		if (BWAPI::Broodwar->mapHash() == "fe0a18342c797203d585dc2a3241f3974b765e0b")
		{
			// Process each choke
			ChokeData & chokeData = *((ChokeData*)choke->Ext());

			//Mark the two chokes to the area in the middle with the 3 blocking buildings as blocked
			for (const auto unit : BWAPI::Broodwar->getStaticNeutralUnits())
			{
				if ((unit->getInitialType() == BWAPI::UnitTypes::Zerg_Egg || unit->getInitialType().isBuilding()) &&
					!unit->getInitialType().isResourceContainer())
				{
					bool blockedByNeutralBuilding = false;
					const auto unitArea = BWEMmap.GetArea(BWAPI::WalkPosition(unit->getPosition()));
					for (auto unitChoke : unitArea->ChokePoints())
					{
						if (!blockedByNeutralBuilding &&
							choke == unitChoke)
						{
							blockedByNeutralBuilding = true;
							Log().Debug() << "BWAPI Neutral building " << unit->getID() << "; marked choke " << choke->Index() << " as blocked";
						}
					}
					chokeData.blocked = blockedByNeutralBuilding;
				}
			}
		}

		// Map override: Plasma, we enrich the BWEM chokepoints with data about mineral walking
		if (BWAPI::Broodwar->mapHash() == "6f5295624a7e3887470f3f2e14727b1411321a67")
		{
			// Process each choke
			ChokeData & chokeData = *((ChokeData*)choke->Ext());
			BWAPI::Position chokeCenter(choke->Center());

			// Determine if the choke is blocked by eggs, and grab the close mineral patches
			bool blockedByEggs = false;
			BWAPI::Unit closestMineralPatch = nullptr;
			BWAPI::Unit secondClosestMineralPatch = nullptr;
			int closestMineralPatchDist = INT_MAX;
			int secondClosestMineralPatchDist = INT_MAX;
			for (const auto staticNeutral : BWAPI::Broodwar->getStaticNeutralUnits())
			{
				if (!blockedByEggs &&
					staticNeutral->getType() == BWAPI::UnitTypes::Zerg_Egg &&
					staticNeutral->getDistance(chokeCenter) < 100)
				{
					blockedByEggs = true;
				}

				if (staticNeutral->getType() == BWAPI::UnitTypes::Resource_Mineral_Field &&
					staticNeutral->getResources() == 32)
				{
					int dist = staticNeutral->getDistance(chokeCenter);
					if (dist <= closestMineralPatchDist)
					{
						secondClosestMineralPatchDist = closestMineralPatchDist;
						closestMineralPatchDist = dist;
						secondClosestMineralPatch = closestMineralPatch;
						closestMineralPatch = staticNeutral;
					}
					else if (dist < secondClosestMineralPatchDist)
					{
						secondClosestMineralPatchDist = dist;
						secondClosestMineralPatch = staticNeutral;
					}
				}
			}

			if (!blockedByEggs) continue;

			chokeData.requiresMineralWalk = true;
			chokeData.firstMineralPatch = closestMineralPatch;
			chokeData.secondMineralPatch = secondClosestMineralPatch;
			chokeData.blocked = blockedByEggs;
		}

	}

	// TODO testing
	//BWAPI::TilePosition homePosition = BWAPI::Broodwar->self()->getStartLocation();
	//BWAPI::Broodwar->printf("start position %d,%d", homePosition.x, homePosition.y);
}

// Read the map data from BWAPI and remember which 32x32 build tiles are walkable.
// NOTE The game map is walkable at the resolution of 8x8 walk tiles, so this is an approximation.
//      We're asking "Can big units walk here?" Small units may be able to squeeze into more places.
void MapTools::setBWAPIMapData()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// 1. Mark all tiles walkable and buildable at first.
	_terrainWalkable = std::vector< std::vector<bool> >(BWAPI::Broodwar->mapWidth(), std::vector<bool>(BWAPI::Broodwar->mapHeight(), true));
	_walkable = std::vector< std::vector<bool> >(BWAPI::Broodwar->mapWidth(), std::vector<bool>(BWAPI::Broodwar->mapHeight(), true));
	_buildable = std::vector< std::vector<bool> >(BWAPI::Broodwar->mapWidth(), std::vector<bool>(BWAPI::Broodwar->mapHeight(), true));
	_depotBuildable = std::vector< std::vector<bool> >(BWAPI::Broodwar->mapWidth(), std::vector<bool>(BWAPI::Broodwar->mapHeight(), true));
	_distanceFromHome = std::vector< std::vector<int> >(BWAPI::Broodwar->mapWidth(), std::vector<int>(BWAPI::Broodwar->mapHeight(), -1));

	// 2. Check terrain: Is it buildable? Is it walkable?
	// This sets _walkable and _terrainWalkable identically.
	for (int x = 0; x < BWAPI::Broodwar->mapWidth(); ++x)
	{
		for (int y = 0; y < BWAPI::Broodwar->mapHeight(); ++y)
		{
			// This initializes all cells of _buildable and _depotBuildable.
			bool buildable = BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(x, y), false);
			_buildable[x][y] = buildable;
			_depotBuildable[x][y] = buildable;

			// Check each 8x8 walk tile within this 32x32 TilePosition.
			int walkableWalkPositions = 0;
			for (int i = 0; i < 4; ++i)
			{
				for (int j = 0; j < 4; ++j)
				{
					if (BWAPI::Broodwar->isWalkable(x * 4 + i, y * 4 + j)) walkableWalkPositions++;
				}
			}

			// On Plasma, consider the tile walkable if at least 14 walk positions are walkable
			if (walkableWalkPositions < 16 &&
				(BWAPI::Broodwar->mapHash() != "6f5295624a7e3887470f3f2e14727b1411321a67" || walkableWalkPositions < 15))
			{
				_terrainWalkable[x][y] = false;
				_walkable[x][y] = false;
			}
		}
	}

	// 3. Check neutral units: Do they block walkability?
	// This affects _walkable but not _terrainWalkable. We don't update buildability here.
	for (const auto unit : BWAPI::Broodwar->getStaticNeutralUnits())
	{
		// Ignore the eggs on Plasma
		//if (BWAPI::Broodwar->mapHash() == "6f5295624a7e3887470f3f2e14727b1411321a67" &&
		//	unit->getType() == BWAPI::UnitTypes::Zerg_Egg)
		//	continue;

		// The neutral units may include moving critters which do not permanently block tiles.
		// Something immobile blocks tiles it occupies until it is destroyed. (Are there exceptions?)
		if (!unit->getType().canMove() && !unit->isFlying())
		{
			BWAPI::TilePosition pos = unit->getTilePosition();
			for (int x = pos.x; x < pos.x + unit->getType().tileWidth(); ++x)
			{
				for (int y = pos.y; y < pos.y + unit->getType().tileHeight(); ++y)
				{
					if (BWAPI::TilePosition(x, y).isValid())   // assume it may be partly off the edge
					{
						_walkable[x][y] = false;
					}
				}
			}
		}
	}

	// 4. Check static resources: Do they block buildability?
	for (const BWAPI::Unit resource : BWAPI::Broodwar->getStaticNeutralUnits())
	{
		if (!resource->getType().isResourceContainer())
		{
			continue;
		}

		int tileX = resource->getTilePosition().x;
		int tileY = resource->getTilePosition().y;

		for (int x = tileX; x<tileX + resource->getType().tileWidth(); ++x)
		{
			for (int y = tileY; y<tileY + resource->getType().tileHeight(); ++y)
			{
				_buildable[x][y] = false;

				// depots can't be built within 3 tiles of any resource
				// TODO rewrite this to be less disgusting
				for (int dx = -3; dx <= 3; dx++)
				{
					for (int dy = -3; dy <= 3; dy++)
					{
						if (BWAPI::TilePosition(x + dx, y + dy).isValid())
						{
							_depotBuildable[x + dx][y + dy] = false;
						}
					}
				}
			}
		}
	}
}

// Ground distance in tiles, -1 if no path exists.
// This is Manhattan distance, not walking distance. Still good for finding paths.
int MapTools::getGroundTileDistance(BWAPI::TilePosition origin, BWAPI::TilePosition destination)
{
	// Profile debug
	//PROFILE_FUNCTION();

	// if we have too many maps, reset our stored maps in case we run out of memory
	if (_allMaps.size() > allMapsSize)
	{
		_allMaps.clear();

		if (Config::Debug::DrawMapDistances)
		{
			BWAPI::Broodwar->printf("Cleared distance map cache");
		}
	}

	// Do we have a distance map to the destination?
	auto it = _allMaps.find(destination);
	if (it != _allMaps.end())
	{
		return (*it).second.getDistance(origin);
	}

	// It's symmetrical. A distance map to the origin is just as good.
	it = _allMaps.find(origin);
	if (it != _allMaps.end())
	{
		return (*it).second.getDistance(destination);
	}

	// Make a new map for this destination.
	_allMaps.insert(std::pair<BWAPI::TilePosition, GridDistances>(destination, GridDistances(destination)));
	return _allMaps[destination].getDistance(origin);
}

int MapTools::getGroundTileDistance(BWAPI::Position origin, BWAPI::Position destination)
{
	return getGroundTileDistance(BWAPI::TilePosition(origin), BWAPI::TilePosition(destination));
}

// Ground distance in pixels (with build tile granularity), -1 if no path exists.
// Build tile granularity means that the distance is a multiple of 32 pixels.
int MapTools::getGroundDistance(BWAPI::Position origin, BWAPI::Position destination)
{
	int tiles = getGroundTileDistance(BWAPI::TilePosition(origin), BWAPI::TilePosition(destination));
	if (tiles > 0)
	{
		return 32 * tiles;
	}
	return tiles; // 0 or -1
}

// Ground distance in pixels using BWEB / BWEM
int MapTools::getGroundDistanceBWEB(BWAPI::Position origin, BWAPI::Position destination, bool neutralblocks)
{
	// Profile debug
	//PROFILE_FUNCTION();

	// Check for valid start and end points
	if (!origin.isValid() || !destination.isValid())
		return -1;

	int dist;
	if (!neutralblocks)
	{
		// Use BWEB getGroundDistance if we don't care about blocking neutrals (256x256 = 100 ms)
		dist = (int)(BWEB::Map::getGroundDistance(origin, destination));

		// Use BWEM getPath if we don't care about blocking neutrals (256x256 = 210 ms)
		//BWEMmap.GetPath(origin, destination, &dist);

		// Return -1 if destination is not reachable
		if (dist < 0) 
			return -1;
	}
	else
	{
		// Use BWEB path
		BWEB::Path jpsPath(origin, destination, BWAPI::UnitTypes::Zerg_Zergling);
		jpsPath.generateJPS([&](const BWAPI::TilePosition &t) { return jpsPath.unitWalkable(t); });

		// Return -1 if destination is not reachable
		if (!jpsPath.isReachable())
			return -1;

		// Get distance
		dist = (int)(jpsPath.getDistance());
	}

	// Return distance
	return dist;

}

// Ground distance in build tiles using BWEB / BWEM
int MapTools::getGroundTileDistanceBWEB(BWAPI::TilePosition origin, BWAPI::TilePosition destination, bool neutralblocks)
{
	int dist = getGroundDistanceBWEB(	BWAPI::Position(origin), 
										BWAPI::Position(destination), 
										neutralblocks);
	if (dist > 0)
	{
		if (!_terrainWalkable[destination.x][destination.y])
			return -1;

		if(neutralblocks && !_walkable[destination.x][destination.y])
			return -1;

		return (int)(dist / 32);
	}
	return dist; // 0 or -1
}

const std::vector<BWAPI::TilePosition> & MapTools::getClosestTilesTo(BWAPI::TilePosition tile)
{
	// Profile debug
	//PROFILE_FUNCTION();

	// make sure the distance map is calculated with pos as a destination
	int a = getGroundTileDistance(tile, tile);

	return _allMaps[tile].getSortedTiles();
}

const std::vector<BWAPI::TilePosition> & MapTools::getClosestTilesTo(BWAPI::Position pos)
{
	return getClosestTilesTo(BWAPI::TilePosition(pos));
}

bool MapTools::isBuildable(BWAPI::TilePosition tile, BWAPI::UnitType type) const
{
	if (!tile.isValid())
	{
		return false;
	}

	int startX = tile.x;
	int endX = tile.x + type.tileWidth();
	int startY = tile.y;
	int endY = tile.y + type.tileHeight();

	for (int x = startX; x<endX; ++x)
	{
		for (int y = startY; y<endY; ++y)
		{
			BWAPI::TilePosition tile(x, y);

			if (!tile.isValid() || !isBuildable(tile) || type.isResourceDepot() && !isDepotBuildable(tile))
			{
				return false;
			}
		}
	}

	return true;
}

void MapTools::drawHomeDistanceMap()
{
	if (!Config::Debug::DrawMapDistances)
	{
		return;
	}

	BWAPI::TilePosition homeTile = BWAPI::Broodwar->self()->getStartLocation();
	//GridDistances d(homeTile, true);
	//BWEB::Path path = BWEB::Path();

	if (!_distanceFromHomeCalc)
	{
		_distanceFromHomeCalc = true;
		for (int x = 0; x < BWAPI::Broodwar->mapWidth(); ++x)
		{
			for (int y = 0; y < BWAPI::Broodwar->mapHeight(); ++y)
			{
				BWAPI::TilePosition tile(x, y);

				//path.createUnitPath(BWAPI::Position(homeTile), BWAPI::Position(tile));
				//int dist = path.getDistance();
				//int dist = d.getDistance(tile);
				//int dist = getGroundTileDistanceBWEB(homeTile, tile, true);
				int dist = getGroundTileDistance(homeTile, tile);
				_distanceFromHome[x][y] = dist;

				/*
				int walkable_SH = _walkable[x][y] == true ? 1 : -1;
				char color_SH = walkable_SH < 0 ? orange : white;

				//int walkable_BWEB = BWEB::Pathfinding::unitWalkable(tile) == true ? 1 : -1;
				//char color_BWEB = walkable_BWEB < 0 ? orange : white;

				//BWAPI::Broodwar->drawTextMap(BWAPI::Position(tile) + BWAPI::Position(12, 12), "%c%d", color_SH, walkable_SH);
				//BWAPI::Broodwar->drawTextMap(BWAPI::Position(tile) + BWAPI::Position(12, 12), "%c%d", color_BWEB, walkable_BWEB);
				*/
			}
		}
	}
	else
	{
		for (int x = 0; x < BWAPI::Broodwar->mapWidth(); ++x)
		{
			for (int y = 0; y < BWAPI::Broodwar->mapHeight(); ++y)
			{
				BWAPI::TilePosition tile(x, y);

				int dist = _distanceFromHome[x][y];

				if (dist < 0) dist = -1;
				char color = dist < 0 ? orange : white;

				BWAPI::Broodwar->drawTextMap(BWAPI::Position(tile) + BWAPI::Position(12, 12), "%c%d", color, dist);

				if (homeTile.x == x && homeTile.y == y)
				{
					BWAPI::Broodwar->drawBoxMap(tile.x, tile.y, tile.x + 1, tile.y + 1, BWAPI::Colors::Yellow);
				}
			}
		}
	}
}

const BWEM::Base * MapTools::nextExpansion(bool hidden, bool wantMinerals)
{
	// Profile debug
	//PROFILE_FUNCTION();

	// Abbreviations.
	BWAPI::Player player = BWAPI::Broodwar->self();
	BWAPI::Player enemy = BWAPI::Broodwar->enemy();

	// We'll go through the bases and pick the one with the best score.
	const BWEM::Base * bestBase = nullptr;
	double bestScore = 0.0;

	BWAPI::TilePosition homeTile = InformationManager::Instance().getMyMainBaseLocation()->Location();
	const BWEM::Base * enemyBase = InformationManager::Instance().getEnemyMainBaseLocation();  // may be null

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			BWAPI::TilePosition tile = base.Location();
			BWAPI::Position pos(tile);
			double score = 0.0;

			// Want to be close to our own base.
			double distanceFromUs = getGroundTileDistance(homeTile, tile);

			// for now, don't expand to unconnected expansions
			if (base.Location() != homeTile && distanceFromUs < 0)
			{
				continue;
			}

			// Do we demand a gas base (!wantMinerals)?
			if (!wantMinerals && base.Geysers().empty())
			{
				continue;
			}

			// Don't expand to an existing base.
			if (InformationManager::Instance().getBaseOwner(&base) != BWAPI::Broodwar->neutral())
			{
				continue;
			}

			// Don't expand to a base already reserved for another expansion.
			if (InformationManager::Instance().isBaseReserved(&base))
			{
				continue;
			}

			bool buildingInTheWay = false;
		
			for (int x = 0; x < player->getRace().getCenter().tileWidth(); ++x)
			{
				for (int y = 0; y < player->getRace().getCenter().tileHeight(); ++y)
				{
					if (BuildingPlacer::Instance().isReserved(tile.x + x, tile.y + y))
					{
						// This happens if we were already planning to expand here. Try somewhere else.
						buildingInTheWay = true;
						break;
					}

					// TODO bug: this doesn't include enemy buildings which are known but out of sight
					for (const auto unit : BWAPI::Broodwar->getUnitsOnTile(BWAPI::TilePosition(tile.x + x, tile.y + y)))
					{
						if (unit->getType().isBuilding() && !unit->isLifted())
						{
							buildingInTheWay = true;
							break;
						}
					}
				}
			}
		
			if (buildingInTheWay)
			{
				continue;
			}

			// Want to be far from the enemy base.
			double distanceFromEnemy = 0.0;
			if (enemyBase) 
			{
				BWAPI::TilePosition enemyTile = enemyBase->Location();
				BWAPI::Position enemyPos(enemyTile);
				distanceFromEnemy = getGroundTileDistance(enemyTile, tile);
				if (distanceFromEnemy < 0)
				{
					//BWAPI::Broodwar->printf("Base not connected");
					distanceFromEnemy = 0.0;
				}
			}

			// Add up the score.
			score = hidden ? (distanceFromEnemy + distanceFromUs * 0.5) : (distanceFromEnemy * 0.5 - distanceFromUs);

			// More resources -> better.
			// NOTE The number of mineral patchers/geysers controls how fast we can mine.
			//      The resource amount controls how long we can keep mining.
			int mineralSize = 0;
			int mineralNumber = 0;
			for (const auto &m : base.Minerals())
			{
				mineralSize += m->Amount();
				mineralNumber++;
			}

			int gasSize = 0;
			int gasNumber = 0;
			for (const auto &g : base.Geysers())
			{
				gasSize += g->Amount();
				gasNumber++;
			}

			score += 5 * mineralNumber + 0.005 * mineralSize;
			if (!wantMinerals)
			{
				score += 20 * gasNumber + 0.01 * gasSize;
			}
			else
			{
				score += 5 * gasNumber + 0.0025 * gasSize;
			}

			// Big penalty for enemy buildings in the same region.
			if (InformationManager::Instance().isEnemyBuildingInRegion(base.GetArea()))
			{
				score -= 100.0;
			}

			//BWAPI::Broodwar->printf("base score %d, %d -> %f",  tile.x, tile.y, score);
			BWAPI::Broodwar->drawTextMap(BWAPI::Position(tile), "%c%g", green, score);
			if (!bestBase || score > bestScore)
			{
				bestBase = &base;
				bestScore = score;
			}
		}
	}

	if (bestBase)
	{
		return bestBase;
	}
	if (!wantMinerals)
	{
		// We wanted a gas base and there isn't one. Try for a mineral-only base.
		return nextExpansion(hidden, true);
	}
	return nullptr;
}

BWAPI::TilePosition MapTools::getNextExpansion(bool hidden, bool minOnlyOK)
{
	const BWEM::Base * base = nextExpansion(hidden, minOnlyOK);
	if (base)
	{
		// BWAPI::Broodwar->printf("foresee base @ %d, %d", base->getTilePosition().x, base->getTilePosition().y);
		return base->Location();
	}
	return BWAPI::TilePositions::None;
}

BWAPI::TilePosition MapTools::reserveNextExpansion(bool hidden, bool minOnlyOK)
{
	const BWEM::Base * base = nextExpansion(hidden, minOnlyOK);
	if (base)
	{
		// BWAPI::Broodwar->printf("reserve base @ %d, %d", base->getTilePosition().x, base->getTilePosition().y);
		InformationManager::Instance().reserveBase(base);
		return base->Location();
	}
	return BWAPI::TilePositions::None;
}

bool MapTools::haveWallPosition(BWAPI::UnitType type)
{
	// Search through each wall to find valid placements
	for (auto &[_, wall] : BWEB::Walls::getWalls()) {
		std::set<BWAPI::TilePosition> placements;

		if (type.tileWidth() == 4)
			placements = wall.getLargeTiles();
		else if (type.tileWidth() == 3)
			placements = wall.getMediumTiles();
		else
			placements = wall.getSmallTiles();

		for (auto &tile : placements) {
			if (BWEB::Map::isUsed(tile) == BWAPI::UnitTypes::None) {
				return true;
			}
		}
	}

	return false;
}

BWAPI::TilePosition MapTools::getBuildPosition(BWAPI::UnitType type, const BWAPI::TilePosition searchCenter, BWAPI::Unit builderUnit)
{
	auto distBest = DBL_MAX;
	auto tileBest = BWAPI::TilePositions::Invalid;
	BWAPI::Position s = BWAPI::Position(searchCenter);

	// Search through each block to find the closest block and valid position
	for (auto &block : BWEB::Blocks::getBlocks()) {
		std::set<BWAPI::TilePosition> placements;

		if (type.tileWidth() == 4)
			placements = block.getLargeTiles();
		else if (type.tileWidth() == 3)
			placements = block.getMediumTiles();
		else
			placements = block.getSmallTiles();

		for (auto &tile : placements) {
			BWAPI::Position p = BWAPI::Position(tile) + BWAPI::Position(type.tileWidth() * 16, type.tileHeight() * 16);
			const auto dist = p.getApproxDistance(s);
			if (dist < distBest && isPlaceable(type, tile) &&
				BWAPI::Broodwar->canBuildHere(tile, type, builderUnit) && creepCheck(type, tile) && psiCheck(type, tile)) {
				distBest = dist;
				tileBest = tile;
			}
		}
	}

	// Search through nearest station to find the closest valid TilePosition
	if (BWEB::Stations::getStations().size())
	{
		auto station = BWEB::Stations::getClosestStation(searchCenter);
		if (type.tileWidth() == 4)
		{
			std::set<BWAPI::TilePosition> placements;
			placements = station->getSecondaryLocations();
			for (auto &tile : placements) {
				BWAPI::Position p = BWAPI::Position(tile) + BWAPI::Position(type.tileWidth() * 16, type.tileHeight() * 16);
				const auto dist = p.getApproxDistance(s);
				if (dist < distBest && isPlaceable(type, tile) &&
					BWAPI::Broodwar->canBuildHere(tile, type, builderUnit) && creepCheck(type, tile) && psiCheck(type, tile)) {
					distBest = dist;
					tileBest = tile;
				}
			}
		}
		if (type.tileWidth() == 3)
		{
			auto tile = station->getMediumPosition();
			if (isPlaceable(type, tile) &&
				BWAPI::Broodwar->canBuildHere(tile, type, builderUnit) && creepCheck(type, tile) && psiCheck(type, tile))
				tileBest = tile;
		}
		if (type.tileWidth() == 2)
		{
			auto tile = station->getSmallPosition();
			if (isPlaceable(type, tile) &&
				BWAPI::Broodwar->canBuildHere(tile, type, builderUnit) && creepCheck(type, tile) && psiCheck(type, tile))
				tileBest = tile;
		}
	}

	return tileBest;
}

BWAPI::TilePosition MapTools::getDefBuildPosition(BWAPI::UnitType type, const BWAPI::TilePosition searchCenter, BWAPI::Unit builderUnit)
{
	auto distBest = DBL_MAX;
	auto tileBest = BWAPI::TilePositions::Invalid;
	BWAPI::Position s = BWAPI::Position(searchCenter);

	// Search through each wall to find the closest valid TilePosition
	for (auto &[_, wall] : BWEB::Walls::getWalls()) {
		std::set<BWAPI::TilePosition> placements;

		if (type.tileWidth() == 4)
			placements = wall.getLargeTiles();
		else if (type.tileWidth() == 3)
			placements = wall.getMediumTiles();
		else
			placements = wall.getSmallTiles();

		for (auto &tile : placements) {
			BWAPI::Position p = BWAPI::Position(tile) + BWAPI::Position(type.tileWidth() * 16, type.tileHeight() * 16);
			const auto dist = p.getApproxDistance(s);
			if (dist < distBest && isPlaceable(type, tile) &&
				BWAPI::Broodwar->canBuildHere(tile, type, builderUnit) && creepCheck(type, tile) && psiCheck(type, tile)) {
				distBest = dist;
				tileBest = tile;
			}
		}

		if (type == BWAPI::UnitTypes::Protoss_Photon_Cannon
			|| type == BWAPI::UnitTypes::Zerg_Creep_Colony
			|| type == BWAPI::UnitTypes::Zerg_Sunken_Colony
			|| type == BWAPI::UnitTypes::Zerg_Spore_Colony)
			placements = wall.getDefenses();

		for (auto &tile : placements) {
			BWAPI::Position p = BWAPI::Position(tile) + BWAPI::Position(type.tileWidth() * 16, type.tileHeight() * 16);
			const auto dist = p.getApproxDistance(s);
			if (dist < distBest && isPlaceable(type, tile) &&
				BWAPI::Broodwar->canBuildHere(tile, type, builderUnit) && creepCheck(type, tile) && psiCheck(type, tile)) {
				distBest = dist;
				tileBest = tile;
			}
		}
	}

	if (type.tileWidth() == 2)
	{
		// Search through nearest station to find the closest valid TilePosition
		auto station = BWEB::Stations::getClosestStation(searchCenter);
		if (BWEB::Stations::getStations().size())
		for (auto &tile : station->getDefenses()) {
			BWAPI::Position p = BWAPI::Position(tile) + BWAPI::Position(type.tileWidth() * 16, type.tileHeight() * 16);
			const auto dist = p.getApproxDistance(s);
			if (dist < distBest && isPlaceable(type, tile) &&
				BWAPI::Broodwar->canBuildHere(tile, type, builderUnit) && creepCheck(type, tile) && psiCheck(type, tile)) {
				distBest = dist;
				tileBest = tile;
			}
		}

		// Search through each block to find the closest block and valid TilePosition
		for (auto &block : BWEB::Blocks::getBlocks()) {
			for (auto &tile : block.getSmallTiles()) {
				BWAPI::Position p = BWAPI::Position(tile) + BWAPI::Position(type.tileWidth() * 16, type.tileHeight() * 16);
				const auto dist = p.getApproxDistance(s);
				if (dist < distBest && isPlaceable(type, tile) &&
					BWAPI::Broodwar->canBuildHere(tile, type, builderUnit) && creepCheck(type, tile) && psiCheck(type, tile)) {
					distBest = dist;
					tileBest = tile;
				}
			}
		}
	}

	return tileBest;
}

BWAPI::TilePosition MapTools::getStationBuildPosition(BWAPI::UnitType type, const BWAPI::TilePosition searchCenter, BWAPI::Unit builderUnit)
{
	auto distBest = DBL_MAX;
	auto tileBest = BWAPI::TilePositions::Invalid;
	BWAPI::Position s = BWAPI::Position(searchCenter);

	if (type.tileWidth() == 2)
	{
		// Search through nearest station to find the closest valid TilePosition
		auto station = BWEB::Stations::getClosestStation(searchCenter);
		if (BWEB::Stations::getStations().size())
		for (auto &tile : station->getDefenses()) {
			BWAPI::Position p = BWAPI::Position(tile) + BWAPI::Position(type.tileWidth() * 16, type.tileHeight() * 16);
			const auto dist = p.getApproxDistance(s);
			if (dist < distBest && dist < 6 * 32 && isPlaceable(type, tile) &&
				BWAPI::Broodwar->canBuildHere(tile, type, builderUnit) && creepCheck(type, tile) && psiCheck(type, tile)) {
				distBest = dist;
				tileBest = tile;
			}
		}

		// Search through each block to find the closest block and valid TilePosition
		for (auto &block : BWEB::Blocks::getBlocks()) {
			for (auto &tile : block.getSmallTiles()) {
				BWAPI::Position p = BWAPI::Position(tile) + BWAPI::Position(type.tileWidth() * 16, type.tileHeight() * 16);
				const auto dist = p.getApproxDistance(s);
				if (dist < distBest && dist < 6 * 32 && isPlaceable(type, tile) &&
					BWAPI::Broodwar->canBuildHere(tile, type, builderUnit) && creepCheck(type, tile) && psiCheck(type, tile)) {
					distBest = dist;
					tileBest = tile;
				}
			}
		}
	}

	return tileBest;
}

bool MapTools::creepCheck(BWAPI::UnitType type, BWAPI::TilePosition tile)
{
	if (type.getRace() != BWAPI::Races::Zerg)
		return true;

	// Creep check
	for (int x = tile.x; x < tile.x + type.tileWidth(); x++) {
		for (int y = tile.y; y < tile.y + type.tileHeight(); y++) {
			BWAPI::TilePosition t(x, y);
			if (!t.isValid())
				return false;
			if (type.requiresCreep() && !BWAPI::Broodwar->hasCreep(t))
				return false;
		}
	}

	return true;
}

bool MapTools::psiCheck(BWAPI::UnitType type, BWAPI::TilePosition tile)
{
	if (type.getRace() != BWAPI::Races::Protoss)
		return true;

	// Psi check
	if (type.requiresPsi() && !BWAPI::Broodwar->hasPower(tile, type))
		return false;

	return true;
}

bool MapTools::isPlaceable(BWAPI::UnitType type, const BWAPI::TilePosition location)
{
	if (type.requiresCreep()) {
		for (auto x = location.x; x < location.x + type.tileWidth(); x++) {
			const BWAPI::TilePosition creepTile(x, location.y + type.tileHeight());
			if (!BWAPI::Broodwar->isBuildable(creepTile))
				return false;
		}
	}

	//if (type.isResourceDepot() && !BWAPI::Broodwar->canBuildHere(location, type, builder))
		//return false;

	for (auto x = location.x; x < location.x + type.tileWidth(); x++) {
		for (auto y = location.y; y < location.y + type.tileHeight(); y++) {

			const BWAPI::TilePosition tile(x, y);
			if (!tile.isValid()
				|| !BWAPI::Broodwar->isBuildable(tile)
				|| !BWAPI::Broodwar->isWalkable(BWAPI::WalkPosition(tile))
				|| BWEB::Map::isUsed(tile) != BWAPI::UnitTypes::None)
				return false;
		}
	}
	return true;
}

std::string MapTools::getMacroLocationString(MacroLocation MacroLocation) const
{
	if (MacroLocation == MacroLocation::Macro)
	{
		return "Macro";
	}
	if (MacroLocation == MacroLocation::Main)
	{
		return "Main";
	}
	if (MacroLocation == MacroLocation::Natural)
	{
		return "Natural";
	}
	if (MacroLocation == MacroLocation::Third)
	{
		return "Third";
	}
	if (MacroLocation == MacroLocation::Expo)
	{
		return "Expo";
	}
	if (MacroLocation == MacroLocation::MinOnly)
	{
		return "MinOnly";
	}
	if (MacroLocation == MacroLocation::GasOnly)
	{
		return "GasOnly";
	}
	if (MacroLocation == MacroLocation::Hidden)
	{
		return "Hidden";
	}
	if (MacroLocation == MacroLocation::Center)
	{
		return "Center";
	}
	if (MacroLocation == MacroLocation::EnemyMain)
	{
		return "EnemyMain";
	}
	if (MacroLocation == MacroLocation::EnemyNatural)
	{
		return "EnemyNatural";
	}
	if (MacroLocation == MacroLocation::Proxy)
	{
		return "Proxy";
	}
	if (MacroLocation == MacroLocation::LeastDefendedGround)
	{
		return "LeastDefendedGround";
	}
	if (MacroLocation == MacroLocation::LeastDefendedAir)
	{
		return "LeastDefendedAir";
	}

	return "Anywhere";
}

BWAPI::Position MapTools::getNaturalDefPosition()
{
	// Default defense position
	BWAPI::Position np = InformationManager::Instance().getNaturalPosition();
	BWAPI::Position dp = np;

	// Calculate defense position from chokes and natural positions
	const BWEM::ChokePoint * choke1 = InformationManager::Instance().getNaturalChoke();
	const BWEM::ChokePoint * choke2 = InformationManager::Instance().getMainChoke();
	if (choke1 && choke2)
	{
		BWAPI::Position cp1 = BWAPI::Position(choke1->Center());
		BWAPI::Position cp2 = BWAPI::Position(choke2->Center());
		// Sunken placement code inspired by AILien
		dp = (np * 2 + cp1 * 2 + cp2 * 1) / (2 + 2 + 1);
	}

	return dp;
}

BWAPI::Position MapTools::getMainDefPosition()
{
	// Default defense position
	BWAPI::Position mp = InformationManager::Instance().getMainPosition();
	BWAPI::Position dp = mp;

	// Calculate defense position from chokes and main positions
	const BWEM::ChokePoint * choke = InformationManager::Instance().getMainChoke();
	if (choke)
	{
		BWAPI::Position cp = BWAPI::Position(choke->Center());
		// Sunken placement code inspired by AILien
		int distance = (int)mp.getApproxDistance(cp) / 150;
		dp = (mp * distance + cp) / (distance + 1);
	}

	return dp;
}

BWAPI::Position MapTools::getBaseDefPosition(BWAPI::Position dp)
{
	// Default defense position
	BWAPI::Position center = BWAPI::Position(BWAPI::Broodwar->mapWidth() * 32 / 2, BWAPI::Broodwar->mapHeight() * 32 / 2);
	BWAPI::Position dpbase = dp;

	// Calculate defense position from center and base position
	// Sunken placement code inspired by AILien
	int distance = (int)dpbase.getApproxDistance(center) / 100;
	dp = (dpbase * distance + center) / (distance + 1);

	return dp;
}

BWAPI::Position MapTools::getMineralDefPosition(BWAPI::Position dp)
{
	// Default defense position
	BWAPI::Position dpbase = dp;
	
	// Get average mineral position
	// Arrak's building placement code
	auto minerals = BWAPI::Broodwar->getUnitsInRadius(dpbase, 6 * 32, BWAPI::Filter::IsMineralField);
	int n = 0;
	if (!minerals.empty()) {
		BWAPI::Position AvgMineralPosition(0, 0);
		for (auto & mineral : minerals) {
			n++;
			AvgMineralPosition += mineral->getInitialPosition();
		}
		AvgMineralPosition = BWAPI::Position(AvgMineralPosition.x / n, AvgMineralPosition.y / n);
		dp = BWAPI::Position((dpbase.x + 2 * AvgMineralPosition.x) / 3, (dpbase.y + 2 * AvgMineralPosition.y) / 3);
	}
	//if we can't find any minerals, it doesn't matter.

	return dp;
}

BWAPI::Position MapTools::getRandomDefPosition(BWAPI::Position dp)
{
	// Arrak's building placement code
	BWAPI::Position randomOffset(32 * rand() % 7, 32 * rand() % 7);
	dp += randomOffset;

	return dp;
}
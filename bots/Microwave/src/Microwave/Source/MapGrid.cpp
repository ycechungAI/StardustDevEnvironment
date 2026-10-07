#include "Common.h"
#include "MapGrid.h"
#include "InformationManager.h"
#include <random>

using namespace UAlbertaBot;

MapGrid & MapGrid::Instance()
{
	static MapGrid instance(BWAPI::Broodwar->mapWidth() * 32, BWAPI::Broodwar->mapHeight() * 32, Config::Tools::MAP_GRID_SIZE);
	return instance;
}

MapGrid::MapGrid() {}

MapGrid::MapGrid(int mapWidth, int mapHeight, int cellSize)
	: mapWidth(mapWidth)
	, mapHeight(mapHeight)
	, cellSize(cellSize)
	, cols((mapWidth + cellSize - 1) / cellSize)
	, rows((mapHeight + cellSize - 1) / cellSize)
	, cells(rows * cols)
	, lastUpdated(0)
{
	calculateCellCenters();
}


// Return the first of:
// 1. Any starting base location that has not been explored.
// 2. The least-recently explored accessible cell (with attention to byGround).
// Item 1 ensures that, if early game scouting failed, we scout with force.
// If byGround, only locations that are accessible by ground from the given location.
// If not byGround, the location position is not used.
BWAPI::Position MapGrid::getLeastExplored(bool byGround, BWAPI::Position position)
{
	BWAPI::Position offset(64, 48);
	BWAPI::Position startPosition(BWAPI::Broodwar->self()->getStartLocation());
	BWAPI::TilePosition startLocation(BWAPI::Broodwar->self()->getStartLocation());

	// 1. Any starting location that has not been explored.
	for (BWAPI::TilePosition tile : BWAPI::Broodwar->getStartLocations())
	{
		// Skip places that we can't get to.
		if (byGround)
		{
			int distance = MapTools::Instance().getGroundTileDistance(startLocation, tile);
			if (distance < 0)
			{
				continue;
			}
		}

		if (!BWAPI::Broodwar->isExplored(tile))
		{
			return BWAPI::Position(tile) + offset;
		}
	}

	// 2. The most distant of the least-recently explored tiles.
	int minSeen = 1000000;
	int minSeenDist = 0;
	int leastRow(0), leastCol(0);

	for (int r = 0; r<rows; ++r)
	{
		for (int c = 0; c<cols; ++c)
		{
			// get the center of this cell
			BWAPI::Position cellCenter = getCellCenter(r, c);

			// Skip places that we can't get to.
			if (byGround)
			{
				int distance = MapTools::Instance().getGroundTileDistance(startLocation, BWAPI::TilePosition(cellCenter));
				if (distance < 0)
				{
					continue;
				}
			}

			int dist = startPosition.getApproxDistance(getCellByIndex(r, c).center);
			int lastVisited = getCellByIndex(r, c).timeLastVisited;

			if (lastVisited < minSeen || (lastVisited == minSeen && dist > minSeenDist))
			{
				leastRow = r;
				leastCol = c;
				minSeen = lastVisited;
				minSeenDist = dist;
			}
		}
	}

	return getCellCenter(leastRow, leastCol);
}

// Return the least-recently explored cell "near" the given position.
// This one does not care about starting positions or distance from home.
BWAPI::Position MapGrid::getLeastExploredNear(const BWAPI::Position & center, bool byGround)
{
	BWAPI::Position offset(64, 48);
	BWAPI::Position startPosition(BWAPI::Broodwar->self()->getStartLocation());
	BWAPI::TilePosition startLocation(BWAPI::Broodwar->self()->getStartLocation());

	int minSeen = 1000000;
	int leastRow(0), leastCol(0);

	for (int r = 0; r < rows; ++r)
	{
		for (int c = 0; c < cols; ++c)
		{
			BWAPI::Position cellCenter = getCellCenter(r, c);

			// Skip places that we can't get to.
			if (byGround)
			{
				int distance = MapTools::Instance().getGroundTileDistance(startLocation, BWAPI::TilePosition(cellCenter));
				if (distance < 0)
				{
					continue;
				}
			}

			if (center.getApproxDistance(cellCenter) <= 32 * 32 &&
				BWEMmap.GetArea(BWAPI::WalkPosition(center)) == BWEMmap.GetArea(BWAPI::WalkPosition(cellCenter)))
			{
				int lastVisited = getCellByIndex(r, c).timeLastVisited;
				//BWAPI::Broodwar->printf("lastVisited: %d %d frame: %d", leastRow, leastCol, lastVisited);

				if (lastVisited < minSeen)
				{
					leastRow = r;
					leastCol = c;
					minSeen = lastVisited;
				}
			}
		}
	}

	return getCellCenter(leastRow, leastCol);
}

BWAPI::Position MapGrid::getLeastExploredNearUs()
{
	int minSeen = 1000000;
	int minSeenDist = 0;
	int leastRow(0), leastCol(0);

	BWAPI::Position home(BWAPI::Broodwar->self()->getStartLocation());
	BWAPI::Position leastposition = home;

	for (int r = 0; r<rows; ++r)
	{
		for (int c = 0; c<cols; ++c)
		{
			int i = 0;
			for (auto& area : BWEMmap.Areas())
			{
				for (const auto& base : area.Bases())
				{
					if (InformationManager::Instance().getBaseOwner(&base) == BWAPI::Broodwar->self())
					{
						BWAPI::Position basePosition(base.Center());
						int dist = basePosition.getApproxDistance(getCellByIndex(r, c).center);
						int lastVisited = getCellByIndex(r, c).timeSurveyorLastVisited;
						int timeLastOpponentSeen = getCellByIndex(r, c).timeLastOpponentSeen;
						int framecount = BWAPI::Broodwar->getFrameCount();

						if (dist < 600 && timeLastOpponentSeen < framecount &&
							(lastVisited < minSeen || (lastVisited == minSeen && dist > minSeenDist)))
						{
							leastRow = r;
							leastCol = c;
							leastposition = getCellCenter(leastRow, leastCol);
							minSeen = lastVisited;
							minSeenDist = dist;
							//BWAPI::Broodwar->printf("Near base %d - dist: %f", i, dist);
						}
					}

					i++;
				}
			}
		}
	}

	return leastposition;
}

BWAPI::Position MapGrid::getLeastExploredNearUs(BWAPI::Position position)
{
	int minSeen = 1000000;
	int minSeenDist = 0;
	int leastRow(0), leastCol(0);

	BWAPI::Position home(BWAPI::Broodwar->self()->getStartLocation());
	BWAPI::Position leastposition = home;

	for (int r = 0; r<rows; ++r)
	{
		for (int c = 0; c<cols; ++c)
		{
			int i = 0;
			for (auto& area : BWEMmap.Areas())
			{
				for (const auto& base : area.Bases())
				{
					if (InformationManager::Instance().getBaseOwner(&base) == BWAPI::Broodwar->self())
					{
						BWAPI::Position basePosition(base.Center());
						int dist = basePosition.getApproxDistance(getCellByIndex(r, c).center);
						int distPosition = position.getApproxDistance(getCellByIndex(r, c).center);
						int lastVisited = getCellByIndex(r, c).timeSurveyorLastVisited;
						int timeLastOpponentSeen = getCellByIndex(r, c).timeLastOpponentSeen;
						int framecount = BWAPI::Broodwar->getFrameCount();

						if (dist < 600)
						{
							//BWAPI::Broodwar->printf("Near base %d - dist = %f", i, dist);

							if (distPosition < 400)
							{
								//BWAPI::Broodwar->printf("Near base %d - distposition = %f", i, dist, distPosition);

								if (timeLastOpponentSeen < framecount &&
									(lastVisited < minSeen || (lastVisited == minSeen && dist > minSeenDist)))
								{
									//BWAPI::Broodwar->printf("Near base %d - lastVisited = %d - minSeen = %d - minSeenDist = %f", i, lastVisited, minSeen, minSeenDist);
									leastRow = r;
									leastCol = c;
									leastposition = getCellCenter(leastRow, leastCol);
									minSeen = lastVisited;
									minSeenDist = dist;
								}
							}
						}
					}

					i++;
				}
			}
		}
	}

	return leastposition;
}

void MapGrid::calculateCellCenters()
{
	for (int r = 0; r < rows; ++r)
	{
		for (int c = 0; c < cols; ++c)
		{
			GridCell & cell = getCellByIndex(r, c);

			int centerX = (c * cellSize) + (cellSize / 2);
			int centerY = (r * cellSize) + (cellSize / 2);

			// if the x position goes past the end of the map
			if (centerX > mapWidth)
			{
				// when did the last cell start
				int lastCellStart = c * cellSize;

				// how wide did we go
				int tooWide = mapWidth - lastCellStart;

				// go half the distance between the last start and how wide we were
				centerX = lastCellStart + (tooWide / 2);
			}
			else if (centerX == mapWidth)
			{
				centerX -= 50;
			}

			if (centerY > mapHeight)
			{
				// when did the last cell start
				int lastCellStart = r * cellSize;

				// how wide did we go
				int tooHigh = mapHeight - lastCellStart;

				// go half the distance between the last start and how wide we were
				centerY = lastCellStart + (tooHigh / 2);
			}
			else if (centerY == mapHeight)
			{
				centerY -= 50;
			}

			cell.center = BWAPI::Position(centerX, centerY);
			assert(cell.center.isValid());
		}
	}
}

BWAPI::Position MapGrid::getCellCenter(int row, int col)
{
	return getCellByIndex(row, col).center;
}

// clear the vectors in the grid
void MapGrid::clearGrid() {

	for (size_t i(0); i<cells.size(); ++i)
	{
		cells[i].ourUnits.clear();
		cells[i].oppUnits.clear();
	}
}

// populate the grid with units
void MapGrid::update()
{
	// Profile debug
	//PROFILE_FUNCTION();

	if (Config::Debug::DrawMapGrid)
	{
		for (int i = 0; i<cols; i++)
		{
			BWAPI::Broodwar->drawLineMap(i*cellSize, 0, i*cellSize, mapHeight, BWAPI::Colors::Blue);
		}

		for (int j = 0; j<rows; j++)
		{
			BWAPI::Broodwar->drawLineMap(0, j*cellSize, mapWidth, j*cellSize, BWAPI::Colors::Blue);
		}

		for (int r = 0; r < rows; ++r)
		{
			for (int c = 0; c < cols; ++c)
			{
				GridCell & cell = getCellByIndex(r, c);

				BWAPI::Broodwar->drawTextMap(cell.center.x, cell.center.y, "Last Seen %d", cell.timeLastVisited);
				BWAPI::Broodwar->drawTextMap(cell.center.x, cell.center.y + 10, "Row/Col (%d, %d)", r, c);
			}
		}
	}

	// clear the grid
	clearGrid();

	//BWAPI::Broodwar->printf("MapGrid info: WH(%d, %d)  CS(%d)  RC(%d, %d)  C(%d)", mapWidth, mapHeight, cellSize, rows, cols, cells.size());

	// add our units to the appropriate cell
	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		if (unit->isCompleted() || unit->getType().isBuilding())
		{ 
			getCell(unit).ourUnits.insert(unit);
			getCell(unit).timeLastVisited = BWAPI::Broodwar->getFrameCount();
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord) {
				getCell(unit).timeSurveyorLastVisited = BWAPI::Broodwar->getFrameCount();
			}
		}
	}

	// add enemy units to the appropriate cell
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->exists() &&
			(unit->isCompleted() || unit->getType().isBuilding()) &&
			unit->getHitPoints() > 0 &&
			unit->getType() != BWAPI::UnitTypes::Unknown)
		{
			getCell(unit).oppUnits.insert(unit);
			getCell(unit).timeLastOpponentSeen = BWAPI::Broodwar->getFrameCount();
		}
	}
}

void MapGrid::GetUnits(BWAPI::Unitset & units, BWAPI::Position center, int radius, bool ourUnits, bool oppUnits)
{
	const int x0(std::max((center.x - radius) / cellSize, 0));
	const int x1(std::min((center.x + radius) / cellSize, cols - 1));
	const int y0(std::max((center.y - radius) / cellSize, 0));
	const int y1(std::min((center.y + radius) / cellSize, rows - 1));
	const int radiusSq(radius * radius);

	for (int y(y0); y <= y1; ++y)
	{
		for (int x(x0); x <= x1; ++x)
		{
			int row = y;
			int col = x;

			const GridCell & cell(getCellByIndex(row, col));
			if (ourUnits)
			{
				for (auto & unit : cell.ourUnits)
				{
					BWAPI::Position d(unit->getPosition() - center);
					if (d.x * d.x + d.y * d.y <= radiusSq)
					{
						if (!units.contains(unit))
						{
							units.insert(unit);
						}
					}
				}
			}
			if (oppUnits)
			{
				for (const auto & unit : cell.oppUnits) if (unit->getType() != BWAPI::UnitTypes::Unknown && unit->isVisible())
				{
					BWAPI::Position d(unit->getPosition() - center);
					if (d.x * d.x + d.y * d.y <= radiusSq)
					{
						if (!units.contains(unit))
						{
							units.insert(unit);
						}
					}
				}
			}
		}
	}
}

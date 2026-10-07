#pragma once

#include <Common.h>
#include "MicroManager.h"

namespace UAlbertaBot
{

	class GridCell
	{
	public:

		int             timeLastVisited;
		int             timeSurveyorLastVisited;
		int             timeLastOpponentSeen;
		int             timeLastScan;
		int             timeLastDarkSwarm;
		int             timeLastPlague;
		BWAPI::Unitset  ourUnits;
		BWAPI::Unitset  oppUnits;
		BWAPI::Position center;

		// Not the ideal place for this constant, but this is where it is used.
		const static int ScanDuration = 240;    // approximate time that a comsat scan provides vision
		const static int DarkSwarmDuration = 240;    // approximate time that a comsat scan provides vision
		const static int PlagueDuration = 240;    // approximate time that a comsat scan provides vision

		GridCell()
			: timeLastVisited(0)
			, timeSurveyorLastVisited(0)
			, timeLastOpponentSeen(0)
			, timeLastScan(-ScanDuration)
			, timeLastDarkSwarm(-DarkSwarmDuration)
			, timeLastPlague(-PlagueDuration)
		{
		}
	};


	class MapGrid
	{
		MapGrid();
		MapGrid(int mapWidth, int mapHeight, int cellSize);

		int							cellSize;
		int							mapWidth, mapHeight;
		int							rows, cols;
		int							lastUpdated;

		std::vector< GridCell >		cells;

		void						calculateCellCenters();

		void						clearGrid();
		BWAPI::Position				getCellCenter(int x, int y);

		BWAPI::Position				naturalExpansion;

	public:

		// yay for singletons!
		static MapGrid &	Instance();

		void				update();
		void				GetUnits(BWAPI::Unitset & units, BWAPI::Position center, int radius, bool ourUnits, bool oppUnits);
		BWAPI::Position		getLeastExplored(bool byGround, BWAPI::Position position);
		BWAPI::Position     getLeastExploredNear(const BWAPI::Position & center, bool byGround);
		BWAPI::Position		getLeastExploredNearUs();
		BWAPI::Position		getLeastExploredNearUs(BWAPI::Position position);

		GridCell & getCellByIndex(int r, int c)		{ return cells[r*cols + c]; }
		GridCell & getCell(BWAPI::Position pos)		{ return getCellByIndex(pos.y / cellSize, pos.x / cellSize); }
		GridCell & getCell(BWAPI::Unit unit)		{ return getCell(unit->getPosition()); }
	};

}
#include "GridCreep.h"

#include "Base.h"
#include "The.h"


// Keep track of zerg creep on the map.
// 1. Discover enemy zerg bases sooner by seeing the creep.
// 2. First scout faster by skipping bases with no creep.
// 
// Long-term goals:
// 3. Know where static creep is, on maps that have it. Exploit it, build there.

using namespace UAlbertaBot;

// Based on the analysis at Making Computer Do Things by Sonko,
// which is itself based on the OpenBW code.
bool GridCreep::tileCanHaveCreep(const BWAPI::TilePosition & pos) const
{
	// This isBuildable takes resources into account.
	// isWalkable() means is fully walkable.
	if (!the.map.isBuildable(pos) || !the.map.isWalkable(pos))
	{
		return false;
	}

	if (pos.y == BWAPI::Broodwar->mapHeight() - 1)
	{
		return true;
	}

	// This isBuildable takes only terrain into account.
	return BWAPI::Broodwar->isBuildable(pos, false);
}

// Find the creep from one building that produces creep.
// NOTE Close for initial base hatcheries, but slightly underpredicts which tiles will have creep.
//      This is intentional, since it's not so easy to fix and underprediction prevents errors.
// TODO does not take barriers into account, so it predicts creep where it cannot reach
void GridCreep::setCreep(BWAPI::Unit building)
{
	BWAPI::Position center(
		(building->getLeft() + building->getRight()) / 2,
		(building->getTop() + building->getBottom()) / 2);
	BWAPI::Position focus1 = center - BWAPI::Position(8 * 32, 0);
	BWAPI::Position focus2 = center + BWAPI::Position(8 * 32, 0);

	for (int x = std::max(0, building->getTilePosition().x - 10);
		x <= std::min(BWAPI::Broodwar->mapWidth() - 1, building->getTilePosition().x + building->getType().width() + 10);
		++x)
	{
		for (int y = std::max(0, building->getTilePosition().y - 7);
			y <= std::min(BWAPI::Broodwar->mapHeight() - 1, building->getTilePosition().y + building->getType().height() + 7);
			++y)
		{
			BWAPI::TilePosition tile(x, y);
			BWAPI::Position xy = TileCenter(tile);
			if (tileCanHaveCreep(tile) && focus1.getDistance(xy) + focus2.getDistance(xy) <= 20 * 32)
			{
				grid[x][y] = 1;
			}
		}
	}
}

// Set the creep at the start of the game.
// Consider neutral zerg creep spreaders on the map and our initial hatchery.
// TODO Creep is not accurate enough to be useful.
void GridCreep::setInitialCreep()
{
	//for (BWAPI::Unit u : BWAPI::Broodwar->getStaticNeutralUnits())
	//{
	//	if (u->getType().producesCreep())
	//	{
	//		setCreep(u);
	//	}
	//}

	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (u->getType().producesCreep())
		{
			setCreep(u);
		}
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

GridCreep::GridCreep()
    : Grid(BWAPI::Broodwar->mapWidth(), BWAPI::Broodwar->mapHeight(), 0)
	, _useful(false)
{
}

// Run this once at the start of the game to initialize the grid of creep-covered tiles.
// After that, we rely on vision to notice new creep.
// It only matters if we are zerg or the enemy is or might be zerg.
void GridCreep::initialize()
{
	_useful = the.enemyRace() == BWAPI::Races::Zerg;

	// The ideal:
	//_useful = the.selfRace() == BWAPI::Races::Zerg || the.enemyRace() == BWAPI::Races::Zerg || the.enemyRace() == BWAPI::Races::Unknown;

	// The initial creep is not calculated with useful accuracy. Yet.
	//if (_useful)
	//{
	//	setInitialCreep();
	//}
}

void GridCreep::update()
{
	if (_useful)
	{
		if (the.enemyRace() != BWAPI::Races::Zerg && the.enemyRace() != BWAPI::Races::Unknown)
		{
			_useful = false;
			return;
		}

		_newCreep.clear();
		for (int x = 0; x < BWAPI::Broodwar->mapWidth(); ++x)
		{
			for (int y = 0; y < BWAPI::Broodwar->mapHeight(); ++y)
			{
				BWAPI::TilePosition tile(x, y);
				int there = BWAPI::Broodwar->hasCreep(tile);		// 0 or 1
				int here = at(x, y);								// 0 or 1
				if (there != here)
				{
					if (there)
					{
						_newCreep.push_back(tile);
					}
					grid[x][y] = there;
				}
			}
		}
	}
}

void GridCreep::draw()
{
	for (int x = 0; x < BWAPI::Broodwar->mapWidth(); ++x)
	{
		for (int y = 0; y < BWAPI::Broodwar->mapHeight(); ++y)
		{
			if (at(x,y))
			{
				BWAPI::Broodwar->drawCircleMap(TileCenter(BWAPI::TilePosition(x,y)), 2, BWAPI::Colors::Green, true);
			}

			// For debugging when predicting unseen creep.
			//if (BWAPI::Broodwar->hasCreep(BWAPI::TilePosition(x, y)))
			//{
			//	BWAPI::Broodwar->drawCircleMap(TileCenter(BWAPI::TilePosition(x, y)), 3, BWAPI::Colors::Red, false);
			//}
		}
	}
}

// For use by the early game first scout which searches out the enemy base, when the enemy is zerg.
// true: If the enemy base were here, we should see creep and do not. Therefore it is not here.
// false: We haven't seen enough to know yet.
// NOTE Could be wrong if we approach from a direction where something blocks creep spread.
//      I don't know any map where that's possible.
bool GridCreep::missingCreep(Base * base) const
{
	BWAPI::Position center = base->getCenter();
	BWAPI::Position focus1 = center - BWAPI::Position(8 * 32, 0);
	BWAPI::Position focus2 = center + BWAPI::Position(8 * 32, 0);

	// Scan tiles near the base for visibility and creep.
	for (int x = std::max(0, base->getTilePosition().x - 8); x <= std::min(BWAPI::Broodwar->mapWidth() - 1, base->getTilePosition().x + 12); ++x)
	{
		for (int y = std::max(0, base->getTilePosition().y - 4); y <= std::min(BWAPI::Broodwar->mapHeight() - 1, base->getTilePosition().y + 9); ++y)
		{
			BWAPI::TilePosition tile(x, y);
			if (BWAPI::Broodwar->isVisible(tile) && !BWAPI::Broodwar->hasCreep(tile) && tileCanHaveCreep(tile))
			{
				BWAPI::Position xy = TileCenter(tile);
				// This test is not perfect; it misses some edge creep tiles.
				// But it creates no false positives unless there are barriers that prevent creep spread.
				if (focus1.getDistance(xy) + focus2.getDistance(xy) <= 20 * 32)
				{
					return true;
				}
			}
		}
	}

	return false;
}

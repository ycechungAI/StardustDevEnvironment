#pragma once

#include "BWAPI.h"
#include "Grid.h"

// TODO in progress

namespace UAlbertaBot
{
class Base;		// forward declaration

class GridCreep : public Grid
{
	bool _useful;

	// Updated every time that new creep is found on the map in update().
	std::vector<BWAPI::TilePosition> _newCreep;

	bool tileCanHaveCreep(const BWAPI::TilePosition & pos) const;
	void setCreep(BWAPI::Unit building);
	void setInitialCreep();

public:

	// Just fills in zeroes.
    GridCreep();

	// To run once at the start of the game.
	void initialize();

	bool isUseful() { return _useful; };
    void update();

	// Creep newly seen this frame. It is erased and refilled every frame.
	const std::vector<BWAPI::TilePosition> & getNewCreep() { return _newCreep; };

	void draw();

	bool missingCreep(Base * base) const;
};

}
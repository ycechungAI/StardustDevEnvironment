#include "SkillMoveout.h"

#include "The.h"

using namespace UAlbertaBot;

// Decide, based on heuristics and saved records, when to move out
// with given units. Otherwise they stay at home.

// Scouting and harassment units are not included.
// They naturally want to be out and about.

// It's essential for terran, and for any units that benefit from mass
// and need to hold back until they have enough.

// Terran: Any main army.
// Protoss: Corsairs versus mutas, get a minimum number.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

void SkillMoveout::initialize()
{
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

// For now, only handle the case of corsairs vs mutalisks, and only by heuristics.
bool SkillMoveout::shouldMoveout(MoveoutKey key, BWAPI::Unitset units)
{
	if (key == MoveoutKey::Corsairs && the.enemyRace() == BWAPI::Races::Zerg)
	{
		return
			units.size() >= size_t(the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk)) ||
			units.size() >= 4;
	}

	return true;
}

SkillMoveout::SkillMoveout()
	: Skill("moveout")
{
	// The game has not started yet, so we can't finish initialization.
	// We have to schedule an update to find the initial base or bases.
	_nextUpdateFrame = 2;
}

// Write the data for the current game to a string.
std::string SkillMoveout::putData() const
{
	std::stringstream s;

	return s.str();
}

// Read in past moveout timings for one game record from a string.
// TODO not implemented yet
void SkillMoveout::getData(GameRecord & r, const std::string & line)
{
}

void SkillMoveout::update()
{
	if (the.now() == 2)
	{
		initialize();

		// We might theoretically find the enemy base within this time, if it's close.
		_nextUpdateFrame = 20 * 24;
	}

	_nextUpdateFrame = MAX_FRAME;
}

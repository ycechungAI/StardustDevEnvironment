#include "Common.h"

int GetIntFromString(const std::string & s)
{
	std::stringstream ss(s);
	int a = 0;
	ss >> a;
	return a;
}

// For example, "Zerg_Zergling" -> "Zergling"
std::string TrimRaceName(const std::string & s)
{
	if (s.substr(0, 5) == "Zerg_")
	{
		return s.substr(5, std::string::npos);
	}
	if (s.substr(0, 8) == "Protoss_")
	{
		return s.substr(8, std::string::npos);
	}
	if (s.substr(0, 7) == "Terran_")
	{
		return s.substr(7, std::string::npos);
	}

	// There is no race prefix. Return it unchanged.
	return s;
}

char RaceChar(BWAPI::Race race)
{
	if (race == BWAPI::Races::Zerg)
	{
		return 'Z';
	}
	if (race == BWAPI::Races::Protoss)
	{
		return 'P';
	}
	if (race == BWAPI::Races::Terran)
	{
		return 'T';
	}
	return 'U';
}

// Safely return the name of a unit type.
// NOTE Can fail for some non-unit unit types which Steamhammer does not use.
std::string UnitTypeName(BWAPI::UnitType type)
{
	if (type == BWAPI::UnitTypes::None) return "None";
	if (type == BWAPI::UnitTypes::Unknown) return "Unknown";

	std::string nicer = TrimRaceName(type.getName());
	std::replace(nicer.begin(), nicer.end(), '_', ' ');

	return nicer;
}

std::string UnitTypeName(BWAPI::Unit unit)
{
	return UnitTypeName(unit->getType());
}

// Predict a visible unit's movement a given number of frames into the future,
// on the assumption that it keeps moving in a straight line.
// If it is predicted to go off the map, clip the prediction to a valid position on the map.
BWAPI::Position PredictMovement(BWAPI::Unit unit, int frames)
{
	UAB_ASSERT(unit && unit->getPosition().isValid(), "bad unit");

	BWAPI::Position pos(
		unit->getPosition().x + int(frames * unit->getVelocityX()),
		unit->getPosition().y + int(frames * unit->getVelocityY())
		);
	ClipToMap(pos);
	return pos;
}

// Clip (x,y) to the bounds of the map.
void ClipToMap(BWAPI::Position & pos)
{
	if (pos.x < 0)
	{
		pos.x = 0;
	}
	else if (pos.x >= 32 * BWAPI::Broodwar->mapWidth())
	{
		pos.x = 32 * BWAPI::Broodwar->mapWidth() - 1;
	}

	if (pos.y < 0)
	{
		pos.y = 0;
	}
	else if (pos.y >= 32 * BWAPI::Broodwar->mapHeight())
	{
		pos.y = 32 * BWAPI::Broodwar->mapHeight() - 1;
	}
}

// Convert TilePosition to Position
// Faster than BWAPI built-in conversion
const auto TileToPosition(BWAPI::TilePosition t)
{
	return BWAPI::Position(t.x * 32, t.y * 32);
}

// Point b specifies a direction from point a.
// Return a position at the given distance and direction from a.
// CAUTION The result may be off the map. See DistanceAndDirection() below.
// The distance can be negative.
BWAPI::Position RawDistanceAndDirection(const BWAPI::Position & a, const BWAPI::Position & b, int distance)
{
	if (a == b)
	{
		return a;
	}

	double2 difference(b - a);
	return a + (difference.normal() * double(distance));
}

// Point b specifies a direction from point a.
// Return a position at the given distance and direction from a, clipped to the map boundaries.
// The distance can be negative.
BWAPI::Position DistanceAndDirection(const BWAPI::Position & a, const BWAPI::Position & b, int distance)
{
	return RawDistanceAndDirection(a, b, distance).makeValid();
}

BWAPI::Position TileCenter(const BWAPI::TilePosition & tile)
{
	return BWAPI::Position(tile) + BWAPI::Position(16, 16);
}

// Return the intersetion of two sets of units.
// It will run faster if a is the smaller set.
BWAPI::Unitset Intersection(const BWAPI::Unitset & a, const BWAPI::Unitset & b)
{
	BWAPI::Unitset result;

	for (BWAPI::Unit u : a)
	{
		if (b.contains(u))
		{
			result.insert(u);
		}
	}

	return result;
}

#include "SkillBases.h"

#include "Bases.h"
#include "The.h"

using namespace UAlbertaBot;

// Remember when bases are created or destroyed.
// Can be used to decide when and where to expand, and when and where to seek enemy bases.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

// Set all bases to neutral.
// We'll recognize our own bases immediately when they are not neutral.
void SkillBases::initialize()
{
	_status.reserve(1 + the.bases.getAll().size());			// the first base ID is 1, not 0

	_status.push_back(BaseStatus(nullptr, the.neutral()));	// placeholder for the nonexistent base 0
	for (Base * base : the.bases.getAll())
	{
		UAB_ASSERT(base->getID() == int(_status.size()), "id out of order");
		_status.push_back(BaseStatus(base, the.neutral()));
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

SkillBases::SkillBases()
	: Skill("bases")
{
	// The game has not started yet, so we can't finish initialization.
	// We have to schedule an update to find the initial base or bases.
	_nextUpdateFrame = 0;
}

// Write the data for the current game to a string.
// The format is slightly tricky:
//   <base ID> <frame>
// Positive ID for my bases, negative for yours.
// Positive frame for a created base, negative for a destroyed base.
std::string SkillBases::putData() const
{
	std::stringstream s;

	for (const BaseRecord & record : _records)
	{
		s <<
			' ' << (record.mine ? record.id : -record.id) <<
			' ' << (record.created ? record.frame : -record.frame);
	}

	return s.str();
}

// Read in past base timings for one game record from a string.
// TODO not implemented yet
void SkillBases::getData(GameRecord & r, const std::string & line)
{
}

// Unit frame values are scouting times--when it was first seen.
// Building values are the building's completion time--possibly in the future.
// Don't record MAX_FRAME completion times. They exist but don't convey information.
void SkillBases::update()
{
	if (the.now() == 0)
	{
		initialize();

		// We might theoretically find the enemy base within this time, if it's close.
		_nextUpdateFrame = 20 * 24;
	}

	for (Base * base : the.bases.getAll())
	{
		BaseStatus & status = _status.at(base->getID());
		UAB_ASSERT(base == status.base, "bad status");

		if (status.owner != base->getOwner())
		{
			// There's a change. Make a record.

			if (status.owner != the.neutral() && base->getOwner() != the.neutral())
			{
				// Actually two records. The former base here is gone.
				_records.push_back(
					BaseRecord(
						base->getID(),
						status.owner == the.self(),
						false,
						the.now())
				);
			}

			// The new base status.
			if (base->getOwner() == the.neutral())
			{
				// Base destroyed.
				_records.push_back(
					BaseRecord(
						base->getID(),
						status.owner == the.self(),
						false,
						the.now())
				);
			}
			else
			{
				// Base created.
				_records.push_back(
					BaseRecord(
						base->getID(),
						base->getOwner() == the.self(),
						true,
						the.now())
				);
			}

			// Update the status.
			status.owner = base->getOwner();
		}
	}

	_nextUpdateFrame = the.now() + UpdateInterval;
}

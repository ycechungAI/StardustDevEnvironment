#include "SpiderMineData.h"

#include "Bases.h"
#include "Random.h"
#include "The.h"

using namespace UAlbertaBot;

// Steamhammer divides spider mines into two classes:
// 1. Static mines: Those laid at fixed, pre-determined positions on the map,
//    such as mines to block bases from being built.
// 2. Dynamic mines: Those laid as vultures move around the map for their own reasons.

// Static mines are laid by the Minelaying squad. Vultures in
// that squad do nothing else until they are reassigned. They try to
// avoid combat. When all fixed positions are mined, the squad is
// disbanded.

// Dynamic mines are laid when a vulture spots an opportunity. They include
// mines laid in combat, e.g. to kill off sieged tanks.

// SpiderMineData keeps information about all spider mines. It decides
// on the fixed positions and also records dynamic mines.

// Every mine is laid at an "abstract position" or MineLocation which
// represents its purpose. For example, a mine laid to block a base
// from being built is at MineLocation::BlockBase. The idea is to record
// how successful each MineLocation is, at delaying enemy actions,
// seeing enemy movement, or damaging enemy units, to later concentrate on
// the most valuable MineLocations. The information is not used--yet.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Jobs.

// The priority is forced to be at least 0.1.
// Zero priority will cause division by zero. I recommend 1.0 or greater.
SpiderMineData::Job::Job(SpiderMineData::MineLocation loc, const BWAPI::Position & pos, double p)
	: location(loc)
	, position(pos)
	, base(nullptr)
	, priority(std::max(p, 0.1))
	, vulture(nullptr)
{
}

SpiderMineData::Job::Job(SpiderMineData::MineLocation loc, const BWAPI::Position & pos, const Base * b, double p)
	: location(loc)
	, position(pos)
	, base(b)
	, priority(std::max(p, 0.1))
	, vulture(nullptr)
{
}

// The game situation makes it valid to try to place a mine here,
// though we may learn differently.
// Used when deciding whether to assign a vulture.
bool SpiderMineData::Job::ok() const
{
	return
		(!base || base->getOwner() == the.neutral()) &&
		the.airHitsFixed.at(position) == 0;		// not in static detection range
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

// The map makes it possible to place a mine here.
// Used in deciding whether to create a given job.
bool SpiderMineData::validPosition(const BWAPI::Position & pos)
{
	return
		pos.isValid() &&
		pos != BWAPI::Positions::Origin &&
		the.bases.myStart()->getDistance(pos) >= 0 &&
		the.map.isWalkable(BWAPI::TilePosition(pos));
}

// If the job is good, record it. Otherwise ignore it.
void SpiderMineData::postJob(MineLocation loc, const BWAPI::Position & pos, const Base * b, double p)
{
	if (validPosition(pos))
	{
		_jobs.push_back(new Job(loc, pos, b, p));
	}
}

// BlockBase, WatchBase, or (in principle) both or neither.
void SpiderMineData::baseJobs()
{
	for (Base * base : the.bases.getAll())
	{
		if (base->getOwner() != the.neutral())
		{
			continue;
		}

		// Where the mineral line is in relation to the depot, on average.
		BWAPI::Position offset = base->getMineralOffset();
		BWAPI::Position pos;

		bool block = false;

		// BlockBase jobs.
		if (block)
		{
			BWAPI::Position center = base->getCenter();

			// The mineral line is more vertical than horizontal?
			bool vertical = std::abs(offset.x) >= std::abs(offset.y);

			// Three mines: One with high priority at an edge of the base location,
			// two with low priority that make it harder to place the base nearby.
			if (vertical)
			{
				pos = center + BWAPI::Position(offset.x > 0 ? -2 * 32 + 16 : 2 * 32 - 16, 0);
			}
			else
			{
				pos = center + BWAPI::Position(0, offset.y > 0 ? -32 : 32);
			}
			postJob(MineLocation::BlockBase, pos.makeValid(), base, 1000.0);

			pos = center + BWAPI::Position(offset.x > 0 ? -4 * 32 : 4 * 32, offset.y > 0 ? -32 : 32);
			postJob(MineLocation::BlockBase, pos.makeValid(), base, 10.0);

			pos = center + BWAPI::Position(offset.x > 0 ? 32 : -32, offset.y > 0 ? -3 * 32 : 3 * 32);
			postJob(MineLocation::BlockBase, pos.makeValid(), base, 10.0);
		}
		
		// WatchBase jobs.
		if (!block)
		{
			// For now, one mine next to the base location.

			// ORIGINALLY Three mines: One with high priority right next to the base location,
			// two separated mines with low priority to maintain watch if others are gone.

			BWAPI::TilePosition oppositeMinerals =
				base->getTilePosition() + BWAPI::TilePosition(0, offset.y > 0 ? -1 : +3);

			pos = TileCenter(oppositeMinerals);
			postJob(MineLocation::WatchBase, pos.makeValid(), base, 1000.0);

			pos = TileCenter(oppositeMinerals + BWAPI::TilePosition(-2, offset.y > 0 ? -1 : +1));
			postJob(MineLocation::WatchBase, pos.makeValid(), base, 10.0);

			pos = TileCenter(oppositeMinerals + BWAPI::TilePosition(+2, offset.y > 0 ? -1 : +1));
			//postJob(MineLocation::WatchBase, pos.makeValid(), base, 10.0);
		}
	}
}

// Create the static mine jobs.
// Do this only once, so that the _jobs data is never reallocated and moved.
void SpiderMineData::initialize()
{
	baseJobs();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

SpiderMineData::SpiderMineData()
	: _initialized(false)
{
}

// -- -- -- --
// Jobs.

// This finds a job for a vulture, but does not assign it.
// It's also used to find out if all the jobs have been assigned.
SpiderMineData::Job * SpiderMineData::getJob(BWAPI::Unit vulture)
{
	if (!_initialized)
	{
		initialize();						// lazy initialization when first needed
		_initialized = true;
	}

	Job * bestJob = nullptr;
	double best = MAX_DISTANCE;				// lower is better

	for (Job * job : _jobs)
	{
		if (job->vulture == nullptr && job->ok())
		{
			// The value of the assignment is the vulture's distance from the position
			// divided by the job's priority.
			// Lower time and higher priority are better.
			double value = vulture->getDistance(job->position) / job->priority;
			if (value < best)
			{
				bestJob = job;
				best = value;
			}
		}
	}

	return bestJob;		// may be null
}

// Assign the vulture to a job, unless the job is null or the vulture already has one.
void SpiderMineData::assign(BWAPI::Unit vulture, Job * job)
{
	UAB_ASSERT(vulture, "no vulture");

	if (job && vulture && !assignment(vulture))
	{
		job->vulture = vulture;
		_assignments[vulture] = job;
	}
}

SpiderMineData::Job * SpiderMineData::assignment(BWAPI::Unit vulture) const
{
	auto it = _assignments.find(vulture);
	if (it == _assignments.end())
	{
		return nullptr;
	}
	return it->second;
}

// -- -- -- --

// Success. Clear the assignment.
// Record that the assigned mine is laid.
void SpiderMineData::jobCompleted(BWAPI::Unit vulture, BWAPI::Unit mine)
{
	UAB_ASSERT(vulture, "no vulture");
	Job * job = _assignments.at(vulture);
	UAB_ASSERT(job, "no job");
	//BWAPI::Broodwar->printf("job completed %d,%d", job->position.x, job->position.y);

	// Do not clear the job->vulture pointer.
	// It's used to remember that the job is complete.

	// _mines[mine] = mine->getPosition();

	_assignments.erase(vulture);
}

// Failure. Clear the assignment.
// The vulture has died or given up other otherwise needs to be de-assigned.
void SpiderMineData::jobFailed(BWAPI::Unit vulture)
{
	Job * job = _assignments.at(vulture);
	UAB_ASSERT(job, "no job");
	//BWAPI::Broodwar->printf("job failed %d,%d", job->position.x, job->position.y);
	job->vulture = nullptr;

	_assignments.erase(vulture);
}

// -- -- -- --

// A free-ranging vulture says it planted a mine (or was about to).
// This is intended to record the information for later analysis.
// For now, do nothing.
void SpiderMineData::report(MineLocation loc, const BWAPI::Position & pos)
{
}

// -- -- -- --

// Draw the static mine placements on the map.
// It draws the jobs, not the mines.
void SpiderMineData::draw()
{
	for (Job * job : _jobs)
	{
		if (job->vulture)
		{
			BWAPI::Broodwar->drawCircleMap(job->position, 3, BWAPI::Colors::Green);
		}
		else if (!job->ok())
		{
			// Draw a little red -.
			BWAPI::Broodwar->drawLineMap(
				job->position.x - 2, job->position.y,
				job->position.x + 2, job->position.y,
				BWAPI::Colors::Red);
		}
		else
		{
			BWAPI::Broodwar->drawCircleMap(job->position, 3, BWAPI::Colors::Yellow);
		}
	}
}

// -- -- -- --

// NOTE Specialized for the case that's failing on BASIL.
void SpiderMineData::sanityCheckAssignments() const
{
	for (auto it = _assignments.begin(); it != _assignments.end(); ++it)
	{
		BWAPI::Unit vulture = it->first;
		const Job * job = it->second;
		UAB_ASSERT(vulture, "no vulture");
		UAB_ASSERT(job, "no job");
		UAB_ASSERT(job->vulture == vulture, "job mismatch");
		bool ok = false;
		bool dupe = false;
		for (const Job * j : _jobs)
		{
			UAB_ASSERT(j->location == MineLocation::WatchBase, "bad loc");
			UAB_ASSERT(j->position.isValid(), "bad position");
			UAB_ASSERT(j->base, "no base");
			UAB_ASSERT(j->base->getPosition().getApproxDistance(j->position) <= 8 * 32, "far from base");
			UAB_ASSERT(j->priority > 0.0 && j->priority <= 1000.0, "bad priority");
			if (job == j)
			{
				if (!ok)
				{
					ok = true;
				}
				else
				{
					dupe = true;
				}
			}
		}
		UAB_ASSERT(ok, "bad job");
		UAB_ASSERT(!dupe, "duplicate job");
	}

	// Count how many are left.
	size_t unfinished = 0;
	for (const Job * j : _jobs)
	{
		if (!j->vulture)
		{
			++unfinished;
		}
	}

	BWAPI::Broodwar->printf("%d assignments, %d unfinished", _assignments.size(), unfinished);
}

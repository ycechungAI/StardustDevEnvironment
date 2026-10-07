#include "MicroMinelaying.h"

#include "Bases.h"
#include "Random.h"
#include "The.h"

using namespace UAlbertaBot;

// This squad is for vulture which are to be assigned to lay mines
// in fixed places, as decided by SpiderMineData. There are only so many
// fixed places to lay mines, so the squad has a limited lifetime.
// Other mines may be laid "live" during combat.
// CombatCommander adds and removes vultures from the squad.

// Each vulture is assigned a job to lay one mine.
// If it succeeds, the mine is recorded. If it fails, the job is released.
// Repeat until the vulture is out of mines.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

// -- -- -- --
// Job assignment.

void MicroMinelaying::assignVulture(BWAPI::Unit vulture)
{
	if (!the.mines.assignment(vulture))
	{
		SpiderMineData::Job * job = the.mines.getJob(vulture);
		the.mines.assign(vulture, job);
	}
}

// Clear the assignment of any vulture that is dead, or out of mines,
// or no longer in the squad.
// NOTE If the assignment was completed, it should already be cleared.
void MicroMinelaying::handleDeadVultures()
{
	for (auto it = the.mines.getAssignments().begin(); it != the.mines.getAssignments().end(); )
	{
		UAB_ASSERT(it->first, "no vulture");
		UAB_ASSERT(it->second, "no job");

		BWAPI::Unit vulture = it->first;
		if (!vulture->exists() ||
			vulture->getSpiderMineCount() == 0 ||
			!getUnits().contains(vulture))
		{
			// Null out the job->vulture pointer, since the job was not completed.
			it->second->vulture = nullptr;
			it = the.mines.getAssignments().erase(it);
		}
		else
		{
			++it;
		}
	}
}

void MicroMinelaying::microVulture(BWAPI::Unit vulture, SpiderMineData::Job * job)
{
	UAB_ASSERT(job && vulture && vulture->exists() &&
		the.mines.assignment(vulture) == job &&
		job->vulture == vulture,
		"bad job or vulture");

	// Look for the mine, to see if we laid it.
	const BWAPI::Unit mine = BWAPI::Broodwar->getClosestUnit(
		job->position,
		BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine && BWAPI::Filter::IsOwned,
		8);

	if (vulture->getDistance(job->position) <= 33 && mine)
	{
		//BWAPI::Broodwar->printf("vulture %d laid mine @ %d,%d", vulture->getID(), job->position.x, job->position.y);
		the.mines.jobCompleted(vulture, mine);
	}
	else if (!job->ok())
	{
		//BWAPI::Broodwar->printf("job failed vulture %d", vulture->getID());
		// There is no ground path to the location, etc.
		the.mines.jobFailed(vulture);
	}
	else if (vulture->getDistance(job->position) < 64 &&
		BWAPI::Broodwar->isVisible(BWAPI::TilePosition(job->position)))
	{
		//BWAPI::Broodwar->drawTextMap(vulture->getPosition(), "%cvulture %d lay", white, vulture->getID());
		the.micro.LaySpiderMine(vulture, job->position);
	}
	else
	{
		//BWAPI::Broodwar->drawTextMap(vulture->getPosition(), "%cvulture %d move to %d,%d", white, vulture->getID(), job->position.x, job->position.y);
		the.micro.MoveSafely(vulture, job->position);
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

MicroMinelaying::MicroMinelaying()
{
}

// Called every frame.
// 1. If there are no vultures, do nothing.
// 2. If a vulture is dead or out of mines, free its assignment for another to take.
// 3. Update the assignments and micro the vultures.
void MicroMinelaying::update()
{
	// 1. If there are no vultures, do nothing.
	if (getUnits().empty())
	{
		return;
	}

	if (BWAPI::Broodwar->getFrameCount() % 7 == 0)
	{
		// 2. If a vulture is dead or out of mines, free its assignment for another to take.
		handleDeadVultures();

		// 3. Update the assignments and micro the vultures.
		for (BWAPI::Unit u : getUnits())
		{
			assignVulture(u);		// if not already assigned
		}
		the.mines.sanityCheckAssignments();
		for (std::pair<BWAPI::Unit, SpiderMineData::Job *> assignment : the.mines.getAssignments())
		{
			microVulture(assignment.first, assignment.second);
		}
	}
	the.mines.sanityCheckAssignments();

	// Debug drawing - the job positions.
	the.mines.draw();
}

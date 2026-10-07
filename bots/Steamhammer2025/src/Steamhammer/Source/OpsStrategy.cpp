#include "OpsStrategy.h"

#include "Bases.h"
#include "The.h"
#include "UABAssert.h"

using namespace UAlbertaBot;

OpsStrategy::OpsStrategy()
	: _aggression(AggressionLevel::Aggressive)
{
}

// The real aggression level is not always the same as the requested level.
AggressionLevel OpsStrategy::getAggression() const
{
	if (the.ops.enemySeemsDead())
	{
		return AggressionLevel::Aggressive;
	}

	// Do not try to contain if the enemy does not own its starting base.
	// Do not try to contain if the enemy has "too many" bases.
	// NOTE If all production is in the main (common with bots), the base count may not matter.
	if (_aggression == AggressionLevel::Contain)
	{
		if (!the.bases.enemyStart() ||
			the.bases.enemyStart()->getOwner() != the.enemy() ||
			the.bases.baseCount(the.enemy()) >= 4)
		{
			return AggressionLevel::Aggressive;
		}
	}

	return _aggression;
}

void OpsStrategy::setAggression(AggressionLevel level)
{
	UAB_ASSERT(int(level) >= 0 && level < AggressionLevel::SIZE, "bad aggression level");

	_aggression = level;
}

const char * OpsStrategy::getAggressionString() const
{
	switch (getAggression())
	{
	case AggressionLevel::Defensive : return "Defend";
	case AggressionLevel::Contain   : return "Contain";
	case AggressionLevel::Aggressive: return "Attack";
	}

	return "ERROR";
}

// Nothing for now. Rely on other modules explicitly setting the value.
void OpsStrategy::update()
{
}

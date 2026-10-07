#pragma once

// The game record of the current game, updated as we go along.

#include <string>

namespace UAlbertaBot
{

enum class AggressionLevel : int
{
	  Defensive = 0	// only possible when in the opening book or explicitly ordered by code
	, Contain		// try to contain outside a natural or main
	, Aggressive	// the normal setting
	, SIZE
};

class OpsStrategy
{
private:
	AggressionLevel	_aggression;

public:
	OpsStrategy();

	AggressionLevel getAggression() const;
	void setAggression(AggressionLevel level);

	const char * getAggressionString() const;

	void update();			// call at most once per frame, only when the aggression level may matter
};

}
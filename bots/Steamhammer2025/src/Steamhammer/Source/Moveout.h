#pragma once

#include "PlayerSnapshot.h"
#include "Squad.h"

enum class SquadType
	{ GroundAttack
	, OtherGround
	, Air
	};

namespace UAlbertaBot
{
class Moveout
{
private:
	bool _rushBuild;			// can be set by the opening book

	bool isValkyrieSquad(const PlayerSnapshot & snap, int count) const;
	bool isCorsairScoutSquad(const PlayerSnapshot & snap, int count) const;
	bool isInfantry(const PlayerSnapshot & snap, int count) const;
	bool isMostlyVultures(const PlayerSnapshot & snap, int count) const;

public:

	Moveout();

	void setRush(bool rush) { _rushBuild = rush; };

	bool go(const Squad * squad, SquadType type);
};

}
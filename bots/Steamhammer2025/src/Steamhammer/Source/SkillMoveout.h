#pragma once

#include "Skill.h"
#include <BWAPI.h>

namespace UAlbertaBot
{
// The key identifies the main units in a squad.
// These are all unit types that are want to mass--to some extent--before moving out.
enum class MoveoutKey
	{ Infantry
	, InfantryMech
	, Goliaths
	, Tanks

	, Corsairs

	, GuardianAmbush
	, GuardianHydra
	};

// The history data to be recorded.
struct MoveoutRecord
{
	MoveoutKey key;
	int count;

	int killsBefore;
	int killsAfter;
};

class SkillMoveout : public Skill
{
private:
	int _nextUpdateFrame;

	void initialize();						// happens on start of game, not at object creation		

	std::vector<MoveoutRecord *> moveouts;	// unit groups that want to know when to move out

public:

	SkillMoveout();

	bool shouldMoveout(MoveoutKey key, BWAPI::Unitset units);

    std::string putData() const;
    void getData(GameRecord & r, const std::string & line);

    bool enabled() const   { return true;  };
    bool feasible() const  { return false; };
    bool good() const      { return false; };
    void execute()         { };
    void update();
};

}
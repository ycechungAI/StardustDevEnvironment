#pragma once

#include <set>

#include "Skill.h"
#include <BWAPI.h>

namespace UAlbertaBot
{
class Base;				// forward declaration

// Keep track of most recently seen base ownership.
// NOTE It would be enough to just store the owners in an array indexed by base ID.
struct BaseStatus
{
	Base * base;
	BWAPI::Player owner;

	BaseStatus(Base * b, BWAPI::Player o)
		: base(b)
		, owner(o)
	{}
};

// The history data to be recorded.
struct BaseRecord
{
	int id;				// base->getID()
	bool mine;			// mine or yours
	bool created;		// created or destroyed
	int frame;			// when

	BaseRecord(int i, bool m, bool c, int f)
		: id(i)
		, mine(m)
		, created(c)
		, frame(f)
	{}
};

class SkillBases : public Skill
{
private:
	const int UpdateInterval = 5 * 24;		// this is fine enough time resolution

	std::vector<BaseStatus> _status;		// array indexed by base ID
	std::vector<BaseRecord> _records;		// past records in order, open-ended list

	void initialize();						// happens on start of game, not at object creation		

public:

	SkillBases();

    std::string putData() const;
    void getData(GameRecord & r, const std::string & line);

    bool enabled() const   { return true;  };
    bool feasible() const  { return false; };
    bool good() const      { return false; };
    void execute()         { };
    void update();

    const std::vector<BaseRecord> & getRecords() const { return _records; };
};

}
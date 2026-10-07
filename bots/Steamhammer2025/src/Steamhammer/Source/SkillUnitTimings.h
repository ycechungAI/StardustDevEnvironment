#pragma once

#include "Skill.h"
#include <BWAPI.h>

namespace UAlbertaBot
{

struct TimingRequest
{
	BWAPI::UnitType type;
	double percentile;
};

struct TimingResponse
{
	int count;
	double rate;
	int minFrame;
	int maxFrame;
	int timing;
};

class SkillUnitTimings : public Skill
{
private:
    std::map<BWAPI::UnitType, int> timings;
    std::vector< std::map<BWAPI::UnitType, int> > pastTimings;

	int findPercentile(std::vector<int> & xs, double percentile) const;

public:

    SkillUnitTimings();

    std::string putData() const;
    void getData(GameRecord & r, const std::string & line);

    bool enabled() const   { return true;  };
    bool feasible() const  { return false; };
    bool good() const      { return false; };
    void execute()         { };
    void update();

    const std::map<BWAPI::UnitType, int> &                getTimings()     const { return timings;     };
    const std::vector< std::map<BWAPI::UnitType, int> > & getPastTimings() const { return pastTimings; };

	bool everSeen(BWAPI::UnitType type) const;
	void predict(const TimingRequest & request, TimingResponse & response) const;
};

}
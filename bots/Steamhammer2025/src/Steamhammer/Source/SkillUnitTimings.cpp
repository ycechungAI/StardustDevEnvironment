#include "SkillUnitTimings.h"

#include "InformationManager.h"
#include "The.h"

using namespace UAlbertaBot;

// Enemy unit timings.
// Record the first time each enemy unit type is sighted, in frames.
// For unfinished building types, record the building's predicted completion time instead,
// marking it by negating the frame count.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

// Return a member of xs which is greater than or equal to percentile of the values.
// (It's not actually a percentile.)
// For example, findPercentile(xs, 0.5) is approximately the median.
// Return MAX_FRAME if xs is empty.
int SkillUnitTimings::findPercentile(std::vector<int> & xs, double percentile) const
{
	if (xs.size() == 0)
	{
		return MAX_FRAME;
	}

	std::sort(xs.begin(), xs.end());
	size_t i = size_t(percentile * (xs.size() - 1));
	return xs.at(i);
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

SkillUnitTimings::SkillUnitTimings()
    : Skill("unit timings")
{
    // We should not see any enemy units before this. (Maybe on a tiny map.)
    _nextUpdateFrame = 20 * 24;
}

// Write the timings for the current game to a string.
std::string SkillUnitTimings::putData() const
{
    std::stringstream s;

    for (const std::pair<BWAPI::UnitType, int> & typeTime : timings)
    {
        s << ' ' << int(typeTime.first) << ' ' << typeTime.second;
    }

    return s.str();
}

// Read in past timings for one game record from a string,
// creating a map UnitType -> frame.
// Use abs() to fix any negative past frame values. The feature is deprecated.
// (<type> <frame>)*
void SkillUnitTimings::getData(GameRecord & r, const std::string & line)
{
	std::map<BWAPI::UnitType, int> timingRecord;
    std::stringstream s(line);

    int typeIndex;
    int frame;
    while (s >> typeIndex >> frame)
    {
		frame = std::abs(frame);
		if (BWAPI::UnitType(typeIndex).isValid() && frame < MAX_FRAME)
		{
			timingRecord[BWAPI::UnitType(typeIndex)] = frame;
		}
    }
	pastTimings.push_back(timingRecord);
}

// Unit frame values are scouting times--when it was first seen.
// Building values are the building's completion time--possibly in the future.
// Don't record MAX_FRAME completion times. They don't convey information.
void SkillUnitTimings::update()
{
    for (const auto & kv : the.info.getUnitData(the.enemy()).getUnits())
    {
        const UnitInfo & ui(kv.second);

        auto it = timings.find(ui.type);
        if (it == timings.end())
        {
            int frame = ui.updateFrame;
            if (ui.type.isBuilding() && !ui.completed)
            {
                frame = ui.completeBy;
            }
			if (frame < MAX_FRAME)
			{
				timings.insert(std::pair<BWAPI::UnitType, int>(ui.type, frame));
			}
        }
    }

    _nextUpdateFrame = the.now() + 23;
}

// Do we have any record of having seen this unit type?
// For finding unit types that the enemy does not use.
bool UAlbertaBot::SkillUnitTimings::everSeen(BWAPI::UnitType type) const
{
	for (auto & record : getPastTimings())
	{
		if (record.find(type) != record.end())
		{
			return true;
		}
	}
	return false;
}

// Predict enemy unit timing according to the request.
// Sets frame values to MAX_FRAME (never) if the enemy unit does not show up.
// NOTE Uses all recorded games. Doesn't try to restrict it to similar games.
void SkillUnitTimings::predict(const TimingRequest & request, TimingResponse & response) const
{
	size_t nGames = 0;
	int minFrame = MAX_FRAME;
	int maxFrame = 0;

	std::vector<int> times;

	for (auto & record : getPastTimings())
	{
		++nGames;
		auto found = record.find(request.type);
		if (found != record.end())
		{
			int t = found->second;
			if (t < minFrame)
			{
				minFrame = t;
			}
			if (t > maxFrame)
			{
				maxFrame = t;
			}
			times.push_back(t);
		}
	}

	response.count = int(times.size());
	response.rate = (nGames == 0) ? 0.0 : times.size() / double(nGames);
	response.minFrame = minFrame;
	response.maxFrame = maxFrame;
	response.timing = findPercentile(times, request.percentile);
}

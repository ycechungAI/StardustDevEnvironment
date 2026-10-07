#pragma once

// Predict the opponent's behavior from game records and scouting information.

#include <BWAPI.h>
#include <set>

namespace UAlbertaBot
{
class SkillUnitTimings;		// forward declaration

class Forecast
{
	SkillUnitTimings * _unitTiming;

	// When certain things will be needed.
	int _cloakSeenFrame;		// fact
	int _detectionFrame;		// prediction
	int _airSeenFrame;			// fact
	int _airDefenseFrame;		// prediction

	// Unit types never seen (and at least 3 game records exist).
	std::set<BWAPI::UnitType> _neverSeen;
	// Units expected by a certain frame.
	std::map<BWAPI::UnitType, int> _enemies;

	int predictOneUnit(BWAPI::UnitType t, double probability = 0.65) const;
	int predictTwoUnits(BWAPI::UnitType t1, BWAPI::UnitType t2) const;

	void downcast(int & prediction, int frame);		// update a prediction with a new minimum

	void neverSeenUnits();
	void initialForecast();
	void updateForecast();

	void drawPredictedTime(int x, int y, int frame) const;
    void draw() const;

public:

	Forecast();

	int getDetectionFrame() const { return _detectionFrame; };
	int getAirDefenseFrame() const { return _airDefenseFrame; };
	int getStaticAirDefenseFrame() const { return std::min(_detectionFrame, _airDefenseFrame); };
	int getEnemyUnitFrame(BWAPI::UnitType type) const;
	bool neverSeen(BWAPI::UnitType type) const;

    void update();
};

};

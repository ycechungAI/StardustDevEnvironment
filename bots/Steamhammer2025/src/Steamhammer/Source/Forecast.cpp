#include "Forecast.h"

#include "OpponentModel.h"
#include "SkillUnitTimings.h"
#include "The.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// -- -- -- -- -- -- -- -- -- -- --
// Private methods.

// Fill in a set of enemy unit types which have never been recorded
// (at all, in any matchup).
// The enemy race must not be Unknown, and at least 3 games must have been played
// with the same matchup as the current game.
void Forecast::neverSeenUnits()
{
	if (the.enemyRace() == BWAPI::Races::Unknown || the.oppModel().nGamesSameMatchup() < 3)
	{
		return;
	}
	
	for (BWAPI::UnitType type : BWAPI::UnitTypes::allUnitTypes())
	{
		if (type.getRace() == the.enemyRace() &&
			!type.isBuilding() &&
			!type.isHero() &&
			!type.isWorker() &&
			type != BWAPI::UnitTypes::Protoss_Scarab &&
			type != BWAPI::UnitTypes::Protoss_Interceptor &&
			type != BWAPI::UnitTypes::Zerg_Cocoon &&
			type != BWAPI::UnitTypes::Zerg_Egg &&
			type != BWAPI::UnitTypes::Zerg_Larva &&
			!_unitTiming->everSeen(type))
		{
			_neverSeen.insert(type);
			BWAPI::Broodwar->printf("never seen %s", type.getName().c_str());
		}
	}
}

// Update a prediction.
// We're predicting dangers. We want the earliest dangerous frame.
void Forecast::downcast(int & prediction, int frame)
{
	prediction = std::min(prediction, frame);
}

// Return a prediction for when one enemy unit might appear.
int Forecast::predictOneUnit(BWAPI::UnitType t, double probability) const
{
	TimingRequest request;
	request.percentile = probability;
	TimingResponse response;

	request.type = t;
	_unitTiming->predict(request, response);
	if (response.rate > probability)
	{
		return response.timing;
	}
	return MAX_FRAME;
}

// Return a prediction for when either of two enemy units might appear.
// This does a little better than predicting one and then predicting the other, since either is enough.
int Forecast::predictTwoUnits(BWAPI::UnitType t1, BWAPI::UnitType t2) const
{
	TimingRequest request;
	request.percentile = 0.333;
	TimingResponse response1;
	TimingResponse response2;

	request.type = t1;
	_unitTiming->predict(request, response1);
	int earliest1 = response1.timing;

	request.type = t2;
	_unitTiming->predict(request, response2);
	int earliest2 = response2.timing;

	if (response1.rate + response2.rate > 0.5)		// pretend they are independent
	{
		return std::min(earliest1, earliest2);
	}
	return MAX_FRAME;
}

// From past game records, make an initial estimate of when
// we may first need air defense or detection.
// Ignore advanced units like arbiters. They can be scouted later.
// The needs are different for each matchup.
void Forecast::initialForecast()
{
	if (the.enemyRace() == BWAPI::Races::Unknown)		// don't have the data to handle this
	{
		return;
	}

	if (the.selfRace() == BWAPI::Races::Terran)
	{
		if (the.enemyRace() == BWAPI::Races::Terran)
		{
			// TvT
			int frame = predictOneUnit(BWAPI::UnitTypes::Terran_Wraith);
			downcast(_airDefenseFrame, frame);
		}
		else if (the.enemyRace() == BWAPI::Races::Protoss)
		{
			// TvP
			// Be more cautious about DTs: Prepare if there's a 33% chance of them.
			int frame = predictOneUnit(BWAPI::UnitTypes::Protoss_Dark_Templar, 0.33);
			downcast(_detectionFrame, frame);
		}
		else if (the.enemyRace() == BWAPI::Races::Zerg)
		{
			// TvZ
			int frame = predictOneUnit(BWAPI::UnitTypes::Zerg_Mutalisk);
			downcast(_airDefenseFrame, frame);
			frame = predictOneUnit(BWAPI::UnitTypes::Zerg_Lurker);
			downcast(_detectionFrame, frame);
		}
	}
	else if (the.selfRace() == BWAPI::Races::Protoss)
	{
		if (the.enemyRace() == BWAPI::Races::Terran)
		{
			// PvT
			int frame = predictOneUnit(BWAPI::UnitTypes::Terran_Dropship);
			downcast(_airDefenseFrame, frame);
			frame = predictOneUnit(BWAPI::UnitTypes::Terran_Wraith);
			downcast(_airDefenseFrame, frame);
		}
		else if (the.enemyRace() == BWAPI::Races::Protoss)
		{
			// PvP
			// Be more cautious about DTs: Prepare if there's a 33% chance of them.
			int frame = predictOneUnit(BWAPI::UnitTypes::Protoss_Dark_Templar, 0.33);
			downcast(_detectionFrame, frame);
			frame = predictOneUnit(BWAPI::UnitTypes::Protoss_Shuttle);
			downcast(_airDefenseFrame, frame);
		}
		else if (the.enemyRace() == BWAPI::Races::Zerg)
		{
			// PvZ
			int frame = predictOneUnit(BWAPI::UnitTypes::Zerg_Mutalisk);
			downcast(_airDefenseFrame, frame);
			frame = predictOneUnit(BWAPI::UnitTypes::Zerg_Lurker);
			downcast(_detectionFrame, frame);
		}
	}
	else if (the.selfRace() == BWAPI::Races::Zerg)
	{
		if (the.enemyRace() == BWAPI::Races::Terran)
		{
			// ZvT
			int frame = predictTwoUnits(BWAPI::UnitTypes::Terran_Wraith, BWAPI::UnitTypes::Terran_Valkyrie);
			downcast(_airDefenseFrame, frame);
		}
		else if (the.enemyRace() == BWAPI::Races::Protoss)
		{
			// ZvP
			int frame = predictTwoUnits(BWAPI::UnitTypes::Protoss_Corsair, BWAPI::UnitTypes::Protoss_Scout);
			downcast(_airDefenseFrame, frame);
		}
		else if (the.enemyRace() == BWAPI::Races::Zerg)
		{
			// ZvZ
			int frame = predictOneUnit(BWAPI::UnitTypes::Zerg_Mutalisk);
			downcast(_airDefenseFrame, frame);
		}
	}
}

void Forecast::updateForecast()
{
	// Infer unseen units from upgrades.
	if (the.enemyRace() == BWAPI::Races::Terran)
	{
		if (the.your.ever.count(BWAPI::UnitTypes::Terran_Vulture) == 0 && the.info.enemyHasVultureSpeed())
		{
			_enemies[BWAPI::UnitTypes::Terran_Vulture] = the.now();
		}
		if (the.your.ever.count(BWAPI::UnitTypes::Terran_Goliath) == 0 && the.info.enemyHasGoliathRange())
		{
			_enemies[BWAPI::UnitTypes::Terran_Goliath] = the.now();
		}
		if (the.your.ever.count(BWAPI::UnitTypes::Terran_Ghost) == 0 && the.info.enemyHasGhostSight())
		{
			_enemies[BWAPI::UnitTypes::Terran_Ghost] = the.now();
			_detectionFrame = std::min(_detectionFrame, the.now());
		}
	}
	else if (the.enemyRace() == BWAPI::Races::Protoss)
	{
		if (the.your.ever.count(BWAPI::UnitTypes::Protoss_Reaver) == 0 && the.info.enemyHasReaverDamage())
		{
			_enemies[BWAPI::UnitTypes::Protoss_Reaver] = the.now();
		}
		if (the.your.ever.count(BWAPI::UnitTypes::Protoss_Carrier) == 0 && the.info.enemyHas8Interceptors())
		{
			_enemies[BWAPI::UnitTypes::Protoss_Carrier] = the.now();
		}
	}
	else if (the.enemyRace() == BWAPI::Races::Zerg)
	{
		if (the.your.ever.count(BWAPI::UnitTypes::Zerg_Hydralisk) == 0 &&
			(the.info.enemyHasHydraSpeed() || the.info.enemyHasHydraRange()))
		{
			_enemies[BWAPI::UnitTypes::Zerg_Hydralisk] = the.now();
		}
		if (the.your.ever.count(BWAPI::UnitTypes::Zerg_Ultralisk) == 0 &&
			(the.info.enemyHasUltraArmor() || the.info.enemyHasUltraSpeed()))
		{
			_enemies[BWAPI::UnitTypes::Zerg_Ultralisk] = the.now();
		}
	}

	if (!_cloakSeenFrame)
	{
		if (the.info.enemyCloakedUnitsSeen())
		{
			_cloakSeenFrame = the.now();
			_detectionFrame = std::min(_detectionFrame, the.now());
		}
		else
		{
			// Try to predict from scouting information.
			// Terran: Mines, wraiths, ghosts use tech that we can't scout.
			// Protoss: Dark templar, arbiters. Don't worry about observers.
			if (the.enemyRace() == BWAPI::Races::Protoss)
			{
				// NOTE The only realistic way to infer an arbiter tribunal is to see an arbiter.
				if (the.your.ever.count(BWAPI::UnitTypes::Protoss_Arbiter_Tribunal) > 0)
				{
					int t =
						the.info.getEnemyBuildingTiming(BWAPI::UnitTypes::Protoss_Arbiter_Tribunal) +
						BWAPI::UnitTypes::Protoss_Arbiter.buildTime();
					_detectionFrame = std::min(_detectionFrame, t);
					_enemies[BWAPI::UnitTypes::Protoss_Arbiter] = _detectionFrame;
				}
				if (the.your.ever.count(BWAPI::UnitTypes::Protoss_Templar_Archives) > 0 ||
					the.your.inferred.count(BWAPI::UnitTypes::Protoss_Templar_Archives) > 0)
				{
					int t =
						the.info.getEnemyBuildingTiming(BWAPI::UnitTypes::Protoss_Templar_Archives) +
						BWAPI::UnitTypes::Protoss_Dark_Templar.buildTime();
					_detectionFrame = std::min(_detectionFrame, t);
					_enemies[BWAPI::UnitTypes::Protoss_Dark_Templar] = _detectionFrame;
				}
				else if (the.your.ever.count(BWAPI::UnitTypes::Protoss_Citadel_of_Adun) > 0 ||
					the.your.inferred.count(BWAPI::UnitTypes::Protoss_Citadel_of_Adun) > 0)
				{
					int t =
						the.info.getEnemyBuildingTiming(BWAPI::UnitTypes::Protoss_Citadel_of_Adun) +
						BWAPI::UnitTypes::Protoss_Templar_Archives.buildTime() +
						BWAPI::UnitTypes::Protoss_Dark_Templar.buildTime();
					_detectionFrame = std::min(_detectionFrame, t);
					_enemies[BWAPI::UnitTypes::Protoss_Dark_Templar] = _detectionFrame;
				}
			}
			// Zerg: Lurkers. Scouting does not help with burrow.
			else if (the.enemyRace() == BWAPI::Races::Zerg)
			{
				if ((the.your.ever.count(BWAPI::UnitTypes::Zerg_Lair) > 0 ||
					the.your.inferred.count(BWAPI::UnitTypes::Zerg_Lair) > 0) &&
					(the.your.ever.count(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0 ||
					the.your.inferred.count(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0))
				{
					// Ignore the lurker morph time. Lurkers are dangerous.
					int t =
						std::max(the.info.getEnemyBuildingTiming(BWAPI::UnitTypes::Zerg_Lair),
							the.info.getEnemyBuildingTiming(BWAPI::UnitTypes::Zerg_Hydralisk_Den)) +
						BWAPI::TechTypes::Lurker_Aspect.researchTime();
					_detectionFrame = std::min(_detectionFrame, t);
					_enemies[BWAPI::UnitTypes::Zerg_Lurker] = _detectionFrame;
				}
			}
		}
	}

	if (!_airSeenFrame)
	{
		if (the.info.enemyHasAirToGround() || the.info.enemyHasTransport())
		{
			_airSeenFrame = the.now();
			_airDefenseFrame = std::min(_airDefenseFrame, the.now());
		}
		else
		{
			if (the.enemyRace() == BWAPI::Races::Terran)
			{
				if (the.your.ever.count(BWAPI::UnitTypes::Terran_Physics_Lab) > 0)
				{
					int t =
						the.info.getEnemyBuildingTiming(BWAPI::UnitTypes::Terran_Physics_Lab) +
						BWAPI::UnitTypes::Terran_Battlecruiser.buildTime();
					_airDefenseFrame = std::min(_airDefenseFrame, t);
					_enemies[BWAPI::UnitTypes::Terran_Battlecruiser] = _airDefenseFrame;
				}
			}
			else if (the.enemyRace() == BWAPI::Races::Protoss)
			{
				if (the.your.ever.count(BWAPI::UnitTypes::Protoss_Fleet_Beacon) > 0 ||
					the.your.inferred.count(BWAPI::UnitTypes::Protoss_Fleet_Beacon) > 0)
				{
					int t =
						the.info.getEnemyBuildingTiming(BWAPI::UnitTypes::Protoss_Fleet_Beacon) +
						BWAPI::UnitTypes::Protoss_Carrier.buildTime();
					_airDefenseFrame = std::min(_airDefenseFrame, t);
					_enemies[BWAPI::UnitTypes::Protoss_Carrier] = _airDefenseFrame;
				}
			}
			else if (the.enemyRace() == BWAPI::Races::Zerg)
			{
				if (the.your.ever.count(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0 ||
					the.your.inferred.count(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0)
				{
					int t =
						the.info.getEnemyBuildingTiming(BWAPI::UnitTypes::Zerg_Greater_Spire) +
						BWAPI::UnitTypes::Zerg_Guardian.buildTime();
					_airDefenseFrame = std::min(_airDefenseFrame, t);
					_enemies[BWAPI::UnitTypes::Zerg_Guardian] = _airDefenseFrame;
				}
			}
		}
	}
}

void Forecast::drawPredictedTime(int x, int y, int frame) const
{
	const int xOffset = 110;

	if (frame <= the.now())
	{
		BWAPI::Broodwar->drawTextScreen(x + xOffset, y, "%cnow", red);
	}
	else if (frame >= MAX_FRAME)
	{
		BWAPI::Broodwar->drawTextScreen(x + xOffset, y, "never");
	}
	else
	{
		BWAPI::Broodwar->drawTextScreen(x + xOffset, y, "%c%2u:%02u",
			yellow,
			int(frame / (23.8 * 60)), int(frame / 23.8) % 60);
	}
}

void Forecast::draw() const
{
	if (!Config::Debug::DrawForecast)
	{
		return;
	}

	int x = 170;
	int y = 70;

	BWAPI::Broodwar->drawTextScreen(x, y, "%cForecast", white);
	y += 12;

	BWAPI::Broodwar->drawTextScreen(x, y, "%cneed detection by", cyan);
	drawPredictedTime(x, y, _detectionFrame);
	y += 10;

	BWAPI::Broodwar->drawTextScreen(x, y, "%cneed air defense by", cyan);
	drawPredictedTime(x, y, _airDefenseFrame);
	y += 12;

	for (std::pair<BWAPI::UnitType, int> enemyTime : _enemies)
	{
		BWAPI::Broodwar->drawTextScreen(x, y, "%c%s", green, NiceMacroActName(enemyTime.first.getName()).c_str());
		drawPredictedTime(x, y, enemyTime.second);
		y += 10;
	}

	y += 2;
	for (BWAPI::UnitType type : _neverSeen)
	{
		BWAPI::Broodwar->drawTextScreen(x, y, "%cnever seen", green);
		BWAPI::Broodwar->drawTextScreen(x + 110, y, "%c%s", orange, NiceMacroActName(type.getName()).c_str());
		y += 10;
	}
}

// -- -- -- -- -- -- -- -- -- -- --
// Public methods.

Forecast::Forecast()
    : _unitTiming(nullptr)
	, _cloakSeenFrame(0)
	, _detectionFrame(MAX_FRAME)
	, _airSeenFrame(0)
	, _airDefenseFrame(MAX_FRAME)
{
}

// If the enemy unit type has been forecast, return its predicted frame of appearance.
// If not, answer depending on whether the unit type has been seen this game.
// If there's no hint of it, return MAX_FRAME.
int Forecast::getEnemyUnitFrame(BWAPI::UnitType type) const
{
	auto it = _enemies.find(type);
	if (it == _enemies.end())
	{
		if (the.your.ever.count(type) > 0)
		{
			return the.now();
		}
		return MAX_FRAME;
	}
	return it->second;
}

// The enemy unit type has never been recorded,
// and has not been seen so far in this game either.
bool Forecast::neverSeen(BWAPI::UnitType type) const
{
	return
		_neverSeen.find(type) != _neverSeen.end() &&
		the.your.ever.count(type) == 0;
};

// Called every frame.
// It depends on the unit timing skill, SkillUnitTiming, and can't predict without it.
void Forecast::update()
{
	if (the.now() % (3 * 24 + 1) == 0)
	{
		if (!_unitTiming)
		{
			_unitTiming = dynamic_cast<SkillUnitTimings *>(the.skillkit.getSkill("unit timings"));
			if (!_unitTiming)
			{
				return;
			}

			// These only have to run once.
			neverSeenUnits();
			initialForecast();
		}
		else
		{
			if (!_unitTiming)
			{
				return;
			}
			// This runs repeatedly.
			updateForecast();
		}
	}

    draw();
}

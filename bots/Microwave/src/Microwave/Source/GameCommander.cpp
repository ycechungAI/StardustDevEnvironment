#include "Common.h"
#include "GameCommander.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

GameCommander::GameCommander() 
    : _initialScoutSet(false)
	, _scoutAlways(false)
	, _scoutIfNeeded(false)
	, _scoutOnMinerals(0)
	, _surrenderTime(0)
	, _minRemainingLatencyFrames(999)
	, _countFrameLong(0)
{
}

void GameCommander::update()
{
	// Profile debug
	//PROFILE_FUNCTION();

	_timerManager.startTimer(TimerManager::All);

	// populate the unit vectors we will pass into various managers
	handleUnitAssignments();

	// No drones, no minerals and no army... give up :(
	if (!_surrenderTime && surrenderMonkey())
	{
		_surrenderTime = BWAPI::Broodwar->getFrameCount();
		BWAPI::Broodwar->sendText("gg! Nicely played %s, DeepMind could learn from you! :)", BWAPI::Broodwar->enemy()->getName().c_str());
	}
	if (_surrenderTime)
	{
		if (BWAPI::Broodwar->getFrameCount() - _surrenderTime >= 48)  // 48 frames = 2 game seconds
		{
			Log().Get() << "Surrendering";
			BWAPI::Broodwar->leaveGame();
		}
		_timerManager.stopTimer(TimerManager::All);
		return;
	}

	// Managers that gather inforation
	_timerManager.startTimer(TimerManager::Information);
	InformationManager::Instance().update();
	_timerManager.stopTimer(TimerManager::Information);

	_timerManager.startTimer(TimerManager::Cluster);
	Cluster::Instance().update();
	_timerManager.stopTimer(TimerManager::Cluster);

	_timerManager.startTimer(TimerManager::MapGrid);
	MapGrid::Instance().update();
	_timerManager.stopTimer(TimerManager::MapGrid);

	// Managers that act on information
	_timerManager.startTimer(TimerManager::Strategy);
	StrategyManager::Instance().update();
	_timerManager.stopTimer(TimerManager::Strategy);

	_timerManager.startTimer(TimerManager::Production);
	ProductionManager::Instance().update();
	_timerManager.stopTimer(TimerManager::Production);

	_timerManager.startTimer(TimerManager::Building);
	BuildingManager::Instance().update();
	_timerManager.stopTimer(TimerManager::Building);

	_timerManager.startTimer(TimerManager::Worker);
	WorkerManager::Instance().update();
	_timerManager.stopTimer(TimerManager::Worker);

	_timerManager.startTimer(TimerManager::Combat);
	_combatCommander.update(_combatUnits);
	_timerManager.stopTimer(TimerManager::Combat);

	_timerManager.startTimer(TimerManager::Scout);
    ScoutManager::Instance().update();
	_timerManager.stopTimer(TimerManager::Scout);
	
	if (BWAPI::Broodwar->getFrameCount() % 2000 == 1999)
	{
		int count = 0;
		for (const auto unit : BWAPI::Broodwar->self()->getUnits())
		{
			if (UnitUtil::IsCombatUnit(unit) && unit->isCompleted())
			{
				++count;
			}
		}

		Log().Get() << "Summary: " << count << " combat units, " << UnitUtil::GetCompletedUnitCount(BWAPI::Broodwar->self()->getRace().getWorker()) 
			<< " workers, " << BWAPI::Broodwar->self()->minerals() << " minerals, " << BWAPI::Broodwar->self()->gas() << " gas";
	}

	_timerManager.stopTimer(TimerManager::All);

	drawDebugInterface();

	if (_timerManager.getTotalElapsed() > 42)
	{
		++_countFrameLong;
		_timerManager.log();
	}
}

void GameCommander::drawDebugInterface()
{
	// Profile debug
	//PROFILE_FUNCTION();

	InformationManager::Instance().drawExtendedInterface();
	InformationManager::Instance().drawUnitInformation(425,30);
	InformationManager::Instance().drawMapInformation();
	InformationManager::Instance().drawBaseInformation(575, 30);
	BuildingManager::Instance().drawBuildingInformation(200, 70);
	BuildingPlacer::Instance().drawReservedTiles();
	ProductionManager::Instance().drawProductionInformation(30, 75);
	MapTools::Instance().drawHomeDistanceMap();
	
	_combatCommander.drawSquadInformation(160, 230);
    _timerManager.displayTimers(490, 225);
    drawGameInformation(4, 2);
	drawUnitOrders();

	// draw position of mouse cursor
	if (Config::Debug::DrawMouseCursorInfo)
	{
		int mouseX = BWAPI::Broodwar->getMousePosition().x + BWAPI::Broodwar->getScreenPosition().x;
		int mouseY = BWAPI::Broodwar->getMousePosition().y + BWAPI::Broodwar->getScreenPosition().y;
		BWAPI::Broodwar->drawTextMap(mouseX + 20, mouseY, " %d %d", mouseX, mouseY);
	}
}

void GameCommander::drawGameInformation(int x, int y)
{
	if (!Config::Debug::DrawGameInfo)
	{
		return;
	}

	int remainingLatencyFrames = BWAPI::Broodwar->getRemainingLatencyFrames();
	if (remainingLatencyFrames < _minRemainingLatencyFrames)
	{
		_minRemainingLatencyFrames = remainingLatencyFrames;
	}

	BWAPI::Broodwar->drawTextScreen(x, y, "\x04Game:");
	BWAPI::Broodwar->drawTextScreen(x+50, y, "%c%s (%s) \x04vs. %c%s  \x04Map: %s", 
			BWAPI::Broodwar->self()->getTextColor(), 
			BWAPI::Broodwar->self()->getName().c_str(), 
			Config::BotInfo::Version.c_str(),
			BWAPI::Broodwar->enemy()->getTextColor(), 
			BWAPI::Broodwar->enemy()->getName().c_str(),
			BWAPI::Broodwar->mapFileName().c_str());

	y += 12;
    BWAPI::Broodwar->drawTextScreen(x, y, "\x04Time:");
    BWAPI::Broodwar->drawTextScreen(x+50, y, "\x04%d   %dm %ds   LF: %d  TS: %d  FPS: %d  APM: %d  Frame: %.1lfms  Mean: %.1lfms  Max: %.1lfms  >42ms: %d", 
		BWAPI::Broodwar->getFrameCount(), 
		(int)(BWAPI::Broodwar->getFrameCount()/(23.81*60)), 
		(int)((int)(BWAPI::Broodwar->getFrameCount()/23.81)%60),
		BWAPI::Broodwar->getLatencyFrames(),
		BWAPI::Broodwar->getLatencyFrames() - _minRemainingLatencyFrames + 1,
		BWAPI::Broodwar->getFPS(),
		BWAPI::Broodwar->getAPM(),
		_timerManager.getTotalElapsed(),
		_timerManager.getMeanMilliseconds(),
		_timerManager.getMaxMilliseconds(),
		_countFrameLong);

	std::string specific;
	if (Config::Strategy::FoundMapSpecificStrategy) specific = " (map)";
	if (Config::Strategy::FoundEnemySpecificStrategy) specific = " (enemy)";
	if (Config::Strategy::FoundHumanSpecificStrategy) specific = " (human)";

	y += 12;
	BWAPI::Broodwar->drawTextScreen(x, y, "\x04Strategy:");
	BWAPI::Broodwar->drawTextScreen(x+50, y, "\x03%s%s", 
		Config::Strategy::StrategyName.c_str(), specific.c_str());
	BWAPI::Broodwar->setTextSize();

	StrategyManager::Instance().drawStrategyInformation(x, y);
	y += 12;
	StrategyBossZerg::Instance().drawStrategyInformation(x, y);
}

void GameCommander::drawUnitOrders()
{
	if (!Config::Debug::DrawUnitOrders)
	{
		return;
	}

	for (const auto unit : BWAPI::Broodwar->getAllUnits())
	{
		if (!unit->getPosition().isValid())
		{
			continue;
		}

		std::string extra = "";
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Cocoon ||
			unit->getType().isBuilding() && !unit->isCompleted())
		{
			extra = unit->getBuildType().getName();
		}
		else if (unit->isTraining() && !unit->getTrainingQueue().empty())
		{
			extra = unit->getTrainingQueue()[0].getName();
		}
		else if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
			unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
		{
			extra = unit->getType().getName();
		}
		else if (unit->isResearching())
		{
			extra = unit->getTech().getName();
		}
		else if (unit->isUpgrading())
		{
			extra = unit->getUpgrade().getName();
		}

		int x = unit->getPosition().x;
		int y = unit->getPosition().y - 2;
		if (extra != "")
		{
			int textLength = extra.length() * 5;
			BWAPI::Broodwar->drawTextMap(x-(textLength/2), y, "%c%s", yellow, extra.c_str());
		}
		int textLength = unit->getOrder().getName().length() * 5;
		BWAPI::Broodwar->drawTextMap(x-(textLength/2), y + 10, "%c%s", cyan, unit->getOrder().getName().c_str());
	}
}

// assigns units to various managers
void GameCommander::handleUnitAssignments()
{
	// Profile debug
	//PROFILE_FUNCTION();

	_validUnits.clear();
    _combatUnits.clear();

	// filter our units for those which are valid and usable
	setValidUnits();

	// set each type of unit
	setScoutUnits();
	setCombatUnits();
}

bool GameCommander::isAssigned(BWAPI::Unit unit) const
{
	return _combatUnits.contains(unit) || _scoutUnits.contains(unit);
}

// validates units as usable for distribution to various managers
void GameCommander::setValidUnits()
{
	// make sure the unit is completed and alive and usable
	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		if (UnitUtil::IsValidUnit(unit))
		{	
			_validUnits.insert(unit);
		}
	}
}

void GameCommander::setScoutUnits()
{
	// If we're zerg, assign the first overlord to scout.
	if (BWAPI::Broodwar->getFrameCount() == 0 &&
		BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg)
	{
		for (const auto unit : BWAPI::Broodwar->self()->getUnits())
		{
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
			{
				ScoutManager::Instance().setOverlordScout(unit);
				assignUnit(unit, _scoutUnits);
				break;
			}
		}
	}

    // Send a scout if we haven't yet and should.
	if (!_initialScoutSet)
    {
		if (_scoutAlways ||
			(_scoutIfNeeded && !InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy())))
		{
			int minerals = std::max(0, BWAPI::Broodwar->self()->minerals());
			int getReservedMinerals = std::max(0, BuildingManager::Instance().getReservedMinerals());
			int freeMinerals = minerals - getReservedMinerals;

			if (freeMinerals >= _scoutOnMinerals)
			{
				BWAPI::Unit workerScout = getAnyFreeWorker();

				// if we find a worker (which we should) add it to the scout units
				if (workerScout)
				{
					ScoutManager::Instance().setWorkerScout(workerScout);
					assignUnit(workerScout, _scoutUnits);
					_initialScoutSet = true;
				}
			}
		}
    }
}

// sets combat units to be passed to CombatCommander
void GameCommander::setCombatUnits()
{
	for (auto & unit : _validUnits)
	{
		if (!isAssigned(unit) && UnitUtil::IsCombatUnit(unit) || unit->getType().isWorker())		
		{	
			assignUnit(unit, _combatUnits);
		}
	}
}

// Release the zerg scouting overlord for other duty.
void GameCommander::releaseOverlord(BWAPI::Unit overlord)
{
	_scoutUnits.erase(overlord);
}

// Sets amount of minerals to start scouting
void GameCommander::setScoutOnMinerals(int amount)
{
	_scoutOnMinerals = amount;
}

// Call when the strategy wants an unconditional worker scout.
void GameCommander::goScoutAlways()
{
	_scoutAlways = true;
}

// Call when the strategy wants a worker scout provided the enemy is not yet located.
void GameCommander::goScoutIfNeeded()
{
	_scoutIfNeeded = true;
}

// Get the first spawning pool, supply depot, or pylon.
// This is by default the timing of the initial scout. It has nothing to do with supply.
BWAPI::Unit GameCommander::getFirstSupplyProvider()
{
	BWAPI::Unit supplyProvider = nullptr;

	if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg)
	{
		for (auto & unit : BWAPI::Broodwar->self()->getUnits())
		{
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Spawning_Pool)
			{
				supplyProvider = unit;
			}
		}
	}
	else
	{
		
		for (auto & unit : BWAPI::Broodwar->self()->getUnits())
		{
			if (unit->getType() == BWAPI::Broodwar->self()->getRace().getSupplyProvider())
			{
				supplyProvider = unit;
			}
		}
	}

	return supplyProvider;
}

void GameCommander::onUnitShow(BWAPI::Unit unit)			
{ 
	InformationManager::Instance().onUnitShow(unit); 
	WorkerManager::Instance().onUnitShow(unit);
}

void GameCommander::onUnitHide(BWAPI::Unit unit)			
{ 
	InformationManager::Instance().onUnitHide(unit); 
}

void GameCommander::onUnitCreate(BWAPI::Unit unit)		
{ 
	InformationManager::Instance().onUnitCreate(unit); 
}

void GameCommander::onUnitComplete(BWAPI::Unit unit)
{
	InformationManager::Instance().onUnitComplete(unit);
}

void GameCommander::onUnitRenegade(BWAPI::Unit unit)		
{ 
	InformationManager::Instance().onUnitRenegade(unit); 
}

void GameCommander::onUnitDestroy(BWAPI::Unit unit)		
{ 	
	ProductionManager::Instance().onUnitDestroy(unit);
	WorkerManager::Instance().onUnitDestroy(unit);
	InformationManager::Instance().onUnitDestroy(unit); 
}

void GameCommander::onUnitMorph(BWAPI::Unit unit)		
{ 
	InformationManager::Instance().onUnitMorph(unit);
	WorkerManager::Instance().onUnitMorph(unit);
}

// Used only to choose a worker to scout.
BWAPI::Unit GameCommander::getAnyFreeWorker()
{
	for (auto & unit : _validUnits)
	{
		if (unit->getType().isWorker()
			&& !isAssigned(unit)
			&& WorkerManager::Instance().isFree(unit)
			&& !unit->isCarryingMinerals()
			&& !unit->isCarryingGas())
		{
			return unit;
		}
	}

	return nullptr;
}

void GameCommander::assignUnit(BWAPI::Unit unit, BWAPI::Unitset & set)
{
    if (_scoutUnits.contains(unit)) { _scoutUnits.erase(unit); }
    else if (_combatUnits.contains(unit)) { _combatUnits.erase(unit); }

    set.insert(unit);
}

// Decide whether to give up early. See config option SurrenderWhenHopeIsLost.
bool GameCommander::surrenderMonkey()
{
	//if (!Config::Strategy::SurrenderWhenHopeIsLost)
	//{
	//	return false;
	//}

	// Only check once every five seconds. No hurry to give up.
	if (BWAPI::Broodwar->getFrameCount() % (5 * 24) != 0)
	{
		return false;
	}

	// Don't surrender right at the start.
	if (BWAPI::Broodwar->getFrameCount() < 24 * 60)
	{
		return false;
	}

	// Surrender if all conditions are met:
	// 1. We don't have the cash to make a worker.
	// 2. We have no unit that can attack.
	// 3. The enemy has at least one visible unit that can destroy buildings.
	// Terran does not float buildings, so we check whether the enemy can attack ground.

	// 1. Our cash.
	if (BWAPI::Broodwar->self()->minerals() >= 50)
	{
		return false;
	}

	// 2. Our units.
	for (const auto unit : _validUnits)
	{
		if (unit->canAttack())
		{
			return false;
		}
	}

	// 3. Enemy units.
	bool safe = true;
	for (const auto unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->isVisible() && UnitUtil::CanAttackGround(unit))
		{
			safe = false;
			break;
		}
	}
	if (safe)
	{
		return false;
	}

	// Surrender monkey says surrender!
	return true;
}

GameCommander & GameCommander::Instance()
{
	static GameCommander instance;
	return instance;
}

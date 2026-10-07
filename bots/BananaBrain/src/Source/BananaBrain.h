#pragma once

#include <BWAPI.h>
#include "bwem.h"
#include "JPS.h"
#include <set>
#include <array>
#include <vector>
#include <map>
#include <deque>
#include <algorithm>
#include <ctime>
#include <functional>
#include <chrono>
#include <random>
#include <optional>
#include <climits>

using namespace BWAPI;

#ifndef M_PI
static constexpr auto M_PI = 3.14159265358979323846;
#endif

#include "FastPosition.h"
#include "UnitUtils.h"
#include "Utils.h"
#include "UnitPotential.h"
#include "Configuration.h"

#include "BaseState.h"
#include "PathFinder.h"
#include "Information.h"
#include "Grids.h"
#include "Tactics.h"
#include "OpponentModel.h"
#include "Micro.h"
#include "Macro.h"
#include "WallPlacement.h"
#include "BuildingPlacement.h"
#include "Worker.h"
#include "Results.h"
#include "Strategy.h"

// Remember not to use "Broodwar" in any global class constructor!

namespace {
	auto& bwem_map = BWEM::Map::Instance();
	
	auto& configuration = Configuration::Instance();
	auto& base_state = BaseState::Instance();
	auto& path_finder = PathFinder::Instance();
	auto& walkability_grid = WalkabilityGrid::Instance();
	auto& connectivity_grid = ConnectivityGrid::Instance();
	auto& threat_grid = ThreatGrid::Instance();
	auto& unit_grid = UnitGrid::Instance();
	auto& room_grid = RoomGrid::Instance();
	auto& micro_manager = MicroManager::Instance();
	auto& information_manager = InformationManager::Instance();
	auto& tactics_manager = TacticsManager::Instance();
	auto& opponent_model = OpponentModel::Instance();
	auto& worker_manager = WorkerManager::Instance();
	auto& building_manager = BuildingManager::Instance();
	auto& building_placement_manager = BuildingPlacementManager::Instance();
	auto& spending_manager = SpendingManager::Instance();
	auto& training_manager = TrainingManager::Instance();
	auto& result_store = ResultStore::Instance();
}

class BananaBrain : public AIModule
{
public:
	// Virtual functions for callbacks, leave these as they are.
	virtual void onStart();
	virtual void onEnd(bool isWinner);
	virtual void onFrame();
	virtual void onSendText(std::string text);
	virtual void onReceiveText(Player player, std::string text);
	virtual void onPlayerLeft(Player player);
	virtual void onNukeDetect(Position target);
	virtual void onUnitDiscover(Unit unit);
	virtual void onUnitEvade(Unit unit);
	virtual void onUnitShow(Unit unit);
	virtual void onUnitHide(Unit unit);
	virtual void onUnitCreate(Unit unit);
	virtual void onUnitDestroy(Unit unit);
	virtual void onUnitMorph(Unit unit);
	virtual void onUnitRenegade(Unit unit);
	virtual void onSaveGame(std::string gameName);
	virtual void onUnitComplete(Unit unit);
	// Everything below this line is safe to modify.
	
	void before();
	void after();
	void surrender_if_hope_lost();
	void draw();
	void draw_info();
	
private:
	int max_duration_ = 0;
	int frame_zero_duration_ = 0;
	bool initialized_ = false;
	bool is_1v1_ = false;
	bool is_ffa_ = false;
	std::unique_ptr<Strategy> strategy_;
};

class PerformanceTimer
{
public:
	PerformanceTimer();
	int duration();
	
private:
	int64_t start_;
};

#include "BananaBrain.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void BananaBrain::onStart()
{
	// Enable the UserInput flag, which allows us to control the bot and type messages.
	//Broodwar->enableFlag(Flag::UserInput);
 
	if (!Broodwar->isReplay())
	{
		bool ok = true;
		
		switch (Broodwar->self()->getRace()) {
			case Races::Protoss:
				strategy_.reset(new ProtossStrategy());
				break;
			case Races::Terran:
				strategy_.reset(new TerranStrategy());
				break;
			case Races::Zerg:
				strategy_.reset(new ZergStrategy());
				break;
			default:
				Broodwar->sendText("Error: This bot only plays Protoss or Terran or Zerg");
				ok = false;
				break;
		}
		
		is_1v1_ = (Broodwar->enemies().size() == 1 && Broodwar->allies().size() == 0);
		
		if (!is_1v1_) {
			if (Broodwar->getGameType() == GameTypes::Free_For_All ||
				Broodwar->getGameType() == GameTypes::Team_Free_For_All) {
				is_ffa_ = true;
			} else {
				int computer_opponents = 0;
				for (auto& enemy : Broodwar->enemies()) {
					if (enemy->getType() == PlayerTypes::Computer) {
						computer_opponents++;
					}
				}
				if (computer_opponents < 2) {
					is_ffa_ = true;
				}
			}
		}
		
		if (ok) {
			configuration.init();
			srand(static_cast<unsigned int>(time(nullptr)));
			bwem_map.Initialize(BroodwarPtr);
			bwem_map.FindBasesForStartingLocations();
			bwem_map.EnableAutomaticPathAnalysis();
			base_state.init_bases();
			path_finder.init();
			opponent_model.init();
			building_placement_manager.init();
			spending_manager.init_resource_counters();
			if (is_1v1_) result_store.init();
			strategy_->pick_strategy(is_1v1_);
			walkability_grid.init();
			room_grid.init();
			worker_manager.init_optimal_mining_data();
			tactics_manager.set_is_ffa(is_ffa_);
			initialized_ = true;
		}
	}
}

void BananaBrain::onEnd(bool winner)
{
	if (!initialized_) return;
	if (is_1v1_) {
		strategy_->apply_result(winner);
		result_store.store();
	}
	worker_manager.store_optimal_mining_data();
}

void BananaBrain::onFrame()
{
	if (!initialized_) return;
	if (Broodwar->isPaused() || !Broodwar->self()) return;
	if (configuration.human_opponent() && Broodwar->getFrameCount() == 240) Broodwar->sendText("glhf");
	
	PerformanceTimer performance_timer;
	
	before();
	strategy_->frame();
	after();
	surrender_if_hope_lost();
	
	if (configuration.draw_enabled()) {
		draw();
		int duration = performance_timer.duration();
		if (Broodwar->getFrameCount() == 0) {
			frame_zero_duration_ = duration;
		} else {
			max_duration_ = std::max(duration, max_duration_);
		}
		Broodwar->drawTextScreen(4, 16, "Frame duration: %d ms, max %d ms, frame zero %d ms", duration, max_duration_, frame_zero_duration_);
	}
}

void BananaBrain::onSendText(std::string text)
{
}

void BananaBrain::onReceiveText(BWAPI::Player player, std::string text)
{
}

void BananaBrain::onPlayerLeft(BWAPI::Player player)
{
}

void BananaBrain::onNukeDetect(BWAPI::Position target)
{
}

void BananaBrain::onUnitDiscover(BWAPI::Unit unit)
{
	if (unit->getType().isBuilding()) connectivity_grid.invalidate();
	information_manager.onUnitDiscover(unit);
}

void BananaBrain::onUnitEvade(BWAPI::Unit unit)
{
	information_manager.onUnitEvade(unit);
}

void BananaBrain::onUnitShow(BWAPI::Unit unit)
{
}

void BananaBrain::onUnitHide(BWAPI::Unit unit)
{
}

void BananaBrain::onUnitCreate(BWAPI::Unit unit)
{
	if (unit->getType().isBuilding()) connectivity_grid.invalidate();
	training_manager.onUnitCreate(unit);
}

void BananaBrain::onUnitDestroy(BWAPI::Unit unit)
{
	information_manager.onUnitDestroy(unit);
	building_manager.onUnitLost(unit);
	training_manager.onUnitLost(unit);
	worker_manager.onUnitLost(unit);
	bwem_handle_destroy_safe(unit);
	if (unit->getType().isBuilding()) connectivity_grid.invalidate();
	building_placement_manager.onUnitDestroy(unit);
}

void BananaBrain::onUnitMorph(BWAPI::Unit unit)
{
	worker_manager.onUnitMorph(unit);
	training_manager.onUnitMorph(unit);
}

void BananaBrain::onUnitRenegade(BWAPI::Unit unit)
{
	worker_manager.onUnitLost(unit);
}

void BananaBrain::onSaveGame(std::string gameName)
{
}

void BananaBrain::onUnitComplete(BWAPI::Unit unit)
{
	training_manager.onUnitComplete(unit);
}

void BananaBrain::draw() {
	draw_info();
	base_state.draw();
	information_manager.draw();
	tactics_manager.draw();
	micro_manager.draw();
	building_placement_manager.draw();
	worker_manager.draw_for_workers();
	//room_grid.draw();
	//threat_grid.draw(true);
}

void BananaBrain::before()
{
	information_manager.update_units_and_buildings();
	walkability_grid.update();
	connectivity_grid.update();
	base_state.update_base_information();
	path_finder.close_small_chokepoints_if_needed();
	unit_grid.update();
	training_manager.init_unit_count_map();
	building_manager.init_building_count_map();
	building_manager.init_base_defense_map();
	building_manager.init_upgrade_and_research();
	building_manager.update_supply_requests();
	information_manager.update_information();
	tactics_manager.update();
	opponent_model.update();
	threat_grid.update();
	micro_manager.prepare_combat();
	worker_manager.before();
}

void BananaBrain::after()
{
	spending_manager.init_spendable();
	worker_manager.after();
	
	training_manager.update_overlord_training();
	if (training_manager.worker_production() && !training_manager.worker_cut()) training_manager.apply_worker_train_orders();
	building_manager.update_requested_building_count_for_pre_upgrade();
	building_manager.apply_building_requests(true);
    building_manager.apply_upgrades(true);
	building_manager.apply_research(true);
	if (training_manager.prioritize_training()) training_manager.apply_train_orders();
	building_manager.apply_building_requests(false);
    building_manager.apply_upgrades(false);
	building_manager.apply_research(false);
	if (!training_manager.prioritize_training()) training_manager.apply_train_orders();
	if (training_manager.worker_production() && training_manager.worker_cut()) training_manager.apply_worker_train_orders();
	
	building_manager.repair_damaged_buildings();
	building_manager.continue_unfinished_buildings_without_worker();
	worker_manager.apply_worker_orders();
	micro_manager.apply_combat_orders();
	building_manager.cancel_doomed_buildings();
}

void BananaBrain::surrender_if_hope_lost()
{
	if ((Broodwar->getFrameCount() % (2 * 24)) != 0) return;
	
	for (auto& unit : Broodwar->self()->getUnits()) {
		if (unit->getType().isResourceDepot() && MineralGas(Broodwar->self()).can_pay(Broodwar->self()->getRace().getWorker())) return;
		if (unit->getType().isBuilding() && !unit->getTrainingQueue().empty()) return;
		if (unit->getType().groundWeapon() != WeaponTypes::None ||
			unit->getType().airWeapon() != WeaponTypes::None ||
			unit->getType() == UnitTypes::Protoss_Carrier ||
			unit->getType() == UnitTypes::Protoss_Reaver ||
			unit->getType() == UnitTypes::Zerg_Egg ||
			unit->getType() == UnitTypes::Zerg_Lurker_Egg ||
			unit->getType() == UnitTypes::Zerg_Cocoon) return;
	}
	
	Unitset enemy_units = Broodwar->enemies().getUnits();
	bool visible_attacker_found = std::any_of(enemy_units.begin(), enemy_units.end(), [](Unit unit){
		return (unit->getType().groundWeapon() != WeaponTypes::None ||
				unit->getType() == UnitTypes::Protoss_Carrier ||
				unit->getType() == UnitTypes::Protoss_Reaver);
	});
	if (!visible_attacker_found) return;
	
	if (configuration.human_opponent()) Broodwar->sendText("gg");
	Broodwar->leaveGame();
}

void BananaBrain::draw_info()
{
	Broodwar->drawTextScreen(4, 26, "Time %s (frame %d)", frame_to_string(Broodwar->getFrameCount()).c_str(), Broodwar->getFrameCount());
	Broodwar->drawTextScreen(4, 36, "Playing: %s vs %s on %s%s", Broodwar->self()->getRace().getName().c_str(), opponent_model.enemy_race().getName().c_str(), Broodwar->mapFileName().c_str(), base_state.is_island_map() ? " (island map)" : "");
	Broodwar->drawTextScreen(4, 46, "Income: %d/%d ratio=%.1f",
							 spending_manager.income_per_minute().minerals,
							 spending_manager.income_per_minute().gas,
							 (double)spending_manager.income_per_minute().minerals / (double)spending_manager.income_per_minute().gas);
	Broodwar->drawTextScreen(4, 56, "Training: %.1f/%.1f/%.1f ratio=%.1f",
							 spending_manager.training_cost_per_minute().minerals,
							 spending_manager.training_cost_per_minute().gas,
							 spending_manager.training_cost_per_minute().supply,
							 spending_manager.training_cost_per_minute().minerals / spending_manager.training_cost_per_minute().gas);
	Broodwar->drawTextScreen(4, 66, "Worker training: %.1f/%.1f/%.1f", spending_manager.worker_training_cost_per_minute().minerals, spending_manager.worker_training_cost_per_minute().gas, spending_manager.worker_training_cost_per_minute().supply);
	Broodwar->drawTextScreen(4, 76, "Remainder: %d/%d", spending_manager.remainder().minerals, spending_manager.remainder().gas);
	Broodwar->drawTextScreen(4, 86, "Spendable: %d/%d", spending_manager.spendable().minerals, spending_manager.spendable().gas);
	Broodwar->drawTextScreen(4, 96, "Worker/Army supply: %g/%g opponent: %g/%g",
							 tactics_manager.worker_supply() * 0.5, tactics_manager.army_supply() * 0.5,
							 tactics_manager.enemy_worker_supply() * 0.5, tactics_manager.enemy_army_supply() * 0.5);
	Broodwar->drawTextScreen(4, 106, "Enemy defense and offense supply: %g/%g",
							 tactics_manager.enemy_defense_supply() * 0.5,
							 tactics_manager.enemy_offense_supply() * 0.5);
	Broodwar->drawTextScreen(4, 116, "Average #workers/mineral: %.1f, #mining bases: %d",
							 worker_manager.average_workers_per_mineral(),
							 base_state.mining_base_count());
	if (Broodwar->self()->getRace() == Races::Protoss) {
		Broodwar->drawTextScreen(4, 126, "Gateway distribution: Z %.2f, D %.2f, Ht %.2f, Dt %.2f",
								 training_manager.gateway_train_distribution().get(UnitTypes::Protoss_Zealot),
								 training_manager.gateway_train_distribution().get(UnitTypes::Protoss_Dragoon),
								 training_manager.gateway_train_distribution().get(UnitTypes::Protoss_High_Templar),
								 training_manager.gateway_train_distribution().get(UnitTypes::Protoss_Dark_Templar));
	}
	if (Broodwar->self()->getRace() == Races::Terran) {
		Broodwar->drawTextScreen(4, 126, "Factory distribution: V %.2f, S %.2f, G %.2f",
								 training_manager.factory_train_distribution().get(UnitTypes::Terran_Vulture),
								 training_manager.factory_train_distribution().get(UnitTypes::Terran_Siege_Tank_Tank_Mode),
								 training_manager.factory_train_distribution().get(UnitTypes::Terran_Goliath));
	}
	if (Broodwar->self()->getRace() == Races::Zerg) {
		Broodwar->drawTextScreen(4, 126, "Larva distribution: D %.2f, O %.2f, Z %.2f, H %.2f, M %.2f S %.2f Df %.2f U %.2f",
								 training_manager.larva_train_distribution().get(UnitTypes::Zerg_Drone),
								 training_manager.larva_train_distribution().get(UnitTypes::Zerg_Overlord),
								 training_manager.larva_train_distribution().get(UnitTypes::Zerg_Zergling),
								 training_manager.larva_train_distribution().get(UnitTypes::Zerg_Hydralisk),
								 training_manager.larva_train_distribution().get(UnitTypes::Zerg_Mutalisk),
								 training_manager.larva_train_distribution().get(UnitTypes::Zerg_Scourge),
								 training_manager.larva_train_distribution().get(UnitTypes::Zerg_Defiler),
								 training_manager.larva_train_distribution().get(UnitTypes::Zerg_Ultralisk));
	}
	Broodwar->drawTextScreen(4, 136, "Mode: %s", strategy_->mode().c_str());
	Broodwar->drawTextScreen(4, 146, "Opening: %s", strategy_->opening().c_str());
	Broodwar->drawTextScreen(4, 156, "Late game strategy: %s", strategy_->late_game_strategy().c_str());
	Broodwar->drawTextScreen(4, 166, "Enemy opening: %s", opponent_model.enemy_opening_info().c_str());
	Broodwar->drawTextScreen(4, 176, "Lost workers/units: %d/%d",
							 worker_manager.lost_worker_count(),
							 training_manager.lost_unit_count());
}

PerformanceTimer::PerformanceTimer()
{
	LARGE_INTEGER start;
	QueryPerformanceCounter(&start);
	start_ = start.QuadPart;
}

int PerformanceTimer::duration()
{
	LARGE_INTEGER end;
	LARGE_INTEGER frequency;
	QueryPerformanceCounter(&end);
	QueryPerformanceFrequency(&frequency);
	double frequency_rec = 1000.0 / frequency.QuadPart;
	return int((end.QuadPart - start_) * frequency_rec + 0.5);
}

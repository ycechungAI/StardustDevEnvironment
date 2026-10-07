#include "BananaBrain.h"

void TerranStrategy::pick_strategy(bool is_1v1)
{
	if (!is_1v1) {
		opening_ = kTvU_1Fact;
		return;
	}
	
	Race race = opponent_model.enemy_race();
	switch (race) {
		case Races::Zerg:
			if (!configuration.TvZ_opening().empty()) {
				opening_ = configuration.TvZ_opening();
			} else {
				opening_ = result_store.pick_strategy({kTvZ_Fantasy,kTvZ_Sparks,kTvZ_Ayumi,kTvZ_1RaxFE,kTvZ_2Rax,kTvZ_14CC,kTvZ_5FactGoliath,kTvZ_2PortWraith,kTvZ_8RaxMech,kTvZ_BBS,kTvZ_ProxyBBS});
			}
			break;
		case Races::Terran:
			if (!configuration.TvT_opening().empty()) {
				opening_ = configuration.TvT_opening();
			} else {
				opening_ = result_store.pick_strategy({kTvT_2FactVults,kTvT_3FactVults,kTvT_1FactFE,kTvT_1FactFEBioMech,kTvT_1RaxFE,kTvT_14CC,kTvT_1PortWraith,kTvT_2PortWraith,kTvT_Proxy5Rax,kTvT_8RaxMech,kTvT_BBS,kTvT_ProxyBBS});
			}
			break;
		case Races::Protoss:
			if (!configuration.TvP_opening().empty()) {
				opening_ = configuration.TvP_opening();
			} else {
				opening_ = result_store.pick_strategy({kTvP_2FactVults,kTvP_GundamRush,kTvP_JoyORush,kTvP_ShallowTwo,kTvP_DeepSix,kTvP_SiegeExpand,kTvP_1FactFE,kTvP_1RaxFE,KTvP_14CC,kTvP_StrongFD,kTvP_101010FD,kTvP_BBS,kTvP_ProxyBBS});
			}
			break;
		default:
			if (!configuration.TvU_opening().empty()) {
				opening_ = configuration.TvU_opening();
			} else {
				opening_ = result_store.pick_strategy({kTvU_1Fact,kTvU_2Rax,kTvU_BBS,kTvU_ProxyBBS});
			}
			break;
	}
}

std::string TerranStrategy::mode() const
{
	switch (mode_) {
		case Mode::Opening:
			return "Opening";
		case Mode::MainMech:
			return "Main Mech";
		case Mode::MainBio:
			return "Main Bio";
		case Mode::MainBioMech:
			return "Main BioMech";
		case Mode::DefendFastPool:
			return "Defend Fast Pool";
		default:
			return "?";
	}
}

void TerranStrategy::opening_TvZ_fantasy()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool && !building_placement_manager.has_active_wall()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendFastPool;
		return;
	}
	if ((is_enemy_offense_larger_than_defense() && !building_placement_manager.has_active_wall()) ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (!opening_wall_positioned_) {
		opening_wall_positioned_successfully_ = building_placement_manager.calculate_tvz_wall_position(true);
		opening_wall_positioned_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		training_manager.unit_count(UnitTypes::Terran_Marine) < 2 &&
		training_manager.unit_count(UnitTypes::Terran_Vulture) == 0 &&
		training_manager.unit_count(UnitTypes::Terran_Goliath) == 0) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
		building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Machine_Shop) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Starport, 1);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Starport)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Control_Tower, 1);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Machine_Shop)) {
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
		building_manager.set_automatic_supply(true);
		if (!done_or_in_progress(TechTypes::Spider_Mines)) {
			building_manager.request_research(TechTypes::Spider_Mines);
		} else if (!done_or_in_progress(UpgradeTypes::Ion_Thrusters)) {
			building_manager.request_upgrade(UpgradeTypes::Ion_Thrusters);
		}
	}
	if (done_or_in_progress(TechTypes::Spider_Mines)) {
		if (!building_manager.request_bases(2)) {
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::MainMech;
			return;
		}
		auto& terran_wall_position = building_placement_manager.terran_wall_position();
		if (terran_wall_position) {
			for (auto& information_unit : information_manager.my_units()) {
				if (information_unit->type == UnitTypes::Terran_Barracks &&
					information_unit->unit->isCompleted() &&
					information_unit->unit->isIdle() &&
					!information_unit->unit->isLifted()) {
					information_unit->unit->lift();
					training_manager.barracks_train_distribution().clear();
				}
			}
		}
		micro_manager.set_spot_with_barracks_and_engineering_bay(true);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center) >= 2 &&
			training_manager.unit_count(UnitTypes::Terran_Dropship) < 1) {
			training_manager.starport_train_distribution().set(UnitTypes::Terran_Dropship, 1.0);
		}
		if (training_manager.unit_count_completed(UnitTypes::Terran_Vulture) >= 4 &&
			training_manager.unit_count_completed(UnitTypes::Terran_Dropship) >= 1) {
			micro_manager.perform_drop(UnitTypes::Terran_Vulture);
			if (training_manager.unit_count_loaded(UnitTypes::Terran_Vulture) >= 4) {
				attacking_ = true;
			}
		}
		if (training_manager.unit_count(UnitTypes::Terran_Dropship) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Armory, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Armory) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 2);
		}
		if (building_manager.building_exists(UnitTypes::Terran_Armory)) {
			if (!done_or_in_progress(UpgradeTypes::Terran_Vehicle_Plating)) {
				building_manager.request_upgrade(UpgradeTypes::Terran_Vehicle_Plating);
			}
			if (training_manager.unit_count(UnitTypes::Terran_Valkyrie) < 1) {
				training_manager.starport_train_distribution().set(UnitTypes::Terran_Valkyrie, 1.0);
			}
			update_factory_train_distribution(false, true);
		}
	}
	if (training_manager.unit_count_completed(UnitTypes::Terran_Valkyrie) > 0 &&
		building_manager.building_count(UnitTypes::Terran_Factory) >= 2 &&
		attacking_) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
}

void TerranStrategy::opening_TvZ_sparks()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainBio;
		return;
	}
	if (expect_lurkers() ||
		opponent_model.cloaked_present()) {
		mode_ = Mode::MainBio;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::MainBio;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 12) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 2);
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (supply >= 20) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	if (supply >= 24) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Academy, 1);
		building_manager.set_automatic_supply(true);
	}
	if (supply >= 27) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 3);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 3) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 1);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Academy)) {
		building_manager.request_research(TechTypes::Stim_Packs);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Engineering_Bay) &&
		done_or_in_progress(TechTypes::Stim_Packs) &&
		!done_or_in_progress(UpgradeTypes::Terran_Infantry_Weapons)) {
		building_manager.request_upgrade(UpgradeTypes::Terran_Infantry_Weapons);
	}
	
	if (done_or_in_progress(TechTypes::Stim_Packs) &&
		done_or_in_progress(UpgradeTypes::Terran_Infantry_Weapons) &&
		training_manager.unit_count_completed(UnitTypes::Terran_Marine) >= 18 &&
		training_manager.unit_count_completed(UnitTypes::Terran_Firebat) >= 2 &&
		training_manager.unit_count_completed(UnitTypes::Terran_Medic) >= 4) {
		attacking_ = true;
		mode_ = Mode::MainBio;
		return;
	}
	
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		bool academy = building_manager.building_exists(UnitTypes::Terran_Academy);
		bool enough_firebats = (training_manager.unit_count(UnitTypes::Terran_Firebat) >= 2);
		bool enough_medics = (training_manager.unit_count(UnitTypes::Terran_Medic) >= 4);
		update_barracks_train_distribution(academy && !enough_firebats, false, academy && !enough_medics);
	}
}

void TerranStrategy::opening_TvZ_ayumi()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainBio;
		return;
	}
	if (expect_lurkers() ||
		opponent_model.cloaked_present()) {
		mode_ = Mode::MainBio;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::MainBio;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 14) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (supply >= 20) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::MainBio;
			return;
		}
	}
	if (supply >= 23) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
	}
	if (supply >= 29) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 4);
	}
	if (supply >= 35) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	if (supply >= 37) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 4);
	}
	if (supply >= 38) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Academy, 1);
	}
	if (supply >= 44) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 5);
	}
	if (supply >= 52) {
		building_manager.set_automatic_supply(true);
		building_manager.request_research(TechTypes::Stim_Packs);
		if (training_manager.unit_count(UnitTypes::Terran_Medic) < 4) {
			training_manager.barracks_train_distribution().set(UnitTypes::Terran_Medic, 1.0);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		training_manager.barracks_train_distribution().is_empty()) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (done_or_in_progress(TechTypes::Stim_Packs) &&
		training_manager.unit_count_completed(UnitTypes::Terran_Marine) >= 24 &&
		training_manager.unit_count_completed(UnitTypes::Terran_Medic) >= 4) {
		attacking_ = true;
		mode_ = Mode::MainBio;
		return;
	}
}

void TerranStrategy::opening_TvZ_1raxfe()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		building_placement_manager.clear_specific_positions();
		worker_manager.stop_combat();
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		worker_manager.stop_combat();
		mode_ = Mode::MainBio;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 11) {
		worker_manager.send_initial_scout();
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 13 && base_state.start_base_count() >= 4) {
		worker_manager.send_second_scout();
	}
	if (opponent_model.enemy_opening() == EnemyOpening::Z_12Hatch) {
		if (!building_placement_manager.specific_positions_for_type(UnitTypes::Terran_Command_Center).empty()) {
			building_placement_manager.clear_specific_positions_for_type(UnitTypes::Terran_Command_Center);
			building_manager.cancel_extra_buildings_of_type(UnitTypes::Terran_Command_Center, 2, 1);
		}
		worker_manager.stop_combat();
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Bunker);
		
		if (supply >= 15 &&
			building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center) < 2) {
			if (!building_manager.request_bases(2)) {
				mode_ = Mode::MainBio;
				return;
			}
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center) >= 2) {
			training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
			additional_barracks();
		}
		if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
			contains(base_state.controlled_and_planned_bases(), base_state.natural_base())) {
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::MainBio;
			return;
		}
	} else {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
		
		if ((supply >= 15 &&
			 opponent_model.enemy_opening() != EnemyOpening::Unknown) ||
			supply >= 18) {
			worker_manager.combat(3);
			if (opening_command_center_location_ == TilePositions::Unknown) {
				opening_command_center_location_ = building_placement_manager.calculate_command_center_in_main_position();
			}
			if (opening_command_center_location_.isValid()) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Command_Center, 2);
			} else {
				if (!building_manager.request_bases(2)) {
					building_placement_manager.clear_specific_positions();
					worker_manager.stop_combat();
					mode_ = Mode::MainBio;
					return;
				}
			}
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center) >= 2) {
			additional_barracks();
		}
		if (tactics_manager.defense_supply() > tactics_manager.enemy_offense_supply() &&
			building_manager.building_count(UnitTypes::Terran_Command_Center) >= 2) {
			if (contains(base_state.controlled_and_planned_bases(), base_state.natural_base())) {
				worker_manager.stop_combat();
				if (opening_bunker_location_ == TilePositions::Unknown) {
					opening_bunker_location_ = building_placement_manager.calculate_bunker_near_choke_position(true);
				}
				const auto bunker_location_safe = [&](){
					for (int y = 0; y < UnitTypes::Terran_Bunker.tileHeight(); y++) {
						for (int x = 0; x < UnitTypes::Terran_Bunker.tileWidth(); x++) {
							FastTilePosition tile_position = opening_bunker_location_ + FastTilePosition(x, y);
							if (threat_grid.ground_threat(tile_position)) {
								return false;
							}
						}
					}
					return true;
				};
				if (!opening_bunker_location_.isValid() || bunker_location_safe()) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Bunker, 1);
				}
				if (building_manager.building_exists(UnitTypes::Terran_Bunker) &&
					contains(base_state.controlled_bases(), base_state.natural_base())) {
					building_placement_manager.clear_specific_positions();
					mode_ = Mode::MainBio;
					return;
				}
			}
		}
	}
	
	if (tactics_manager.defense_supply() > tactics_manager.enemy_offense_supply() &&
		building_manager.building_count(UnitTypes::Terran_Command_Center) >= 2) {
		if (opening_command_center_location_.isValid()) {
			for (auto& information_unit : information_manager.my_units()) {
				if (information_unit->type == UnitTypes::Terran_Command_Center &&
					information_unit->tile_position() == opening_command_center_location_ &&
					information_unit->unit->isCompleted() &&
					information_unit->unit->isIdle() &&
					!information_unit->unit->isLifted()) {
					information_unit->unit->lift();
					training_manager.set_worker_production(false);
				}
			}
		}
	}
	
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center) >= 2) {
		building_manager.set_automatic_supply(true);
	}
}

void TerranStrategy::opening_TvZ_2rax()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainBio;
		return;
	}
	if (expect_lurkers() ||
		opponent_model.cloaked_present()) {
		mode_ = Mode::MainBio;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::MainBio;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 2);
	}
	if (supply >= 14) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (building_manager.building_count(UnitTypes::Terran_Supply_Depot) >= 2) {
		building_manager.set_automatic_supply(true);
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	if (supply >= 18) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Academy, 1);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Academy) &&
		!done_or_in_progress(TechTypes::Stim_Packs)) {
		building_manager.request_research(TechTypes::Stim_Packs);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		bool academy = building_manager.building_exists(UnitTypes::Terran_Academy);
		bool enough_medics = (training_manager.unit_count(UnitTypes::Terran_Medic) >= 2);
		update_barracks_train_distribution(academy && enough_medics, false, academy && !enough_medics);
	}
	if (Broodwar->self()->hasResearched(TechTypes::Stim_Packs) &&
		training_manager.unit_count_completed(UnitTypes::Terran_Medic) >= 2) {
		attacking_ = true;
		mode_ = Mode::MainBio;
		return;
	}
}

void TerranStrategy::opening_TvZ_14cc()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool && !building_placement_manager.has_active_wall()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendFastPool;
		return;
	}
	if ((is_enemy_offense_larger_than_defense() && !building_placement_manager.has_active_wall()) ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainBio;
		return;
	}
	if (!opening_wall_positioned_) {
		opening_wall_positioned_successfully_ = building_placement_manager.calculate_tvz_natural_wall_position(true);
		opening_wall_positioned_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 14) {
		if (!building_manager.request_bases(2)) {
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::MainBio;
			return;
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
		}
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (supply >= 18) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	bool bunker_needed = true;
	auto& terran_natural_wall_position = building_placement_manager.terran_natural_wall_position();
	if (terran_natural_wall_position && terran_natural_wall_position->gap_unit_count == 0) {
		bunker_needed = false;
	}
	if (supply >= 20 && bunker_needed) {
		if (!opening_wall_positioned_successfully_ && opening_bunker_location_ == TilePositions::Unknown) {
			opening_bunker_location_ = building_placement_manager.calculate_bunker_near_choke_position();
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Bunker, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Bunker) >= 1 ||
		(!bunker_needed && building_manager.building_count_including_planned(UnitTypes::Terran_Refinery) >= 1)) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainBio;
		return;
	}
}

void TerranStrategy::opening_TvZ_5factgoliath()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainMech;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::MainMech;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 12) {
		worker_manager.send_initial_scout();
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Terran_Marine) < 2) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (supply >= 17) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1) {
			worker_manager.set_limit_refinery_workers(0);
		}
	}
	if (supply >= 20) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::MainMech;
			return;
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center) >= 2) {
			worker_manager.set_limit_refinery_workers(-1);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Terran_Vulture) < 3) {
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
	}
	if (supply >= 23) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
	}
	if (supply >= 24) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Armory, 1);
	}
	if (supply >= 29) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 2);
	}
	if (training_manager.unit_count_built(UnitTypes::Terran_Vulture) >= 3) {
		attack_with_vultures_ = true;
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
		if (!done_or_in_progress(UpgradeTypes::Terran_Vehicle_Plating)) {
			building_manager.request_upgrade(UpgradeTypes::Terran_Vehicle_Plating);
		}
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Machine_Shop) >= 1) {
		building_manager.request_upgrade(UpgradeTypes::Charon_Boosters);
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Goliath, 1.0);
		building_manager.set_automatic_supply(true);
	}
	if (training_manager.unit_count_built(UnitTypes::Terran_Goliath) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 3);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 3) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 1);
		}
		if (building_manager.building_exists(UnitTypes::Terran_Engineering_Bay)) {
			for (auto& base : base_state.controlled_bases()) {
				building_manager.set_requested_base_defense_turret_count_at_least(base, 2);
			}
		}
		if (building_manager.building_count(UnitTypes::Terran_Factory) >= 3 &&
			building_manager.building_exists(UnitTypes::Terran_Missile_Turret)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 2);
		}
	}
	if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Terran_Vehicle_Plating) >= 1 &&
		!done_or_in_progress(UpgradeTypes::Terran_Vehicle_Weapons)) {
		building_manager.request_upgrade(UpgradeTypes::Terran_Vehicle_Weapons);
	}
	if (training_manager.unit_count_built(UnitTypes::Terran_Goliath) >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 5);
		if (!opening_attack_started_) {
			opening_attack_started_ = true;
			attacking_ = true;
		}
	}
	if (opening_attack_started_) {
		attack_check_condition();
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 5) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Academy, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Academy) >= 1 &&
		training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) < 3) {
		training_manager.factory_train_distribution().clear();
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Academy)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Comsat_Station, 2);
		building_manager.request_research(TechTypes::Tank_Siege_Mode);
	}
	if (done_or_in_progress(UpgradeTypes::Terran_Vehicle_Plating) &&
		done_or_in_progress(UpgradeTypes::Terran_Vehicle_Weapons) &&
		done_or_in_progress(TechTypes::Tank_Siege_Mode)) {
		if (!building_manager.request_bases(3)) {
			mode_ = Mode::MainMech;
			return;
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center) >= 3) {
			mode_ = Mode::MainMech;
			return;
		}
	}
	
	if (building_manager.building_exists(UnitTypes::Terran_Factory)) {
		micro_manager.set_spot_with_barracks_and_engineering_bay(true);
		for (auto& information_unit : information_manager.my_units()) {
			if ((information_unit->type == UnitTypes::Terran_Barracks ||
				 information_unit->type == UnitTypes::Terran_Engineering_Bay) &&
				information_unit->unit->isCompleted() &&
				information_unit->unit->isIdle() &&
				!information_unit->unit->isLifted()) {
				information_unit->unit->lift();
			}
		}
	}
}

void TerranStrategy::opening_TvT_3factvults()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax) {
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Machine_Shop);
		mode_ = Mode::MainMech;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::MainMech;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Refinery) >= 1) {
			worker_manager.send_initial_scout();
		}
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (supply >= 16 &&
		training_manager.unit_count(UnitTypes::Terran_Marine) < 1) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1);
	}
	if (supply >= 18) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1 &&
			training_manager.unit_count(UnitTypes::Terran_Marine) < 2) {
			training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1);
		}
	}
	if (supply >= 20) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 2 &&
			training_manager.unit_count(UnitTypes::Terran_Marine) < 3) {
			training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory)) {
		if (training_manager.unit_count(UnitTypes::Terran_Vulture) < 1) {
			training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1);
		} else {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
		}
	}
	if (supply >= 22) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
	}
	if (supply >= 25) {
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
		if (training_manager.unit_count(UnitTypes::Terran_Vulture) >= 2) {
			building_manager.request_upgrade(UpgradeTypes::Ion_Thrusters);
		}
	}
	if (supply >= 27) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 2);
	}
	if (supply >= 30) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 4);
	}
	if (supply >= 32) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 3);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 3) {
			building_manager.request_research(TechTypes::Spider_Mines);
			if (done_or_in_progress(UpgradeTypes::Ion_Thrusters) &&
				done_or_in_progress(TechTypes::Spider_Mines)) {
				mode_ = Mode::MainMech;
			}
		}
	}
}

void TerranStrategy::opening_TvT_1factfe(bool biomech)
{
	Mode followup_mode = biomech ? Mode::MainBioMech : Mode::MainMech;
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = followup_mode;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Terran_Command_Center, 1, 1);
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Machine_Shop);
		mode_ = followup_mode;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = followup_mode;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		training_manager.unit_count(UnitTypes::Terran_Marine) < 2) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (supply >= 13) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		}
	}
	if (Broodwar->self()->gatheredGas() >= UnitTypes::Terran_Factory.gasPrice() - 16 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Terran_Vulture) == 0) {
		int shortage = UnitTypes::Terran_Factory.gasPrice() - Broodwar->self()->gatheredGas();
		worker_manager.set_limit_refinery_workers(std::max(1, shortage / 8));
	}
	if (supply >= 20) {
		if (!building_manager.request_bases(2)) {
			mode_ = followup_mode;
			return;
		}
		if (training_manager.unit_count(UnitTypes::Terran_Vulture) < 1) {
			training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
		}
	}
	if (supply >= 23) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
	}
	if (supply >= 24) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
	}
	if (supply >= 26) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 2);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 2) {
		mode_ = followup_mode;
		return;
	}
}

void TerranStrategy::opening_TvT_1raxfe()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainMech;
		building_placement_manager.clear_specific_positions();
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Terran_Command_Center, 1, 1);
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 12 && building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 14) {
		training_manager.set_worker_production(false);
		if (training_manager.unit_count(UnitTypes::Terran_Marine) < 1) {
			training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
		}
	}
	if (supply >= 15) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::MainMech;
			building_placement_manager.clear_specific_positions();
			return;
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Refinery) >= 1 &&
			training_manager.unit_count(UnitTypes::Terran_Marine) < 2) {
			training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
		}
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 2) {
			training_manager.set_worker_production(true);
		}
	}
	if (supply >= 21) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1) {
			if (opponent_model.enemy_opening() == EnemyOpening::T_FastExpand) {
				building_manager.cancel_buildings_of_type(UnitTypes::Terran_Bunker);
				if (!attacking_) {
					attacking_ = true;
					worker_manager.combat(1);
					opening_attack_with_workers_ = true;
				}
				if (building_manager.building_exists(UnitTypes::Terran_Factory)) {
					mode_ = Mode::MainMech;
					building_placement_manager.clear_specific_positions();
				}
			} else {
				if (opening_bunker_location_ == TilePositions::Unknown) {
					opening_bunker_location_ = building_placement_manager.calculate_bunker_near_choke_position();
				}
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Bunker, 1);
				if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
					building_manager.building_count_including_planned(UnitTypes::Terran_Bunker) >= 1) {
					mode_ = Mode::MainMech;
					building_placement_manager.clear_specific_positions();
				}
			}
		}
	}
}

void TerranStrategy::opening_TvT_14cc()
{
	if ((is_enemy_offense_larger_than_defense() && !building_placement_manager.has_active_wall()) ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Terran_Command_Center, 1, 1);
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Barracks);
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (!opening_wall_positioned_) {
		opening_wall_positioned_successfully_ = building_placement_manager.calculate_tvt_natural_wall_position(true);
		opening_wall_positioned_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 14) {
		if (!building_manager.request_bases(2)) {
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::MainMech;
			return;
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
		}
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Refinery) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		opponent_model.enemy_opening() != EnemyOpening::T_FastExpand) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (supply >= 21) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
	}
	if (supply >= 23 &&
		opponent_model.enemy_opening() != EnemyOpening::T_FastExpand) {
		if (!opening_wall_positioned_successfully_ && opening_bunker_location_ == TilePositions::Unknown) {
			opening_bunker_location_ = building_placement_manager.calculate_bunker_near_choke_position();
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Bunker, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Bunker) >= 1 ||
		(opponent_model.enemy_opening() == EnemyOpening::T_FastExpand &&
		 building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1)) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
}

void TerranStrategy::opening_TvT_1portwraith()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax) {
		mode_ = Mode::MainMech;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::MainMech;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Refinery) >= 1) {
			worker_manager.send_initial_scout();
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		training_manager.unit_count(UnitTypes::Terran_Marine) < 1) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		if (building_manager.building_exists(UnitTypes::Terran_Factory)) {
			if (training_manager.unit_count(UnitTypes::Terran_Vulture) < 1) {
				training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1);
			} else {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
			}
		}
	}
	if (supply >= 17) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (supply >= 24) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Starport, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Starport) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Starport)) {
		training_manager.starport_train_distribution().set(UnitTypes::Terran_Wraith, 1.0);
	}
	if (supply >= 28) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 4);
		if (training_manager.unit_count(UnitTypes::Terran_Wraith) >= 1) {
			wraith_strategy_ = true;
			mode_ = Mode::MainMech;
		}
	}
}

void TerranStrategy::opening_TvT_proxy5rax()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (opening_proxy_location_ == TilePositions::Unknown) {
		opening_proxy_location_ = building_placement_manager.calculate_proxy_barracks_position();
	}
	int supply = opening_supply_count();
	if (opening_proxy_location_.isValid() &&
		building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) < 1) {
		worker_manager.send_proxy_builder(center_position_for(UnitTypes::Terran_Barracks, opening_proxy_location_), 0);
	}
	if (supply >= 5) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 7) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) {
		building_manager.set_automatic_supply(true);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		worker_manager.send_initial_scout();
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
		additional_barracks();
	}
	if (information_manager.enemy_building_seen() ||
		tactics_manager.enemy_start_base() != nullptr ||
		tactics_manager.possible_enemy_start_bases().size() <= 1) {
		worker_manager.stop_scouting();
	}
	if (!opening_attack_started_ &&
		training_manager.unit_count_completed(UnitTypes::Terran_Marine) >= 1) {
		opening_attack_started_ = true;
		attacking_ = true;
		building_placement_manager.clear_specific_positions();
	}
	if (base_state.mineable_mineral_count() < 1000) {
		building_manager.request_next_base();
	}
}

void TerranStrategy::opening_TvP_gundamrush()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::MainMech;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	if (supply >= 13) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 17) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		if (training_manager.unit_count(UnitTypes::Terran_Marine) < 1) {
			training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
		} else {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 2);
		}
		if (building_manager.building_exists(UnitTypes::Terran_Factory)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Terran_Machine_Shop) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
				training_manager.set_worker_cut(true);
			}
		}
		if (building_manager.building_exists(UnitTypes::Terran_Machine_Shop)) {
			training_manager.set_worker_cut(false);
			training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, 1.0);
			if (training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) >= 1) {
				building_manager.request_research(TechTypes::Spider_Mines);
			}
		}
		if (building_manager.building_count(UnitTypes::Terran_Factory) >= 2 &&
			training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) >= 1) {
			training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
		}
		if (Broodwar->self()->hasResearched(TechTypes::Spider_Mines)) {
			building_manager.request_research(TechTypes::Tank_Siege_Mode);
		}
	}
	if (training_manager.unit_count_completed(UnitTypes::Terran_Vulture) >= 1 &&
		training_manager.unit_count_completed(UnitTypes::Terran_Siege_Tank_Tank_Mode) >= 1) {
		attacking_ = true;
		worker_manager.combat(3);
		opening_attack_with_workers_ = true;
		if (done_or_in_progress(TechTypes::Spider_Mines) &&
			done_or_in_progress(TechTypes::Tank_Siege_Mode)) {
			mode_ = Mode::MainMech;
			return;
		}
	}
}

void TerranStrategy::opening_TvP_joyorush()
{
	if ((is_enemy_offense_larger_than_defense() && !building_placement_manager.has_active_wall()) ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (!opening_wall_positioned_) {
		opening_wall_positioned_successfully_ = building_placement_manager.calculate_tvp_wall_position(true);
		opening_wall_positioned_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
	}
	if (supply >= 19) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 2);
		if (training_manager.unit_count(UnitTypes::Terran_Marine) < 7) {
			training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
	}
	if (supply >= 22) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
		building_manager.building_exists(UnitTypes::Terran_Machine_Shop) &&
		training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) < 1) {
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, 1.0);
	}
	if (training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) >= 1 &&
		building_manager.building_count(UnitTypes::Terran_Factory) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 2);
	}
	if (supply >= 28) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 4);
		if (training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) < 3) {
			training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, 1.0);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Machine_Shop)) {
		if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Ion_Thrusters) < 1) {
			building_manager.request_upgrade(UpgradeTypes::Ion_Thrusters);
		} else if (!Broodwar->self()->hasResearched(TechTypes::Spider_Mines)) {
			building_manager.request_research(TechTypes::Spider_Mines);
		}
	}
	if (training_manager.unit_count_completed(UnitTypes::Terran_Siege_Tank_Tank_Mode) >= 3 &&
		building_manager.building_count(UnitTypes::Terran_Factory) >= 2 &&
		done_or_in_progress(UpgradeTypes::Ion_Thrusters) &&
		done_or_in_progress(TechTypes::Spider_Mines)) {
		building_placement_manager.clear_specific_positions();
		attacking_ = true;
		worker_manager.combat(3);
		opening_attack_with_workers_ = true;
		mode_ = Mode::MainMech;
		return;
	}
}

void TerranStrategy::opening_TvP_shallowtwo()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainBioMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainBioMech;
		return;
	}
	if (opening_bunker_location_ == TilePositions::Unknown) {
		opening_bunker_location_ = building_placement_manager.calculate_bunker_near_choke_position();
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 2);
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (building_manager.building_count(UnitTypes::Terran_Supply_Depot) >= 2) {
		building_manager.set_automatic_supply(true);
	}
	if (supply >= 17) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Bunker, 1);
	}
	if (supply >= 23) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	if (supply >= 26) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Academy, 1);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Academy)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Comsat_Station, 1);
		building_manager.request_research(TechTypes::Stim_Packs);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		bool academy = building_manager.building_exists(UnitTypes::Terran_Academy);
		bool enough_medics = (training_manager.unit_count(UnitTypes::Terran_Medic) >= 2);
		update_barracks_train_distribution(academy && enough_medics, false, academy && !enough_medics);
	}
	if (Broodwar->self()->hasResearched(TechTypes::Stim_Packs) &&
		training_manager.unit_count_completed(UnitTypes::Terran_Medic) >= 2) {
		if (training_manager.unit_count_loaded(UnitTypes::Terran_Marine) > 0) {
			micro_manager.unload_bunkers();
		} else {
			attacking_ = true;
			mode_ = Mode::MainBioMech;
			building_placement_manager.clear_specific_positions();
			return;
		}
	}
}

void TerranStrategy::opening_TvP_deepsix()
{
	if ((is_enemy_offense_larger_than_defense() && !building_placement_manager.has_active_wall()) ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainBioMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainBioMech;
		return;
	}
	opening_TvP_siegeexpand_start(true);
	int supply = opening_supply_count();
	if (building_manager.building_count(UnitTypes::Terran_Command_Center) >= 2 &&
		done_or_in_progress(TechTypes::Tank_Siege_Mode)) {
		building_manager.set_automatic_supply(true);
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Refinery) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 2);
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 2);
		}
		if (building_manager.building_count(UnitTypes::Terran_Factory) >= 2 &&
			building_manager.building_count(UnitTypes::Terran_Machine_Shop) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Engineering_Bay) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Academy, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Academy) >= 1 &&
			building_manager.building_exists(UnitTypes::Terran_Engineering_Bay)) {
			for (auto& base : base_state.controlled_bases()) {
				building_manager.set_requested_base_defense_turret_count_at_least(base, 1);
			}
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Missile_Turret) >= (int)base_state.controlled_bases().size()) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 6);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 6) {
			building_manager.request_research(TechTypes::Stim_Packs);
			if (Broodwar->self()->hasResearched(TechTypes::Stim_Packs)) {
				building_manager.request_upgrade(UpgradeTypes::U_238_Shells);
			}
			building_manager.request_upgrade(UpgradeTypes::Terran_Infantry_Armor);
		}
		if (building_manager.building_count(UnitTypes::Terran_Barracks) >= 6) {
			update_bio_train_distribution(true);
			if (done_or_in_progress(TechTypes::Stim_Packs) &&
				done_or_in_progress(UpgradeTypes::U_238_Shells) &&
				done_or_in_progress(UpgradeTypes::Terran_Infantry_Armor)) {
				building_placement_manager.clear_specific_positions();
				mode_ = Mode::MainBioMech;
			}
		}
	}
}

void TerranStrategy::opening_TvP_siegeexpand()
{
	if ((is_enemy_offense_larger_than_defense() && !building_placement_manager.has_active_wall()) ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	opening_TvP_siegeexpand_start(false);
	int supply = opening_supply_count();
	if (supply >= 28) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Engineering_Bay) >= 1 &&
		done_or_in_progress(TechTypes::Tank_Siege_Mode)) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
	}
}

void TerranStrategy::opening_TvP_siegeexpand_start(bool biomech)
{
	if (!opening_wall_positioned_) {
		opening_wall_positioned_successfully_ = building_placement_manager.calculate_tvp_natural_wall_position();
		opening_wall_positioned_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		}
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1) {
			worker_manager.set_limit_refinery_workers(1);
		}
		if (building_manager.building_exists(UnitTypes::Terran_Factory)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Terran_Machine_Shop) >= 1) {
				worker_manager.set_limit_refinery_workers(-1);
			}
			if (building_manager.building_exists(UnitTypes::Terran_Machine_Shop) &&
				training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) + training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Siege_Mode) < 1) {
				training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, 1.0);
			}
		}
	}
	if (supply >= 21) {
		if (!building_manager.request_bases(2)) {
			building_placement_manager.clear_specific_positions();
			mode_ = biomech ? Mode::MainBioMech : Mode::MainMech;
			return;
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center) >= 2) {
			training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, 1.0);
		}
	}
	if (supply >= 24) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
	}
	if (supply >= 25) {
		building_manager.request_research(TechTypes::Tank_Siege_Mode);
	}
}

void TerranStrategy::opening_TvP_1factfe()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainMech;
		building_placement_manager.clear_specific_positions();
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 12 && building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		training_manager.unit_count(UnitTypes::Terran_Marine) < 3) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (supply >= 13) {
		worker_manager.send_initial_scout();
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (supply >= 16 &&
		building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
	}
	if (Broodwar->self()->gatheredGas() >= UnitTypes::Terran_Factory.gasPrice() - 16 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Terran_Vulture) == 0) {
		int shortage = UnitTypes::Terran_Factory.gasPrice() - Broodwar->self()->gatheredGas();
		worker_manager.set_limit_refinery_workers(std::max(1, shortage / 8));
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
		training_manager.unit_count(UnitTypes::Terran_Vulture) < 1) {
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
	}
	if (supply >= 20) {
		if (opening_bunker_location_ == TilePositions::Unknown) {
			opening_bunker_location_ = building_placement_manager.calculate_bunker_near_choke_position(true);
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Bunker, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Bunker) >= 1 &&
			!building_manager.request_bases(2)) {
			mode_ = Mode::MainMech;
			building_placement_manager.clear_specific_positions();
			return;
		}
	}
	if (supply >= 23) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
	}
	if (supply >= 24) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
	}
	if (supply >= 26) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 2);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 2) {
		mode_ = Mode::MainMech;
		building_placement_manager.clear_specific_positions();
		return;
	}
}

void TerranStrategy::opening_TvP_1raxfe()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainMech;
		building_placement_manager.clear_specific_positions();
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 12 && building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 15) {
		training_manager.set_worker_production(false);
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::MainMech;
			building_placement_manager.clear_specific_positions();
			return;
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center)) {
			training_manager.set_worker_production(true);
		}
	}
	bool opponent_fast_expanding = (opponent_model.enemy_opening() == EnemyOpening::P_FastExpand ||
									opponent_model.enemy_opening() == EnemyOpening::P_ForgeFastExpand);
	if (supply >= 16) {
		training_manager.set_worker_production(false);
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Refinery) >= 1) {
			training_manager.set_worker_production(true);
			if (training_manager.unit_count(UnitTypes::Terran_Marine) < 2 &&
				!opponent_fast_expanding) {
				training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
			}
		}
	}
	if (supply >= 21) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1) {
			if (opponent_fast_expanding) {
				mode_ = Mode::MainMech;
			} else {
				if (opening_bunker_location_ == TilePositions::Unknown) {
					opening_bunker_location_ = building_placement_manager.calculate_bunker_near_choke_position();
				}
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Bunker, 1);
				if (building_manager.building_count_including_planned(UnitTypes::Terran_Bunker) >= 1) {
					mode_ = Mode::MainMech;
					building_placement_manager.clear_specific_positions();
				}
			}
		}
	}
}

void TerranStrategy::opening_TvP_14cc()
{
	if ((is_enemy_offense_larger_than_defense() && !building_placement_manager.has_active_wall()) ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (!opening_wall_positioned_) {
		opening_wall_positioned_successfully_ = building_placement_manager.calculate_tvp_natural_wall_position(true);
		opening_wall_positioned_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 14) {
		if (!building_manager.request_bases(2)) {
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::MainMech;
			return;
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
		}
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Refinery) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
		}
	}
	bool opponent_fast_expanding = (opponent_model.enemy_opening() == EnemyOpening::P_FastExpand ||
									opponent_model.enemy_opening() == EnemyOpening::P_ForgeFastExpand);
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		!opponent_fast_expanding) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (supply >= 21) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
	}
	if (supply >= 23 && !opponent_fast_expanding) {
		if (!opening_wall_positioned_successfully_ && opening_bunker_location_ == TilePositions::Unknown) {
			opening_bunker_location_ = building_placement_manager.calculate_bunker_near_choke_position();
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Bunker, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Bunker) >= 1 ||
		(opponent_fast_expanding &&
		 building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1)) {
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::MainMech;
			return;
		}
}

void TerranStrategy::opening_TvP_strongfd()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::MainMech;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 10) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
		training_manager.set_worker_production(false);
		if (building_manager.building_count_including_warping(UnitTypes::Terran_Barracks) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
			if (building_manager.building_exists(UnitTypes::Terran_Refinery)) {
				training_manager.set_worker_production(true);
			}
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Refinery)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		training_manager.unit_count(UnitTypes::Terran_Marine) < 8) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (Broodwar->self()->gatheredGas() >= 100) {
		worker_manager.set_limit_refinery_workers(1);
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
		worker_manager.set_limit_refinery_workers(Broodwar->self()->gatheredGas() >= 300 ? 1 : 3);
	}
	if (supply >= 22) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
	}
	if (supply >= 28) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 4);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Machine_Shop)) {
		if (supply >= 34 && !done_or_in_progress(TechTypes::Spider_Mines)) {
			building_manager.request_research(TechTypes::Spider_Mines);
		} else if (training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) < 2) {
			training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, 1.0);
		} else if (training_manager.unit_count(UnitTypes::Terran_Vulture) < 2) {
			training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
		} else {
			training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, 1.0);
		}
	}
	if (training_manager.unit_count_completed(UnitTypes::Terran_Siege_Tank_Tank_Mode) >= 2 &&
		!opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) >= 2) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::MainMech;
			return;
		}
	}
	if (building_manager.building_count_including_warping(UnitTypes::Terran_Command_Center) >= 2 &&
		done_or_in_progress(TechTypes::Spider_Mines) &&
		opening_attack_started_) {
		mode_ = Mode::MainMech;
		return;
	}
}

void TerranStrategy::opening_TvP_101010fd()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::MainMech;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 10) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		training_manager.set_worker_production(false);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1 &&
			Broodwar->self()->minerals() >= UnitTypes::Terran_Barracks.mineralPrice() + UnitTypes::Terran_Refinery.mineralPrice()) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 1 &&
			building_manager.building_count_including_planned(UnitTypes::Terran_Refinery) >= 1) {
			training_manager.set_worker_production(true);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Refinery)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (supply >= 13) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 14) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Terran_Factory) >= 1) {
			worker_manager.set_limit_refinery_workers(1);
		}
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
		if (training_manager.unit_count_built(UnitTypes::Terran_Marine) < 8) {
			training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
		}
	}
	if (supply >= 19) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
		worker_manager.set_limit_refinery_workers(-1);
		if (building_manager.building_exists(UnitTypes::Terran_Machine_Shop)) {
			if (training_manager.unit_count_built(UnitTypes::Terran_Siege_Tank_Tank_Mode) < 2) {
				training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, 1.0);
			} else {
				training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
			}
			if (!done_or_in_progress(TechTypes::Spider_Mines)) {
				building_manager.request_research(TechTypes::Spider_Mines);
			} else if (!done_or_in_progress(TechTypes::Tank_Siege_Mode)) {
				building_manager.request_research(TechTypes::Tank_Siege_Mode);
			}
		}
	}
	if (supply >= 28) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 4);
	}
	if (!opening_attack_started_ &&
		training_manager.unit_count_built(UnitTypes::Terran_Marine) >= 8 &&
		training_manager.unit_count_built(UnitTypes::Terran_Siege_Tank_Tank_Mode) >= 2) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_ &&
		done_or_in_progress(TechTypes::Tank_Siege_Mode)) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::MainMech;
			return;
		}
		if (building_manager.building_count(UnitTypes::Terran_Command_Center) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 2);
			if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 2) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 1);
				if (building_manager.building_count_including_planned(UnitTypes::Terran_Engineering_Bay) >= 1) {
					mode_ = Mode::MainMech;
					return;
				}
			}
		}
	}
}

void TerranStrategy::opening_TvU_1fact()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool && !building_placement_manager.has_active_wall()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_enemy_offense_larger_than_defense() &&
		!building_placement_manager.has_active_wall()) {
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Starport);
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (!opening_wall_positioned_) {
		opening_wall_positioned_successfully_ = building_placement_manager.calculate_tvz_wall_position();
		opening_wall_positioned_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		if (base_state.start_base_count() >= 4) worker_manager.send_second_scout();
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1 &&
			opponent_model.enemy_race() == Races::Protoss) {
			building_manager.cancel_buildings_of_type(UnitTypes::Terran_Starport);
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::MainMech;
			return;
		}
	}
	if (supply >= 22) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Starport, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Starport) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Starport) < 2 ||
			building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) < 3) {
			training_manager.set_worker_production(false);
		}
	}
	if (supply >= 30) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 4);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		training_manager.unit_count(UnitTypes::Terran_Marine) < 4) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
		training_manager.unit_count(UnitTypes::Terran_Vulture) < 1) {
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Starport)) {
		training_manager.starport_train_distribution().set(UnitTypes::Terran_Wraith, 1.0);
	}
	if (building_manager.building_count(UnitTypes::Terran_Starport) >= 2 &&
		building_manager.building_count(UnitTypes::Terran_Supply_Depot) >= 4 &&
		training_manager.unit_count_completed(UnitTypes::Terran_Wraith) >= 2) {
		attacking_ = true;
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
}

void TerranStrategy::opening_TvU_2rax()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	Mode followup_mode = (opponent_model.enemy_race() == Races::Zerg) ? Mode::MainBio : Mode::MainBioMech;
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = followup_mode;
		return;
	}
	if (opponent_model.enemy_race() == Races::Zerg &&
		(expect_lurkers() ||
		 opponent_model.cloaked_present())) {
		mode_ = Mode::MainBio;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = followup_mode;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 12) {
		worker_manager.send_second_scout();
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 2);
	}
	if (supply >= 14) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (building_manager.building_count(UnitTypes::Terran_Supply_Depot) >= 2) {
		building_manager.set_automatic_supply(true);
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	if (supply >= 17 &&
		opponent_model.enemy_race() == Races::Protoss) {
		if (opening_bunker_location_ == TilePositions::Unknown) {
			opening_bunker_location_ = building_placement_manager.calculate_bunker_near_choke_position();
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Bunker, 1);
	}
	if (supply >= 18) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Academy, 1);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Academy)) {
		if (!done_or_in_progress(TechTypes::Stim_Packs)) {
			building_manager.request_research(TechTypes::Stim_Packs);
		}
		if (opponent_model.enemy_race() == Races::Protoss) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Comsat_Station, 1);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		bool academy = building_manager.building_exists(UnitTypes::Terran_Academy);
		bool enough_medics = (training_manager.unit_count(UnitTypes::Terran_Medic) >= 2);
		bool firebats = (opponent_model.enemy_race() != Races::Terran);
		update_barracks_train_distribution(academy && enough_medics && firebats, false, academy && !enough_medics);
	}
	if (Broodwar->self()->hasResearched(TechTypes::Stim_Packs) &&
		training_manager.unit_count_completed(UnitTypes::Terran_Medic) >= 2) {
		if (training_manager.unit_count_loaded(UnitTypes::Terran_Marine) > 0) {
			micro_manager.unload_bunkers();
		} else {
			attacking_ = true;
			building_placement_manager.clear_specific_positions();
			mode_ = followup_mode;
			return;
		}
	}
}

void TerranStrategy::opening_TvX_2factvults()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::MainMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax) {
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Machine_Shop);
		mode_ = Mode::MainMech;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::MainMech;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		training_manager.unit_count(UnitTypes::Terran_Marine) < 1) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
		}
	}
	if (supply >= 18) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 2);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, building_manager.building_count(UnitTypes::Terran_Factory));
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Machine_Shop) >= building_manager.building_count(UnitTypes::Terran_Factory)) {
			training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
		}
	}
	if (building_manager.building_count(UnitTypes::Terran_Machine_Shop) >= 1 && !done_or_in_progress(UpgradeTypes::Ion_Thrusters)) {
		building_manager.request_upgrade(UpgradeTypes::Ion_Thrusters);
	}
	if (building_manager.building_count(UnitTypes::Terran_Machine_Shop) >= 2 && !done_or_in_progress(TechTypes::Spider_Mines)) {
		building_manager.request_research(TechTypes::Spider_Mines);
	}
	if (supply >= 23) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
	}
	if (supply >= 28) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 4);
	}
	if (building_manager.building_count(UnitTypes::Terran_Factory) >= 2 &&
		training_manager.unit_count_completed(UnitTypes::Terran_Vulture) >= 6) {
		attacking_ = true;
		mode_ = Mode::MainMech;
	}
}

void TerranStrategy::opening_TvX_2portwraith()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool && !building_placement_manager.has_active_wall()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendFastPool;
		return;
	}
	if ((is_enemy_offense_larger_than_defense() && !building_placement_manager.has_active_wall()) ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
	if (!opening_wall_positioned_ && opponent_model.enemy_race() == Races::Zerg) {
		opening_wall_positioned_successfully_ = building_placement_manager.calculate_tvz_wall_position();
		opening_wall_positioned_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
	}
	if (supply >= 22) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Starport, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Starport) >= 2) {
			wraith_strategy_ = true;
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 3);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Starport) < 2 ||
			building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) < 3) {
			training_manager.set_worker_production(false);
		}
	}
	if (supply >= 30) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 4);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		training_manager.unit_count(UnitTypes::Terran_Marine) < 4) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
		training_manager.unit_count(UnitTypes::Terran_Vulture) < 1) {
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Starport)) {
		training_manager.starport_train_distribution().set(UnitTypes::Terran_Wraith, 1.0);
	}
	if (building_manager.building_count(UnitTypes::Terran_Starport) >= 2 &&
		building_manager.building_count(UnitTypes::Terran_Supply_Depot) >= 4 &&
		training_manager.unit_count_completed(UnitTypes::Terran_Wraith) >= 2) {
		attacking_ = true;
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::MainMech;
		return;
	}
}

void TerranStrategy::opening_TvX_8raxmech()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (opening_proxy_location_ == TilePositions::Unknown) {
		opening_proxy_location_ = building_placement_manager.calculate_proxy_barracks_position();
	}
	int supply = opening_supply_count();
	if (opening_proxy_location_.isValid() &&
		building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) < 1) {
		worker_manager.send_proxy_builder(center_position_for(UnitTypes::Terran_Barracks, opening_proxy_location_), 3 * UnitTypes::Terran_SCV.buildTime());
	}
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	}
	if (supply >= 9) {
		training_manager.set_worker_production(false);
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 1) {
			training_manager.set_worker_production(true);
		}
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		worker_manager.send_initial_scout();
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		building_manager.set_automatic_supply(true);
	}
	worker_manager.set_limit_refinery_workers(2);
	if (building_manager.building_exists(UnitTypes::Terran_Refinery)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Terran_Vulture) < 1) {
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
	}
	if (training_manager.unit_count_built_or_in_progress(UnitTypes::Terran_Vulture) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Machine_Shop) >= 1) {
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::MainMech;
			return;
		}
	}
	
	if (!opening_attack_started_ &&
		training_manager.unit_count(UnitTypes::Terran_Marine) >= 2) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) {
		attack_check_condition();
	}
}

void TerranStrategy::opening_TvX_bbs()
{
	worker_manager.set_scout_wait_at_natural(false);
	int supply = opening_supply_count();
	if (supply >= 8) {
		if (Broodwar->self()->minerals() >= 2 * UnitTypes::Terran_Barracks.mineralPrice()) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 2);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) < 2) {
			training_manager.set_worker_production(false);
		}
	}
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 14) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 2) {
		building_manager.set_automatic_supply(true);
	}
	if (training_manager.unit_count(UnitTypes::Terran_SCV) >= 11) {
		training_manager.set_worker_production(false);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
		additional_barracks();
	}
	if (building_manager.building_count(UnitTypes::Terran_Barracks) >= 2) {
		worker_manager.send_initial_scout();
		if (base_state.start_base_count() >= 4) worker_manager.send_second_scout();
	}
	if (information_manager.enemy_building_seen() ||
		tactics_manager.enemy_start_base() != nullptr ||
		tactics_manager.possible_enemy_start_bases().size() <= 1) {
		worker_manager.stop_scouting();
	}
	if (!opening_attack_started_ &&
		training_manager.unit_count_completed(UnitTypes::Terran_Marine) >= 2) {
		opening_attack_started_ = true;
		attacking_ = true;
	}
	if (base_state.mineable_mineral_count() < 1000) {
		building_manager.request_next_base();
	}
}

void TerranStrategy::opening_TvX_proxybbs()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (opening_proxy_location_ == TilePositions::Unknown) {
		opening_proxy_location_ = building_placement_manager.calculate_proxy_barracks_position();
	}
	if (opening_second_proxy_location_ == TilePositions::Unknown) {
		opening_second_proxy_location_ = building_placement_manager.calculate_proxy_barracks_position();
	}
	if (opening_proxy_location_.isValid() &&
		building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) < 2) {
		worker_manager.send_proxy_builder(center_position_for(UnitTypes::Terran_Barracks, opening_proxy_location_), 5 * UnitTypes::Terran_SCV.buildTime());
		worker_manager.send_proxy_builder(center_position_for(UnitTypes::Terran_Barracks, opening_second_proxy_location_), 5 * UnitTypes::Terran_SCV.buildTime());
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		if (Broodwar->self()->minerals() >= 2 * UnitTypes::Terran_Barracks.mineralPrice()) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 2);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) < 2) {
			training_manager.set_worker_production(false);
		}
	}
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 14) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 2);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Supply_Depot) >= 2) {
		building_manager.set_automatic_supply(true);
	}
	if (training_manager.unit_count(UnitTypes::Terran_SCV) >= 11) {
		training_manager.set_worker_production(false);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
		additional_barracks();
	}
	if (building_manager.building_count(UnitTypes::Terran_Barracks) >= 2) {
		worker_manager.send_initial_scout();
		if (base_state.start_base_count() >= 4) worker_manager.send_second_scout();
	}
	if (information_manager.enemy_building_seen() ||
		tactics_manager.enemy_start_base() != nullptr ||
		tactics_manager.possible_enemy_start_bases().size() <= 1) {
		worker_manager.stop_scouting();
	}
	if (!opening_attack_started_ &&
		training_manager.unit_count_completed(UnitTypes::Terran_Marine) >= 2) {
		opening_attack_started_ = true;
		attacking_ = true;
		building_placement_manager.clear_specific_positions();
	}
	if (base_state.mineable_mineral_count() < 1000) {
		building_manager.request_next_base();
	}
}

void TerranStrategy::mode_opening()
{
	building_manager.set_automatic_supply(false);
	training_manager.set_worker_cut(true);
	training_manager.set_prioritize_training(false);
	training_manager.barracks_train_distribution().clear();
	training_manager.factory_train_distribution().clear();
	training_manager.starport_train_distribution().clear();
	if (opening_ == kTvZ_Fantasy) opening_TvZ_fantasy();
	else if (opening_ == kTvZ_Sparks) opening_TvZ_sparks();
	else if (opening_ == kTvZ_Ayumi) opening_TvZ_ayumi();
	else if (opening_ == kTvZ_1RaxFE) opening_TvZ_1raxfe();
	else if (opening_ == kTvZ_2Rax) opening_TvZ_2rax();
	else if (opening_ == kTvZ_14CC) opening_TvZ_14cc();
	else if (opening_ == kTvZ_5FactGoliath) opening_TvZ_5factgoliath();
	else if (opening_ == kTvZ_2PortWraith) opening_TvX_2portwraith();
	else if (opening_ == kTvZ_8RaxMech) opening_TvX_8raxmech();
	else if (opening_ == kTvZ_BBS) opening_TvX_bbs();
	else if (opening_ == kTvZ_ProxyBBS) opening_TvX_proxybbs();
	else if (opening_ == kTvT_2FactVults) opening_TvX_2factvults();
	else if (opening_ == kTvT_3FactVults) opening_TvT_3factvults();
	else if (opening_ == kTvT_1FactFE) opening_TvT_1factfe(false);
	else if (opening_ == kTvT_1FactFEBioMech) opening_TvT_1factfe(true);
	else if (opening_ == kTvT_1RaxFE) opening_TvT_1raxfe();
	else if (opening_ == kTvT_14CC) opening_TvT_14cc();
	else if (opening_ == kTvT_1PortWraith) opening_TvT_1portwraith();
	else if (opening_ == kTvT_2PortWraith) opening_TvX_2portwraith();
	else if (opening_ == kTvT_Proxy5Rax) opening_TvT_proxy5rax();
	else if (opening_ == kTvT_8RaxMech) opening_TvX_8raxmech();
	else if (opening_ == kTvT_BBS) opening_TvX_bbs();
	else if (opening_ == kTvT_ProxyBBS) opening_TvX_proxybbs();
	else if (opening_ == kTvP_2FactVults) opening_TvX_2factvults();
	else if (opening_ == kTvP_GundamRush) opening_TvP_gundamrush();
	else if (opening_ == kTvP_JoyORush) opening_TvP_joyorush();
	else if (opening_ == kTvP_ShallowTwo) opening_TvP_shallowtwo();
	else if (opening_ == kTvP_DeepSix) opening_TvP_deepsix();
	else if (opening_ == kTvP_SiegeExpand) opening_TvP_siegeexpand();
	else if (opening_ == kTvP_1FactFE) opening_TvP_1factfe();
	else if (opening_ == kTvP_1RaxFE) opening_TvP_1raxfe();
	else if (opening_ == KTvP_14CC) opening_TvP_14cc();
	else if (opening_ == kTvP_StrongFD) opening_TvP_strongfd();
	else if (opening_ == kTvP_101010FD) opening_TvP_101010fd();
	else if (opening_ == kTvP_BBS) opening_TvX_bbs();
	else if (opening_ == kTvP_ProxyBBS) opening_TvX_proxybbs();
	else if (opening_ == kTvU_1Fact) opening_TvU_1fact();
	else if (opening_ == kTvU_2Rax) opening_TvU_2rax();
	else if (opening_ == kTvU_BBS) opening_TvX_bbs();
	else if (opening_ == kTvU_ProxyBBS) opening_TvX_proxybbs();
	
	scout_for_cannon_rush_if_needed();
}

void TerranStrategy::mode_main_mech()
{
	main_boilerplate();
	
	if (!building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	} else {
		additional_factories();
		if (training_manager.unit_count(UnitTypes::Terran_Goliath) > 0 && !done_or_in_progress(UpgradeTypes::Charon_Boosters)) {
			building_manager.request_upgrade(UpgradeTypes::Charon_Boosters);
		}
		if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
			opponent_model.enemy_race() == Races::Zerg) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Armory, 1);
		}
		if (need_emergency_detection()) {
			build_emergency_detection();
		} else if (expect_zerg_air_attack() &&
				   building_manager.building_count_including_planned(UnitTypes::Terran_Engineering_Bay) == 0) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 1, true);
		} else if (need_more_anti_air() &&
				   building_manager.building_exists(UnitTypes::Terran_Factory) &&
				   building_manager.building_count_including_planned(UnitTypes::Terran_Armory) == 0) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Armory, 1, true);
		} else {
			if (building_manager.building_count(UnitTypes::Terran_Factory) >= 2) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 1);
			}
			if (building_manager.building_exists(UnitTypes::Terran_Engineering_Bay)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Academy, 1);
			}
			if (building_manager.building_exists(UnitTypes::Terran_Academy)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Comsat_Station, std::min(2, building_manager.building_count(UnitTypes::Terran_Command_Center)));
			}
			if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 3) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Starport, 1);
			}
			if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
				building_manager.building_count_including_planned(UnitTypes::Terran_Starport) >= 1 &&
				building_manager.building_count_including_planned(UnitTypes::Terran_Engineering_Bay) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Armory, 1);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Terran_Armory) >= 1) {
				if (opponent_model.enemy_race() == Races::Zerg) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Control_Tower, 1);
				}
				if (opponent_model.enemy_race() != Races::Zerg ||
					building_manager.building_count_including_planned(UnitTypes::Terran_Control_Tower) >= 1) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Science_Facility, 1);
				}
			}
			
			if (building_manager.building_exists(UnitTypes::Terran_Science_Facility) &&
				!Broodwar->self()->hasResearched(TechTypes::EMP_Shockwave) && want_emp()) {
				building_manager.request_research(TechTypes::EMP_Shockwave);
			}
			
			if (building_manager.building_exists(UnitTypes::Terran_Machine_Shop)) {
				if (!done_or_in_progress(TechTypes::Tank_Siege_Mode)) {
					bool prioritize_siege_mode = (training_manager.prioritize_training() &&
												  tank_count_built_or_in_progress() >= 1);
					building_manager.request_research(TechTypes::Tank_Siege_Mode, prioritize_siege_mode);
				} else if (opponent_model.enemy_race() == Races::Protoss &&
						   !done_or_in_progress(TechTypes::Spider_Mines)) {
					building_manager.request_research(TechTypes::Spider_Mines);
				} else if (!done_or_in_progress(UpgradeTypes::Ion_Thrusters)) {
					building_manager.request_upgrade(UpgradeTypes::Ion_Thrusters);
				} else if (!done_or_in_progress(TechTypes::Spider_Mines)) {
					building_manager.request_research(TechTypes::Spider_Mines);
				}
			}
			
			if (opponent_model.enemy_race() == Races::Terran &&
				building_manager.building_exists(UnitTypes::Terran_Starport) &&
				building_manager.building_exists(UnitTypes::Terran_Science_Facility) &&
				base_state.mining_base_count() >= 4) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Starport, 2);
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Control_Tower, building_manager.building_count(UnitTypes::Terran_Starport));
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Physics_Lab, 1);
				if (building_manager.building_exists(UnitTypes::Terran_Science_Facility) &&
					building_manager.building_exists(UnitTypes::Terran_Physics_Lab)) {
					if (!done_or_in_progress(TechTypes::Yamato_Gun)) {
						building_manager.request_research(TechTypes::Yamato_Gun);
					} else if (!done_or_in_progress(UpgradeTypes::Colossus_Reactor)) {
						building_manager.request_upgrade(UpgradeTypes::Colossus_Reactor);
					}
				}
			}
			
			if (wraith_strategy_ &&
				building_manager.building_exists(UnitTypes::Terran_Starport) &&
				training_manager.unit_count(UnitTypes::Terran_Wraith) >= wraiths_needed_for_cloak()) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Control_Tower, 1);
				if (building_manager.building_exists(UnitTypes::Terran_Control_Tower)) {
					building_manager.request_research(TechTypes::Cloaking_Field);
				}
			}
			
			if (building_manager.building_exists(UnitTypes::Terran_Armory)) {
                bool science_facility = building_manager.building_exists(UnitTypes::Terran_Science_Facility);
				MineralGas income = spending_manager.income_per_minute();
				if (base_state.mining_base_count() >= 3 &&
                    science_facility) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Armory, 2);
				}
				bool vehicle_weapons_max = Broodwar->self()->getUpgradeLevel(UpgradeTypes::Terran_Vehicle_Weapons) == (science_facility ? UpgradeTypes::Terran_Vehicle_Weapons.maxRepeats() : 1);
				bool vehicle_plating_max = Broodwar->self()->getUpgradeLevel(UpgradeTypes::Terran_Vehicle_Plating) == (science_facility ? UpgradeTypes::Terran_Vehicle_Plating.maxRepeats() : 1);
				bool dual_amory = (building_manager.building_count(UnitTypes::Terran_Armory) >= 2);
				if (!vehicle_weapons_max) building_manager.request_upgrade(UpgradeTypes::Terran_Vehicle_Weapons);
				if (!vehicle_plating_max && (vehicle_weapons_max || dual_amory)) building_manager.request_upgrade(UpgradeTypes::Terran_Vehicle_Plating);
			}
		}
		
		bool extra_turrets = expect_zerg_air_attack();
		if (building_manager.building_exists(UnitTypes::Terran_Engineering_Bay)) {
			for (auto& base : base_state.controlled_bases()) {
				int count;
				if (extra_turrets) {
					count = (base == base_state.start_base()) ? 3 : 2;
				} else {
					count = 1;
				}
				building_manager.set_requested_base_defense_turret_count_at_least(base, count);
			}
		}
	}
	
	update_mech_train_distributions();
	attack_check_condition();
	
	if (building_placement_manager.proxy_barracks_position().isValid()) {
		bool order_issued = false;
		if (training_manager.barracks_train_distribution().is_empty()) {
			for (auto& information_unit : information_manager.my_units()) {
				if (information_unit->type == UnitTypes::Terran_Barracks &&
					information_unit->unit->isCompleted() &&
					information_unit->unit->isIdle() &&
					!information_unit->unit->isLifted()) {
					information_unit->unit->lift();
					order_issued = true;
				}
			}
		}
		if (!order_issued) {
			for (auto& information_unit : information_manager.my_units()) {
				if (information_unit->type == UnitTypes::Terran_Barracks &&
					information_unit->unit->isCompleted() &&
					information_unit->unit->isLifted()) {
					building_placement_manager.clear_proxy_barracks_position();
				}
			}
		}
	} else {
		if (allow_open_wall() &&
			training_manager.barracks_train_distribution().is_empty()) {
			for (auto& information_unit : information_manager.my_units()) {
				if (information_unit->type == UnitTypes::Terran_Barracks &&
					information_unit->unit->isCompleted() &&
					information_unit->unit->isIdle() &&
					!information_unit->unit->isLifted()) {
					information_unit->unit->lift();
				}
			}
		}
	}
	for (auto& information_unit : information_manager.my_units()) {
		if (information_unit->type == UnitTypes::Terran_Engineering_Bay &&
			information_unit->unit->isCompleted() &&
			information_unit->unit->isIdle() &&
			!information_unit->unit->isLifted()) {
			information_unit->unit->lift();
		}
	}
	micro_manager.set_spot_with_barracks_and_engineering_bay(true);
	auto& terran_wall_position = building_placement_manager.terran_wall_position();
	if (terran_wall_position && !terran_wall_position->machine_shop_placable) {
		for (auto& information_unit : information_manager.my_units()) {
			if (information_unit->type == UnitTypes::Terran_Factory &&
				information_unit->tile_position() == terran_wall_position->factory_position &&
				information_unit->unit->isCompleted() &&
				information_unit->unit->isIdle() &&
				!information_unit->unit->isLifted()) {
				information_unit->unit->lift();
				training_manager.factory_train_distribution().clear();
			}
		}
	}
	
	if (opening_attack_with_workers_ &&
		!attacking_) {
		worker_manager.stop_combat();
		opening_attack_with_workers_ = false;
	}
	if (!cannon_rush_handled_ && opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) mode_ = Mode::DefendCannonRush;
}

void TerranStrategy::mode_main_bio(bool biomech)
{
	main_boilerplate();
	
	if (!building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	} else {
		additional_barracks();
		if (building_manager.building_count(UnitTypes::Terran_Barracks) >= 2 &&
			building_manager.building_exists(UnitTypes::Terran_Refinery)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Academy, 1);
		}
		if (building_manager.building_exists(UnitTypes::Terran_Academy)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Comsat_Station, std::min(2, building_manager.building_count(UnitTypes::Terran_Command_Center)));
		}
		if (need_emergency_detection()) {
			build_emergency_detection();
		} else if (expect_zerg_air_attack() &&
				   building_manager.building_count_including_planned(UnitTypes::Terran_Engineering_Bay) == 0) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 1, true);
		} else {
			if (building_manager.building_exists(UnitTypes::Terran_Academy)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 1);
			}
			if (building_manager.building_exists(UnitTypes::Terran_Academy)) {
				if (!done_or_in_progress(TechTypes::Stim_Packs)) {
					building_manager.request_research(TechTypes::Stim_Packs);
				} else if (!done_or_in_progress(UpgradeTypes::U_238_Shells)) {
					building_manager.request_upgrade(UpgradeTypes::U_238_Shells);
				}
			}
			if (building_manager.building_exists(UnitTypes::Terran_Engineering_Bay) &&
				done_or_in_progress(TechTypes::Stim_Packs) &&
				done_or_in_progress(UpgradeTypes::U_238_Shells) &&
				building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 3) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
			}
			if (building_manager.building_exists(UnitTypes::Terran_Comsat_Station) &&
				building_manager.building_count(UnitTypes::Terran_Factory) >= (biomech ? 2 : 1)) {
				int starport_count = 1;
				if (done_or_in_progress(TechTypes::Irradiate)) {
					starport_count = 2;
				}
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Starport, starport_count);
			}
			if (building_manager.building_exists(UnitTypes::Terran_Starport)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Control_Tower, 1);
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Science_Facility, 1);
			}
			if (building_manager.building_exists(UnitTypes::Terran_Science_Facility) &&
				training_manager.unit_count(UnitTypes::Terran_Science_Vessel) >= 1) {
				if (!Broodwar->self()->hasResearched(TechTypes::Irradiate)) {
					if (want_irradiate()) {
						building_manager.request_research(TechTypes::Irradiate);
					}
				} else if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Titan_Reactor) < 1) {
					building_manager.request_upgrade(UpgradeTypes::Titan_Reactor);
				}
			}
			
			if (biomech) {
				if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
				}
				if (building_manager.building_count(UnitTypes::Terran_Barracks) >= 4) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 2);
				}
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, building_manager.building_count(UnitTypes::Terran_Factory));
				if (building_manager.building_exists(UnitTypes::Terran_Machine_Shop)) {
					bool prioritize_siege_mode = (training_manager.prioritize_training() &&
												  tank_count_built_or_in_progress() >= 1);
					building_manager.request_research(TechTypes::Tank_Siege_Mode, prioritize_siege_mode);
				}
			}
			
			if (opponent_model.enemy_race() == Races::Terran &&
				building_manager.building_exists(UnitTypes::Terran_Control_Tower) &&
				training_manager.unit_count(UnitTypes::Terran_Wraith) >= wraiths_needed_for_cloak()) {
				building_manager.request_research(TechTypes::Cloaking_Field);
			}
			
			if (building_manager.building_exists(UnitTypes::Terran_Science_Facility) &&
				opponent_model.enemy_race() == Races::Protoss &&
				count_lockdown_targets() > 0) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Covert_Ops, 1);
				if (building_manager.building_exists(UnitTypes::Terran_Covert_Ops)) {
					building_manager.request_research(TechTypes::Lockdown);
				}
			}
			
			if (building_manager.building_exists(UnitTypes::Terran_Science_Facility) &&
				(information_manager.enemy_exists(UnitTypes::Zerg_Hive) ||
				 information_manager.enemy_exists(UnitTypes::Zerg_Greater_Spire) ||
				 information_manager.enemy_exists(UnitTypes::Zerg_Defiler_Mound) ||
				 information_manager.enemy_exists(UnitTypes::Zerg_Ultralisk_Cavern) ||
				 information_manager.enemy_exists(UnitTypes::Zerg_Guardian) ||
				 information_manager.enemy_exists(UnitTypes::Zerg_Devourer) ||
				 information_manager.enemy_exists(UnitTypes::Zerg_Defiler) ||
				 information_manager.enemy_has_upgrade(UpgradeTypes::Adrenal_Glands))) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Physics_Lab, 1);
				if (building_manager.building_exists(UnitTypes::Terran_Science_Facility) &&
					building_manager.building_exists(UnitTypes::Terran_Physics_Lab)) {
					if (!done_or_in_progress(TechTypes::Yamato_Gun)) {
						building_manager.request_research(TechTypes::Yamato_Gun);
					} else if (!done_or_in_progress(UpgradeTypes::Colossus_Reactor)) {
						building_manager.request_upgrade(UpgradeTypes::Colossus_Reactor);
					}
				}
			}
			
			if (building_manager.building_exists(UnitTypes::Terran_Engineering_Bay) &&
				done_or_in_progress(TechTypes::Stim_Packs) &&
				done_or_in_progress(UpgradeTypes::U_238_Shells)) {
                bool science_facility = building_manager.building_exists(UnitTypes::Terran_Science_Facility);
				MineralGas income = spending_manager.income_per_minute();
				if (science_facility &&
                    base_state.mining_base_count() >= 3) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 2);
				}
				bool infantry_weapons_max = Broodwar->self()->getUpgradeLevel(UpgradeTypes::Terran_Infantry_Weapons) == (science_facility ? UpgradeTypes::Terran_Infantry_Weapons.maxRepeats() : 1);
				bool infantry_armor_max = Broodwar->self()->getUpgradeLevel(UpgradeTypes::Terran_Infantry_Armor) == (science_facility ? UpgradeTypes::Terran_Infantry_Armor.maxRepeats() : 1);
				bool dual_engineering_bay = (building_manager.building_count(UnitTypes::Terran_Engineering_Bay) >= 2);
				if (!infantry_weapons_max) building_manager.request_upgrade(UpgradeTypes::Terran_Infantry_Weapons);
				if (!infantry_armor_max && (infantry_weapons_max || dual_engineering_bay)) building_manager.request_upgrade(UpgradeTypes::Terran_Infantry_Armor);
			}
		}
		
		bool extra_turrets = expect_zerg_air_attack();
		if (building_manager.building_exists(UnitTypes::Terran_Engineering_Bay)) {
			for (auto& base : base_state.controlled_bases()) {
				int count = extra_turrets ? 2 : 1;
				if (base == base_state.start_base()) {
					count = 3;
				} else if (base == base_state.natural_base()) {
					count = 2;
				}
				building_manager.set_requested_base_defense_turret_count_at_least(base, count);
			}
		}
	}
	
	update_bio_train_distribution(biomech);
	attack_check_condition();
	if (!biomech &&
		training_manager.unit_count_built(UnitTypes::Terran_Medic) < 2 &&
		base_state.controlled_geyser_count() > 0) {
		attacking_ = false;
	}
	
	if (allow_open_wall()) {
		TilePosition wall_barracks_position = TilePositions::None;
		auto& terran_wall_position = building_placement_manager.terran_wall_position();
		if (terran_wall_position) {
			wall_barracks_position = terran_wall_position->barracks_position;
		} else {
			auto& terran_natural_wall_position = building_placement_manager.terran_natural_wall_position();
			if (terran_natural_wall_position) {
				wall_barracks_position = terran_natural_wall_position->barracks_position;
			}
		}
		if (wall_barracks_position.isValid()) {
			for (auto& information_unit : information_manager.my_units()) {
				if (information_unit->type == UnitTypes::Terran_Barracks &&
					information_unit->tile_position() == wall_barracks_position &&
					information_unit->unit->isCompleted() &&
					information_unit->unit->isIdle() &&
					!information_unit->unit->isLifted()) {
					information_unit->unit->lift();
					training_manager.barracks_train_distribution().clear();
				}
			}
		}
	}
	
	if (attacking_ &&
		micro_manager.is_load_bunkers()) {
		micro_manager.unload_bunkers();
	}
	if (!cannon_rush_handled_ && opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) mode_ = Mode::DefendCannonRush;
}

void TerranStrategy::mode_defend_fast_pool()
{
	if (!fast_pool_handled_) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Terran_Command_Center, 1, 1);
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Terran_Barracks, 1, 1);
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Factory);
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Bunker);
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Refinery);
		opening_bunker_location_ = TilePositions::Unknown;
		fast_pool_handled_ = true;
		attacking_ = false;
		worker_manager.combat(training_manager.unit_count_completed(UnitTypes::Terran_SCV) - 8);
	}
	
	if (opening_bunker_location_ == TilePositions::Unknown) {
		opening_bunker_location_ = building_placement_manager.calculate_bunker_near_mineral_line_position();
	}
	
	int supply = (Broodwar->self()->supplyUsed() + 1) / 2;
	if (supply >= 9) building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	if (supply >= 11) building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
	if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		training_manager.barracks_train_distribution().clear();
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
		if (training_manager.unit_count(UnitTypes::Terran_Marine) == 0 &&
			training_manager.unit_count(UnitTypes::Terran_SCV) > 0) {
			training_manager.set_prioritize_training(true);
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Bunker, 1);
		building_manager.set_automatic_supply(true);
		if (building_manager.building_exists(UnitTypes::Terran_Bunker) &&
			training_manager.unit_count_loaded(UnitTypes::Terran_Marine) >= 1) {
			additional_barracks();
		}
	}
	if (training_manager.unit_count(UnitTypes::Terran_Marine) >= 8) {
		mode_ = Mode::MainBio;
		return;
	}
	
	if (training_manager.unit_count_loaded(UnitTypes::Terran_Marine) >= 1) {
		worker_manager.stop_combat();
	}
	
	TilePosition wall_barracks_position = TilePositions::None;
	auto& terran_wall_position = building_placement_manager.terran_wall_position();
	if (terran_wall_position) {
		wall_barracks_position = terran_wall_position->barracks_position;
	} else {
		auto& terran_natural_wall_position = building_placement_manager.terran_natural_wall_position();
		if (terran_natural_wall_position) {
			wall_barracks_position = terran_natural_wall_position->barracks_position;
		}
	}
	if (wall_barracks_position.isValid()) {
		for (auto& information_unit : information_manager.my_units()) {
			if (information_unit->type == UnitTypes::Terran_Barracks &&
				information_unit->tile_position() == wall_barracks_position &&
				information_unit->unit->isCompleted() &&
				information_unit->unit->isIdle() &&
				!information_unit->unit->isLifted()) {
				information_unit->unit->lift();
				training_manager.barracks_train_distribution().clear();
				if (opening_barracks_location_ == TilePositions::Unknown &&
					opening_bunker_location_.isValid()) {
					opening_barracks_location_ = building_placement_manager.calculate_barracks_near_bunker_position(opening_bunker_location_, wall_barracks_position);
				}
			}
		}
	}
}

void TerranStrategy::mode_defend_cannon_rush()
{
	bool cannons_or_pylons_in_base = false;
	int cannons_near_base = 0;
	for (auto enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->type == UnitTypes::Protoss_Photon_Cannon &&
			enemy_unit->base_distance <= 768) {
			cannons_near_base++;
		}
		if ((enemy_unit->type == UnitTypes::Protoss_Pylon || enemy_unit->type == UnitTypes::Protoss_Photon_Cannon) &&
			enemy_unit->base_distance == 0) {
			cannons_or_pylons_in_base = true;
		}
	}
	cannons_or_pylons_in_base_seen_ |= cannons_or_pylons_in_base;
	bool combat_unit_not_near_base = false;
	for (auto information_unit : information_manager.my_units()) {
		if (information_unit->base_distance > 768 &&
			(information_unit->type == UnitTypes::Terran_Marine || is_siege_tank(information_unit->type))) {
			combat_unit_not_near_base = true;
			break;
		}
	}
	if (!cannon_rush_handled_) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Terran_Command_Center, 1, 1);
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Terran_Barracks, 1, 1);
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Bunker);
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Academy);
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Engineering_Bay);
		building_manager.cancel_buildings_of_type(UnitTypes::Terran_Starport);
		building_placement_manager.clear_specific_positions();
		worker_manager.stop_scouting();
		attacking_ = false;
		opening_attack_started_ = false;
		cannon_rush_handled_ = true;
	}
	training_manager.barracks_train_distribution().clear();
	training_manager.factory_train_distribution().clear();
	training_manager.starport_train_distribution().clear();
	training_manager.set_prioritize_training(false);
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Supply_Depot, 1);
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Barracks) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
		}
	}
	if ((cannons_or_pylons_in_base ||
		 (cannons_or_pylons_in_base_seen_ &&
		  training_manager.unit_count(UnitTypes::Terran_Marine) == 0)) &&
		training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) == 0 &&
		training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Siege_Mode) == 0) {
		if (building_manager.building_exists(UnitTypes::Terran_Barracks)) {
			if (training_manager.unit_count(UnitTypes::Terran_Marine) < 4) {
				training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
				training_manager.set_prioritize_training(true);
			}
			building_manager.set_automatic_supply(true);
		}
	}
	int tank_count = training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) + training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Siege_Mode);
	if (building_manager.building_exists(UnitTypes::Terran_Factory) && tank_count >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 1);
		building_manager.set_requested_base_defense_turret_count_at_least(base_state.start_base(), 2);
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Missile_Turret) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Starport, 1);
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Control_Tower, 1);
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Science_Facility, 1);
			if (building_manager.building_exists(UnitTypes::Terran_Starport) &&
				building_manager.building_exists(UnitTypes::Terran_Control_Tower) &&
				building_manager.building_exists(UnitTypes::Terran_Science_Facility)) {
				if (training_manager.unit_count(UnitTypes::Terran_Science_Vessel) < 1) {
					training_manager.starport_train_distribution().set(UnitTypes::Terran_Science_Vessel, 1.0);
				} else {
					building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Armory, 1);
				}
			}
		}
	}
	if (is_gas_stolen()) {
		if (training_manager.unit_count(UnitTypes::Terran_Marine) < 2) {
			training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
		}
	} else if (building_manager.building_exists(UnitTypes::Terran_Barracks) &&
			   building_manager.building_exists(UnitTypes::Terran_Refinery)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1);
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, building_manager.building_count(UnitTypes::Terran_Factory));
		if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
			building_manager.building_exists(UnitTypes::Terran_Machine_Shop)) {
			if (tank_count < 2) {
				training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, 1.0);
			} else {
				update_factory_train_distribution(true, false);
			}
			building_manager.request_research(TechTypes::Tank_Siege_Mode);
		}
		additional_factories();
	}
	for (auto& information_unit : information_manager.my_units()) {
		if (information_unit->type == UnitTypes::Terran_Engineering_Bay &&
			information_unit->unit->isCompleted() &&
			information_unit->unit->isIdle() &&
			!information_unit->unit->isLifted()) {
			information_unit->unit->lift();
		}
	}
	micro_manager.set_spot_with_barracks_and_engineering_bay(true);
	if ((training_manager.unit_count_completed(UnitTypes::Terran_Siege_Tank_Tank_Mode) >= 1 ||
		 training_manager.unit_count_completed(UnitTypes::Terran_Siege_Tank_Siege_Mode) >= 1) &&
		Broodwar->self()->hasResearched(TechTypes::Tank_Siege_Mode)) {
		attacking_ = true;
	} else {
		attacking_ = false;
	}
	for (auto information_unit : information_manager.my_units()) {
		if ((information_unit->type == UnitTypes::Terran_Barracks || information_unit->type == UnitTypes::Terran_Supply_Depot) &&
			information_unit->base_distance > 0 &&
			!information_unit->unit->isCompleted()) {
			for (auto enemy_unit : information_manager.enemy_units()) {
				if (enemy_unit->type == UnitTypes::Protoss_Photon_Cannon &&
					can_attack_in_range_at_positions(UnitTypes::Protoss_Photon_Cannon, enemy_unit->position, enemy_unit->player, information_unit->type, information_unit->position)) {
					building_manager.cancel_building(information_unit->unit);
					break;
				}
			}
		}
	}
	if (cannons_near_base == 0 &&
		attacking_ &&
		combat_unit_not_near_base) {
		if (worker_manager.average_workers_per_mineral() >= 2.0 &&
			!base_state.next_available_bases().empty() &&
			!dark_templars_without_mobile_detection()) {
			building_manager.request_next_base();
		}
		if (Broodwar->self()->minerals() + base_state.mineable_mineral_count() < 1500) {
			building_manager.request_next_base(true);
		}
		if (training_manager.unit_count(UnitTypes::Terran_Science_Vessel) >= 1 &&
			building_manager.building_count_including_planned(UnitTypes::Terran_Armory) >= 1) {
			building_placement_manager.clear_specific_positions();
			attack_minimum_ = 2 * 8;
			mode_ = Mode::MainMech;
		}
	}
	if (allow_open_wall()) {
		TilePosition wall_barracks_position = TilePositions::None;
		auto& terran_wall_position = building_placement_manager.terran_wall_position();
		if (terran_wall_position) {
			wall_barracks_position = terran_wall_position->barracks_position;
		} else {
			auto& terran_natural_wall_position = building_placement_manager.terran_natural_wall_position();
			if (terran_natural_wall_position) {
				wall_barracks_position = terran_natural_wall_position->barracks_position;
			}
		}
		if (wall_barracks_position.isValid()) {
			for (auto& information_unit : information_manager.my_units()) {
				if (information_unit->type == UnitTypes::Terran_Barracks &&
					information_unit->tile_position() == wall_barracks_position &&
					information_unit->unit->isCompleted() &&
					information_unit->unit->isIdle() &&
					!information_unit->unit->isLifted()) {
					information_unit->unit->lift();
					training_manager.barracks_train_distribution().clear();
				}
			}
		}
	}
}

void TerranStrategy::main_boilerplate()
{
	bool defending_rush = is_defending_rush();
	bool more_anti_air = need_more_anti_air();
	bool contained = is_contained();
	training_manager.set_prioritize_training(defending_rush || contained || more_anti_air || tactics_manager.is_opponent_army_too_large());
	bool do_not_expand = defending_rush || more_anti_air || contained || dark_templars_without_mobile_detection();
	
	if (building_manager.building_count(UnitTypes::Terran_Command_Center) >= 1 && building_manager.building_exists(UnitTypes::Terran_Refinery)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, base_state.controlled_geyser_count());
	}
	if (building_manager.building_exists(UnitTypes::Terran_Command_Center) &&
		building_manager.building_exists(UnitTypes::Terran_Barracks) &&
		base_state.controlled_geyser_count() >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Refinery, 1);
	}
	
	if (worker_manager.average_workers_per_mineral() >= 2.0 && !base_state.next_available_bases().empty() && !do_not_expand) {
		building_manager.request_next_base();
	}
	if (Broodwar->self()->minerals() + base_state.mineable_mineral_count() < 1500) {
		building_manager.request_next_base(true);
	}
	if (wraith_strategy_ &&
		information_manager.enemy_count(UnitTypes::Zerg_Hydralisk) >= 8) {
		wraith_strategy_ = false;
	}
	attack_with_vultures_ = true;
	if (!gas_steal_handled_ &&
		(base_state.controlled_and_planned_bases().size() >= 2 ||
		 (is_gas_stolen() &&
		  !building_manager.request_next_base()))) {
		gas_steal_handled_ = true;
	}
}

bool TerranStrategy::need_emergency_detection()
{
	bool cloaked_attack_expected = (opponent_model.cloaked_present() ||
									expect_lurkers() ||
									expect_dark_templars() ||
									information_manager.enemy_exists(UnitTypes::Terran_Control_Tower) ||
									opponent_model.blocked_expansion_seen());
	bool detection_present;
	if (opponent_model.enemy_race() == Races::Terran) {
		detection_present = (building_manager.building_count_including_planned(UnitTypes::Terran_Comsat_Station) >= 1);
	} else {
		detection_present = (training_manager.unit_count_built(UnitTypes::Terran_Science_Vessel) >= 1);
	}
	return cloaked_attack_expected && !detection_present;
}

void TerranStrategy::build_emergency_detection()
{
	if (building_manager.building_count_including_planned(UnitTypes::Terran_Missile_Turret) == 0) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Engineering_Bay, 1, true);
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Missile_Turret, 1, true);
	} else {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Academy, 1, true);
		if (building_manager.building_exists(UnitTypes::Terran_Academy)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Comsat_Station, 1, true);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Terran_Comsat_Station) >= 1) {
			if (need_additional_tanks_to_counter_lurkers() ||
				(need_counter_lurkers_with_tanks() && !done_or_in_progress(TechTypes::Tank_Siege_Mode))) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1, true);
				if (building_manager.building_count_including_planned(UnitTypes::Terran_Factory) >= 1) {
					bool lifted_factory = false;
					auto& terran_wall_position = building_placement_manager.terran_wall_position();
					if (terran_wall_position && !terran_wall_position->machine_shop_placable) {
						for (auto& information_unit : information_manager.my_units()) {
							if (information_unit->type == UnitTypes::Terran_Factory &&
								information_unit->tile_position() == terran_wall_position->factory_position &&
								information_unit->unit->isCompleted() &&
								information_unit->unit->isIdle() &&
								!information_unit->unit->isLifted()) {
								information_unit->unit->lift();
								training_manager.factory_train_distribution().clear();
								lifted_factory = true;
							}
						}
					}
					if (!lifted_factory) {
						building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, 1, true);
					}
				}
				if (building_manager.building_exists(UnitTypes::Terran_Machine_Shop)) {
					building_manager.request_research(TechTypes::Tank_Siege_Mode, true);
				}
			} else if (opponent_model.enemy_race() != Races::Terran) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, 1, true);
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Starport, 1, true);
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Control_Tower, 1, true);
				building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Science_Facility, 1, true);
			}
		}
	}
}

void TerranStrategy::update_bio_train_distribution(bool biomech)
{
	training_manager.factory_train_distribution().clear();
	training_manager.starport_train_distribution().clear();
	if (building_manager.building_exists(UnitTypes::Terran_Factory) &&
		building_manager.building_exists(UnitTypes::Terran_Machine_Shop) &&
		(biomech || need_additional_tanks_to_counter_lurkers())) {
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Terran_Starport)) {
		bool science_facility_available = (building_manager.building_exists(UnitTypes::Terran_Control_Tower) &&
										   building_manager.building_exists(UnitTypes::Terran_Science_Facility));
		bool prioritize_science_vessel = (science_facility_available &&
										  training_manager.unit_count(UnitTypes::Terran_Science_Vessel) == 0 &&
										  opponent_model.enemy_race() != Races::Terran);
		int max_science_vessels = 0;
		if (science_facility_available) {
			if (opponent_model.enemy_race() != Races::Terran) {
				max_science_vessels = 1;
			}
			if (done_or_in_progress(TechTypes::Irradiate)) {
				int count = std::min(8, count_irradiate_targets());
				max_science_vessels = std::max(max_science_vessels, count);
			}
		}
		int max_wraiths = 0;
		if (biomech &&
			opponent_model.enemy_race() == Races::Terran &&
			!prioritize_science_vessel) {
			int enemy_tank_count = information_manager.enemy_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) + information_manager.enemy_count(UnitTypes::Terran_Siege_Tank_Siege_Mode);
			max_wraiths = std::max(max_wraiths, (1 + enemy_tank_count) / 2);
		}
		int max_battlecruiser = 0;
		if (science_facility_available &&
			building_manager.building_exists(UnitTypes::Terran_Physics_Lab) &&
			!prioritize_science_vessel) {
			max_battlecruiser = 2;
		}
		if (training_manager.unit_count(UnitTypes::Terran_Wraith) < max_wraiths) {
			training_manager.starport_train_distribution().set(UnitTypes::Terran_Wraith, 1.0);
		}
		if (training_manager.unit_count(UnitTypes::Terran_Science_Vessel) < max_science_vessels) {
			training_manager.starport_train_distribution().set(UnitTypes::Terran_Science_Vessel, 1.0);
		}
		if (training_manager.unit_count(UnitTypes::Terran_Battlecruiser) < max_battlecruiser) {
			training_manager.starport_train_distribution().set(UnitTypes::Terran_Battlecruiser, 1.0);
		}
	}
	int infantry_count = (training_manager.unit_count(UnitTypes::Terran_Marine) +
						  training_manager.unit_count(UnitTypes::Terran_Firebat));
	int medic_count = training_manager.unit_count(UnitTypes::Terran_Medic);
	int enemy_zergling_count = information_manager.enemy_count(UnitTypes::Zerg_Zergling);
	bool firebat = (enemy_zergling_count >= 8 && training_manager.unit_count(UnitTypes::Terran_Firebat) < enemy_zergling_count / 4);
	bool ghost = building_manager.building_exists(UnitTypes::Terran_Covert_Ops) && (training_manager.unit_count(UnitTypes::Terran_Ghost) < std::min(10, count_lockdown_targets()));
	bool medic = (medic_count < ((infantry_count + 3) / 4));
	if (!building_manager.building_exists(UnitTypes::Terran_Academy)) {
		firebat = false;
		medic = false;
	}
	update_barracks_train_distribution(firebat, ghost, medic);
}

void TerranStrategy::update_barracks_train_distribution(bool firebat,bool ghost,bool medic)
{
	CostPerMinute factory_cpm = training_manager.starport_train_distribution().cost_per_minute(building_manager.building_count(UnitTypes::Terran_Factory));
	CostPerMinute starport_cpm = training_manager.starport_train_distribution().cost_per_minute(building_manager.building_count(UnitTypes::Terran_Starport));
	CostPerMinute remaining_cpm = spending_manager.worker_training_cost_per_minute() + factory_cpm + starport_cpm;
	double minerals = spending_manager.income_per_minute().minerals + Broodwar->self()->minerals() - remaining_cpm.minerals;
	double gas = spending_manager.income_per_minute().gas + Broodwar->self()->gas() - remaining_cpm.gas;
	double ratio = minerals / gas;
	if (std::isfinite(ratio) && ratio > 0.0 && (firebat || ghost || medic)) {
		double b = firebat ? 1.0 : 0.0;
		double c = ghost ? 0.5 : 0.0;
		double d = medic ? 1.0 : 0.0;
		double a = std::max(0.0, b * (ratio * 0.5 - 1.0) + c * (ratio * 0.72 - 0.24) + d * (ratio * 0.4 - 0.8));
		
		training_manager.barracks_train_distribution().clear();
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, a);
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Firebat, b);
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Ghost, c);
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Medic, d);
	} else {
		training_manager.barracks_train_distribution().clear();
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
}

void TerranStrategy::update_mech_train_distributions()
{
	training_manager.barracks_train_distribution().clear();
	training_manager.starport_train_distribution().clear();
	
	if (building_manager.building_exists(UnitTypes::Terran_Starport)) {
		bool prioritize_science_vessel = (opponent_model.enemy_race() != Races::Terran && need_emergency_detection());
		
		if (!prioritize_science_vessel) {
			if (opponent_model.enemy_race() == Races::Terran &&
				building_manager.building_exists(UnitTypes::Terran_Control_Tower) &&
				building_manager.building_exists(UnitTypes::Terran_Physics_Lab)) {
				int wraith_supply = UnitTypes::Terran_Wraith.supplyRequired() * training_manager.unit_count(UnitTypes::Terran_Wraith);
				int battlecruiser_supply = UnitTypes::Terran_Battlecruiser.supplyRequired() * training_manager.unit_count(UnitTypes::Terran_Battlecruiser);
				if (wraith_supply <= battlecruiser_supply) {
					training_manager.starport_train_distribution().set(UnitTypes::Terran_Wraith, 1.0);
				} else {
					training_manager.starport_train_distribution().set(UnitTypes::Terran_Battlecruiser, 1.0);
				}
			} else {
				int max_wraiths = 0;
				if (wraith_strategy_) {
					max_wraiths = std::max(max_wraiths, 12);
				}
				if (opponent_model.enemy_race() == Races::Terran) {
					int enemy_tank_count = information_manager.enemy_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) + information_manager.enemy_count(UnitTypes::Terran_Siege_Tank_Siege_Mode);
					max_wraiths = std::max(max_wraiths, (1 + enemy_tank_count) / 2);
				}
				if (training_manager.unit_count(UnitTypes::Terran_Wraith) < max_wraiths) {
					training_manager.starport_train_distribution().set(UnitTypes::Terran_Wraith, 1.0);
				}
			}
		}
		
		if (building_manager.building_exists(UnitTypes::Terran_Science_Facility) &&
			building_manager.building_exists(UnitTypes::Terran_Control_Tower)) {
			int max_science_vessels = 0;
			if (prioritize_science_vessel) {
				max_science_vessels = 1;
			}
			if (done_or_in_progress(TechTypes::EMP_Shockwave)) {
				int count = std::min(8, count_emp_targets());
				max_science_vessels = std::max(max_science_vessels, count);
			}
			if (training_manager.unit_count(UnitTypes::Terran_Science_Vessel) < max_science_vessels) {
				training_manager.starport_train_distribution().set(UnitTypes::Terran_Science_Vessel, 1.0);
			}
		}
		
		if (!prioritize_science_vessel &&
			building_manager.building_exists(UnitTypes::Terran_Armory) &&
			building_manager.building_exists(UnitTypes::Terran_Control_Tower)) {
			int max_valkyries = 0;
			if (opponent_model.enemy_race() == Races::Zerg) {
				max_valkyries = 1;
				if (information_manager.enemy_exists(UnitTypes::Zerg_Spire) ||
					information_manager.enemy_exists(UnitTypes::Zerg_Greater_Spire) ||
					information_manager.enemy_exists(UnitTypes::Zerg_Mutalisk) ||
					information_manager.enemy_exists(UnitTypes::Zerg_Scourge) ||
					information_manager.enemy_exists(UnitTypes::Zerg_Guardian) ||
					information_manager.enemy_exists(UnitTypes::Zerg_Devourer)) {
					max_valkyries = 3;
				}
			}
			if (training_manager.unit_count(UnitTypes::Terran_Valkyrie) < max_valkyries) {
				training_manager.starport_train_distribution().set(UnitTypes::Terran_Valkyrie, 1.0);
			}
		}
	}
	
	bool vulture_available = building_manager.building_exists(UnitTypes::Terran_Factory);
	bool siege_tank_available = vulture_available && building_manager.building_exists(UnitTypes::Terran_Machine_Shop);
	bool goliath_available = vulture_available && building_manager.building_exists(UnitTypes::Terran_Armory);
	
	bool more_anti_air = need_more_anti_air();
	bool siege_tank;
	bool goliath;
	
	if (opponent_model.enemy_race() != Races::Zerg ||
		(siege_tank_available && need_additional_tanks_to_counter_lurkers())) {
		siege_tank = siege_tank_available && (!more_anti_air || !goliath_available);
		goliath = goliath_available && more_anti_air;
	} else {
		int goliath_count = training_manager.unit_count(UnitTypes::Terran_Goliath);
		bool allow_siege_tank = (tank_count() < goliath_count / 4) && !more_anti_air;
		siege_tank = siege_tank_available && (allow_siege_tank || !goliath_available);
		goliath = goliath_available && (!allow_siege_tank || !siege_tank_available);
	}
	
	update_factory_train_distribution(siege_tank, goliath);
	
	if (training_manager.prioritize_training() &&
		!building_manager.building_exists(UnitTypes::Terran_Factory) &&
		building_manager.building_exists(UnitTypes::Terran_Barracks)) {
		training_manager.barracks_train_distribution().set(UnitTypes::Terran_Marine, 1.0);
	}
}

void TerranStrategy::update_factory_train_distribution(bool siege_tank,bool goliath)
{
	CostPerMinute starport_cpm = training_manager.starport_train_distribution().cost_per_minute(building_manager.building_count(UnitTypes::Terran_Starport));
	CostPerMinute remaining_cpm = spending_manager.worker_training_cost_per_minute() + starport_cpm;
	double minerals = spending_manager.income_per_minute().minerals + Broodwar->self()->minerals() - remaining_cpm.minerals;
	double gas = spending_manager.income_per_minute().gas + Broodwar->self()->gas() - remaining_cpm.gas;
	double ratio = minerals / gas;
	if (std::isfinite(ratio) && ratio > 0.0 && (siege_tank || goliath)) {
		double b = siege_tank ? 1.0 : 0.0;
		double c = goliath ? 0.625 : 0.0;
		double a = std::max(0.0, b * (ratio * 0.8 - 1.2) + c * (ratio * 0.5 - 1.0));
		
		training_manager.factory_train_distribution().clear();
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, a);
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Siege_Tank_Tank_Mode, b);
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Goliath, c);
	} else {
		training_manager.factory_train_distribution().clear();
		training_manager.factory_train_distribution().set(UnitTypes::Terran_Vulture, 1.0);
	}
}

void TerranStrategy::additional_barracks()
{
	int additional_producers = training_manager.barracks_train_distribution().additional_producers();
	int requested_barracks_count = building_manager.building_count(UnitTypes::Terran_Barracks) + additional_producers;
	building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Barracks, requested_barracks_count, training_manager.prioritize_training());
}

void TerranStrategy::additional_factories()
{
	int additional_producers = training_manager.factory_train_distribution().additional_producers();
	int requested_factory_count = building_manager.building_count(UnitTypes::Terran_Factory) + additional_producers;
	building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Factory, requested_factory_count, training_manager.prioritize_training());
	if (opponent_model.enemy_opening() != EnemyOpening::T_ProxyRax ||
		training_manager.unit_count_built_or_in_progress(UnitTypes::Terran_Vulture) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Terran_Machine_Shop, building_manager.building_count(UnitTypes::Terran_Factory), training_manager.prioritize_training());
	}
}

int TerranStrategy::tank_count()
{
	return training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Tank_Mode) + training_manager.unit_count(UnitTypes::Terran_Siege_Tank_Siege_Mode);
}

int TerranStrategy::tank_count_built_or_in_progress()
{
	return training_manager.unit_count_built_or_in_progress(UnitTypes::Terran_Siege_Tank_Tank_Mode) + training_manager.unit_count_built_or_in_progress(UnitTypes::Terran_Siege_Tank_Siege_Mode);
}

bool TerranStrategy::need_counter_lurkers_with_tanks()
{
	return (expect_lurkers() ||
			information_manager.enemy_exists(UnitTypes::Zerg_Lurker) ||
			information_manager.enemy_exists(UnitTypes::Zerg_Lurker_Egg));
}

bool TerranStrategy::need_additional_tanks_to_counter_lurkers()
{
	return ((expect_lurkers() && tank_count_built_or_in_progress() < 3) ||
			((information_manager.enemy_exists(UnitTypes::Zerg_Lurker) ||
			  information_manager.enemy_exists(UnitTypes::Zerg_Lurker_Egg)) && tank_count() < 3));
}

int TerranStrategy::anti_air_supply()
{
	int result = 0;
	for (auto type : { UnitTypes::Terran_Marine, UnitTypes::Terran_Goliath, UnitTypes::Terran_Wraith, UnitTypes::Terran_Valkyrie, UnitTypes::Terran_Battlecruiser } ) {
		if (type == UnitTypes::Terran_Marine && mode_ == Mode::MainMech) continue;
		result += (type.supplyRequired() * training_manager.unit_count(type));
	}
	return result;
}

int TerranStrategy::enemy_air_supply()
{
	int result = 0;
	for (auto type : { UnitTypes::Terran_Wraith, UnitTypes::Terran_Dropship, UnitTypes::Terran_Battlecruiser, UnitTypes::Terran_Valkyrie,
		UnitTypes::Protoss_Shuttle, UnitTypes::Protoss_Scout, UnitTypes::Protoss_Carrier, UnitTypes::Protoss_Arbiter, UnitTypes::Protoss_Corsair,
		UnitTypes::Zerg_Mutalisk, UnitTypes::Zerg_Scourge, UnitTypes::Zerg_Guardian, UnitTypes::Zerg_Devourer } ) {
		result += (type.supplyRequired() * information_manager.enemy_count(type));
	}
	return result;
}

int TerranStrategy::wraiths_needed_for_cloak()
{
	return (opponent_model.enemy_race() == Races::Zerg) ? 6 : 2;
}

bool TerranStrategy::enemy_has_flying_building()
{
	for (auto& information_unit : information_manager.enemy_units()) {
		if (information_unit->type.isBuilding() &&
			information_unit->flying) {
			return true;
		}
	}
	return false;
}

bool TerranStrategy::need_more_anti_air()
{
	int supply = anti_air_supply();
	int enemy_supply = enemy_air_supply();
	return (enemy_supply > 0 && enemy_supply >= supply) || (supply == 0 && enemy_has_flying_building());
}

bool TerranStrategy::want_irradiate()
{
	return (information_manager.enemy_seen(UnitTypes::Zerg_Spire) ||
			information_manager.enemy_seen(UnitTypes::Zerg_Greater_Spire) ||
			information_manager.enemy_seen(UnitTypes::Zerg_Defiler_Mound) ||
			information_manager.enemy_seen(UnitTypes::Zerg_Ultralisk_Cavern) ||
			information_manager.enemy_seen(UnitTypes::Zerg_Hive) ||
			count_irradiate_targets() > 0);
}

int TerranStrategy::count_irradiate_targets()
{
	int result = 0;
	for (auto type : { UnitTypes::Zerg_Devourer, UnitTypes::Zerg_Guardian, UnitTypes::Zerg_Mutalisk,
						UnitTypes::Zerg_Lurker, UnitTypes::Zerg_Defiler, UnitTypes::Zerg_Ultralisk } ) {
			result += information_manager.enemy_count(type);
		}
	return result;
}

bool TerranStrategy::want_emp()
{
	return (information_manager.enemy_seen(UnitTypes::Protoss_Arbiter_Tribunal) ||
			count_emp_targets() > 0);
}

int TerranStrategy::count_emp_targets()
{
	int result = 0;
	for (auto type : { UnitTypes::Protoss_Arbiter, UnitTypes::Protoss_High_Templar, UnitTypes::Protoss_Archon, UnitTypes::Protoss_Dark_Archon } ) {
			result += information_manager.enemy_count(type);
		}
	return result;
}

int TerranStrategy::count_lockdown_targets()
{
	int result = 0;
	for (auto information_unit : information_manager.enemy_units()) {
		if (!information_unit->type.isWorker() &&
			!information_unit->type.isBuilding() &&
			information_unit->type.isMechanical()) {
			result++;
		}
	}
	return result;
}

bool TerranStrategy::expect_zerg_air_attack()
{
	return (information_manager.enemy_exists(UnitTypes::Zerg_Spire) ||
			information_manager.enemy_exists(UnitTypes::Zerg_Greater_Spire) ||
			information_manager.enemy_exists(UnitTypes::Zerg_Scourge) ||
			information_manager.enemy_exists(UnitTypes::Zerg_Mutalisk));
}

bool TerranStrategy::allow_open_wall()
{
	return ((attacking_ ||
			 building_manager.building_count_including_requested(UnitTypes::Terran_Command_Center) > building_manager.building_count_including_planned(UnitTypes::Terran_Command_Center)) &&
			!is_enemy_offense_larger_than_defense() &&
			(!opponent_model.cloaked_present() ||
			 building_manager.building_exists(UnitTypes::Terran_Comsat_Station) ||
			 training_manager.unit_count_completed(UnitTypes::Terran_Science_Vessel) >= 1));
}

void TerranStrategy::frame_inner()
{
	switch (mode_) {
		case Mode::Opening:
			mode_opening();
			break;
		case Mode::MainMech:
			mode_main_mech();
			break;
		case Mode::MainBio:
			mode_main_bio(false);
			break;
		case Mode::MainBioMech:
			mode_main_bio(true);
			break;
		case Mode::DefendFastPool:
			mode_defend_fast_pool();
			break;
		case Mode::DefendCannonRush:
			mode_defend_cannon_rush();
			break;
	}
	
	if (building_manager.building_count_including_requested(UnitTypes::Terran_Comsat_Station) > building_manager.building_count_including_planned(UnitTypes::Terran_Comsat_Station)) {
		training_manager.set_worker_cut(true);
	}
}

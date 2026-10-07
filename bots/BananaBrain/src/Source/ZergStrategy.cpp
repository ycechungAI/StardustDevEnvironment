#include "BananaBrain.h"

void ZergStrategy::pick_strategy(bool is_1v1)
{
	if (!is_1v1) {
		opening_ = kZvU_9PoolSpeed;
		return;
	}
	
	Race race = opponent_model.enemy_race();
	switch (race) {
		case Races::Zerg:
			if (!configuration.ZvZ_opening().empty()) {
				opening_ = configuration.ZvZ_opening();
			} else {
				opening_ = result_store.pick_strategy({kZvZ_4Pool,kZvZ_5Pool,kZvZ_2HatchLing,kZvZ_3HatchLing,kZvZ_9HatchLing,kZvZ_10HatchLing,kZvZ_9PoolSpire,kZvZ_9Gas9Pool,kZvZ_9Gas10Pool,kZvZ_11Gas10Pool,kZvZ_OverPool9Gas,kZvZ_12Pool,KZvZ_12PoolMain});
			}
			break;
		case Races::Terran:
			if (!configuration.ZvT_opening().empty()) {
				opening_ = configuration.ZvT_opening();
			} else {
				opening_ = result_store.pick_strategy({kZvT_4Pool,kZvT_5Pool,kZvT_7Pool,kZvT_2HatchLing,kZvT_3HatchLing,kZvT_9HatchLing,kZvT_2HatchMuta_12Hatch,kZvT_2HatchMuta_12Pool,kZvT_2_5HatchMuta,kZvT_3HatchMuta,kZvT_13PoolMuta,kZvT_MutaHydra_12Hatch,kZvT_MutaHydra_9Pool,kZvT_3HatchLurker});
			}
			break;
		case Races::Protoss:
			if (!configuration.ZvP_opening().empty()) {
				opening_ = configuration.ZvP_opening();
			} else {
				opening_ = result_store.pick_strategy({kZvP_5Pool,kZvP_2HatchLing,kZvP_3HatchLing,kZvP_9HatchLing,kZvP_10HatchLing,kZvP_2HatchMuta,kZvP_3HatchMuta,kZvP_2HatchHydra,kZvP_9734,kZvP_3HatchLurker,kZvP_NeoSauron,kZvP_4HatchBeforeGas,kZvP_5HatchBeforeGas,kZvP_6Hatch});
			}
			break;
		default:
			if (!configuration.ZvU_opening().empty()) {
				opening_ = configuration.ZvU_opening();
			} else {
				opening_ = result_store.pick_strategy({kZvU_4Pool,kZvU_5Pool,kZvU_2HatchLing,kZvU_3HatchLing,kZvU_9HatchLing,kZvU_10HatchLing,kZvU_9PoolSpeed,kZvU_11Pool});
			}
			break;
	}
}

std::string ZergStrategy::mode() const
{
	switch (mode_) {
		case Mode::Opening:
			return "Opening";
		case Mode::DefendOneBaseProtoss:
			return "Defend one base protoss";
		case Mode::DefendProxyRax:
			return "Defend proxy rax";
		case Mode::DefendFastPool:
			return "Defend fast pool";
		case Mode::Main_MutaHydraLurkerLing:
			return "Main Muta/Hydra/Lurker/Ling";
		case Mode::Main_HydraLurkerLing:
			return "Main Hydra/Lurker/Ling";
		case Mode::Main_UltraLing:
			return "Main Ultra/Ling";
		case Mode::Main_ZvZ:
			return "Main ZvZ";
		case Mode::Main_ZvZLateGame:
			return "Main ZvZ late game";
		default:
			return "?";
	}
}

void ZergStrategy::opening_ZvZ_9poolspire()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 0;
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main_ZvZ;
		return;
	}
	bool save_larvae = morphing_building_hp_at_least(UnitTypes::Zerg_Spire, 350);
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 9 && building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (supply >= 8 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
		!save_larvae &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) < 6) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= 8) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 6) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	}
	if (done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
		if (!save_larvae &&
			training_manager.unit_count(UnitTypes::Zerg_Zergling) < 8) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
		} else {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
		}
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1 &&
		!save_larvae &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) < 14) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (done_or_in_progress(UpgradeTypes::Metabolic_Boost) &&
		building_manager.building_exists(UnitTypes::Zerg_Lair)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
	}
	if (supply >= 16 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spire) &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 3 &&
		training_manager.unit_count(UnitTypes::Zerg_Mutalisk) < 3) {
		training_manager.larva_train_distribution().clear();
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
	}
	if (opening_attack_started_ &&
		training_manager.unit_count(UnitTypes::Zerg_Mutalisk) >= 3) {
		mode_ = Mode::Main_ZvZ;
		return;
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 2 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) attack_check_condition();
	if (!save_larvae &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) < 9 &&
		training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvZ_9gas9pool()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 0;
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main_ZvZ;
		return;
	}
	bool save_larvae = morphing_building_hp_at_least(UnitTypes::Zerg_Spire, 350);
	int supply = opening_supply_count();
	if (supply >= 9 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (supply >= 9 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= 8) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (supply >= 11 &&
		building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
		if (done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1) {
			worker_manager.set_limit_refinery_workers(2);
		}
		if (building_manager.building_exists(UnitTypes::Zerg_Lair)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
		}
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Spire) >= 1) {
			worker_manager.set_limit_refinery_workers(3);
		}
	}
	if (supply >= 17 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spire)) {
		if (training_manager.unit_count(UnitTypes::Zerg_Mutalisk) < 3) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
		} else {
			mode_ = Mode::Main_ZvZ;
		}
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 2 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) attack_check_condition();
	if (!save_larvae &&
		training_manager.larva_train_distribution().is_empty()) {
		int wanted_drone_count = 11;
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) {
			wanted_drone_count--;
		}
		if (training_manager.unit_count(UnitTypes::Zerg_Drone) < wanted_drone_count) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
		} else if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
				   done_or_in_progress(UpgradeTypes::Metabolic_Boost) &&
				   building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1 &&
				   training_manager.unit_count(UnitTypes::Zerg_Zergling) < 17) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
		}
	}
}

void ZergStrategy::opening_ZvZ_9gas10pool()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 0;
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main_ZvZ;
		return;
	}
	bool save_larvae = morphing_building_hp_at_least(UnitTypes::Zerg_Spire, 350);
	int supply = opening_supply_count();
	if (supply >= 9 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (supply >= 10 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= 8) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (supply >= 12 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	if (supply >= 14 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 2);
		if (building_manager.building_exists(UnitTypes::Zerg_Lair)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
		}
	}
	if (supply >= 17 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 4) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spire)) {
		if (training_manager.unit_count(UnitTypes::Zerg_Mutalisk) < 6) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
		} else {
			mode_ = Mode::Main_ZvZ;
		}
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 2 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) attack_check_condition();
	if (!save_larvae &&
		training_manager.larva_train_distribution().is_empty()) {
		int wanted_drone_count = 12;
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) {
			wanted_drone_count--;
		}
		if (training_manager.unit_count(UnitTypes::Zerg_Drone) < wanted_drone_count) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
		} else if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
				   building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1 &&
				   training_manager.unit_count(UnitTypes::Zerg_Zergling) < 21) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
		}
	}
}

void ZergStrategy::opening_ZvZ_11gas10pool()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 1;
		mode_ = Mode::DefendFastPool;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (supply >= 10 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 11 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		if (training_manager.unit_count(UnitTypes::Zerg_Zergling) < 6 || opening_is_more_zerglings_needed()) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
		}
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 6) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	}
	if (supply >= 16) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			training_manager.set_automatic_supply(true);
		}
	}
	if (done_or_in_progress(UpgradeTypes::Metabolic_Boost) &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 3) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) {
		mode_ = Mode::Main_ZvZ;
	}
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvZ_overpool9gas()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 0;
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main_ZvZ;
		return;
	}
	bool save_larvae = morphing_building_hp_at_least(UnitTypes::Zerg_Spire, 350);
	int supply = opening_supply_count();
	if (supply >= 9) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
		}
	}
	if (supply >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (supply >= 10 &&
		!save_larvae &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) < 13) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 13) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
		if (done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
		}
	}
	if (supply >= 17 ||
		building_manager.building_exists(UnitTypes::Zerg_Lair)) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			training_manager.set_automatic_supply(true);
		}
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Lair)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
	}
	if (building_manager.building_count_including_warping(UnitTypes::Zerg_Spire) >= 1 &&
		information_manager.enemy_count(UnitTypes::Zerg_Zergling) > training_manager.unit_count(UnitTypes::Zerg_Zergling)) {
		building_manager.request_base_defense_sunken_colony(base_state.start_base());
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spire) &&
		training_manager.unit_count(UnitTypes::Zerg_Mutalisk) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
	}
	if (opening_attack_started_ &&
		training_manager.unit_count(UnitTypes::Zerg_Mutalisk) >= 3) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_ZvZ;
			return;
		}
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2) {
			mode_ = Mode::Main_ZvZ;
			return;
		}
	}
	if (!opening_attack_started_ && training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 6) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) attack_check_condition();
	
	if (!save_larvae && training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvZ_12pool()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 1;
		mode_ = Mode::DefendFastPool;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			training_manager.set_automatic_supply(true);
		}
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		}
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
		if (Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice()) {
			worker_manager.set_limit_refinery_workers(2);
		}
	}
	if (supply >= 11 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Extractor) >= 1) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_ZvZ;
			return;
		}
	}
	if (supply >= 10 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2) {
		if (training_manager.unit_count(UnitTypes::Zerg_Zergling) < 6 || opening_is_more_zerglings_needed()) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
		}
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 6) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	}
	if (done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Lair)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) {
		mode_ = Mode::Main_ZvZ;
	}
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvZ_12poolmain()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 1;
		mode_ = Mode::DefendFastPool;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			training_manager.set_automatic_supply(true);
		}
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 11 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 2);
	}
	if (supply >= 10 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
		if (Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice() &&
			(opponent_model.enemy_opening() == EnemyOpening::Z_9Pool ||
			 opponent_model.enemy_opening() == EnemyOpening::Z_9PoolSpeed ||
			 opponent_model.enemy_opening() == EnemyOpening::Z_OverPool)) {
			worker_manager.set_limit_refinery_workers(1);
		}
	}
	if (supply >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Extractor) >= 1) {
		if (training_manager.unit_count(UnitTypes::Zerg_Zergling) < 6 || opening_is_more_zerglings_needed()) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
		}
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 6) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	}
	if (done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
		if (opening_sunkens_placed_ == -1) {
			opening_sunkens_placed_ = place_defense_creep_colonies(base_state.start_base(), 1);
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, opening_sunkens_placed_);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Sunken_Colony) >= opening_sunkens_placed_) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Lair)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1 &&
		!counter_mutalisks_with_spore()) {
		mode_ = Mode::Main_ZvZ;
	}
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvT_7pool()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	int supply = opening_supply_count();
	bool end_lings = ((opening_attack_started_ && !attacking_) || training_manager.unit_count_built(UnitTypes::Zerg_Zergling) >= 22);
	if (supply >= 7) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 7 && building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		}
	}
	if (supply >= 8 && !end_lings) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 12) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 14 && end_lings) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	drone_scout_for_zergling_rush();
	
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1) {
		mode_ = Mode::Main_MutaHydraLurkerLing;
		return;
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 6 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) attack_check_condition();
	
	if (worker_manager.is_scouting() &&
		(information_manager.enemy_building_seen() ||
		 tactics_manager.possible_enemy_start_bases().size() <= 1)) {
		worker_manager.stop_scouting();
	}
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvT_2hatchmuta(bool pool_first)
{
	micro_manager.set_overlord_scout_map_center(true);
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax) {
		mode_ = Mode::DefendProxyRax;
		return;
	}
	if (building_manager.building_placement_failed()) {
		mode_ = (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) ? Mode::Main_MutaHydraLurkerLing : Mode::Main_HydraLurkerLing;
		return;
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) == 0 &&
		!building_manager.building_exists(UnitTypes::Zerg_Sunken_Colony) &&
		(is_enemy_offense_larger_than_defense() ||
		 opening_lost_too_many_workers())) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	bool save_larvae = (morphing_building_hp_at_least(UnitTypes::Zerg_Spawning_Pool, 300) ||
						morphing_building_hp_at_least(UnitTypes::Zerg_Spire, 350));
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9) {
		worker_manager.send_initial_scout();
	}
	if (!pool_first) {
		if (supply >= 12) {
			if (!building_manager.request_bases(2)) {
				mode_ = Mode::Main_HydraLurkerLing;
				return;
			}
		}
		if (supply >= 11 &&
			building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
		}
		if (supply >= 10 &&
			building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		}
		if (supply >= 12 &&
			building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1 &&
			building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
			if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 6) {
				training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
			} else {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
			}
		}
	} else {
		if (supply >= 12) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
		}
		if (supply >= 11 &&
			building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		}
		if (supply >= 11 &&
			building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1) {
			if (!building_manager.request_bases(2)) {
				mode_ = Mode::Main_HydraLurkerLing;
				return;
			}
		}
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1 &&
			training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 6) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
		}
	}
	
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= 10) {
		worker_manager.set_force_refinery_workers(true);
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 6 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_ && building_manager.building_exists(UnitTypes::Zerg_Sunken_Colony)) {
		attacking_ = false;
	}
	
	if (supply >= 16 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	
	if (supply >= 16 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 3 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) >= 6) {
		if (!done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
			building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
		}
		if (building_manager.building_exists(UnitTypes::Zerg_Lair) &&
			training_manager.unit_count(UnitTypes::Zerg_Drone) >= 16) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
			if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 5) {
				training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
			}
		}
		if (building_manager.building_exists(UnitTypes::Zerg_Spire) &&
			training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 5 &&
			training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) < 12) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
		}
		if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) >= 6) {
			training_manager.set_automatic_supply(true);
		}
		if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) >= 12) {
			mode_ = Mode::Main_MutaHydraLurkerLing;
		}
	}
	
	int wanted_sunken_colony_count = 0;
	if (opponent_model.enemy_opening() == EnemyOpening::T_2Rax ||
		opponent_model.enemy_opening() == EnemyOpening::Unknown) {
		int bio_count = information_manager.enemy_count(UnitTypes::Terran_Marine) + information_manager.enemy_count(UnitTypes::Terran_Firebat) + information_manager.enemy_count(UnitTypes::Terran_Medic);
		if (bio_count >= 5) {
			wanted_sunken_colony_count = 3;
		}
	}
	if (information_manager.enemy_exists(UnitTypes::Terran_Vulture)) {
		wanted_sunken_colony_count = 2;
	}
	if (wanted_sunken_colony_count > 0 &&
		(opening_sunkens_placed_ == -1 ||
		 (opening_sunkens_placed_ == 0 && (Broodwar->getFrameCount() % 50) == 0))) {
		opening_sunkens_placed_ = place_defense_creep_colonies(base_state.natural_base(), wanted_sunken_colony_count);
	}
	if (opening_sunkens_placed_ > 0) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, opening_sunkens_placed_);
	}
	
	if (!save_larvae && training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvT_2_5hatchmuta()
{
	micro_manager.set_overlord_scout_map_center(true);
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax) {
		mode_ = Mode::DefendProxyRax;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) ? Mode::Main_MutaHydraLurkerLing : Mode::Main_HydraLurkerLing;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 12) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
		}
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		}
	}
	if (supply >= 13 &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) < 4) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1 &&
			training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		}
	}
	if (supply >= 18) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	}
	if (supply >= 21) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 3);
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 3) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
		}
	}
	if (supply >= 20 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spire) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
	}
	if (supply >= 26) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 5) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			if (opening_sunkens_placed_ == -1) {
				opening_sunkens_placed_ = place_defense_creep_colonies(base_state.natural_base(), 2);
			}
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, opening_sunkens_placed_);
		}
	}
	if (supply >= 24 &&
		opening_sunkens_placed_ >= 0 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Sunken_Colony) >= opening_sunkens_placed_) {
		if (training_manager.unit_count(UnitTypes::Zerg_Mutalisk) < 7) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
		} else {
			mode_ = Mode::Main_MutaHydraLurkerLing;
		}
	}
	
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvT_3hatchmuta()
{
	micro_manager.set_overlord_scout_map_center(true);
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax) {
		mode_ = Mode::DefendProxyRax;
		return;
	}
	if (building_manager.building_placement_failed()) {
		mode_ = (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) ? Mode::Main_MutaHydraLurkerLing : Mode::Main_HydraLurkerLing;
		return;
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) == 0 &&
		!building_manager.building_exists(UnitTypes::Zerg_Sunken_Colony) &&
		(is_enemy_offense_larger_than_defense() ||
		 opening_lost_too_many_workers())) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	base_state.set_skip_mineral_only(true);
	bool save_larvae = (morphing_building_hp_at_least(UnitTypes::Zerg_Spawning_Pool, 300) ||
						morphing_building_hp_at_least(UnitTypes::Zerg_Spire, 350));
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 12) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 11 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 13 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 3);
	}
	if (supply >= 12 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (supply >= 12 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 4) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 16) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
		}
	}
	if (supply >= 21) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 2) {
			building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
		}
		if (done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
		}
	}
	if (supply >= 24 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 4) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	int wanted_creep_colony_count = -1;
	int wanted_sunken_colony_count = -1;
	if (supply >= 25) {
		wanted_creep_colony_count = 1;
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) >= 26 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 12) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) >= 12) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 7) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			wanted_sunken_colony_count = 2;
		}
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_2Rax ||
		opponent_model.enemy_opening() == EnemyOpening::Unknown) {
		int bio_count = information_manager.enemy_count(UnitTypes::Terran_Marine) + information_manager.enemy_count(UnitTypes::Terran_Firebat) + information_manager.enemy_count(UnitTypes::Terran_Medic);
		if (bio_count >= 5) {
			wanted_sunken_colony_count = 3;
		}
	}
	if (information_manager.enemy_exists(UnitTypes::Terran_Vulture)) {
		wanted_sunken_colony_count = 2;
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spire) &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 7) {
		training_manager.set_automatic_supply(true);
		if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) < 12) {
			training_manager.larva_train_distribution().clear();
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
		} else {
			mode_ = Mode::Main_MutaHydraLurkerLing;
		}
	}
	
	int wanted_placements = std::max(wanted_creep_colony_count, wanted_sunken_colony_count);
	if ((Broodwar->getFrameCount() % 25) == 0 &&
		wanted_placements > 0 &&
		wanted_placements > opening_sunkens_placed_) {
		if (opening_sunkens_placed_ == -1) {
			opening_sunkens_placed_ = place_defense_creep_colonies(base_state.natural_base(), wanted_placements);
		} else {
			opening_sunkens_placed_ += place_defense_creep_colonies(base_state.natural_base(), wanted_placements - opening_sunkens_placed_);
		}
	}
	if (wanted_creep_colony_count > 0) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Creep_Colony, std::min(wanted_creep_colony_count, opening_sunkens_placed_), true);
	}
	if (wanted_sunken_colony_count > 0) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, std::min(wanted_sunken_colony_count, opening_sunkens_placed_), true);
	}
	
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= 10) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 2 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_ && building_manager.building_exists(UnitTypes::Zerg_Sunken_Colony)) {
		attacking_ = false;
	}
	
	if (!save_larvae && training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvT_13poolmuta()
{
	base_state.set_skip_mineral_only(true);
	micro_manager.set_overlord_scout_map_center(true);
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
		(is_enemy_offense_larger_than_defense() &&
		 !building_manager.building_exists(UnitTypes::Zerg_Spire))) {
		mode_ = Mode::DefendProxyRax;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 12 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (supply >= 13 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Extractor) >= 1) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	if (supply >= 13 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 4) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 16 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 17) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
	}
	if (supply >= 19) {
		if (opening_sunkens_placed_ == -1 ||
			(opening_sunkens_placed_ == 0 && (Broodwar->getFrameCount() % 50) == 0)) {
			opening_sunkens_placed_ = place_defense_creep_colonies(base_state.natural_base(), 1);
		}
	}
	if (supply >= 20 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 4) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (morphing_building_hp_at_least(UnitTypes::Zerg_Spire, 450) ||
		building_manager.building_exists(UnitTypes::Zerg_Spire)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
	}
	int wanted_drone_count = std::max(18, 12 + 3 * building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor));
	if (building_manager.building_exists(UnitTypes::Zerg_Spire)) {
		training_manager.set_automatic_supply(true);
		if (training_manager.unit_count(UnitTypes::Zerg_Drone) >= wanted_drone_count) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
		}
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Mutalisk) >= 6) {
		attacking_ = true;
		if (opponent_model.cloaked_or_mine_present() &&
			!done_or_in_progress(UpgradeTypes::Pneumatized_Carapace)) {
			building_manager.request_upgrade(UpgradeTypes::Pneumatized_Carapace);
		} else {
			attack_and_carapace_upgrades({UpgradeTypes::Zerg_Flyer_Carapace, UpgradeTypes::Zerg_Flyer_Attacks});
		}
	}
	
	if (attacking_) {
		int geyser_count = base_state.controlled_geyser_count();
		int active_extractor_count = 0;
		for (auto& information_unit : information_manager.my_units()) {
			Unit unit = information_unit->unit;
			if (unit->getType() == UnitTypes::Zerg_Extractor && unit->isCompleted() && unit->getResources() > 500) active_extractor_count++;
		}
		int wanted_extractor_count = building_manager.building_count(UnitTypes::Zerg_Extractor) + std::max(0, 2 - active_extractor_count);
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, std::min(geyser_count, wanted_extractor_count));
		if (wanted_extractor_count > geyser_count || base_state.mineable_mineral_count() < 1500) {
			building_manager.request_next_base();
		}
	}
	
	counter_air_harass_with_spore();
	if (information_manager.enemy_exists(UnitTypes::Terran_Vulture)) {
		if (opening_sunkens_placed_ == 1) {
			opening_sunkens_placed_ += place_defense_creep_colonies(base_state.natural_base(), 1);
			opening_sunkens_placed_ += place_defense_creep_colonies(base_state.start_base(), 1);
		}
		building_manager.request_base_defense_sunken_colony(base_state.start_base());
	}
	if (opening_sunkens_placed_ > 0) {
		int sunken_count = opening_sunkens_placed_;
		if (building_manager.base_defense_sunken_colony_planned_or_exists(base_state.start_base())) {
			sunken_count++;
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, sunken_count);
	}
	
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= 10) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (training_manager.larva_train_distribution().is_empty() &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) < wanted_drone_count) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvT_mutahydra(bool pool_first)
{
	base_state.set_skip_mineral_only(true);
	micro_manager.set_overlord_scout_map_center(true);
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
		(is_enemy_offense_larger_than_defense() &&
		 !building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den) &&
		 !building_manager.building_exists(UnitTypes::Zerg_Spire))) {
		mode_ = Mode::DefendProxyRax;
		return;
	}
	bool save_larvae = false;
	int supply = opening_supply_count();
	if (!pool_first) {
		if (supply >= 9 &&
			training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		}
		if (supply >= 12) {
			if (!building_manager.request_bases(2)) {
				mode_ = Mode::Main_HydraLurkerLing;
				return;
			}
		}
		if (supply >= 11 &&
			building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
		}
		if (supply >= 10 &&
			building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		}
	} else {
		if (supply >= 9) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
		}
		if (supply >= 9 &&
			building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1 &&
			training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		}
		if (supply >= 12) {
			if (!building_manager.request_bases(2)) {
				mode_ = Mode::Main_HydraLurkerLing;
				return;
			}
		}
		if (supply >= 11 &&
			building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2 &&
			training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		}
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 15 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 17) {
		if (opening_sunkens_placed_ == -1 ||
			(opening_sunkens_placed_ == 0 && (Broodwar->getFrameCount() % 50) == 0)) {
			opening_sunkens_placed_ = place_defense_creep_colonies(base_state.natural_base(), 1);
		}
		if (opening_sunkens_placed_ > 0) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, opening_sunkens_placed_);
		}
	}
	if (building_manager.building_count_including_warping(UnitTypes::Zerg_Sunken_Colony) >= 1 &&
		Broodwar->self()->gas() >= 250) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hydralisk_Den, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hydralisk_Den) >= 1 &&
		building_manager.building_exists(UnitTypes::Zerg_Lair)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 2 &&
		building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den)) {
		building_manager.request_upgrade(UpgradeTypes::Grooved_Spines);
	}
	if (done_or_in_progress(UpgradeTypes::Grooved_Spines) &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Hydralisk) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
	}
	if (supply >= 21 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Hydralisk) < 5) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
	}
	if (supply >= 24) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 5) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else if (!attacking_) {
			save_larvae = true;
		}
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spire) &&
		Broodwar->self()->getUpgradeLevel(UpgradeTypes::Muscular_Augments) < 1) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Mutalisk) > 0) {
		attacking_ = true;
	}
	if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) >= 8) {
		building_manager.request_upgrade(UpgradeTypes::Muscular_Augments);
		training_manager.set_automatic_supply(true);
	}
	int wanted_drone_count = std::max(20, 14 + 3 * building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor));
	if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Muscular_Augments) >= 1 &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= wanted_drone_count) {
		if (Broodwar->self()->minerals() >= 300 && Broodwar->self()->gas() >= 300) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
		} else {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
		}
	}
	
	if (attacking_) {
		int geyser_count = base_state.controlled_geyser_count();
		int active_extractor_count = 0;
		for (auto& information_unit : information_manager.my_units()) {
			Unit unit = information_unit->unit;
			if (unit->getType() == UnitTypes::Zerg_Extractor && unit->isCompleted() && unit->getResources() > 500) active_extractor_count++;
		}
		int wanted_extractor_count = building_manager.building_count(UnitTypes::Zerg_Extractor) + std::max(0, 2 - active_extractor_count);
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, std::min(geyser_count, wanted_extractor_count));
		if (wanted_extractor_count > geyser_count || base_state.mineable_mineral_count() < 1500) {
			building_manager.request_next_base();
		}
	}
	
	if (!save_larvae && training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvT_3hatchlurker()
{
	micro_manager.set_overlord_scout_map_center(true);
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax) {
		mode_ = Mode::DefendProxyRax;
		return;
	}
	if (building_manager.building_placement_failed()) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	if (!building_manager.building_exists(UnitTypes::Zerg_Sunken_Colony) &&
		(is_enemy_offense_larger_than_defense() ||
		 opening_lost_too_many_workers())) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	base_state.set_skip_mineral_only(true);
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 12) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 11 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 13 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 3);
	}
	if (supply >= 12 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2 &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) < 6) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 16) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		}
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	if (supply >= 22) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 4) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			training_manager.set_automatic_supply(true);
			building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
			if (done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 4);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 4) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hydralisk_Den, 1);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hydralisk_Den) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 2) {
				building_manager.request_research(TechTypes::Lurker_Aspect);
			}
			if (done_or_in_progress(TechTypes::Lurker_Aspect)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Evolution_Chamber, 1);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Evolution_Chamber) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
			}
			if (building_manager.building_exists(UnitTypes::Zerg_Evolution_Chamber)) {
				building_manager.request_upgrade(UpgradeTypes::Zerg_Carapace);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) {
				if (training_manager.unit_count(UnitTypes::Zerg_Hydralisk) + training_manager.unit_count(UnitTypes::Zerg_Lurker) < 9) {
					training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
				} else {
					training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
				}
			}
			if (Broodwar->self()->hasResearched(TechTypes::Lurker_Aspect)) {
				training_manager.set_requested_lurker_count(9);
			}
			if (training_manager.unit_count(UnitTypes::Zerg_Lurker) >= 9 &&
				done_or_in_progress(UpgradeTypes::Zerg_Carapace)) {
				mode_ = Mode::Main_HydraLurkerLing;
				return;
			}
		}
	}
	
	int wanted_sunken_count = 0;
	if (opponent_model.enemy_opening() == EnemyOpening::T_2Rax ||
		opponent_model.enemy_opening() == EnemyOpening::Unknown) {
		int bio_count = information_manager.enemy_count(UnitTypes::Terran_Marine) + information_manager.enemy_count(UnitTypes::Terran_Firebat) + information_manager.enemy_count(UnitTypes::Terran_Medic);
		if (bio_count >= 5) {
			wanted_sunken_count = 3;
		}
	}
	if (information_manager.enemy_exists(UnitTypes::Terran_Vulture)) {
		wanted_sunken_count = 2;
	}
	if ((Broodwar->getFrameCount() % 25) == 0 &&
		wanted_sunken_count > opening_sunkens_placed_) {
		if (opening_sunkens_placed_ == -1) {
			opening_sunkens_placed_ = place_defense_creep_colonies(base_state.natural_base(), wanted_sunken_count);
		} else {
			opening_sunkens_placed_ += place_defense_creep_colonies(base_state.natural_base(), wanted_sunken_count - opening_sunkens_placed_);
		}
	}
	if (wanted_sunken_count > 0) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, std::min(wanted_sunken_count, opening_sunkens_placed_), true);
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 6 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) attack_check_condition();
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvP_2hatchmuta()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	if ((opponent_model.enemy_opening() != EnemyOpening::Unknown ||
		 opponent_model.enemy_natural_sufficiently_scouted()) &&
		opponent_model.enemy_opening() != EnemyOpening::P_FastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_ForgeFastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_CannonTurtle) {
		building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
		mode_ = Mode::DefendOneBaseProtoss;
		return;
	}
	base_state.set_skip_mineral_only(true);
	bool save_larvae = (morphing_building_hp_at_least(UnitTypes::Zerg_Spawning_Pool, 300) ||
						morphing_building_hp_at_least(UnitTypes::Zerg_Spire, 350));
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		if (!building_manager.request_bases(2)) {
			building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 10 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 8) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 14) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1 &&
		building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= 10) {
		worker_manager.set_force_refinery_workers(true);
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 6 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) attack_check_condition();
	
	if (supply >= 16) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else if (!done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
			building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
		}
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Lair)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
	}
	if (building_manager.building_count_including_warping(UnitTypes::Zerg_Spire) >= 1 &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= 22) {
		if (!building_manager.request_bases(3)) {
			building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
			mode_ = Mode::Main_MutaHydraLurkerLing;
			return;
		}
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 3) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 2 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 5) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 5 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) < 9) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
	}
	if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) >= 6 &&
		building_manager.building_count(UnitTypes::Zerg_Hatchery) >= 3) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) <= 6) {
			training_manager.larva_train_distribution().clear();
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			if (!opening_macro_hatch_placed) {
				building_placement_manager.calculate_macro_hatch_position(base_state.natural_base());
				opening_macro_hatch_placed = true;
			}
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 4);
		}
	}
	if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) >= 9 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 4) {
		building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
		mode_ = Mode::Main_MutaHydraLurkerLing;
	}
	if (!save_larvae && training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvP_3hatchmuta()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	if ((opponent_model.enemy_opening() != EnemyOpening::Unknown ||
		 opponent_model.enemy_natural_sufficiently_scouted()) &&
		opponent_model.enemy_opening() != EnemyOpening::P_FastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_ForgeFastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_CannonTurtle) {
		mode_ = Mode::DefendOneBaseProtoss;
		return;
	}
	base_state.set_skip_mineral_only(true);
	bool save_larvae = (morphing_building_hp_at_least(UnitTypes::Zerg_Spawning_Pool, 300) ||
						morphing_building_hp_at_least(UnitTypes::Zerg_Spire, 350));
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 10 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 6) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 13) {
		if (!building_manager.request_bases(3)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 16) {
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Extractor) == 0) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		} else if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 2) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
			}
			if (supply >= 24 &&
				training_manager.unit_count(UnitTypes::Zerg_Overlord) < 4) {
				training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1 &&
				opening_sunkens_placed_ == -1) {
				opening_sunkens_placed_ = place_defense_creep_colonies(base_state.natural_base(), 2);
			}
			if (opening_sunkens_placed_ > 0) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, opening_sunkens_placed_);
			}
			if (opening_sunkens_placed_ != -1 &&
				building_manager.building_count_including_planned(UnitTypes::Zerg_Sunken_Colony) >= opening_sunkens_placed_ &&
				training_manager.unit_count(UnitTypes::Zerg_Overlord) < 6) {
				training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
			}
			if (training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 6 &&
				building_manager.building_exists(UnitTypes::Zerg_Spire)) {
				if (training_manager.unit_count(UnitTypes::Zerg_Mutalisk) < 9) {
					training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
				} else {
					mode_ = Mode::Main_MutaHydraLurkerLing;
				}
			}
		}
	}
	
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= 10) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 6 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) attack_check_condition();
	
	if (!save_larvae &&
		training_manager.larva_train_distribution().is_empty() &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) < 30) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvP_2hatchhydra()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	if ((opponent_model.enemy_opening() != EnemyOpening::Unknown ||
		 opponent_model.enemy_natural_sufficiently_scouted()) &&
		opponent_model.enemy_opening() != EnemyOpening::P_FastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_ForgeFastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_CannonTurtle) {
		mode_ = Mode::DefendOneBaseProtoss;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 10 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 4) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (supply >= 12 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1 &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) < 6) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hydralisk_Den, 1);
	}
	if (supply >= 16 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 18 &&
		building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den) &&
		training_manager.larva_train_distribution().is_empty() &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= 15) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den)) {
		if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Muscular_Augments) < 1) {
			building_manager.request_upgrade(UpgradeTypes::Muscular_Augments);
		} else if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Grooved_Spines) < 1) {
			building_manager.request_upgrade(UpgradeTypes::Grooved_Spines);
		}
	}
	counter_air_harass_with_spore();
	if (opening_attack_started_ &&
		done_or_in_progress(UpgradeTypes::Muscular_Augments) &&
		done_or_in_progress(UpgradeTypes::Grooved_Spines)) {
		mode_ = Mode::Main_HydraLurkerLing;
	}
	
	if (!opening_attack_started_ &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 4) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) attack_check_condition();
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvP_9734()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	if ((opponent_model.enemy_opening() != EnemyOpening::Unknown ||
		 opponent_model.enemy_natural_sufficiently_scouted()) &&
		opponent_model.enemy_opening() != EnemyOpening::P_FastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_ForgeFastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_CannonTurtle) {
		mode_ = Mode::DefendOneBaseProtoss;
		return;
	}
	base_state.set_skip_mineral_only(true);
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 10 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 2 &&
		!opening_attack_started_) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 13) {
		if (!building_manager.request_bases(3)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 12 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 3) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (supply >= 17) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hydralisk_Den, 1);
	}
	if (supply >= 16 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hydralisk_Den) >= 1 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 18) {
		building_manager.request_upgrade(UpgradeTypes::Muscular_Augments);
	}
	if (supply >= 18 &&
		done_or_in_progress(UpgradeTypes::Muscular_Augments) &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) < 6 &&
		!opening_attack_started_) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 24 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 4) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 25 && !opening_attack_started_) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
	}
	if (supply >= 32) {
		building_manager.request_upgrade(UpgradeTypes::Grooved_Spines);
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 5) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		}
	}
	if (supply >= 35) {
		if (!opening_attack_started_) {
			attacking_ = true;
			opening_attack_started_ = true;
		}
		if (!building_manager.request_bases(4)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (opening_attack_started_) attack_check_condition();
	
	counter_air_harass_with_spore();
	if (opening_attack_started_ &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 4) {
		mode_ = Mode::Main_HydraLurkerLing;
	}
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	} else if (building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den) &&
			   building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 3 &&
			   training_manager.unit_count(UnitTypes::Zerg_Drone) < 19 &&
			   training_manager.larva_train_distribution().get(UnitTypes::Zerg_Overlord) == 0.0) {
		training_manager.larva_train_distribution().clear();
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvP_3hatchlurker()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	if ((opponent_model.enemy_opening() != EnemyOpening::Unknown ||
		 opponent_model.enemy_natural_sufficiently_scouted()) &&
		opponent_model.enemy_opening() != EnemyOpening::P_FastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_ForgeFastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_CannonTurtle) {
		mode_ = Mode::DefendOneBaseProtoss;
		return;
	}
	base_state.set_skip_mineral_only(true);
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 10 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 6) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 13) {
		if (!building_manager.request_bases(3)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 16) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		}
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	if (supply >= 22) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 4) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			training_manager.set_automatic_supply(true);
			building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
			if (done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 4);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 4) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hydralisk_Den, 1);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hydralisk_Den) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 2) {
				building_manager.request_research(TechTypes::Lurker_Aspect);
			}
			if (done_or_in_progress(TechTypes::Lurker_Aspect)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Evolution_Chamber, 1);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Evolution_Chamber) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
			}
			if (building_manager.building_exists(UnitTypes::Zerg_Evolution_Chamber)) {
				building_manager.request_upgrade(UpgradeTypes::Zerg_Carapace);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) {
				if (training_manager.unit_count(UnitTypes::Zerg_Hydralisk) + training_manager.unit_count(UnitTypes::Zerg_Lurker) < 9) {
					training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
				} else {
					training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
				}
			}
			if (Broodwar->self()->hasResearched(TechTypes::Lurker_Aspect)) {
				training_manager.set_requested_lurker_count(9);
			}
			if (training_manager.unit_count(UnitTypes::Zerg_Lurker) >= 9 &&
				done_or_in_progress(UpgradeTypes::Zerg_Carapace)) {
				mode_ = Mode::Main_HydraLurkerLing;
				return;
			}
		}
	}
	
	counter_air_harass_with_spore();
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 6 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_ && building_manager.building_exists(UnitTypes::Zerg_Sunken_Colony)) {
		attacking_ = false;
	}
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvP_neosauron()
{
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	if ((opponent_model.enemy_opening() != EnemyOpening::Unknown ||
		 opponent_model.enemy_natural_sufficiently_scouted()) &&
		opponent_model.enemy_opening() != EnemyOpening::P_FastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_ForgeFastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_CannonTurtle) {
		mode_ = Mode::DefendOneBaseProtoss;
		return;
	}
	
	bool hydra_started = training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Hydralisk) > 0;
	bool muta_started = training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) > 0;
	base_state.set_skip_mineral_only(true);
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		training_manager.set_automatic_supply(true);
	}
	if (supply >= 11) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 10 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 6) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 13) {
		if (!building_manager.request_bases(3)) {
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		if (!hydra_started &&
			training_manager.unit_count(UnitTypes::Zerg_Zergling) * UnitTypes::Zerg_Zergling.supplyRequired() < tactics_manager.enemy_army_supply() &&
			training_manager.unit_count(UnitTypes::Zerg_Zergling) < 12) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
			training_manager.set_prioritize_training(true);
		}
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 3) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Lair) &&
		done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
	}
	
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) >= 26 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 4);
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 4) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
		}
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) >= 29 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hydralisk_Den, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hydralisk_Den) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 5);
		}
		if (building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den) &&
			building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 5) {
			building_manager.request_upgrade(UpgradeTypes::Muscular_Augments);
		}
		if (done_or_in_progress(UpgradeTypes::Muscular_Augments)) {
			building_manager.request_upgrade(UpgradeTypes::Pneumatized_Carapace);
		}
		if (done_or_in_progress(UpgradeTypes::Pneumatized_Carapace)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Evolution_Chamber, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Evolution_Chamber) >= 1 &&
			building_manager.building_exists(UnitTypes::Zerg_Spire) &&
			training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Scourge) < 4) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Scourge, 1.0);
		}
		if (training_manager.unit_count_built(UnitTypes::Zerg_Scourge) >= 4 &&
			!done_or_in_progress(UpgradeTypes::Zerg_Missile_Attacks)) {
			building_manager.request_upgrade(UpgradeTypes::Zerg_Missile_Attacks);
		}
		if (done_or_in_progress(UpgradeTypes::Zerg_Missile_Attacks)) {
			if (opening_sunkens_placed_ == -1) {
				opening_sunkens_placed_ = place_defense_creep_colonies(base_state.natural_base(), 2);
			}
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, opening_sunkens_placed_);
		}
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) >= 31 &&
		done_or_in_progress(UpgradeTypes::Zerg_Missile_Attacks)) {
		hydra_started = true;
		building_manager.request_upgrade(UpgradeTypes::Grooved_Spines);
	}
	if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Hydralisk) >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 3);
	}
	if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Hydralisk) >= 18 &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) < 36) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
	if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Hydralisk) >= 70) {
		bool skip_muta = (information_manager.enemy_count(UnitTypes::Protoss_Dragoon) >= 5);
		if (!skip_muta) {
			muta_started = true;
		}
		if (skip_muta || training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) >= 12) {
			building_manager.request_research(TechTypes::Lurker_Aspect);
		}
		if (done_or_in_progress(TechTypes::Lurker_Aspect)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Evolution_Chamber, 3);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Evolution_Chamber) >= 3) {
			if (!building_manager.request_bases(4)) {
				mode_ = Mode::Main_HydraLurkerLing;
				return;
			}
		}
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 6 &&
			training_manager.unit_count(UnitTypes::Zerg_Drone) < 54) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
		}
		if (training_manager.unit_count(UnitTypes::Zerg_Drone) >= 54 &&
			!done_or_in_progress(UpgradeTypes::Zerg_Missile_Attacks, 2)) {
			building_manager.request_upgrade(UpgradeTypes::Zerg_Missile_Attacks);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Evolution_Chamber) >= 3) {
			if (!done_or_in_progress(UpgradeTypes::Zerg_Melee_Attacks)) {
				building_manager.request_upgrade(UpgradeTypes::Zerg_Melee_Attacks);
			}
			if (!done_or_in_progress(UpgradeTypes::Zerg_Carapace)) {
				building_manager.request_upgrade(UpgradeTypes::Zerg_Carapace);
			}
		}
	}
	counter_air_harass_with_spore();
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) >= 54 &&
		done_or_in_progress(TechTypes::Lurker_Aspect) &&
		done_or_in_progress(UpgradeTypes::Zerg_Missile_Attacks, 2) &&
		done_or_in_progress(UpgradeTypes::Zerg_Melee_Attacks) &&
		done_or_in_progress(UpgradeTypes::Zerg_Carapace)) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 2 &&
		!opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) {
		attack_check_condition();
	}
	
	if (hydra_started &&
		is_enemy_offense_larger_than_defense()) {
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	
	if (training_manager.larva_train_distribution().is_empty()) {
		if (muta_started &&
			training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) < 12) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
		} else if (hydra_started) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
		} else {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
		}
	}
}

void ZergStrategy::opening_ZvP_4hatchbeforegas()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	if ((opponent_model.enemy_opening() != EnemyOpening::Unknown ||
		 opponent_model.enemy_natural_sufficiently_scouted()) &&
		opponent_model.enemy_opening() != EnemyOpening::P_FastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_ForgeFastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_CannonTurtle) {
		building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
		mode_ = Mode::DefendOneBaseProtoss;
		return;
	}
	base_state.set_skip_mineral_only(true);
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		if (!building_manager.request_bases(2)) {
			building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 10 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 6) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 13) {
		if (!building_manager.request_bases(3)) {
			building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 16) {
		if (!opening_macro_hatch_placed) {
			for (auto& base : base_state.controlled_bases()) {
				if (base != base_state.start_base()) {
					building_placement_manager.calculate_macro_hatch_position(base);
				}
			}
			opening_macro_hatch_placed = true;
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 4);
	}
	if (supply >= 18) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Evolution_Chamber, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Evolution_Chamber) &&
		!done_or_in_progress(UpgradeTypes::Zerg_Carapace)) {
		building_manager.request_upgrade(UpgradeTypes::Zerg_Carapace);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Evolution_Chamber) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 5);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 5) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) >= 1) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	}
	counter_air_harass_with_spore();
	if (done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
		building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
		mode_ = Mode::Main_HydraLurkerLing;
	}
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvP_5hatchbeforegas()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	if ((opponent_model.enemy_opening() != EnemyOpening::Unknown ||
		 opponent_model.enemy_natural_sufficiently_scouted()) &&
		opponent_model.enemy_opening() != EnemyOpening::P_FastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_ForgeFastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_CannonTurtle) {
		building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
		mode_ = Mode::DefendOneBaseProtoss;
		return;
	}
	base_state.set_skip_mineral_only(true);
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		if (!building_manager.request_bases(2)) {
			building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 10 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 6) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (supply >= 13) {
		if (!building_manager.request_bases(3)) {
			building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 17 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 18) {
		if (!opening_macro_hatch_placed) {
			for (auto& base : base_state.controlled_bases()) {
				if (base != base_state.start_base()) {
					building_placement_manager.calculate_macro_hatch_position(base);
				}
			}
			opening_macro_hatch_placed = true;
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 4);
	}
	if (supply >= 27) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 5);
	}
	if (supply >= 26 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 5) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (supply >= 24) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 5) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hydralisk_Den, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hydralisk_Den) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
			}
			if (building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den)) {
				building_manager.request_upgrade(UpgradeTypes::Muscular_Augments);
				training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
				training_manager.set_automatic_supply(true);
			}
			if (done_or_in_progress(UpgradeTypes::Muscular_Augments) &&
				building_manager.building_exists(UnitTypes::Zerg_Lair)) {
				building_manager.request_upgrade(UpgradeTypes::Pneumatized_Carapace);
			}
			if (done_or_in_progress(UpgradeTypes::Pneumatized_Carapace)) {
				building_manager.request_upgrade(UpgradeTypes::Grooved_Spines);
			}
			if (done_or_in_progress(UpgradeTypes::Grooved_Spines)) {
				building_manager.request_research(TechTypes::Lurker_Aspect);
			}
			if (done_or_in_progress(TechTypes::Lurker_Aspect)) {
				building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
				mode_ = Mode::Main_HydraLurkerLing;
			}
		}
	}
	
	counter_air_harass_with_spore();
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 2 &&
		!opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) {
		attack_check_condition();
	}
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvP_6hatch()
{
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
		mode_ = Mode::Main_HydraLurkerLing;
		return;
	}
	if ((opponent_model.enemy_opening() != EnemyOpening::Unknown ||
		 opponent_model.enemy_natural_sufficiently_scouted()) &&
		opponent_model.enemy_opening() != EnemyOpening::P_FastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_ForgeFastExpand &&
		opponent_model.enemy_opening() != EnemyOpening::P_CannonTurtle) {
		building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
		mode_ = Mode::DefendOneBaseProtoss;
		return;
	}
	base_state.set_skip_mineral_only(true);
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		if (!building_manager.request_bases(2)) {
			building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 14) {
		if (!building_manager.request_bases(3)) {
			building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
			mode_ = Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (supply >= 16 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	}
	if (supply >= 23 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 4) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 26) {
		if (!opening_macro_hatch_placed) {
			for (auto& base : base_state.controlled_bases()) {
				if (base != base_state.start_base()) {
					building_placement_manager.calculate_macro_hatch_position(base);
				}
			}
			opening_macro_hatch_placed = true;
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 4);
	}
	if (supply >= 30) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hydralisk_Den, 1);
	}
	if (supply >= 31) {
		if (training_manager.unit_count(UnitTypes::Zerg_Overlord) < 5) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		} else {
			training_manager.set_automatic_supply(true);
		}
	}
	if (supply >= 32) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 5);
	}
	if (supply >= 35) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Evolution_Chamber, 1);
		}
	}
	if (supply >= 60) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 6);
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 6) {
			building_placement_manager.clear_specific_positions_for_type(UnitTypes::Zerg_Hatchery);
			mode_ = Mode::Main_HydraLurkerLing;
		}
	}
	if (information_manager.enemy_count(UnitTypes::Protoss_Dragoon) >= 2) {
		if (!done_or_in_progress(UpgradeTypes::Grooved_Spines)) {
			building_manager.request_upgrade(UpgradeTypes::Grooved_Spines);
		} else if (!done_or_in_progress(UpgradeTypes::Muscular_Augments)) {
			building_manager.request_upgrade(UpgradeTypes::Muscular_Augments);
		}
	} else {
		if (!done_or_in_progress(UpgradeTypes::Muscular_Augments)) {
			building_manager.request_upgrade(UpgradeTypes::Grooved_Spines);
		} else if (!done_or_in_progress(UpgradeTypes::Grooved_Spines)) {
			building_manager.request_upgrade(UpgradeTypes::Grooved_Spines);
		}
	}
	if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Grooved_Spines) >= 1 &&
		Broodwar->self()->getUpgradeLevel(UpgradeTypes::Muscular_Augments) >= 1) {
		building_manager.request_research(TechTypes::Lurker_Aspect);
	}
	if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Hydralisk) >= 2) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Evolution_Chamber) &&
		!done_or_in_progress(UpgradeTypes::Zerg_Missile_Attacks)) {
		building_manager.request_upgrade(UpgradeTypes::Zerg_Missile_Attacks);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 2 &&
		!opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_) {
		attack_check_condition();
	}
	
	if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	} else if (building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den) &&
			   ((building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 5 &&
				 training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Hydralisk) < 26) ||
				(training_manager.unit_count(UnitTypes::Zerg_Drone) >= 44 &&
				 (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Grooved_Spines) >= 1 ||
				  Broodwar->self()->getUpgradeLevel(UpgradeTypes::Muscular_Augments) >= 1)) ||
				training_manager.unit_count(UnitTypes::Zerg_Hydralisk) < 2 * information_manager.enemy_count(UnitTypes::Protoss_Corsair))) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
	} else {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvU_9poolspeed()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 0;
		mode_ = Mode::DefendFastPool;
		return;
	}
	int supply = opening_supply_count();
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 8) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		}
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (supply >= 8 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Extractor) >= 1 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 &&
		building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		if (training_manager.unit_count(UnitTypes::Zerg_Zergling) < 6) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
		} else {
			building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
		}
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 2) {
		attacking_ = true;
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 6 &&
		done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
		if (opponent_model.enemy_race() == Races::Zerg) {
			worker_manager.stop_scouting();
		}
		mode_ = (opponent_model.enemy_race() == Races::Zerg) ? Mode::Main_ZvZ : Mode::Main_HydraLurkerLing;
		return;
	}
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvU_11pool()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 0;
		mode_ = Mode::DefendFastPool;
		return;
	}
	int supply = opening_supply_count();
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 8) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 9 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (supply >= 11 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Extractor) >= 1) {
		if (!building_manager.request_bases(2)) {
			mode_ = (opponent_model.enemy_race() == Races::Zerg) ? Mode::Main_ZvZ : Mode::Main_HydraLurkerLing;
			return;
		}
	}
	if (supply >= 10 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
		if (training_manager.unit_count(UnitTypes::Zerg_Zergling) < 6) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
		}
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 2) {
		attacking_ = true;
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 6 &&
		attacking_) {
		if (opponent_model.enemy_race() == Races::Zerg) {
			worker_manager.stop_scouting();
		}
		mode_ = (opponent_model.enemy_race() == Races::Zerg) ? Mode::Main_ZvZ : Mode::Main_HydraLurkerLing;
		return;
	}
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
}

void ZergStrategy::opening_ZvX_4pool()
{
	training_manager.set_automatic_supply(true);
	building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	
	if (building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1 &&
		base_state.start_base_count() >= 3) {
		if (worker_manager.lost_worker_count() == 0 &&
			training_manager.unit_count(UnitTypes::Zerg_Drone) < 4) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
		}
		if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 4) {
			drone_scout_for_zergling_rush();
		}
	}
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 6) {
		attacking_ = true;
	}
	build_mutalisks_if_needed_for_zergling_rush();
}

void ZergStrategy::opening_ZvX_5pool()
{
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) >= 5) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (opponent_model.initial_enemy_race() == Races::Terran ||
		opponent_model.initial_enemy_race() == Races::Protoss) {
		drone_scout_for_zergling_rush();
	}
	
	int wanted_drone_count;
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) == 0) {
		wanted_drone_count = 5;
	} else if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) <= 1) {
		wanted_drone_count = 6;
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Extractor) >= 1) {
			wanted_drone_count--;
		}
	} else {
		wanted_drone_count = (building_manager.building_count(UnitTypes::Zerg_Hatchery) * 7) / 2 + 1;
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) < wanted_drone_count) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	} else if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	
	int supply = (Broodwar->self()->supplyUsed() + 1) / 2;
	if (supply >= 9 && !opening_extractor_trick_) {
		if (training_manager.unit_count(UnitTypes::Zerg_Larva) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		}
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Extractor) >= 1 &&
			training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 8) {
			building_manager.cancel_buildings_of_type(UnitTypes::Zerg_Extractor);
			opening_extractor_trick_ = true;
		}
	}
	training_manager.set_automatic_supply(opening_extractor_trick_);
	
	if (spending_manager.spendable().minerals >= UnitTypes::Zerg_Hatchery.mineralPrice() + 3 * UnitTypes::Zerg_Drone.mineralPrice() ||
		(spending_manager.spendable().minerals >= UnitTypes::Zerg_Hatchery.mineralPrice() && !tactics_manager.is_opponent_army_too_large())) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 1 + building_manager.building_count(UnitTypes::Zerg_Hatchery));
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 6) {
		attacking_ = true;
	}
	if (attacking_ && !building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (!counter_air_harass_with_spore() &&
		!counter_mutalisks_with_spore()) {
		build_mutalisks_if_needed_for_zergling_rush();
	}
	if (base_state.mineable_mineral_count() < 1500) {
		building_manager.request_next_base();
	}
}

void ZergStrategy::opening_ZvX_2hatchling()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 0;
		mode_ = Mode::DefendFastPool;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 9 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2 &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 6 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 2);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2 &&
		Broodwar->self()->gatheredGas() < UpgradeTypes::Metabolic_Boost.gasPrice()) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice() - 16) {
		int shortage = UpgradeTypes::Metabolic_Boost.gasPrice() - Broodwar->self()->gatheredGas();
		worker_manager.set_limit_refinery_workers(shortage / 8);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2 &&
		Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice()) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
		training_manager.set_automatic_supply(true);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		Broodwar->self()->gatheredGas() < UpgradeTypes::Metabolic_Boost.gasPrice()) {
		worker_manager.set_force_refinery_workers(true);
	}
	drone_scout_for_zergling_rush();
	
	int wanted_drone_count = 9;
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) wanted_drone_count--;
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1 ||
		Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice()) {
		wanted_drone_count--;
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) < wanted_drone_count) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	} else if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 2) {
		attacking_ = true;
	}
	if (attacking_ && !building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (!counter_air_harass_with_spore() &&
		!counter_mutalisks_with_spore()) {
		build_mutalisks_if_needed_for_zergling_rush();
	}
	if (base_state.mineable_mineral_count() < 1000) {
		building_manager.request_next_base();
	}
}

void ZergStrategy::opening_ZvX_3hatchling()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 1;
		mode_ = Mode::DefendFastPool;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().clear();
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
		}
	}
	if (supply >= 11 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 13 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 3);
	}
	if (supply >= 12 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 3 &&
		Broodwar->self()->gatheredGas() < UpgradeTypes::Metabolic_Boost.gasPrice()) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 3 &&
		Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice() &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 12) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
		training_manager.set_automatic_supply(true);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		Broodwar->self()->gatheredGas() < UpgradeTypes::Metabolic_Boost.gasPrice()) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice() - 16) {
		int shortage = UpgradeTypes::Metabolic_Boost.gasPrice() - Broodwar->self()->gatheredGas();
		worker_manager.set_limit_refinery_workers(shortage / 8);
	}
	
	int wanted_drone_count = 8;
	if (training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) wanted_drone_count += 4;
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) wanted_drone_count--;
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) wanted_drone_count--;
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) wanted_drone_count += 3;
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 3) wanted_drone_count--;
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1 ||
		Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice()) {
		wanted_drone_count--;
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) < wanted_drone_count) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	} else if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 6) {
		attacking_ = true;
	}
	if (attacking_ && !building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (!counter_air_harass_with_spore() &&
		!counter_mutalisks_with_spore()) {
		build_mutalisks_if_needed_for_zergling_rush();
	}
	if (base_state.mineable_mineral_count() < 1500) {
		building_manager.request_next_base();
	}
}

void ZergStrategy::opening_ZvX_9hatchling()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 0;
		mode_ = Mode::DefendFastPool;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 9 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 2);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2 &&
		Broodwar->self()->gatheredGas() < UpgradeTypes::Metabolic_Boost.gasPrice()) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (supply >= 9 &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 4 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice() - 16) {
		int shortage = UpgradeTypes::Metabolic_Boost.gasPrice() - Broodwar->self()->gatheredGas();
		worker_manager.set_limit_refinery_workers(shortage / 8);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2 &&
		Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice()) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
		training_manager.set_automatic_supply(true);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		Broodwar->self()->gatheredGas() < UpgradeTypes::Metabolic_Boost.gasPrice()) {
		worker_manager.set_force_refinery_workers(true);
	}
	
	int wanted_drone_count = 9;
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) wanted_drone_count--;
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Extractor) >= 1 ||
		Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice()) {
		wanted_drone_count--;
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) < wanted_drone_count) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	} else if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 2) {
		attacking_ = true;
	}
	if (attacking_ && !building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (!counter_air_harass_with_spore() &&
		!counter_mutalisks_with_spore()) {
		build_mutalisks_if_needed_for_zergling_rush();
	}
	if (base_state.mineable_mineral_count() < 1000) {
		building_manager.request_next_base();
	}
}

void ZergStrategy::opening_ZvX_10hatchling()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		fast_pool_sunken_count_ = 1;
		mode_ = Mode::DefendFastPool;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 9 && building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) <= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Extractor) >= 1 &&
			training_manager.unit_count(UnitTypes::Zerg_Drone) >= 9) {
			building_manager.cancel_buildings_of_type(UnitTypes::Zerg_Extractor);
		}
	}
	if (supply >= 10) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 2);
	}
	if (supply >= 9 &&
		building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1 &&
			training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
		}
	}
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1 &&
		training_manager.unit_count(UnitTypes::Zerg_Drone) >= 9 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2 &&
		training_manager.unit_count(UnitTypes::Zerg_Zergling) >= 6 &&
		Broodwar->self()->gatheredGas() < UpgradeTypes::Metabolic_Boost.gasPrice()) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		Broodwar->self()->gatheredGas() < UpgradeTypes::Metabolic_Boost.gasPrice()) {
		worker_manager.set_force_refinery_workers(true);
	}
	if (Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice() - 16) {
		int shortage = UpgradeTypes::Metabolic_Boost.gasPrice() - Broodwar->self()->gatheredGas();
		worker_manager.set_limit_refinery_workers(shortage / 8);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
		Broodwar->self()->gatheredGas() >= UpgradeTypes::Metabolic_Boost.gasPrice()) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	}
	if (supply >= 18 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 3) {
		training_manager.set_automatic_supply(true);
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 3 &&
		done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
		if (spending_manager.spendable().minerals >= UnitTypes::Zerg_Hatchery.mineralPrice()) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 3);
		}
	}
	
	if (training_manager.larva_train_distribution().is_empty()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 2) {
		attacking_ = true;
	}
	if (!counter_air_harass_with_spore() &&
		!counter_mutalisks_with_spore()) {
		build_mutalisks_if_needed_for_zergling_rush();
	}
	if (base_state.mineable_mineral_count() < 1100) {
		building_manager.request_next_base();
	}
}

bool ZergStrategy::opening_is_more_zerglings_needed()
{
	TrainDistribution train_distribution(UnitTypes::Zerg_Larva);
	set_train_distribution(train_distribution, UnitTypes::Zerg_Zergling);
	CostPerMinute cost_per_minute = train_distribution.cost_per_minute(building_manager.building_count(UnitTypes::Zerg_Hatchery));
	return (is_defending_rush() || is_contained() || tactics_manager.is_opponent_army_too_large(cost_per_minute.supply));
}

void ZergStrategy::drone_scout_for_zergling_rush()
{
	if (information_manager.enemy_building_seen() ||
		tactics_manager.enemy_start_base() != nullptr ||
		tactics_manager.possible_enemy_start_bases().size() <= 1) {
		worker_manager.stop_scouting();
	} else {
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) > 0) {
			const InformationUnit* spawning_pool = nullptr;
			for (auto& unit : information_manager.my_units()) {
				if (unit->type == UnitTypes::Zerg_Spawning_Pool) {
					spawning_pool = unit;
					break;
				}
			}
			if (spawning_pool != nullptr) {
				int zergling_spawn_time = spawning_pool->complete_frame() + UnitTypes::Zerg_Zergling.buildTime();
				Position start_position = base_state.start_base()->Center();
				std::vector<const BWEM::Base*> bases = tactics_manager.possible_enemy_start_bases();
				std::vector<Position> destinations;
				for (auto& base : bases) destinations.push_back(base->Center());
				Position common_position = path_finder.first_common_path_position(start_position, destinations);
				int zergling_at_common_position_time = zergling_spawn_time + int(1.1 * ground_distance(start_position, common_position) / UnitTypes::Zerg_Zergling.topSpeed() + 0.5);
				int drone_scout_time = INT_MAX;
				for (auto& base : base_state.undiscovered_starting_bases()) {
					Position position = base->Center();
					int distance = ground_distance(start_position, position);
					if (distance >= 0) {
						int frames = int(1.1 * distance / UnitTypes::Zerg_Drone.topSpeed() + 0.5);
						drone_scout_time = std::min(drone_scout_time, frames);
					}
				}
				int time_left = zergling_at_common_position_time - Broodwar->getFrameCount();
				if (drone_scout_time < INT_MAX && time_left < drone_scout_time) {
					worker_manager.send_initial_scout();
				}
			}
		}
	}
}

bool ZergStrategy::morphing_building_hp_at_least(UnitType building_type,int hp)
{
	return std::any_of(Broodwar->self()->getUnits().begin(),
					   Broodwar->self()->getUnits().end(),
					   [=](Unit unit) {
						   return (!unit->isCompleted() && unit->getType() == building_type && unit->getHitPoints() >= hp);
					   });
}

bool ZergStrategy::counter_air_harass_with_spore()
{
	bool result = false;
	if (information_manager.enemy_exists(UnitTypes::Protoss_Corsair) ||
		information_manager.enemy_exists(UnitTypes::Protoss_Scout) ||
		information_manager.enemy_exists(UnitTypes::Protoss_Stargate) ||
		information_manager.enemy_exists(UnitTypes::Terran_Wraith) ||
		information_manager.enemy_exists(UnitTypes::Terran_Valkyrie) ||
		information_manager.enemy_exists(UnitTypes::Terran_Starport) ||
		information_manager.enemy_exists(UnitTypes::Terran_Control_Tower)) {
		result = build_spore_in_each_base();
	}
	return result;
}

bool ZergStrategy::counter_mutalisks_with_spore()
{
	bool result = false;
	if (!mutalisk_transition_during_zergling_rush_in_progress() &&
		(information_manager.enemy_completed_exists(UnitTypes::Zerg_Spire) ||
		 information_manager.enemy_exists(UnitTypes::Zerg_Mutalisk) ||
		 information_manager.enemy_exists(UnitTypes::Zerg_Scourge))) {
		result = build_spore_in_each_base();
	}
	return result;
}

bool ZergStrategy::build_spore_in_each_base()
{
	bool result = false;
	for (auto& base : base_state.controlled_bases()) {
		building_manager.request_base_defense_spore_colony(base);
		if (contains(building_placement_manager.creep_colony_positions(), base) &&
			!building_manager.base_defense_spore_colony_planned_or_exists(base)) {
			result = true;
		}
	}
	if (result) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Evolution_Chamber, 1, true);
	}
	return result;
}

bool ZergStrategy::mutalisk_transition_during_zergling_rush_in_progress()
{
	return ((building_manager.building_count_including_warping(UnitTypes::Zerg_Lair) > 0 ||
			 building_manager.building_count_including_planned(UnitTypes::Zerg_Spire) > 0) &&
			training_manager.unit_count(UnitTypes::Zerg_Mutalisk) == 0);
}

void ZergStrategy::build_mutalisks_if_needed_for_zergling_rush()
{
	int zergling_count = training_manager.unit_count_built(UnitTypes::Zerg_Zergling);
	if (((zergling_count >= 50 && opponent_only_has_flying_buildings()) ||
		 zergling_count >= 100) &&
		training_manager.unit_count(UnitTypes::Zerg_Mutalisk) < 6) {
		worker_manager.set_force_refinery_workers(true);
		worker_manager.set_limit_refinery_workers(-1);
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1, true);
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1, true);
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1, true);
		if (training_manager.unit_count(UnitTypes::Zerg_Drone) < 10) {
			training_manager.larva_train_distribution().clear();
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
		} else if (building_manager.building_exists(UnitTypes::Zerg_Spire)) {
			training_manager.larva_train_distribution().clear();
			if (Broodwar->self()->gas() >= UnitTypes::Zerg_Mutalisk.gasPrice()) {
				training_manager.larva_train_distribution().set(UnitTypes::Zerg_Mutalisk, 1.0);
			} else if (Broodwar->self()->minerals() >= UnitTypes::Zerg_Mutalisk.mineralPrice() + UnitTypes::Zerg_Zergling.mineralPrice()) {
				training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
			}
		}
	}
}

void ZergStrategy::mode_opening()
{
	training_manager.set_automatic_supply(false);
	training_manager.set_worker_cut(true);
	training_manager.set_prioritize_training(false);
	training_manager.set_requested_lurker_count(0);
	training_manager.larva_train_distribution().clear();
	worker_manager.set_scout_wait_at_natural(false);
	attack_minimum_ = 8;
	if (opening_ == kZvZ_4Pool) opening_ZvX_4pool();
	else if (opening_ == kZvZ_5Pool) opening_ZvX_5pool();
	else if (opening_ == kZvZ_2HatchLing) opening_ZvX_2hatchling();
	else if (opening_ == kZvZ_3HatchLing) opening_ZvX_3hatchling();
	else if (opening_ == kZvZ_9HatchLing) opening_ZvX_9hatchling();
	else if (opening_ == kZvZ_10HatchLing) opening_ZvX_10hatchling();
	else if (opening_ == kZvZ_9PoolSpire) opening_ZvZ_9poolspire();
	else if (opening_ == kZvZ_9Gas9Pool) opening_ZvZ_9gas9pool();
	else if (opening_ == kZvZ_9Gas10Pool) opening_ZvZ_9gas10pool();
	else if (opening_ == kZvZ_11Gas10Pool) opening_ZvZ_11gas10pool();
	else if (opening_ == kZvZ_OverPool9Gas) opening_ZvZ_overpool9gas();
	else if (opening_ == kZvZ_12Pool) opening_ZvZ_12pool();
	else if (opening_ == KZvZ_12PoolMain) opening_ZvZ_12poolmain();
	else if (opening_ == kZvT_4Pool) opening_ZvX_4pool();
	else if (opening_ == kZvT_5Pool) opening_ZvX_5pool();
	else if (opening_ == kZvT_7Pool) opening_ZvT_7pool();
	else if (opening_ == kZvT_2HatchLing) opening_ZvX_2hatchling();
	else if (opening_ == kZvT_3HatchLing) opening_ZvX_3hatchling();
	else if (opening_ == kZvT_9HatchLing) opening_ZvX_9hatchling();
	else if (opening_ == kZvT_2HatchMuta_12Hatch) opening_ZvT_2hatchmuta(false);
	else if (opening_ == kZvT_2HatchMuta_12Pool) opening_ZvT_2hatchmuta(true);
	else if (opening_ == kZvT_2_5HatchMuta) opening_ZvT_2_5hatchmuta();
	else if (opening_ == kZvT_3HatchMuta) opening_ZvT_3hatchmuta();
	else if (opening_ == kZvT_13PoolMuta) opening_ZvT_13poolmuta();
	else if (opening_ == kZvT_MutaHydra_12Hatch) opening_ZvT_mutahydra(false);
	else if (opening_ == kZvT_MutaHydra_9Pool) opening_ZvT_mutahydra(true);
	else if (opening_ == kZvT_3HatchLurker) opening_ZvT_3hatchlurker();
	else if (opening_ == kZvP_5Pool) opening_ZvX_5pool();
	else if (opening_ == kZvP_2HatchLing) opening_ZvX_2hatchling();
	else if (opening_ == kZvP_3HatchLing) opening_ZvX_3hatchling();
	else if (opening_ == kZvP_9HatchLing) opening_ZvX_9hatchling();
	else if (opening_ == kZvP_10HatchLing) opening_ZvX_10hatchling();
	else if (opening_ == kZvP_2HatchMuta) opening_ZvP_2hatchmuta();
	else if (opening_ == kZvP_3HatchMuta) opening_ZvP_3hatchmuta();
	else if (opening_ == kZvP_2HatchHydra) opening_ZvP_2hatchhydra();
	else if (opening_ == kZvP_9734) opening_ZvP_9734();
	else if (opening_ == kZvP_3HatchLurker) opening_ZvP_3hatchlurker();
	else if (opening_ == kZvP_NeoSauron) opening_ZvP_neosauron();
	else if (opening_ == kZvP_4HatchBeforeGas) opening_ZvP_4hatchbeforegas();
	else if (opening_ == kZvP_5HatchBeforeGas) opening_ZvP_5hatchbeforegas();
	else if (opening_ == kZvP_6Hatch) opening_ZvP_6hatch();
	else if (opening_ == kZvU_4Pool) opening_ZvX_4pool();
	else if (opening_ == kZvU_5Pool) opening_ZvX_5pool();
	else if (opening_ == kZvU_2HatchLing) opening_ZvX_2hatchling();
	else if (opening_ == kZvU_3HatchLing) opening_ZvX_3hatchling();
	else if (opening_ == kZvU_9HatchLing) opening_ZvX_9hatchling();
	else if (opening_ == kZvU_10HatchLing) opening_ZvX_10hatchling();
	else if (opening_ == kZvU_9PoolSpeed) opening_ZvU_9poolspeed();
	else if (opening_ == kZvU_11Pool) opening_ZvU_11pool();
	
	scout_for_cannon_rush_if_needed();
}

void ZergStrategy::mode_defend_one_base_protoss()
{
	if (!one_base_protoss_handled_) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Zerg_Hatchery, 2, 2);
		building_manager.cancel_buildings_of_type(UnitTypes::Zerg_Lair);
		building_manager.cancel_buildings_of_type(UnitTypes::Zerg_Spire);
		attacking_ = false;
		one_base_protoss_handled_ = true;
	}
	bool cannons_near_natural = false;
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush &&
		base_state.natural_base() != nullptr) {
		for (auto enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->type == UnitTypes::Protoss_Photon_Cannon &&
				calculate_distance(UnitTypes::Protoss_Photon_Cannon, enemy_unit->position,
								   UnitTypes::Zerg_Hatchery, base_state.natural_base()->Center()) <= 320) {
				cannons_near_natural = true;
				break;
			}
		}
	}
	if (cannons_near_natural) {
		building_manager.cancel_expansion_hatcheries();
		attack_minimum_ = 1;
	}
	training_manager.larva_train_distribution().clear();
	int supply = opening_supply_count();
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 9 && training_manager.unit_count(UnitTypes::Zerg_Overlord) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 9 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		worker_manager.send_initial_scout();
	}
	if (supply >= 11) {
		if (!cannons_near_natural &&
			building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) < 2) {
			if (!building_manager.request_bases(2)) {
				mode_ = Mode::Main_HydraLurkerLing;
				return;
			}
		} else if (training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) >= 6) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 2);
		}
	}
	if (supply >= 14 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2 &&
		training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) >= 6) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (supply >= 16 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 17) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hydralisk_Den, 1);
		training_manager.set_automatic_supply(true);
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Hydralisk) >= 2) {
		building_manager.request_upgrade(!cannons_near_natural ? UpgradeTypes::Muscular_Augments : UpgradeTypes::Grooved_Spines);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Hydralisk) >= 8) {
		building_manager.request_upgrade(!cannons_near_natural ? UpgradeTypes::Grooved_Spines : UpgradeTypes::Muscular_Augments);
	}
	int wanted_sunken_count = std::min(4, (tactics_manager.enemy_army_supply() - tactics_manager.defense_supply()) / (4 * 2));
	if ((Broodwar->getFrameCount() % 25) == 0 &&
		wanted_sunken_count > 0 &&
		wanted_sunken_count > opening_sunkens_placed_) {
		if (opening_sunkens_placed_ == -1) {
			opening_sunkens_placed_ = place_defense_creep_colonies(base_state.natural_base(), wanted_sunken_count);
		} else {
			opening_sunkens_placed_ += place_defense_creep_colonies(base_state.natural_base(), wanted_sunken_count - opening_sunkens_placed_);
		}
	}
	if (wanted_sunken_count > 0) {
		int requested_count = std::min(std::min(wanted_sunken_count, opening_sunkens_placed_), building_manager.building_count_including_planned(UnitTypes::Zerg_Sunken_Colony) + 1);
		int reserve_count = building_manager.building_count_including_planned(UnitTypes::Zerg_Sunken_Colony) - building_manager.building_exists(UnitTypes::Zerg_Sunken_Colony);
		if (reserve_count > 0) {
			spending_manager.reserve_resources(MineralGas(UnitTypes::Zerg_Sunken_Colony) * reserve_count);
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, requested_count, true);
	}
	int wanted_drone_count;
	if (building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) < 1) {
		wanted_drone_count = 9;
	} else if (building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) < 2) {
		wanted_drone_count = 11;
	} else if (!building_manager.building_exists(UnitTypes::Zerg_Extractor) ||
			   training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Zergling) < 6) {
		wanted_drone_count = 10;
	} else {
		wanted_drone_count = 15;
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor) &&
		training_manager.unit_count_completed(UnitTypes::Zerg_Drone) >= 6 &&
		Broodwar->self()->gas() < 200) {
		worker_manager.set_force_refinery_workers(true);
		worker_manager.set_limit_refinery_workers(2);
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) < wanted_drone_count) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	} else if (building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den) &&
			   Broodwar->self()->gas() >= UnitTypes::Zerg_Hydralisk.gasPrice()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
	} else if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	attack_check_condition();
	counter_air_harass_with_spore();
	if (training_manager.unit_count(UnitTypes::Zerg_Hydralisk) >= 12) {
		mode_ = Mode::Main_HydraLurkerLing;
	}
}

void ZergStrategy::mode_defend_proxy_rax()
{
	if (!proxy_rax_handled_) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Zerg_Hatchery, 1, 1);
		building_manager.cancel_buildings_of_type(UnitTypes::Zerg_Lair);
		building_manager.cancel_buildings_of_type(UnitTypes::Zerg_Spire);
		attacking_ = false;
		proxy_rax_handled_ = true;
	}
	training_manager.larva_train_distribution().clear();
	int supply = opening_supply_count();
	if (supply >= 9 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 2) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	}
	if (supply >= 11 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 2);
	}
	if (supply >= 12 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) >= 1 &&
		building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, 1);
	}
	if (building_manager.building_exists(UnitTypes::Zerg_Extractor)) {
		worker_manager.set_force_refinery_workers(true);
		worker_manager.set_limit_refinery_workers(2);
	}
	if (supply >= 14) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	}
	if (supply >= 16 &&
		training_manager.unit_count(UnitTypes::Zerg_Overlord) < 3) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Overlord, 1.0);
	}
	if (supply >= 17) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hydralisk_Den, 1);
		training_manager.set_automatic_supply(true);
	}

	if (training_manager.unit_count_completed(UnitTypes::Zerg_Hydralisk) >= 2) {
		building_manager.request_upgrade(UpgradeTypes::Muscular_Augments);
	}
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Hydralisk) >= 8) {
		building_manager.request_upgrade(UpgradeTypes::Grooved_Spines);
	}
	if (tactics_manager.enemy_army_supply() > tactics_manager.defense_supply()) {
		if (opening_sunkens_placed_ == -1) {
			opening_sunkens_placed_ = place_defense_creep_colonies(base_state.start_base(), 1);
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, opening_sunkens_placed_);
	}
	int wanted_drone_count = 10;
	if (building_manager.building_count_including_warping(UnitTypes::Zerg_Spawning_Pool) < 1) {
		wanted_drone_count++;
		if (building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery) < 2) {
			wanted_drone_count++;
		}
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) < wanted_drone_count) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	} else if (building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den) &&
			   Broodwar->self()->gas() >= UnitTypes::Zerg_Hydralisk.gasPrice()) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Hydralisk, 1.0);
	} else if (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
	}
	attack_check_condition();
	if (training_manager.unit_count(UnitTypes::Zerg_Hydralisk) >= 12) {
		mode_ = Mode::Main_HydraLurkerLing;
	}
}

void ZergStrategy::mode_defend_fast_pool()
{
	if (!fast_pool_handled_) {
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1 &&
			building_manager.building_count_including_planned(UnitTypes::Zerg_Sunken_Colony) >= fast_pool_sunken_count_) {
			building_manager.cancel_extra_buildings_of_type(UnitTypes::Zerg_Hatchery, 2, 2);
		} else {
			building_manager.cancel_extra_buildings_of_type(UnitTypes::Zerg_Hatchery, 1, 1);
		}
		building_manager.cancel_expansion_hatcheries();
		building_manager.cancel_buildings_of_type(UnitTypes::Zerg_Extractor);
		building_manager.cancel_buildings_of_type(UnitTypes::Zerg_Lair);
		building_manager.cancel_buildings_of_type(UnitTypes::Zerg_Spire);
		attacking_ = false;
		fast_pool_handled_ = true;
	}
	training_manager.larva_train_distribution().clear();
	int wanted_drone_count = 5 + fast_pool_sunken_count_;
	if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) >= 1) wanted_drone_count--;
	int non_spore_creep_colonies = building_manager.building_count_including_planned(UnitTypes::Zerg_Creep_Colony) - building_manager.building_count_including_planned(UnitTypes::Zerg_Spore_Colony);
	wanted_drone_count -= non_spore_creep_colonies;
	if (is_enemy_offense_larger_than_defense()) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, std::min(fast_pool_sunken_count_, non_spore_creep_colonies), true);
	}
	if (training_manager.unit_count(UnitTypes::Zerg_Drone) < wanted_drone_count) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
	} else if (building_manager.building_count_including_planned(UnitTypes::Zerg_Spawning_Pool) == 0) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	} else {
		if (fast_pool_sunken_count_ >= 1) building_manager.request_base_defense_sunken_colony(base_state.start_base());
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, fast_pool_sunken_count_, true);
		if (building_manager.building_count_including_planned(UnitTypes::Zerg_Hatchery) >= 2) {
			wanted_drone_count = (building_manager.building_count(UnitTypes::Zerg_Hatchery) * 7) / 2 + 1;
		}
		
		bool enough_drones = training_manager.unit_count(UnitTypes::Zerg_Drone) >= wanted_drone_count;
		bool enough_sunkens = building_manager.building_count_including_planned(UnitTypes::Zerg_Sunken_Colony) >= fast_pool_sunken_count_;
		
		if (!enough_drones) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
		} else {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Zergling, 1.0);
		}
		
		if (enough_drones && enough_sunkens) {
			if (spending_manager.spendable().minerals >= UnitTypes::Zerg_Hatchery.mineralPrice() + 3 * UnitTypes::Zerg_Drone.mineralPrice() ||
				(spending_manager.spendable().minerals >= UnitTypes::Zerg_Hatchery.mineralPrice() && !tactics_manager.is_opponent_army_too_large())) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, 1 + building_manager.building_count(UnitTypes::Zerg_Hatchery));
			}
		}
		
		if (enough_sunkens &&
			training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 6) {
			attacking_ = true;
		}
		if (!counter_mutalisks_with_spore()) {
			build_mutalisks_if_needed_for_zergling_rush();
		}
		if (base_state.mineable_mineral_count() < 1500) {
			building_manager.request_next_base();
		}
	}
	training_manager.set_automatic_supply(true);
	worker_manager.set_limit_refinery_workers(0);
}

void ZergStrategy::mode_main_muta_hydra_lurker_ling(bool mutalisk_harass)
{
	bool mutalisk_phase;
	bool prepare_for_hydralisk_phase;
	bool full_lurkers;
	bool add_lurkers;
	if (opponent_model.enemy_race() == Races::Terran) {
		if (!is_terran_bio()) {
			// ZvT Mech
			mutalisk_phase = training_manager.unit_count(UnitTypes::Zerg_Mutalisk) < 12;
			prepare_for_hydralisk_phase = training_manager.unit_count(UnitTypes::Zerg_Mutalisk) >= 9;
			full_lurkers = false;
			add_lurkers = false;
		} else {
			// ZvT Bio
			mutalisk_phase = mutalisk_harass && training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) < 12;
			prepare_for_hydralisk_phase = !mutalisk_harass || training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) >= 9;
			full_lurkers = true;
			add_lurkers = false;
		}
	} else {
		// ZvP
		mutalisk_phase = mutalisk_harass && training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) < 12;
		prepare_for_hydralisk_phase = !mutalisk_harass || training_manager.unit_count_built_or_in_progress(UnitTypes::Zerg_Mutalisk) >= 9;
		full_lurkers = false;
		add_lurkers = true;
	}
	
	UnitType type = UnitTypes::None;
	bool mutalisk_available = building_manager.building_exists(UnitTypes::Zerg_Spire);
	bool scourge_available = mutalisk_available;
	bool hydralisk_available = building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den);
	bool zergling_available = building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool);
	bool lurker_available = hydralisk_available && Broodwar->self()->hasResearched(TechTypes::Lurker_Aspect);
	bool defiler_available = building_manager.building_exists(UnitTypes::Zerg_Defiler_Mound);
	int required_scourge_count = (opponent_model.enemy_race() == Races::Terran) ? calculate_number_of_scourges_required() : 0;
	if (mutalisk_available && mutalisk_phase) {
		type = UnitTypes::Zerg_Mutalisk;
	} else if (scourge_available && training_manager.unit_count(UnitTypes::Zerg_Scourge) < required_scourge_count) {
		type = UnitTypes::Zerg_Scourge;
	} else if (defiler_available && full_lurkers && training_manager.unit_count(UnitTypes::Zerg_Defiler) < 2) {
		type = UnitTypes::Zerg_Defiler;
	} else if (lurker_available && full_lurkers) {
		type = UnitTypes::Zerg_Lurker;
	} else if (hydralisk_available) {
		type = UnitTypes::Zerg_Hydralisk;
	} else if (zergling_available) {
		type = UnitTypes::Zerg_Zergling;
	}
	
	bool prioritize_units = main_boilerplate(type);
	building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, base_state.controlled_geyser_count());
	if (lurker_available && add_lurkers) training_manager.set_requested_lurker_count(4);
	
	if (!building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	} else if ((mutalisk_phase || required_scourge_count >= 4) &&
			   !building_manager.building_exists(UnitTypes::Zerg_Spire)) {
		if (!building_manager.building_exists(UnitTypes::Zerg_Lair)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
		} else {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
		}
	} else if (prepare_for_hydralisk_phase) {
		if (full_lurkers &&
			building_manager.building_count_including_planned(UnitTypes::Zerg_Lair) == 0) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
		} else {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hydralisk_Den, 1);
			if (add_lurkers &&
				building_manager.building_count_including_planned(UnitTypes::Zerg_Hydralisk_Den) >= 1 &&
				done_or_in_progress(UpgradeTypes::Muscular_Augments) &&
				done_or_in_progress(UpgradeTypes::Grooved_Spines)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
			}
		}
	}
	
	bool hydra_tech_done = true;
	if (!building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den)) hydra_tech_done = false;
	else if (!full_lurkers &&
		(!done_or_in_progress(UpgradeTypes::Muscular_Augments) || !done_or_in_progress(UpgradeTypes::Grooved_Spines))) hydra_tech_done = false;
	else if ((full_lurkers || add_lurkers) && !done_or_in_progress(TechTypes::Lurker_Aspect)) hydra_tech_done = false;
	
	if (!counter_air_harass_with_spore()) {
		border_sunken_colonies((!attacking_ && prioritize_units) ? 2 : 0);
		if (hydra_tech_done) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Evolution_Chamber, 1);
			
			if (building_manager.building_exists(UnitTypes::Zerg_Evolution_Chamber) &&
				base_state.mining_base_count() >= bases_needed_for_tech_to_hive()) {
				tech_to_hive();
				if (tech_to_hive_done()) {
					mode_ = Mode::Main_UltraLing;
				}
			}
		}
	}
	
	if (full_lurkers) {
		if (building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den) &&
			building_manager.building_exists(UnitTypes::Zerg_Lair)) {
			building_manager.request_research(TechTypes::Lurker_Aspect);
		}
	} else {
		hydra_upgrades(add_lurkers);
	}
	if (opponent_model.cloaked_or_mine_present() &&
		!done_or_in_progress(UpgradeTypes::Pneumatized_Carapace)) {
		building_manager.request_upgrade(UpgradeTypes::Pneumatized_Carapace);
	} else if (hydra_tech_done) {
		attack_and_carapace_upgrades({UpgradeTypes::Zerg_Missile_Attacks, UpgradeTypes::Zerg_Carapace});
	}
	attack_check_condition();
}

void ZergStrategy::mode_main_ultra_ling()
{
	UnitType type = UnitTypes::None;
	bool mutalisk_available = building_manager.building_exists(UnitTypes::Zerg_Spire);
	bool scourge_available = mutalisk_available;
	bool lurker_available = building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den) && Broodwar->self()->hasResearched(TechTypes::Lurker_Aspect);
	bool zergling_available = building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool);
	bool defiler_available = building_manager.building_exists(UnitTypes::Zerg_Defiler_Mound);
	bool ultralisk_available = building_manager.building_exists(UnitTypes::Zerg_Ultralisk_Cavern);
	if (scourge_available && training_manager.unit_count(UnitTypes::Zerg_Scourge) < calculate_number_of_scourges_required()) {
		type = UnitTypes::Zerg_Scourge;
	} else if ((opponent_only_has_flying_buildings() && training_manager.unit_count(UnitTypes::Zerg_Mutalisk) < 6) ||
			   (opponent_model.enemy_race() == Races::Terran && Broodwar->self()->supplyUsed() >= 2 * 196)) {
		type = UnitTypes::Zerg_Mutalisk;
	} else if (defiler_available && training_manager.unit_count(UnitTypes::Zerg_Defiler) < 2) {
		type = UnitTypes::Zerg_Defiler;
	} else if (ultralisk_available) {
		type = UnitTypes::Zerg_Ultralisk;
	} else if (lurker_available) {
		type = UnitTypes::Zerg_Lurker;
	} else if (mutalisk_available) {
		type = UnitTypes::Zerg_Mutalisk;
	} else if (zergling_available) {
		type = UnitTypes::Zerg_Zergling;
	}
	
	bool prioritize_units = main_boilerplate(type);
	building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, base_state.controlled_geyser_count());
	
	if (lurker_available && training_manager.unit_count(UnitTypes::Zerg_Defiler) > 2) {
		training_manager.set_requested_lurker_count(-1);
	}
	
	if (!counter_air_harass_with_spore()) {
		border_sunken_colonies((!attacking_ && prioritize_units) ? 2 : 0);
		tech_to_hive();
		if (tech_to_hive_done()) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Evolution_Chamber, 1);
			if (building_manager.building_exists(UnitTypes::Zerg_Evolution_Chamber) &&
				building_manager.building_exists(UnitTypes::Zerg_Ultralisk_Cavern) &&
				training_manager.unit_count(UnitTypes::Zerg_Ultralisk) >= 2) {
				if (!done_or_in_progress(UpgradeTypes::Chitinous_Plating)) {
					building_manager.request_upgrade(UpgradeTypes::Chitinous_Plating);
				} else if (!done_or_in_progress(UpgradeTypes::Anabolic_Synthesis)) {
					building_manager.request_upgrade(UpgradeTypes::Anabolic_Synthesis);
				} else if (building_manager.building_count_including_planned(UnitTypes::Zerg_Evolution_Chamber) < 2) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Evolution_Chamber, 2);
				} else {
					building_manager.request_upgrade(UpgradeTypes::Metasynaptic_Node);
				}
			}
		}
	}
	
	if (opponent_model.cloaked_or_mine_present() &&
		!done_or_in_progress(UpgradeTypes::Pneumatized_Carapace)) {
		building_manager.request_upgrade(UpgradeTypes::Pneumatized_Carapace);
	} else {
		attack_and_carapace_upgrades({UpgradeTypes::Zerg_Melee_Attacks, UpgradeTypes::Zerg_Carapace});
	}
	attack_check_condition();
}

void ZergStrategy::mode_main_ZvZ(bool lategame)
{
	UnitType type = UnitTypes::None;
	bool mutalisk_available = building_manager.building_exists(UnitTypes::Zerg_Spire);
	bool zergling_available = building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool);
	if (mutalisk_available) {
		type = UnitTypes::Zerg_Mutalisk;
	} else if (zergling_available) {
		type = UnitTypes::Zerg_Zergling;
	}
	
	if (!lategame) {
		training_manager.larva_train_distribution().clear();
		
		int max_workers = worker_manager.determine_max_workers();
		int drone_count = training_manager.unit_count(UnitTypes::Zerg_Drone);
		int hatch_count = building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery);
		
		TrainDistribution train_distribution(UnitTypes::Zerg_Larva);
		set_train_distribution(train_distribution, type);
		CostPerMinute cost_per_minute = train_distribution.cost_per_minute(building_manager.building_count(UnitTypes::Zerg_Hatchery));
		
		int wanted_macro_hatch_count = (base_state.mining_base_count() >= 2) ? 4 : 1;
		double max_workers_per_mineral = (base_state.mining_base_count() >= 2) ? kAverageWorkersPerMineral : kExpandWorkersPerMineral;
		
		bool prioritize_units = is_contained() || tactics_manager.is_opponent_army_too_large(cost_per_minute.supply) || (building_manager.building_exists(UnitTypes::Zerg_Spire) && training_manager.unit_count(UnitTypes::Zerg_Mutalisk) <= information_manager.enemy_count(UnitTypes::Zerg_Mutalisk));
		training_manager.set_prioritize_training(prioritize_units);
		
		if (drone_count < 5) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
			training_manager.set_prioritize_training(true);
		} else if (prioritize_units) {
			set_train_distribution(type);
		} else if (drone_count < max_workers &&
				   worker_manager.average_workers_per_mineral() < max_workers_per_mineral) {
			training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
			training_manager.set_prioritize_training(true);
		} else {
			set_train_distribution(type);
		}
		
		set_extractor_workers();
		
		int incomplete_hatch_count = hatch_count - building_manager.building_count(UnitTypes::Zerg_Hatchery);
		int wait_for_minerals = UnitTypes::Zerg_Hatchery.mineralPrice() + incomplete_hatch_count * (UnitTypes::Zerg_Hatchery.mineralPrice() / 2);
		if (spending_manager.spendable().minerals >= wait_for_minerals &&
			hatch_count < wanted_macro_hatch_count) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, hatch_count + 1);
		}
		
		if (!prioritize_units &&
			hatch_count >= wanted_macro_hatch_count &&
			worker_manager.average_workers_per_mineral() >= max_workers_per_mineral) {
			building_manager.request_next_base();
		}
		if (Broodwar->self()->minerals() + base_state.mineable_mineral_count() < 1500) {
			building_manager.request_next_base(true);
		}
		border_sunken_colonies((!attacking_ && prioritize_units) ? 2 : 0);
		if (base_state.mining_base_count() > 2) mode_ = Mode::Main_ZvZLateGame;
	} else {
		main_boilerplate(type);
	}
	building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Extractor, base_state.controlled_geyser_count());
	
	if ((opponent_model.cloaked_present() || expect_lurkers()) &&
		!done_or_in_progress(UpgradeTypes::Pneumatized_Carapace)) {
		building_manager.request_upgrade(UpgradeTypes::Pneumatized_Carapace);
	} else {
		attack_and_carapace_upgrades({UpgradeTypes::Zerg_Melee_Attacks,UpgradeTypes::Zerg_Flyer_Carapace,UpgradeTypes::Zerg_Carapace,UpgradeTypes::Zerg_Flyer_Attacks});
	}
	
	if (!building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	} else if (!done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	} else if (!building_manager.building_exists(UnitTypes::Zerg_Lair)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	} else if (!building_manager.building_exists(UnitTypes::Zerg_Spire)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
	} else if (!building_manager.building_exists(UnitTypes::Zerg_Evolution_Chamber)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Evolution_Chamber, 1);
	} else if (base_state.mining_base_count() >= bases_needed_for_tech_to_hive()) {
		tech_to_hive();
		if (tech_to_hive_done()) {
			mode_ = Mode::Main_UltraLing;
		}
	}
	
	attack_check_condition();
}

bool ZergStrategy::main_boilerplate(UnitType type)
{
	training_manager.larva_train_distribution().clear();
	training_manager.set_requested_lurker_count(0);
	
	int max_workers = worker_manager.determine_max_workers();
	int drone_count = training_manager.unit_count(UnitTypes::Zerg_Drone);
	int hatch_count = building_manager.building_count_including_warping(UnitTypes::Zerg_Hatchery);
	
	double hatch_per_base = 2.0;
	if (opponent_model.enemy_race() == Races::Protoss) {
		if (base_state.mining_base_count() >= 3) hatch_per_base = 3.0;
	} else if (opponent_model.enemy_race() == Races::Terran) {
		hatch_per_base = 1.0;
		if (base_state.mining_base_count() >= 2) hatch_per_base = 4.0 / 3.0;
		if (base_state.mining_base_count() >= 4) hatch_per_base = 2.25;
	}
	int wanted_macro_hatch_count = std::min(10, int(hatch_per_base * base_state.mining_base_count() + 0.5));
	
	bool prioritize_units = is_contained() || (tactics_manager.defense_supply() < (2 * tactics_manager.enemy_army_supply()) / 3);
	training_manager.set_prioritize_training(prioritize_units);
	// @
	/*FILE *f = fopen("bwapi-data\\write\\zergmacro.txt", "a");
	fprintf(f, "%d (%s) | %g %d %d | %d\n",
			Broodwar->getFrameCount(),
			frame_to_string(Broodwar->getFrameCount()).c_str(),
			hatch_per_base,
			hatch_count,
			wanted_macro_hatch_count,
			int(prioritize_units));
	fclose(f);*/
	// /@
	
	if (drone_count < 5) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
		training_manager.set_prioritize_training(true);
	} else if (prioritize_units) {
		set_train_distribution(type);
	} else if (drone_count < max_workers &&
			   worker_manager.average_workers_per_mineral() < kAverageWorkersPerMineral) {
		training_manager.larva_train_distribution().set(UnitTypes::Zerg_Drone, 1.0);
		training_manager.set_prioritize_training(true);
	} else {
		set_train_distribution(type);
	}
	
	set_extractor_workers(type == UnitTypes::Zerg_Hydralisk);
	
	bool allow_expand = !is_contained();
	int incomplete_hatch_count = hatch_count - building_manager.building_count(UnitTypes::Zerg_Hatchery);
	int wait_for_minerals = UnitTypes::Zerg_Hatchery.mineralPrice() + incomplete_hatch_count * (UnitTypes::Zerg_Hatchery.mineralPrice() / 2);
	if (spending_manager.spendable().minerals >= wait_for_minerals &&
		hatch_count < wanted_macro_hatch_count) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hatchery, hatch_count + 1);
	}

	if (allow_expand &&
		hatch_count >= wanted_macro_hatch_count &&
		base_state.mining_base_count() < 5 &&
		worker_manager.average_workers_per_mineral() >= kExpandWorkersPerMineral) {
		building_manager.request_next_base();
	}
	if (Broodwar->self()->minerals() + base_state.mineable_mineral_count() < 1500) {
		building_manager.request_next_base(true);
	}
	
	return prioritize_units;
}

void ZergStrategy::set_extractor_workers(bool hydralisks)
{
	int completed_drone_count = training_manager.unit_count_completed(UnitTypes::Zerg_Drone);
	int hatch_count = building_manager.building_count(UnitTypes::Zerg_Hatchery);
	if (completed_drone_count >= 6) {
		worker_manager.set_force_refinery_workers(true);
		if (hydralisks) {
			worker_manager.set_limit_refinery_workers((completed_drone_count + 3) / 6);
		} else {
			worker_manager.set_limit_refinery_workers((4 * completed_drone_count + 5) / 10);
		}
	}
}

void ZergStrategy::border_sunken_colonies(int count)
{
	Border border(base_state.controlled_areas());
	const std::set<const BWEM::Area*>& border_areas = border.inside_areas();
	
	std::vector<const BWEM::Base*> remove_bases;
	for (auto& entry : creep_colonies_placed_) {
		if (!contains(border_areas, entry.first->GetArea())) {
			remove_bases.push_back(entry.first);
		}
	}
	for (auto& base : remove_bases) creep_colonies_placed_.erase(base);
	
	remove_elements_in_place(building_placement_manager.specific_positions_for_type(UnitTypes::Zerg_Creep_Colony),
							 [&border_areas](TilePosition tile_position){
		Position position = center_position(tile_position);
		const BWEM::Area* area = area_at(position);
		return !contains(border_areas, area);
	});
	
	for (auto& base : base_state.controlled_bases()) {
		const BWEM::Area* area = base->GetArea();
		if (contains(border_areas, area)) {
			bool needs_init = !contains(creep_colonies_placed_, base);
			int& placed = creep_colonies_placed_[base];
			if (needs_init) {
				std::set<TilePosition> exclude_tile_positions;
				for (auto& entry : building_placement_manager.creep_colony_positions()) {
					exclude_tile_positions.insert(entry.second);
				}
				auto& specific_tile_positions = building_placement_manager.specific_positions_for_type(UnitTypes::Zerg_Creep_Colony);
				for (auto& information_unit : information_manager.my_units()) {
					if ((information_unit->type == UnitTypes::Zerg_Creep_Colony ||
						 information_unit->type == UnitTypes::Zerg_Sunken_Colony) &&
						information_unit->area == area &&
						!contains(exclude_tile_positions, information_unit->tile_position())) {
						placed++;
						specific_tile_positions.push_back(information_unit->tile_position());
					}
				}
			}
			if (placed < count) {
				place_defense_creep_colonies(base, count - placed);
			}
		}
	}
	
	int existing_sunken_colony_count = building_manager.building_count(UnitTypes::Zerg_Sunken_Colony);
	int additional_sunken_colony_count = 0;
	for (auto& tile_position : building_placement_manager.specific_positions_for_type(UnitTypes::Zerg_Creep_Colony)) {
		if (can_place_defense_creep_colony(tile_position)) {
			additional_sunken_colony_count++;
		}
	}
	int drone_count = training_manager.unit_count(UnitTypes::Zerg_Drone);
	int creep_colony_count = building_manager.building_count_including_warping(UnitTypes::Zerg_Creep_Colony);
	additional_sunken_colony_count = std::min(additional_sunken_colony_count, drone_count + creep_colony_count - 10);
	int total_sunken_count = existing_sunken_colony_count + additional_sunken_colony_count;
	building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Sunken_Colony, total_sunken_count, true);
}

int ZergStrategy::place_defense_creep_colonies(const BWEM::Base* base,int requested_count)
{
	int count = building_placement_manager.calculate_defense_creep_colony_positions(base, requested_count);
	creep_colonies_placed_[base] += count;
	return count;
}

bool ZergStrategy::can_place_defense_creep_colony(FastTilePosition creep_colony_tile_position)
{
	for (int dy = 0; dy < UnitTypes::Zerg_Creep_Colony.tileHeight(); dy++) {
		for (int dx = 0; dx < UnitTypes::Zerg_Creep_Colony.tileWidth(); dx++) {
			FastTilePosition tile_position = creep_colony_tile_position + FastTilePosition(dx, dy);
			if (unit_grid.is_completed_friendly_building_in_tile(UnitTypes::Zerg_Sunken_Colony, tile_position)) return false;
			if (unit_grid.is_friendly_building_in_tile(UnitTypes::Zerg_Spore_Colony, tile_position)) return false;
			if (!Broodwar->hasCreep(tile_position)) return false;
			if (threat_grid.ground_threat(tile_position)) return false;
		}
	}
	return true;
}

void ZergStrategy::hydra_upgrades(bool lurker_aspect)
{
	if (building_manager.building_exists(UnitTypes::Zerg_Hydralisk_Den)) {
		if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Muscular_Augments) < 1) {
			building_manager.request_upgrade(UpgradeTypes::Muscular_Augments);
		} else if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Grooved_Spines) < 1) {
			building_manager.request_upgrade(UpgradeTypes::Grooved_Spines);
		} else if (lurker_aspect &&
				   building_manager.building_exists(UnitTypes::Zerg_Lair) &&
				   !Broodwar->self()->hasResearched(TechTypes::Lurker_Aspect)) {
			building_manager.request_research(TechTypes::Lurker_Aspect);
		}
	}
}

void ZergStrategy::attack_and_carapace_upgrades(std::initializer_list<UpgradeType> upgrade_types)
{
	if (building_manager.building_exists(UnitTypes::Zerg_Evolution_Chamber) ||
		building_manager.building_exists(UnitTypes::Zerg_Spire)) {
		int max_simultaneous_upgrades = (base_state.mining_base_count() >= 3) ? 2 : 1;
		int max_level;
		if (building_manager.building_exists(UnitTypes::Zerg_Hive)) {
			max_level = 3;
		} else if (building_manager.building_exists(UnitTypes::Zerg_Lair)) {
			max_level = 2;
		} else {
			max_level = 1;
		}
		for (UpgradeType upgrade_type : upgrade_types) {
			if (Broodwar->self()->isUpgrading(upgrade_type)) {
				max_simultaneous_upgrades--;
			}
		}
		for (int level = 0; level < max_level; level++) {
			for (UpgradeType upgrade_type : upgrade_types) {
				if (max_simultaneous_upgrades <= 0) break;
				if (!building_manager.building_exists(upgrade_type.whatUpgrades())) continue;
				if (Broodwar->self()->isUpgrading(upgrade_type)) continue;
				if (Broodwar->self()->getUpgradeLevel(upgrade_type) != level) continue;
				building_manager.request_upgrade(upgrade_type);
				max_simultaneous_upgrades--;
			}
		}
	}
}

int ZergStrategy::bases_needed_for_tech_to_hive()
{
	return opponent_model.enemy_race() == Races::Protoss ? 4 : 3;
}

void ZergStrategy::tech_to_hive()
{
	if (!building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spawning_Pool, 1);
	} else if (!done_or_in_progress(UpgradeTypes::Metabolic_Boost)) {
		building_manager.request_upgrade(UpgradeTypes::Metabolic_Boost);
	} else if (!building_manager.building_exists(UnitTypes::Zerg_Lair)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Lair, 1);
	} else if (!building_manager.building_exists(UnitTypes::Zerg_Spire)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Spire, 1);
	} else if (!building_manager.building_exists(UnitTypes::Zerg_Queens_Nest)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Queens_Nest, 1);
	} else if (!building_manager.building_exists(UnitTypes::Zerg_Hive)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Hive, 1);
	} else if (!building_manager.building_exists(UnitTypes::Zerg_Defiler_Mound)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Defiler_Mound, 1);
	} else if (!done_or_in_progress(TechTypes::Consume)) {
		building_manager.request_research(TechTypes::Consume);
	} else if (!done_or_in_progress(TechTypes::Plague)) {
		building_manager.request_research(TechTypes::Plague);
	} else if (!done_or_in_progress(UpgradeTypes::Adrenal_Glands)) {
		building_manager.request_upgrade(UpgradeTypes::Adrenal_Glands);
	} else if (!building_manager.building_exists(UnitTypes::Zerg_Ultralisk_Cavern)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Zerg_Ultralisk_Cavern, 1);
	}
}

bool ZergStrategy::tech_to_hive_done()
{
	return (building_manager.building_exists(UnitTypes::Zerg_Spawning_Pool) &&
			Broodwar->self()->getUpgradeLevel(UpgradeTypes::Metabolic_Boost) > 0 &&
			building_manager.building_exists(UnitTypes::Zerg_Lair) &&
			building_manager.building_exists(UnitTypes::Zerg_Spire) &&
			building_manager.building_exists(UnitTypes::Zerg_Queens_Nest) &&
			building_manager.building_exists(UnitTypes::Zerg_Hive) &&
			building_manager.building_exists(UnitTypes::Zerg_Defiler_Mound) &&
			Broodwar->self()->hasResearched(TechTypes::Consume) &&
			Broodwar->self()->hasResearched(TechTypes::Plague) &&
			Broodwar->self()->getUpgradeLevel(UpgradeTypes::Adrenal_Glands) > 0 &&
			building_manager.building_exists(UnitTypes::Zerg_Ultralisk_Cavern));
}

double ZergStrategy::calculate_mineral_gas_ratio()
{
	double minerals = spending_manager.income_per_minute().minerals + Broodwar->self()->minerals();
	double gas = spending_manager.income_per_minute().gas + Broodwar->self()->gas();
	return minerals / gas;
}

int ZergStrategy::calculate_number_of_scourges_required()
{
	int result = 0;
	for (auto& information_unit : information_manager.enemy_units()) {
		UnitType type = information_unit->type;
		if (type == UnitTypes::Terran_Wraith ||
			type == UnitTypes::Terran_Battlecruiser ||
			type == UnitTypes::Terran_Science_Vessel ||
			type == UnitTypes::Terran_Valkyrie ||
			type == UnitTypes::Protoss_Shuttle ||
			type == UnitTypes::Protoss_Corsair ||
			type == UnitTypes::Protoss_Scout ||
			type == UnitTypes::Protoss_Carrier ||
			type == UnitTypes::Protoss_Arbiter) {
			DPF dpf = calculate_damage_per_frame(UnitTypes::Zerg_Scourge, Broodwar->self(), type, information_unit->player);
			result += int(std::ceil(information_unit->expected_shields() / dpf.shield + information_unit->expected_hitpoints() / dpf.hp + 0.5));
		}
	}
	return result;
}

bool ZergStrategy::opponent_only_has_flying_buildings()
{
	bool building_present = false;
	bool non_flying_building_present = false;
	for (auto& information_unit : information_manager.enemy_units()) {
		if (information_unit->type.isBuilding()) {
			building_present = true;
			if (!information_unit->flying) non_flying_building_present = true;
		}
	}
	return building_present && !non_flying_building_present;
}

bool ZergStrategy::is_terran_bio()
{
	int bio_supply = 0;
	int mech_supply = 0;
	for (auto type : { UnitTypes::Terran_Marine, UnitTypes::Terran_Firebat, UnitTypes::Terran_Medic, UnitTypes::Terran_Ghost }) {
		bio_supply += type.supplyRequired() * information_manager.enemy_count(type);
	}
	for (auto type : { UnitTypes::Terran_Vulture, UnitTypes::Terran_Siege_Tank_Tank_Mode, UnitTypes::Terran_Siege_Tank_Siege_Mode, UnitTypes::Terran_Goliath }) {
		mech_supply += type.supplyRequired() * information_manager.enemy_count(type);
	}
	return bio_supply >= 2 * 5 && bio_supply >= mech_supply;
}

FastPosition ZergStrategy::determine_offensive_stage_position()
{
	FastPosition best_position = Positions::None;
	int best_distance = INT_MAX;
	
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			FastTilePosition tile_position(x, y);
			if (walkability_grid.is_walkable(tile_position)) {
				FastPosition position = center_position(tile_position);
				const BWEM::Area* area = area_at(position);
				if (area != nullptr) {
					bool position_in_base = false;
					int maximum_distance = -1;
					for (auto& base : tactics_manager.possible_enemy_start_bases()) {
						if (base->GetArea() == area) {
							position_in_base = true;
							break;
						}
						FastPosition base_position = base->Center();
						int distance = ground_distance(position, base_position);
						maximum_distance = std::max(maximum_distance, distance);
					}
					if (!position_in_base) {
						if (maximum_distance < best_distance) {
							best_position = position;
							best_distance = maximum_distance;
						}
					}
				}
			}
		}
	}
	
	return best_position;
}

void ZergStrategy::set_train_distribution(UnitType type)
{
	set_train_distribution(training_manager.larva_train_distribution(), type);
	training_manager.set_requested_lurker_count(type == UnitTypes::Zerg_Lurker ? -1 : 0);
}

void ZergStrategy::set_train_distribution(TrainDistribution& train_distribution,UnitType type)
{
	if (type == UnitTypes::Zerg_Lurker) {
		train_distribution.set(UnitTypes::Zerg_Hydralisk, 1.0);
		train_distribution.set(UnitTypes::Zerg_Zergling, std::max(calculate_mineral_gas_ratio() * 2.5 - 2.5, 0.0));
	} else if (type != UnitTypes::None) {
		train_distribution.set(type, 1.0);
		switch (type) {
			case UnitTypes::Zerg_Hydralisk:
				train_distribution.set(UnitTypes::Zerg_Zergling, std::max(calculate_mineral_gas_ratio() * 0.5 - 1.5, 0.0));
				break;
			case UnitTypes::Zerg_Mutalisk:
				train_distribution.set(UnitTypes::Zerg_Zergling, std::max(calculate_mineral_gas_ratio() * 2.0 - 2.0, 0.0));
				break;
			case UnitTypes::Zerg_Ultralisk:
				train_distribution.set(UnitTypes::Zerg_Zergling, std::max(calculate_mineral_gas_ratio() * 4.0 - 4.0, 0.0));
				break;
		}
	}
}

void ZergStrategy::frame_inner()
{
	switch (mode_) {
		case Mode::Opening:
			mode_opening();
			break;
		case Mode::DefendOneBaseProtoss:
			mode_defend_one_base_protoss();
			break;
		case Mode::DefendProxyRax:
			mode_defend_proxy_rax();
			break;
		case Mode::DefendFastPool:
			mode_defend_fast_pool();
			break;
		case Mode::Main_MutaHydraLurkerLing:
			mode_main_muta_hydra_lurker_ling(true);
			break;
		case Mode::Main_HydraLurkerLing:
			mode_main_muta_hydra_lurker_ling(false);
			break;
		case Mode::Main_UltraLing:
			mode_main_ultra_ling();
			break;
		case Mode::Main_ZvZ:
			mode_main_ZvZ();
			break;
		case Mode::Main_ZvZLateGame:
			mode_main_ZvZ(true);
			break;
	}
}

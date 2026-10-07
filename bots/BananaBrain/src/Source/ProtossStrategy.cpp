#include "BananaBrain.h"

void ProtossStrategy::pick_strategy(bool is_1v1)
{
	if (!is_1v1) {
		opening_ = kPvU_1012Gate;
		return;
	}
	
	Race race = opponent_model.enemy_race();
	if (race == Races::Zerg) {
		if (!configuration.PvZ_opening().empty()) {
			opening_ = configuration.PvZ_opening();
		} else {
			opening_ = result_store.pick_strategy({kPvZ_SairDt,kPvZ_1BaseSpeedZeal,kPvZ_2BaseSpeedZeal,kPvZ_Bisu,kPvZ_NeoBisu,kPvZ_4Gate2Archon,kPvZ_5GateGoon,kPvZ_SairGoon,kPvZ_SairReaver,kPvZ_4GateGoon,kPvZ_99Gate,kPvZ_99ProxyGate,kPvZ_1012Gate});
		}
	} else if (race == Races::Terran) {
		if (!configuration.PvT_opening().empty()) {
			opening_ = configuration.PvT_opening();
		} else {
			opening_ = result_store.pick_strategy({kPvT_2GateDt,kPvT_1GateDtExpo,kPvT_2GateRngExpo,kPvT_1GateReaver,kPvT_1015Gate,kPvT_Bulldog,kPvT_12Nexus,kPvT_28Nexus,kPvT_32Nexus,kPvT_DtDrop,kPvT_4GateGoon,kPvT_99Gate,kPvT_99ProxyGate,kPvT_1012Gate});
		}
	} else if (race == Races::Protoss) {
		if (!configuration.PvP_opening().empty()) {
			opening_ = configuration.PvP_opening();
		} else {
			opening_ = result_store.pick_strategy({kPvP_NZCore,kPvP_ZCore,kPvP_ZZCore,kPvP_ZCoreZ,kPvP_1012GateDt,kPvP_2GateDtExpo,kPvP_2GateReaver,kPvP_3GateRobo,kPvP_3GateSpeedZeal,kPvP_12Nexus,kPvP_4GateGoon,kPvP_99Gate,kPvP_99ProxyGate,kPvP_1012Gate});
		}
	} else {
		if (!configuration.PvU_opening().empty()) {
			opening_ = configuration.PvU_opening();
		} else {
			opening_ = result_store.pick_strategy({kPvU_FFE,kPvU_4GateGoon,kPvU_99Gate,kPvU_99ProxyGate,kPvU_1012Gate});
		}
	}
}

ProtossStrategy::LateGameStrategy ProtossStrategy::determine_late_game_strategy()
{
	if (late_game_strategy_ == LateGameStrategy::None) {
		int max_altitude = bwem_max_altitude();
		if (max_altitude <= kLateGameCarrierMaxAltitude) {
			late_game_strategy_ = LateGameStrategy::Carriers;
		} else if (max_altitude >= kLateGameArbiterMinAltitude) {
			late_game_strategy_ = LateGameStrategy::Arbiters;
		} else {
			std::uniform_real_distribution<double> dist;
			double r = dist(random_generator());
			double cutoff = kLateGameCarrierMaxAltitude + r * (kLateGameArbiterMinAltitude - kLateGameCarrierMaxAltitude);
			late_game_strategy_ = (max_altitude < cutoff) ? LateGameStrategy::Carriers : LateGameStrategy::Arbiters;
		}
	}
	return late_game_strategy_;
}

std::string ProtossStrategy::mode() const
{
	switch (mode_) {
		case Mode::Opening:
			return "Opening";
		case Mode::DefendFastPool:
			return "Defend fast pool";
		case Mode::DefendFastPoolFFE:
			return "Defend fast pool (FFE)";
		case Mode::DefendProxyGate:
			return "Defend proxy gate";
		case Mode::DefendCannonRush:
			return "Defend cannon rush";
		case Mode::DefendFourGateGoon:
			return "Defend four gate goon";
		case Mode::DefendThreeHatchLingFFE:
			return "Defend three hatch ling (FFE)";
		case Mode::ReactiveFastExpand:
			return "Reactive fast expand";
		case Mode::Main:
			return "Main";
		default:
			return "?";
	}
}

std::string ProtossStrategy::late_game_strategy() const
{
	switch (late_game_strategy_) {
		case LateGameStrategy::None:
			return "none";
		case LateGameStrategy::Arbiters:
			return "arbiters";
		case LateGameStrategy::Carriers:
			return "carriers";
		default:
			return "?";
	}
}

void ProtossStrategy::opening_PvZ_SairDt()
{
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool ||
		opponent_model.enemy_opening() == EnemyOpening::Z_9Pool ||
		opponent_model.enemy_opening() == EnemyOpening::Z_9PoolSpeed) {
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Cybernetics_Core);
		mode_ = Mode::Main;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
		if (training_manager.unit_count(UnitTypes::Protoss_Zealot) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
		}
	}
	
	if (building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvZ_1BaseSpeedZeal()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool ||
		opponent_model.enemy_opening() == EnemyOpening::Z_9Pool ||
		opponent_model.enemy_opening() == EnemyOpening::Z_9PoolSpeed) {
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		training_manager.lost_unit_count() >= 3 ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	if (supply >= 12) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (supply >= 13) opening_build_units(UnitTypes::Protoss_Zealot, 1);
	if (supply >= 16) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	if (supply >= 17) opening_build_units(UnitTypes::Protoss_Zealot, 2);
	if (supply >= 20) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	if (supply >= 21) opening_build_units(UnitTypes::Protoss_Zealot, 3);
	if (supply >= 23) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
	if (supply >= 25) opening_build_units(UnitTypes::Protoss_Dragoon, 1);
	if (supply >= 27) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
	if (supply >= 29) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
		opening_build_units(UnitTypes::Protoss_Zealot, 4);
	}
	if (supply >= 31) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 4);
		building_manager.request_upgrade(UpgradeTypes::Leg_Enhancements);
		opening_build_units(UnitTypes::Protoss_Zealot, 6);
	}
	if (supply >= 37) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 5);
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
		opening_build_units(UnitTypes::Protoss_Zealot, 8);
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 8) {
		attacking_ = true;
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Templar_Archives) >= 1) {
			mode_ = Mode::Main;
		}
	}
}

void ProtossStrategy::opening_PvZ_2BaseSpeedZeal()
{
	opening_forge_fast_expand();
	if (!opening_forge_fast_expand_completed_) return;
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (!done_or_in_progress(UpgradeTypes::Protoss_Ground_Weapons)) {
		building_manager.request_upgrade(UpgradeTypes::Protoss_Ground_Weapons);
	}
	if (supply >= 24) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
		building_manager.set_automatic_supply(true);
	}
	if (supply >= 27) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Citadel_of_Adun) >= 1 &&
			building_manager.building_count(UnitTypes::Protoss_Pylon) >= 3) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 3);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 3) {
			building_manager.request_upgrade(UpgradeTypes::Leg_Enhancements);
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 2);
		}
	}
	if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Protoss_Ground_Weapons) >= 1 &&
		Broodwar->self()->getUpgradeLevel(UpgradeTypes::Leg_Enhancements) >= 1 &&
		training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 8 &&
		!opening_attack_started_) {
		opening_attack_started_ = true;
		attacking_ = true;
	}
	
	if (opening_attack_started_) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
		if (building_manager.building_exists(UnitTypes::Protoss_Templar_Archives)) {
			if (training_manager.unit_count(UnitTypes::Protoss_Dark_Templar) < 1) {
				training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Dark_Templar, 1.0);
			}
			if (training_manager.unit_count(UnitTypes::Protoss_High_Templar) < 2) {
				training_manager.gateway_train_distribution().set(UnitTypes::Protoss_High_Templar, 1.0);
			}
		}
		if (training_manager.unit_count_completed(UnitTypes::Protoss_High_Templar) >= 2) {
			building_manager.request_research(TechTypes::Psionic_Storm);
			opening_build_units(UnitTypes::Protoss_Dragoon, 5);
		}
		if (training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 5 &&
			done_or_in_progress(TechTypes::Psionic_Storm)) {
			bool observer_first = expect_lurkers() || opponent_model.cloaked_present();
			if ((observer_first && training_manager.unit_count(UnitTypes::Protoss_Observer) < 1) ||
				(building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 3 &&
				 building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 8)) {
					observer_tree();
					if (observer_tree_done()) training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Observer, 1.0);
				} else {
					if (!building_manager.request_bases(3)) {
						mode_ = Mode::Main;
						return;
					}
				}
		}
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Nexus) >= 3 &&
			training_manager.unit_count(UnitTypes::Protoss_Observer) >= 1) {
			mode_ = Mode::Main;
		}
		if (expect_zerg_flyers()) {
			mode_ = Mode::Main;
		}
	}
	if (training_manager.gateway_train_distribution().is_empty()) {
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
	}
	if (opening_attack_started_) {
		additional_gateways();
		attack_check_condition();
	}
}

void ProtossStrategy::opening_PvZ_Bisu()
{
	opening_forge_fast_expand();
	if (!opening_forge_fast_expand_completed_) return;
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	building_manager.set_automatic_supply(true);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (supply >= 31) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Stargate, 1);
		}
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Stargate)) {
		training_manager.stargate_train_distribution().set(UnitTypes::Protoss_Corsair, 1.0);
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Corsair) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Citadel_of_Adun)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Templar_Archives)) {
		opening_build_units(UnitTypes::Protoss_Dark_Templar, 2);
	}
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Dark_Templar) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 4);
	}
	if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 4) {
		mode_ = Mode::Main;
	}
	if (training_manager.gateway_train_distribution().is_empty()) {
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
	}
}

void ProtossStrategy::opening_PvZ_NeoBisu()
{
	bool dragoon = false;
	bool high_templar = false;
	opening_forge_fast_expand();
	if (!opening_forge_fast_expand_completed_) return;
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	building_manager.set_automatic_supply(true);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 2);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core) &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Assimilator) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Stargate, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Stargate) >= 1) {
		if (!done_or_in_progress(UpgradeTypes::Protoss_Ground_Weapons)) {
			building_manager.request_upgrade(UpgradeTypes::Protoss_Ground_Weapons);
		}
		if (done_or_in_progress(UpgradeTypes::Protoss_Ground_Weapons) &&
			!done_or_in_progress(UpgradeTypes::Protoss_Air_Weapons)) {
			building_manager.request_upgrade(UpgradeTypes::Protoss_Air_Weapons);
		}
	}
	if (done_or_in_progress(UpgradeTypes::Protoss_Air_Weapons) &&
		building_manager.building_exists(UnitTypes::Protoss_Stargate) &&
		training_manager.unit_count(UnitTypes::Protoss_Corsair) < 6) {
		training_manager.stargate_train_distribution().set(UnitTypes::Protoss_Corsair, 1.0);
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Corsair) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Citadel_of_Adun) &&
		!done_or_in_progress(UpgradeTypes::Leg_Enhancements)) {
		building_manager.request_upgrade(UpgradeTypes::Leg_Enhancements);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Citadel_of_Adun) &&
		done_or_in_progress(UpgradeTypes::Leg_Enhancements)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
	}
	if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Protoss_Ground_Weapons) >= 1 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Templar_Archives) >= 1 &&
		!opening_attack_started_) {
		opening_attack_started_ = true;
		attacking_ = true;
	}
	
	if (opening_attack_started_ &&
		building_manager.building_exists(UnitTypes::Protoss_Templar_Archives)) {
		if (!done_or_in_progress(TechTypes::Psionic_Storm)) {
			building_manager.request_research(TechTypes::Psionic_Storm);
		}
		if (done_or_in_progress(TechTypes::Psionic_Storm) &&
			!done_or_in_progress(UpgradeTypes::Protoss_Ground_Weapons, 2)) {
			building_manager.request_upgrade(UpgradeTypes::Protoss_Ground_Weapons);
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 6);
		if (training_manager.unit_count(UnitTypes::Protoss_High_Templar) < 2) {
			high_templar = true;
		}
		if (training_manager.unit_count(UnitTypes::Protoss_High_Templar) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1);
			building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
			if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility) &&
				done_or_in_progress(UpgradeTypes::Singularity_Charge)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Observatory, 1);
			}
			if (building_manager.building_count_including_warping(UnitTypes::Protoss_Robotics_Facility) >= 1 &&
				done_or_in_progress(UpgradeTypes::Singularity_Charge)) {
				dragoon = true;
			}
			if (building_manager.building_exists(UnitTypes::Protoss_Observatory) &&
				training_manager.unit_count(UnitTypes::Protoss_Observer) < 1) {
				training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Observer, 1.0);
			}
			if (building_manager.building_count_including_warping(UnitTypes::Protoss_Observatory) >= 1 &&
				done_or_in_progress(UpgradeTypes::Singularity_Charge)) {
				if (!building_manager.request_bases(3)) {
					mode_ = Mode::Main;
					return;
				}
			}
		}
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Stargate) >= 1) {
		update_gateway_train_distribution(dragoon, high_templar, false);
	}
	if ((building_manager.building_count_including_warping(UnitTypes::Protoss_Nexus) >= 3 ||
		 base_state.next_available_bases().empty()) &&
		training_manager.unit_count(UnitTypes::Protoss_Observer) >= 1 &&
		Broodwar->self()->getUpgradeLevel(UpgradeTypes::Protoss_Ground_Weapons) >= 2) {
		zerg_dragoon_strategy_ = true;
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvZ_4Gate2Archon()
{
	opening_forge_fast_expand();
	if (!opening_forge_fast_expand_completed_) return;
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	building_manager.set_automatic_supply(true);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (supply >= 25 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 2);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		opening_build_units(UnitTypes::Protoss_Dragoon, 1);
		if (training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Stargate, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Stargate) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Citadel_of_Adun) >= 1 &&
			building_manager.building_exists(UnitTypes::Protoss_Stargate) &&
			training_manager.unit_count(UnitTypes::Protoss_Corsair) < 1) {
			training_manager.stargate_train_distribution().set(UnitTypes::Protoss_Corsair, 1.0);
		}
		if (training_manager.unit_count(UnitTypes::Protoss_Corsair) >= 1) {
			building_manager.request_upgrade(UpgradeTypes::Protoss_Ground_Weapons);
		}
		if (done_or_in_progress(UpgradeTypes::Protoss_Ground_Weapons) &&
			building_manager.building_exists(UnitTypes::Protoss_Citadel_of_Adun)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Templar_Archives) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 4);
		}
		if (building_manager.building_count(UnitTypes::Protoss_Gateway) == 4 &&
			training_manager.unit_count(UnitTypes::Protoss_High_Templar) + 2 * training_manager.unit_count(UnitTypes::Protoss_Archon) < 4) {
			training_manager.gateway_train_distribution().set(UnitTypes::Protoss_High_Templar, 1.0);
		}
		if (training_manager.unit_count(UnitTypes::Protoss_Archon) < 2) micro_manager.set_force_archon_warp(true);
		if (training_manager.unit_count(UnitTypes::Protoss_Archon) >= 2) {
			building_manager.request_upgrade(UpgradeTypes::Leg_Enhancements);
			if (done_or_in_progress(UpgradeTypes::Leg_Enhancements) &&
				Broodwar->self()->getUpgradeLevel(UpgradeTypes::Protoss_Ground_Weapons) >= 1) {
				attacking_ = true;
				mode_ = Mode::Main;
			}
		}
	}
	if (training_manager.gateway_train_distribution().is_empty()) {
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
	}
}

void ProtossStrategy::opening_PvZ_5GateGoon()
{
	opening_forge_fast_expand();
	if (!opening_forge_fast_expand_completed_) return;
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	building_manager.set_automatic_supply(true);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 1 &&
		building_manager.building_exists(UnitTypes::Protoss_Gateway)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core) &&
		training_manager.unit_count(UnitTypes::Protoss_Zealot) >= 1) {
		building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
		if (done_or_in_progress(UpgradeTypes::Singularity_Charge)) {
			opening_build_units(UnitTypes::Protoss_Dragoon, 1);
			if (training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 1) {
				update_gateway_train_distribution(true, false, false);
			}
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 2);
		}
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 2 &&
		building_manager.building_count(UnitTypes::Protoss_Pylon) >= 3) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 5);
	}
	if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 5) {
		zerg_dragoon_strategy_ = true;
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvZ_SairGoon()
{
	opening_forge_fast_expand();
	if (!opening_forge_fast_expand_completed_) return;
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	building_manager.set_automatic_supply(true);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Stargate, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Stargate) >= 1) {
			if (training_manager.unit_count_built_or_in_progress(UnitTypes::Protoss_Dragoon) == 0) {
				opening_build_units_once(UnitTypes::Protoss_Dragoon, 1);
			} else {
				update_gateway_train_distribution(true, false, false);
			}
		}
		if (building_manager.building_exists(UnitTypes::Protoss_Stargate)) {
			training_manager.stargate_train_distribution().set(UnitTypes::Protoss_Corsair, 1.0);
		}
	}
	if (supply >= 25 && training_manager.unit_count_built_or_in_progress(UnitTypes::Protoss_Dragoon) >= 1) {
		training_manager.set_worker_production(false);
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 2 &&
			building_manager.building_count(UnitTypes::Protoss_Pylon) >= 3) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 3);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 3) {
			training_manager.set_worker_production(true);
			building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
		}
		if (done_or_in_progress(UpgradeTypes::Singularity_Charge)) {
			building_manager.request_upgrade(UpgradeTypes::Protoss_Ground_Weapons);
		}
		if (done_or_in_progress(UpgradeTypes::Singularity_Charge) &&
			done_or_in_progress(UpgradeTypes::Protoss_Ground_Weapons) &&
			building_manager.building_count(UnitTypes::Protoss_Gateway) >= 3) {
			additional_gateways();
		}
	}
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 6 &&
		Broodwar->self()->getUpgradeLevel(UpgradeTypes::Singularity_Charge) >= 1 &&
		Broodwar->self()->getUpgradeLevel(UpgradeTypes::Protoss_Ground_Weapons) >= 1) {
		zerg_dragoon_strategy_ = true;
		attacking_ = true;
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvZ_SairReaver()
{
	opening_forge_fast_expand();
	if (!opening_forge_fast_expand_completed_) return;
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	building_manager.set_automatic_supply(true);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Stargate, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Stargate) >= 1) {
			training_manager.stargate_train_distribution().set(UnitTypes::Protoss_Corsair, 1.0);
			if (training_manager.unit_count(UnitTypes::Protoss_Corsair) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1);
			}
		}
		if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Support_Bay, 1);
			if (training_manager.unit_count(UnitTypes::Protoss_Shuttle) < 1) {
				training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Shuttle, 1.0);
			}
		}
		if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility) &&
			building_manager.building_exists(UnitTypes::Protoss_Robotics_Support_Bay) &&
			training_manager.unit_count(UnitTypes::Protoss_Shuttle) >= 1) {
			if (!done_or_in_progress(UpgradeTypes::Protoss_Air_Weapons)) {
				building_manager.request_upgrade(UpgradeTypes::Protoss_Air_Weapons);
			} else if (!done_or_in_progress(UpgradeTypes::Gravitic_Drive)) {
				building_manager.request_upgrade(UpgradeTypes::Gravitic_Drive);
			}
			if (training_manager.unit_count(UnitTypes::Protoss_Reaver) < 1) {
				training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Reaver, 1.0);
			}
		}
		if (training_manager.unit_count(UnitTypes::Protoss_Shuttle) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 3);
		}
		if (building_manager.building_exists(UnitTypes::Protoss_Stargate) &&
			building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility) &&
			building_manager.building_exists(UnitTypes::Protoss_Robotics_Support_Bay) &&
			training_manager.unit_count_completed(UnitTypes::Protoss_Shuttle) >= 1 &&
			training_manager.unit_count_completed(UnitTypes::Protoss_Reaver) >= 1 &&
			done_or_in_progress(UpgradeTypes::Protoss_Air_Weapons) &&
			done_or_in_progress(UpgradeTypes::Gravitic_Drive)) {
			attacking_ = true;
			mode_ = Mode::Main;
		}
	}
	training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
}

void ProtossStrategy::opening_PvT_2GateDt()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
		opponent_model.enemy_opening() == EnemyOpening::T_BBS ||
		opponent_model.enemy_opening() == EnemyOpening::T_2Rax) {
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Citadel_of_Adun);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Templar_Archives);
		mode_ = Mode::Main;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
	}
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 1) worker_manager.send_initial_scout();
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Zealot) > 0) {
		opening_zealot_started_ = true;
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Assimilator) > 0 && !opening_zealot_started_) {
			training_manager.set_worker_production(false);
		}
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Gateway) &&
		training_manager.unit_count(UnitTypes::Protoss_Zealot) == 0 &&
		!opening_zealot_started_) {
		opening_build_units(UnitTypes::Protoss_Zealot, 1);
	}
	if (supply >= 14 && opening_zealot_started_) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (supply >= 15 && building_manager.building_count_including_planned(UnitTypes::Protoss_Cybernetics_Core) > 0) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 2) {
		opening_build_units(UnitTypes::Protoss_Dragoon, 1);
		if (training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Citadel_of_Adun) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
			opening_build_units(UnitTypes::Protoss_Dragoon, 2);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 2 &&
			training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Templar_Archives) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
		}
		if (building_manager.building_exists(UnitTypes::Protoss_Templar_Archives) &&
			building_manager.building_count(UnitTypes::Protoss_Gateway) >= 2) {
			opening_build_units(UnitTypes::Protoss_Dark_Templar, 2);
		}
	}
	
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Dark_Templar) >= 2) {
		attacking_ = true;
		initial_dark_templar_attack_ = true;
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvT_1GateDtExpo()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		worker_manager.stop_combat();
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
		opponent_model.enemy_opening() == EnemyOpening::T_BBS ||
		opponent_model.enemy_opening() == EnemyOpening::T_2Rax) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Nexus, 1, 1);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Citadel_of_Adun);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Templar_Archives);
		mode_ = Mode::Main;
		worker_manager.stop_combat();
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		worker_manager.stop_combat();
		return;
	}
	
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	if (supply >= 12) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (supply >= 14) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
			opening_build_units(UnitTypes::Protoss_Zealot, 1);
		}
	}
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 1 &&
		!opening_blocking_probe_) {
		if (stage_chokepoint_ != nullptr) {
			Gap gap(stage_chokepoint_);
			if (gap.block_positions({UnitTypes::Protoss_Probe, UnitTypes::Protoss_Zealot}, UnitTypes::Terran_SCV, true, true).size() == 2) {
				worker_manager.combat(1);
			}
		}
		opening_blocking_probe_ = true;
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	if (supply >= 18) {
		if (!is_scouting_worker_in_base()) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Citadel_of_Adun) >= 1) {
				opening_build_units(UnitTypes::Protoss_Dragoon, 1);
			}
		} else {
			opening_build_units(UnitTypes::Protoss_Dragoon, 1);
			if (training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
			}
		}
	}
	if (supply >= 23) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
		if (training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 1) {
			worker_manager.stop_combat();
		}
	}
	if (supply >= 24) {
		training_manager.set_worker_production(false);
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Templar_Archives) >= 1) {
			opening_build_units(UnitTypes::Protoss_Dark_Templar, 1);
			if (!building_manager.request_bases(2)) {
				mode_ = Mode::Main;
				worker_manager.stop_combat();
				return;
			}
		}
		if (training_manager.unit_count(UnitTypes::Protoss_Dark_Templar) >= 1 &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
			training_manager.set_worker_production(true);
			update_gateway_train_distribution();
			additional_gateways();
		}
	}
	if (is_scouting_worker_in_base() ||
		building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
		worker_manager.stop_combat();
	}
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Dark_Templar) >= 1) {
		attacking_ = true;
		initial_dark_templar_attack_ = true;
		opening_attack_started_ = true;
	}
	if (opening_attack_started_ && building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvT_2GateRngExpo()
{
	if (building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	if (opening_lost_too_many_workers() && !building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
		opponent_model.enemy_opening() == EnemyOpening::T_BBS ||
		opponent_model.enemy_opening() == EnemyOpening::T_2Rax) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Nexus, 1, 1);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Robotics_Facility);
		mode_ = Mode::Main;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	if (supply >= 12) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (supply >= 13) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	if (supply >= 15) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	if (supply >= 17) {
		building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
		if (done_or_in_progress(UpgradeTypes::Singularity_Charge)) {
			worker_manager.set_limit_refinery_workers(2);
		}
	}
	if (is_enemy_offense_larger_than_defense()) {
		if (!building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
			mode_ = Mode::Main;
			return;
		} else {
			building_manager.set_automatic_supply(true);
			update_gateway_train_distribution();
			additional_gateways();
		}
	} else {
		if (supply >= 18 && done_or_in_progress(UpgradeTypes::Singularity_Charge)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
		}
		if (supply >= 20 && building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 2) {
			if (!building_manager.request_bases(2)) {
				mode_ = Mode::Main;
				return;
			}
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
				if (opening_worker_count() >= 20) {
					training_manager.set_worker_production(false);
				}
				opening_build_units(UnitTypes::Protoss_Dragoon, 2);
			}
		}
		if (supply >= 24 && training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
			worker_manager.set_limit_refinery_workers(-1);
			training_manager.set_worker_production(true);
		}
		if (supply >= 25 && building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 3) {
			opening_build_units(UnitTypes::Protoss_Dragoon, 4);
		}
		if (supply >= 31 && training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 4) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Robotics_Facility) >= 1) {
				mode_ = Mode::Main;
				return;
			}
		}
	}
	
	if (opening_attack_started_) attack_check_condition();
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 1 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
}

void ProtossStrategy::opening_PvT_1GateReaver()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
		opponent_model.enemy_opening() == EnemyOpening::T_BBS ||
		opponent_model.enemy_opening() == EnemyOpening::T_2Rax) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Nexus, 1, 1);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Robotics_Facility);
		mode_ = Mode::Main;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	bool dragoons = false;
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	if (supply >= 18 &&
		training_manager.unit_count(UnitTypes::Protoss_Dragoon) < 5) {
		dragoons = true;
	}
	if (supply >= 20) {
		building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
	}
	if (supply >= 21) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
	}
	if (supply >= 26) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1);
	}
	if (supply >= 29) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 4);
	}
	if (supply >= 34) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Observatory, 1);
		if (building_manager.building_exists(UnitTypes::Protoss_Observatory) &&
			training_manager.unit_count(UnitTypes::Protoss_Observer) < 1) {
			training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Observer, 1.0);
		}
	}
	if (supply >= 37) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main;
			return;
		}
	}
	if (building_manager.building_count(UnitTypes::Protoss_Gateway) >= 2) {
		dragoons = true;
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 4 &&
		training_manager.unit_count(UnitTypes::Protoss_Observer) >= 1 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Nexus) >= 2 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 2) {
		building_manager.set_automatic_supply(true);
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Support_Bay, 1);
		if (training_manager.unit_count(UnitTypes::Protoss_Shuttle) < 1) {
			training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Shuttle, 1.0);
		} else if (training_manager.unit_count(UnitTypes::Protoss_Reaver) < 1 &&
				   building_manager.building_exists(UnitTypes::Protoss_Robotics_Support_Bay)) {
			training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Reaver, 1.0);
		}
		if (training_manager.unit_count_completed(UnitTypes::Protoss_Shuttle) >= 1 &&
			training_manager.unit_count_completed(UnitTypes::Protoss_Reaver) >= 1) {
			mode_ = Mode::Main;
		}
	}
	if (dragoons) {
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Dragoon, 1.0);
	}
}

void ProtossStrategy::opening_PvT_1015Gate()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (is_enemy_offense_larger_than_defense() ||
		opponent_model.cloaked_present() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
		opponent_model.enemy_opening() == EnemyOpening::T_BBS ||
		opponent_model.enemy_opening() == EnemyOpening::T_2Rax) {
		building_placement_manager.clear_specific_positions();
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Nexus, 1, 1);
		mode_ = Mode::Main;
		return;
	}
	if (!opening_two_gateways_near_choke_placed_) {
		building_placement_manager.calculate_two_gateways_near_choke_position();
		opening_two_gateways_near_choke_placed_ = true;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	if (supply >= 11 && building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 15 && building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
		if (opening_worker_count() >= 15) {
			training_manager.set_worker_production(false);
		}
		if (building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
			building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
			if (!opening_attack_started_) opening_build_units(UnitTypes::Protoss_Dragoon, 1);
		}
	}
	if (supply >= 17) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 2 && !opening_attack_started_) {
			opening_build_units(UnitTypes::Protoss_Dragoon, 3);
		}
		if (training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 3 && !opening_attack_started_) {
			opening_attack_started_ = true;
			attacking_ = true;
		}
	}
	if (supply >= 21) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 3) {
			training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Dragoon, 1.0);
		}
	}
	if (supply >= 29) {
		if (!building_manager.request_bases(2)) {
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::Main;
			return;
		}
		training_manager.set_worker_production(true);
	}
	
	if (building_manager.building_count_including_warping(UnitTypes::Protoss_Nexus) >= 2) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvT_Bulldog()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (is_enemy_offense_larger_than_defense() ||
		opponent_model.cloaked_present() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
		opponent_model.enemy_opening() == EnemyOpening::T_BBS ||
		opponent_model.enemy_opening() == EnemyOpening::T_2Rax) {
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Robotics_Facility);
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (!opening_two_gateways_near_choke_placed_) {
		building_placement_manager.calculate_two_gateways_near_choke_position();
		opening_two_gateways_near_choke_placed_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 8) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	if (supply >= 11 && building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 15) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	if (supply >= 17) opening_build_units(UnitTypes::Protoss_Dragoon, 1);
	if (supply >= 20) {
		building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
		opening_build_units(UnitTypes::Protoss_Dragoon, 2);
	}
	if (supply >= 22) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
	}
	if (supply >= 24) opening_build_units(UnitTypes::Protoss_Dragoon, 4);
	if (supply >= 29) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1);
		opening_build_units(UnitTypes::Protoss_Dragoon, 6);
	}
	if (supply >= 33) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 4);
		opening_build_units(UnitTypes::Protoss_Zealot, 2);
	}
	if (supply >= 37) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 5);
	}
	if (supply >= 38) {
		training_manager.robotics_facility_train_distribution().clear();
		if (training_manager.unit_count(UnitTypes::Protoss_Shuttle) == 0) {
			training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Shuttle, 1.0);
			training_manager.set_worker_production(false);
		}
	}
	if (supply >= 40) opening_build_units(UnitTypes::Protoss_Zealot, 4);
	if (supply >= 44) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Observatory, 1);
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 4 &&
		training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 6 &&
		training_manager.unit_count_completed(UnitTypes::Protoss_Shuttle) >= 1) {
		micro_manager.perform_bulldog_zealot_drop();
	}
	if (training_manager.unit_count_loaded(UnitTypes::Protoss_Zealot) >= 4) {
		attacking_ = true;
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Observatory) >= 1) {
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::Main;
		}
	}
	
	if (opening_stage_location_ == WalkPositions::Unknown) {
		opening_stage_location_ = determine_start_base_hidden_corner();
	}
	if (opening_stage_location_.isValid()) {
		Position alternative_stage_position = walkability_grid.walkable_tile_near(center_position(opening_stage_location_), 256);
		if (alternative_stage_position.isValid()) {
			type_specific_stage_[UnitTypes::Protoss_Zealot] = alternative_stage_position;
			type_specific_stage_[UnitTypes::Protoss_Shuttle] = alternative_stage_position;
		}
	}
}

void ProtossStrategy::opening_PvT_12Nexus()
{
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	if (is_enemy_offense_larger_than_defense()) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Nexus, 1, 1);
		mode_ = Mode::Main;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) {
			worker_manager.set_scout_map_center(true);
			worker_manager.send_initial_scout();
		}
	}
	if (supply >= 10 && base_state.next_available_bases().empty()) {
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Nexus) == 1) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Nexus, 1, 1);
		if (supply >= 10) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		}
		if (supply >= 12) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
		}
		if (supply >= 13) opening_build_units(UnitTypes::Protoss_Zealot, 1);
		if (supply >= 15) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
		if (supply >= 17) opening_build_units(UnitTypes::Protoss_Zealot, 3);
		if (supply >= 21) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 2 &&
			building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 3) {
			attack_minimum_ = 2 * 8;
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::Main;
		}
		return;
	}
	if (supply >= 12) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main;
			return;
		}
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
		opponent_model.enemy_opening() == EnemyOpening::T_BBS ||
		opponent_model.enemy_opening() == EnemyOpening::T_2Rax ||
		opponent_model.enemy_opening() == EnemyOpening::T_WorkerRush) {
		if (supply >= 14) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		}
		if (supply >= 15 &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 1) {
				opening_build_units_once(UnitTypes::Protoss_Zealot, 1);
			}
		}
		if (training_manager.unit_count_built_or_in_progress(UnitTypes::Protoss_Zealot) == 0) {
			building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Cybernetics_Core);
		} else if (supply >= 17) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 2) {
				mode_ = Mode::Main;
			}
		}
		return;
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
		}
	}
	if (supply >= 15) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (supply >= 16 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		opening_build_units(UnitTypes::Protoss_Dragoon, 1);
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 1) {
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvT_28Nexus()
{
	if (building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	if (opening_lost_too_many_workers() && !building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		mode_ = Mode::Main;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
	if (supply >= 10) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	if (supply >= 11) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1) worker_manager.send_initial_scout();
	}
	if (is_enemy_offense_larger_than_defense()) {
		if (!building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
			mode_ = Mode::Main;
			return;
		} else {
			building_manager.set_automatic_supply(true);
			update_gateway_train_distribution();
			additional_gateways();
		}
	} else {
		if (supply >= 15) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
		if (supply >= 16 && building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 2) {
			opening_build_units(UnitTypes::Protoss_Dragoon, 1);
		}
		if (supply >= 19 && training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 1) {
			building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
		}
		if (supply >= 21 && done_or_in_progress(UpgradeTypes::Singularity_Charge)) {
			opening_build_units(UnitTypes::Protoss_Dragoon, 2);
		}
		if (supply >= 24 && training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
		}
		if (supply >= 25 && building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 3) {
			opening_build_units(UnitTypes::Protoss_Dragoon, 3);
		}
		if (supply >= 28 && training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 3) {
			if (!building_manager.request_bases(2)) {
				mode_ = Mode::Main;
				return;
			}
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
				mode_ = Mode::Main;
				return;
			}
		}
	}
	
	if (opening_attack_started_) attack_check_condition();
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 1 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
}

void ProtossStrategy::opening_PvT_32Nexus()
{
	if (building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	if (opening_lost_too_many_workers() && !building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		mode_ = Mode::Main;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count() + training_manager.lost_unit_supply() / 2;
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	if (supply >= 12) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	if (supply >= 13) opening_build_units_once(UnitTypes::Protoss_Zealot, 1);
	if (supply >= 16) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (supply >= 17) opening_build_units_once(UnitTypes::Protoss_Zealot, 2);
	if (supply >= 20) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	if (is_enemy_offense_larger_than_defense()) {
		if (!building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
			mode_ = Mode::Main;
			return;
		} else {
			building_manager.set_automatic_supply(true);
			update_gateway_train_distribution();
			additional_gateways();
		}
	} else {
		if (supply >= 22) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
		if (supply >= 23 && building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 3) {
			opening_build_units_once(UnitTypes::Protoss_Dragoon, 1);
		}
		if (supply >= 26 && training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 1) {
			building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
		}
		if (supply >= 28 && done_or_in_progress(UpgradeTypes::Singularity_Charge)) {
			opening_build_units_once(UnitTypes::Protoss_Dragoon, 2);
		}
		if (supply >= 30 && training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 4);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 4) {
				opening_build_units_once(UnitTypes::Protoss_Dragoon, 3);
			}
		}
		if (supply >= 32 && training_manager.unit_count_built_or_in_progress(UnitTypes::Protoss_Dragoon) >= 3) {
			if (!building_manager.request_bases(2)) {
				mode_ = Mode::Main;
				return;
			}
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
				mode_ = Mode::Main;
				return;
			}
		}
	}
	
	if (opening_attack_started_) attack_check_condition();
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 1 && !opening_attack_started_) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
}

void ProtossStrategy::opening_PvT_DtDrop()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		worker_manager.stop_combat();
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
		opponent_model.enemy_opening() == EnemyOpening::T_BBS ||
		opponent_model.enemy_opening() == EnemyOpening::T_2Rax) {
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Citadel_of_Adun);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Templar_Archives);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Robotics_Facility);
		worker_manager.stop_combat();
		mode_ = Mode::Main;
		return;
	}
	if (is_gas_stolen()) {
		worker_manager.stop_combat();
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Zealot) > 0) {
		opening_zealot_started_ = true;
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Assimilator) > 0 && !opening_zealot_started_) {
			training_manager.set_worker_production(false);
		}
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Gateway) &&
		training_manager.unit_count(UnitTypes::Protoss_Zealot) == 0 &&
		!opening_zealot_started_) {
		opening_build_units(UnitTypes::Protoss_Zealot, 1);
	}
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 1 &&
		!opening_blocking_probe_) {
		if (stage_chokepoint_ != nullptr) {
			Gap gap(stage_chokepoint_);
			if (gap.block_positions({UnitTypes::Protoss_Probe, UnitTypes::Protoss_Zealot}, UnitTypes::Terran_SCV, true, true).size() == 2) {
				worker_manager.combat(1);
			}
		}
		opening_blocking_probe_ = true;
	}
	if (supply >= 14 && opening_zealot_started_) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	if (supply >= 15 && building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	
	if (building_manager.building_exists(UnitTypes::Protoss_Assimilator) &&
		building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		building_manager.set_automatic_supply(true);
		if (!is_scouting_worker_in_base()) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
		} else {
			opening_build_units(UnitTypes::Protoss_Dragoon, 1);
			if (supply >= 30 &&
				training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
			}
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Citadel_of_Adun) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1);
		}
		if (building_manager.building_exists(UnitTypes::Protoss_Citadel_of_Adun) &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Robotics_Facility) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Templar_Archives) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
		}
		if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility) &&
			training_manager.unit_count(UnitTypes::Protoss_Shuttle) < 1) {
			training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Shuttle, 1.0);
			training_manager.set_prioritize_training(true);
		}
		if (building_manager.building_exists(UnitTypes::Protoss_Templar_Archives) &&
			training_manager.unit_count(UnitTypes::Protoss_Dark_Templar) < 2) {
			opening_build_units(UnitTypes::Protoss_Dark_Templar, 2);
			training_manager.set_prioritize_training(true);
		}
		if (training_manager.unit_count_completed(UnitTypes::Protoss_Dark_Templar) >= 2 &&
			training_manager.unit_count_completed(UnitTypes::Protoss_Shuttle) >= 1) {
			micro_manager.perform_drop(UnitTypes::Protoss_Dark_Templar);
		}
		if (training_manager.unit_count_loaded(UnitTypes::Protoss_Dark_Templar) >= 2) {
			attacking_ = true;
			initial_dark_templar_attack_ = true;
			worker_manager.stop_combat();
			mode_ = Mode::Main;
		}
		
		if (training_manager.gateway_train_distribution().get(UnitTypes::Protoss_Dark_Templar) == 0.0 &&
			training_manager.robotics_facility_train_distribution().get(UnitTypes::Protoss_Shuttle) == 0.0 &&
			templar_archives_remaining_time() >= UnitTypes::Protoss_Dragoon.buildTime()) {
			update_gateway_train_distribution();
			additional_gateways();
		}
	}
	
	if (is_scouting_worker_in_base() ||
		training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) + training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 2) {
		worker_manager.stop_combat();
	}
	if (opening_stage_location_ == WalkPositions::Unknown) {
		opening_stage_location_ = determine_start_base_hidden_corner();
	}
	if (opening_stage_location_.isValid()) {
		Position alternative_stage_position = walkability_grid.walkable_tile_near(center_position(opening_stage_location_), 256);
		if (alternative_stage_position.isValid()) {
			type_specific_stage_[UnitTypes::Protoss_Dark_Templar] = alternative_stage_position;
			type_specific_stage_[UnitTypes::Protoss_Shuttle] = alternative_stage_position;
		}
	}
}

void ProtossStrategy::opening_PvP_NZCore()
{
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ForgeFastExpand || opponent_model.enemy_opening() == EnemyOpening::P_FastExpand) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::ReactiveFastExpand;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendProxyGate;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (!opening_gateway_near_choke_placed_) {
		building_placement_manager.calculate_gateway_near_choke_position();
		opening_gateway_near_choke_placed_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	if (supply >= 13 && building_manager.building_exists(UnitTypes::Protoss_Gateway)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (supply >= 15) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	if (supply >= 17 && building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		opening_build_units(UnitTypes::Protoss_Dragoon, 1);
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 1) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvP_ZCore()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ForgeFastExpand || opponent_model.enemy_opening() == EnemyOpening::P_FastExpand) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::ReactiveFastExpand;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendProxyGate;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (!opening_gateway_near_choke_placed_) {
		building_placement_manager.calculate_gateway_near_choke_position();
		opening_gateway_near_choke_placed_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	if (supply >= 13 && building_manager.building_exists(UnitTypes::Protoss_Gateway)) {
		opening_build_units(UnitTypes::Protoss_Zealot, 1);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	if (supply >= 17) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Gateway) && building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		opening_build_units(UnitTypes::Protoss_Dragoon, 1);
	}
	if (!opening_attack_started_ && training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 1) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 1) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvP_ZZCore()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ForgeFastExpand || opponent_model.enemy_opening() == EnemyOpening::P_FastExpand) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::ReactiveFastExpand;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendProxyGate;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (!opening_gateway_near_choke_placed_) {
		building_placement_manager.calculate_gateway_near_choke_position();
		opening_gateway_near_choke_placed_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	if (supply >= 13 && building_manager.building_exists(UnitTypes::Protoss_Gateway)) {
		opening_build_units(UnitTypes::Protoss_Zealot, 1);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	if (supply >= 17) {
		opening_build_units(UnitTypes::Protoss_Zealot, 2);
	}
	if (supply >= 20) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (supply >= 23) {
		training_manager.set_worker_production(false);
		if (building_manager.building_exists(UnitTypes::Protoss_Gateway) && building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
			opening_build_units(UnitTypes::Protoss_Dragoon, 1);
		}
	}
	if (!opening_attack_started_ && training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 1) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 1) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvP_ZCoreZ()
{
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ForgeFastExpand || opponent_model.enemy_opening() == EnemyOpening::P_FastExpand) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::ReactiveFastExpand;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendProxyGate;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (!opening_gateway_near_choke_placed_) {
		building_placement_manager.calculate_gateway_near_choke_position();
		opening_gateway_near_choke_placed_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	if (supply >= 13 && building_manager.building_exists(UnitTypes::Protoss_Gateway)) {
		opening_build_units(UnitTypes::Protoss_Zealot, 1);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	if (supply >= 17) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (supply >= 18) {
		opening_build_units(UnitTypes::Protoss_Zealot, 2);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Gateway) && building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		opening_build_units(UnitTypes::Protoss_Dragoon, 1);
	}
	if (!opening_attack_started_ && training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 1) {
		attacking_ = true;
		opening_attack_started_ = true;
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 1) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvP_1012GateDt()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (opening_lost_too_many_workers() && building_manager.building_count(UnitTypes::Protoss_Gateway) < 2) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonTurtle) {
		building_placement_manager.clear_specific_positions();
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Citadel_of_Adun);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Templar_Archives);
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ForgeFastExpand) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::ReactiveFastExpand;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendProxyGate;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (!opening_two_gateways_near_choke_placed_) {
		building_placement_manager.calculate_two_gateways_near_choke_position();
		opening_two_gateways_near_choke_placed_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	if (supply >= 12) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
	if (is_enemy_offense_larger_than_defense()) {
		if (building_manager.building_count(UnitTypes::Protoss_Gateway) < 2) {
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::Main;
			return;
		} else {
			building_manager.set_automatic_supply(true);
			update_gateway_train_distribution();
			additional_gateways();
		}
	} else if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 2) {
		if (training_manager.unit_count(UnitTypes::Protoss_Zealot) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
		}
		if (training_manager.unit_count(UnitTypes::Protoss_Zealot) >= 4) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Citadel_of_Adun) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
		}
		building_manager.set_automatic_supply(true);
		if (building_manager.building_exists(UnitTypes::Protoss_Templar_Archives) &&
			training_manager.unit_count(UnitTypes::Protoss_Dark_Templar) < 2) {
			training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Dark_Templar, 1.0);
		} else {
			update_gateway_train_distribution();
		}
		if (!opening_attack_started_ &&
			training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 2) {
			opening_attack_started_ = true;
			attacking_ = true;
		}
		if (training_manager.unit_count_completed(UnitTypes::Protoss_Dark_Templar) >= 2) {
			attacking_ = true;
			initial_dark_templar_attack_ = true;
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::Main;
		}
	}
	if (opening_attack_started_) attack_check_condition();
}

void ProtossStrategy::opening_PvP_2GateDtExpo()
{
	if (building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	if (opening_lost_too_many_workers() && !building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ForgeFastExpand) {
		mode_ = Mode::ReactiveFastExpand;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate) {
		mode_ = Mode::DefendProxyGate;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	if (supply >= 12) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (supply >= 13) opening_build_units(UnitTypes::Protoss_Zealot, 1);
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	if (supply >= 18) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	if (supply >= 19) opening_build_units(UnitTypes::Protoss_Zealot, 2);
	if (supply >= 22) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
	if (supply >= 23) opening_build_units(UnitTypes::Protoss_Dragoon, 1);
	if (is_enemy_offense_larger_than_defense()) {
		if (!building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
			mode_ = Mode::Main;
			return;
		} else {
			building_manager.set_automatic_supply(true);
			update_gateway_train_distribution();
			additional_gateways();
		}
	} else {
		if ((supply >= 26 && !is_scouting_worker_in_base()) || supply >= 32) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
		}
		if (supply >= 27 &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Citadel_of_Adun) >= 1) {
			opening_build_units(UnitTypes::Protoss_Dragoon, 2);
			if (opening_worker_count() >= 21) {
				training_manager.set_worker_production(false);
			}
		}
		if (supply >= 29) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 2) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 4);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 4) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
			}
			if (building_manager.building_count(UnitTypes::Protoss_Gateway) >= 2) {
				opening_build_units(UnitTypes::Protoss_Zealot, 4);
				training_manager.set_worker_production(true);
				if (training_manager.unit_count(UnitTypes::Protoss_Zealot) >= 4) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 5);
				}
			}
			if (building_manager.building_exists(UnitTypes::Protoss_Templar_Archives)) {
				opening_build_units(UnitTypes::Protoss_Dark_Templar, 2);
			}
			if (training_manager.unit_count(UnitTypes::Protoss_Dark_Templar) >= 2) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Forge) >= 1) {
				if (base_state.natural_base() == nullptr) {
					mode_ = Mode::Main;
					return;
				}
				building_manager.request_base_defense_pylon(base_state.natural_base());
				if (opponent_model.cloaked_present() ||
					expect_dark_templars()) {
					if (building_manager.building_count_including_warping(UnitTypes::Protoss_Photon_Cannon) == 0) {
						building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Nexus, 1, 1);
					}
					if (base_state.is_backdoor_natural()) {
						building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Photon_Cannon, 2);
					} else {
						if (building_manager.base_defense_pylon_exists(base_state.natural_base())) {
							building_manager.set_requested_base_defense_cannon_count_at_least(base_state.natural_base(), 2);
						}
					}
					if (building_manager.building_count_including_warping(UnitTypes::Protoss_Photon_Cannon) >= 2) {
						if (!building_manager.request_bases(2)) {
							mode_ = Mode::Main;
							return;
						}
					}
				} else {
					if (!building_manager.request_bases(2)) {
						mode_ = Mode::Main;
						return;
					}
					if (building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
						if (base_state.is_backdoor_natural()) {
							building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Photon_Cannon, 2);
						} else {
							if (building_manager.base_defense_pylon_exists(base_state.natural_base())) {
								building_manager.set_requested_base_defense_cannon_count_at_least(base_state.natural_base(), 2);
							}
						}
					}
				}
			}
		}
		
		if (training_manager.unit_count_completed(UnitTypes::Protoss_Dark_Templar) >= 2) {
			opening_attack_started_ = true;
			attacking_ = true;
			initial_dark_templar_attack_ = true;
		}
		if (opening_attack_started_ &&
			building_manager.building_count_including_warping(UnitTypes::Protoss_Photon_Cannon) >= 2 &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
			mode_ = Mode::Main;
		}
	}
}

void ProtossStrategy::opening_PvP_2GateReaver()
{
	if (building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	if (opening_lost_too_many_workers() && !building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ForgeFastExpand) {
		mode_ = Mode::ReactiveFastExpand;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate) {
		mode_ = Mode::DefendProxyGate;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	bool dragoons = false;
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	if (supply >= 11) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (supply >= 14) {
		opening_build_units(UnitTypes::Protoss_Zealot, 1);
	}
	if (supply >= 16) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 2 &&
		building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		opening_build_units(UnitTypes::Protoss_Dragoon, 1);
	}
	if (is_enemy_offense_larger_than_defense()) {
		if (!building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
			mode_ = Mode::Main;
			return;
		} else {
			building_manager.set_automatic_supply(true);
			update_gateway_train_distribution();
			additional_gateways();
		}
	} else {
		if (supply >= 21) {
			building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
		}
		if (supply >= 22) {
			opening_build_units(UnitTypes::Protoss_Dragoon, 2);
		}
		if (supply >= 24) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
		}
		if (supply >= 25) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Robotics_Facility) >= 1) {
				dragoons = true;
			}
		}
		if (supply >= 29) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
		}
		if (supply >= 33) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 4);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 4) {
				if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility)) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Observatory, 1);
					if (building_manager.building_exists(UnitTypes::Protoss_Observatory) &&
						training_manager.unit_count(UnitTypes::Protoss_Observer) < 1) {
						training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Observer, 1.0);
					}
				}
			}
		}
		if (supply >= 40 && training_manager.unit_count(UnitTypes::Protoss_Observer) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Support_Bay, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Robotics_Support_Bay) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 5);
			}
		}
		if (supply >= 41) {
			if (training_manager.unit_count(UnitTypes::Protoss_Reaver) < 1) {
				if (opening_worker_count() >= 26) {
					training_manager.set_worker_production(false);
				}
				dragoons = false;
				training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Reaver, 1.0);
			}
		}
		if (supply >= 49) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 6);
			if (training_manager.unit_count(UnitTypes::Protoss_Reaver) < 2) {
				training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Reaver, 1.0);
			}
		}
		if (supply >= 56) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 7);
			if (training_manager.unit_count(UnitTypes::Protoss_Reaver) >= 2 &&
				training_manager.unit_count(UnitTypes::Protoss_Shuttle) < 2) {
				training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Shuttle, 1.0);
			}
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 7) {
			building_manager.set_automatic_supply(true);
		}
		if (supply >= 65 && !building_manager.request_bases(2)) {
			mode_ = Mode::Main;
			return;
		}
		if (training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 12 &&
			training_manager.unit_count_completed(UnitTypes::Protoss_Reaver) >= 2 &&
			training_manager.unit_count_completed(UnitTypes::Protoss_Shuttle) >= 2 &&
			!opening_attack_started_) {
			opening_attack_started_ = true;
			attacking_ = true;
		}
		if (opening_attack_started_ &&
			building_manager.building_count_including_warping(UnitTypes::Protoss_Nexus) >= 2) {
			mode_ = Mode::Main;
		}
		if (dragoons) {
			training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Dragoon, 1.0);
		}
	}
}

void ProtossStrategy::opening_PvP_3GateRobo()
{
	if (building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	if (opening_lost_too_many_workers() && !building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ForgeFastExpand || opponent_model.enemy_opening() == EnemyOpening::P_FastExpand) {
		mode_ = Mode::ReactiveFastExpand;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate) {
		mode_ = Mode::DefendProxyGate;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	if (supply >= 12) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (supply >= 14) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
		opening_build_units(UnitTypes::Protoss_Zealot, 1);
	}
	if (supply >= 16) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	if (supply >= 18) opening_build_units(UnitTypes::Protoss_Dragoon, 1);
	if (is_enemy_offense_larger_than_defense()) {
		if (!building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
			mode_ = Mode::Main;
			return;
		} else {
			building_manager.set_automatic_supply(true);
			update_gateway_train_distribution();
			additional_gateways();
		}
	} else {
		if (supply >= 20) building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
		if (supply >= 21 && done_or_in_progress(UpgradeTypes::Singularity_Charge)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
		}
		if (supply >= 22 && building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 3) {
			opening_build_units(UnitTypes::Protoss_Dragoon, 2);
		}
		if (supply >= 26 && training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1);
		}
		if (supply >= 29 && building_manager.building_count_including_planned(UnitTypes::Protoss_Robotics_Facility) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 3);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 3) {
				opening_build_units(UnitTypes::Protoss_Dragoon, 3);
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 4);
			}
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 4) {
			building_manager.set_automatic_supply(true);
		}
		if (supply >= 33 &&
			training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 3 &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 4) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Observatory, 1);
			if (building_manager.building_count_including_warping(UnitTypes::Protoss_Observatory) >= 1) {
				update_gateway_train_distribution();
			}
			if (building_manager.building_exists(UnitTypes::Protoss_Observatory) &&
				training_manager.unit_count(UnitTypes::Protoss_Observatory) == 0) {
				training_manager.gateway_train_distribution().clear();
				training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Observer, 1.0);
			}
		}
		if (training_manager.unit_count_completed(UnitTypes::Protoss_Observer) >= 1) {
			mode_ = Mode::Main;
			attacking_ = true;
		}
	}
}

void ProtossStrategy::opening_PvP_3GateSpeedZeal()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (!opening_two_gateways_near_choke_placed_) {
		building_placement_manager.calculate_two_gateways_near_choke_position();
		opening_two_gateways_near_choke_placed_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	if (supply >= 12) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
	if (supply >= 13) opening_build_units(UnitTypes::Protoss_Zealot, 1);
	if (supply >= 15) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	if (supply >= 17) {
		opening_build_units(UnitTypes::Protoss_Zealot, 3);
	}
	if (supply >= 21) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
	if (supply >= 23) opening_build_units(UnitTypes::Protoss_Zealot, 5);
	if (supply >= 27) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
			training_manager.set_worker_cut(true);
		}
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
		building_manager.set_automatic_supply(true);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Citadel_of_Adun) >= 1 &&
			opening_worker_count() >= 18) {
			training_manager.set_worker_production(false);
		}
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Citadel_of_Adun) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 3);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 3) {
		building_manager.request_upgrade(UpgradeTypes::Leg_Enhancements);
	}
	const int gas_needed = UnitTypes::Protoss_Citadel_of_Adun.gasPrice() + UpgradeTypes::Leg_Enhancements.gasPrice();
	if (Broodwar->self()->gatheredGas() >= gas_needed - 16) {
		int shortage = gas_needed - Broodwar->self()->gatheredGas();
		worker_manager.set_limit_refinery_workers(shortage / 8);
	}
	
	if (opponent_model.cloaked_present() ||
		expect_dark_templars()) {
		bool cloaked_attack_expected_soon = (opponent_model.cloaked_present() ||
											 information_manager.enemy_completed_exists(UnitTypes::Protoss_Templar_Archives));
		bool build_emergency_cannons = (cloaked_attack_expected_soon &&
										!handle_cloaked_attack_with_observers_ &&
										training_manager.unit_count(UnitTypes::Protoss_Observer) == 0 &&
										building_manager.building_count(UnitTypes::Protoss_Robotics_Facility) == 0);
		bool wait_for_emergency_cannons = false;
		if (build_emergency_cannons) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1, true);
			reserve_resources_for_emergency_cannons();
			if (building_manager.building_exists(UnitTypes::Protoss_Forge)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Photon_Cannon, 2, true);
			}
			wait_for_emergency_cannons = (building_manager.building_count_including_warping(UnitTypes::Protoss_Photon_Cannon) < 2);
		}
		
		if (!wait_for_emergency_cannons) {
			handle_cloaked_attack_with_observers_ = true;
			if (!observer_tree_done()) {
				observer_tree(true);
			} else if (training_manager.unit_count(UnitTypes::Protoss_Observer) < 1) {
				training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Observer, 1.0);
				training_manager.set_prioritize_training(true);
				training_manager.set_worker_cut(true);
			} else {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1, true);
				reserve_resources_for_emergency_cannons();
				if (building_manager.building_exists(UnitTypes::Protoss_Forge)) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Photon_Cannon, 2, true);
				}
			}
		}
	}
	
	if (!opening_attack_started_ &&
		training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 14) {
		opening_attack_started_ = true;
		attacking_ = true;
	}
	if (opening_attack_started_) {
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
		training_manager.set_worker_production(true);
		training_manager.set_worker_cut(true);
		building_manager.set_automatic_supply(true);
		additional_gateways();
		if (training_manager.unit_count_built(UnitTypes::Protoss_Zealot) >= 25) {
			building_placement_manager.clear_specific_positions();
			mode_ = Mode::Main;
		}
	}
}

void ProtossStrategy::opening_PvP_12Nexus()
{
	if (building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		return;
	}
	if (opening_lost_too_many_workers() && !building_manager.building_exists(UnitTypes::Protoss_Gateway)) {
		mode_ = Mode::Main;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate) {
		mode_ = Mode::DefendProxyGate;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10 && base_state.next_available_bases().empty()) {
		mode_ = Mode::Main;
		return;
	}
	if (supply >= 12) {
		if (!building_manager.request_bases(2)) {
			mode_ = Mode::Main;
			return;
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		}
	}
	if (is_enemy_offense_larger_than_defense()) {
		if (!building_manager.building_exists(UnitTypes::Protoss_Gateway)) {
			mode_ = Mode::Main;
			return;
		} else {
			building_manager.set_automatic_supply(true);
			update_gateway_train_distribution();
			additional_gateways();
			opening_attack_started_ = true;
			attacking_ = false;
		}
	} else {
		if (supply >= 14 && building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
		}
		if (supply >= 15 && building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 2) {
			if (base_state.natural_base() != nullptr) {
				building_manager.request_base_defense_pylon(base_state.natural_base());
			} else {
				mode_ = Mode::Main;
				return;
			}
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 2) {
				opening_build_units(UnitTypes::Protoss_Zealot, 5);
				building_manager.set_automatic_supply(true);
			}
			if (training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 5) opening_initial_zealots_done_ = true;
			if (opening_initial_zealots_done_ && !opening_attack_started_ && opponent_model.enemy_opening() == EnemyOpening::P_1GateCore) {
				opening_attack_started_ = true;
				attacking_ = true;
			}
			if (opening_attack_started_ && attacking_) {
				if (opponent_model.enemy_opening() == EnemyOpening::P_CannonTurtle) attack_frame_ = std::max(attack_frame_, 10000);
				attack_check_condition();
				if (opponent_model.enemy_opening() != EnemyOpening::P_1GateCore) attacking_ = false;
			}
			training_manager.set_prioritize_training(!opening_initial_zealots_done_);
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
			}
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Forge) >= 1) {
				const BWEM::Base* base = base_state.is_backdoor_natural() ? base_state.start_base() : base_state.natural_base();
				building_manager.set_requested_base_defense_cannon_count_at_least(base, 2);
			}
			if (building_manager.building_count_including_warping(UnitTypes::Protoss_Photon_Cannon) >= 2 &&
				opening_initial_zealots_done_) {
				attack_minimum_ = 2 * 20;
				mode_ = Mode::Main;
			}
		}
	}
}

void ProtossStrategy::opening_PvU_FFE()
{
	opening_forge_fast_expand();
	if (opening_forge_fast_expand_completed_) mode_ = Mode::Main;
}

void ProtossStrategy::opening_PvX_4GateGoon()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		building_placement_manager.clear_specific_positions();
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate) {
		mode_ = Mode::DefendProxyGate;
		building_placement_manager.clear_specific_positions();
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
		opponent_model.enemy_opening() == EnemyOpening::T_BBS ||
		opponent_model.enemy_opening() == EnemyOpening::T_2Rax) {
		mode_ = Mode::Main;
		building_placement_manager.clear_specific_positions();
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::T_FastExpand) {
		mode_ = Mode::ReactiveFastExpand;
		building_placement_manager.clear_specific_positions();
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool ||
		opponent_model.enemy_opening() == EnemyOpening::Z_9Pool ||
		opponent_model.enemy_opening() == EnemyOpening::Z_9PoolSpeed) {
		mode_ = Mode::DefendFastPool;
		building_placement_manager.clear_specific_positions();
		return;
	}
	if (is_gas_stolen()) {
		mode_ = Mode::Main;
		building_placement_manager.clear_specific_positions();
		return;
	}
	if (opponent_model.enemy_race() == Races::Protoss &&
		!opponent_dark_templar_frame_) {
		opponent_dark_templar_frame_ = result_store.minimum_historical_value([](auto& result){
			return result.opponent_dark_templar_frame >= 0 ? result.opponent_dark_templar_frame : INT_MAX;
		}, 7300, 10, 15);
	}
	if (opponent_model.enemy_race() == Races::Zerg &&
		!opponent_lurker_frame_) {
		opponent_lurker_frame_ = result_store.minimum_historical_value([](auto& result){
			return result.opponent_lurker_frame >= 0 ? result.opponent_lurker_frame : INT_MAX;
		}, 7300, 10, 15);
	}
	int supply = opening_supply_count();
	if (supply >= 8) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
	if (supply >= 10) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 1) {
			worker_manager.send_initial_scout();
			if (!opening_two_gateways_near_choke_placed_) {
				building_placement_manager.calculate_two_gateways_near_choke_position(true);
				opening_two_gateways_near_choke_placed_ = true;
			}
		}
	}
	if (supply >= 12) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	if (supply >= 13) opening_build_units(UnitTypes::Protoss_Zealot, 1);
	if (supply >= 16) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	if (supply >= 17) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	if (supply >= 18) opening_build_units(UnitTypes::Protoss_Zealot, 2);
	if (supply >= 22) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
	if (supply >= 23) opening_build_units(UnitTypes::Protoss_Dragoon, 1);
	if (supply >= 26) building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
	if (supply >= 27) {
		opening_build_units(UnitTypes::Protoss_Dragoon, 2);
	}
	if (opening_worker_count() >= 23) {
		training_manager.set_worker_production(false);
	}
	if (supply >= 31) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 4);
		opening_build_units(UnitTypes::Protoss_Dragoon, 3);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 4) {
		training_manager.gateway_train_distribution().clear();
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Dragoon, 1.0);
		building_manager.set_automatic_supply(true);
	}
	if (opponent_model.enemy_race() == Races::Terran &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 4) {
		if (!building_manager.request_bases(2) ||
			building_manager.building_count_including_warping(UnitTypes::Protoss_Nexus) >= 2) {
			mode_ = Mode::Main;
            if (opponent_model.enemy_opening() == EnemyOpening::T_WallIn) attack_frame_ = 10000;
			building_placement_manager.clear_specific_positions();
		}
	}
	
	const auto expect_dark_templars_based_on_opponent_dark_templar_frame = [&](){
		bool result = false;
		if (opponent_dark_templar_frame_ &&
			*opponent_dark_templar_frame_ < INT_MAX &&
			stage_chokepoint_ != nullptr) {
			int min_distance = INT_MAX;
			Position choke_position = chokepoint_center(stage_chokepoint_);
			for (auto& base : tactics_manager.possible_enemy_start_bases()) {
				int distance = ground_distance(choke_position, base->Center());
				if (distance > 0) min_distance = std::min(min_distance, distance);
			}
			if (min_distance < INT_MAX) {
				int travel_frames = int(min_distance / UnitTypes::Protoss_Dark_Templar.topSpeed() + 0.5);
				int arrive_frame = *opponent_dark_templar_frame_ + travel_frames;
				int build_detection_frames = (UnitTypes::Protoss_Forge.buildTime() +
											  building_extra_frames(UnitTypes::Protoss_Forge) +
											  UnitTypes::Protoss_Photon_Cannon.buildTime() +
											  building_extra_frames(UnitTypes::Protoss_Photon_Cannon) +
											  5 * 24);
				int build_detection_frame = arrive_frame - build_detection_frames;
				if (Broodwar->getFrameCount() >= build_detection_frame) {
					result = true;
				}
			}
		}
		return result;
	};
	
	const auto expect_lurkers_based_on_opponent_lurker_frame = [&](){
		bool result = false;
		if (opponent_lurker_frame_ &&
			*opponent_lurker_frame_ < INT_MAX) {
			int build_detection_frames = (UnitTypes::Protoss_Forge.buildTime() +
										  building_extra_frames(UnitTypes::Protoss_Forge) +
										  UnitTypes::Protoss_Photon_Cannon.buildTime() +
										  building_extra_frames(UnitTypes::Protoss_Photon_Cannon) +
										  5 * 24);
			int build_detection_frame = *opponent_lurker_frame_ - build_detection_frames;
			if (Broodwar->getFrameCount() >= build_detection_frame) {
				result = true;
			}
		}
		return result;
	};
	
	if (opponent_model.cloaked_present() ||
		expect_dark_templars() ||
		expect_dark_templars_based_on_opponent_dark_templar_frame() ||
		expect_lurkers() ||
		expect_lurkers_based_on_opponent_lurker_frame()) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1, true);
		reserve_resources_for_emergency_cannons();
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Photon_Cannon, 1, true);
		if (opponent_model.cloaked_present() &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) >= 1) {
			if (!observer_tree_done()) {
				observer_tree(true);
			} else if (training_manager.unit_count(UnitTypes::Protoss_Observer) < 1) {
				training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Observer, 1.0);
				training_manager.set_prioritize_training(true);
				training_manager.set_worker_cut(true);
			}
		}
	}
	
	if (expect_zerg_flyers()) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1, true);
		reserve_resources_for_emergency_cannons();
		building_manager.request_base_defense_pylon(base_state.start_base());
		if (building_manager.building_exists(UnitTypes::Protoss_Forge) &&
			building_manager.base_defense_pylon_exists(base_state.start_base())) {
			building_manager.set_requested_base_defense_cannon_count_at_least(base_state.start_base(), 3);
		}
	}
	
	if (!opening_attack_started_ &&
		training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 11) {
		opening_attack_started_ = true;
	}
	
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonTurtle) attack_frame_ = std::max(attack_frame_, 10000);
	if (opening_attack_started_) {
		attacking_ = (Broodwar->getFrameCount() >= attack_frame_);
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Dragoon, 1.0);
		building_manager.set_automatic_supply(true);
		if (training_manager.unit_count_built(UnitTypes::Protoss_Dragoon) >= 27) {
			zerg_dragoon_strategy_ = true;
			mode_ = Mode::Main;
			building_placement_manager.clear_specific_positions();
		}
	}
}

void ProtossStrategy::opening_PvX_99Gate()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool ||
		opponent_model.enemy_opening() == EnemyOpening::Z_9Pool ||
		opponent_model.enemy_opening() == EnemyOpening::Z_9PoolSpeed) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (!opening_two_gateways_near_choke_placed_) {
		if (opponent_model.enemy_race() == Races::Protoss ||
			opponent_model.enemy_race() == Races::Terran) {
			building_placement_manager.calculate_two_gateways_near_choke_position();
		}
		opening_two_gateways_near_choke_placed_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
	}
	if (supply >= 9 && building_manager.building_count(UnitTypes::Protoss_Pylon) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 2) {
			worker_manager.send_initial_scout();
		}
	}
	if (supply >= 11) {
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
		training_manager.set_worker_production(false);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 4) {
		attacking_ = true;
		attack_minimum_ = 2 * 8;
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvX_99ProxyGate()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		mode_ = Mode::DefendFastPool;
		building_placement_manager.clear_specific_positions();
		if (opening_proxy_location_.isValid() &&
			!unit_grid.is_friendly_building_in_tile(UnitTypes::Protoss_Pylon, opening_proxy_location_)) {
			building_placement_manager.clear_proxy_pylon_position();
		}
		return;
	}
	if (opening_lost_too_many_workers() ||
		building_manager.lost_building_count() >= 1 ||
		building_manager.building_placement_failed()) {
		mode_ = Mode::Main;
		building_placement_manager.clear_specific_positions();
		if (opening_proxy_location_.isValid() &&
			!unit_grid.is_friendly_building_in_tile(UnitTypes::Protoss_Pylon, opening_proxy_location_)) {
			building_placement_manager.clear_proxy_pylon_position();
		}
		return;
	}
	if (opening_proxy_location_ == TilePositions::Unknown) {
		opening_proxy_location_ = building_placement_manager.calculate_proxy_gateway_position();
	}
	Position proxy_location = Positions::None;
	if (opening_proxy_location_.isValid()) {
		proxy_location = center_position_for(UnitTypes::Protoss_Pylon, opening_proxy_location_);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) > 0) {
			Position walkable_position = walkability_grid.walkable_tile_near(proxy_location, 128);
			if (walkable_position.isValid()) proxy_location = walkable_position;
		}
	}
	int supply = opening_supply_count();
	if (opening_proxy_location_.isValid() &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) < 2) {
		worker_manager.send_proxy_builder(proxy_location, 4 * UnitTypes::Protoss_Probe.buildTime());
	}
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
	}
	if (supply >= 9 && building_manager.building_count(UnitTypes::Protoss_Pylon) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
		}
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 2) {
			worker_manager.send_initial_scout();
		}
	}
	if (supply >= 11) {
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
		training_manager.set_worker_production(false);
	}
	if (supply >= 13) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) >= 2 &&
		tactics_manager.enemy_base_attack_position(false).isValid()) {
		attacking_ = true;
		attack_minimum_ = 2 * 8;
		attack_near_target_only_ = true;
		building_placement_manager.clear_specific_positions();
		cannon_rush_handled_ = true;
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_PvX_1012Gate()
{
	worker_manager.set_scout_wait_at_natural(false);
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool ||
		opponent_model.enemy_opening() == EnemyOpening::Z_9Pool ||
		opponent_model.enemy_opening() == EnemyOpening::Z_9PoolSpeed) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendFastPool;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_ForgeFastExpand) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::ReactiveFastExpand;
		return;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::DefendCannonRush;
		return;
	}
	if (is_enemy_offense_larger_than_defense() ||
		opening_lost_too_many_workers() ||
		building_manager.building_placement_failed()) {
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
		return;
	}
	if (!opening_two_gateways_near_choke_placed_) {
		if (opponent_model.enemy_race() == Races::Protoss ||
			opponent_model.enemy_race() == Races::Terran) {
			building_placement_manager.calculate_two_gateways_near_choke_position();
		}
		opening_two_gateways_near_choke_placed_ = true;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	if (supply >= 12) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
	if (supply >= 13) opening_build_units(UnitTypes::Protoss_Zealot, 1);
	if (supply >= 15) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	if (supply >= 17) opening_build_units(UnitTypes::Protoss_Zealot, 3);
	if (supply >= 21) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 3);
	if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 2 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 3) {
		attack_minimum_ = 2 * 8;
		building_placement_manager.clear_specific_positions();
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::opening_build_units(UnitType unit_type,int count,bool stop_probe_production)
{
	if (training_manager.unit_count(unit_type) < count) {
		training_manager.gateway_train_distribution().set(unit_type, 1.0);
		if (stop_probe_production) training_manager.set_worker_production(false);
	}
}

void ProtossStrategy::opening_build_units_once(UnitType unit_type,int count,bool stop_probe_production)
{
	if (training_manager.unit_count_built_or_in_progress(unit_type) < count) {
		training_manager.gateway_train_distribution().set(unit_type, 1.0);
		if (stop_probe_production) training_manager.set_worker_production(false);
	}
}

void ProtossStrategy::opening_forge_fast_expand()
{
	opening_forge_fast_expand_hydra_bust_ = (Broodwar->getFrameCount() < 7000 &&
										   information_manager.enemy_count(UnitTypes::Zerg_Hatchery) >= 2 &&
										   information_manager.enemy_exists(UnitTypes::Zerg_Hydralisk_Den) &&
										   information_manager.enemy_count(UnitTypes::Zerg_Zergling) <= 2);
	if (opening_forge_fast_expand_completed_) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		if (opening_forge_fast_expand_positioned_successfully_) {
			if (opening_forge_fast_expand_hydra_bust_) opening_forge_fast_expand_cannons_ = 4;
			if (opponent_model.enemy_opening() == EnemyOpening::Z_OverPool &&
				information_manager.enemy_seen_count(UnitTypes::Zerg_Zergling) >= 5) {
				opening_forge_fast_expand_cannons_ = std::max(opening_forge_fast_expand_cannons_, 2);
			}
			if (worker_manager.initial_scout_death_position().isValid()) {
				opening_forge_fast_expand_cannons_ = std::max(opening_forge_fast_expand_cannons_, 2);
			}
			if (information_manager.enemy_seen_count(UnitTypes::Zerg_Zergling) > 8) {
				opening_forge_fast_expand_cannons_ = 4;
			}
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
			opening_forge_fast_expand_build_cannons(opening_forge_fast_expand_cannons_);
		}
		if (is_enemy_offense_larger_than_defense() &&
			opponent_model.enemy_race() == Races::Zerg) {
			if (!opening_forge_fast_expand_hydra_bust_ &&
				!information_manager.enemy_seen(UnitTypes::Zerg_Hydralisk) &&
				!information_manager.enemy_seen(UnitTypes::Zerg_Lurker)) {
				mode_ = Mode::DefendThreeHatchLingFFE;
			} else {
				opening_forge_fast_expand_cannons_ = 4;
			}
		}
		if (tactics_manager.enemy_offense_air_supply() > tactics_manager.anti_air_supply()) {
			mode_ = Mode::Main;
			building_placement_manager.clear_specific_positions();
			building_placement_manager.restore_saved_ffe_natural_base_defense_positions();
			return;
		}
		if (expect_zerg_flyers()) {
			building_placement_manager.restore_saved_ffe_natural_base_defense_positions();
			base_defense_cannons(true);
		}
		return;
	}
	if (!opening_forge_fast_expand_positioned_) {
		opening_forge_fast_expand_positioned_successfully_ = building_placement_manager.calculate_forge_fast_expand_position();
		opening_forge_fast_expand_positioned_ = true;
	}
	int supply = opening_supply_count();
	if (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) {
		mode_ = Mode::DefendFastPoolFFE;
		building_placement_manager.clear_specific_positions();
		building_placement_manager.restore_saved_ffe_natural_base_defense_positions();
		return;
	}
	if (is_enemy_offense_larger_than_defense() &&
		!opening_forge_fast_expand_hydra_bust_ &&
		!information_manager.enemy_seen(UnitTypes::Zerg_Hydralisk) &&
		!information_manager.enemy_seen(UnitTypes::Zerg_Lurker)) {
		if (building_manager.building_exists(UnitTypes::Protoss_Photon_Cannon) &&
			opponent_model.enemy_race() == Races::Zerg) {
			mode_ = Mode::DefendThreeHatchLingFFE;
			return;
		} else if (building_manager.building_count_including_warping(UnitTypes::Protoss_Photon_Cannon) >= 1) {
			worker_manager.combat(std::min(5, training_manager.unit_count(UnitTypes::Protoss_Probe) - 4),
								  stage_chokepoint_ != nullptr ? chokepoint_center(stage_chokepoint_) : Positions::Unknown);
		} else {
			worker_manager.stop_combat();
			mode_ = Mode::DefendFastPoolFFE;
			building_placement_manager.clear_specific_positions();
			building_placement_manager.restore_saved_ffe_natural_base_defense_positions();
			return;
		}
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Photon_Cannon)) {
		worker_manager.stop_combat();
	}
	
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (supply >= 10 &&
		building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		if (!opening_forge_fast_expand_positioned_successfully_) {
			mode_ = Mode::Main;
			return;
		}
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
		if (base_state.start_base_count() >= 4 &&
			building_manager.building_count_including_warping(UnitTypes::Protoss_Forge) >= 1) worker_manager.send_second_scout();
	}
	
	switch (opponent_model.enemy_opening()) {
		case EnemyOpening::Z_12Pool:
			opening_forge_fast_expand_12pool();
			break;
		case EnemyOpening::Z_12Hatch:
			opening_forge_fast_expand_12hatch();
			break;
		case EnemyOpening::Z_OverPool:
			opening_forge_fast_expand_overpool();
			break;
		default:
			opening_forge_fast_expand_9pool();
			break;
	}
	
	if (building_manager.building_count_including_warping(UnitTypes::Protoss_Photon_Cannon) >= opening_forge_fast_expand_cannons_ &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Nexus) >= 2 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 1 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 2 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Assimilator) >= 1 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1 &&
		training_manager.unit_count(UnitTypes::Protoss_Zealot) >= 1) {
		opening_forge_fast_expand_completed_ = true;
		fast_pool_handled_ = true;
	}
}

void ProtossStrategy::opening_forge_fast_expand_12pool()
{
	int supply = opening_supply_count();
	int zergling_count = information_manager.enemy_seen_count(UnitTypes::Zerg_Zergling);
	opening_forge_fast_expand_cannons_ = (zergling_count > 8 ? 4 : 1);
	if (opening_forge_fast_expand_hydra_bust_) opening_forge_fast_expand_cannons_ = 4;
	int cannon_count = 0;
	
	if (supply >= 15 &&
		building_manager.building_exists(UnitTypes::Protoss_Forge)) {
		training_manager.set_worker_production(false);
		building_manager.request_base(base_state.natural_base());
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
			cannon_count = opening_forge_fast_expand_cannons_;
			opening_forge_fast_expand_build_cannons(opening_forge_fast_expand_cannons_);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) >= opening_forge_fast_expand_cannons_) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 1 &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) >= opening_forge_fast_expand_cannons_ &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
			training_manager.set_worker_production(true);
		}
	}
	
	if (supply >= 16 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 1 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) >= opening_forge_fast_expand_cannons_ &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	
	if (supply >= 17 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	
	if (supply >= 18 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Assimilator) >= 1 &&
		building_manager.building_count(UnitTypes::Protoss_Pylon) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	
	if (supply >= 19 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		opening_build_units(UnitTypes::Protoss_Zealot, 1);
	}
	
	building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Photon_Cannon, opening_forge_fast_expand_cannons_, cannon_count);
}

void ProtossStrategy::opening_forge_fast_expand_12hatch()
{
	int supply = opening_supply_count();
	int zergling_count = information_manager.enemy_seen_count(UnitTypes::Zerg_Zergling);
	opening_forge_fast_expand_cannons_ = (zergling_count > 8 ? 4 : 1);
	if (opening_forge_fast_expand_hydra_bust_) opening_forge_fast_expand_cannons_ = 4;
	int cannon_count = 0;
	
	if (supply >= 15 &&
		building_manager.building_exists(UnitTypes::Protoss_Forge)) {
		building_manager.request_base(base_state.natural_base());
	}
	
	if (supply >= 16 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	
	if (supply >= 17 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Pylon) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	
	if (supply >= 18 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 1) {
		cannon_count = opening_forge_fast_expand_cannons_;
		opening_forge_fast_expand_build_cannons(opening_forge_fast_expand_cannons_);
	}
	
	if (supply >= 19 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 2 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) >= opening_forge_fast_expand_cannons_) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	
	if (supply >= 20 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Assimilator) >= 1 &&
		building_manager.building_count(UnitTypes::Protoss_Pylon) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	
	if (supply >= 22 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		opening_build_units(UnitTypes::Protoss_Zealot, 1);
	}
	
	building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Photon_Cannon, opening_forge_fast_expand_cannons_, cannon_count);
}

void ProtossStrategy::opening_forge_fast_expand_overpool()
{
	int supply = opening_supply_count();
	int zergling_count = information_manager.enemy_seen_count(UnitTypes::Zerg_Zergling);
	opening_forge_fast_expand_cannons_ = (zergling_count >= 5) ? (zergling_count > 8 ? 4 : 2) : 1;
	if (opening_forge_fast_expand_hydra_bust_) opening_forge_fast_expand_cannons_ = 4;
	int cannon_count = 0;
	
	if (supply >= 13 &&
		building_manager.building_exists(UnitTypes::Protoss_Forge)) {
		training_manager.set_worker_production(false);
		building_manager.request_base(base_state.natural_base());
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
			cannon_count = opening_forge_fast_expand_cannons_;
			opening_forge_fast_expand_build_cannons(opening_forge_fast_expand_cannons_);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) >= opening_forge_fast_expand_cannons_ &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
			training_manager.set_worker_production(true);
		}
	}
	
	if (supply >= 15 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) >= opening_forge_fast_expand_cannons_ &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	
	if (supply >= 16 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 1 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) >= opening_forge_fast_expand_cannons_ &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	
	if (supply >= 17 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	
	if (supply >= 18 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Assimilator) >= 1 &&
		building_manager.building_count(UnitTypes::Protoss_Pylon) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	
	if (supply >= 19 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		opening_build_units(UnitTypes::Protoss_Zealot, 1);
	}
	
	building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Photon_Cannon, opening_forge_fast_expand_cannons_, cannon_count);
}

void ProtossStrategy::opening_forge_fast_expand_9pool()
{
	int supply = opening_supply_count();
	
	int max_expected_workers = 10;
	if (information_manager.enemy_count(UnitTypes::Zerg_Extractor) >= 1) max_expected_workers--;
	if (information_manager.enemy_count(UnitTypes::Zerg_Hatchery) >= 2) max_expected_workers--;
	opening_forge_fast_expand_cannons_ = (information_manager.enemy_count(UnitTypes::Zerg_Drone) > max_expected_workers) ? 2 : 4;
	if (opening_forge_fast_expand_hydra_bust_) opening_forge_fast_expand_cannons_ = 4;
	if (opponent_model.enemy_opening() == EnemyOpening::Z_12HatchMain) opening_forge_fast_expand_cannons_ = 4;
	
	if (supply >= 14 &&
		building_manager.building_exists(UnitTypes::Protoss_Forge)) {
		opening_forge_fast_expand_build_cannons(opening_forge_fast_expand_cannons_);
	}
	if (supply >= 15 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) >= opening_forge_fast_expand_cannons_) {
		training_manager.set_worker_production(false);
		building_manager.request_base(base_state.natural_base());
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Nexus) >= 2) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
			if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 1) {
				training_manager.set_worker_production(true);
			}
		}
	}
	if (supply >= 16 &&
		building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) >= opening_forge_fast_expand_cannons_ &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Nexus) >= 2 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 2);
	}
	
	if (supply >= 17 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	
	if (supply >= 18 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Assimilator) >= 1 &&
		building_manager.building_count(UnitTypes::Protoss_Pylon) >= 2) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	
	if (supply >= 19 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		opening_build_units(UnitTypes::Protoss_Zealot, 1);
	}
}

void ProtossStrategy::opening_forge_fast_expand_build_cannons(int cannon_count)
{
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) < cannon_count) {
		int warping_cannon_count = building_manager.building_count_including_warping(UnitTypes::Protoss_Photon_Cannon);
		const BWEM::Base* wall_base = base_state.is_backdoor_natural() ? base_state.start_base() : base_state.natural_base();
		building_manager.set_requested_base_defense_cannon_count_at_least(wall_base, warping_cannon_count + 1);
	}
}

FastWalkPosition ProtossStrategy::determine_start_base_hidden_corner()
{
	key_value_vector<FastWalkPosition,int> candidates;
	
	for (int y = 0; y < Broodwar->mapHeight() * 4; y++) {
		for (int x = 0; x < Broodwar->mapWidth() * 4; x++) {
			FastWalkPosition walk_position(x, y);
			FastPosition position = center_position(walk_position);
			const BWEM::Area* area = area_at(walk_position);
			if (area != nullptr && contains(base_state.controlled_areas(), area)) {
				int distance = calculate_distance(Broodwar->self()->getRace().getResourceDepot(),
												  base_state.start_base()->Center(),
												  FastPosition(walk_position));
				for (auto& cp : area->ChokePoints()) {
					if (!cp->Blocked()) {
						int cp_distance = chokepoint_center(cp).getApproxDistance(position);
						distance = std::min(distance, cp_distance);
					}
				}
				candidates.emplace_back(walk_position, distance);
			}
		}
	}
	
	return key_with_largest_value(candidates);
}

int ProtossStrategy::templar_archives_remaining_time()
{
	int result = INT_MAX;
	for (auto& information_unit : information_manager.my_units()) {
		if (information_unit->type == UnitTypes::Protoss_Templar_Archives) {
			result = std::min(result, information_unit->unit->getRemainingBuildTime());
		}
	}
	return result;
}

void ProtossStrategy::mode_opening()
{
	building_manager.set_automatic_supply(false);
	training_manager.set_worker_cut(true);
	training_manager.set_prioritize_training(false);
	training_manager.gateway_train_distribution().clear();
	training_manager.robotics_facility_train_distribution().clear();
	training_manager.stargate_train_distribution().clear();
	if (opening_ == kPvZ_SairDt) opening_PvZ_SairDt();
	else if (opening_ == kPvZ_1BaseSpeedZeal) opening_PvZ_1BaseSpeedZeal();
	else if (opening_ == kPvZ_2BaseSpeedZeal) opening_PvZ_2BaseSpeedZeal();
	else if (opening_ == kPvZ_Bisu) opening_PvZ_Bisu();
	else if (opening_ == kPvZ_NeoBisu) opening_PvZ_NeoBisu();
	else if (opening_ == kPvZ_4Gate2Archon) opening_PvZ_4Gate2Archon();
	else if (opening_ == kPvZ_5GateGoon) opening_PvZ_5GateGoon();
	else if (opening_ == kPvZ_SairGoon) opening_PvZ_SairGoon();
	else if (opening_ == kPvZ_SairReaver) opening_PvZ_SairReaver();
	else if (opening_ == kPvZ_4GateGoon) opening_PvX_4GateGoon();
	else if (opening_ == kPvZ_99Gate) opening_PvX_99Gate();
	else if (opening_ == kPvZ_99ProxyGate) opening_PvX_99ProxyGate();
	else if (opening_ == kPvZ_1012Gate) opening_PvX_1012Gate();
	else if (opening_ == kPvT_2GateDt) opening_PvT_2GateDt();
	else if (opening_ == kPvT_1GateDtExpo) opening_PvT_1GateDtExpo();
	else if (opening_ == kPvT_2GateRngExpo) opening_PvT_2GateRngExpo();
	else if (opening_ == kPvT_1GateReaver) opening_PvT_1GateReaver();
	else if (opening_ == kPvT_1015Gate) opening_PvT_1015Gate();
	else if (opening_ == kPvT_Bulldog) opening_PvT_Bulldog();
	else if (opening_ == kPvT_12Nexus) opening_PvT_12Nexus();
	else if (opening_ == kPvT_28Nexus) opening_PvT_28Nexus();
	else if (opening_ == kPvT_32Nexus) opening_PvT_32Nexus();
	else if (opening_ == kPvT_DtDrop) opening_PvT_DtDrop();
	else if (opening_ == kPvT_4GateGoon) opening_PvX_4GateGoon();
	else if (opening_ == kPvT_99Gate) opening_PvX_99Gate();
	else if (opening_ == kPvT_99ProxyGate) opening_PvX_99ProxyGate();
	else if (opening_ == kPvT_1012Gate) opening_PvX_1012Gate();
	else if (opening_ == kPvP_NZCore) opening_PvP_NZCore();
	else if (opening_ == kPvP_ZCore) opening_PvP_ZCore();
	else if (opening_ == kPvP_ZZCore) opening_PvP_ZZCore();
	else if (opening_ == kPvP_ZCoreZ) opening_PvP_ZCoreZ();
	else if (opening_ == kPvP_1012GateDt) opening_PvP_1012GateDt();
	else if (opening_ == kPvP_2GateDtExpo) opening_PvP_2GateDtExpo();
	else if (opening_ == kPvP_2GateReaver) opening_PvP_2GateReaver();
	else if (opening_ == kPvP_3GateRobo) opening_PvP_3GateRobo();
	else if (opening_ == kPvP_3GateSpeedZeal) opening_PvP_3GateSpeedZeal();
	else if (opening_ == kPvP_12Nexus) opening_PvP_12Nexus();
	else if (opening_ == kPvP_4GateGoon) opening_PvX_4GateGoon();
	else if (opening_ == kPvP_99Gate) opening_PvX_99Gate();
	else if (opening_ == kPvP_99ProxyGate) opening_PvX_99ProxyGate();
	else if (opening_ == kPvP_1012Gate) opening_PvX_1012Gate();
	else if (opening_ == kPvU_FFE) opening_PvU_FFE();
	else if (opening_ == kPvU_4GateGoon) opening_PvX_4GateGoon();
	else if (opening_ == kPvU_99Gate) opening_PvX_99Gate();
	else if (opening_ == kPvU_99ProxyGate) opening_PvX_99ProxyGate();
	else if (opening_ == kPvU_1012Gate) opening_PvX_1012Gate();
	
	scout_for_cannon_rush_if_needed();
}

void ProtossStrategy::mode_defend_fast_pool()
{
	if (!fast_pool_handled_) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Nexus, 1, 1);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Assimilator);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Citadel_of_Adun);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Templar_Archives);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Stargate);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Robotics_Facility);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Forge);
		fast_pool_handled_ = true;
		attacking_ = false;
	}
	int supply = (Broodwar->self()->supplyUsed() + 1) / 2;
	if (supply >= 8) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
	if (supply >= 10) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	training_manager.gateway_train_distribution().clear();
	training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
	if (building_manager.building_exists(UnitTypes::Protoss_Gateway)) additional_gateways();
	int wanted_zealot_count = (opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool) ? 4 : 10;
	if (training_manager.unit_count(UnitTypes::Protoss_Zealot) >= wanted_zealot_count) {
		mode_ = Mode::Main;
		attack_minimum_ = 2 * 8;
	}
}

void ProtossStrategy::mode_defend_fast_pool_ffe()
{
	const BWEM::Base* start_base = base_state.start_base();
	building_manager.request_base_defense_pylon(start_base);
	if (building_manager.base_defense_pylon_exists(start_base)) {
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Forge) > 0 &&
			building_manager.building_count_including_warping(UnitTypes::Protoss_Photon_Cannon) < 2 &&
			!building_manager.building_exists(UnitTypes::Protoss_Gateway)) {
			building_manager.set_requested_base_defense_cannon_count_at_least(start_base, 2);
		} else {
			building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Forge);
			int supply = (Broodwar->self()->supplyUsed() + 1) / 2;
			if (supply >= 10) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
			training_manager.gateway_train_distribution().clear();
			training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
			if (building_manager.building_exists(UnitTypes::Protoss_Gateway)) additional_gateways();
			if (training_manager.unit_count(UnitTypes::Protoss_Zealot) >= 4) {
				mode_ = Mode::Main;
				attack_minimum_ = 2 * 8;
				fast_pool_handled_ = true;
			}
		}
	}
	
	attacking_ = false;
	
	std::vector<Unit> unfinished_cannons;
	for (Unit unit : Broodwar->self()->getUnits()) {
		if (!unit->isCompleted() && unit->getType() == UnitTypes::Protoss_Photon_Cannon) {
			unfinished_cannons.push_back(unit);
		}
	}
	if (!unfinished_cannons.empty()) {
		std::vector<Unit> zerglings;
		for (Unit unit : Broodwar->enemies().getUnits()) {
			if (unit->isVisible() && unit->getType() == UnitTypes::Zerg_Zergling) {
				zerglings.push_back(unit);
			}
		}
		
		Unit closest_unfinished_cannon = nullptr;
		int min_distance = UnitTypes::Protoss_Photon_Cannon.sightRange();
		
		for (Unit cannon_unit : unfinished_cannons) {
			for (Unit zergling_unit : zerglings) {
				int distance = cannon_unit->getDistance(zergling_unit);
				if (distance < min_distance) {
					closest_unfinished_cannon = cannon_unit;
					min_distance = distance;
				}
			}
		}
		
		if (closest_unfinished_cannon != nullptr) {
			worker_manager.defend_building(closest_unfinished_cannon, training_manager.unit_count_completed(UnitTypes::Protoss_Probe) - 4);
		}
	}
}

void ProtossStrategy::mode_defend_proxy_gate()
{
	training_manager.gateway_train_distribution().clear();
	if (!proxy_gate_handled_) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Nexus, 1, 1);
		proxy_gate_handled_ = true;
		attacking_ = false;
		building_placement_manager.clear_specific_positions();
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		mode_ = Mode::DefendCannonRush;
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Shield_Battery);
		return;
	}
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
	}
	if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		building_manager.set_automatic_supply(true);
	}
	
	if (building_manager.building_count_including_warping(UnitTypes::Protoss_Cybernetics_Core) == 0 ||
		is_gas_stolen()) {
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Cybernetics_Core);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Assimilator);
		worker_manager.set_limit_refinery_workers(0);
		
		if (supply >= 10 && building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
			training_manager.set_worker_production(false);
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2);
			if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 2) {
				training_manager.set_worker_production(true);
				training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
			}
		}
		
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 2 &&
			training_manager.unit_count(UnitTypes::Protoss_Zealot) >= 4) {
			mode_ = Mode::Main;
		}
	} else {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Assimilator) >= 1) {
			opening_build_units(UnitTypes::Protoss_Dragoon, 1);
		}
		if (training_manager.unit_count(UnitTypes::Protoss_Dragoon) >= 1) {
			training_manager.set_worker_production(false);
			if (!shield_battery_placed_) {
				building_placement_manager.calculate_shield_battery_near_choke_position();
				shield_battery_placed_ = true;
			}
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Shield_Battery, 1);
			if (building_manager.building_count_including_warping(UnitTypes::Protoss_Shield_Battery) >= 1) {
				training_manager.set_worker_production(true);
				update_gateway_train_distribution();
				additional_gateways();
				if (training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) >= 8) {
					building_placement_manager.clear_specific_positions();
					mode_ = Mode::Main;
				}
			}
		}
	}
}

void ProtossStrategy::mode_defend_cannon_rush()
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
	bool combat_unit_not_near_base = false;
	for (auto information_unit : information_manager.my_units()) {
		if (information_unit->base_distance > 768 &&
			(information_unit->type == UnitTypes::Protoss_Zealot ||
			 information_unit->type == UnitTypes::Protoss_Dragoon ||
			 information_unit->type == UnitTypes::Protoss_Reaver)) {
			combat_unit_not_near_base = true;
			break;
		}
	}
	if (!cannon_rush_handled_) {
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Nexus, 1, 1);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Citadel_of_Adun);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Templar_Archives);
		if (!is_gas_stolen()) {
			building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Gateway, 1, 1);
		}
		building_placement_manager.clear_specific_positions();
		worker_manager.stop_scouting();
		attacking_ = false;
		opening_attack_started_ = false;
		cannon_rush_handled_ = true;
	}
	training_manager.gateway_train_distribution().clear();
	training_manager.robotics_facility_train_distribution().clear();
	attack_minimum_ = 2 * 2;
	int supply = opening_supply_count();
	if (supply >= 8) building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
	if (supply >= 10) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 1) worker_manager.send_initial_scout();
		building_manager.set_automatic_supply(true);
	}
	if (supply >= 12) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
	}
	if (supply >= 13 && building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Gateway)) {
		training_manager.gateway_train_distribution().clear();
		if (building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core) &&
			(building_manager.building_exists(UnitTypes::Protoss_Assimilator) ||
			 Broodwar->self()->gas() >= UnitTypes::Protoss_Dragoon.gasPrice())) {
			training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Dragoon, 1.0);
		} else if (is_gas_stolen() ||
				   training_manager.unit_count(UnitTypes::Protoss_Zealot) < 2) {
			training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
		}
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core) &&
		building_manager.building_exists(UnitTypes::Protoss_Assimilator) &&
		(!cannons_or_pylons_in_base ||
		 training_manager.unit_count_built_or_in_progress(UnitTypes::Protoss_Dragoon) >= 2)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1, true);
		if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Support_Bay, 1, true);
			if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Support_Bay) &&
				training_manager.unit_count(UnitTypes::Protoss_Reaver) < 2) {
				training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Reaver, 1.0);
			}
			if (training_manager.unit_count(UnitTypes::Protoss_Reaver) >= 2) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Observatory, 1);
				if (building_manager.building_exists(UnitTypes::Protoss_Observatory) &&
					training_manager.unit_count(UnitTypes::Protoss_Observer) < 1) {
					training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Observer, 1.0);
				}
			}
		}
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Gateway) &&
		(building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core) ||
		 is_gas_stolen())) {
		additional_gateways();
	}
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Reaver) >= 1) {
		attacking_ = true;
	} else {
		attack_check_condition();
	}
	/*const auto is_safe_expand_possible = [&](){
		const BWEM::Base* base = base_state.natural_base();
		if (base != nullptr) {
			FastTilePosition base_tile_position = base->Location();
			for (int dy = 0; dy < UnitTypes::Protoss_Nexus.tileHeight(); dy++) {
				for (int dx = 0; dx < UnitTypes::Protoss_Nexus.tileWidth(); dx++) {
					FastTilePosition tile_position = base_tile_position + FastTilePosition(dx, dy);
					if (threat_grid.ground_threat_excluding_workers(tile_position)) {
						return false;
					}
				}
			}
			return true;
		} else {
			return false;
		}
	};
	if (attacking_ &&
		worker_manager.average_workers_per_mineral() >= 2.0 &&
		is_safe_expand_possible()) {
		building_manager.request_base(base_state.natural_base());
	}
	if (Broodwar->self()->minerals() + base_state.mineable_mineral_count() < 1500 &&
		is_safe_expand_possible()) {
		building_manager.request_base(base_state.natural_base(), true);
	}*/
	if (cannons_near_base == 0 &&
		attacking_ &&
		combat_unit_not_near_base &&
		(!building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility) ||
		 training_manager.unit_count(UnitTypes::Protoss_Observer) >= 1)) {
		attack_minimum_ = 2 * 8;
		mode_ = Mode::Main;
	}
}

void ProtossStrategy::mode_defend_four_gate_goon()
{
	bool opponent_has_detection = (information_manager.enemy_exists(UnitTypes::Protoss_Observatory) ||
								   information_manager.enemy_exists(UnitTypes::Protoss_Observer));
	
	if (!four_gate_goon_handled_) {
		four_gate_goon_handled_ = true;
		if (building_manager.building_count(UnitTypes::Protoss_Gateway) >= 2 &&
			building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility) &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Robotics_Support_Bay) >= 1) {
			mode_ = Mode::Main;
			return;
		}
		if (opponent_has_detection ||
			Broodwar->getFrameCount() > 9000 ||
			training_manager.unit_count_built_or_in_progress(UnitTypes::Protoss_Dark_Templar) >= 1) {
			mode_ = Mode::Main;
			return;
		}
		building_manager.cancel_extra_buildings_of_type(UnitTypes::Protoss_Nexus, 2, 2);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Robotics_Facility);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Robotics_Support_Bay);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Observatory);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Stargate);
	}
	
	training_manager.gateway_train_distribution().clear();
	training_manager.robotics_facility_train_distribution().clear();
	training_manager.stargate_train_distribution().clear();
	
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1, true);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Assimilator) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1, true);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Cybernetics_Core) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 2, true);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Gateway) >= 2 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Templar_Archives) == 0) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1, true);
	}
	if (building_manager.building_count_including_planned(UnitTypes::Protoss_Citadel_of_Adun) >= 1) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1, true);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Gateway)) {
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Templar_Archives) >= 1 &&
			templar_archives_remaining_time() < UnitTypes::Protoss_Dragoon.buildTime() &&
			training_manager.unit_count(UnitTypes::Protoss_Dark_Templar) < 2) {
			training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Dark_Templar, 1.0);
		} else {
			update_gateway_train_distribution();
			additional_gateways();
		}
	}
	attack_check_condition();
	
	if (training_manager.unit_count(UnitTypes::Protoss_Dark_Templar) >= 2 &&
		training_manager.unit_count_completed(UnitTypes::Protoss_Dark_Templar) >= 1) {
		initial_dark_templar_attack_ = true;
		mode_ = Mode::Main;
		return;
	}
}

void ProtossStrategy::mode_defend_three_hatch_ling_ffe()
{
	if (!three_hatch_ling_handled_) {
		three_hatch_ling_handled_ = true;
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Robotics_Facility);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Robotics_Support_Bay);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Observatory);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Stargate);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Citadel_of_Adun);
		building_manager.cancel_buildings_of_type(UnitTypes::Protoss_Templar_Archives);
	}
	
	auto const calculate_scaled_photon_cannon_supply = [&](){
		double scaled_count = 0.0;
		double max_hp = 3.0 * UnitTypes::Protoss_Photon_Cannon.maxHitPoints() + UnitTypes::Protoss_Photon_Cannon.maxShields();
		for (auto& information_unit : information_manager.my_units()) {
			if (information_unit->type == UnitTypes::Protoss_Photon_Cannon) {
				Unit unit = information_unit->unit;
				if (unit->isCompleted()) {
					double hp = 3.0 * unit->getHitPoints() + unit->getShields();
					scaled_count += (hp / max_hp);
				}
			}
		}
		return scaled_count * tactics_manager.defense_supply_equivalent(UnitTypes::Protoss_Photon_Cannon);
	};
	
	if (calculate_scaled_photon_cannon_supply() < tactics_manager.enemy_offense_supply()) {
		worker_manager.combat(std::min(10, training_manager.unit_count(UnitTypes::Protoss_Probe) - 4),
						  stage_chokepoint_ != nullptr ? chokepoint_center(stage_chokepoint_) : Positions::Unknown);
	} else {
		worker_manager.stop_combat();
	}
	
	int supply = opening_supply_count();
	if (supply >= 8) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1) worker_manager.send_initial_scout();
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Pylon)) {
		building_manager.set_automatic_supply(true);
		if (supply >= 10) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
			if (base_state.start_base_count() >= 4 &&
				building_manager.building_count_including_warping(UnitTypes::Protoss_Forge) >= 1) worker_manager.send_second_scout();
		}
		if (building_manager.building_exists(UnitTypes::Protoss_Forge)) {
			opening_forge_fast_expand_build_cannons(4);
		}
		if (building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) >= 4) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
		}
	}
	
	if (building_manager.building_count_including_warping(UnitTypes::Protoss_Pylon) >= 1 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Photon_Cannon) >= 4 &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Gateway) >= 1) {
		additional_gateways();
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
		if (building_manager.building_count(UnitTypes::Protoss_Photon_Cannon) >= 4) {
			bool attacker_in_base = false;
			for (auto& enemy_unit : information_manager.enemy_units()) {
				if (enemy_unit->base_distance == 0 && can_attack(enemy_unit->type, false)) {
					attacker_in_base = true;
					break;
				}
			}
			if (!attacker_in_base) {
				worker_manager.stop_combat();
				mode_ = Mode::Main;
				return;
			}
		}
	}
	
	if (!contains(base_state.controlled_and_planned_bases(), base_state.natural_base())) {
		worker_manager.stop_combat();
		mode_ = Mode::Main;
		return;
	}
}

void ProtossStrategy::mode_reactive_fast_expand()
{
	reactive_fast_expand_handled_ = true;
	if (!building_manager.request_bases(2) ||
		building_manager.building_count_including_warping(UnitTypes::Protoss_Nexus) >= 2) {
		mode_ = Mode::Main;
	}
	attack_check_condition();
}

void ProtossStrategy::mode_main()
{
	bool defending_rush = is_defending_rush();
	bool more_anti_air = need_more_anti_air();
	bool contained = is_contained();
	training_manager.set_prioritize_training(defending_rush || contained || more_anti_air || tactics_manager.is_opponent_army_too_large());
	bool do_not_expand = defending_rush || more_anti_air || contained || dark_templars_without_mobile_detection();
	
	if (building_manager.building_exists(UnitTypes::Protoss_Gateway)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Assimilator) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1);
		}
	} else {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, 1);
	}
	
	bool cannons_in_natural = (base_state.natural_base() != nullptr &&
							   !base_state.is_backdoor_natural() &&
							   building_manager.base_defense_cannons_including_planned(base_state.natural_base()) >= 1);
	bool air_attack_expected = (information_manager.enemy_exists(UnitTypes::Terran_Wraith) ||
								expect_zerg_flyers() ||
								information_manager.enemy_exists(UnitTypes::Protoss_Shuttle) ||
								information_manager.enemy_exists(UnitTypes::Terran_Dropship));
	bool cloaked_attack_expected_soon = (opponent_model.cloaked_present() ||
										 information_manager.enemy_completed_exists(UnitTypes::Protoss_Templar_Archives));
	bool cloaked_attack_expected = expect_cloaked_attack();
	bool build_emergency_cannons = (air_attack_expected ||
									(cloaked_attack_expected_soon &&
									 !handle_cloaked_attack_with_observers_ &&
									 !cannons_in_natural &&
									 training_manager.unit_count(UnitTypes::Protoss_Observer) == 0 &&
									 building_manager.building_count(UnitTypes::Protoss_Robotics_Facility) == 0) ||
									(cloaked_attack_expected &&
									 (!building_manager.building_exists(UnitTypes::Protoss_Assimilator) ||
									  !building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) &&
									 !cannons_in_natural));
	bool wait_for_emergency_cannons = false;
	if (build_emergency_cannons) {
		const BWEM::Base* base = base_state.main_base();
		int wanted_cannons = (building_manager.building_count_including_planned(UnitTypes::Protoss_Forge) >= 1) ? 2 : 1;
		if (base != nullptr) {
			wait_for_emergency_cannons = (building_manager.base_defense_cannons_including_planned(base) < wanted_cannons);
			if (base_state.natural_base() != nullptr &&
				!base_state.is_backdoor_natural() &&
				contains(base_state.controlled_and_planned_bases(), base_state.natural_base()) &&
				stage_type_ != StageType::Minerals) {
				base = base_state.natural_base();
			}
			if (wait_for_emergency_cannons) {
				building_manager.request_base_defense_pylon(base);
				if (building_manager.base_defense_pylon_exists(base)) {
					building_manager.set_requested_base_defense_cannon_count_at_least(base, 2);
				}
			}
		} else {
			wait_for_emergency_cannons = (building_manager.building_count_including_planned(UnitTypes::Protoss_Photon_Cannon) < wanted_cannons);
			if (wait_for_emergency_cannons) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Photon_Cannon, 2, true);
			}
		}
		if (wait_for_emergency_cannons) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1, true);
			reserve_resources_for_emergency_cannons();
		}
	}
	
	if (!wait_for_emergency_cannons &&
		cloaked_attack_expected &&
		!building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, 1, true);
		if (building_manager.building_count_including_warping(UnitTypes::Protoss_Assimilator) >= 1) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Cybernetics_Core, 1, true);
		}
	}
	
	if (!wait_for_emergency_cannons && building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core)) {
		if ((cloaked_attack_expected || opponent_model.blocked_expansion_seen()) && !observer_tree_done()) {
			handle_cloaked_attack_with_observers_ = true;
			observer_tree(true);
		} else if (cloaked_attack_expected && building_manager.building_count_including_planned(UnitTypes::Protoss_Forge) == 0) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1, true);
		} else if (need_counter_carriers() && !counter_carriers_done()) {
			counter_carriers();
		} else {
			bool cannons_in_all_bases = (opponent_model.cloaked_present() &&
										 !cannons_in_natural &&
										 training_manager.unit_count(UnitTypes::Protoss_Observer) >= 1);
			if (air_attack_expected) {
				building_placement_manager.restore_saved_ffe_natural_base_defense_positions();
			}
			base_defense_cannons(cannons_in_all_bases);
			tech_tree();
		}
	}
	
	if (building_manager.building_count(UnitTypes::Protoss_Nexus) >= 1 && building_manager.building_exists(UnitTypes::Protoss_Assimilator)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Assimilator, base_state.controlled_geyser_count());
	}
	
	if (worker_manager.average_workers_per_mineral() >= 2.0 && !base_state.next_available_bases().empty() && !do_not_expand) {
		building_manager.request_next_base();
	}
	if (Broodwar->self()->minerals() + base_state.mineable_mineral_count() < 1500) {
		building_manager.request_next_base(true);
	}
	
	if (building_manager.non_pylon_building_placement_failed() &&
		!building_manager.pylon_placement_failed()) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Pylon, 1 + building_manager.building_count(UnitTypes::Protoss_Pylon));
	}
	
	attack_check_condition();
	update_train_distributions();
	additional_gateways();
	
	if (!fast_pool_handled_ &&
		(opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool ||
		 opponent_model.enemy_opening() == EnemyOpening::Z_9Pool ||
		 opponent_model.enemy_opening() == EnemyOpening::Z_9PoolSpeed)) mode_ = Mode::DefendFastPool;
	if (!proxy_gate_handled_ && opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate) mode_ = Mode::DefendProxyGate;
	if (!cannon_rush_handled_ && opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) mode_ = Mode::DefendCannonRush;
	if (!four_gate_goon_handled_ && opponent_model.enemy_opening() == EnemyOpening::P_4GateGoon) mode_ = Mode::DefendFourGateGoon;
	if (!three_hatch_ling_handled_ &&
		opening_forge_fast_expand_completed_ &&
		opponent_model.enemy_race() == Races::Zerg &&
		Broodwar->getFrameCount() < 10000 &&
		information_manager.enemy_seen_count(UnitTypes::Zerg_Zergling) > 8 &&
		is_enemy_offense_larger_than_defense() &&
		worker_manager.lost_worker_count() < 10 &&
		contains(base_state.border().inside_areas(), base_state.natural_base()->GetArea()) &&
		!information_manager.enemy_seen(UnitTypes::Zerg_Hydralisk) &&
		!information_manager.enemy_seen(UnitTypes::Zerg_Lurker)) {
		mode_ = Mode::DefendThreeHatchLingFFE;
	}
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonTurtle) attack_frame_ = std::max(attack_frame_, 10000);
}

void ProtossStrategy::base_defense_cannons(bool cannons_in_all_bases)
{
	if (building_manager.building_exists(UnitTypes::Protoss_Forge)) {
		const BWEM::Base* skip_base = nullptr;
		if (!cannons_in_all_bases) {
			skip_base = base_state.is_backdoor_natural() ? base_state.natural_base() : base_state.start_base();
		}
		
		for (auto& base : base_state.controlled_bases()) {
			if (base != skip_base) {
				if (!building_manager.base_defense_pylon_exists(base)) {
					building_manager.request_base_defense_pylon(base);
				} else {
					building_manager.set_requested_base_defense_cannon_count_at_least(base, 2);
				}
			}
		}
		if (expect_zerg_flyers()) {
			base_defense_cannons_for_zerg_flyers();
		}
	}
}

void ProtossStrategy::base_defense_cannons_for_zerg_flyers()
{
	bool two_planned_in_each_base = true;
	for (auto& base : base_state.controlled_bases()) {
		if (!building_manager.base_defense_pylon_exists(base) ||
			building_manager.base_defense_cannons_including_planned(base) < 2) {
			two_planned_in_each_base = false;
			break;
		}
	}
	if (two_planned_in_each_base) {
		for (auto& base : base_state.controlled_bases()) {
			building_manager.set_requested_base_defense_cannon_count_at_least(base, 3);
		}
	}
}

void ProtossStrategy::tech_tree()
{
	bool want_singularity_charge = (opponent_model.enemy_race() != Races::Zerg || dragoons_verus_zerg());
	bool have_singularity_charge = Broodwar->self()->getUpgradeLevel(UpgradeTypes::Singularity_Charge) > 0;
	if (want_singularity_charge && building_manager.building_count_including_planned(UnitTypes::Protoss_Cybernetics_Core) > 0) building_manager.request_upgrade(UpgradeTypes::Singularity_Charge);
	bool tier1_done = building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core) && (have_singularity_charge || !want_singularity_charge);
	
	bool tier2_done = false;
	if (tier1_done) {
        if (opponent_model.enemy_race() == Races::Zerg && !dragoons_verus_zerg() && hydralisks_too_fast()) {
            building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1, true);
            if (building_manager.building_exists(UnitTypes::Protoss_Citadel_of_Adun)) {
                building_manager.request_upgrade(UpgradeTypes::Leg_Enhancements, true);
            }
        } else if (building_manager.building_count_including_planned(UnitTypes::Protoss_Nexus) >= 2 &&
			building_manager.building_count_including_planned(UnitTypes::Protoss_Forge) == 0) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
		} else if (opponent_model.enemy_race() == Races::Zerg) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Stargate, 1);
			if (building_manager.building_exists(UnitTypes::Protoss_Stargate)) {
				templar_tree();
				if (templar_tree_done()) tier2_done = true;
			}
		} else if (building_manager.building_count_including_planned(UnitTypes::Protoss_Templar_Archives) > 0 &&
				   building_manager.building_count_including_planned(UnitTypes::Protoss_Robotics_Facility) == 0) {
			templar_tree();
			if (templar_tree_done()) {
				robotics_facility_tree();
				if (robotics_facility_tree_done()) tier2_done = true;
			}
		} else {
			robotics_facility_tree();
			if (robotics_facility_tree_done()) {
				templar_tree();
				if (templar_tree_done()) tier2_done = true;
			}
		}
	}
	
	bool tier3_done = false;
	if (tier2_done) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 1);
		if (building_manager.building_exists(UnitTypes::Protoss_Forge)) {
			forge_updates();
			
			if (opponent_model.enemy_race() == Races::Terran) {
				if (!building_manager.building_exists(UnitTypes::Protoss_Arbiter_Tribunal) &&
					determine_late_game_strategy() == LateGameStrategy::Carriers) {
					carrier_tree();
					if (carrier_tree_done()) tier3_done = true;
				} else {
					arbiter_tree();
					if (arbiter_tree_done()) tier3_done = true;
				}
			} else if (opponent_model.enemy_race() == Races::Zerg) {
				if (want_reavers()) {
					building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1);
					if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility)) {
						building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Support_Bay, 1);
					}
				}
				if (!want_reavers() || (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility) && building_manager.building_exists(UnitTypes::Protoss_Robotics_Support_Bay))) {
					tier3_done = true;
				}
			} else {
				tier3_done = true;
			}
		}
	}
	
	MineralGas income = spending_manager.income_per_minute();
	if (tier3_done && base_state.mining_base_count() >= 3) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Forge, 2);
	}
	
	if (training_manager.unit_count(UnitTypes::Protoss_Observer) >= 2) {
		building_manager.request_upgrade(UpgradeTypes::Gravitic_Boosters);
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Observer) >= 4 &&
		Broodwar->self()->getUpgradeLevel(UpgradeTypes::Gravitic_Boosters) >= 1) {
		building_manager.request_upgrade(UpgradeTypes::Sensor_Array);
	}
	if (training_manager.unit_count(UnitTypes::Protoss_High_Templar) >= 3) {
		building_manager.request_upgrade(UpgradeTypes::Khaydarin_Amulet);
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Shuttle) >= 2 &&
		training_manager.unit_count(UnitTypes::Protoss_Reaver) >= 2) {
		building_manager.request_upgrade(UpgradeTypes::Gravitic_Drive);
	}
	if (training_manager.unit_count(UnitTypes::Protoss_Reaver) >= 4) {
		building_manager.request_upgrade(UpgradeTypes::Scarab_Damage);
	}
}

void ProtossStrategy::observer_tree(bool important)
{
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1, important);
	if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Observatory, 1, important);
	}
}

bool ProtossStrategy::observer_tree_done()
{
	return (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility) &&
			building_manager.building_exists(UnitTypes::Protoss_Observatory));
}

void ProtossStrategy::robotics_facility_tree()
{
	if (opponent_model.enemy_race() == Races::Terran) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Facility, 1);
		if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility)) {
			if (want_reavers()) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Support_Bay, 1);
			}
			if (!want_reavers() ||
				building_manager.building_exists(UnitTypes::Protoss_Robotics_Support_Bay)) {
				building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Observatory, 1);
			}
		}
	} else {
		observer_tree();
		if (observer_tree_done() && want_reavers()) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Robotics_Support_Bay, 1);
		}
	}
}

bool ProtossStrategy::robotics_facility_tree_done()
{
	return (observer_tree_done() &&
			(!want_reavers() || building_manager.building_exists(UnitTypes::Protoss_Robotics_Support_Bay)));
}

void ProtossStrategy::templar_tree()
{
	if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Leg_Enhancements) == 0 || building_manager.building_count_including_warping(UnitTypes::Protoss_Templar_Archives) == 0) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Citadel_of_Adun)) {
		building_manager.request_upgrade(UpgradeTypes::Leg_Enhancements);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Citadel_of_Adun) &&
		done_or_in_progress(UpgradeTypes::Leg_Enhancements)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
	}
	if (building_manager.building_exists(UnitTypes::Protoss_Templar_Archives) && Broodwar->self()->getUpgradeLevel(UpgradeTypes::Leg_Enhancements) > 0) {
		building_manager.request_research(TechTypes::Psionic_Storm);
	}
}

bool ProtossStrategy::templar_tree_done()
{
	return (building_manager.building_exists(UnitTypes::Protoss_Templar_Archives) &&
			done_or_in_progress(UpgradeTypes::Leg_Enhancements) &&
			done_or_in_progress(TechTypes::Psionic_Storm));
}

void ProtossStrategy::forge_updates()
{
	bool ground_weapons_max = Broodwar->self()->getUpgradeLevel(UpgradeTypes::Protoss_Ground_Weapons) == UpgradeTypes::Protoss_Ground_Weapons.maxRepeats();
	bool ground_armor_max = Broodwar->self()->getUpgradeLevel(UpgradeTypes::Protoss_Ground_Armor) == UpgradeTypes::Protoss_Ground_Armor.maxRepeats();
	bool plasma_shield_max = Broodwar->self()->getUpgradeLevel(UpgradeTypes::Protoss_Plasma_Shields) == UpgradeTypes::Protoss_Plasma_Shields.maxRepeats();
	bool dual_forge = building_manager.building_count(UnitTypes::Protoss_Forge) > 1;
	if (!ground_weapons_max) building_manager.request_upgrade(UpgradeTypes::Protoss_Ground_Weapons);
	if (!ground_armor_max &&
		(ground_weapons_max || dual_forge)) building_manager.request_upgrade(UpgradeTypes::Protoss_Ground_Armor);
	if (!plasma_shield_max &&
		((ground_weapons_max && ground_armor_max) || (ground_weapons_max && dual_forge))) building_manager.request_upgrade(UpgradeTypes::Protoss_Plasma_Shields);
}

void ProtossStrategy::arbiter_tree()
{
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Stargate, 1);
	if (building_manager.building_exists(UnitTypes::Protoss_Stargate)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Arbiter_Tribunal, 1);
		if (building_manager.building_exists(UnitTypes::Protoss_Arbiter_Tribunal)) {
			building_manager.request_research(TechTypes::Stasis_Field);
		}
	}
}

bool ProtossStrategy::arbiter_tree_done()
{
	return (building_manager.building_exists(UnitTypes::Protoss_Stargate) &&
			building_manager.building_exists(UnitTypes::Protoss_Arbiter_Tribunal) &&
			Broodwar->self()->hasResearched(TechTypes::Stasis_Field));
}

void ProtossStrategy::carrier_tree()
{
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Stargate, 1);
	if (building_manager.building_exists(UnitTypes::Protoss_Stargate)) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Fleet_Beacon, 1);
		if (building_manager.building_exists(UnitTypes::Protoss_Fleet_Beacon) && training_manager.unit_count_completed(UnitTypes::Protoss_Carrier) > 0) {
			building_manager.request_upgrade(UpgradeTypes::Carrier_Capacity);
		}
	}
}

bool ProtossStrategy::carrier_tree_done()
{
	return (building_manager.building_exists(UnitTypes::Protoss_Stargate) &&
			building_manager.building_exists(UnitTypes::Protoss_Fleet_Beacon) &&
			Broodwar->self()->getUpgradeLevel(UpgradeTypes::Carrier_Capacity) > 0);
}

void ProtossStrategy::update_train_distributions()
{
	update_stargate_train_distribution();
	update_robotics_facility_train_distribution();
	update_gateway_train_distribution();
}

void ProtossStrategy::update_stargate_train_distribution()
{
	training_manager.stargate_train_distribution().clear();
	if (building_manager.building_exists(UnitTypes::Protoss_Stargate)) {
		if (opponent_model.enemy_race() == Races::Zerg) {
			int max_corsairs;
			if (!need_more_anti_air()) {
				if (is_defending_rush()) {
					max_corsairs = 0;
				} else if (is_contained()) {
					max_corsairs = 1;
				} else {
					max_corsairs = 3;
				}
			} else {
				max_corsairs = 99;
			}
			if (training_manager.unit_count(UnitTypes::Protoss_Corsair) < max_corsairs) {
				training_manager.stargate_train_distribution().set(UnitTypes::Protoss_Corsair, 1.0);
			}
		}
		
		if (building_manager.building_exists(UnitTypes::Protoss_Arbiter_Tribunal)) {
			int max_arbiters;
			bool has_stasis = Broodwar->self()->hasResearched(TechTypes::Stasis_Field);
			if (is_defending_rush() || is_contained()) {
				max_arbiters = has_stasis ? 1 : 0;
			} else {
				int current_units = (training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) +
									 training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) +
									 training_manager.unit_count_completed(UnitTypes::Protoss_High_Templar) +
									 training_manager.unit_count_completed(UnitTypes::Protoss_Archon) * 2 +
									 training_manager.unit_count_completed(UnitTypes::Protoss_Dark_Archon) * 2 +
									 training_manager.unit_count_completed(UnitTypes::Protoss_Reaver) * 2 +
									 training_manager.unit_count_completed(UnitTypes::Protoss_Carrier) * 3);
				max_arbiters = has_stasis ? std::max(1, (current_units + 7) / 8) : 1;
			}
			if (training_manager.unit_count(UnitTypes::Protoss_Arbiter) < max_arbiters) {
				training_manager.stargate_train_distribution().set(UnitTypes::Protoss_Arbiter, 1.0);
			}
		}
		
		if (opponent_model.enemy_race() == Races::Terran &&
			building_manager.building_exists(UnitTypes::Protoss_Fleet_Beacon) &&
			!is_defending_rush() &&
			!is_contained()) {
			training_manager.stargate_train_distribution().set(UnitTypes::Protoss_Carrier, 1.0);
		}
	}
}

void ProtossStrategy::update_robotics_facility_train_distribution()
{
	training_manager.robotics_facility_train_distribution().clear();
	if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Facility)) {
		bool save_gas_for_dark_archons = (need_counter_carriers() &&
										  potential_dark_archon_count() < counter_carriers_requested_dark_archon_count());
		bool shuttle_for_zealot_bombing = (information_manager.enemy_exists(UnitTypes::Terran_Siege_Tank_Siege_Mode) &&
										   training_manager.unit_count(UnitTypes::Protoss_Zealot) >= 1 &&
										   information_manager.enemy_count(UnitTypes::Terran_Marine) < 4 &&
										   information_manager.enemy_count(UnitTypes::Terran_Goliath) < 2);
		bool first_observer_needed_for_detection = opponent_model.cloaked_or_mine_present() || expect_cloaked_attack() || opponent_model.blocked_expansion_seen();
		
		if (building_manager.building_exists(UnitTypes::Protoss_Observatory)) {
			int max_observers;
			if (is_defending_rush() || is_contained() || save_gas_for_dark_archons) {
				max_observers = first_observer_needed_for_detection ? 1 : 0;
			} if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Support_Bay) &&
				  want_reavers() &&
				  (training_manager.unit_count_built_or_in_progress(UnitTypes::Protoss_Reaver) == 0 ||
				   training_manager.unit_count_built_or_in_progress(UnitTypes::Protoss_Shuttle) == 0)) {
				max_observers = (first_observer_needed_for_detection || opponent_model.enemy_race() != Races::Terran) ? 1 : 0;
			} else {
				max_observers = (opponent_model.enemy_race() == Races::Zerg) ? 2 : 3;
			}
			if (training_manager.unit_count(UnitTypes::Protoss_Observer) < max_observers) {
				training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Observer, 1.0);
				if (first_observer_needed_for_detection && training_manager.unit_count(UnitTypes::Protoss_Observer) == 0) {
					training_manager.set_prioritize_training(true);
					training_manager.set_worker_cut(true);
				}
			}
		}
		
		if (building_manager.building_exists(UnitTypes::Protoss_Robotics_Support_Bay) && want_reavers()) {
			bool skip_first_observer = (is_defending_rush() || is_contained()) && !first_observer_needed_for_detection;
			bool prioritize_first_observer = (training_manager.unit_count(UnitTypes::Protoss_Observer) == 0 &&
											  building_manager.building_exists(UnitTypes::Protoss_Observatory) &&
											  !skip_first_observer);
			if (!prioritize_first_observer && !save_gas_for_dark_archons) {
				int current_gateway_units = (training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) +
											 training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) +
											 training_manager.unit_count_completed(UnitTypes::Protoss_Dark_Templar) +
											 training_manager.unit_count_completed(UnitTypes::Protoss_Archon));
				int max_reavers = (current_gateway_units + 1) / 4;
				int reaver_cap_for_shuttle_reaver_micro = 1 + training_manager.unit_count_completed(UnitTypes::Protoss_Shuttle);
				if (shuttle_for_zealot_bombing) reaver_cap_for_shuttle_reaver_micro--;
				max_reavers = std::min(max_reavers, reaver_cap_for_shuttle_reaver_micro);
				if (training_manager.unit_count(UnitTypes::Protoss_Reaver) < max_reavers) {
					training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Reaver, 1.0);
				}
			}
		}
		
		int max_shuttles = 0;
		if (want_reavers()) max_shuttles += training_manager.unit_count(UnitTypes::Protoss_Reaver);
		if (shuttle_for_zealot_bombing) max_shuttles++;
		if (max_shuttles == 0 &&
			opponent_model.enemy_race() == Races::Terran &&
			want_reavers() &&
			!building_manager.building_exists(UnitTypes::Protoss_Observatory) &&
			!building_manager.building_exists(UnitTypes::Protoss_Robotics_Support_Bay)) {
			max_shuttles = 1;
		}
		
		if (training_manager.unit_count(UnitTypes::Protoss_Shuttle) < max_shuttles) {
			training_manager.robotics_facility_train_distribution().set(UnitTypes::Protoss_Shuttle, 1.0);
		}
	}
}

void ProtossStrategy::update_gateway_train_distribution()
{
	bool zealot_available = building_manager.building_exists(UnitTypes::Protoss_Gateway);
	bool speedlot_available = zealot_available && Broodwar->self()->getUpgradeLevel(UpgradeTypes::Leg_Enhancements) > 0;
	bool dragoon_available = zealot_available && building_manager.building_exists(UnitTypes::Protoss_Cybernetics_Core);
	bool templar_available = zealot_available && building_manager.building_exists(UnitTypes::Protoss_Templar_Archives);
	
	bool dragoon = false;
	bool high_templar = false;
	bool dark_templar = false;
	
	switch (opponent_model.enemy_race()) {
		case Races::Zerg: {
			bool more_anti_air = need_more_anti_air();
			dragoon = dragoon_available && dragoons_verus_zerg();
			high_templar = templar_available && (!more_anti_air || !dragoon);
			dark_templar = templar_available && training_manager.unit_count(UnitTypes::Protoss_Corsair) >= 3 && !more_anti_air;
			break; }
		case Races::Terran: {
			dragoon = dragoon_available;
			high_templar = templar_available;
			dark_templar = templar_available && information_manager.enemy_count(UnitTypes::Terran_Science_Vessel) == 0 && information_manager.enemy_count(UnitTypes::Terran_Comsat_Station) == 0 && information_manager.enemy_count(UnitTypes::Terran_Missile_Turret) == 0;
			break; }
		case Races::Protoss:
			dragoon = dragoon_available;
			high_templar = templar_available;
			dark_templar = templar_available && information_manager.enemy_count(UnitTypes::Protoss_Observer) == 0 && information_manager.enemy_count(UnitTypes::Protoss_Observatory) == 0 && information_manager.enemy_count(UnitTypes::Protoss_Photon_Cannon) == 0;
			break;
		default:
			dragoon = dragoon_available;
			high_templar = templar_available;
			break;
	}
	
	if (high_templar && !done_or_in_progress(TechTypes::Psionic_Storm)) high_templar = false;
	if (high_templar && training_manager.unit_count(UnitTypes::Protoss_High_Templar) >= 8) high_templar = false;
	if (dark_templar && training_manager.unit_count(UnitTypes::Protoss_Dark_Templar) >= 8) dark_templar = false;
	
	if (need_counter_carriers()) {
		int requested_dark_archon_count = counter_carriers_requested_dark_archon_count();
		micro_manager.set_requested_dark_archon_count(requested_dark_archon_count);
		if (templar_available && potential_dark_archon_count() < requested_dark_archon_count) {
			dragoon = false;
			high_templar = false;
			dark_templar = true;
		}
	} else {
		micro_manager.set_requested_dark_archon_count(0);
	}
	
	if (high_templar && !want_high_templars()) high_templar = false;
	
	int prevent_high_templar_from_archon_warp_count;
	if (opponent_model.enemy_race() == Races::Zerg) {
		prevent_high_templar_from_archon_warp_count = 0;
	} else if (opponent_model.enemy_race() == Races::Protoss) {
		prevent_high_templar_from_archon_warp_count = 4;
	} else {
		prevent_high_templar_from_archon_warp_count = -1;
	}
	micro_manager.set_prevent_high_templar_from_archon_warp_count(prevent_high_templar_from_archon_warp_count);
	
	update_gateway_train_distribution(dragoon, high_templar, dark_templar);
}

void ProtossStrategy::update_gateway_train_distribution(bool dragoon,bool high_templar,bool dark_templar)
{
	CostPerMinute robotics_facility_cpm = training_manager.robotics_facility_train_distribution().cost_per_minute(building_manager.building_count(UnitTypes::Protoss_Robotics_Facility));
	CostPerMinute stargate_cpm = training_manager.stargate_train_distribution().cost_per_minute(building_manager.building_count(UnitTypes::Protoss_Stargate));
	CostPerMinute remaining_cpm = spending_manager.worker_training_cost_per_minute() + robotics_facility_cpm + stargate_cpm;
	double minerals = spending_manager.income_per_minute().minerals + Broodwar->self()->minerals() - remaining_cpm.minerals;
	double gas = spending_manager.income_per_minute().gas + Broodwar->self()->gas() - remaining_cpm.gas;
	double ratio = minerals / gas;
	if (std::isfinite(ratio) && ratio > 0.0 && (dragoon || high_templar || dark_templar)) {
		double b = dragoon ? 1.0 : 0.0;
		double c = high_templar ? 0.2 : 0.0;
		double d = dark_templar ? 0.4 : 0.0;
		double a = std::max(0.0, b * (ratio * 0.4 - 1.0) + c * (ratio * 1.2 - 0.4) + d * (ratio * 0.8 - 1.0));
		
		training_manager.gateway_train_distribution().clear();
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, a);
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Dragoon, b);
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_High_Templar, c);
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Dark_Templar, d);
	} else {
		training_manager.gateway_train_distribution().clear();
		training_manager.gateway_train_distribution().set(UnitTypes::Protoss_Zealot, 1.0);
	}
}

void ProtossStrategy::reserve_resources_for_emergency_cannons()
{
	if (!building_manager.building_exists(UnitTypes::Protoss_Forge) &&
		building_manager.building_count_including_warping(UnitTypes::Protoss_Forge) >= 1) {
		for (auto& information_unit : information_manager.my_units()) {
			if (information_unit->type == UnitTypes::Protoss_Forge) {
				int start_frame = information_unit->first_frame;
				int end_frame = start_frame + UnitTypes::Protoss_Forge.buildTime() + building_extra_frames(UnitTypes::Protoss_Forge);
				double fraction = double(Broodwar->getFrameCount() - start_frame) / double(end_frame - start_frame);
				spending_manager.reserve_resources(MineralGas(UnitTypes::Protoss_Photon_Cannon) * fraction);
				break;
			}
		}
	}
}

bool ProtossStrategy::want_reavers()
{
	return information_manager.enemy_count(UnitTypes::Protoss_High_Templar) <= 1;
}

bool ProtossStrategy::want_high_templars()
{
	return !is_defending_rush() && !is_contained() && !opponent_model.emp_seen();
}

bool ProtossStrategy::dragoons_verus_zerg()
{
	return (zerg_dragoon_strategy_ ||
			need_more_anti_air() ||
			(information_manager.enemy_count(UnitTypes::Zerg_Lurker) >= 2 &&
			 training_manager.unit_count(UnitTypes::Protoss_Dragoon) <= information_manager.enemy_count(UnitTypes::Zerg_Lurker)));
}

bool ProtossStrategy::need_more_anti_air()
{
	int anti_air_count = (training_manager.unit_count(UnitTypes::Protoss_Dragoon) +
						  training_manager.unit_count(UnitTypes::Protoss_Corsair) +
						  training_manager.unit_count(UnitTypes::Protoss_Scout));
	return (anti_air_count < (3 * zerg_flyer_count() / 2) ||
			((information_manager.enemy_exists(UnitTypes::Zerg_Spire) || information_manager.enemy_exists(UnitTypes::Zerg_Greater_Spire)) &&
			 anti_air_count < 6));
}

bool ProtossStrategy::hydralisks_too_fast()
{
	bool muscular_augments = std::any_of(Broodwar->enemies().begin(), Broodwar->enemies().end(), [](auto& player){
		return (information_manager.upgrade_level(player, UpgradeTypes::Muscular_Augments) > 0);
	});
	return (muscular_augments && !done_or_in_progress(UpgradeTypes::Leg_Enhancements));
}

int ProtossStrategy::zerg_flyer_count()
{
	return (information_manager.enemy_count(UnitTypes::Zerg_Mutalisk) +
			information_manager.enemy_count(UnitTypes::Zerg_Guardian) +
			information_manager.enemy_count(UnitTypes::Zerg_Devourer) +
			(information_manager.enemy_count(UnitTypes::Zerg_Scourge) + 1) / 2 +
			information_manager.enemy_count(UnitTypes::Zerg_Queen));
}

bool ProtossStrategy::expect_zerg_flyers()
{
	return (information_manager.enemy_exists(UnitTypes::Zerg_Spire) ||
			information_manager.enemy_exists(UnitTypes::Zerg_Greater_Spire) ||
			zerg_flyer_count() > 0);
}

bool ProtossStrategy::expect_cloaked_attack()
{
	return (expect_dark_templars() ||
			opponent_model.cloaked_present() ||
			expect_lurkers() ||
			information_manager.enemy_exists(UnitTypes::Terran_Control_Tower));
}

bool ProtossStrategy::need_counter_carriers()
{
	return information_manager.enemy_count(UnitTypes::Protoss_Carrier) > 0 || information_manager.enemy_exists(UnitTypes::Protoss_Fleet_Beacon);
}

void ProtossStrategy::counter_carriers()
{
	if (building_manager.building_count_including_warping(UnitTypes::Protoss_Templar_Archives) == 0) {
		building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Citadel_of_Adun, 1);
		if (building_manager.building_exists(UnitTypes::Protoss_Citadel_of_Adun)) {
			building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Templar_Archives, 1);
		}
	}
	
	if (building_manager.building_exists(UnitTypes::Protoss_Templar_Archives)) {
		if (!Broodwar->self()->hasResearched(TechTypes::Mind_Control)) {
			building_manager.request_research(TechTypes::Mind_Control);
		} else if (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Argus_Talisman) == 0) {
			building_manager.request_upgrade(UpgradeTypes::Argus_Talisman);
		}
	}
}

bool ProtossStrategy::counter_carriers_done()
{
	return (building_manager.building_exists(UnitTypes::Protoss_Templar_Archives) &&
			Broodwar->self()->hasResearched(TechTypes::Mind_Control) &&
			Broodwar->self()->getUpgradeLevel(UpgradeTypes::Argus_Talisman) > 0);
}

int ProtossStrategy::counter_carriers_requested_dark_archon_count()
{
	return clamp(2, information_manager.enemy_count(UnitTypes::Protoss_Carrier), 8);
}

int ProtossStrategy::potential_dark_archon_count()
{
	return training_manager.unit_count(UnitTypes::Protoss_Dark_Templar) / 2 + training_manager.unit_count(UnitTypes::Protoss_Dark_Archon);
}

void ProtossStrategy::additional_gateways()
{
	int additional_producers = training_manager.gateway_train_distribution().additional_producers();
	int requested_gateway_count = building_manager.building_count(UnitTypes::Protoss_Gateway) + additional_producers;
	building_manager.set_requested_building_count_at_least(UnitTypes::Protoss_Gateway, requested_gateway_count, training_manager.prioritize_training());
}

void ProtossStrategy::frame_inner()
{
	switch (mode_) {
		case Mode::Opening:
			mode_opening();
			break;
		case Mode::DefendFastPool:
			mode_defend_fast_pool();
			break;
		case Mode::DefendFastPoolFFE:
			mode_defend_fast_pool_ffe();
			break;
		case Mode::DefendProxyGate:
			mode_defend_proxy_gate();
			break;
		case Mode::DefendCannonRush:
			mode_defend_cannon_rush();
			break;
		case Mode::DefendFourGateGoon:
			mode_defend_four_gate_goon();
			break;
		case Mode::DefendThreeHatchLingFFE:
			mode_defend_three_hatch_ling_ffe();
			break;
		case Mode::ReactiveFastExpand:
			mode_reactive_fast_expand();
			break;
		case Mode::Main:
			mode_main();
			break;
	}
}

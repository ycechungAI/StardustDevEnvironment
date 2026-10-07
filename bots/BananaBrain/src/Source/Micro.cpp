#include "BananaBrain.h"

void DragoonState::update(Unit unit)
{
	unit_ = unit;
	if (last_attack_start_frame_ >= Broodwar->getFrameCount() ||
		Broodwar->getFrameCount() - last_attack_start_frame_ > kDragoonAttackFrames - Broodwar->getRemainingLatencyFrames()) {
		last_attack_start_frame_ = -1;
		if (unit_->isStartingAttack()) {
			last_attack_start_frame_ = Broodwar->getFrameCount();
		} else if (unit_->getLastCommand().getType() == UnitCommandTypes::Attack_Unit &&
				   unit_->getLastCommand().getTarget() != nullptr &&
				   unit_->getLastCommand().getTarget()->exists() &&
				   unit_->getLastCommand().getTarget()->isVisible()) {
			last_attack_start_frame_ = std::max(Broodwar->getFrameCount() + 1,
												std::max(unit_->getLastCommandFrame() + Broodwar->getRemainingLatencyFrames(),
														 Broodwar->getFrameCount() + unit_->getGroundWeaponCooldown()));
		}
	}
}

bool DragoonState::is_busy()
{
	int attack_frame_delta = Broodwar->getFrameCount() - last_attack_start_frame_;
	
	if (attack_frame_delta >= 0 && attack_frame_delta <= kDragoonAttackFrames - Broodwar->getRemainingLatencyFrames()) {
		return true;
	}
	
	if (attack_frame_delta < 0 && attack_frame_delta > -Broodwar->getRemainingLatencyFrames()) {
		Unit target = unit_->getLastCommand().getTarget();
		if (target == nullptr || !target->exists() || !target->isVisible() || !not_cloaked(target)) return false;
		
		Position unit_predicted_position = predict_position(unit_, -attack_frame_delta);
		Position target_predicted_position = predict_position(target, -attack_frame_delta);
		int predicted_distance = calculate_distance(unit_->getType(), unit_predicted_position, target->getType(), target_predicted_position);
		return predicted_distance <= weapon_max_range(WeaponTypes::Phase_Disruptor, Broodwar->self());
	}
	
	return false;
}

void UnstickState::update(Unit unit)
{
	unit_ = unit;
	if (!unit_->isMoving()) {
		stuck_since_ = -1;
	} else if (stuck_since_ != -1 && last_position_.isValid() && unit_->getPosition() != last_position_) {
		stuck_since_ = -1;
	} else if (stuck_since_ != -1 && unit_->getLastCommand().getType() == UnitCommandTypes::Stop &&
			   Broodwar->getRemainingLatencyFrames() == Broodwar->getLatencyFrames()) {
		stuck_since_ = -1;
	} else if (unit_->isAttackFrame()) {
		stuck_since_ = Broodwar->getFrameCount();
	}
	last_position_ = unit->getPosition();
}

bool UnstickState::is_stuck()
{
	if (unit_->isStuck()) return false;	// isStuck() means a unit overlaps with another unit, in which case we should not use stop. It is a different way of being stuck than this class detects.
	return (stuck_since_ != -1 && stuck_since_ < (Broodwar->getFrameCount() - Broodwar->getRemainingLatencyFrames() - 10));
}

bool CombatUnitTarget::should_update_target(Unit combat_unit) const
{
	bool result = false;
	if (Broodwar->getFrameCount() >= frame + kTargetUpdateFrames ||
		(combat_unit->getID() % kTargetUpdateFrames) == (Broodwar->getFrameCount() % kTargetUpdateFrames)) {
		result = true;
	} else if (unit != nullptr && (!unit->exists() || !unit->isVisible() || !not_cloaked(unit))) {
		result = true;
	}
	return result;
}

void MicroManager::prepare_combat()
{
	update_units_lists();
}

void MicroManager::apply_combat_orders()
{
	update_units_near_base();
	update_units_near_main_base();
	update_ignore_when_attacking();
	
	apply_transport_orders();
	apply_overlord_orders();
	apply_combat_unit_orders();
	apply_dark_templar_orders();
	apply_lurker_orders();
	apply_siege_tank_orders();
	apply_air_to_air_unit_orders();
	apply_mutalisk_orders();
	apply_observer_orders();
	apply_arbiter_orders();
	apply_science_vessel_orders();
	apply_high_templar_orders();
	apply_defiler_orders();
	apply_medic_orders();
	apply_dark_archon_orders();
	apply_comsat_station_orders();
	apply_flying_building_orders();
	
	expire_tentative_abilities();
}

void MicroManager::update_units_lists()
{
	combat_units_.clear();
	dark_templars_.clear();
	lurkers_.clear();
	siege_tanks_.clear();
	air_to_air_units_.clear();
	mutalisks_.clear();
	observers_.clear();
	high_templars_.clear();
	defilers_.clear();
	medics_.clear();
	dark_archons_.clear();
	transports_.clear();
	overlords_.clear();
	arbiters_.clear();
	science_vessels_.clear();
	comsat_stations_.clear();
	flying_buildings_.clear();
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		UnitType type = information_unit->type;
		if (!type.isWorker() && !type.isBuilding() && unit->isCompleted() &&
			unit->isVisible() && !is_disabled(unit) && !unit->isLoaded()) {
			bool controlled = true;
			if (type == UnitTypes::Protoss_Dark_Templar) {
				dark_templars_.push_back(unit);
			} else if (type == UnitTypes::Zerg_Lurker) {
				lurkers_.push_back(unit);
			} else if (is_siege_tank(type)) {
				siege_tanks_.push_back(unit);
			} else if (type == UnitTypes::Protoss_Corsair || type == UnitTypes::Terran_Valkyrie) {
				air_to_air_units_.push_back(unit);
			} else if (type == UnitTypes::Zerg_Mutalisk) {
				mutalisks_.push_back(unit);
			} else if (type == UnitTypes::Protoss_Observer) {
				observers_.push_back(unit);
			} else if (type == UnitTypes::Protoss_High_Templar) {
				high_templars_.push_back(unit);
			} else if (type == UnitTypes::Zerg_Defiler) {
				defilers_.push_back(unit);
			} else if (type == UnitTypes::Terran_Medic) {
				medics_.push_back(unit);
			} else if (type == UnitTypes::Protoss_Dark_Archon) {
				dark_archons_.push_back(unit);
			} else if (type == UnitTypes::Protoss_Shuttle || type == UnitTypes::Terran_Dropship) {
				transports_.push_back(unit);
			} else if (type == UnitTypes::Zerg_Overlord) {
				overlords_.push_back(unit);
			} else if (type == UnitTypes::Protoss_Arbiter) {
				arbiters_.push_back(unit);
			} else if (type == UnitTypes::Terran_Science_Vessel) {
				science_vessels_.push_back(unit);
			} else if (type != UnitTypes::Protoss_Interceptor &&
					   type != UnitTypes::Protoss_Scarab &&
					   (type.groundWeapon() != WeaponTypes::None ||
						type.airWeapon() != WeaponTypes::None ||
						type == UnitTypes::Protoss_Carrier ||
						type == UnitTypes::Protoss_Reaver)) {
				combat_units_.push_back(unit);
			} else {
				controlled = false;
			}
			if (controlled) {
				if (combat_state_.count(unit) == 0) combat_state_.emplace(unit, unit);
				combat_state_.at(unit).last_controlled_frame_ = Broodwar->getFrameCount();
			}
		} else if (type.isBuilding() && unit->isCompleted()) {
			if (type == UnitTypes::Terran_Comsat_Station) {
				comsat_stations_.push_back(unit);
			} else if (unit->isLifted()) {
				flying_buildings_.push_back(unit);
			}
		} else if (type.isWorker() && unit->isCompleted() && unit->isVisible() &&
				   !is_disabled(unit) && !unit->isLoaded() && worker_manager.is_combat(unit)) {
			combat_units_.push_back(unit);
			if (combat_state_.count(unit) == 0) combat_state_.emplace(unit, unit);
			combat_state_.at(unit).last_controlled_frame_ = Broodwar->getFrameCount();
		}
	}
	std::vector<Unit> uncontrolled_units;
	for (auto& entry : combat_state_) {
		if (entry.second.last_controlled_frame_ + 24 < Broodwar->getFrameCount()) {
			uncontrolled_units.push_back(entry.first);
		}
	}
	for (auto& uncontrolled_unit : uncontrolled_units) combat_state_.erase(uncontrolled_unit);
	
	all_enemy_units_.clear();
	harassable_enemy_units_.clear();
	enemy_units_threatening_buildings_or_workers_.clear();
	for (auto& information_unit : information_manager.enemy_units()) {
		Unit unit = information_unit->unit;
		if (unit->exists() && unit->isVisible() && not_incomplete(unit) && !unit->getType().isSpell()) all_enemy_units_.push_back(unit);
	}
	for (auto& unit : all_enemy_units_) {
		if (not_cloaked(unit) && !unit->isStasised() && !unit->isInvincible()) {
			harassable_enemy_units_.push_back(unit);
		}
	}
	const auto enemy_unit_threatens_building_or_worker = [&](auto enemy_unit){
		for (auto& information_unit : information_manager.my_units()) {
			if (information_unit->type.isBuilding() &&
				can_attack_in_range(enemy_unit, information_unit->unit)) {
				return true;
			}
		}
		for (auto& entry : worker_manager.worker_map()) {
			const Worker& worker = entry.second;
			if (!worker.order()->is_scouting()) {
				Unit worker_unit = worker.unit();
				if (can_attack_in_range(enemy_unit, worker_unit, 32)) {
					return true;
				}
			}
		}
		return false;
	};
	for (auto& enemy_unit : harassable_enemy_units_) {
		if (enemy_unit_threatens_building_or_worker(enemy_unit)) {
			enemy_units_threatening_buildings_or_workers_.insert(enemy_unit);
		}
	}
	
	extended_combat_units_.clear();
	std::copy(combat_units_.begin(), combat_units_.end(), std::back_inserter(extended_combat_units_));
	std::copy(lurkers_.begin(), lurkers_.end(), std::back_inserter(extended_combat_units_));
	std::copy(siege_tanks_.begin(), siege_tanks_.end(), std::back_inserter(extended_combat_units_));
}

int MicroManager::offense_distance_to_base(const EnemyCluster& cluster,const std::vector<Unit>& buildings_outside_base)
{
	int distance = INT_MAX;
	for (auto& enemy_unit : cluster.units()) {
		distance = std::min(distance, offense_distance_to_base(enemy_unit, buildings_outside_base));
	}
	return distance;
}

int MicroManager::offense_distance_to_base(const InformationUnit* enemy_unit,const std::vector<Unit>& buildings_outside_base)
{
	if (!enemy_unit->position.isValid()) return INT_MAX;
	
	int range = offense_max_range(enemy_unit->type, enemy_unit->player, false);
	if (range < 0) return INT_MAX;
	
	int distance = enemy_unit->base_distance;
	
	for (Unit unit : buildings_outside_base) {
		int building_distance = calculate_distance(enemy_unit->type, enemy_unit->position, unit->getType(), unit->getPosition());
		distance = std::min(distance, building_distance);
	}
	
	return std::max(0, distance - range);
}

void MicroManager::update_units_near_base()
{
	std::vector<Unit> buildings_outside_base;
	for (auto& information_unit : information_manager.my_units()) {
		if (information_unit->type.isBuilding() &&
			information_unit->base_distance > 0) {
			buildings_outside_base.push_back(information_unit->unit);
		}
	}
	for (auto& cluster : tactics_manager.clusters()) {
		int distance = offense_distance_to_base(cluster, buildings_outside_base);
		if (distance <= 0) {
			for (auto& enemy_unit : cluster.units()) units_near_base_.insert(enemy_unit->unit);
		} else if (distance >= 320) {
			for (auto& enemy_unit : cluster.units()) units_near_base_.erase(enemy_unit->unit);
		}
	}
	remove_nonexistant_units(units_near_base_);
}

void MicroManager::update_units_near_main_base()
{
	const BWEM::Base* main_base = base_state.main_base();
	for (auto enemy_unit : information_manager.enemy_units()) {
		int distance = std::min(distance_to_base(main_base, enemy_unit), distance_to_proxy(enemy_unit));
		int trigger_distance = (enemy_unit->type == UnitTypes::Protoss_Photon_Cannon) ? UnitTypes::Protoss_Photon_Cannon.groundWeapon().maxRange() : 320;
		if (distance <= trigger_distance) {
			units_near_main_base_.insert(enemy_unit->unit);
		} else if (distance > trigger_distance + 64) {
			units_near_main_base_.erase(enemy_unit->unit);
		}
	}
	remove_nonexistant_units(units_near_main_base_);
}

void MicroManager::update_ignore_when_attacking()
{
	ignore_when_attacking_.clear();
	if (opponent_model.enemy_opening() != EnemyOpening::P_CannonRush) {
		for (auto& cluster : tactics_manager.clusters()) {
			if (is_scouting_worker_cluster(cluster)) {
				for (auto& enemy_unit : cluster.units()) {
					ignore_when_attacking_.insert(enemy_unit->unit);
				}
			}
		}
	}
}

void MicroManager::remove_nonexistant_units(std::set<Unit>& unit_set)
{
	std::vector<Unit> remove_units;
	for (auto& unit : unit_set) {
		if (!contains(information_manager.all_units(), unit) ||
			!information_manager.all_units().at(unit).position.isValid()) {
			remove_units.push_back(unit);
		}
	}
	for (auto& unit : remove_units) {
		unit_set.erase(unit);
	}
}

bool MicroManager::no_cluster_units_near_base(const EnemyCluster& cluster)
{
	for (auto& enemy_unit : cluster.units()) {
		if (contains(units_near_base_, enemy_unit->unit)) return false;
	}
	return true;
}

bool MicroManager::is_scouting_worker_cluster(const EnemyCluster& cluster)
{
	bool result = false;
	if (cluster.units().size() == 1 || cluster.units().size() == 2) {
		result = std::all_of(cluster.units().begin(), cluster.units().end(), [](auto& enemy_unit){
			return enemy_unit->type.isWorker();
		});
	}
	return result;
}

void MicroManager::apply_transport_orders()
{
	remove_missing_keys(transport_state_, transports_);
	loading_units_.clear();
	std::set<Unit> unpaired_reavers = determine_unpaired_reavers();
	for (auto& transport_unit : transports_) {
		bool order_issued = false;
		TransportState& state = transport_state_[transport_unit];
		CombatState& combat_state = combat_state_.at(transport_unit);
		
		if (configuration.draw_enabled()) {
			const char *text;
			switch (state.command) {
				case TransportCommand::Default:
					text = "-";
					break;
				case TransportCommand::LoadForDropInEnemyBase:
					text = "Ld";
					break;
				case TransportCommand::DropInEnemyBase:
					text = "Dr";
					break;
				case TransportCommand::BulldogApproach:
					text = "BdA";
					break;
				case TransportCommand::BulldogLoadZealots:
					text = "BdL";
					break;
				case TransportCommand::BulldogDropZealots:
					text = "BdD";
					break;
				case TransportCommand::ReaverMicro:
					text = "R";
					break;
				default:
					text = "?";
					break;
			}
			Broodwar->drawTextMap(transport_unit->getPosition(), text);
		}
		
		if (state.command == TransportCommand::LoadForDropInEnemyBase && is_less_than_half_damaged(transport_unit)) {
			if (!order_issued) {
				order_issued = load_closest_unit_of_type(transport_unit, state.unit_type, true);
			}
			if (!order_issued) {
				if (!transport_unit->getLoadedUnits().empty()) {
					state.position = tactics_manager.enemy_start_drop_position();
					if (!state.position.isValid()) state.position = tactics_manager.enemy_base_attack_position();
					if (state.position.isValid()) state.command = TransportCommand::DropInEnemyBase;
				} else {
					state.command = TransportCommand::Default;
				}
			}
		} else if (state.command == TransportCommand::DropInEnemyBase) {
			if (transport_unit->getLoadedUnits().empty()) {
				state.command = TransportCommand::Default;
			} else {
				Position position = state.position;
				if (transport_unit->getDistance(position) > 128 &&
					(!transport_unit->isUnderAttack() || is_less_than_half_damaged(transport_unit))) {
					order_issued = move_flyer_near_safe_approach_unsafe(transport_unit, position);
				}
				if (!order_issued) {
					Unit first_loaded_unit = *(transport_unit->getLoadedUnits().begin());
					transport_unit->unload(first_loaded_unit);
					run_by_target_position_ = state.position;
					desperados_.insert(first_loaded_unit);
					order_issued = true;
				}
			}
		} else if (state.command == TransportCommand::BulldogLoadZealots) {
			if (!order_issued && transport_unit->getLoadedUnits().size() < 4) {
				order_issued = load_closest_unit_of_type(transport_unit, UnitTypes::Protoss_Zealot, true);
			}
			if (!order_issued) {
				if (!transport_unit->getLoadedUnits().empty()) {
					state.position = tactics_manager.enemy_start_drop_position();
					if (!state.position.isValid()) state.position = tactics_manager.enemy_base_attack_position();
					if (state.position.isValid()) state.command = TransportCommand::BulldogApproach;
				} else {
					state.command = TransportCommand::Default;
				}
			}
		} else if (state.command == TransportCommand::BulldogApproach) {
			if (!combat_state.target_position().isValid()) {
				state.command = TransportCommand::Default;
			} else {
				bool close_enough = false;
				for (auto& cluster : tactics_manager.clusters()) {
					if (!is_scouting_worker_cluster(cluster)) {
						for (auto& unit : Broodwar->self()->getUnits()) {
							if (unit->isCompleted() &&
								(unit->getType() == UnitTypes::Protoss_Zealot ||
								 unit->getType() == UnitTypes::Protoss_Dragoon) &&
								cluster.in_front(unit)) {
								close_enough = true;
								break;
							}
						}
					}
					if (close_enough) break;
				}
				if (close_enough) state.command = TransportCommand::BulldogDropZealots;
			}
		} else if (state.command == TransportCommand::BulldogDropZealots) {
			if (transport_unit->getLoadedUnits().empty()) {
				state.command = TransportCommand::Default;
			} else {
				Position tank_position = closest_sieged_tank(transport_unit->getPosition());
				if (tank_position.isValid()) {
					Unit first_loaded_unit = *(transport_unit->getLoadedUnits().begin());
					Position shuttle_position = predict_position(transport_unit, Broodwar->getRemainingLatencyFrames());
					int distance = calculate_distance(UnitTypes::Terran_Siege_Tank_Siege_Mode, tank_position, first_loaded_unit->getType(), shuttle_position);
					if (distance <= first_loaded_unit->getType().groundWeapon().maxRange()) {
						transport_unit->unload(first_loaded_unit);
						order_issued = true;
					} else {
						unit_move(transport_unit, tank_position);
						order_issued = true;
					}
				} else {
					Position position = state.position;
					if (transport_unit->getDistance(position) > 128 &&
						(!transport_unit->isUnderAttack() || is_less_than_half_damaged(transport_unit))) {
						order_issued = move_flyer_near_safe_approach_unsafe(transport_unit, position);
					}
					if (!order_issued) {
						Unit first_loaded_unit = *(transport_unit->getLoadedUnits().begin());
						transport_unit->unload(first_loaded_unit);
						run_by_target_position_ = state.position;
						desperados_.insert(first_loaded_unit);
						order_issued = true;
					}
				}
			}
		} else if (state.command == TransportCommand::ReaverMicro) {
			if (transport_unit->getLoadedUnits().empty()) {
				if (state.reaver_unit->exists()) {
					if (transport_unit->getDistance(state.reaver_unit) <= 32 &&
						(state.reaver_unit->getGroundWeaponCooldown() > 30 ||
						 (state.reaver_unit->getGroundWeaponCooldown() == 0 && !reaver_in_shuttle_can_attack(state.reaver_unit)))) {
						load_unit_into_transport(transport_unit, state.reaver_unit);
						order_issued = true;
					} else {
						order_issued = move_flyer_near_safe(transport_unit, state.reaver_unit->getPosition());
					}
				} else {
					state.command = TransportCommand::Default;
				}
			} else {
				if (check_terrain_collision(UnitTypes::Protoss_Reaver, transport_unit->getPosition()) &&
					((state.reaver_unit->getScarabCount() > 0 && reaver_in_shuttle_can_attack(transport_unit)) ||
					 contains(drop_reaver_to_build_scarab_, state.reaver_unit))) {
					Unit first_loaded_unit = *(transport_unit->getLoadedUnits().begin());
					transport_unit->unload(first_loaded_unit);
					order_issued = true;
				}
				
				if (!order_issued) {
					Position position = Positions::None;
					
					if (combat_state.target_position().isValid()) {
						Unit closest_combat_unit = combat_unit_closest_to_position(combat_state.target_position());
						position = (closest_combat_unit == nullptr) ? combat_state.target_position() : closest_combat_unit->getPosition();
					}
					
					if (!position.isValid() && !units_near_base_.empty()) {
						Unit closest_invader_unit = smallest_priority(units_near_base_, [transport_unit](Unit invader_unit) {
							const InformationUnit& invader = information_manager.all_units().at(invader_unit);
							return transport_unit->getDistance(invader.position);
						});
						const InformationUnit& closest_invader = information_manager.all_units().at(closest_invader_unit);
						Unit closest_combat_unit = combat_unit_in_base_closest_to_position(closest_invader.position, closest_invader.base_distance);
						position = (closest_combat_unit == nullptr) ? closest_invader.position : closest_combat_unit->getPosition();
					}
					
					if (!position.isValid() && combat_state.stage_position().isValid()) {
						position = combat_state.stage_position();
					}
					
					if (position.isValid()) {
						order_issued = move_flyer_near_safe(transport_unit, position);
					}
				}
			}
		} else if (state.command == TransportCommand::Default) {
			if (!order_issued) {
				Position tank_position = closest_sieged_tank(transport_unit->getPosition());
				if (tank_position.isValid()) {
					if (transport_unit->getLoadedUnits().empty()) {
						if (information_manager.enemy_count(UnitTypes::Terran_Marine) < 4 &&
							information_manager.enemy_count(UnitTypes::Terran_Goliath) < 2) {
							order_issued = load_closest_unit_of_type(transport_unit, UnitTypes::Protoss_Zealot);
						}
					} else {
						Unit first_loaded_unit = *(transport_unit->getLoadedUnits().begin());
						Position shuttle_position = predict_position(transport_unit, Broodwar->getRemainingLatencyFrames());
						int distance = calculate_distance(UnitTypes::Terran_Siege_Tank_Siege_Mode, tank_position, first_loaded_unit->getType(), shuttle_position);
						if (distance <= first_loaded_unit->getType().groundWeapon().maxRange()) {
							transport_unit->unload(first_loaded_unit);
							order_issued = true;
						} else {
							unit_move(transport_unit, tank_position);
							order_issued = true;
						}
					}
				}
			}
			if (!order_issued && !unpaired_reavers.empty() && transport_unit->getLoadedUnits().empty()) {
				Unit reaver_unit = *unpaired_reavers.begin();
				unpaired_reavers.erase(reaver_unit);
				state.reaver_unit = reaver_unit;
				state.command = TransportCommand::ReaverMicro;
			}
		}
		
		if (!order_issued && combat_state.target_position().isValid()) {
			Unit closest_combat_unit = combat_unit_closest_to_position(combat_state.target_position());
			if (closest_combat_unit != nullptr) {
				order_issued = move_flyer_near_safe(transport_unit, closest_combat_unit->getPosition());
			}
		}
		
		if (!order_issued && combat_state.stage_position().isValid()) {
			order_issued = move_flyer_near_safe(transport_unit, combat_state.stage_position());
		}
		
		if (!order_issued) {
			if (!transport_unit->isHoldingPosition()) transport_unit->holdPosition();
		}
	}
}

void MicroManager::apply_overlord_orders()
{
	remove_missing_keys(overlord_state_, overlords_);
	bool allow_wait_in_base = allow_overlord_wait_in_base();
	
	for (auto& special_unit : overlords_) {
		bool order_issued = false;
		OverlordState& state = overlord_state_[special_unit];
		
		if (state.command == OverlordCommand::Default) {
			if (overlord_scout_map_center_ &&
				!overlord_scout_map_center_ordered_) {
				state.command = OverlordCommand::ScoutMapCenter;
				overlord_scout_map_center_ordered_ = true;
			}
			if (state.command == OverlordCommand::Default &&
				tactics_manager.enemy_start_base() == nullptr &&
				tactics_manager.possible_enemy_start_bases().size() > 1) {
				std::set<const BWEM::Base*> bases_to_explore = base_state.undiscovered_starting_bases(true);
				key_value_vector<const BWEM::Base*,int> base_distances;
				for (auto& base : bases_to_explore) {
					Position position = center_position_for(Broodwar->self()->getRace().getResourceDepot(), base->Location());
					base_distances.emplace_back(base, position.getApproxDistance(special_unit->getPosition()));
				}
				const BWEM::Base* base = key_with_smallest_value(base_distances);
				if (base != nullptr) {
					state.command = OverlordCommand::InitialScout;
					state.base = base;
				}
			}
			if (state.command == OverlordCommand::Default) {
				Position special_unit_position = special_unit->getPosition();
				Position position = Positions::None;
				int min_distance = INT_MAX;
				for (auto& information_unit : information_manager.my_units()) {
					if (information_unit->type == UnitTypes::Zerg_Spore_Colony) {
						int distance = information_unit->position.getApproxDistance(special_unit_position);
						if (distance < min_distance) {
							position = information_unit->position;
							min_distance = distance;
						}
					}
				}
				if (!position.isValid()) {
					const BWEM::Base* base = smallest_priority(base_state.controlled_bases(),
															   [special_unit_position](auto& base) {
																   Position position = center_position_for(Broodwar->self()->getRace().getResourceDepot(), base->Location());
																   return position.getApproxDistance(special_unit_position);
															   });
					if (base != nullptr) {
						position = center_position_for(Broodwar->self()->getRace().getResourceDepot(), base->Location());
					}
				}
				if (position.isValid()) {
					order_issued = move_flyer_near_safe(special_unit, position);
				}
			}
		}
		
		if (state.command == OverlordCommand::ScoutMapCenter) {
			Position center = bwem_map.Center();
			if (center.getApproxDistance(special_unit->getPosition()) < 32 ||
				opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax) {
				state.command = OverlordCommand::Default;
			} else {
				unit_move(special_unit, center);
				order_issued = true;
			}
		}
		
		if (state.command == OverlordCommand::InitialScout) {
			if (tactics_manager.enemy_start_base() != nullptr ||
				tactics_manager.possible_enemy_start_bases().size() <= 1 ||
				!contains(base_state.unexplored_start_bases(), state.base)) {
				state.command = OverlordCommand::Default;
			} else {
				Position position = center_position_for(Broodwar->self()->getRace().getResourceDepot(), state.base->Location());
				order_issued = move_flyer_near_safe(special_unit, position);
			}
		}
		
		if (state.command == OverlordCommand::WaitInBase) {
			if (!allow_wait_in_base) {
				state.command = OverlordCommand::Default;
			} else {
				Position position = center_position_for(Broodwar->self()->getRace().getResourceDepot(), state.base->Location());
				order_issued = move_flyer_near_safe(special_unit, position);
			}
		}
		
		if (state.command == OverlordCommand::Detect) {
			Position position = determine_first_detector_location(special_unit);
			if (position.isValid()) {
				order_issued = move_flyer_near_safe(special_unit, position);
			}
		}
		
		if (state.command == OverlordCommand::WorkerNeedsDetection) {
			Position position = state.position;
			if (special_unit->getDistance(position) < 32) {
				state.command = OverlordCommand::Default;
			} else {
				order_issued = move_flyer_near_safe(special_unit, position);
			}
		}
		
		if (!order_issued) {
			if (!special_unit->isHoldingPosition()) special_unit->holdPosition();
		}
	}
	
	if (allow_wait_in_base) {
		if (tactics_manager.enemy_start_base() != nullptr) {
			order_overlord_wait_in_base(tactics_manager.enemy_start_base());
			order_overlord_wait_in_base(tactics_manager.enemy_natural_base());
		} else {
			std::vector<const BWEM::Base*> bases = tactics_manager.possible_enemy_start_bases();
			if (bases.size() == 1) {
				const BWEM::Base* base = bases[0];
				order_overlord_wait_in_base(base);
				order_overlord_wait_in_base(base_state.natural_base_for_start_base(base));
			}
		}
	}
	
	if (opponent_model.cloaked_or_mine_present()) {
		order_overlord_detect();
	}
	
	order_overlord_worker_need_detection();
}

void MicroManager::apply_combat_unit_orders()
{
	remove_missing_keys(combat_unit_targets_, combat_units_);
	remove_missing_keys(vulture_state_, combat_units_);
	update_run_by();
	scourge_target_map_.clear();
	if (training_manager.unit_count_completed(UnitTypes::Zerg_Scourge) > 0) {
		for (auto& combat_unit : combat_units_) {
			if (combat_unit->getType() == UnitTypes::Zerg_Scourge) {
				CombatUnitTarget& target = combat_unit_targets_[combat_unit];
				if (target.unit != nullptr) {
					scourge_target_map_[target.unit].push_back(combat_unit);
				}
			}
		}
	}
	std::map<Unit,int> zealot_incoming_spider_mine_count;
	if (training_manager.unit_count_completed(UnitTypes::Protoss_Zealot) > 0) {
		for (Unit enemy_unit : all_enemy_units_) {
			if (enemy_unit->getType() == UnitTypes::Terran_Vulture_Spider_Mine &&
				enemy_unit->getOrderTarget() != nullptr &&
				enemy_unit->getOrderTarget()->getType() == UnitTypes::Protoss_Zealot) {
				zealot_incoming_spider_mine_count[enemy_unit->getOrderTarget()]++;
			}
		}
	}
	
	std::set<Unit> spell_casting_units;
	for (auto& tentative_yamato : tentative_yamatoes_) spell_casting_units.insert(tentative_yamato.unit);
	for (auto& tentative_lockdown : tentative_lockdowns_) spell_casting_units.insert(tentative_lockdown.unit);
	for (auto& tentative_mine : tentative_mines_) spell_casting_units.insert(tentative_mine.unit);
	MinePlacementCheck mine_placement_check;
	
	std::vector<Unit> nearby_enemy_units;
	std::vector<Unit> nearby_friendly_ground;
	for (auto& combat_unit : combat_units_) {
		if (loading_units_.count(combat_unit) > 0) continue;
		UnitType unit_type = combat_unit->getType();
		
		UnstickState& state = unstick_state_[combat_unit];
		state.update(combat_unit);
		if (unit_type == UnitTypes::Protoss_Dragoon) {
			DragoonState& state = dragoon_state_[combat_unit];
			state.update(combat_unit);
			if (state.is_busy()) continue;
		}
		if (state.is_stuck()) {
			combat_unit->stop();
			continue;
		}
		
		if (recharge_at_shield_battery(combat_unit)) {
			continue;
		}
		
		if (contains(spell_casting_units, combat_unit)) {
			continue;
		}
		
		if (unit_type == UnitTypes::Terran_Wraith &&
			Broodwar->self()->hasResearched(TechTypes::Cloaking_Field) &&
			combat_unit->isUnderAttack() &&
			!combat_unit->isCloaked() &&
			combat_unit->getEnergy() >= 75) {
			combat_unit->cloak();
			continue;
		}
		
		if (unit_type == UnitTypes::Terran_Battlecruiser &&
			Broodwar->self()->hasResearched(TechTypes::Yamato_Gun) &&
			combat_unit->getEnergy() >= TechTypes::Yamato_Gun.energyCost() &&
			combat_unit->getSpellCooldown() == 0 &&
			yamato(combat_unit)) {
			continue;
		}
		
		if (unit_type == UnitTypes::Terran_Ghost &&
			Broodwar->self()->hasResearched(TechTypes::Lockdown) &&
			combat_unit->getEnergy() >= TechTypes::Lockdown.energyCost() &&
			combat_unit->getSpellCooldown() == 0 &&
			lockdown(combat_unit)) {
			continue;
		}
		
		CombatState& combat_state = combat_state_.at(combat_unit);
		Position target_position = combat_state.target_position();
		Position stage_position = combat_state.stage_position();
		
		Unit selected_enemy_unit = nullptr;
		bool enable_advance = true;
		bool enable_retreat = true;
		
		Unit nearby_sieged_tank = determine_nearby_sieged_tank(combat_unit);
		if (nearby_sieged_tank != nullptr) {
			selected_enemy_unit = nearby_sieged_tank;
		} else if (combat_unit->getType() == UnitTypes::Protoss_Carrier &&
				   combat_unit->getInterceptorCount() == 0) {
			enable_advance = false;
		} else {
			CombatUnitTarget& target = combat_unit_targets_[combat_unit];
			if (target.should_update_target(combat_unit)) {
				Unit previous_unit = target.unit;
				std::tie(target.unit, target.enable_advance, target.enable_retreat) = select_enemy_unit_for_combat_unit(combat_unit);
				target.frame = Broodwar->getFrameCount();
				if (target.unit != previous_unit) {
					target.last_switch_frame = Broodwar->getFrameCount();
					if (combat_unit->getType() == UnitTypes::Zerg_Scourge) {
						if (previous_unit != nullptr) {
							auto scourges = scourge_target_map_[previous_unit];
							auto it = std::find(scourges.begin(), scourges.end(), combat_unit);
							if (it != scourges.end()) scourges.erase(it);
						}
						if (target.unit != nullptr) {
							scourge_target_map_[target.unit].push_back(combat_unit);
						}
					}
				}
			}
			selected_enemy_unit = target.unit;
			enable_advance = target.enable_advance;
			enable_retreat = target.enable_retreat || !target_position.isValid();
		}
		
		// @
		/*if (combat_unit->getType() == UnitTypes::Protoss_Carrier) {
			FILE *f = fopen("bwapi-data\\write\\carrier.txt", "a");
			Unit target_unit = determine_carrier_target(combat_unit);
			int visible = 0;
			int targeting = 0;
			for (auto interceptor_unit : combat_unit->getInterceptors()) {
				if (interceptor_unit->isCompleted()) {
					if (interceptor_unit->isVisible()) visible++;
					if (interceptor_unit->getOrderTarget() != nullptr) targeting++;
				}
			}
			fprintf(f, "%d (%s) | [%d] (%d/%d/%d) selected=%s[%d] target=%s[%d] %d %d\n",
					Broodwar->getFrameCount(),
					frame_to_string(Broodwar->getFrameCount()).c_str(),
					combat_unit->getID(),
					visible,
					targeting,
					combat_unit->getInterceptorCount(),
					selected_enemy_unit != nullptr ? selected_enemy_unit->getType().c_str() : "none",
					selected_enemy_unit != nullptr ? selected_enemy_unit->getID() : -1,
					target_unit != nullptr ? target_unit->getType().c_str() : "none",
					target_unit != nullptr ? target_unit->getID() : -1,
					int(enable_advance),
					int(enable_retreat));
			fclose(f);
		}*/
		// /@
		
		if (load_bunkers_ &&
			unit_type == UnitTypes::Terran_Marine &&
			stage_position.isValid() &&
			enable_retreat &&
			move_into_bunker(combat_unit, stage_position)) {
			continue;
		}
		
		if (combat_unit->getType() == UnitTypes::Protoss_Zealot &&
			!contains(zealot_incoming_spider_mine_count, combat_unit)) {
			bool order_issued = unit_potential(combat_unit, [&zealot_incoming_spider_mine_count](UnitPotential& potential){
				int range = WeaponTypes::Spider_Mines.outerSplashRadius() + 32;
				for (auto [unit,count] : zealot_incoming_spider_mine_count) {
					potential.add_potential(unit, double(count), range);
				}
			});
			if (order_issued) {
				continue;
			}
		}
		
		Position saved_defense_position = Positions::None;
		int saved_defense_frame_limit = -1;
		if (combat_unit->getSpiderMineCount() > 0 &&
			Broodwar->self()->hasResearched(TechTypes::Spider_Mines)) {
			auto& vulture_state = vulture_state_[combat_unit];
			std::swap(vulture_state.defense_position, saved_defense_position);
			std::swap(vulture_state.defense_frame_limit, saved_defense_frame_limit);
		}
		
		if (selected_enemy_unit != nullptr &&
			Broodwar->self()->hasResearched(TechTypes::Stim_Packs) &&
			is_stimmable(unit_type) &&
			!combat_unit->isStimmed() &&
			combat_unit->getHitPoints() >= 20 &&
			can_attack(selected_enemy_unit) &&
			can_attack_in_range(combat_unit, selected_enemy_unit, 64) &&
			determine_allow_stim(combat_unit)) {
			combat_unit->useTech(TechTypes::Stim_Packs);
		} else if (selected_enemy_unit != nullptr) {
			bool order_issued = false;
			if (is_melee_or_worker(combat_unit->getType()) || combat_unit->getType() == UnitTypes::Protoss_Carrier) {
				order_issued = unit_potential(combat_unit, [](UnitPotential& potential){
					potential.repel_storms();
				});
			}
			if (!is_melee_or_worker(combat_unit->getType()) && combat_unit->getType() != UnitTypes::Protoss_Carrier && is_on_cooldown(combat_unit, selected_enemy_unit->isFlying())) {
				if (combat_unit->getType() == UnitTypes::Protoss_Dragoon && combat_unit->getLastCommand().getType() == UnitCommandTypes::Attack_Unit) {
					combat_unit->stop();
				} else {
					set_nearby_units_for_kiting(nearby_enemy_units, nearby_friendly_ground, combat_unit);
					bool repel_runby_defense = (contains(desperados_, combat_unit) && !run_by_defense_.empty());
					bool moved = unit_potential(combat_unit, [this,selected_enemy_unit,&nearby_enemy_units,&nearby_friendly_ground,repel_runby_defense](UnitPotential& potential){
						potential.kite_units(nearby_enemy_units);
						if (repel_runby_defense) {
							potential.repel_units(run_by_defense_, 0, 100.0);
						}
						if (!potential.unit()->isFlying()) {
							int ground_splash_distance = threat_grid.ground_splash_distance(FastTilePosition(potential.position()));
							if (ground_splash_distance > 0) {
								potential.repel_friendly(nearby_friendly_ground, potential.unit(), ground_splash_distance);
							}
						}
						potential.repel_storms();
						bool step_in = potential.empty();
						potential.repel_buildings();
						potential.repel_terrain();
						if (step_in) {
							potential.add_potential(selected_enemy_unit, -0.1);
						}
					});
					if (!moved &&
						!combat_unit->isIdle() &&
						!combat_unit->isMoving() &&
						combat_unit->getType() != UnitTypes::Protoss_Reaver) {
						combat_unit->stop();
					}
				}
				order_issued = true;
			}
			if (!order_issued &&
				combat_unit->getType() == UnitTypes::Protoss_Carrier &&
				carrier_interceptors_attacking(combat_unit) &&
				!should_carrier_retarget(combat_unit, selected_enemy_unit)) {
				Position leash_position = determine_carrier_leash_position(combat_unit);
				Unit target_unit = determine_carrier_target(combat_unit);
				// @
				/*FILE *f = fopen("bwapi-data\\write\\carrier.txt", "a");
				fprintf(f, "  Leash delta (%d,%d)\n", leash_position.x - combat_unit->getPosition().x, leash_position.y - combat_unit->getPosition().y);
				fprintf(f, "  Carrier-target distance %d\n", calculate_distance(UnitTypes::Protoss_Carrier, combat_unit->getPosition(), target_unit->getType(), target_unit->getPosition()));
				fprintf(f, "  Leash-target distance %d\n", calculate_distance(UnitTypes::Protoss_Carrier, leash_position, target_unit->getType(), target_unit->getPosition()));
				fclose(f);*/
				// /@
				if (leash_position.isValid() &&
					calculate_distance(UnitTypes::Protoss_Carrier, leash_position, target_unit->getType(), target_unit->getPosition()) >= combat_unit->getDistance(target_unit) &&
					leash_position.getApproxDistance(combat_unit->getPosition()) > Broodwar->getRemainingLatencyFrames() * UnitTypes::Protoss_Carrier.topSpeed()) {
					unit_move(combat_unit, leash_position);
				}
				order_issued = true;
			}
			if (!order_issued) {
				Position intercept_position = calculate_interception_position(combat_unit, selected_enemy_unit);
				path_finder.execute_path(combat_unit, intercept_position, [this,combat_unit,selected_enemy_unit,intercept_position](){
					if (selected_enemy_unit->getPosition() == intercept_position &&
						(combat_unit->getType() != UnitTypes::Protoss_Dragoon ||
						 can_attack_in_range_at_positions(combat_unit,
														  predict_position(combat_unit, Broodwar->getRemainingLatencyFrames()),
														  selected_enemy_unit,
														  predict_position(selected_enemy_unit, Broodwar->getRemainingLatencyFrames())))) {
						unit_attack(combat_unit, selected_enemy_unit);
					} else {
						unit_move(combat_unit, intercept_position);
					}
				});
			}
		} else if (running_by_.count(combat_unit) > 0 || desperados_.count(combat_unit) > 0) {
			move_runby(combat_unit, run_by_target_position_);
		} else if (target_position == Positions::Unknown) {
			random_move(combat_unit);
		} else if (target_position.isValid() && enable_advance) {
			if (!combat_state.near_target_only()) {
				path_finder.execute_path(combat_unit, target_position, [target_position,combat_unit](){
					if (combat_unit->isFlying() ||
						combat_unit->getDistance(target_position) > 384 ||
						(Broodwar->getFrameCount() % 24) == 0) {
						unit_move(combat_unit, target_position);
					}
				});
			} else {
				move_with_blockade_breaking(combat_unit, target_position);
			}
		} else {
			bool moved = unit_potential(combat_unit, [this](UnitPotential& potential){
				potential.repel_storms();
			});
			if (!moved) {
				if (stage_position.isValid() && enable_retreat) {
					bool order_issued = false;
					if (combat_unit->getSpiderMineCount() > 0 &&
						Broodwar->self()->hasResearched(TechTypes::Spider_Mines)) {
						int distance = combat_unit->getDistance(stage_position);
						if (distance < 256) {
							int frame_limit = Broodwar->getFrameCount() + int(1.5 * distance / Broodwar->self()->topSpeed(unit_type) + 0.5);
							if (saved_defense_position == stage_position) {
								frame_limit = std::min(frame_limit, saved_defense_frame_limit);
							}
							auto& vulture_state = vulture_state_[combat_unit];
							vulture_state.defense_position = stage_position;
							vulture_state.defense_frame_limit = frame_limit;
							if (Broodwar->getFrameCount() >= frame_limit &&
								mine_placement_check.allow_mine_at(combat_unit->getPosition(), mine_placement_check.is_near_chokepoint(combat_unit->getPosition()) ? 8 : 32)) {
								mine(combat_unit, combat_unit->getPosition());
								order_issued = true;
							}
						}
					}
					if (!order_issued) move_retreat(combat_unit, stage_position);
				} else {
					if (combat_unit->getSpiderMineCount() > 0 &&
						Broodwar->self()->hasResearched(TechTypes::Spider_Mines) &&
						mine_placement_check.allow_mine_at(combat_unit->getPosition(), 32)) {
						mine(combat_unit, combat_unit->getPosition());
					} else if (!combat_unit->isHoldingPosition()) {
						combat_unit->holdPosition();
					}
				}
			}
		}
	}
}

void MicroManager::apply_dark_templar_orders()
{
	std::pair<Unit,Unit> dark_archon_meld;
	bool need_dark_archons = false;
	if (training_manager.unit_count(UnitTypes::Protoss_Dark_Archon) < requested_dark_archon_count_) {
		need_dark_archons = true;
		std::vector<Unit> suitable_dark_templars;
		for (auto& unit : dark_templars_) {
			if (unit_in_safe_location(unit)) suitable_dark_templars.push_back(unit);
		}
		if (suitable_dark_templars.size() >= 2) {
			int smallest_distance = INT_MAX;
			for (size_t i = 0; i < suitable_dark_templars.size(); i++) {
				for (size_t j = i + 1; j < suitable_dark_templars.size(); j++) {
					int distance = ground_distance(suitable_dark_templars[i]->getPosition(), suitable_dark_templars[j]->getPosition());
					if (distance <= 128 && distance < smallest_distance) {
						smallest_distance = distance;
						dark_archon_meld.first = suitable_dark_templars[i];
						dark_archon_meld.second = suitable_dark_templars[j];
					}
				}
			}
		}
	}
	
	std::map<Unit,DarkTemplarPathNearbyUnits,CompareUnitByID> candidate_dark_templars_for_pathing;
	
	for (auto& combat_unit : dark_templars_) {
		if (loading_units_.count(combat_unit) > 0) continue;
		
		UnstickState& state = unstick_state_[combat_unit];
		state.update(combat_unit);
		if (state.is_stuck()) {
			combat_unit->stop();
			continue;
		}
		
		if (combat_unit == dark_archon_meld.first) {
			if (!unit_has_target(dark_archon_meld.first, dark_archon_meld.second)) {
				dark_archon_meld.first->useTech(TechTypes::Dark_Archon_Meld, dark_archon_meld.second);
			}
			continue;
		}
		if (combat_unit == dark_archon_meld.second) {
			if (!unit_has_target(dark_archon_meld.second, dark_archon_meld.first)) {
				dark_archon_meld.second->useTech(TechTypes::Dark_Archon_Meld, dark_archon_meld.first);
			}
			continue;
		}
		
		if (recharge_at_shield_battery(combat_unit)) {
			continue;
		}
		
		Unit mine_unit = determine_incoming_mine(combat_unit);
		if (mine_unit != nullptr) {
			unit_attack(combat_unit, mine_unit);
			continue;
		}
		
		constexpr int max_distance = kDarkTemplarPathMaxDistance;
		FastPosition start_position = combat_unit->getPosition();
		DarkTemplarPathNearbyUnits nearby_units;
		int combat_unit_max_dimension = max_unit_dimension(UnitTypes::Protoss_Dark_Templar);
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->position.isValid()) {
				int d = (std::max(std::abs(start_position.x - enemy_unit->position.x), std::abs(start_position.y - enemy_unit->position.y)) -
						 combat_unit_max_dimension -
						 max_unit_dimension(enemy_unit->type));
				int completion = enemy_unit->is_completed() ? INT_MIN : enemy_unit->complete_frame() - Broodwar->getFrameCount();
				int weapon_range = weapon_max_range(enemy_unit->type, enemy_unit->player, combat_unit->isFlying());
				if (weapon_range >= 0 && d < max_distance + weapon_range) {
					nearby_units.enemy_attack_units.emplace_back(enemy_unit, completion);
				}
				int detect_range = enemy_unit->detection_range();
				if (detect_range >= 0 && d < max_distance + detect_range) {
					nearby_units.enemy_detector_units.emplace_back(enemy_unit, completion);
				}
				if (can_attack(UnitTypes::Protoss_Dark_Templar, enemy_unit->flying) &&
					(enemy_unit->unit->exists() ? not_cloaked(enemy_unit->unit) : (!enemy_unit->burrowed && !enemy_unit->type.hasPermanentCloak())) &&
					d < max_distance + UnitTypes::Protoss_Dark_Templar.groundWeapon().maxRange()) {
					nearby_units.enemy_attackable_units.emplace_back(enemy_unit, completion);
				}
			}
		}
		
		if (nearby_units.is_nonempty()) {
			candidate_dark_templars_for_pathing.emplace(combat_unit, std::move(nearby_units));
		} else {
			CombatState& combat_state = combat_state_.at(combat_unit);
			Position target_position = combat_state.target_position();
			Position stage_position = combat_state.stage_position();
			
			if (!need_dark_archons && target_position == Positions::Unknown) {
				random_move(combat_unit);
			} else if (!need_dark_archons && target_position.isValid()) {
				path_finder.execute_path(combat_unit, target_position, [target_position,combat_unit](){
					unit_move(combat_unit, target_position);
				});
			} else {
				bool moved = unit_potential(combat_unit, [this](UnitPotential& potential){
					potential.repel_storms();
				});
				if (!moved) {
					if (stage_position.isValid()) {
						move_retreat(combat_unit, stage_position);
					} else {
						if (!combat_unit->isHoldingPosition()) combat_unit->holdPosition();
					}
				}
			}
		}
	}
	
	Unit combat_unit = nullptr;
	for (auto& entry : candidate_dark_templars_for_pathing) {
		if (!contains(dark_templar_turn_, entry.first)) {
			combat_unit = entry.first;
			break;
		}
	}
	if (combat_unit == nullptr &&
		!candidate_dark_templars_for_pathing.empty() &&
		Broodwar->getFrameCount() >= 3 + dark_templar_turn_last_reset_) {
		dark_templar_turn_.clear();
		dark_templar_turn_last_reset_ = Broodwar->getFrameCount();
		combat_unit = candidate_dark_templars_for_pathing.begin()->first;
	}
	
	if (combat_unit != nullptr) {
		CombatState& combat_state = combat_state_.at(combat_unit);
		Position target_position = combat_state.target_position();
		Position stage_position = combat_state.stage_position();
		const DarkTemplarPathNearbyUnits& nearby_units = candidate_dark_templars_for_pathing[combat_unit];
		
		bool path_order_issued = dark_templar_path_based_order(combat_unit, nearby_units);
		if (!path_order_issued) {
			if (target_position == Positions::Unknown) {
				random_move(combat_unit);
			} else if (target_position.isValid()) {
				path_finder.execute_path(combat_unit, target_position, [target_position,combat_unit](){
					unit_move(combat_unit, target_position);
				});
			} else {
				bool moved = unit_potential(combat_unit, [this](UnitPotential& potential){
					potential.repel_storms();
				});
				if (!moved) {
					if (stage_position.isValid()) {
						move_retreat(combat_unit, stage_position);
					} else {
						if (!combat_unit->isHoldingPosition()) combat_unit->holdPosition();
					}
				}
			}
		}
		
		dark_templar_turn_.insert(combat_unit);
	}
}

void MicroManager::apply_lurker_orders()
{
	for (auto& combat_unit : lurkers_) {
		CombatState& combat_state = combat_state_.at(combat_unit);
		Position target_position = combat_state.target_position();
		Position stage_position = combat_state.stage_position();
		
		Unit selected_enemy_unit;
		bool enable_advance;
		bool enable_retreat;
		std::tie(selected_enemy_unit, enable_advance, enable_retreat) = select_enemy_unit_for_lurker(combat_unit);
		enable_retreat = enable_retreat || !target_position.isValid();
		
		if (selected_enemy_unit != nullptr) {
			if (!combat_unit->isBurrowed()) {
				if (can_attack_in_range_with_prediction(combat_unit, selected_enemy_unit)) {
					int distance = combat_unit->getDistance(selected_enemy_unit);
					bool moved = unit_potential(combat_unit, [this,combat_unit,selected_enemy_unit,distance](UnitPotential& potential){
						int current_distance = calculate_distance(combat_unit->getType(), potential.position(), selected_enemy_unit->getType(), selected_enemy_unit->getPosition());
						if (current_distance >= distance && potential.position() != potential.initial_position()) {
							potential.block();
						} else {
							if (potential.position() != potential.initial_position()) {
								potential.repel_units(all_enemy_units_);
							}
							potential.repel_storms();
							if (potential.empty()) potential.add_potential(selected_enemy_unit, -0.1);
						}
					});
					if (!moved) combat_unit->burrow();
				} else {
					Position intercept_position = calculate_interception_position(combat_unit, selected_enemy_unit);
					path_finder.execute_path(combat_unit, intercept_position, [this,combat_unit,selected_enemy_unit,intercept_position](){
						unit_move(combat_unit, intercept_position);
					});
				}
			} else {
				if (can_attack_in_range_with_prediction(combat_unit, selected_enemy_unit)) {
					unit_attack(combat_unit, selected_enemy_unit);
				} else {
					combat_unit->unburrow();
				}
			}
		} else {
			if (combat_unit->isBurrowed()) {
				if (!combat_unit->isUnderAttack()) {
					int smallest_distance = INT_MAX;
					for (Unit enemy_unit : harassable_enemy_units_) {
						if (can_attack_in_range_with_prediction(combat_unit, enemy_unit)) {
							int distance = combat_unit->getDistance(enemy_unit);
							if (distance < smallest_distance) {
								selected_enemy_unit = enemy_unit;
								smallest_distance = distance;
							}
						}
					}
				}
				if (selected_enemy_unit != nullptr) {
					unit_attack(combat_unit, selected_enemy_unit);
				} else {
					combat_unit->unburrow();
				}
			} else {
				if (target_position == Positions::Unknown) {
					random_move(combat_unit);
				} else if (target_position.isValid() && enable_advance) {
					path_finder.execute_path(combat_unit, target_position, [target_position,combat_unit](){
						unit_move(combat_unit, target_position);
					});
				} else {
					bool moved = unit_potential(combat_unit, [this](UnitPotential& potential){
						potential.repel_storms();
					});
					if (!moved) {
						if (stage_position.isValid() && enable_retreat) {
							move_retreat(combat_unit, stage_position);
						} else {
							if (!combat_unit->isHoldingPosition()) combat_unit->holdPosition();
						}
					}
				}
			}
		}
	}
}

void MicroManager::apply_siege_tank_orders()
{
	const int min_range = UnitTypes::Terran_Siege_Tank_Siege_Mode.groundWeapon().minRange();
	const int max_range = UnitTypes::Terran_Siege_Tank_Siege_Mode.groundWeapon().maxRange();
	const int siege_range = 5 * 32;
	
	remove_missing_keys(siege_tank_state_, siege_tanks_);
	
	std::vector<Unit> nearby_enemy_units;
	std::vector<Unit> nearby_friendly_ground;
	for (auto& combat_unit : siege_tanks_) {
		if (loading_units_.count(combat_unit) > 0) continue;
		
		CombatState& combat_state = combat_state_.at(combat_unit);
		Position target_position = combat_state.target_position();
		Position stage_position = combat_state.stage_position();
		
		bool enable_advance = true;
		bool enable_retreat = true;
		
		SiegeTankState& siege_tank_state = siege_tank_state_[combat_unit];
		
		if (combat_unit->isSieged()) {
			std::tie(enable_advance, enable_retreat) = determine_advance_retreat_for_special_unit(combat_unit);
			enable_retreat = enable_retreat || !target_position.isValid();
			if (siege_tank_state.defense_position.isValid() &&
				siege_tank_state.defense_position == stage_position) {
				enable_retreat = false;
			}
			
			bool should_move = false;
			
			if (enable_advance || !enable_retreat) {
				if (!is_on_cooldown(combat_unit, false) &&
					!unit_has_pending_command(combat_unit, UnitCommandTypes::Attack_Unit)) {
					struct PossibleTarget {
						Unit unit = nullptr;
						int distance;
						double kills;
					};
					
					std::vector<PossibleTarget> possible_targets;
					for (Unit enemy_unit : harassable_enemy_units_) {
						if (can_attack_in_range_with_prediction(combat_unit, enemy_unit)) {
							int distance = combat_unit->getDistance(enemy_unit);
							if (distance >= min_range) {
								FastPosition enemy_position = enemy_unit->getPosition();
								UnitType enemy_type = enemy_unit->getType();
								int left = enemy_position.x - (3 * enemy_type.dimensionLeft() + 2) / 4;
								int right = enemy_position.x + (3 * enemy_type.dimensionRight() + 2) / 4;
								int top = enemy_position.y - (3 * enemy_type.dimensionUp() + 2) / 4;
								int bottom = enemy_position.y + (3 * enemy_type.dimensionDown() + 2) / 4;
								int impact_x = clamp(left, combat_unit->getPosition().x, right);
								int impact_y = clamp(top, combat_unit->getPosition().y, bottom);
								FastPosition impact_position(impact_x, impact_y);
								double kills = 0.0;
								for (Unit other_enemy_unit : harassable_enemy_units_) {
									int other_distance = other_enemy_unit->getDistance(impact_position);
									if (other_distance > WeaponTypes::Arclite_Shock_Cannon.outerSplashRadius()) continue;
									int damage_divisor = 0;
									if (other_distance <= WeaponTypes::Arclite_Shock_Cannon.innerSplashRadius()) {
										damage_divisor = 1;;
									} else if (!other_enemy_unit->isBurrowed()) {
										damage_divisor = (other_distance <= WeaponTypes::Arclite_Shock_Cannon.medianSplashRadius()) ? 2 : 4;
									}
									if (damage_divisor > 0) {
										DamageModel damage_model(other_enemy_unit);
										damage_model.apply_damage(combat_unit, damage_divisor);
										int supply = (other_enemy_unit->getType().supplyRequired() + TacticsManager::defense_supply_equivalent(other_enemy_unit));
										if (supply > 0) {
											if (damage_model.is_dead()) {
												kills += supply;
											} else {
												double loss = damage_model.hp_lost() + damage_model.shields_lost();
												double max = other_enemy_unit->getType().maxHitPoints() + other_enemy_unit->getType().maxShields();
												kills += ((loss / max) * supply);
											}
										}
									}
								}
								possible_targets.push_back(PossibleTarget{enemy_unit, distance, kills});
							}
						}
					}
					
					Unit selected_enemy_unit = smallest_priority(possible_targets, [combat_unit](auto& possible_target){
						return std::make_tuple(-possible_target.kills,
											   possible_target.distance,
											   possible_target.unit->getID());
					}).unit;
					
					if (selected_enemy_unit != nullptr) {
						unit_attack(combat_unit, selected_enemy_unit);
					} else {
						should_move = true;
					}
				}
			}
			
			if (should_move) {
				if (target_position == Positions::Unknown ||
					(target_position.isValid() && enable_advance) ||
					(stage_position.isValid() && enable_retreat)) {
					combat_unit->unsiege();
					siege_tank_state = SiegeTankState();
				} else if (!combat_unit->isIdle()) {
					combat_unit->stop();
				}
			}
		} else {
			Position saved_defense_position = Positions::None;
			int saved_defense_frame_limit = -1;
			std::swap(siege_tank_state.defense_position, saved_defense_position);
			std::swap(siege_tank_state.defense_frame_limit, saved_defense_frame_limit);
			
			Unit selected_enemy_unit = nullptr;
			Unit nearby_sieged_tank = determine_nearby_sieged_tank(combat_unit);
			if (nearby_sieged_tank != nullptr) {
				selected_enemy_unit = nearby_sieged_tank;
			} else {
				CombatUnitTarget& target = combat_unit_targets_[combat_unit];
				if (target.should_update_target(combat_unit)) {
					Unit previous_unit = target.unit;
					std::tie(target.unit, target.enable_advance, target.enable_retreat) = select_enemy_unit_for_combat_unit(combat_unit);
					target.frame = Broodwar->getFrameCount();
					if (target.unit != previous_unit) {
						target.last_switch_frame = Broodwar->getFrameCount();
					}
				}
				selected_enemy_unit = target.unit;
				enable_advance = target.enable_advance;
				enable_retreat = target.enable_retreat || !target_position.isValid();
			}
			
			if (selected_enemy_unit != nullptr) {
				int distance = combat_unit->getDistance(selected_enemy_unit);
				if (Broodwar->self()->hasResearched(TechTypes::Tank_Siege_Mode) &&
					is_siege_allowed(combat_unit) &&
					distance > siege_range &&
					distance <= max_range) {
					bool moved = unit_potential(combat_unit, [this,combat_unit,selected_enemy_unit,siege_range,distance](UnitPotential& potential){
						int current_distance = calculate_distance(combat_unit->getType(), potential.position(), selected_enemy_unit->getType(), selected_enemy_unit->getPosition());
						if (current_distance >= distance && potential.position() != potential.initial_position()) {
							potential.block();
						} else {
							if (potential.position() != potential.initial_position()) {
								potential.repel_units(all_enemy_units_);
							}
							potential.repel_storms();
							if (potential.empty() && current_distance > siege_range) {
								potential.add_potential(selected_enemy_unit, -0.1);
							}
						}
					});
					if (!moved) combat_unit->siege();
				} else if (is_on_cooldown(combat_unit, selected_enemy_unit->isFlying())) {
					set_nearby_units_for_kiting(nearby_enemy_units, nearby_friendly_ground, combat_unit);
					unit_potential(combat_unit, [this,selected_enemy_unit,&nearby_enemy_units,&nearby_friendly_ground](UnitPotential& potential){
						potential.kite_units(nearby_enemy_units);
						int ground_splash_distance = threat_grid.ground_splash_distance(FastTilePosition(potential.position()));
						if (ground_splash_distance > 0) {
							potential.repel_friendly(nearby_friendly_ground, potential.unit(), ground_splash_distance);
						}
						potential.repel_storms();
						bool step_in = potential.empty();
						potential.repel_buildings();
						potential.repel_terrain();
						if (step_in) {
							potential.add_potential(selected_enemy_unit, -0.1);
						}
					});
				} else {
					Position intercept_position = calculate_interception_position(combat_unit, selected_enemy_unit);
					path_finder.execute_path(combat_unit, intercept_position, [this,combat_unit,selected_enemy_unit,intercept_position](){
						if (selected_enemy_unit->getPosition() == intercept_position) {
							unit_attack(combat_unit, selected_enemy_unit);
						} else {
							unit_move(combat_unit, intercept_position);
						}
					});
				}
			} else if (target_position == Positions::Unknown) {
				random_move(combat_unit);
			} else if (target_position.isValid() && enable_advance) {
				if (!combat_state.near_target_only()) {
					path_finder.execute_path(combat_unit, target_position, [target_position,combat_unit](){
						unit_move(combat_unit, target_position);
					});
				} else {
					move_with_blockade_breaking(combat_unit, target_position);
				}
			} else {
				bool moved = unit_potential(combat_unit, [this](UnitPotential& potential){
					potential.repel_storms();
				});
				if (!moved) {
					if (stage_position.isValid() && enable_retreat) {
						bool order_issued = false;
						int distance = combat_unit->getDistance(stage_position);
						if (distance < 256 &&
							Broodwar->self()->hasResearched(TechTypes::Tank_Siege_Mode) &&
							is_siege_allowed(combat_unit)) {
							int frame_limit = Broodwar->getFrameCount() + int(1.5 * distance / combat_unit->getType().topSpeed() + 0.5);
							if (saved_defense_position == stage_position) {
								frame_limit = std::min(frame_limit, saved_defense_frame_limit);
							}
							siege_tank_state.defense_position = stage_position;
							siege_tank_state.defense_frame_limit = frame_limit;
							if (Broodwar->getFrameCount() >= frame_limit) {
								combat_unit->siege();
								order_issued = true;
							}
						}
						if (!order_issued) move_retreat(combat_unit, stage_position);
					} else if (Broodwar->self()->hasResearched(TechTypes::Tank_Siege_Mode) &&
							   is_siege_allowed(combat_unit)) {
						combat_unit->siege();
					} else {
						if (!combat_unit->isHoldingPosition()) combat_unit->holdPosition();
					}
				}
			}
		}
	}
}

void MicroManager::apply_air_to_air_unit_orders()
{
	std::vector<Unit> enemy_air_units;
	std::vector<Unit> enemy_other_units;
	for (auto& unit : all_enemy_units_) {
		if (can_attack(unit, true)) {
			if (unit->isFlying() && !unit->isInvincible()) {
				enemy_air_units.push_back(unit);
			} else {
				enemy_other_units.push_back(unit);
			}
		}
	}
	std::map<Unit,int> group_size_map = determine_units_per_group(air_to_air_units_);
	
	remove_missing_keys(air_to_air_targets_, air_to_air_units_);
	for (auto& combat_unit : air_to_air_units_) {
		bool order_issued = false;
		if (!order_issued) {
			order_issued = unit_potential(combat_unit, [&enemy_other_units](UnitPotential& potential){
				potential.repel_units(enemy_other_units, 32);
				potential.repel_storms();
			});
		}
		if (!order_issued) {
			FastPosition predicted_position = predict_position(combat_unit, Broodwar->getRemainingLatencyFrames());
			std::vector<Unit> too_close_units;
			int max_range = weapon_max_range(combat_unit, true);
			for (auto unit : enemy_air_units) {
				FastPosition enemy_predicted_position = predict_position(unit, Broodwar->getRemainingLatencyFrames());
				int distance = calculate_distance(combat_unit->getType(), predicted_position, unit->getType(), enemy_predicted_position);
				int max_distance = max_range - 8;
				if (unit->getType() == UnitTypes::Zerg_Scourge &&
					unit->getAirWeaponCooldown() <= Broodwar->getRemainingLatencyFrames()) {
					max_distance = max_range - 64;
				}
				if (distance <= max_distance) {
					too_close_units.push_back(unit);
				}
			}
			if (!too_close_units.empty()) {
				int scourge_count = 0;
				int other_count = 0;
				for (Unit unit : too_close_units) {
					if (unit->getType() == UnitTypes::Zerg_Scourge) {
						scourge_count++;
					} else {
						other_count++;
					}
				}
				int required_group_size = (other_count > 0 ? other_count + 1 : 0) + scourge_count * 7;
				int group_size = group_size_map[combat_unit];
				
				if (group_size < required_group_size) {
					key_value_vector<Unit,DistanceWithPriority> my_static_air_defense_units;
					for (auto& information_unit : information_manager.my_units()) {
						if ((information_unit->type == UnitTypes::Protoss_Photon_Cannon ||
							 information_unit->type == UnitTypes::Terran_Missile_Turret) &&
							information_unit->is_completed()) {
							int priority = 2;
							if (base_state.natural_base() != nullptr &&
								information_unit->area == base_state.natural_base()->GetArea()) {
								priority = 0;
							} else if (information_unit->area == base_state.start_base()->GetArea()) {
								priority = 1;
							}
							int distance = combat_unit->getPosition().getApproxDistance(information_unit->position);
							my_static_air_defense_units.emplace_back(information_unit->unit, DistanceWithPriority(distance, priority, information_unit->unit->getID()));
						}
					}
					Unit my_static_air_defense_unit = key_with_smallest_value(my_static_air_defense_units);
					if (my_static_air_defense_unit != nullptr) {
						FastPosition e_sum(0, 0);
						int e_count = 0;
						for (auto& too_close_unit : too_close_units) {
							e_sum += too_close_unit->getPosition();
							e_count++;
						}
						const int range = my_static_air_defense_unit->getType().airWeapon().maxRange();
						FastPosition e = e_sum / e_count;
						FastPosition c = combat_unit->getPosition();
						FastPosition s = my_static_air_defense_unit->getPosition();
						FastPosition a = scale_line_segment(s, c, range);
						FastPosition b = s + s - a;
						FastPosition position = s;
						if (!can_attack_in_range(my_static_air_defense_unit, combat_unit)) {
							int a_distance = c.getApproxDistance(a);
							int b_distance = c.getApproxDistance(b);
							position = (a_distance > b_distance) ? a : b;
						} else if (c != e) {
							FastPosition sa = a - s;
							FastPosition sc = c - s;
							FastPosition se = e - s;
							int ip_ac = (sa.x * sc.x + sa.y * sc.y);
							int ip_ae = (sa.x * se.x + sa.y * se.y);
							position = (ip_ac > ip_ae) ? a : b;
						}
						position.makeValid();
						if (combat_unit->getPosition().getApproxDistance(position) >= 32) {
							unit_move(combat_unit, position);
							order_issued = true;
						}
					}
				}
			}
		}
		if (!order_issued) {
			Unit selected_enemy_unit = select_enemy_unit_air_to_air(combat_unit, harassable_enemy_units_);
			if (selected_enemy_unit != nullptr) {
				if (!can_attack_in_range_with_prediction(combat_unit, selected_enemy_unit) ||
					(selected_enemy_unit->getType() != UnitTypes::Zerg_Scourge &&
					 (is_on_cooldown(combat_unit, selected_enemy_unit->isFlying()) ||
					  unit_has_pending_command(combat_unit, UnitCommandTypes::Attack_Unit)))) {
					move_flyer_near_safe_approach_unsafe(combat_unit, selected_enemy_unit->getPosition());
				} else {
					unit_attack(combat_unit, selected_enemy_unit);
				}
			} else {
				Position position = Positions::None;
				
				if (air_to_air_targets_.count(combat_unit) > 0) {
					AirToAirTarget& target = air_to_air_targets_[combat_unit];
					if (Broodwar->getFrameCount() >= target.expire_frame || combat_unit->getDistance(target.position) < 32) {
						air_to_air_targets_.erase(combat_unit);
					} else {
						position = target.position;
					}
				}
				
				if (air_to_air_targets_.count(combat_unit) == 0) {
					position = pick_air_scout_location();
					if (position.isValid()) {
						AirToAirTarget& target = air_to_air_targets_[combat_unit];
						target.position = position;
						int distance = combat_unit->getDistance(position);
						int frames = (int)(distance / combat_unit->getType().topSpeed());
						target.expire_frame = Broodwar->getFrameCount() + frames;
					}
				}
				
				if (position.isValid()) {
					move_flyer_near_safe(combat_unit, position);
				}
			}
		}
	}
}

//#define MUTALISK_DEBUG

void MicroManager::apply_mutalisk_orders()
{
	remove_missing_keys(mutalisk_dive_, mutalisks_);
	int mutalisk_supply = training_manager.unit_count_completed(UnitTypes::Zerg_Mutalisk) * UnitTypes::Zerg_Mutalisk.supplyRequired();
	int defense_without_mutalisks_supply = tactics_manager.defense_supply() - mutalisk_supply;
	int army_without_mutalisks_supply = tactics_manager.army_supply() - mutalisk_supply;
	std::vector<Unit> static_defense_units;
	for (auto information_unit : information_manager.my_units()) {
		if (information_unit->is_completed() &&
			(information_unit->type == UnitTypes::Zerg_Sunken_Colony ||
			 information_unit->type == UnitTypes::Zerg_Spore_Colony)) {
			static_defense_units.push_back(information_unit->unit);
		}
	}
	std::vector<Unit> keep_runby_enemies;
	for (auto information_unit : information_manager.enemy_units()) {
		if (information_unit->position.isValid() &&
			information_unit->base_distance == 0) {
			Unit enemy_unit = information_unit->unit;
			keep_runby_enemies.push_back(enemy_unit);
			if (std::none_of(static_defense_units.begin(),
							 static_defense_units.end(),
							 [enemy_unit](auto static_defense_unit){ return can_attack_in_range(static_defense_unit, enemy_unit); })) {
				mutalisk_runby_enemies_.insert(enemy_unit);
			}
		}
	}
	remove_missing(mutalisk_runby_enemies_, keep_runby_enemies);
	int enemy_runby_supply = 0;
	for (auto unit : mutalisk_runby_enemies_) enemy_runby_supply += unit->getType().supplyRequired();
	if (mutalisk_defense_) {
		if ((defense_without_mutalisks_supply >= 2 * 20 ||
			 tactics_manager.enemy_offense_supply() <= defense_without_mutalisks_supply / 2) &&
			enemy_runby_supply <= army_without_mutalisks_supply / 2) {
			mutalisk_defense_ = false;
		}
	} else {
		if (defense_without_mutalisks_supply <= 2 * 10 &&
			tactics_manager.enemy_offense_supply() > defense_without_mutalisks_supply) {
			mutalisk_defense_ = true;
		}
		if (enemy_runby_supply > army_without_mutalisks_supply) {
			mutalisk_defense_ = true;
		}
	}
	Position target_position = Positions::None;
	bool defense_target_position = false;
	if (mutalisk_defense_) {
		std::vector<InformationUnit*> enemy_units;
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->position.isValid() &&
				enemy_unit->base_distance <= 320 &&
				(enemy_unit->unit->exists() ? enemy_unit->unit->isDetected() : !enemy_unit->type.hasPermanentCloak())) {
				enemy_units.push_back(enemy_unit);
			}
		}
		InformationUnit* enemy_unit = smallest_priority(enemy_units, [](auto& enemy_unit){
			return std::make_tuple(enemy_unit->base_distance, enemy_unit->unit->getID());
		});
		if (enemy_unit != nullptr) {
			target_position = enemy_unit->position;
			defense_target_position = true;
		}
	}
	if (!target_position.isValid()) {
		target_position = tactics_manager.enemy_base_attack_position();
	}
#ifdef MUTALISK_DEBUG
	FILE *f = fopen("bwapi-data\\write\\mutadefense.txt", "a");
	fprintf(f, "%d (%s) | %d %d %d | %d %d (%d,%d)\n",
			Broodwar->getFrameCount(),
			frame_to_string(Broodwar->getFrameCount()).c_str(),
			tactics_manager.enemy_offense_supply(),
			tactics_manager.defense_supply(),
			training_manager.unit_count_completed(UnitTypes::Zerg_Mutalisk) * UnitTypes::Zerg_Mutalisk.supplyRequired(),
			int(mutalisk_defense_),
			int(defense_target_position),
			target_position.x,
			target_position.y);
	fclose(f);
#endif
	std::vector<Unit> active_mutalisks;
	for (auto& combat_unit : mutalisks_) {
		if (combat_unit->isIrradiated()) {
			unit_move(combat_unit, FastPosition(0, 0));
		} else {
			active_mutalisks.push_back(combat_unit);
		}
	}
	std::vector<std::pair<std::vector<Unit>,FastPosition>> mutalisk_groups = group_mutalisks(active_mutalisks);
	FastPosition lead_center;
	if (target_position.isValid()) {
		lead_center = smallest_priority(mutalisk_groups, [target_position](auto& entry){
			const std::vector<Unit>& mutalisks = entry.first;
			const FastPosition& center = entry.second;
			return std::make_tuple(-int(mutalisks.size()), center.getApproxDistance(target_position), mutalisks[0]->getID());
		}).second;
	}
	
	std::vector<std::pair<Unit,int>> enemy_units_with_range;
	for (auto& enemy_unit : harassable_enemy_units_) {
		if (enemy_unit->isCompleted() && enemy_unit->isPowered()) {
			int range = weapon_max_range(enemy_unit, true);
			if (range > 0) {
				enemy_units_with_range.emplace_back(enemy_unit, range);
			}
		}
	}
	
	for (auto [mutalisks,center] : mutalisk_groups) {
		std::vector<Unit> non_diving_mutalisks;
		bool off_center = false;
		bool ready_to_dive = true;
		int max_cooldown_frames = 0;
		int count = int(mutalisks.size());
		int ball_size = std::max(16, count * 3);
		for (auto& combat_unit : mutalisks) {
			bool diving = false;
			auto mutalisk_dive = mutalisk_dive_.find(combat_unit);
			if (mutalisk_dive != mutalisk_dive_.end()) {
				bool ready = (combat_unit->getAirWeaponCooldown() <= Broodwar->getRemainingLatencyFrames());
#ifdef MUTALISK_DEBUG
				char filename[256];
				sprintf_s(filename, sizeof(filename), "bwapi-data\\write\\muta%d.txt", combat_unit->getID());
				FILE *f = fopen(filename, "a");
				fprintf(f, "%d(%s) | ready=%d, cooldown=%d, order=%s, distance=%d, target=%s[%d], ready_seen=%d, expire_frame=%d | ",
						Broodwar->getFrameCount(), frame_to_string(Broodwar->getFrameCount()).c_str(),
						int(ready),
						combat_unit->getAirWeaponCooldown(),
						combat_unit->getOrder().c_str(),
						combat_unit->getDistance(mutalisk_dive->second.target),
						mutalisk_dive->second.target->getType().c_str(),
						mutalisk_dive->second.target->getID(),
						int(mutalisk_dive->second.ready_seen),
						mutalisk_dive->second.expire_frame);
#endif
				if ((!mutalisk_dive->second.ready_seen || ready) &&
					mutalisk_dive->second.expire_frame > Broodwar->getFrameCount() &&
					mutalisk_dive->second.target->exists()) {
					if (ready &&
						can_attack_in_range_with_prediction(combat_unit, mutalisk_dive->second.target)) {
						unit_attack(combat_unit, mutalisk_dive->second.target);
#ifdef MUTALISK_DEBUG
						fputs("attack\n", f);
#endif
					} else {
						unit_move(combat_unit, mutalisk_dive->second.target->getPosition());
#ifdef MUTALISK_DEBUG
						fputs("move\n", f);
#endif
					}
					mutalisk_dive->second.ready_seen |= ready;
					diving = true;
				} else {
					mutalisk_dive_.erase(combat_unit);
#ifdef MUTALISK_DEBUG
						fputs("erase\n", f);
#endif
				}
#ifdef MUTALISK_DEBUG
				fclose(f);
#endif
			}
			if (diving) {
				ready_to_dive = false;
			} else {
				non_diving_mutalisks.push_back(combat_unit);
				int cooldown_frames = combat_unit->getAirWeaponCooldown() - Broodwar->getRemainingLatencyFrames();
				max_cooldown_frames = std::max(max_cooldown_frames, cooldown_frames);
			}
			if (center.getApproxDistance(combat_unit->getPosition()) > ball_size) {
				off_center = true;
			}
		}
		int min_dive_distance = int(UnitTypes::Zerg_Mutalisk.topSpeed() * max_cooldown_frames);
		
		std::vector<Unit> enemy_units;
		int enemy_air_count = 0;
		for (auto& enemy_unit : harassable_enemy_units_) {
			if (enemy_unit->getDistance(center) <= 320) {
				enemy_units.push_back(enemy_unit);
				if (enemy_unit->getType() == UnitTypes::Zerg_Mutalisk ||
					enemy_unit->getType() == UnitTypes::Zerg_Scourge) {
					enemy_air_count++;
				}
			}
		}
		
		const auto calculate_retreat_position = [&](FastPosition current_position){
			FastPosition retreat_position;
			double vx = 0.0;
			double vy = 0.0;
			int n = 0;
			for (auto& unit : all_enemy_units_) {
				if (unit->isCompleted() && !is_disabled(unit) && unit->isPowered()) {
					int max_distance = weapon_max_range(unit, true);
					if (max_distance > 0) {
						int distance = calculate_distance(UnitTypes::Zerg_Mutalisk, current_position, unit->getType(), unit->getPosition());
						if (distance < max_distance + 32 + ball_size) {
							double dx = current_position.x - unit->getPosition().x;
							double dy = current_position.y - unit->getPosition().y;
							double rnorm = 1.0 / std::sqrt(dx * dx + dy * dy);
							vx += (rnorm * dx);
							vy += (rnorm * dy);
							n++;
						}
					}
				}
			}
			for (auto& position : list_existing_storm_positions()) {
				int distance = calculate_distance(UnitTypes::Zerg_Mutalisk, current_position, position);
				if (distance < WeaponTypes::Psionic_Storm.outerSplashRadius() + 32 + ball_size) {
					double dx = current_position.x - position.x;
					double dy = current_position.y - position.y;
					double rnorm = 1.0 / std::sqrt(dx * dx + dy * dy);
					vx += (rnorm * dx);
					vy += (rnorm * dy);
					n++;
				}
			}
			if (n > 0) {
				vx /= n;
				vy /= n;
				
				double bx = 0.0;
				double by = 0.0;
				if (current_position.x < kMutaliskBorderRepulseDistance) {
					bx = kMutaliskBorderRepulseDistance - current_position.x;
				} else if (current_position.x > Broodwar->mapWidth() * 32 - kMutaliskBorderRepulseDistance) {
					bx = (Broodwar->mapWidth() * 32 - kMutaliskBorderRepulseDistance) - current_position.x;
				}
				if (current_position.y < kMutaliskBorderRepulseDistance) {
					by = kMutaliskBorderRepulseDistance - current_position.y;
				} else if (current_position.y > Broodwar->mapHeight() * 32 - kMutaliskBorderRepulseDistance) {
					by = (Broodwar->mapHeight() * 32 - kMutaliskBorderRepulseDistance) - current_position.y;
				}
				if (bx != 0.0 || by != 0.0) {
					double rnormb = 1.0 / std::sqrt(bx * bx + by * by);
					bx *= rnormb;
					by *= rnormb;
					double rnormv = 1.0 / std::sqrt(vx * vx + vy * vy);
					vx *= rnormv;
					vy *= rnormv;
					vx += bx;
					vy += by;
				}
				
				double factor = 128.0 / std::sqrt(vx * vx + vy * vy);
				vx *= factor;
				vy *= factor;
				retreat_position = current_position + FastPosition(int(vx + 0.5), int(vy + 0.5));
			}
			return retreat_position;
		};
		
		struct DiveTarget
		{
			Unit unit = nullptr;
			int priority;
			bool kill;
			double ratio;
			int dive_distance;
		};
		
#ifdef MUTALISK_DEBUG
		FILE *f1 = fopen("bwapi-data\\write\\mutatarget.txt", "a");
#endif
		std::vector<DiveTarget> dive_targets;
		if (ready_to_dive &&
			count >= enemy_air_count) {
			for (auto& enemy_unit : enemy_units) {
				FastPosition delta = edge_to_edge_delta(UnitTypes::Zerg_Mutalisk, center, enemy_unit->getType(), enemy_unit->getPosition());
				if (enemy_unit->getPosition().x < center.x) delta.x = -delta.x;
				if (enemy_unit->getPosition().y < center.y) delta.y = -delta.y;
				FastPosition at_position = center + delta;
				FastPosition dive_position;
				if (FastPosition{0, 0}.getApproxDistance(delta) <= UnitTypes::Zerg_Mutalisk.airWeapon().maxRange()) {
					dive_position = center;
				} else {
					dive_position = scale_line_segment(at_position, center, UnitTypes::Zerg_Mutalisk.airWeapon().maxRange());
				}
				
				int retreat_distance = int(UnitTypes::Zerg_Mutalisk.airWeapon().damageCooldown() * UnitTypes::Zerg_Mutalisk.topSpeed() + 0.5);
				FastPosition retreat_position = calculate_retreat_position(dive_position);
				if (retreat_position != Positions::None && retreat_position != dive_position) {
					retreat_position = scale_line_segment(dive_position, retreat_position, retreat_distance);
				}
				
				DamageModel damage_model(enemy_unit);
				for (int i = 0; i < count; i++) {
					damage_model.apply_damage(UnitTypes::Zerg_Mutalisk, Broodwar->self());
				}
				double supply_kill;
				bool kill;
				int enemy_supply = enemy_unit->getType().supplyRequired() + TacticsManager::defense_supply_equivalent(enemy_unit);
				if (enemy_unit->getType() == UnitTypes::Terran_Missile_Turret ||
					enemy_unit->getType() == UnitTypes::Zerg_Spore_Colony) {
					enemy_supply += 3;
				}
				if (damage_model.is_dead()) {
					kill = true;
					supply_kill = enemy_supply;
				} else {
					kill = false;
					double loss = damage_model.hp_lost() + damage_model.shields_lost();
					double max = enemy_unit->getType().maxHitPoints() + enemy_unit->getType().maxShields();
					supply_kill = (loss / max) * enemy_supply;
				}
				
				double damage_sum = 0.0;
				for (auto [other_enemy_unit, range] : enemy_units_with_range) {
					// @
					/*if (other_enemy_unit == enemy_unit && enemy_unit->getType() == UnitTypes::Protoss_Dragoon) {
						Broodwar->drawLineMap(center, dive_position, Colors::White);
						Broodwar->drawCircleMap(enemy_unit->getPosition(), range + max_unit_dimension(enemy_unit->getType()), Colors::White);
					}*/
					// /@
					int augmented_range = range + max_unit_dimension(other_enemy_unit->getType()) + max_unit_dimension(UnitTypes::Zerg_Mutalisk);
					int length = line_segment_length_within_circle(center, dive_position, other_enemy_unit->getPosition(), augmented_range);
					if (retreat_position != Positions::None && (!kill || other_enemy_unit != enemy_unit)) {
						length += line_segment_length_within_circle(dive_position, retreat_position, other_enemy_unit->getPosition(), augmented_range);
					}
					if (length > 0) {
						int bunker_marines_loaded = 0;
						if (other_enemy_unit->getType() == UnitTypes::Terran_Bunker) {
							bunker_marines_loaded = information_manager.bunker_marines_loaded(other_enemy_unit);
						}
						DamageModel damage_model(UnitTypes::Zerg_Mutalisk, Broodwar->self(), 0);
						int frames = int(length / UnitTypes::Zerg_Mutalisk.topSpeed() + 0.5);
						damage_model.apply_damage_for_frames(other_enemy_unit->getType(), other_enemy_unit->getPlayer(), frames, bunker_marines_loaded);
						double damage = damage_model.hp_lost();
						damage_sum += damage;
						// @
						/*if (other_enemy_unit == enemy_unit && enemy_unit->getType() == UnitTypes::Protoss_Dragoon) {
							FILE *f = fopen("bwapi-data\\write\\divelength.txt", "a");
							fprintf(f, "%d %d %g\n", length, frames, damage);
							fclose(f);
						}*/
						// /@
					}
				}
				
				double supply_loss = (damage_sum / UnitTypes::Zerg_Mutalisk.maxHitPoints()) * UnitTypes::Zerg_Mutalisk.supplyRequired();
				double ratio = supply_kill / std::max(supply_loss, 1e-6);
				
#ifdef MUTALISK_DEBUG
				fprintf(f1, "- %s[%d] kill=%d supply_kill=%g supply_loss=%g ratio=%g\n", enemy_unit->getType().c_str(), enemy_unit->getID(), kill, supply_kill, supply_loss, ratio);
#endif
				
				if ((ratio >= (kill ? 1.0 : 1.25) && count >= 3) || supply_loss == 0.0) {
					int priority = 0;
					if (supply_loss == 0.0 &&
						(can_attack(enemy_unit->getType(), true) &&
						 !enemy_unit->isCompleted()) ||
						(enemy_unit->getType() == UnitTypes::Terran_SCV &&
						 enemy_unit->getBuildUnit() != nullptr &&
						 can_attack(enemy_unit->getBuildUnit()->getType(), true) &&
						 !enemy_unit->getBuildUnit()->isCompleted())) {
						priority = 1;
					} else if (supply_loss == 0.0 && enemy_unit->getType().isWorker()) {
						priority = 2;
					} else if (can_attack(enemy_unit->getType(), true) || is_spellcaster(enemy_unit->getType())) {
						priority = 3;
					} else if (supply_loss == 0.0) {
						if (can_attack(enemy_unit, false)) {
							priority = 4;
						} else if (!is_low_priority_target(enemy_unit)) {
							priority = 5;
						} else {
							priority = 6;
						}
					}
					if (priority > 0 &&
						(priority < 4 || enemy_unit->getDistance(target_position) < 240) &&
						(!defense_target_position || information_manager.all_units().at(enemy_unit).base_distance <= 320)) {
						int dive_distance = INT_MAX;
						for (Unit combat_unit : mutalisks) {
							dive_distance = std::min(dive_distance, predict_position(combat_unit).getApproxDistance(dive_position));
						}
						dive_targets.push_back(DiveTarget{enemy_unit, priority, kill, -ratio, dive_distance});
					}
				}
			}
		}
		DiveTarget dive_target = smallest_priority(dive_targets, [center](auto& dive_target){
			return std::make_tuple(dive_target.priority, !dive_target.kill, dive_target.ratio, dive_target.unit->getDistance(center), dive_target.unit->getID());
		});
#ifdef MUTALISK_DEBUG
		fprintf(f1, "Selected (frame=%d, time=%s, count=%d, min_dive_distance=%d): ", Broodwar->getFrameCount(), frame_to_string(Broodwar->getFrameCount()).c_str(), count, min_dive_distance);
		if (dive_target.unit != nullptr) {
			fprintf(f1, "%s[%d] prio=%d kill=%d ratio=%g dive_distance=%d\n\n", dive_target.unit->getType().c_str(), dive_target.unit->getID(), dive_target.priority, dive_target.kill, -dive_target.ratio, dive_target.dive_distance);
		} else {
			fprintf(f1, "None (ready_to_dive=%d off_center=%d)\n\n", int(ready_to_dive), int(off_center));
		}
		fclose(f1);
#endif
		FastPosition retreat_position = calculate_retreat_position(center);
		
		FastPosition safe_move_position;
		for (auto& combat_unit : non_diving_mutalisks) {
			bool order_issued = false;
			
			if (!order_issued && lead_center.isValid() && center != lead_center && target_position != Positions::Unknown) {
				order_issued = move_flyer_near_safe(combat_unit, lead_center);
			}
			
			if (!order_issued && dive_target.unit != nullptr && dive_target.dive_distance >= min_dive_distance) {
				if (off_center) {
					unit_move(combat_unit, center);
				} else {
					bool ready = (combat_unit->getAirWeaponCooldown() <= Broodwar->getRemainingLatencyFrames());
					if (ready &&
						can_attack_in_range_with_prediction(combat_unit, dive_target.unit)) {
						unit_attack(combat_unit, dive_target.unit);
					} else {
						unit_move(combat_unit, dive_target.unit->getPosition());
					}
					int frames = int(1.5 * dive_target.dive_distance / UnitTypes::Zerg_Mutalisk.topSpeed() + 0.5) + 24;
					mutalisk_dive_[combat_unit] = MutaliskDive{dive_target.unit, ready, Broodwar->getFrameCount() + frames};
#ifdef MUTALISK_DEBUG
					FILE *f2 = fopen("bwapi-data\\write\\mutadive.txt", "a");
					fprintf(f2, "%d (%s) Mutalisk [%d] dives target %s [%d]\n",
							Broodwar->getFrameCount(),
							frame_to_string(Broodwar->getFrameCount()).c_str(),
							combat_unit->getID(),
							dive_target.unit->getType().c_str(),
							dive_target.unit->getID());
					fclose(f2);
#endif
				}
				order_issued = true;
			}
			
			if (!order_issued && retreat_position != Positions::None) {
				unit_move(combat_unit, retreat_position);
				order_issued = true;
			}
			
			if (!order_issued && target_position.isValid()) {
				if (safe_move_position.isValid()) {
					unit_move(combat_unit, safe_move_position);
					order_issued = true;
				} else {
					order_issued = move_flyer_near_safe(combat_unit, target_position);
					if (order_issued &&
						combat_unit->getLastCommand().getType() == UnitCommandTypes::Move) {
						safe_move_position = combat_unit->getLastCommand().getTargetPosition();
					}
				}
			}
			
			if (!order_issued && target_position == Positions::Unknown) {
				random_move(combat_unit);
				order_issued = true;
			}
			
			if (!order_issued && !combat_unit->isIdle()) {
				combat_unit->stop();
			}
		}
	}
}

void MicroManager::apply_observer_orders()
{
	remove_missing_keys(observer_targets_, observers_);
	if (!observers_.empty()) {
		bool no_non_scouting_observer_exists = std::none_of(observers_.begin(), observers_.end(), [this](auto& unit){return !observer_targets_[unit].scouting;});
		bool want_non_scouting_observer = opponent_model.cloaked_or_mine_present() || observers_.size() >= 2;
		if (no_non_scouting_observer_exists && want_non_scouting_observer) {
			Unit selected_unit = smallest_priority(observers_, [](Unit special_unit){return -special_unit->getID();});
			observer_targets_[selected_unit].scouting = false;
		}
		order_observer_worker_need_detection();
		for (auto& special_unit : observers_) {
			bool order_issued = false;
			
			if (!order_issued) {
				Position position = Positions::None;
				ObserverTarget& target = observer_targets_[special_unit];
				
				if (!target.scouting) {
					Position position = determine_first_detector_location(special_unit);
					if (position.isValid()) {
						order_issued = move_flyer_near_safe(special_unit, position);
					}
				} else {
					Position position = Positions::None;
					
					if (target.position.isValid()) {
						if (Broodwar->getFrameCount() >= target.expire_frame || special_unit->getDistance(target.position) < 32) {
							target.position = Positions::None;
						}
					}
					
					if (!target.position.isValid()) {
						Position scout_position = pick_air_scout_location();
						if (scout_position.isValid()) {
							target.position = scout_position;
							int distance = special_unit->getDistance(scout_position);
							double top_speed = Broodwar->self()->topSpeed(special_unit->getType());
							int frames = int(1.25 * distance / top_speed);
							target.expire_frame = Broodwar->getFrameCount() + frames;
						}
					}
					
					if (target.position.isValid()) position = target.position;
					
					if (!position.isValid()) {
						Position stage_position = combat_state_.at(special_unit).stage_position();
						if (stage_position.isValid()) position = stage_position;
					}
					
					if (position.isValid()) {
						order_issued = move_flyer_near_safe(special_unit, position);
						if (!order_issued) {
							target.position = Positions::None;
						} else {
							int distance = special_unit->getDistance(target.position);
							double top_speed = Broodwar->self()->topSpeed(special_unit->getType());
							int frames = int(1.25 * distance / top_speed);
							target.expire_frame = std::min(target.expire_frame, Broodwar->getFrameCount() + frames);
						}
					}
				}
			}
			
			if (!order_issued) {
				if (!special_unit->isHoldingPosition()) special_unit->holdPosition();
			}
		}
	}
}

void MicroManager::apply_arbiter_orders()
{
	std::set<Unit> casting_arbiters;
	for (auto& tentative_stasis : tentative_stasises_) casting_arbiters.insert(tentative_stasis.unit);
	for (auto& special_unit : arbiters_) {
		CombatState& combat_state = combat_state_.at(special_unit);
		Position target_position = combat_state.target_position();
		Position stage_position = combat_state.stage_position();
		
		bool order_issued = false;
		bool stasis_possible = (Broodwar->self()->hasResearched(TechTypes::Stasis_Field) &&
								special_unit->getEnergy() >= TechTypes::Stasis_Field.energyCost() &&
								special_unit->getSpellCooldown() == 0);
		
		if (casting_arbiters.count(special_unit) > 0) order_issued = true;
		
		if (!order_issued && stasis_possible) {
			order_issued = stasis(special_unit);
		}
		
		if (!order_issued) {
			Position position = Positions::None;
			if (target_position.isValid()) {
				Unit closest_combat_unit = combat_unit_closest_to_position(target_position);
				if (closest_combat_unit != nullptr) position = closest_combat_unit->getPosition();
			}
			if (!position.isValid()) {
				Unit closest_combat_unit = combat_unit_closest_to_special_unit(special_unit);
				if (closest_combat_unit != nullptr) position = closest_combat_unit->getPosition();
			}
			if (!position.isValid()) {
				if (stage_position.isValid()) position = stage_position;
			}
			if (position.isValid()) {
				order_issued = unit_potential(special_unit, [this,position](UnitPotential& potential){
					potential.repel_units(all_enemy_units_);
					potential.repel_storms();
					potential.repel_emps();
					if (potential.empty()) {
						potential.add_potential(position, -0.1);
						for (auto& other_arbiter : arbiters_) {
							if (other_arbiter != potential.unit()) {
								potential.add_potential(other_arbiter, 0.1, 2 * WeaponTypes::EMP_Shockwave.outerSplashRadius());
							}
						}
					}
				});
			}
		}
		
		if (!order_issued) {
			if (!special_unit->isHoldingPosition()) special_unit->holdPosition();
		}
	}
}

void MicroManager::apply_science_vessel_orders()
{
	Unit science_vessel_for_worker_need_detection = nullptr;
	Position worker_need_detetection_position = determine_worker_need_detection_position();
	if (worker_need_detetection_position.isValid()) {
		science_vessel_for_worker_need_detection = smallest_priority(science_vessels_, [worker_need_detetection_position](auto& unit){
			return unit->getDistance(worker_need_detetection_position);
		});
	}
	
	std::set<Unit> casting_science_vessels;
	for (auto& tentative_irradiate : tentative_irradiates_) casting_science_vessels.insert(tentative_irradiate.unit);
	for (auto& tentative_emp : tentative_emps_) casting_science_vessels.insert(tentative_emp.unit);
	
	for (auto& special_unit : science_vessels_) {
		CombatState& combat_state = combat_state_.at(special_unit);
		Position target_position = combat_state.target_position();
		Position stage_position = combat_state.stage_position();
		
		bool order_issued = false;
		bool irradiate_possible = (Broodwar->self()->hasResearched(TechTypes::Irradiate) &&
								   special_unit->getEnergy() >= TechTypes::Irradiate.energyCost() &&
								   special_unit->getSpellCooldown() == 0);
		bool emp_possible = (Broodwar->self()->hasResearched(TechTypes::EMP_Shockwave) &&
							 special_unit->getEnergy() >= TechTypes::EMP_Shockwave.energyCost() &&
							 special_unit->getSpellCooldown() == 0);
		
		if (casting_science_vessels.count(special_unit) > 0) order_issued = true;
		
		if (!order_issued && irradiate_possible) {
			order_issued = irradiate(special_unit);
		}
		
		if (!order_issued && emp_possible) {
			order_issued = emp(special_unit);
		}
		
		if (!order_issued) {
			Position position = Positions::None;
			if (special_unit == science_vessel_for_worker_need_detection) {
				position = worker_need_detetection_position;
			}
			if (!position.isValid() && target_position.isValid()) {
				Unit closest_combat_unit = combat_unit_closest_to_position(target_position);
				if (closest_combat_unit != nullptr) position = closest_combat_unit->getPosition();
			}
			if (!position.isValid()) {
				Unit closest_combat_unit = combat_unit_closest_to_special_unit(special_unit);
				if (closest_combat_unit != nullptr) position = closest_combat_unit->getPosition();
			}
			if (!position.isValid()) {
				if (stage_position.isValid()) position = stage_position;
			}
			if (position.isValid()) {
				order_issued = unit_potential(special_unit, [this,position](UnitPotential& potential){
					potential.repel_units(all_enemy_units_);
					potential.repel_storms();
					potential.repel_emps();
					if (potential.empty()) {
						potential.add_potential(position, -0.1);
						for (auto& other_science_vessel : science_vessels_) {
							if (other_science_vessel != potential.unit()) {
								potential.add_potential(other_science_vessel, 0.1, 2 * WeaponTypes::EMP_Shockwave.outerSplashRadius());
							}
						}
					}
				});
			}
		}
		
		if (!order_issued) {
			if (!special_unit->isHoldingPosition()) special_unit->holdPosition();
		}
	}
}

void MicroManager::apply_high_templar_orders()
{
	std::pair<Unit,Unit> archon_warp;
	if (prevent_high_templar_from_archon_warp_count_ >= 0 &&
		high_templars_.size() >= (unsigned int)prevent_high_templar_from_archon_warp_count_ + 2) {
		std::vector<Unit> eligible_high_templars;
		for (auto& unit : high_templars_) {
			if (force_archon_warp_ || unit->getEnergy() < 50) eligible_high_templars.push_back(unit);
		}
		if (eligible_high_templars.size() >= 2) {
			int smallest_distance = INT_MAX;
			for (size_t i = 0; i < eligible_high_templars.size(); i++) {
				for (size_t j = i + 1; j < eligible_high_templars.size(); j++) {
					int distance = ground_distance(eligible_high_templars[i]->getPosition(), eligible_high_templars[j]->getPosition());
					if (distance <= 128 && distance < smallest_distance) {
						smallest_distance = distance;
						archon_warp.first = eligible_high_templars[i];
						archon_warp.second = eligible_high_templars[j];
					}
				}
			}
		}
	}
	std::set<Unit> storming_high_templars;
	for (auto& tentative_storm : tentative_storms_) storming_high_templars.insert(tentative_storm.unit);
	for (auto& special_unit : high_templars_) {
		CombatState& combat_state = combat_state_.at(special_unit);
		Position target_position = combat_state.target_position();
		Position stage_position = combat_state.stage_position();
		
		if (loading_units_.count(special_unit) > 0) continue;
		bool order_issued = false;
		bool storm_possible = (Broodwar->self()->hasResearched(TechTypes::Psionic_Storm) &&
							   special_unit->getEnergy() >= TechTypes::Psionic_Storm.energyCost() &&
							   special_unit->getSpellCooldown() == 0);
		
		if (storming_high_templars.count(special_unit) > 0) order_issued = true;
		
		if (!order_issued && special_unit == archon_warp.first) {
			if (!unit_has_target(archon_warp.first, archon_warp.second)) {
				archon_warp.first->useTech(TechTypes::Archon_Warp, archon_warp.second);
			}
			order_issued = true;
		}
		if (!order_issued && special_unit == archon_warp.second) {
			if (!unit_has_target(archon_warp.second, archon_warp.first)) {
				archon_warp.second->useTech(TechTypes::Archon_Warp, archon_warp.first);
			}
			order_issued = true;
		}
		
		if (!order_issued && storm_possible) {
			order_issued = storm(special_unit);
		}
		
		if (!order_issued && target_position == Positions::Unknown) {
			random_move(special_unit);
			order_issued = true;
		}
		
		bool enable_advance;
		bool enable_retreat;
		std::tie(enable_advance, enable_retreat) = determine_advance_retreat_for_special_unit(special_unit);
		enable_retreat = enable_retreat || !target_position.isValid();
		
		if (!order_issued && target_position.isValid() && storm_possible && enable_advance) {
			order_issued = unit_potential(special_unit, [this](UnitPotential& potential){
				potential.repel_storms();
			});
			
			if (!order_issued) {
				order_issued = path_finder.execute_path(special_unit, target_position, [target_position,special_unit]{
					unit_move(special_unit, target_position);
				});
			}
		}
		
		if (!order_issued) {
			bool moved = unit_potential(special_unit, [this](UnitPotential& potential){
				potential.repel_storms();
			});
			if (!moved) {
				if (stage_position.isValid() && enable_retreat) {
					move_retreat(special_unit, stage_position);
				} else {
					if (!special_unit->isHoldingPosition()) special_unit->holdPosition();
				}
			}
		}
	}
}

void MicroManager::apply_defiler_orders()
{
	std::set<Unit> busy_defilers;
	for (auto& tentative_dark_swarm : tentative_dark_swarms_) busy_defilers.insert(tentative_dark_swarm.unit);
	for (auto& tentative_plague : tentative_plagues_) busy_defilers.insert(tentative_plague.unit);
	for (auto& special_unit : defilers_) {
		CombatState& combat_state = combat_state_.at(special_unit);
		Position target_position = combat_state.target_position();
		Position stage_position = combat_state.stage_position();
		
		if (contains(loading_units_, special_unit)) continue;
		bool order_issued = false;
		
		if (contains(busy_defilers, special_unit)) order_issued = true;
		
		if (special_unit->getEnergy() < 150 &&
			Broodwar->self()->hasResearched(TechTypes::Consume) &&
			!special_unit->isIrradiated() &&
			training_manager.unit_count_completed(UnitTypes::Zerg_Zergling) >= 8) {
			Unit sacrifice_unit = nullptr;
			int smallest_distance = INT_MAX;
			for (auto& unit : Broodwar->self()->getUnits()) {
				if (unit->getType() == UnitTypes::Zerg_Zergling &&
					unit->isCompleted() &&
					!unit->isStasised()) {
					int distance = special_unit->getDistance(unit);
					if (distance < 64 && distance < smallest_distance) {
						sacrifice_unit = unit;
						smallest_distance = distance;
					}
				}
			}
			if (sacrifice_unit != nullptr) {
				special_unit->useTech(TechTypes::Consume, sacrifice_unit);
				order_issued = true;
			}
		}
		
		if (!order_issued &&
			special_unit->getEnergy() >= TechTypes::Dark_Swarm.energyCost() &&
			special_unit->getSpellCooldown() == 0) {
			order_issued = dark_swarm(special_unit);
		}
		
		if (!order_issued &&
			Broodwar->self()->hasResearched(TechTypes::Plague) &&
			special_unit->getEnergy() >= TechTypes::Plague.energyCost() &&
			special_unit->getSpellCooldown() == 0) {
			order_issued = plague(special_unit);
		}
		
		if (!order_issued && target_position == Positions::Unknown) {
			random_move(special_unit);
			order_issued = true;
		}
		
		bool enable_advance;
		bool enable_retreat;
		std::tie(enable_advance, enable_retreat) = determine_advance_retreat_for_special_unit(special_unit);
		enable_retreat = enable_retreat || !target_position.isValid();
		
		if (!order_issued && target_position.isValid() && enable_advance) {
			order_issued = unit_potential(special_unit, [this](UnitPotential& potential){
				potential.repel_storms();
			});
			
			if (!order_issued) {
				order_issued = path_finder.execute_path(special_unit, target_position, [target_position,special_unit]{
					unit_move(special_unit, target_position);
				});
			}
		}
		
		if (!order_issued) {
			bool moved = unit_potential(special_unit, [this](UnitPotential& potential){
				potential.repel_storms();
			});
			if (!moved) {
				if (stage_position.isValid() && enable_retreat) {
					move_retreat(special_unit, stage_position);
				} else {
					if (!special_unit->isHoldingPosition()) special_unit->holdPosition();
				}
			}
		}
	}
}

void MicroManager::apply_medic_orders()
{
	std::set<Unit> units_being_healed;
	for (auto& special_unit : medics_) {
		if (special_unit->getOrderTarget() != nullptr &&
			special_unit->getOrderTarget()->exists() &&
			special_unit->getOrderTarget()->getType().isOrganic()) {
			units_being_healed.insert(special_unit->getOrderTarget());
		} else if (special_unit->getLastCommand().getTarget() != nullptr &&
				   special_unit->getLastCommand().getTarget()->exists() &&
				   special_unit->getLastCommand().getTarget()->getType().isOrganic() &&
				   special_unit->getLastCommandFrame() >= Broodwar->getFrameCount() - Broodwar->getRemainingLatencyFrames()) {
			units_being_healed.insert(special_unit->getLastCommand().getTarget());
		}
	}
	for (auto& special_unit : medics_) {
		CombatState& combat_state = combat_state_.at(special_unit);
		Position target_position = combat_state.target_position();
		Position stage_position = combat_state.stage_position();
		
		if (loading_units_.count(special_unit) > 0) continue;
		bool order_issued = false;
		
		if (!order_issued && target_position == Positions::Unknown) {
			random_move(special_unit);
			order_issued = true;
		}
		
		if (!order_issued && special_unit->getEnergy() > 0) {
			key_value_vector<Unit,int> healable_unit_distances;
			for (Unit unit : Broodwar->self()->getUnits()) {
				if (unit->getType().isOrganic() &&
					unit->isCompleted() &&
					!unit->isStasised() &&
					unit->getHitPoints() < unit->getType().maxHitPoints()) {
					int distance = special_unit->getDistance(unit);
					if (distance < 128) {
						healable_unit_distances.emplace_back(unit, distance);
					}
				}
			}
			Unit heal_unit = key_with_smallest_value(healable_unit_distances);
			if (heal_unit != nullptr) {
				if (!unit_has_target(special_unit, heal_unit)) special_unit->useTech(TechTypes::Healing, heal_unit);
				order_issued = true;
			}
		}
		
		bool enable_advance;
		bool enable_retreat;
		std::tie(enable_advance, enable_retreat) = determine_advance_retreat_for_special_unit(special_unit);
		enable_retreat = enable_retreat || !target_position.isValid();
		
		if (!order_issued && target_position.isValid() && enable_advance) {
			order_issued = unit_potential(special_unit, [this](UnitPotential& potential){
				potential.repel_storms();
			});
			
			if (!order_issued) {
				order_issued = path_finder.execute_path(special_unit, target_position, [target_position,special_unit]{
					unit_move(special_unit, target_position);
				});
			}
		}
		
		if (!order_issued) {
			bool moved = unit_potential(special_unit, [this](UnitPotential& potential){
				potential.repel_storms();
			});
			if (!moved) {
				if (stage_position.isValid() && enable_retreat) {
					move_retreat(special_unit, stage_position);
				} else {
					if (!special_unit->isHoldingPosition()) special_unit->holdPosition();
				}
			}
		}
	}
}

void MicroManager::apply_dark_archon_orders()
{
	std::set<Unit> mind_controlling_dark_archons;
	for (auto& tentative_mind_control : tentative_mind_controls_) mind_controlling_dark_archons.insert(tentative_mind_control.unit);
	for (auto& special_unit : dark_archons_) {
		CombatState& combat_state = combat_state_.at(special_unit);
		Position target_position = combat_state.target_position();
		Position stage_position = combat_state.stage_position();
		
		bool order_issued = false;
		bool mind_control_possible = (Broodwar->self()->hasResearched(TechTypes::Mind_Control) &&
									  special_unit->getEnergy() >= TechTypes::Mind_Control.energyCost() &&
									  special_unit->getSpellCooldown() == 0);
		
		if (mind_controlling_dark_archons.count(special_unit) > 0) order_issued = true;
		
		Unit mind_control_target = nullptr;
		if (mind_control_possible) {
			std::vector<Unit> potential_targets;
			for (auto& unit : all_enemy_units_) {
				if (not_cloaked(unit) && !unit->isStasised() && special_unit->getDistance(unit) <= 14 * 32 && (unit->getType() == UnitTypes::Protoss_Carrier || unit->getType() == UnitTypes::Protoss_Arbiter)) {
					potential_targets.push_back(unit);
				}
			}
			if (!potential_targets.empty()) {
				mind_control_target = smallest_priority(potential_targets, [special_unit](auto& unit) {
					return special_unit->getDistance(unit);
				});
			}
		}
		
		if (!order_issued && mind_control_target != nullptr) {
			if (special_unit->getDistance(mind_control_target) <= WeaponTypes::Mind_Control.maxRange()) {
				special_unit->useTech(TechTypes::Mind_Control, mind_control_target);
				int latency_frames = Broodwar->getRemainingLatencyFrames() + 24;
				tentative_mind_controls_.push_back(TentativeMindControl(special_unit, Broodwar->getFrameCount() + latency_frames, mind_control_target));
				order_issued = true;
			} else {
				order_issued = path_finder.execute_path(special_unit, mind_control_target->getPosition(), [special_unit,mind_control_target]{
					unit_move(special_unit, mind_control_target->getPosition());
				});
			}
		}
		
		if (!order_issued && target_position == Positions::Unknown) {
			random_move(special_unit);
			order_issued = true;
		}
		
		bool enable_advance;
		bool enable_retreat;
		std::tie(enable_advance, enable_retreat) = determine_advance_retreat_for_special_unit(special_unit);
		enable_retreat = enable_retreat || !target_position.isValid();
		
		if (!order_issued && target_position.isValid() && mind_control_possible && enable_advance) {
			order_issued = unit_potential(special_unit, [this](UnitPotential& potential){
				potential.repel_storms();
			});
			
			if (!order_issued) {
				order_issued = path_finder.execute_path(special_unit, target_position, [target_position,special_unit]{
					unit_move(special_unit, target_position);
				});
			}
		}
		
		if (!order_issued) {
			bool moved = unit_potential(special_unit, [this](UnitPotential& potential){
				potential.repel_storms();
			});
			if (!moved) {
				if (stage_position.isValid() && enable_retreat) {
					move_retreat(special_unit, stage_position);
				} else {
					if (!special_unit->isHoldingPosition()) special_unit->holdPosition();
				}
			}
		}
	}
}

void MicroManager::apply_comsat_station_orders()
{
	std::set<Unit> casting_comsats;
	for (auto& tentative_scan : tentative_scans_) casting_comsats.insert(tentative_scan.unit);
	for (auto& comsat_unit : comsat_stations_) {
		if (casting_comsats.count(comsat_unit) > 0) continue;
		bool order_issued = false;
		if (!order_issued && comsat_unit->getEnergy() >= TechTypes::Scanner_Sweep.energyCost()) {
			order_issued = scan_cloaked_unit(comsat_unit);
		}
		if (!order_issued &&
			(comsat_unit->getEnergy() >= 3 * TechTypes::Scanner_Sweep.energyCost() ||
			 (training_manager.unit_count_completed(UnitTypes::Terran_Science_Vessel) >= 1 &&
			  comsat_unit->getEnergy() >= 2 * TechTypes::Scanner_Sweep.energyCost()))) {
			 order_issued = scan_base(comsat_unit);
		 }
	}
}

void MicroManager::apply_flying_building_orders()
{
	const auto determine_repairing_unit = [](Unit building_unit){
		Unit result = nullptr;
		for (auto& entry : worker_manager.worker_map()) {
			const Worker& worker = entry.second;
			if (worker.order()->repair_target() == building_unit) {
				result = entry.first;
				break;
			}
		}
		return result;
	};
	
	const auto determine_closest_combat_unit = [this](Position position){
		key_value_vector<Unit,int> distances;
		for (Unit combat_unit : extended_combat_units_) {
			if (!combat_unit->isFlying()) {
				distances.emplace_back(combat_unit, combat_unit->getDistance(position));
			}
		}
		return key_with_smallest_value(distances);
	};
	
	for (auto& unit : flying_buildings_) {
		if (spot_with_barracks_and_engineering_bay_ &&
			(unit->getType() == UnitTypes::Terran_Barracks || unit->getType() == UnitTypes::Terran_Engineering_Bay)) {
			Unit repairing_unit = determine_repairing_unit(unit);
			if (unit->getHitPoints() <= unit->getType().maxHitPoints() / 3 ||
				repairing_unit != nullptr) {
				Position position = Positions::None;
				const BWEM::Base* main_base = base_state.main_base();
				if (main_base != nullptr) {
					position = main_base->Center();
				} else {
					position = center_position_for(UnitTypes::Terran_Command_Center, Broodwar->self()->getStartLocation());
				}
				if (repairing_unit != nullptr) {
					if (repairing_unit->getPosition().getApproxDistance(unit->getPosition()) > 32) {
						unit_move(unit, repairing_unit->getPosition());
					} else {
						if (!unit->isIdle()) unit->stop();
					}
				} else if (unit->getPosition().getApproxDistance(position) > 32) {
					unit_move(unit, position);
				} else {
					if (!unit->isIdle()) unit->stop();
				}
			} else {
				bool order_issued = false;
				Unit combat_unit = determine_closest_combat_unit(unit->getPosition());
				if (combat_unit != nullptr) {
					CombatState& combat_state = combat_state_.at(combat_unit);
					Position target_position = combat_state.target_position();
					Position stage_position = combat_state.stage_position();
					Position position = Positions::None;
					if (target_position.isValid()) {
						Unit closest_combat_unit = combat_unit_closest_to_position(target_position);
						if (closest_combat_unit != nullptr) {
							position = determine_spot_position(closest_combat_unit->getPosition(), target_position);
						}
					}
					if (!position.isValid() && stage_position.isValid()) {
						position = stage_position;
						if (tactics_manager.enemy_start_base() != nullptr) {
							position = determine_spot_position(stage_position, tactics_manager.enemy_start_base()->Center());
						}
					}
					if (position.isValid()) {
						order_issued = unit_potential(unit, [this,position](UnitPotential& potential){
							potential.repel_units(all_enemy_units_);
							potential.repel_storms();
							potential.repel_emps();
							if (potential.empty()) {
								potential.add_potential(position, -0.1);
								for (auto& other_unit : flying_buildings_) {
									if (other_unit != potential.unit()) {
										potential.add_potential(other_unit, 0.1, 128);
									}
								}
							}
						});
					}
				}
				if (!order_issued && !unit->isIdle()) unit->stop();
			}
		} else {
			if (unit->getOrder() != Orders::BuildingLand) {
				TilePosition tile_position;
				if (unit->getType().isResourceDepot() &&
					!base_state.next_available_bases().empty()) {
					tile_position = base_state.next_available_bases()[0]->Location();
				} else {
					tile_position = building_placement_manager.place_building(unit->getType());
				}
				if (tile_position.isValid()) {
					unit->land(tile_position);
				}
			}
		}
	}
}

bool MicroManager::carrier_interceptors_attacking(Unit carrier_unit)
{
	bool order_seen = false;
	int visible_count = 0;
	for (auto interceptor_unit : carrier_unit->getInterceptors()) {
		if (interceptor_unit->isCompleted() &&
			interceptor_unit->isVisible()) {
			visible_count++;
			if (interceptor_unit->getOrderTarget() != nullptr) {
				order_seen = true;
			}
		}
	}
	return order_seen && visible_count == carrier_unit->getInterceptorCount();
}

Unit MicroManager::determine_carrier_target(Unit carrier_unit)
{
	for (auto interceptor_unit : carrier_unit->getInterceptors()) {
		if (interceptor_unit->isCompleted() &&
			interceptor_unit->isVisible() &&
			interceptor_unit->getOrderTarget() != nullptr) {
			return interceptor_unit->getOrderTarget();
		}
	}
	return nullptr;
}

bool MicroManager::should_carrier_retarget(Unit carrier_unit,Unit other_target_unit)
{
	Unit target_unit = determine_carrier_target(carrier_unit);
	return ((!can_attack(target_unit, carrier_unit) && can_attack(other_target_unit, carrier_unit)) ||
			(!can_attack_in_range_with_prediction(target_unit, carrier_unit) && can_attack_in_range_with_prediction(other_target_unit, carrier_unit)));
}

Position MicroManager::determine_carrier_leash_position(Unit carrier_unit)
{
	Unit target_unit = determine_carrier_target(carrier_unit);
	int target_ground_height = Broodwar->getGroundHeight(TilePosition(target_unit->getPosition()));
	FastPosition target_position = predict_position(target_unit);
	FastPosition delta = target_position - carrier_unit->getPosition();
	FastPosition reverse_position = scale_line_segment(carrier_unit->getPosition(), carrier_unit->getPosition() - delta, 512);
	FastTilePosition carrier_tile_position = FastTilePosition(carrier_unit->getPosition());
	
	key_value_vector<FastPosition,std::pair<int,int>> distances;
	
	for (int i = -60; i <= 60; i += 10) {
		double angle = (i / 180.0) * M_PI;
		FastPosition position = rotate_around(carrier_unit->getPosition(), reverse_position, angle);
		position.makeValid();
		
		const auto& grid = [target_ground_height](FastTilePosition tile_position){
			return Broodwar->getGroundHeight(tile_position) > target_ground_height;
		};
		FastTilePosition tile_position = line_search(carrier_tile_position, FastTilePosition(position), grid);
		if (tile_position.isValid()) {
			int distance = carrier_unit->getPosition().getApproxDistance(center_position(tile_position));
			distances.emplace_back(position, std::make_pair(distance, std::abs(i)));
		}
	}
	
	if (distances.empty()) {
		for (int i = -60; i <= 60; i += 10) {
			double angle = (i / 180.0) * M_PI;
			FastPosition position = rotate_around(carrier_unit->getPosition(), reverse_position, angle);
			position.makeValid();
			
			const auto& grid = [](FastTilePosition tile_position){
				return !walkability_grid.is_terrain_walkable(tile_position);
			};
			FastTilePosition tile_position = line_search(carrier_tile_position, FastTilePosition(position), grid);
			if (tile_position.isValid()) {
				int distance = carrier_unit->getPosition().getApproxDistance(center_position(tile_position));
				distances.emplace_back(position, std::make_pair(distance, std::abs(i)));
			}
		}
	}
	
	FastPosition best_position = key_with_smallest_value(distances, reverse_position.makeValid());
	
	int l = 0;
	int h = carrier_unit->getPosition().getApproxDistance(best_position);
	while (l + 1 < h) {
		int m = (l + h) / 2;
		FastPosition position = scale_line_segment(carrier_unit->getPosition(), best_position, m);
		if (calculate_distance(UnitTypes::Protoss_Carrier, position, target_unit->getType(), target_unit->getPosition()) <= kCarrierLeashRange - 8) {
			l = m;
		} else {
			h = m;
		}
	}
	return scale_line_segment(carrier_unit->getPosition(), best_position, l);
}

Position MicroManager::determine_spot_position(Position start_position,Position target_position)
{
	Position result = start_position;
	int distance = -1;
	const BWEM::CPPath& path = bwem_map.GetPath(start_position, target_position, &distance);
	if (distance >= 0 &&
		has_area(FastWalkPosition(start_position)) &&
		has_area(FastWalkPosition(target_position))) {
		bool found = false;
		for (auto& choke : path) {
			Position choke_position = chokepoint_center(choke);
			if (start_position.getApproxDistance(choke_position) > 128) {
				result = scale_line_segment(start_position, choke_position, 128);
				found = true;
				break;
			}
		}
		if (!found) {
			if (start_position.getApproxDistance(target_position) <= 128) {
				result = target_position;
			} else {
				result = scale_line_segment(start_position, target_position, 128);
			}
		}
	}
	return result;
};

void MicroManager::expire_tentative_abilities()
{
	std::set<Position> storm_positions;
	for (auto& bullet : Broodwar->getBullets()) if (bullet->getType() == BulletTypes::Psionic_Storm) storm_positions.insert(bullet->getPosition());
	remove_elements_in_place(tentative_storms_, [&storm_positions](const TentativeStorm& tentative_storm){
		return tentative_storm.expire_frame < Broodwar->getFrameCount() || storm_positions.count(tentative_storm.position) > 0;
	});
	remove_elements_in_place(tentative_mind_controls_, [](const TentativeMindControl& tentative_mind_control){
		return tentative_mind_control.expire_frame < Broodwar->getFrameCount() || !tentative_mind_control.target->exists() || tentative_mind_control.target->getPlayer() == Broodwar->self();
	});
	remove_elements_in_place(tentative_stasises_, [](const TentativeStasis& tentative_stasis){
		return tentative_stasis.expire_frame < Broodwar->getFrameCount();
	});
	remove_elements_in_place(tentative_dark_swarms_, [](const TentativeDarkSwarm& tentative_dark_swarm){
		return tentative_dark_swarm.expire_frame < Broodwar->getFrameCount();
	});
	remove_elements_in_place(tentative_plagues_, [](const TentativePlague& tentative_plague){
		return tentative_plague.expire_frame < Broodwar->getFrameCount();
	});
	remove_elements_in_place(tentative_irradiates_, [](const TentativeIrradiate& tentative_irradiate){
		return tentative_irradiate.expire_frame < Broodwar->getFrameCount();
	});
	remove_elements_in_place(tentative_emps_, [](const TentativeEMP& tentative_emp){
		return tentative_emp.expire_frame < Broodwar->getFrameCount();
	});
	remove_elements_in_place(tentative_mines_, [](const TentativeMine& tentative_mine){
		return tentative_mine.expire_frame < Broodwar->getFrameCount();
	});
	remove_elements_in_place(tentative_yamatoes_, [](const TentativeYamato& tentative_yamato){
		return tentative_yamato.expire_frame < Broodwar->getFrameCount();
	});
	remove_elements_in_place(tentative_lockdowns_, [](const TentativeLockdown& tentative_lockdown){
		return tentative_lockdown.expire_frame < Broodwar->getFrameCount();
	});
	remove_elements_in_place(tentative_scans_, [](const TentativeScan& tentative_scan){
		return tentative_scan.expire_frame < Broodwar->getFrameCount();
	});
}

void MicroManager::set_nearby_units_for_kiting(std::vector<Unit>& nearby_enemy_units,std::vector<Unit>& nearby_friendly_ground,Unit combat_unit)
{
	nearby_enemy_units.clear();
	for (Unit enemy_unit : all_enemy_units_) {
		if (enemy_unit->isCompleted() && !is_disabled(enemy_unit) && enemy_unit->isPowered()) {
			int max_range = weapon_max_range(enemy_unit, combat_unit->isFlying());
			if (max_range >= 0) {
				int attack_distance = weapon_max_range(combat_unit, enemy_unit->isFlying());
				int distance = combat_unit->getDistance(enemy_unit);
				if (distance <= 128 + std::max(max_range, attack_distance)) {
					nearby_enemy_units.push_back(enemy_unit);
				}
			}
		}
	}
	nearby_friendly_ground.clear();
	for (auto& information_unit : information_manager.my_units()) {
		if (!information_unit->flying) {
			int distance = information_unit->unit->getDistance(combat_unit);
			if (distance < 128) {
				nearby_friendly_ground.push_back(information_unit->unit);
			}
		}
	}
}

std::vector<std::pair<std::vector<Unit>,FastPosition>> MicroManager::group_mutalisks(const std::vector<Unit>& units)
{
	std::vector<std::pair<std::vector<Unit>,FastPosition>> result;
	std::set<Unit> unassigned(units.begin(), units.end());
	
	while (!unassigned.empty()) {
		auto it = unassigned.begin();
		Unit first = *it;
		unassigned.erase(it);
		
		std::vector<Unit> cluster_units;
		std::set<Unit> todo;
		todo.insert(first);
		
		while (!todo.empty()) {
			auto it = todo.begin();
			Unit current = *it;
			todo.erase(it);
			unassigned.erase(current);
			for (Unit candidate : unassigned) {
				if (todo.count(candidate) == 0 &&
					current->getDistance(candidate) <= 32) {
					todo.insert(candidate);
				}
			}
			cluster_units.push_back(current);
		}
		
		FastPosition center(0, 0);
		for (auto& combat_unit : cluster_units) {
			center += combat_unit->getPosition();
		}
		int count = int(cluster_units.size());
		center.x /= count;
		center.y /= count;
		
		std::sort(cluster_units.begin(), cluster_units.end(), [center](const auto& a,const auto& b){ return a->getDistance(center) < b->getDistance(center); });
		result.emplace_back(cluster_units, center);
	}
	
	return result;
}

std::map<Unit,int> MicroManager::determine_units_per_group(const std::vector<Unit>& units)
{
	std::map<Unit,int> result;
	std::set<Unit> unassigned(units.begin(), units.end());
	
	while (!unassigned.empty()) {
		auto it = unassigned.begin();
		Unit first = *it;
		unassigned.erase(it);
		
		std::vector<Unit> cluster_units;
		std::set<Unit> todo;
		todo.insert(first);
		
		while (!todo.empty()) {
			auto it = todo.begin();
			Unit current = *it;
			todo.erase(it);
			unassigned.erase(current);
			for (Unit candidate : unassigned) {
				if (todo.count(candidate) == 0 &&
					current->getDistance(candidate) <= 32) {
					todo.insert(candidate);
				}
			}
			cluster_units.push_back(current);
		}
		
		for (Unit unit : cluster_units) {
			result[unit] = int(cluster_units.size());
		}
	}
	
	return result;
}

bool MicroManager::is_siege_allowed(Unit combat_unit)
{
	for (auto cluster : tactics_manager.clusters()) {
		if (cluster.in_front_with_supply_at_least(combat_unit, 6 * UnitTypes::Terran_Siege_Tank_Tank_Mode.supplyRequired())) {
			return true;
		}
	}
	for (auto& information_unit : information_manager.enemy_units()) {
		if (information_unit->position.isValid()) {
			if (information_unit->flying) {
				if (can_attack_in_range_at_positions(information_unit->type, information_unit->position, information_unit->player, combat_unit->getType(), combat_unit->getPosition())) {
					return false;
				}
			} else if (can_attack(information_unit->type, false) &&
					   connectivity_grid.check_reachability_ranged(connectivity_grid.component_for_position(information_unit->position),
																   weapon_max_range(information_unit->type, information_unit->player, false),
																   combat_unit)) {
				int distance = calculate_distance(combat_unit->getType(), combat_unit->getPosition(), information_unit->type, information_unit->position);
				if (distance < UnitTypes::Terran_Siege_Tank_Siege_Mode.groundWeapon().minRange()) {
					return false;
				}
				if (!information_unit->type.isWorker() &&
					!(information_unit->type == UnitTypes::Zerg_Lurker && information_unit->burrowed)) {
					int range = UnitTypes::Terran_Siege_Tank_Siege_Mode.groundWeapon().minRange() + max_unit_dimension(combat_unit->getType()) + max_unit_dimension(information_unit->type);
					int distance = ground_distance_to_bwcircle(information_unit->position, range, combat_unit->getPosition());
					double speed = top_speed(information_unit->type, information_unit->player);
					distance = distance - int(speed * kSiegeFrames + 0.5);
					if (distance < 0) {
						return false;
					}
				}
			}
		}
	}
	return true;
}

bool MicroManager::allow_overlord_wait_in_base()
{
	bool result = true;
	
	if (opponent_model.enemy_race() == Races::Terran ||
		information_manager.enemy_seen(UnitTypes::Protoss_Cybernetics_Core) ||
		information_manager.enemy_seen(UnitTypes::Protoss_Dragoon) ||
		information_manager.enemy_seen(UnitTypes::Protoss_Stargate) ||
		information_manager.enemy_seen(UnitTypes::Protoss_Corsair) ||
		information_manager.enemy_seen(UnitTypes::Protoss_Scout) ||
		information_manager.enemy_seen(UnitTypes::Protoss_Photon_Cannon) ||
		information_manager.enemy_seen(UnitTypes::Zerg_Spire) ||
		information_manager.enemy_seen(UnitTypes::Zerg_Mutalisk) ||
		information_manager.enemy_seen(UnitTypes::Zerg_Hydralisk_Den) ||
		information_manager.enemy_seen(UnitTypes::Zerg_Hydralisk)) {
		result = false;
	}
	
	return result;
}

void MicroManager::order_overlord_wait_in_base(const BWEM::Base* base)
{
	if (base == nullptr) return;
	
	for (auto& entry : overlord_state_) {
		if (entry.second.command == OverlordCommand::WaitInBase && entry.second.base == base) return;
	}
	
	Position position = center_position_for(Broodwar->self()->getRace().getResourceDepot(), base->Location());
	key_value_vector<Unit,int> overlord_distances;
	for (auto& entry : overlord_state_) {
		if (entry.second.command == OverlordCommand::Default) {
			Unit overlord_unit = entry.first;
			overlord_distances.emplace_back(overlord_unit, overlord_unit->getDistance(position));
		}
	}
	Unit overlord_unit = key_with_smallest_value(overlord_distances);
	if (overlord_unit != nullptr) {
		OverlordState& state = overlord_state_[overlord_unit];
		state.command = OverlordCommand::WaitInBase;
		state.base = base;
	}
}

void MicroManager::order_overlord_detect()
{
	for (Unit special_unit : overlords_) {
		OverlordState& state = overlord_state_[special_unit];
		if (state.command == OverlordCommand::Detect) return;
	}
	
	for (Unit special_unit : overlords_) {
		OverlordState& state = overlord_state_[special_unit];
		if (state.command == OverlordCommand::Default) {
			state.command = OverlordCommand::Detect;
			return;
		}
	}
}

void MicroManager::order_overlord_worker_need_detection()
{
	Position position = determine_worker_need_detection_position();
	if (position.isValid()) {
		for (Unit special_unit : overlords_) {
			OverlordState& state = overlord_state_[special_unit];
			if (state.command == OverlordCommand::WorkerNeedsDetection &&
				state.position == position) {
				return;
			}
		}
		for (Unit special_unit : overlords_) {
			OverlordState& state = overlord_state_[special_unit];
			if (state.command == OverlordCommand::Default) {
				state.command = OverlordCommand::WorkerNeedsDetection;
				state.position = position;
				return;
			}
		}
	}
}

void MicroManager::order_observer_worker_need_detection()
{
	Position position = determine_worker_need_detection_position();
	if (position.isValid() && !observers_.empty()) {
		bool already_assigned = std::any_of(observers_.begin(), observers_.end(), [this,position](auto& unit){
			ObserverTarget& target = observer_targets_[unit];
			return target.scouting && target.position == position;
		});
		if (!already_assigned) {
			Unit closest_observer = smallest_priority(observers_, [this,position](auto& unit) {
				ObserverTarget& target = observer_targets_[unit];
				return std::make_tuple(!target.scouting, unit->getDistance(position));
			});
			ObserverTarget& target = observer_targets_[closest_observer];
			target.scouting = true;
			target.position = position;
		}
	}
}

Position MicroManager::determine_worker_need_detection_position()
{
	Position position = Positions::None;
	for (auto& entry : worker_manager.worker_map()) {
		const WorkerOrder* worker_order = entry.second.order();
		if (worker_order->need_detection()) {
			position = center_position_for(worker_order->building_type(), worker_order->building_position());
			break;
		}
	}
	return position;
}

int MicroManager::distance_to_base(const BWEM::Base *base,const InformationUnit* enemy_unit)
{
	int distance = INT_MAX;
	if (base != nullptr) {
		UnitType center_type = Broodwar->self()->getRace().getResourceDepot();
		distance = calculate_distance(enemy_unit->type, enemy_unit->position, center_type, base->Center());
	}
	return distance;
}

int MicroManager::distance_to_proxy(const InformationUnit* enemy_unit)
{
	int distance = INT_MAX;
	if (building_placement_manager.proxy_pylon_position().isValid()) {
		distance = calculate_distance(enemy_unit->type,
									  enemy_unit->position,
									  UnitTypes::Protoss_Pylon,
									  center_position_for(UnitTypes::Protoss_Pylon, building_placement_manager.proxy_pylon_position()));
	} else if (building_placement_manager.proxy_barracks_position().isValid()) {
		distance = calculate_distance(enemy_unit->type,
									  enemy_unit->position,
									  UnitTypes::Terran_Barracks,
									  center_position_for(UnitTypes::Terran_Barracks, building_placement_manager.proxy_barracks_position()));
		if (building_placement_manager.proxy_second_barracks_position().isValid()) {
			int other_distance = calculate_distance(enemy_unit->type,
													enemy_unit->position,
													UnitTypes::Terran_Barracks,
													center_position_for(UnitTypes::Terran_Barracks, building_placement_manager.proxy_second_barracks_position()));
			distance = std::min(distance, other_distance);
		}
	}
	return distance;
}

Position MicroManager::calculate_interception_position(Unit combat_unit,Unit enemy_unit)
{
	Position result = enemy_unit->getPosition();
 
	if (!can_attack_in_range_with_prediction(combat_unit, enemy_unit)) {
		Position predicted_position = predict_position(enemy_unit, 8);
		Position delta_current = enemy_unit->getPosition() - combat_unit->getPosition();
		Position delta_predict = predicted_position - combat_unit->getPosition();
		int inner_product = delta_current.x * delta_predict.x + delta_current.y * delta_predict.y;
		if (inner_product > 0 && check_collision(enemy_unit, predicted_position)) result = predicted_position;
	}
	
	return result;
}

bool MicroManager::unit_in_safe_location(Unit unit)
{
	const BWEM::Area* area = area_at(unit->getPosition());
	if (area != nullptr && base_state.controlled_areas().count(area) > 0) return true;
	
	for (auto& cp : base_state.border().chokepoints()) {
		Position position = chokepoint_center(cp);
		if (unit->getDistance(position) <= 320) return true;
	}
	
	return false;
}

void MicroManager::perform_drop(UnitType unit_type)
{
	TransportState* state = find_transport_without_command();
	if (state != nullptr) {
		state->command = TransportCommand::LoadForDropInEnemyBase;
		state->unit_type = unit_type;
	}
}

void MicroManager::perform_bulldog_zealot_drop()
{
	TransportState* state = find_transport_without_command();
	if (state != nullptr) state->command = TransportCommand::BulldogLoadZealots;
}

TransportState* MicroManager::find_transport_without_command()
{
	for (Unit unit : transports_) {
		TransportState& state = transport_state_[unit];
		if (state.command == TransportCommand::Default) {
			return &state;
		}
	}
	return nullptr;
}

Position MicroManager::determine_first_detector_location(Unit special_unit)
{
	CombatState& combat_state = combat_state_.at(special_unit);
	Position target_position = combat_state.target_position();
	Position stage_position = combat_state.stage_position();
	
	Position result = Positions::None;
	
	struct Target {
		int base_distance;
		int distance;
		Position position = Positions::None;
	};
	
	std::vector<Target> targets;
	if (stage_position.isValid() &&
		!building_manager.building_exists(UnitTypes::Protoss_Photon_Cannon) &&
		Broodwar->self()->getRace() != Races::Zerg) {
		bool unknown = false;
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->type == UnitTypes::Zerg_Lurker ||
				enemy_unit->type == UnitTypes::Protoss_Dark_Templar ||
				(enemy_unit->unit->exists() && !enemy_unit->type.hasPermanentCloak() && enemy_unit->unit->isCloaked())) {
				if (enemy_unit->is_current() ||
					(enemy_unit->type == UnitTypes::Zerg_Lurker &&
					 enemy_unit->position.isValid() &&
					 enemy_unit->burrowed)) {
					int distance = enemy_unit->position.getApproxDistance(special_unit->getPosition());
					targets.emplace_back(Target{ enemy_unit->base_distance, distance, enemy_unit->position });
				} else {
					unknown = true;
				}
			}
		}
		if (unknown) {
			targets.emplace_back(Target{ 1, INT_MIN, stage_position });
		}
	} else {
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->position.isValid() &&
				(enemy_unit->type == UnitTypes::Zerg_Lurker ||
				 enemy_unit->type == UnitTypes::Protoss_Dark_Templar ||
				 (enemy_unit->unit->exists() && !enemy_unit->type.hasPermanentCloak() && enemy_unit->unit->isCloaked()))) {
				int distance = enemy_unit->position.getApproxDistance(special_unit->getPosition());
				targets.emplace_back(Target{ enemy_unit->base_distance, distance, enemy_unit->position });
			}
		}
	}
	if (target_position.isValid()) {
		Unit closest_combat_unit = combat_unit_closest_to_position(target_position);
		if (closest_combat_unit != nullptr) {
			int base_distance = information_manager.all_units().at(closest_combat_unit).base_distance;
			int distance = closest_combat_unit->getPosition().getApproxDistance(special_unit->getPosition());
			targets.emplace_back(Target{ base_distance, distance, closest_combat_unit->getPosition() });
		}
	}
	Target target = smallest_priority(targets, [](auto& target){
		return std::make_tuple(target.base_distance, target.distance);
	});
	if (target.position.isValid()) {
		result = target.position;
	}
	
	if (!result.isValid()) {
		Unit closest_combat_unit = combat_unit_closest_to_special_unit(special_unit);
		if (closest_combat_unit != nullptr) result = closest_combat_unit->getPosition();
	}
	
	if (!result.isValid()) {
		if (stage_position.isValid()) {
			result = stage_position;
		}
	}
	
	return result;
}

Position MicroManager::closest_sieged_tank(Position position)
{
	std::vector<Position> tank_positions;
	
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->type == UnitTypes::Terran_Siege_Tank_Siege_Mode &&
			!enemy_unit->is_disabled() &&
			!melee_unit_near_sieged_tank(enemy_unit->position) &&
			enemy_unit->position.getApproxDistance(position) <= 500) {
			tank_positions.push_back(enemy_unit->position);
		}
	}
	
	if (tank_positions.empty()) {
		return Positions::None;
	} else {
		return *std::min_element(tank_positions.begin(), tank_positions.end(), [position](Position a,Position b){
			return a.getApproxDistance(position) < b.getApproxDistance(position);
		});
	}
}

bool MicroManager::melee_unit_near_sieged_tank(Position tank_position)
{
	for (auto& combat_unit : combat_units_) {
		if (is_melee(combat_unit->getType()) &&
			calculate_distance(UnitTypes::Terran_Siege_Tank_Siege_Mode, tank_position, combat_unit->getType(), combat_unit->getPosition()) <= 15) return true;
	}
	return false;
}

bool MicroManager::melee_unit_near_sieged_tank(Unit unit)
{
	if (is_melee(unit->getType())) {
		for (auto& siege_unit : all_enemy_units_) {
			if (siege_unit->getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode &&
				!siege_unit->isStasised() &&
				siege_unit->getDistance(unit) < WeaponTypes::Arclite_Shock_Cannon.minRange()) {
				return true;
			}
		}
	}
	return false;
}

bool MicroManager::load_closest_unit_of_type(Unit transport_unit,UnitType load_type,bool ignore_distance)
{
	bool order_issued = false;
	
	if (load_type != UnitTypes::None && transport_unit->getSpaceRemaining() >= load_type.spaceRequired()) {
		std::vector<Unit> candidate_load_targets;
		for (Unit unit : Broodwar->self()->getUnits()) {
			if (unit->getType() == load_type &&
				unit->isCompleted() && !is_disabled(unit) && !unit->isLoaded() &&
				(ignore_distance || transport_unit->getDistance(unit) <= 500) &&
				!melee_unit_near_sieged_tank(unit)) candidate_load_targets.push_back(unit);
		}
		if (!candidate_load_targets.empty()) {
			Unit load_unit = *std::min_element(candidate_load_targets.begin(), candidate_load_targets.end(), [transport_unit](Unit a,Unit b) {
				return std::make_tuple(transport_unit->getDistance(a), a->getID()) < std::make_tuple(transport_unit->getDistance(b), b->getID());
			});
			load_unit_into_transport(transport_unit, load_unit);
			order_issued = true;
		}
	}
	
	return order_issued;
}

void MicroManager::load_unit_into_transport(Unit transport_unit,Unit load_unit)
{
	if (!unit_has_target(transport_unit, load_unit)) transport_unit->load(load_unit);
	if (!unit_has_target(load_unit, transport_unit)) load_unit->follow(transport_unit);
	loading_units_.insert(load_unit);
}

bool MicroManager::reaver_in_shuttle_can_attack(Unit shuttle_or_reaver_unit)
{
	std::vector<Unit> attackable_enemy_units;
	for (Unit enemy_unit : harassable_enemy_units_) {
		if (can_attack_in_range_at_positions(UnitTypes::Protoss_Reaver, predict_position(shuttle_or_reaver_unit), shuttle_or_reaver_unit->getPlayer(),
											 enemy_unit->getType(), predict_position(enemy_unit))) {
			int distance = ground_distance(shuttle_or_reaver_unit->getPosition(), enemy_unit->getPosition());
			if (distance >= 0 &&
				distance <= 256 + max_unit_dimension(UnitTypes::Protoss_Reaver) + max_unit_dimension(enemy_unit->getType())) {
				attackable_enemy_units.push_back(enemy_unit);
			}
		}
	}
	if (attackable_enemy_units.empty()) return false;
	
	FastPosition initial_position = shuttle_or_reaver_unit->getPosition();
	std::queue<FastPosition> queue;
	SparsePositionGrid<256,8,int> distances(initial_position);
	queue.push(initial_position);
	distances[initial_position] = 1;
	
	while (!queue.empty() && !attackable_enemy_units.empty()) {
		FastPosition current_position = queue.front();
		queue.pop();
		
		int current_distance = distances[current_position];
		for (auto enemy_unit : attackable_enemy_units) {
			int scarab_distance = calculate_distance(enemy_unit->getType(), enemy_unit->getPosition(), current_position);
			if (scarab_distance <= WeaponTypes::Scarab.innerSplashRadius()) {
				return true;
			}
		}
		
		for (FastPosition delta_position : { FastPosition(-8, 0), FastPosition(8, 0), FastPosition(0, -8), FastPosition(0, 8) }) {
			 FastPosition next_position = current_position + delta_position;
			if (next_position.isValid() && next_position.getApproxDistance(initial_position) <= 256) {
				int& distance = distances[next_position];
				if (distance == 0) {
					if (check_collision(shuttle_or_reaver_unit, UnitTypes::Protoss_Scarab, next_position)) {
						queue.push(next_position);
						distance = current_distance + 8;
					} else {
						distance = -1;
					}
				}
			}
		}
	}
	
	return false;
}

std::set<Unit> MicroManager::determine_unpaired_reavers()
{
	std::set<Unit> unpaired_reavers;
	for (auto& combat_unit : combat_units_) {
		if (combat_unit->getType() == UnitTypes::Protoss_Reaver) unpaired_reavers.insert(combat_unit);
	}
	for (auto& transport_unit : transports_) {
		TransportState& state = transport_state_[transport_unit];
		if (state.command == TransportCommand::ReaverMicro) unpaired_reavers.erase(state.reaver_unit);
	}
	return unpaired_reavers;
}

Position MicroManager::pick_air_scout_location()
{
	int resource_depot_count = 0;
	for (auto& enemy_unit : information_manager.enemy_units()) if (enemy_unit->type.isResourceDepot()) resource_depot_count++;
	int free_base_count = 0;
	for (auto& base : base_state.bases()) if (base_state.controlled_bases().count(base) == 0 && base_state.opponent_bases().count(base) == 0) free_base_count++;
	
	double sum = 0.0;
	std::vector<std::pair<Position,double>> distribution;
	for (auto enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->type.isResourceDepot()) {
			sum += 19.0 / resource_depot_count;
			distribution.emplace_back(center_position_for(enemy_unit->type, enemy_unit->tile_position()), sum);
		}
	}
	if (resource_depot_count == 0) {
		Position position = tactics_manager.enemy_start_position();
		if (position.isValid()) {
			sum += 19.0;
			distribution.emplace_back(position, sum);
		}
	}
	for (auto& base : base_state.bases()) {
		if (base_state.controlled_bases().count(base) == 0 && base_state.opponent_bases().count(base) == 0) {
			sum += 1.0 / free_base_count;
			distribution.emplace_back(center_position_for(UnitTypes::Protoss_Nexus, base->Location()), sum);
		}
	}
	
	if (distribution.empty()) return Positions::None;
	if (distribution.size() == 1) return distribution[0].first;
	
	std::uniform_real_distribution<double> dist(0.0, sum);
	double r = dist(random_generator());
	for (size_t i = 0; i < distribution.size() - 1; i++) {
		if (r < distribution[i].second) return distribution[i].first;
	}
	return distribution[distribution.size() - 1].first;
}

bool MicroManager::move_flyer_near_safe(Unit unit,Position position)
{
	bool order_issued = false;
	
	auto& threat_component_grid = threat_grid.component_grid(unit->getType());
	int start_component = threat_component_grid.component(unit->getPosition());
	if (start_component == 0) {
		order_issued = unit_potential(unit, [this](UnitPotential& potential){
			potential.repel_units(all_enemy_units_, 32);
			potential.repel_storms();
		});
	}
	
	if (!order_issued) {
		int component = threat_component_grid.component(position);
		if (component == 0) {
			FastPosition best_position = find_closest_safe_position_near_target(unit, position);
			if (best_position.isValid() &&
				unit->getPosition().getApproxDistance(position) > best_position.getApproxDistance(position) + 64) {
				order_issued = move_safe(unit, best_position);
			} else {
				order_issued = unit_potential(unit, [this,position](UnitPotential& potential){
					potential.repel_units(all_enemy_units_);
					potential.repel_storms();
					if (potential.empty()) potential.add_potential(position, -0.1);
				});
			}
		} else {
			order_issued = move_safe(unit, position);
		}
	}
	
	return order_issued;
}

bool MicroManager::move_flyer_near_safe_approach_unsafe(Unit unit,Position position)
{
	bool order_issued = false;
	
	if (!order_issued) {
		auto& threat_component_grid = threat_grid.component_grid(unit->getType());
		int component = threat_component_grid.component(position);
		if (component == 0) {
			FastPosition best_position = find_closest_safe_position_near_target(unit, position);
			if (best_position.isValid() &&
				unit->getPosition().getApproxDistance(position) > best_position.getApproxDistance(position) + 64) {
				order_issued = unit_potential(unit, [this](UnitPotential& potential){
					potential.repel_units(all_enemy_units_, 32);
					potential.repel_storms();
				});
				if (!order_issued) {
					order_issued = move_safe(unit, best_position);
				}
			} else if (unit->getPosition() != position) {
				unit_move(unit, position);
				order_issued = true;
			}
		} else {
			int start_component = threat_component_grid.component(unit->getPosition());
			if (start_component == 0) {
				order_issued = unit_potential(unit, [this](UnitPotential& potential){
					potential.repel_units(all_enemy_units_, 32);
					potential.repel_storms();
				});
			}
			if (!order_issued) {
				order_issued = move_safe(unit, position);
			}
		}
	}
	
	return order_issued;
}

FastPosition MicroManager::find_closest_safe_position_near_target(Unit unit,Position position)
{
	auto& threat_component_grid = threat_grid.component_grid(unit->getType());
	FastTilePosition start_tile_position = threat_component_grid.to_tile_position(unit->getPosition());
	std::set<int> start_components;
	int start_component = threat_component_grid.component(start_tile_position);
	if (start_component == 0) {
		for (auto delta : { FastTilePosition{1, 0}, FastTilePosition{-1, 0}, FastTilePosition{0, -1}, FastTilePosition{0, 1},
			FastTilePosition{1, 1}, FastTilePosition{-1, 1}, FastTilePosition{-1, -1}, FastTilePosition{1, -1} }) {
				int component = threat_component_grid.component(start_tile_position + delta);
				if (component != 0) start_components.insert(component);
			}
	} else {
		start_components.insert(start_component);
	}
	FastPosition best_position;
	std::pair<int,int> best_distance(INT_MAX, INT_MAX);
	if (!start_components.empty()) {
		FastTilePosition tile_position = threat_component_grid.to_tile_position(position);
		FastPosition start_position = center_position_for(Broodwar->self()->getRace().getResourceDepot(), Broodwar->self()->getStartLocation());
		for (int dy = -20; dy <= 20; dy++) {
			for (int dx = -20; dx <= 20; dx++) {
				FastTilePosition candidate_tile_position = tile_position + FastTilePosition(dx, dy);
				int candidate_component = threat_component_grid.component(candidate_tile_position);
				if (contains(start_components, candidate_component)) {
					FastPosition candidate_position = threat_component_grid.to_position(candidate_tile_position);
					std::pair<int,int> distance = std::make_pair(candidate_position.getApproxDistance(position), candidate_position.getApproxDistance(start_position));
					if (distance < best_distance) {
						best_position = candidate_position;
						best_distance = distance;
					}
				}
			}
		}
	}
	return best_position;
}

void MicroManager::unload_bunkers()
{
	load_bunkers_ = false;
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		if (information_unit->type == UnitTypes::Terran_Bunker &&
			information_unit->is_completed() &&
			!unit->getLoadedUnits().empty()) {
			unit->unloadAll();
		}
	}
}

bool MicroManager::move_into_bunker(Unit combat_unit,Position stage_position)
{
	int smallest_distance = INT_MAX;
	Unit bunker_unit = nullptr;
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		if (information_unit->type == UnitTypes::Terran_Bunker &&
			information_unit->is_completed() &&
			unit->getLoadedUnits().size() < 4 &&
			calculate_distance(UnitTypes::Terran_Bunker, information_unit->position, stage_position) <= 320) {
			int distance = calculate_distance(combat_unit->getType(), combat_unit->getPosition(), stage_position);
			if (distance <= 320 &&
				distance < smallest_distance) {
				smallest_distance = distance;
				bunker_unit = unit;
			}
		}
	}
	if (bunker_unit != nullptr) {
		unit_right_click(combat_unit, bunker_unit);
		return true;
	} else {
		return false;
	}
}

void MicroManager::move_retreat(Unit unit,Position target_position)
{
	int divider = (unit->getPosition().getApproxDistance(target_position) <= 128 &&
				   !combat_state_.at(unit).block_chokepoint()) ? 24 : 3;
	if ((Broodwar->getFrameCount() % divider) == (unit->getID() % divider)) {
		bool order_issued = move_safe(unit, target_position);
		if (!order_issued) {
			if (!unit->isHoldingPosition()) unit->holdPosition();
		}
	}
}

bool MicroManager::move_safe(Unit unit,Position target_position)
{
	auto& threat_component_grid = threat_grid.component_grid(unit->getType());
	FastTilePosition start_tile_position = threat_component_grid.to_tile_position(unit->getPosition());
	FastTilePosition target_tile_position = threat_component_grid.to_tile_position(target_position);
	
	if (start_tile_position == target_tile_position || unit->getDistance(target_position) <= 32) {
		if (unit->getPosition() != target_position) {
			unit_move(unit, target_position);
			return true;
		}
		return false;
	}
	
	int target_component = threat_component_grid.component(target_tile_position);
	if (target_component == 0) {
		move_with_blockade_breaking(unit, target_position);
		return true;
	}
	
	int start_component = threat_component_grid.component(start_tile_position);
	if (start_component == 0) {
		int closest_distance = INT_MAX;
		FastTilePosition initial_start_tile_position = start_tile_position;
		for (auto delta : { FastTilePosition{1, 0}, FastTilePosition{-1, 0}, FastTilePosition{0, -1}, FastTilePosition{0, 1},
							FastTilePosition{1, 1}, FastTilePosition{-1, 1}, FastTilePosition{-1, -1}, FastTilePosition{1, -1} }) {
			FastTilePosition tile_positon = initial_start_tile_position + delta;
			int component = threat_component_grid.component(tile_positon);
			if (component == target_component) {
				int distance = target_position.getApproxDistance(threat_component_grid.to_position(tile_positon));
				if (distance < closest_distance) {
					start_component = component;
					start_tile_position = tile_positon;
					closest_distance = distance;
				}
			}
		}
	}
	if (start_component != target_component) {
		move_with_blockade_breaking(unit, target_position);
		return true;
	}
	
	const auto& grid = [&threat_component_grid,start_component](unsigned int x,unsigned int y){
		return threat_component_grid.component(FastTilePosition(x, y)) == start_component;
	};
	if (unit->isFlying() && line_safe(start_tile_position, target_tile_position, grid)) {
		unit_move(unit, FastPosition(target_tile_position));
		return true;
	}
	JPS::PathVector path;
	bool found = JPS::findPath(path, grid, start_tile_position.x, start_tile_position.y, target_tile_position.x, target_tile_position.y, 1);
	if (found) {
		std::vector<FastTilePosition> path_tiles;
		for (size_t i = 0; i < path.size(); i++) {
			if (i == 0 ||
				i == path.size() - 1 ||
				path[i].x - path[i - 1].x != path[i + 1].x - path[i].x ||
				path[i].y - path[i - 1].y != path[i + 1].y - path[i].y) {
				path_tiles.emplace_back(path[i].x, path[i].y);
			}
		}
		FastPosition move_position = target_position;
		int lookahead_distance = unit->isFlying() ? int(unit->getPlayer()->topSpeed(unit->getType()) * Broodwar->getRemainingLatencyFrames()) : 96;
		for (auto tile_position : path_tiles) {
			move_position = threat_component_grid.to_position(tile_position);
			if (unit->getPosition().getApproxDistance(move_position) > lookahead_distance) break;
		}
		if (unit->isFlying() &&
			unit->getPosition().getApproxDistance(move_position) * 256 < unit->getType().haltDistance()) {
			FastPosition new_position = scale_line_segment(unit->getPosition(), move_position, int(1.02 * unit->getType().haltDistance() / 256.0));
			if (new_position.isValid()) move_position = new_position;
		}
		unit_move(unit, move_position);
		// @
		/*for (size_t i = 0; i < path_tiles.size() - 1; i++) {
			FastTilePosition a = path_tiles[i];
			FastTilePosition b = path_tiles[i + 1];
			Broodwar->drawLineMap(threat_component_grid.to_position(a), threat_component_grid.to_position(b), Colors::White);
		}
		for (auto tile_position : path_tiles) {
			Broodwar->drawCircleMap(threat_component_grid.to_position(tile_position), 3, Colors::White, true);
		}*/
		//Broodwar->drawLineMap(unit->getPosition(), move_position, Colors::Green);
		// /@
		return true;
	}
	
	return false;	// Should be unreachable
}

void MicroManager::move_runby(Unit unit,Position target_position)
{
	FastTilePosition start_tile_position(unit->getPosition());
	FastTilePosition target_tile_position(target_position);
	
	UnitType target_unit_type;
	TilePosition target_building_tile_position;
	std::tie(target_unit_type, target_building_tile_position) = determine_building_at_position(target_position);
	
	int start_component;
	std::tie(start_component, start_tile_position) = connectivity_grid.component_and_tile_for_position(unit->getPosition());
	if (start_component == 0) {
		move_with_blockade_breaking(unit, center_position(target_tile_position));
		return;
	}
	
	if (target_unit_type != UnitTypes::None && !run_by_defense_.empty()) {
		key_value_vector<FastTilePosition,int> distances;
		
		int left = target_building_tile_position.x - 1;
		int right = target_building_tile_position.x + target_unit_type.tileWidth();
		int top = target_building_tile_position.y - 1;
		int bottom = target_building_tile_position.y + target_unit_type.tileHeight();
		
		const auto add_component_at_position = [&](int x,int y){
			int component = connectivity_grid.component_for_position(FastTilePosition(x, y));
			if (component == start_component) {
				FastPosition position = center_position(FastTilePosition(x, y));
				int distance = INT_MAX;
				for (auto& defense_unit : run_by_defense_) {
					const InformationUnit& enemy_defense_unit = information_manager.all_units().at(defense_unit);
					if (can_attack_in_range_at_positions(enemy_defense_unit.type, enemy_defense_unit.position, enemy_defense_unit.player, unit->getType(), position, 32)) {
						return;
					}
					distance = std::min(distance, position.getApproxDistance(enemy_defense_unit.position));
				}
				distances.emplace_back(position, distance);
			}
		};
		
		for (int x = left; x <= right; x++) {
			add_component_at_position(x, top);
			add_component_at_position(x, bottom);
		}
		for (int y = top; y <= bottom; y++) {
			add_component_at_position(left, y);
			add_component_at_position(right, y);
		}
		
		target_tile_position = key_with_largest_value(distances, target_tile_position);
	}
	
	if (start_tile_position == target_tile_position || unit->getDistance(center_position(target_tile_position)) <= 32) {
		if (unit->getPosition() != target_position) unit_move(unit, target_position);
		return;
	}
	int target_component = connectivity_grid.component_for_position(target_tile_position);
	if (target_component != start_component) {
		move_with_blockade_breaking(unit, center_position(target_tile_position));
		return;
	}
	
	const auto& grid = [start_component](unsigned int x,unsigned int y){
		return connectivity_grid.component_for_position(FastTilePosition(x, y)) == start_component;
	};
	JPS::PathVector path;
	bool found = JPS::findPath(path, grid, start_tile_position.x, start_tile_position.y, target_tile_position.x, target_tile_position.y, 1);
	if (found) {
		FastTilePosition move_tile_position;
		for (auto position : path) {
			move_tile_position = FastTilePosition(position.x, position.y);
			if (unit->getPosition().getApproxDistance(center_position(move_tile_position)) > 96) break;
		}
		unit_move(unit, center_position(move_tile_position));
		// @
		//Broodwar->drawLineMap(unit->getPosition(), center_position(move_tile_position), Colors::White);
		/*for (size_t i = 0; i < path.size() - 1; i++) {
			FastTilePosition a(path[i].x, path[i].y);
			FastTilePosition b(path[i + 1].x, path[i + 1].y);
			Broodwar->drawLineMap(center_position(a), center_position(b), Colors::White);
		 }
		Broodwar->drawCircleMap(center_position(target_tile_position), 10, Colors::White, true);*/
		// /@
	}
	move_with_blockade_breaking(unit, center_position(target_tile_position));	// should not be reachable
}

void MicroManager::move_with_blockade_breaking(Unit unit,Position target_position)
{
	bool use_move = true;
	
	if (!unit->isFlying() && can_attack(unit)) {
		FastPosition initial_position = unit->getPosition();
		int initial_ground_distance = ground_distance(initial_position, target_position);
		
		std::queue<FastPosition> queue;
		SparsePositionGrid<kBlockadeBreakingRange + 16,16,bool> visited(initial_position);
		queue.push(initial_position);
		visited[initial_position] = true;
		
		use_move = false;
		while (!queue.empty() && !use_move) {
			FastPosition current_position = queue.front();
			queue.pop();
			
			for (FastPosition delta_position : { FastPosition(-16, 0), FastPosition(16, 0), FastPosition(0, -16), FastPosition(0, 16) }) {
				FastPosition next_position = current_position + delta_position;
				if (next_position.isValid() && !visited[next_position]) {
					if (check_collision(unit, next_position)) {
						if (next_position.getApproxDistance(target_position) <= 16) {
							// Reached the target position
							use_move = true;
							break;
						}
						if (next_position.getApproxDistance(initial_position) <= kBlockadeBreakingRange) {
							queue.push(next_position);
						} else if (ground_distance(next_position, target_position) < initial_ground_distance) {
							// Found a border position closer to the target than the initial position
							use_move = true;
							break;
						}
					}
					visited[next_position] = true;
				}
			}
		}
	}
	
	if (!use_move) {
		Unit target = smallest_priority(harassable_enemy_units_, [target_position,unit](Unit enemy_unit){
			double angle = -INFINITY;
			if (!enemy_unit->getType().isBuilding() && can_attack_in_range(unit, enemy_unit)) {
				Position target_delta = target_position - unit->getPosition();
				double target_delta_norm = std::sqrt(target_delta.x * target_delta.x + target_delta.y * target_delta.y);
				Position enemy_delta = enemy_unit->getPosition() - unit->getPosition();
				double enemy_delta_norm = std::sqrt(enemy_delta.x * enemy_delta.x + enemy_delta.y * enemy_delta.y);
				angle = (target_delta.x * enemy_delta.x + target_delta.y * enemy_delta.y) / (target_delta_norm * enemy_delta_norm);
			}
			return std::make_tuple(-angle, unit->getDistance(enemy_unit), target_tie_breaker(unit, enemy_unit));
		});
		if (target != nullptr && can_attack_in_range(unit, target)) {
			unit_attack(unit, target);
		} else {
			use_move = true;
		}
	}
	
	if (use_move) {
		path_finder.execute_path(unit, target_position, [this,unit,target_position](){
			unit_move(unit, target_position);
		});
	}
}

bool MicroManager::recharge_at_shield_battery(Unit unit)
{
	CombatState& combat_state = combat_state_.at(unit);
	
	if (combat_state.shield_battery() != nullptr) {
		Unit shield_battery_unit = combat_state.shield_battery();
		if (!shield_battery_unit->exists() ||
			shield_battery_unit->getEnergy() < 2 ||
			unit->getShields() >= unit->getType().maxShields()) {
			combat_state.set_shield_battery(nullptr);
		} else {
			if (!recharge_at_shield_battery(unit, shield_battery_unit)) {
				combat_state.set_shield_battery(nullptr);
			}
		}
	}
	
	if (combat_state.shield_battery() == nullptr) {
		if (unit->getShields() < unit->getType().maxShields() / 3) {
			std::map<Unit,int> distances;
			for (auto shield_battery_unit : Broodwar->self()->getUnits()) {
				if (shield_battery_unit->getType() == UnitTypes::Protoss_Shield_Battery &&
					shield_battery_unit->isCompleted() &&
					shield_battery_unit->getEnergy() > 10) {
					int distance = unit->getDistance(shield_battery_unit);
					if (distance <= kShieldBatterySeekRange) distances[shield_battery_unit] = distance;
				}
			}
			std::vector<Unit> shield_battery_units = keys_sorted(distances);
			for (auto shield_battery_unit : shield_battery_units) {
				if (recharge_at_shield_battery(unit, shield_battery_unit)) {
					combat_state.set_shield_battery(shield_battery_unit);
					break;
				}
			}
		}
	}
	
	return combat_state.shield_battery() != nullptr;
}

bool MicroManager::recharge_at_shield_battery(Unit unit,Unit shield_battery_unit)
{
	FastPosition start_position(unit->getPosition());
	FastPosition target_position(shield_battery_unit->getPosition());
	
	enum class State : uint8_t
	{
		None = 0, Open, Closed
	};
	
	std::multimap<float,FastPosition> to_visit;
	SparsePositionGrid<kShieldBatterySeekRange,16,State> state(start_position);
	SparsePositionGrid<kShieldBatterySeekRange,16,float> g_score(start_position);
	to_visit.emplace((float)start_position.getApproxDistance(target_position), start_position);
	state[start_position] = State::Open;
	
	while (!to_visit.empty()) {
		FastPosition current_position = to_visit.begin()->second;
		if (calculate_distance(unit->getType(), current_position, shield_battery_unit->getType(), target_position) <= kShieldBatteryRechargeRange) {
			unit_right_click(unit, shield_battery_unit);
			return true;
		}
		to_visit.erase(to_visit.begin());
		State& current_state = state[current_position];
		if (current_state != State::Open) continue;
		current_state = State::Closed;
		float current_g_score = g_score[current_position];
		
		for (auto delta : { FastPosition(-16, 0), FastPosition(16, 0), FastPosition(0, -16), FastPosition(0, 16) }) {
			FastPosition next_position = current_position + delta;
			if (next_position.getApproxDistance(start_position) < kShieldBatterySeekRange &&
				check_collision(unit, next_position) &&
				state[next_position] != State::Closed) {
				State& next_state = state[next_position];
				float tentative_g_score = current_g_score + 1.0f;
				if (next_state == State::None) {
					next_state = State::Open;
				} else if (tentative_g_score >= g_score[next_position]) {
					continue;
				}
				g_score[next_position] = tentative_g_score;
				to_visit.emplace(tentative_g_score + next_position.getApproxDistance(target_position), next_position);
			}
		}
	}
	
	return false;
}

void MicroManager::draw()
{
	std::map<Position,int> target_positions;
	std::map<Position,int> stage_positions;
	for (auto& entry : combat_state_) {
		if (entry.second.target_position().isValid()) target_positions[entry.second.target_position()]++;
		if (entry.second.stage_position().isValid()) stage_positions[entry.second.stage_position()]++;
	}
	for (auto& entry : target_positions) Broodwar->drawCircleMap(entry.first, clamp(2, entry.second, 20), Colors::Red, true);
	for (auto& entry : stage_positions) Broodwar->drawCircleMap(entry.first, clamp(2, entry.second, 20), Colors::Green, true);
	if (run_by_target_position_.isValid()) {
		for (auto& defense_unit : run_by_defense_) {
			const InformationUnit& enemy_defense_unit = information_manager.all_units().at(defense_unit);
			Broodwar->drawCircleMap(enemy_defense_unit.position, 20, Colors::Blue, true);
		}
		Broodwar->drawCircleMap(run_by_target_position_, 20, Colors::Blue, true);
		for (auto& unit : running_by_) Broodwar->drawCircleMap(unit->getPosition(), 10, Colors::Blue, true);
		for (auto& unit : desperados_) Broodwar->drawCircleMap(unit->getPosition(), 10, Colors::Blue, false);
	}
}

Unit MicroManager::determine_nearby_sieged_tank(Unit combat_unit)
{
	if (combat_unit->getType() == UnitTypes::Protoss_Carrier) return nullptr;
	if (!information_manager.enemy_exists(UnitTypes::Terran_Siege_Tank_Siege_Mode)) return nullptr;
	std::vector<Unit> candidates;
	for (auto& unit : harassable_enemy_units_) {
		if (unit->getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode &&
			can_attack_in_range(combat_unit, unit) &&
			combat_unit->getDistance(unit) < WeaponTypes::Arclite_Shock_Cannon.minRange()) {
			if (combat_unit->getTarget() == unit) return unit;
			candidates.push_back(unit);
		}
	}
	return smallest_priority(candidates, [combat_unit](Unit unit){
		return combat_unit->getDistance(unit);
	});
}

Unit MicroManager::combat_unit_closest_to_position(Position position)
{
	bool target_above_ground = has_area(WalkPosition(position));
	key_value_vector<Unit,int> distances;
	for (Unit combat_unit : extended_combat_units_) {
		if (!combat_unit->isFlying()) {
			int distance = target_above_ground ? ground_distance(combat_unit->getPosition(), position) : combat_unit->getDistance(position);
			if (distance >= 0) distances.emplace_back(combat_unit, distance);
		}
	}
	return key_with_smallest_value(distances);
}

Unit MicroManager::combat_unit_in_base_closest_to_position(Position position,int max_base_distance)
{
	bool target_above_ground = has_area(WalkPosition(position));
	key_value_vector<Unit,int> distances;
	for (Unit combat_unit : extended_combat_units_) {
		if (!combat_unit->isFlying() && information_manager.all_units().at(combat_unit).base_distance <= max_base_distance) {
			int distance = target_above_ground ? ground_distance(combat_unit->getPosition(), position) : combat_unit->getDistance(position);
			if (distance >= 0) distances.emplace_back(combat_unit, distance);
		}
	}
	return key_with_smallest_value(distances);
}

Unit MicroManager::combat_unit_closest_to_special_unit(Unit special_unit)
{
	key_value_vector<Unit,int> distances;
	if (special_unit->isFlying()) {
		for (Unit combat_unit : extended_combat_units_) {
			distances.emplace_back(combat_unit, special_unit->getDistance(combat_unit));
		}
	} else {
		for (Unit combat_unit : extended_combat_units_) {
			if (!combat_unit->isFlying()) {
				int distance = ground_distance(combat_unit->getPosition(), special_unit->getPosition());
				if (distance >= 0) distances.emplace_back(combat_unit, distance);
			}
		}
	}
	return key_with_smallest_value(distances);
}

bool MicroManager::is_reaver_can_attack_in_range(Unit combat_unit,Unit enemy_unit)
{
	return (combat_unit->getType() == UnitTypes::Protoss_Reaver &&
			can_attack_in_range_with_prediction(combat_unit, enemy_unit));
}

bool MicroManager::valid_target(Unit combat_unit,Unit enemy_unit)
{
	CombatState& combat_state = combat_state_.at(combat_unit);
	Position target_position = combat_state.target_position();
	bool ignore_when_attacking = (ignore_when_attacking_.count(enemy_unit) > 0 &&
								  !can_attack_in_range_with_prediction(combat_unit, enemy_unit) &&
								  !(combat_unit->getDistance(enemy_unit) <= combat_unit->getType().sightRange() &&
									combat_unit->getPlayer()->topSpeed(combat_unit->getType()) > 1.1 * enemy_unit->getPlayer()->topSpeed(enemy_unit->getType()) &&
									(combat_unit->isFlying() || !enemy_unit->isFlying())));
	
	if (target_position.isValid() &&
		combat_state.near_target_only() &&
		enemy_unit->getDistance(target_position) > 320 &&
		!(connectivity_grid.is_wall_building(enemy_unit) && can_attack_in_range(combat_unit, enemy_unit))) {
		return false;
	}
	
	if (target_position.isValid() &&
		combat_unit->getType() == UnitTypes::Zerg_Hydralisk &&
		enemy_unit->getType() == UnitTypes::Protoss_Corsair &&
		building_manager.building_exists(UnitTypes::Zerg_Spore_Colony) &&
		!can_attack_in_range_with_prediction(combat_unit, enemy_unit)) {
		return false;
	}
	
	if (target_position.isValid() || target_position == Positions::Unknown) {
		return !ignore_when_attacking;
	}
	
	if (building_placement_manager.proxy_pylon_position().isValid() && ignore_when_attacking) {
		return false;
	}
	
	if (is_reaver_can_attack_in_range(combat_unit, enemy_unit)) {
		return true;
	}
	
	if (enemy_unit->getType() == UnitTypes::Terran_Vulture_Spider_Mine &&
		enemy_unit->getOrderTarget() != nullptr &&
		combat_unit->getDistance(enemy_unit) <= 320) {
		return true;
	}
	
	const InformationUnit& enemy_information_unit = information_manager.all_units().at(enemy_unit);
	if (enemy_information_unit.base_distance == 0) return true;
	if (enemy_unit->getType() == UnitTypes::Protoss_Photon_Cannon && enemy_information_unit.base_distance <= 320) return true;
	
	if (contains(enemy_units_threatening_buildings_or_workers_, enemy_unit)) {
		return true;
	}
	
	if (units_near_base_.count(enemy_unit) > 0 && !combat_state.block_chokepoint()) return true;
	if (combat_state.block_chokepoint() &&
		combat_state.stage_position().isValid() &&
		can_attack_in_range_at_position_with_prediction(combat_unit, combat_state.stage_position(), enemy_unit)) return true;
	
	return false;
}

Unit MicroManager::determine_incoming_mine(Unit combat_unit)
{
	if (combat_unit->getLastCommand().getType() == UnitCommandTypes::Attack_Unit &&
		combat_unit->getLastCommand().getTarget() != nullptr &&
		combat_unit->getLastCommand().getTarget()->exists() &&
		combat_unit->getLastCommand().getTarget()->getType() == UnitTypes::Terran_Vulture_Spider_Mine &&
		combat_unit->getLastCommandFrame() >= Broodwar->getFrameCount() - Broodwar->getRemainingLatencyFrames()) {
		return combat_unit->getLastCommand().getTarget();
	}
	
	if (combat_unit->getOrderTarget() != nullptr &&
		combat_unit->getOrderTarget()->exists() &&
		combat_unit->getOrderTarget()->getType() == UnitTypes::Terran_Vulture_Spider_Mine) {
		return combat_unit->getOrderTarget();
	}
	
	std::vector<Unit> incoming_mines;
	for (auto& unit : harassable_enemy_units_) {
		if (unit->getType() == UnitTypes::Terran_Vulture_Spider_Mine &&
			(unit->getOrderTarget() == combat_unit || unit->getDistance(combat_unit) <= UnitTypes::Terran_Vulture_Spider_Mine.seekRange())) incoming_mines.push_back(unit);
	}
	Unit closest_incoming_mine = smallest_priority(incoming_mines, [combat_unit](Unit incoming_mine) {
		return combat_unit->getDistance(incoming_mine);
	});
	return closest_incoming_mine;
}

bool MicroManager::dark_templar_path_based_order(Unit combat_unit,const DarkTemplarPathNearbyUnits& nearby_units)
{
	constexpr int max_distance = kDarkTemplarPathMaxDistance;
	constexpr int step_size = kDarkTemplarPathStepSize;
	bool order_issued = false;
	
	FastPosition start_position = combat_unit->getPosition();
	const std::vector<std::pair<const InformationUnit*,int>>& enemy_attack_units = nearby_units.enemy_attack_units;
	const std::vector<std::pair<const InformationUnit*,int>>& enemy_detector_units = nearby_units.enemy_detector_units;
	const std::vector<std::pair<const InformationUnit*,int>>& enemy_attackable_units = nearby_units.enemy_attackable_units;
	
	DPF ranged_dpf;
	DPF melee_and_worker_dpf;
	int melee_and_worker_count = 0;
	for (auto [enemy_unit,completion] : enemy_attack_units) {
		int bunker_marines_loaded = 0;
		if (enemy_unit->type == UnitTypes::Terran_Bunker) {
			bunker_marines_loaded = information_manager.bunker_marines_loaded(enemy_unit->unit);
		}
		DPF dpf = calculate_damage_per_frame(enemy_unit->type, enemy_unit->player, UnitTypes::Protoss_Dark_Templar, Broodwar->self(), bunker_marines_loaded);
		if (dpf) {
			if (is_melee_or_worker(enemy_unit->type)) {
				melee_and_worker_dpf.shield += dpf.shield;
				melee_and_worker_dpf.hp += dpf.hp;
				melee_and_worker_count++;
			} else {
				ranged_dpf.shield += dpf.shield;
				ranged_dpf.hp += dpf.hp;
			}
		}
	}
	DPF total_dpf = ranged_dpf;
	if (melee_and_worker_count > 0) {
		total_dpf.shield += (melee_and_worker_dpf.shield / melee_and_worker_count);
		total_dpf.hp += (melee_and_worker_dpf.hp / melee_and_worker_count);
	}
	int total_damage_frames = std::max(1, int(combat_unit->getShields() / total_dpf.shield + combat_unit->getHitPoints() / total_dpf.hp));
	
	CombatState& combat_state = combat_state_.at(combat_unit);
	Position target_position = combat_state.target_position();
	Position stage_position = combat_state.stage_position();
	struct Distance
	{
		int damage_frames_left;
		int frames;
		
		Distance() : damage_frames_left(-1), frames(INT_MAX) {}
		Distance(int damage_frames_left) : damage_frames_left(damage_frames_left), frames(0) {}
		bool is_dead() const { return damage_frames_left <= 0; }
		bool is_unreachable() const { return damage_frames_left < 0; }
		bool operator<(const Distance& o) const {
			return std::make_tuple(-damage_frames_left, frames) < std::make_tuple(-o.damage_frames_left, o.frames);
		};
	};
	
	std::multimap<Distance,FastPosition> Q;
	SparsePositionGrid<max_distance,step_size,bool> closed(start_position);
	SparsePositionGrid<max_distance,step_size,Distance> dist(start_position);
	SparsePositionGrid<max_distance,step_size,FastPosition> prev(start_position);
	SparsePositionGrid<max_distance,step_size,int> detected(start_position);
	Distance start_distance(total_damage_frames);
	dist[start_position] = start_distance;
	prev[start_position] = start_position;
	Q.emplace(start_distance, start_position);
	
	for (auto position : detected) {
		int detection_completion = INT_MAX;
		for (auto [enemy_unit,completion] : enemy_detector_units) {
			if (calculate_distance(UnitTypes::Protoss_Dark_Templar, position, enemy_unit->type, enemy_unit->position) <= enemy_unit->detection_range()) {
				detection_completion = std::min(detection_completion, completion);
			}
		}
		detected[position] = detection_completion;
	}
	
	std::map<const InformationUnit*,int> kill_frames_map;
	const auto determine_kill_frames = [&](const InformationUnit* information_unit){
		int& kill_frames = kill_frames_map[information_unit];
		if (kill_frames == 0) {
			DPF dpf = calculate_damage_per_frame(UnitTypes::Protoss_Dark_Templar, Broodwar->self(), information_unit->type, information_unit->player);
			if (dpf) {
				kill_frames = int(information_unit->expected_shields() / dpf.shield + information_unit->expected_hitpoints() / dpf.hp + 0.5);
			}
			if (kill_frames < 1) kill_frames = 1;
		}
		return int(kill_frames);
	};
	
	const int straight_frames = int(step_size / UnitTypes::Protoss_Dark_Templar.topSpeed() + 0.5);
	const double oblique_length = std::sqrt(2.0 * step_size * step_size);
	const int oblique_frames = int(oblique_length / UnitTypes::Protoss_Dark_Templar.topSpeed() + 0.5);
	while (!Q.empty()) {
		FastPosition current_position = Q.begin()->second;
		Q.erase(Q.begin());
		if (closed[current_position]) continue;
		closed[current_position] = true;
		if (dist[current_position].is_dead()) continue;
		
		for (auto delta : { FastPosition{step_size, 0}, FastPosition{-step_size, 0}, FastPosition{0, -step_size}, FastPosition{0, step_size},
			FastPosition{step_size, step_size}, FastPosition{-step_size, step_size}, FastPosition{step_size, -step_size}, FastPosition{-step_size, -step_size} }) {
				FastPosition next_position = current_position + delta;
				if (closed.is_valid(next_position) &&
					!closed[next_position] &&
					check_terrain_collision(UnitTypes::Protoss_Dark_Templar, next_position)) {
					std::vector<const InformationUnit*> next_colliding_units = colliding_units_sorted(combat_unit, next_position);
					
					bool only_valid_targets = true;
					for (auto& information_unit : next_colliding_units) {
						if (information_unit->player == Broodwar->self()) {
							only_valid_targets = false;
							break;
						}
						bool invincible = information_unit->unit->exists() ? information_unit->unit->isInvincible() : information_unit->type.isInvincible();
						if (invincible) {
							only_valid_targets = false;
							break;
						}
					}
					
					if (only_valid_targets) {
						Distance alt = dist[current_position];
						
						int frames = (delta.x != 0 && delta.y != 0) ? oblique_frames : straight_frames;
						for (auto& information_unit : next_colliding_units) {
							if (check_unit_collision(combat_unit, current_position, information_unit, 1)) {
								frames += determine_kill_frames(information_unit);
							}
						}
						alt.frames += frames;
						if (detected[next_position] <= alt.frames) {
							alt.damage_frames_left = std::max(0, alt.damage_frames_left - frames);
						}
						
						if (alt < dist[next_position]) {
							dist[next_position] = alt;
							prev[next_position] = current_position;
							Q.emplace(alt, next_position);
						}
					} else {
						closed[next_position] = true;
						//Broodwar->drawCircleMap(next_position, 3, Colors::Red);	// @
					}
				}
			}
	}
	
	enum class ActionType
	{
		AttackUnit = 1,
		Move = 2,
		SuicideAttackUnit = 3
	};
	
	struct Action
	{
		ActionType type;
		FastPosition position;
		const InformationUnit* target = nullptr;
		int priority;
		Distance distance;
	};
	std::vector<Action> actions;
	
	const BWEM::Area* current_area = area_at(combat_unit->getPosition());
	Position order_position = target_position.isValid() ? target_position : stage_position;
	const BWEM::Area* order_area = target_position.isValid() ? area_at(order_position) : nullptr;
	
	auto const is_closer_to_target = [target_position,combat_unit](const InformationUnit* enemy_unit){
		bool result = true;
		if (target_position.isValid()) {
			int distance_to_target = ground_distance(combat_unit->getPosition(), target_position);
			if (distance_to_target >= 0) {
				int predicted_distance = -1;
				if (enemy_unit->unit->exists()) predicted_distance = ground_distance(predict_position(enemy_unit->unit, 8), target_position);
				if (predicted_distance >= 0) {
					result = (predicted_distance <= distance_to_target);
				} else {
					int distance = ground_distance(enemy_unit->position, target_position);
					if (distance >= 0) {
						result = (distance <= distance_to_target);
					}
				}
			}
		}
		return result;
	};
	
	int combat_unit_max_dimension = max_unit_dimension(UnitTypes::Protoss_Dark_Templar);
	for (auto [enemy_unit,completion] : enemy_attackable_units) {
		int d = combat_unit_max_dimension + max_unit_dimension(enemy_unit->type) + weapon_max_range(combat_unit, enemy_unit->flying);
		d = step_size * ((d + step_size) / step_size);
		Distance best_distance;
		FastPosition best_position;
		FastPosition enemy_position = dist.snap_to_grid(enemy_unit->position);
		for (int y = enemy_position.y - d; y <= enemy_position.y + d + step_size - 1; y += step_size) {
			for (int x = enemy_position.x - d; x <= enemy_position.x + d + step_size - 1; x += step_size) {
				FastPosition position(x, y);
				if (dist.is_valid(position) &&
					!dist[position].is_dead() &&
					can_attack_in_range_at_positions(combat_unit->getType(), position, Broodwar->self(), enemy_unit->type, enemy_unit->position) &&
					dist[position] < best_distance) {
					best_distance = dist[position];
					best_position = position;
				}
			}
		}
		
		if (!best_distance.is_dead()) {
			UnitType unit_type = enemy_unit->type;
			int frame = best_distance.frames;
			bool completed = (completion <= frame);
			int base_priority;
			if (unit_type.isDetector()) {
				base_priority = completed ? 1 : 2;
			} else if (unit_type == UnitTypes::Terran_Comsat_Station) {
				base_priority = completed ? 3 : 4;
			} else if (unit_type.isWorker() && enemy_unit->unit->exists() && (enemy_unit->unit->isGatheringMinerals() || enemy_unit->unit->isGatheringGas())) {
				base_priority = 5;
			} else if (unit_type.isResourceDepot()) {
				base_priority = completed ? 300 : 301;
			} else if (unit_type == UnitTypes::Protoss_Forge ||
					   unit_type == UnitTypes::Protoss_Robotics_Facility ||
					   unit_type == UnitTypes::Protoss_Observatory ||
					   unit_type == UnitTypes::Terran_Academy ||
					   unit_type == UnitTypes::Terran_Engineering_Bay ||
					   unit_type == UnitTypes::Terran_Science_Facility ||
					   unit_type == UnitTypes::Zerg_Evolution_Chamber) {
				base_priority = completed ? 302 : 303;
			} else if (unit_type.isWorker()) {
				base_priority = 304;
			} else {
				base_priority = 600;
			}
			const BWEM::Area* area = enemy_unit->area;
			int priority;
			if (current_area != nullptr && area == current_area) {
				priority = base_priority + 100;
			} else if (order_area != nullptr && area == order_area) {
				priority = base_priority + 200;
			} else {
				priority = base_priority + 300;
			}
			if ((order_area == nullptr || area != order_area) && base_priority >= 300 &&
				unit_type.canMove() && !is_closer_to_target(enemy_unit)) {
				priority += 1000;
			}
			
			int frames = determine_kill_frames(enemy_unit);
			Distance distance = best_distance;
			distance.frames += frames;
			if (detected[best_position] <= distance.frames) {
				distance.damage_frames_left = std::max(0, distance.damage_frames_left - frames);
			}
			
			Action action;
			action.type = distance.is_dead() ? ActionType::SuicideAttackUnit : ActionType::AttackUnit;
			action.position = best_position;
			action.target = enemy_unit;
			action.priority = priority;
			action.distance = best_distance;
			actions.push_back(action);
		}
	}
	auto const add_move_action = [&](int x,int y){
		FastPosition position(x, y);
		const Distance& distance = dist[position];
		if (!distance.is_dead()) {
			int distance_to_order_position = ground_distance(position, order_position);
			if (distance_to_order_position >= 0) {
				Action action;
				action.type = ActionType::Move;
				action.position = position;
				action.target = nullptr;
				action.priority = distance_to_order_position + int(distance.frames * UnitTypes::Protoss_Dark_Templar.topSpeed() + 0.5);
				action.distance = distance;
				actions.push_back(action);
			}
		}
	};
	for (int y = start_position.y - max_distance; y <= start_position.y + max_distance; y += step_size) {
		add_move_action(start_position.x - max_distance, y);
		add_move_action(start_position.x + max_distance, y);
	}
	for (int x = start_position.x - max_distance; x <= start_position.x + max_distance; x += step_size) {
		add_move_action(x, start_position.y - max_distance);
		add_move_action(x, start_position.y + max_distance);
	}
	if (dist.is_valid(order_position)) {
		add_move_action(order_position.x, order_position.y);
	}
	
	// @ Plot actions
	/*for (auto& action : actions) {
	 draw_cross_map(action.position, 3, Colors::Red);
		}*/
	// @ Plot alive positions
	/*for (int y = start_position.y - max_distance; y <= start_position.y + max_distance; y += step_size) {
	 for (int x = start_position.x - max_distance; x <= start_position.x + max_distance; x += step_size) {
	 FastPosition position(x, y);
	 if (!dist[position].is_dead()) {
	 draw_cross_map(position, 3, Colors::Green);
	 }
	 }
		}*/
	
	const int dark_templar_latency_distance = int(std::ceil(Broodwar->getRemainingLatencyFrames() * combat_unit->getType().topSpeed()));
	if (!actions.empty()) {
		Action action = smallest_priority(actions, [combat_unit](auto& action){
			return std::make_tuple(action.type, action.priority, action.distance, action.target == nullptr ? 0 : target_tie_breaker(combat_unit, action.target->unit), action.position);
		});
		std::vector<FastPosition> path;
		FastPosition current_position = action.position;
		path.push_back(current_position);
		while (current_position != start_position) {
			current_position = prev[current_position];
			path.push_back(current_position);
		}
		std::reverse(path.begin(), path.end());
		const InformationUnit* enemy_unit = action.target;
		FastPosition position = action.position;
		bool collision_found = false;
		for (size_t i = 0; i < std::min(size_t(8), path.size()); i++) {
			std::vector<const InformationUnit*> colliding_units = colliding_units_sorted(combat_unit, path[i]);
			// @
			/*if (std::any_of(colliding_units.begin(), colliding_units.end(), [](auto& information_unit){
			 return information_unit->player == Broodwar->self();
				})) {
			 Broodwar << (int)i << "/" << (int)path.size() << ";" << combat_unit->getPosition().x << "," << combat_unit->getPosition().y << ";" << path[i].x << "," << path[i].y << std::endl;
				}*/
			// /@
			const InformationUnit* colliding_unit = smallest_priority(colliding_units, [combat_unit](auto& information_unit){
				if (information_unit->player == Broodwar->self()) return INT_MAX;
				return calculate_distance(UnitTypes::Protoss_Dark_Templar, combat_unit->getPosition(), information_unit->type, information_unit->position);
			});
			if (colliding_unit != nullptr && colliding_unit->player != Broodwar->self()) {
				enemy_unit = colliding_unit;
				position = path[i];
				collision_found = true;
				//Broodwar->drawTextMap(combat_unit->getPosition(), "%dA", (int)path.size());	// @
				break;
			}
		}
		if (!collision_found) {
			if (path.size() >= 8) {
				enemy_unit = nullptr;
				position = path[7];
				//Broodwar->drawTextMap(combat_unit->getPosition(), "%dB", (int)path.size());	// @
			} else if (action.target != nullptr) {
				int distance = action.position.getApproxDistance(combat_unit->getPosition());
				if (distance >= dark_templar_latency_distance) {
					enemy_unit = nullptr;
					position = action.position;
					//Broodwar->drawTextMap(combat_unit->getPosition(), "%dC", (int)path.size());	// @
				} else {
					//Broodwar->drawTextMap(combat_unit->getPosition(), "%dD", (int)path.size());	// @
				}
			} else {
				//Broodwar->drawTextMap(combat_unit->getPosition(), "%dE", (int)path.size());	// @
			}
		}
		// @
		/*for (size_t i = 0; i < path.size(); i++) {
		 draw_cross_map(path[i], 2, Colors::White);
			}
			if (action.position.isValid()) {
		 Broodwar->drawLineMap(combat_unit->getPosition(), action.position, Colors::White);
			}
			if (action.target != nullptr) {
		 Broodwar->drawLineMap(combat_unit->getPosition(), action.target->position, Colors::Red);
		 //Broodwar->drawTextMap(combat_unit->getPosition(), "%c%s", Text::Red, action.target->type.c_str());
			}*/
		// /@
		
		if (enemy_unit != nullptr) {
			order_issued = unit_potential(combat_unit, [](UnitPotential& potential){
				potential.repel_storms();
			});
			if (!order_issued && enemy_unit->unit->exists()) {
				Unit selected_enemy_unit = enemy_unit->unit;
				order_issued = path_finder.execute_path(combat_unit, selected_enemy_unit->getPosition(), [this,combat_unit,selected_enemy_unit](){
					if (can_attack_in_range(combat_unit, selected_enemy_unit)) {
						unit_attack(combat_unit, selected_enemy_unit);
					} else {
						unit_move(combat_unit, selected_enemy_unit->getPosition());
					}
				});
			}
		}
		if (!order_issued && position.isValid()) {
			order_issued = path_finder.execute_path(combat_unit, position, [combat_unit,position](){
				//Broodwar->drawLineMap(combat_unit->getPosition(), position, Colors::White);	// @
				unit_move(combat_unit, position);
			});
		}
	}
	
	return order_issued;
}

std::tuple<Unit,bool,bool> MicroManager::select_enemy_unit_for_combat_unit(Unit combat_unit)
{
	CombatState& combat_state = combat_state_.at(combat_unit);
	
	Unit selected_enemy_unit = nullptr;
	bool enable_advance = true;
	bool enable_retreat = true;
	
	if (running_by_.count(combat_unit) > 0 || desperados_.count(combat_unit) > 0) {
		if (desperados_.count(combat_unit) > 0) {
			std::vector<Unit> enemy_units_not_near_defense;	// @ Should probably be moved to a more global location
			for (auto& unit : harassable_enemy_units_) {
				// @ Take ignore_when_attacking_ into account?
				bool not_near_defense = std::none_of(run_by_defense_.begin(), run_by_defense_.end(), [unit](auto& defense_unit){
					const InformationUnit& enemy_defense_unit = information_manager.all_units().at(defense_unit);
					return can_attack_in_range_at_positions(enemy_defense_unit.type, enemy_defense_unit.position, enemy_defense_unit.player, unit->getType(), unit->getPosition(), 32);
				});
				if (not_near_defense) enemy_units_not_near_defense.push_back(unit);
			}
			if (enemy_units_not_near_defense.empty()) {
				if (calculate_distance(UnitTypes::Protoss_Nexus, run_by_target_position_, combat_unit->getType(), combat_unit->getPosition()) < 32) {
					desperados_.erase(combat_unit);
				}
			} else {
				selected_enemy_unit = select_enemy_unit_for_combat_unit(combat_unit, enemy_units_not_near_defense);
			}
		}
	} else {
		std::vector<Unit> attackable_enemy_units;
		std::tie(attackable_enemy_units, enable_advance, enable_retreat) = determine_attackable_enemy_units(combat_unit);
		if (combat_unit->getType() == UnitTypes::Protoss_Reaver) {
			selected_enemy_unit = select_enemy_unit_for_reaver(combat_unit, attackable_enemy_units);
		} else {
			selected_enemy_unit = select_enemy_unit_for_combat_unit(combat_unit, attackable_enemy_units);
		}
	}
	
	return std::make_tuple(selected_enemy_unit, enable_advance, enable_retreat);
}

std::tuple<Unit,bool,bool> MicroManager::select_enemy_unit_for_lurker(Unit combat_unit)
{
	std::vector<Unit> attackable_enemy_units;
	bool enable_advance;
	bool enable_retreat;
	std::tie(attackable_enemy_units, enable_advance, enable_retreat) = determine_attackable_enemy_units(combat_unit);
	
	std::vector<Unit> possible_targets;
	int component = connectivity_grid.component_for_position(combat_unit->getPosition());
	for (auto& unit : attackable_enemy_units) {
		if (!unit->isStasised() &&
			can_attack(combat_unit, unit) &&
			connectivity_grid.check_reachability_ranged(component, weapon_max_range(combat_unit, unit->isFlying()), unit)) {
			possible_targets.push_back(unit);
		}
	}
	
	Unit selected_enemy_unit = smallest_priority(possible_targets, [combat_unit](Unit unit) {
		return std::make_tuple(is_low_priority_target(unit),
							   combat_unit->getDistance(unit),
							   target_tie_breaker(combat_unit, unit));
	});
	
	return std::make_tuple(selected_enemy_unit, enable_advance, enable_retreat);
}

std::tuple<std::vector<Unit>,bool,bool> MicroManager::determine_attackable_enemy_units(Unit combat_unit)
{
	CombatState& combat_state = combat_state_.at(combat_unit);
	Position target_position = combat_state.target_position();
	bool enable_attack = true;
	bool enable_advance = true;
	bool enable_retreat = true;
	
	std::vector<Unit> enemy_units;
	for (auto& enemy_unit : harassable_enemy_units_) {
		if (valid_target(combat_unit, enemy_unit)) {
			enemy_units.push_back(enemy_unit);
		}
	}
	
	if (combat_state.always_advance()) {
		return std::make_tuple(enemy_units, enable_advance, enable_retreat);
	} else {
		std::set<Unit> do_not_attack_units;
		for (auto& cluster : tactics_manager.clusters()) {
			if (!cluster.expect_win(combat_unit)) {
				if (cluster.near_front(combat_unit)) {
					enable_advance = false;
					enable_retreat = false;
					if (no_cluster_units_near_base(cluster)) {
						for (auto enemy_unit : cluster.units()) {
							if (is_melee_or_worker(combat_unit->getType()) ||
								!can_attack_in_range_with_prediction(combat_unit, enemy_unit->unit)) {
								do_not_attack_units.insert(enemy_unit->unit);
							}
						}
					}
				} else if (cluster.in_front(combat_unit)) {
					enable_attack = false;
					enable_advance = false;
					enable_retreat = true;
					break;
				} else if (no_cluster_units_near_base(cluster)) {
					for (auto enemy_unit : cluster.units()) {
						do_not_attack_units.insert(enemy_unit->unit);
					}
				}
			}
		}
		
		std::vector<Unit> attackable_enemy_units;
		if (!enable_attack) {
			for (auto unit : enemy_units) {
				if (contains(units_near_main_base_, unit) ||
					is_reaver_can_attack_in_range(combat_unit, unit)) {
					attackable_enemy_units.push_back(unit);
				}
			}
		} else {
			for (auto unit : enemy_units) {
				if (!contains(do_not_attack_units, unit) ||
					contains(units_near_main_base_, unit) ||
					(target_position.isValid() && unit->getPosition() == target_position) ||
					is_reaver_can_attack_in_range(combat_unit, unit)) {
					attackable_enemy_units.push_back(unit);
				}
			}
		}
		
		return std::make_tuple(attackable_enemy_units, enable_advance, enable_retreat);
	}
}

std::tuple<bool,bool> MicroManager::determine_advance_retreat_for_special_unit(Unit special_unit)
{
	CombatState& combat_state = combat_state_.at(special_unit);
	bool enable_advance = true;
	bool enable_retreat = true;
	
	if (!combat_state.always_advance()) {
		for (auto& cluster : tactics_manager.clusters()) {
			if (!cluster.expect_win(special_unit)) {
				if (cluster.near_front(special_unit)) {
					enable_advance = false;
					enable_retreat = false;
				} else if (cluster.in_front(special_unit)) {
					enable_advance = false;
					enable_retreat = true;
					break;
				}
			} else {
				if (cluster.is_engaged(special_unit)) {
					enable_advance = false;
					enable_retreat = true;
					break;
				} else if (cluster.is_engaged(special_unit)) {
					enable_advance = false;
					enable_retreat = false;
				}
			}
		}
	}
	
	return std::make_tuple(enable_advance, enable_retreat);
}

bool MicroManager::determine_allow_stim(Unit combat_unit)
{
	for (auto& cluster : tactics_manager.clusters()) {
		if (cluster.stim_allowed(combat_unit)) {
			return true;
		}
	}
	return false;
}

Unit MicroManager::select_enemy_unit_for_reaver(Unit combat_unit,const std::vector<Unit>& enemy_units)
{
	struct OutOfRangeTarget {
		Unit unit = nullptr;
		int distance;
	};
	
	std::vector<Unit> remaining_enemy_units;
	std::vector<OutOfRangeTarget> out_of_range_enemy_units;
	for (Unit enemy_unit : enemy_units) {
		int distance = ground_distance(combat_unit->getPosition(), enemy_unit->getPosition());
		if (can_attack_in_range_with_prediction(combat_unit, enemy_unit) &&
			distance >= 0 &&
			distance <= 256 + max_unit_dimension(UnitTypes::Protoss_Reaver) + max_unit_dimension(enemy_unit->getType())) {
			remaining_enemy_units.push_back(enemy_unit);
		} else if (can_attack(combat_unit, enemy_unit)) {
			out_of_range_enemy_units.push_back(OutOfRangeTarget{enemy_unit, distance});
		}
	}
	
	struct PossibleTarget {
		Unit unit = nullptr;
		FastPosition position;
		int distance;
		int kills = 0;
		int damage = 0;
		
		void add_damage(Unit combat_unit,Unit enemy_unit,int damage_divisor)
		{
			DamageModel damage_model(enemy_unit);
			damage_model.apply_damage(combat_unit, damage_divisor);
			int supply = (enemy_unit->getType().supplyRequired() + TacticsManager::defense_supply_equivalent(enemy_unit));
			if (supply > 0 && damage_model.is_dead()) {
				kills += supply;
			} else {
				damage += int(damage_model.shields_lost() + damage_model.hp_lost() + 0.5);
			}
		}
	};
	std::vector<PossibleTarget> possible_targets;
	
	FastPosition initial_position = combat_unit->getPosition();
	std::queue<FastPosition> queue;
	SparsePositionGrid<256,8,int> distances(initial_position);
	queue.push(initial_position);
	distances[initial_position] = 1;
	
	while (!queue.empty() && !remaining_enemy_units.empty()) {
		FastPosition current_position = queue.front();
		queue.pop();
		
		int current_distance = distances[current_position];
		remove_elements_in_place(remaining_enemy_units, [&possible_targets,combat_unit,current_position,current_distance](Unit enemy_unit){
			int scarab_distance = calculate_distance(enemy_unit->getType(), enemy_unit->getPosition(), current_position);
			if (scarab_distance <= WeaponTypes::Scarab.innerSplashRadius()) {
				possible_targets.push_back(PossibleTarget{enemy_unit, current_position, current_distance});
				return true;
			}
			return false;
		});
		
		for (FastPosition delta_position : { FastPosition(-8, 0), FastPosition(8, 0), FastPosition(0, -8), FastPosition(0, 8) }) {
			 FastPosition next_position = current_position + delta_position;
			if (next_position.isValid() && next_position.getApproxDistance(initial_position) <= 256) {
				int& distance = distances[next_position];
				if (distance == 0) {
					if (check_collision(combat_unit, UnitTypes::Protoss_Scarab, next_position)) {
						queue.push(next_position);
						distance = current_distance + 8;
					} else {
						distance = -1;
					}
				}
			}
		}
	}
	
	for (auto& possible_target : possible_targets) {
		for (Unit enemy_unit : enemy_units) {
			int distance = possible_target.position.getApproxDistance(enemy_unit->getPosition());
			if (distance > WeaponTypes::Scarab.outerSplashRadius()) continue;
			if (distance <= WeaponTypes::Scarab.innerSplashRadius()) {
				possible_target.add_damage(combat_unit, enemy_unit, 1);
			} else if (!enemy_unit->isBurrowed()) {
				if (distance <= WeaponTypes::Scarab.medianSplashRadius()) {
					possible_target.add_damage(combat_unit, enemy_unit, 2);
				} else {
					possible_target.add_damage(combat_unit, enemy_unit, 4);
				}
			}
		}
	}
	
	Unit result = smallest_priority(possible_targets, [combat_unit](auto& possible_target){
		return std::make_tuple(!can_attack(possible_target.unit),
							   -possible_target.kills,
							   -possible_target.damage,
							   possible_target.distance,
							   possible_target.unit->getID());
	}).unit;
	
	if (result == nullptr) {
		result = smallest_priority(out_of_range_enemy_units, [combat_unit](auto& target){
			return std::make_tuple(is_low_priority_target(target.unit),
								   target.distance,
								   target_tie_breaker(combat_unit, target.unit));
		}).unit;
	}
	
	return result;
}

Unit MicroManager::select_enemy_unit_for_combat_unit(Unit combat_unit,const std::vector<Unit>& enemy_units)
{
	std::vector<Unit> possible_targets;
	for (auto& unit : enemy_units) {
		if (!unit->isStasised() && can_attack(combat_unit, unit)) possible_targets.push_back(unit);
	}
	int component = connectivity_grid.component_for_position(combat_unit->getPosition());
	if (is_melee_or_worker(combat_unit->getType())) {
		remove_elements_in_place(possible_targets, [this,component](Unit unit){
			return !connectivity_grid.check_reachability_melee(component, unit);
		});
	} else if (!combat_unit->isFlying()) {
		remove_elements_in_place(possible_targets, [this,combat_unit,component](Unit unit){
			return !connectivity_grid.check_reachability_ranged(component, weapon_max_range(combat_unit, unit->isFlying()), unit);
		});
	}
	if (combat_unit->getType() == UnitTypes::Zerg_Scourge) {
		remove_elements_in_place(possible_targets, [this,combat_unit](Unit unit){
			DPF dpf = calculate_damage_per_frame(combat_unit, unit);
			unsigned int number_of_scourge = (unsigned int)(std::ceil(unit->getShields() / dpf.shield + unit->getHitPoints() / dpf.hp + 0.5));
			auto& scourges = scourge_target_map_[unit];
			return scourges.size() >= number_of_scourge && std::find(scourges.begin(), scourges.end(), combat_unit) == scourges.end();
		});
	}
	Unit result = nullptr;
	
	// Attack enemy based on the minimizing the amount of incoming damage
	if (result == nullptr) {
		struct Target
		{
			double incoming_dpf;
			double incoming_dpf_sum;
			Unit unit;
			Position approach_position;
			int approach_distance;
		};
		std::vector<Target> targets;
		std::map<Unit,std::pair<int,FastPosition>> approach_map = calculate_approach_distances_and_positions(combat_unit, possible_targets);
		for (auto& entry : approach_map) {
			Unit unit = entry.first;
			int approach_distance = entry.second.first;
			Position approach_position = entry.second.second;
			double incoming_dpf;
			const InformationUnit& enemy_unit = information_manager.all_units().at(unit);
			if (approach_distance == 0) {
				incoming_dpf = calculate_incoming_dpf(combat_unit, combat_unit->getPosition(), enemy_unit);
			} else {
				incoming_dpf = calculate_incoming_dpf(combat_unit, approach_position, enemy_unit);
			}
			if (approach_distance == 0 || (approach_distance >= 0 && incoming_dpf > 0.0)) {
				targets.push_back(Target{incoming_dpf, 0.0, unit, approach_position, approach_distance});
			}
		}
		double incoming_dpf_sum = calculate_incoming_dpf_sum(combat_unit, combat_unit->getPosition());
		for (auto& target : targets) {
			if (target.approach_distance > 0) {
				double destination_incoming_dpf_sum = calculate_incoming_dpf_sum(combat_unit, target.approach_position);
				target.incoming_dpf_sum = std::max(destination_incoming_dpf_sum, incoming_dpf_sum);
			} else {
				target.incoming_dpf_sum = incoming_dpf_sum;
			}
		}
		
		CombatUnitTarget& combat_unit_target = combat_unit_targets_[combat_unit];
		Unit existing_target = combat_unit_target.unit;
		int existing_target_frame = combat_unit_target.last_switch_frame;
		
		auto result_target = smallest_priority(targets, [combat_unit,existing_target,existing_target_frame](Target target){
			Unit unit = target.unit;
			double travel_damage = 0.0;
			int latency_frames = (unit != existing_target) ? Broodwar->getRemainingLatencyFrames() : std::max(0, Broodwar->getRemainingLatencyFrames() - (Broodwar->getFrameCount() - existing_target_frame));
			if (target.approach_distance > 0) {
				double travel_frames = target.approach_distance / Broodwar->self()->topSpeed(combat_unit->getType());
				travel_frames += latency_frames;
				travel_damage = target.incoming_dpf_sum * travel_frames;
			}
			double kill_frames = calculate_kill_time(combat_unit, unit);
			if (target.approach_distance == 0) kill_frames += latency_frames;
			double expected_incoming_damage = kill_frames * (target.incoming_dpf_sum - target.incoming_dpf);
			return std::make_tuple(is_low_priority_target(unit),
								   combat_unit->getType() == UnitTypes::Protoss_Carrier && target.approach_distance > 0,
								   travel_damage + expected_incoming_damage,
								   unit != existing_target,
								   target_tie_breaker(combat_unit, unit));
		}, Target{0.0, 0.0, nullptr} );
		result = result_target.unit;
	}
	
	// Attack closest enemy
	if (result == nullptr) {
		result = smallest_priority(possible_targets, [combat_unit](Unit unit){
			return std::make_tuple(is_low_priority_target(unit),
								   combat_unit->getDistance(unit),
								   target_tie_breaker(combat_unit, unit));
		});
	}
	
	// When attacking a wall, attack the building with the least health points and largest perimeter
	if (result != nullptr && connectivity_grid.is_wall_building(result)) {
		std::vector<Unit> wall_buildings;
		for (auto& unit : possible_targets) if (connectivity_grid.is_wall_building(unit)) wall_buildings.push_back(unit);
		if (!wall_buildings.empty()) {
			result = smallest_priority(wall_buildings, [component,combat_unit](Unit unit) {
				return std::make_tuple(unit->getHitPoints() + unit->getShields(),
									   -connectivity_grid.wall_building_perimeter(unit, component),
									   target_tie_breaker(combat_unit, unit));
			});
		}
	}
	
	result = replace_by_repairing_scv(combat_unit, result);
	
	// (melee/wraith/vulture/marine only) When attacking a building that can not attack the combat unit, attack a gathering worker in the same area instead
	if ((is_melee(combat_unit->getType()) ||
		 combat_unit->getType() == UnitTypes::Terran_Wraith ||
		 combat_unit->getType() == UnitTypes::Terran_Vulture ||
		 combat_unit->getType() == UnitTypes::Terran_Marine) &&
		result != nullptr && result->getType().isBuilding() && !can_attack(result, combat_unit) && !unit_in_safe_location(result)) {
		if (combat_unit->getOrderTarget() != nullptr && combat_unit->getOrderTarget()->getType().isWorker()) {
			result = combat_unit->getOrderTarget();
		} else {
			std::vector<Unit> targets;
			const BWEM::Area* area = area_at(combat_unit->getPosition());
			for (auto& unit : possible_targets) {
				if (unit->getType().isWorker() &&
					area == information_manager.all_units().at(unit).area) targets.push_back(unit);
			}
			Unit target = smallest_priority(targets, [combat_unit](Unit unit){
				return std::make_tuple(!unit->isGatheringMinerals() && !unit->isGatheringMinerals(),
									   combat_unit->getDistance(unit),
									   target_tie_breaker(combat_unit, unit));
			});
			if (target != nullptr) result = target;
		}
	}
	
	return result;
}

Unit MicroManager::select_enemy_unit_air_to_air(Unit combat_unit,const std::vector<Unit>& enemy_units)
{
	std::vector<Unit> possible_targets;
	for (auto& unit : enemy_units) {
		if (!unit->isStasised() && can_attack(combat_unit, unit)) possible_targets.push_back(unit);
	}
	Unit result = nullptr;
	
	// Attack enemy that is in range, that can be killed the fastest
	if (result == nullptr) {
		std::vector<Unit> can_attack_targets;
		for (auto& unit : possible_targets) {
			if (can_attack_in_range_with_prediction(combat_unit, unit)) {
				can_attack_targets.push_back(unit);
			}
		}
		result = smallest_priority(can_attack_targets, [combat_unit](Unit unit){
			bool not_already_has_target = !unit_has_target(combat_unit, unit);
			double kill_frames = calculate_kill_time(combat_unit, unit);
			return std::make_tuple(is_low_priority_target(unit),
								   kill_frames,
								   target_tie_breaker(combat_unit, unit));
		});
	}
	
	// Attack closest enemy
	if (result == nullptr) {
		std::vector<Unit> reachable_targets;
		
		auto& threat_component_grid = threat_grid.component_grid(combat_unit->getType());
		FastTilePosition start_tile_position = threat_component_grid.to_tile_position(combat_unit->getPosition());
		std::set<int> start_components;
		int start_component = threat_component_grid.component(start_tile_position);
		if (start_component == 0) {
			for (auto delta : { FastTilePosition{1, 0}, FastTilePosition{-1, 0}, FastTilePosition{0, -1}, FastTilePosition{0, 1},
				FastTilePosition{1, 1}, FastTilePosition{-1, 1}, FastTilePosition{-1, -1}, FastTilePosition{1, -1} }) {
					int component = threat_component_grid.component(start_tile_position + delta);
					if (component != 0) start_components.insert(component);
				}
		} else {
			start_components.insert(start_component);
		}
		
		if (!start_components.empty()) {
			int max_range = weapon_max_range(combat_unit, true);
			int max_tile_delta = 1 + max_range / 32;
			
			auto const can_hit_target = [combat_unit,max_range,max_tile_delta,&start_components,&threat_component_grid](Unit enemy_unit){
				FastPosition position = enemy_unit->getPosition();
				FastTilePosition tile_position = threat_component_grid.to_tile_position(position);
				for (int dy = -max_tile_delta; dy <= max_tile_delta; dy++) {
					for (int dx = -max_tile_delta; dx <= max_tile_delta; dx++) {
						FastTilePosition candidate_tile_position = tile_position + FastTilePosition(dx, dy);
						if (candidate_tile_position.isValid()) {
							int candidate_component = threat_component_grid.component(candidate_tile_position);
							if (contains(start_components, candidate_component)) {
								FastPosition candidate_position = threat_component_grid.to_position(candidate_tile_position);
								int distance = calculate_distance(combat_unit->getType(), candidate_position, enemy_unit->getType(), enemy_unit->getPosition());
								if (distance < max_range) {
									return true;
								}
							}
						}
					}
				}
				return false;
			};
			
			for (Unit enemy_unit : possible_targets) {
				if (can_hit_target(enemy_unit)) {
					reachable_targets.push_back(enemy_unit);
				}
			}
		}
		
		result = smallest_priority(reachable_targets, [combat_unit](Unit unit){
			return std::make_tuple(is_low_priority_target(unit),
								   combat_unit->getDistance(unit),
								   target_tie_breaker(combat_unit, unit));
		});
	}
	
	return result;
}

int MicroManager::target_tie_breaker(Unit combat_unit,Unit target_unit)
{
	int result;
	if (combat_unit->getOrderTarget() == target_unit) {
		result = INT_MIN;
	} else {
		result = target_unit->getID();
	}
	return result;
}

double MicroManager::calculate_kill_time(Unit combat_unit,Unit enemy_unit)
{
	double hit_chance = calculate_chance_to_hit(combat_unit, enemy_unit);
	if (hit_chance == 0.0) return INFINITY;
	DPF dpf = calculate_damage_per_frame(combat_unit, enemy_unit);
	if (!dpf) return INFINITY;
	return (enemy_unit->getShields() / dpf.shield + enemy_unit->getHitPoints() / dpf.hp) / hit_chance;
}

double MicroManager::calculate_incoming_dpf_sum(Unit combat_unit,Position position)
{
	double result = 0.0;
	for (auto& enemy_unit : information_manager.enemy_units()) {
		result += calculate_incoming_dpf(combat_unit, position, *enemy_unit);
	}
	return result;
}

double MicroManager::calculate_incoming_dpf(Unit combat_unit,Position position,const InformationUnit& enemy_unit)
{
	if (enemy_unit.type == UnitTypes::Terran_Medic) return calculate_incoming_dpf_for_medic(combat_unit, position, enemy_unit);
	if (is_spellcaster(enemy_unit.type)) {
		int distance = calculate_distance(combat_unit->getType(), position, enemy_unit.type, enemy_unit.position);
		if (distance <= offense_max_range(enemy_unit.type, enemy_unit.player, combat_unit->isFlying()) &&
			is_spellcaster(enemy_unit.type)) return 100.0;
		return 0.0;
	}
	if (enemy_unit.type == UnitTypes::Protoss_Shuttle &&
		information_manager.enemy_seen(UnitTypes::Protoss_Reaver) &&
		can_attack_in_range_at_positions(UnitTypes::Protoss_Reaver, enemy_unit.position, enemy_unit.player, combat_unit->getType(), position)) {
		DPF dpf = calculate_damage_per_frame(UnitTypes::Protoss_Reaver, enemy_unit.player, combat_unit->getType(), combat_unit->getPlayer());
		double base_dpf_value = (combat_unit->getShields() > 5) ? dpf.shield : dpf.hp;
		return 0.99 * calculate_splash_factor(UnitTypes::Protoss_Reaver, combat_unit->isFlying()) * base_dpf_value;
	}
	if (!can_attack(enemy_unit.type, combat_unit->isFlying())) return 0.0;
	if (!is_suicidal_with_target(enemy_unit) &&
		!can_attack_in_range_at_positions(enemy_unit.type, enemy_unit.position, enemy_unit.player, combat_unit->getType(), position)) {
		if (enemy_unit.type.isWorker() || is_suicidal(enemy_unit.type)) {
			bool can_attack_enemy;
			if (combat_unit->getPosition() == position && enemy_unit.unit->exists()) {
				can_attack_enemy = can_attack_in_range_with_prediction(combat_unit, enemy_unit.unit);
			} else {
				can_attack_enemy = can_attack_in_range_at_positions(combat_unit->getType(), position, combat_unit->getPlayer(), enemy_unit.type, enemy_unit.position);
			}
			if (can_attack_enemy) return 1e-6;
		}
		return 0.0;
	}
	int bunker_marines_loaded = 0;
	if (enemy_unit.type == UnitTypes::Terran_Bunker) {
		bunker_marines_loaded = information_manager.bunker_marines_loaded(enemy_unit.unit);
	}
	DPF dpf = calculate_damage_per_frame(enemy_unit.type, enemy_unit.player, combat_unit->getType(), combat_unit->getPlayer(), bunker_marines_loaded);
	double base_dpf_value = (combat_unit->getShields() > 5) ? dpf.shield : dpf.hp;
	double result;
	if (combat_unit->getPosition() == position && enemy_unit.unit->exists()) {
		result = (base_dpf_value *
				  calculate_splash_factor(enemy_unit.unit, combat_unit) *
				  calculate_chance_to_hit(enemy_unit.unit, combat_unit));
	} else {
		result = (base_dpf_value *
				  calculate_splash_factor(enemy_unit.type, combat_unit->isFlying()) *
				  calculate_chance_to_hit(enemy_unit.type, enemy_unit.position, position));
	}
	if (enemy_unit.type == UnitTypes::Protoss_Photon_Cannon &&
		!enemy_unit.is_completed()) {
		int duration = enemy_unit.type.buildTime() + building_extra_frames(enemy_unit.type);
		double fraction = clamp(0.0, double(Broodwar->getFrameCount() - enemy_unit.start_frame) / double(duration), 1.0);
		result *= fraction;
	}
	return result;
}

double MicroManager::calculate_incoming_dpf_for_medic(Unit combat_unit,Position position,const InformationUnit& enemy_medic_unit)
{
	const EnemyCluster* cluster = tactics_manager.cluster_for_unit(enemy_medic_unit.unit);
	if (cluster == nullptr) return 0.0;
	
	double dpf_sum = 0.0;
	
	for (auto& enemy_unit : cluster->units()) {
		if (enemy_unit->unit != enemy_medic_unit.unit &&
			calculate_distance(enemy_unit->type, enemy_unit->position, UnitTypes::Terran_Medic, enemy_medic_unit.position) <= kMedicHealRange) {
			if (enemy_unit->type == UnitTypes::Terran_Medic) return 0.0;
			if (enemy_unit->type.isOrganic()) dpf_sum += calculate_incoming_dpf(combat_unit, position, *enemy_unit);
		}
	}
	
	return dpf_sum;
}

bool MicroManager::is_suicidal_with_target(const InformationUnit& enemy_unit)
{
	return enemy_unit.unit->exists() && is_suicidal(enemy_unit.type) && enemy_unit.unit->getOrderTarget() != nullptr;
}

double MicroManager::calculate_chance_to_hit(Unit attacking_unit,Unit defending_unit)
{
	double result;
	
	if (is_melee(attacking_unit->getType()) ||
		attacking_unit->isFlying() ||
		attacking_unit->getType() == UnitTypes::Protoss_Reaver) {
		result = 1.0;
	} else if (defending_unit->isUnderDarkSwarm()) {
		result = 0.0;
	} else if (Broodwar->getGroundHeight(attacking_unit->getTilePosition()) <
			   Broodwar->getGroundHeight(defending_unit->getTilePosition())) {
		result = 0.53125;
	} else {
		result = 0.99609375;
	}
	
	return result;
}

double MicroManager::calculate_chance_to_hit(UnitType attacking_unit_type,Position attacking_unit_position,Position defending_unit_position)
{
	double result;
	
	if (is_melee(attacking_unit_type)) {
		result = 1.0;
	} else if (Broodwar->getGroundHeight(TilePosition(attacking_unit_position)) <
			   Broodwar->getGroundHeight(TilePosition(defending_unit_position))) {
		result = 0.53125;
	} else {
		result = 0.99609375;
	}
	
	return result;
}

double MicroManager::calculate_splash_factor(Unit attacking_unit,Unit defending_unit)
{
	return calculate_splash_factor(attacking_unit->getType(), defending_unit->isFlying());
}

double MicroManager::calculate_splash_factor(UnitType attacking_unit_type,bool defending_unit_flying)
{
	WeaponType weapon_type;
	if (attacking_unit_type == UnitTypes::Protoss_Reaver) {
		weapon_type = WeaponTypes::Scarab;
	} else {
		weapon_type = defending_unit_flying ? attacking_unit_type.airWeapon() : attacking_unit_type.groundWeapon();
	}
	
	double factor = 1.0;
	if (weapon_type != WeaponTypes::None && weapon_type != WeaponTypes::Unknown) {
		if (weapon_type.explosionType() == ExplosionTypes::Radial_Splash) factor = 2.5;
		else if (weapon_type.explosionType() == ExplosionTypes::Enemy_Splash) factor = 1.5;
	}
	return factor;
}

std::map<Unit,std::pair<int,FastPosition>> MicroManager::calculate_approach_distances_and_positions(Unit combat_unit,std::vector<Unit>& enemy_units)
{
	std::map<Unit,std::pair<int,FastPosition>> result;
	
	if (combat_unit->isFlying()) {
		for (auto& enemy_unit : enemy_units) {
			if (can_attack_in_range_with_prediction(combat_unit, enemy_unit)) {
				result[enemy_unit] = std::make_pair(0, combat_unit->getPosition());
			} else {
				int distance = combat_unit->getDistance(enemy_unit);
				int range = weapon_max_range(combat_unit, enemy_unit->isFlying());
				int approach_distance = std::max(8, distance - range);
				double t = clamp(0.0, double(range) / double(distance), 1.0);
				Position approach_position = lever(enemy_unit->getPosition(), combat_unit->getPosition(), t);
				result[enemy_unit] = std::make_pair(approach_distance, approach_position);
			}
		}
	} else {
		if (combat_unit->getType() == UnitTypes::Protoss_Dragoon) {
			for (auto& enemy_unit : enemy_units) {
				if (!is_on_cooldown(combat_unit, enemy_unit->isFlying()) &&
					(can_attack(enemy_unit, false) || is_spellcaster(enemy_unit->getType())) &&
					can_attack_in_range_at_positions(combat_unit,
													 predict_position(combat_unit, Broodwar->getRemainingLatencyFrames()),
													 enemy_unit,
													 predict_position(enemy_unit, Broodwar->getRemainingLatencyFrames()))) {
					result[enemy_unit] = std::make_pair(0, combat_unit->getPosition());
				}
			}
		}
		
		if (result.empty()) {
			struct EnemyUnit
			{
				Unit unit;
				int span;
				FastPosition position;
			};
			
			std::vector<EnemyUnit> remaining_enemy_units;
			UnitType type = combat_unit->getType();
			for (auto& enemy_unit : enemy_units) {
				int range = weapon_max_range(combat_unit, enemy_unit->isFlying());
				int air_approach_distance = std::max(8, combat_unit->getDistance(enemy_unit) - range);
				if (air_approach_distance <= 320) {
					UnitType enemy_type = enemy_unit->getType();
					int horizontal = std::max(type.dimensionRight() + enemy_type.dimensionLeft(),
											  type.dimensionLeft() + enemy_type.dimensionRight());
					int vertical = std::max(type.dimensionUp() + enemy_type.dimensionDown(),
											type.dimensionDown() + enemy_type.dimensionUp());
					int span = 1 + range + std::max(horizontal, vertical);
					remaining_enemy_units.push_back(EnemyUnit{enemy_unit, span, enemy_unit->getPosition()});
				}
			}
			
			if (!remaining_enemy_units.empty()) {
				int max_distance = 0;
				for (auto& enemy_unit : remaining_enemy_units) {
					int range = weapon_max_range(combat_unit, enemy_unit.unit->isFlying());
					max_distance = std::max(max_distance, 320 - range);
				}
				
				FastPosition initial_position = combat_unit->getPosition();
				std::queue<FastPosition> queue;
				SparsePositionGrid<320,16,int> distances(initial_position);
				queue.push(initial_position);
				distances[initial_position] = 1;
				
				while (!queue.empty() && !remaining_enemy_units.empty()) {
					FastPosition current_position = queue.front();
					queue.pop();
					
					int current_distance = distances[current_position];
					remove_elements_in_place(remaining_enemy_units, [&result,combat_unit,current_position,current_distance](auto& enemy_unit){
						bool remove_enemy = false;
						Unit unit = enemy_unit.unit;
						int span = enemy_unit.span;
						int delta = std::max(std::abs(current_position.x - enemy_unit.position.x),
											 std::abs(current_position.y - enemy_unit.position.y));
						if (delta <= span) {
							int shot_distance = calculate_distance(combat_unit->getType(), current_position, unit->getType(), enemy_unit.position);
							int range = weapon_max_range(combat_unit, unit->isFlying());
							if (shot_distance <= range) {
								result[unit] = std::make_pair(current_distance - 1, current_position);
								remove_enemy = true;
							}
						}
						return remove_enemy;
					});
					
					for (FastPosition delta_position : { FastPosition(-16, 0), FastPosition(16, 0), FastPosition(0, -16), FastPosition(0, 16) }) {
						FastPosition next_position = current_position + delta_position;
						int& distance = distances[next_position];
						if (next_position.isValid() && distance == 0) {
							if (next_position.getApproxDistance(initial_position) <= max_distance && check_collision(combat_unit, next_position)) {
								queue.push(next_position);
								distance = current_distance + 16;
							} else {
								distance = -1;
							}
						}
					}
				}
			}
		}
	}
	
	return result;
}

Unit MicroManager::replace_by_repairing_scv(Unit combat_unit,Unit target)
{
	Unit result = target;
	
	if (result != nullptr) {
		UnitType type = result->getType();
		if (!target->isCompleted() &&
			type.isBuilding() &&
			target->getBuildUnit() != nullptr &&
			target->getBuildUnit()->getType() == UnitTypes::Terran_SCV &&
			connectivity_grid.check_reachability(combat_unit, target->getBuildUnit())) {
			result = target->getBuildUnit();
		} else {
			if (type.getRace() == Races::Terran && type.isMechanical() && !type.isWorker()) {
				key_value_vector<Unit,int> repair_scv_distances;
				for (auto& unit : harassable_enemy_units_) {
					if (unit->getType() == UnitTypes::Terran_SCV &&
						unit->isRepairing() &&
						unit->getOrderTarget() == target &&
						connectivity_grid.check_reachability(combat_unit, unit)) {
						repair_scv_distances.emplace_back(unit, combat_unit->getDistance(unit));
					}
				}
				if (repair_scv_distances.size() >= 2) result = key_with_smallest_value(repair_scv_distances);
			}
		}
	}
	
	return result;
}

bool MicroManager::storm(Unit high_templar_unit)
{
	const int range = WeaponTypes::Psionic_Storm.maxRange();
	const int radius = WeaponTypes::Psionic_Storm.outerSplashRadius();
	const int threshold = should_emergency_storm(high_templar_unit) ? kStormThresholdUnderAttack : kStormThreshold;
	Position position = high_templar_unit->getPosition();
	
	std::vector<std::pair<Unit,int>> unit_scores;
	int sum_positive_scores = 0;
	for (auto& unit : Broodwar->getAllUnits()) {
		if (!unit->exists() || !unit->isVisible() || !unit->isCompleted() || unit->isStasised()) continue;
		UnitType unit_type = unit->getType();
		if (unit_type.isBuilding()) continue;
		
		int sign;
		if (unit->getPlayer() == Broodwar->self()) {
			sign = -1;
		} else if (unit->getPlayer()->isEnemy(Broodwar->self())) {
			sign = 1;
		} else {
			continue;
		}
		
		Position unit_position = unit->getPosition();
		if (unit_position.x < position.x - range - radius - unit_type.dimensionRight() ||
			unit_position.x > position.x + range + radius + unit_type.dimensionLeft() ||
			unit_position.y < position.y - range - radius - unit_type.dimensionDown() ||
			unit_position.y > position.y + range + radius + unit_type.dimensionUp()) {
			continue;
		}
		
		int score = sign * unit_type.supplyRequired();
		unit_scores.emplace_back(unit, score);
		if (score > 0) sum_positive_scores += score;
	}
	
	key_value_vector<Position,std::pair<int,int>> scores;
	if (sum_positive_scores >= threshold) {
		std::vector<Position> existing_storms = list_existing_storm_positions();
		for (int y = position.y - range; y <= position.y + range; y += 32) {
			for (int x = position.x - range; x <= position.x + range; x += 32) {
				Position candidate = Position(x, y);
				int distance = high_templar_unit->getPosition().getApproxDistance(candidate);
				if (distance < range && !position_list_intersects(existing_storms, candidate, WeaponTypes::Psionic_Storm.outerSplashRadius())) {
					std::pair<int,int> score = storm_score(candidate, unit_scores);
					if (score.first >= threshold) scores.emplace_back(candidate, score);
				}
			}
		}
	}
	Position target = key_with_largest_value(scores, Positions::None);
	if (target.isValid()) {
		high_templar_unit->useTech(TechTypes::Psionic_Storm, target);
		int latency_frames = Broodwar->getRemainingLatencyFrames() + 24;
		tentative_storms_.push_back(TentativeStorm(high_templar_unit, Broodwar->getFrameCount() + latency_frames, target));
		return true;
	} else {
		return false;
	}
}

bool MicroManager::should_emergency_storm(Unit high_templar_unit)
{
	bool result = false;
	if (high_templar_unit->isUnderAttack()) {
		int storms_left = high_templar_unit->getEnergy() / TechTypes::Psionic_Storm.energyCost();
		if (storms_left >= 2) {
			result = true;
		} else if (storms_left >= 1) {
			double frames_to_live = calculate_frames_to_live(high_templar_unit);
			result = (frames_to_live <= Broodwar->getRemainingLatencyFrames() + 12);
		}
	}
	return result;
}

std::pair<int,int> MicroManager::storm_score(Position position,const std::vector<std::pair<Unit,int>>& unit_scores)
{
	int result = 0;
	int max_border_distance = 0;
	
	for (auto& entry : unit_scores) {
		Unit unit = entry.first;
		Position unit_position = predict_position(unit);
		int border_distance = WeaponTypes::Psionic_Storm.outerSplashRadius() - unit_position.getApproxDistance(position);
		if (border_distance >= 0) {
			result += entry.second;
			if (entry.second > 0) max_border_distance = std::max(max_border_distance, border_distance);
		}
	}
	
	return std::make_pair(result, max_border_distance);
}

bool MicroManager::scan_cloaked_unit(Unit comsat_unit)
{
	const int radius = UnitTypes::Spell_Scanner_Sweep.sightRange();
	key_value_vector<Position,int> cloaked_units;
	for (auto& unit : all_enemy_units_) {
		if (not_cloaked(unit) || unit->isStasised()) continue;
		int distance = information_manager.all_units().at(unit).base_distance;
		cloaked_units.emplace_back(unit->getPosition(), distance);
	}
	std::sort(cloaked_units.begin(), cloaked_units.end(), [](const auto& a,const auto& b){
		return a.second < b.second;
	});
	if (opponent_model.enemy_race() == Races::Terran) {
		Position worker_need_detection_position = determine_worker_need_detection_position();
		if (worker_need_detection_position.isValid()) {
			cloaked_units.emplace_back(worker_need_detection_position, INT_MAX);
		}
	}
	std::vector<Position> existing_scan_positions = list_existing_scan_positions();
	for (auto& entry : cloaked_units) {
		Position candidate = entry.first;
		if (!position_list_intersects(existing_scan_positions, candidate, radius)) {
			comsat_unit->useTech(TechTypes::Scanner_Sweep, candidate);
			int latency_frames = Broodwar->getRemainingLatencyFrames() + 12;
			tentative_scans_.emplace_back(comsat_unit, Broodwar->getFrameCount() + latency_frames, candidate);
			return true;
		}
	}
	return false;
}

bool MicroManager::scan_base(Unit comsat_unit)
{
	Position position = pick_air_scout_location();
	if (position.isValid() &&
		!Broodwar->isVisible(TilePosition(position)) &&
		!position_list_intersects(list_existing_scan_positions(), position, UnitTypes::Spell_Scanner_Sweep.sightRange())) {
		comsat_unit->useTech(TechTypes::Scanner_Sweep, position);
		int latency_frames = Broodwar->getRemainingLatencyFrames() + 12;
		tentative_scans_.emplace_back(comsat_unit, Broodwar->getFrameCount() + latency_frames, position);
		return true;
	}
	return false;
}

bool MicroManager::irradiate(Unit science_vessel_unit)
{
	const int range = WeaponTypes::Irradiate.maxRange();
	std::set<Unit> units_being_irradiated;
	for (auto& tentative_irradiate : tentative_irradiates_) units_being_irradiated.insert(tentative_irradiate.target);
	key_value_vector<Unit,int> target_distances;
	for (auto& unit : all_enemy_units_) {
		int distance = science_vessel_unit->getDistance(unit);
		if (distance > range ||
			unit->isIrradiated() ||
			unit->isStasised() ||
			unit->getHitPoints() < 100 ||
			units_being_irradiated.count(unit) > 0) continue;
		UnitType type = unit->getType();
		if (type == UnitTypes::Zerg_Devourer ||
			type == UnitTypes::Zerg_Guardian ||
			type == UnitTypes::Zerg_Mutalisk ||
			type == UnitTypes::Zerg_Lurker ||
			type == UnitTypes::Zerg_Defiler ||
			type == UnitTypes::Zerg_Ultralisk) {
			target_distances.emplace_back(unit, distance);
		}
	}
	Unit target = key_with_smallest_value(target_distances);
	if (target != nullptr) {
		science_vessel_unit->useTech(TechTypes::Irradiate, target);
		int latency_frames = Broodwar->getRemainingLatencyFrames() + 12;
		tentative_irradiates_.emplace_back(science_vessel_unit, Broodwar->getFrameCount() + latency_frames, target);
		return true;
	} else {
		return false;
	}
}

bool MicroManager::yamato(Unit battle_cruiser_unit)
{
	const int range = WeaponTypes::Yamato_Gun.maxRange();
	std::set<Unit> units_being_yamatoed;
	for (auto& tentative_yamato : tentative_yamatoes_) units_being_yamatoed.insert(tentative_yamato.target);
	key_value_vector<Unit,int> target_distances;
	for (auto& unit : all_enemy_units_) {
		UnitType type = unit->getType();
		int distance = battle_cruiser_unit->getDistance(unit);
		if (distance > range ||
			unit->isIrradiated() ||
			unit->isStasised() ||
			unit->getHitPoints() < std::min(3 * (type.maxHitPoints() + type.maxShields()) / 4, 200) ||
			contains(units_being_yamatoed, unit)) continue;
		if (type == UnitTypes::Zerg_Defiler ||
			type == UnitTypes::Zerg_Ultralisk ||
			(type.isBuilding() && type.getRace() == Races::Zerg && unit->getHitPoints() < 260) ||
			is_siege_tank(type) ||
			type == UnitTypes::Terran_Goliath ||
			type == UnitTypes::Terran_Battlecruiser) {
			target_distances.emplace_back(unit, distance);
		}
	}
	Unit target = key_with_smallest_value(target_distances);
	if (target != nullptr) {
		battle_cruiser_unit->useTech(TechTypes::Yamato_Gun, target);
		int latency_frames = Broodwar->getRemainingLatencyFrames() + 12;
		tentative_yamatoes_.emplace_back(battle_cruiser_unit, Broodwar->getFrameCount() + latency_frames, target);
		return true;
	} else {
		return false;
	}
}

bool MicroManager::lockdown(Unit ghost_unit)
{
	bool allow_any_target = should_lockdown_any_target(ghost_unit);
	const int range = WeaponTypes::Lockdown.maxRange();
	std::set<Unit> units_being_lockdowned;
	for (auto& tentative_lockdown : tentative_lockdowns_) units_being_lockdowned.insert(tentative_lockdown.target);
	key_value_vector<Unit,int> target_hitpoints;
	for (auto& unit : all_enemy_units_) {
		if (ghost_unit->getDistance(unit) > range ||
			is_disabled(unit) ||
			contains(units_being_lockdowned, unit)) {
			continue;
		}
		UnitType type = unit->getType();
		bool valid_target = false;
		if (type == UnitTypes::Protoss_Arbiter ||
			type == UnitTypes::Protoss_Carrier ||
			type == UnitTypes::Protoss_Shuttle) {
			valid_target = true;
		} else if (allow_any_target &&
			!type.isBuilding() &&
			type.isMechanical() &&
			type != UnitTypes::Protoss_Interceptor) {
			valid_target = true;
		}
		if (valid_target) {
			int hitpoints = 3 * unit->getHitPoints() + unit->getShields();
			target_hitpoints.emplace_back(unit, hitpoints);
		}
	}
	Unit target = key_with_largest_value(target_hitpoints);
	if (target != nullptr) {
		ghost_unit->useTech(TechTypes::Lockdown, target);
		int latency_frames = Broodwar->getRemainingLatencyFrames() + 12;
		tentative_lockdowns_.emplace_back(ghost_unit, Broodwar->getFrameCount() + latency_frames, target);
		return true;
	} else {
		return false;
	}
}

bool MicroManager::should_lockdown_any_target(Unit ghost_unit)
{
	bool result = false;
	if (ghost_unit->getEnergy() >= (3 * TechTypes::Lockdown.energyCost() / 2)) {
		result = true;
	} else if (calculate_frames_to_live(ghost_unit) <= Broodwar->getRemainingLatencyFrames() + 12) {
		result = true;
	}
	return result;
}

bool MicroManager::stasis(Unit arbiter_unit)
{
	const int range = WeaponTypes::Stasis_Field.maxRange();
	const int radius = kStasisRadius;
	const int threshold = kStasisThreshold;
	Position position = arbiter_unit->getPosition();
	
	std::vector<std::pair<Unit,int>> unit_scores;
	for (auto& unit : Broodwar->getAllUnits()) {
		if (!unit->exists() || !unit->isVisible() || !unit->isCompleted() || unit->isStasised() || unit->isBurrowed()) continue;
		UnitType unit_type = unit->getType();
		if (unit_type.isBuilding() || unit_type.isWorker() || unit_type == UnitTypes::Zerg_Overlord || is_low_priority_target(unit)) continue;
		
		int score;
		if (unit->getPlayer() == Broodwar->self()) {
			score = -1;
		} else if (unit->getPlayer()->isEnemy(Broodwar->self())) {
			score = (is_siege_tank(unit_type) || unit_type == UnitTypes::Terran_Science_Vessel) ? 2 : 1;
		} else {
			continue;
		}
		
		Position unit_position = unit->getPosition();
		if (unit_position.x < position.x - range - radius - unit_type.dimensionRight() ||
			unit_position.x > position.x + range + radius + unit_type.dimensionLeft() ||
			unit_position.y < position.y - range - radius - unit_type.dimensionDown() ||
			unit_position.y > position.y + range + radius + unit_type.dimensionUp()) {
		}
		
		unit_scores.emplace_back(unit, score);
	}
	
	key_value_vector<Position,std::tuple<int,int,int>> scores;
	std::vector<Position> existing_storms = list_existing_storm_positions();
	for (int y = position.y - range; y <= position.y + range; y += 32) {
		for (int x = position.x - range; x <= position.x + range; x += 32) {
			Position candidate = Position(x, y);
			int distance = arbiter_unit->getPosition().getApproxDistance(candidate);
			if (distance < range && !position_list_intersects(existing_storms, candidate, WeaponTypes::Psionic_Storm.outerSplashRadius())) {
				auto score = stasis_score(position, candidate, unit_scores);
				if (std::get<0>(score) >= threshold) {
					scores.emplace_back(candidate, score);
				}
			}
		}
	}
	Position target = key_with_largest_value(scores, Positions::None);
	if (target.isValid()) {
		arbiter_unit->useTech(TechTypes::Stasis_Field, target);
		int latency_frames = Broodwar->getRemainingLatencyFrames() + 12;
		tentative_stasises_.push_back(TentativeStasis(arbiter_unit, Broodwar->getFrameCount() + latency_frames, target));
		return true;
	} else {
		return false;
	}
}

std::tuple<int,int,int> MicroManager::stasis_score(Position arbiter_position,Position position,const std::vector<std::pair<Unit,int>>& unit_scores)
{
	const int radius = kStasisRadius;
	int result = 0;
	int min_distance = INT_MAX;
	int max_border_distance = 0;
	
	for (auto [unit,score] : unit_scores) {
		Position unit_position = predict_position(unit);
		int border_distance = radius - chebyshev_norm(edge_to_point_delta(unit->getType(), unit_position, position));
		if (border_distance >= 0) {
			if (score < 0) return std::make_tuple(INT_MIN, INT_MIN, INT_MIN);
			min_distance = std::min(min_distance, arbiter_position.getApproxDistance(unit_position));
			max_border_distance = std::max(max_border_distance, border_distance);
			result += score;
		}
	}
	
	return std::make_tuple(result, min_distance, max_border_distance);
}

bool MicroManager::dark_swarm(Unit defiler_unit)
{
	const int range = WeaponTypes::Dark_Swarm.maxRange();
	const int radius = UnitTypes::Spell_Dark_Swarm.width() / 2;
	const int threshold = should_emergency_dark_swarm(defiler_unit) ? kDarkSwarmThresholdUnderAttack : kDarkSwarmThreshold;
	Position position = defiler_unit->getPosition();
	
	std::vector<std::pair<Unit,int>> unit_scores;
	int sum_positive_scores = 0;
	bool allied_fighting_exists = false;
	for (auto& unit : Broodwar->getAllUnits()) {
		if (!unit->exists() || !unit->isVisible() || !unit->isCompleted() || unit->isStasised()) continue;
		UnitType unit_type = unit->getType();
		
		int factor;
		if (unit->getPlayer()->isAlly(Broodwar->self()) &&
			!unit->isFlying()) {
			factor = 1;
		} else if (unit->getPlayer()->isEnemy(Broodwar->self()) &&
				   (is_melee(unit_type) ||
					unit_type == UnitTypes::Zerg_Lurker ||
					unit_type == UnitTypes::Terran_Firebat)) {
			factor = -2;
		} else {
			continue;
		}
		
		Position unit_position = unit->getPosition();
		if (unit_position.x < position.x - range - radius - unit_type.dimensionRight() ||
			unit_position.x > position.x + range + radius + unit_type.dimensionLeft() ||
			unit_position.y < position.y - range - radius - unit_type.dimensionDown() ||
			unit_position.y > position.y + range + radius + unit_type.dimensionUp()) {
			continue;
		}
		
		int score = factor * unit_type.supplyRequired();
		unit_scores.emplace_back(unit, score);
		if (score > 0) sum_positive_scores += score;
		if (unit->getPlayer()->isAlly(Broodwar->self()) &&
			unit_is_fighting(unit)) {
			allied_fighting_exists = true;
		}
	}
	
	key_value_vector<Position,std::pair<int,int>> scores;
	if (sum_positive_scores > 0 && allied_fighting_exists) {
		std::vector<Position> existing_dark_swarms = list_existing_dark_swarm_positions();
		for (int y = position.y - range; y <= position.y + range; y += 32) {
			for (int x = position.x - range; x <= position.x + range; x += 32) {
				Position candidate = Position(x, y);
				int distance = defiler_unit->getPosition().getApproxDistance(candidate);
				if (distance < range && !position_list_intersects_square(existing_dark_swarms, candidate, radius)) {
					std::pair<int,int> score = dark_swarm_score(candidate, unit_scores);
					if (score.first >= threshold) scores.emplace_back(candidate, score);
				}
			}
		}
	}
	Position target = key_with_largest_value(scores, Positions::None);
	if (target.isValid()) {
		defiler_unit->useTech(TechTypes::Dark_Swarm, target);
		int latency_frames = Broodwar->getRemainingLatencyFrames() + 24;
		tentative_dark_swarms_.push_back(TentativeDarkSwarm(defiler_unit, Broodwar->getFrameCount() + latency_frames, target));
		return true;
	} else {
		return false;
	}
}

bool MicroManager::should_emergency_dark_swarm(Unit defiler_unit)
{
	bool result = false;
	if (defiler_unit->isUnderAttack()) {
		int swarms_left = defiler_unit->getEnergy() / TechTypes::Dark_Swarm.energyCost();
		if (swarms_left >= 2) {
			result = true;
		} else if (swarms_left >= 1) {
			double frames_to_live = calculate_frames_to_live(defiler_unit);
			result = (frames_to_live <= Broodwar->getRemainingLatencyFrames() + 12);
		}
	}
	return result;
}

std::pair<int,int> MicroManager::dark_swarm_score(Position position,const std::vector<std::pair<Unit,int>>& unit_scores)
{
	const int radius = UnitTypes::Spell_Dark_Swarm.width() / 2;
	int result = 0;
	int max_border_distance = 0;
	
	bool allied_fighting_exists = false;
	for (auto [unit,score] : unit_scores)
	{
		Position unit_position = predict_position(unit);
		int border_distance = radius - chebyshev_norm(edge_to_point_delta(unit->getType(), unit_position, position));
		if (border_distance >= 0) {
			result += score;
			if (score > 0) max_border_distance = std::max(max_border_distance, border_distance);
			if (unit->getPlayer()->isAlly(Broodwar->self()) &&
				unit_is_fighting(unit)) {
				allied_fighting_exists = true;
			}
		}
	}
	
	if (!allied_fighting_exists) return std::make_pair(INT_MIN, INT_MIN);
	return std::make_pair(result, max_border_distance);
}

bool MicroManager::unit_is_fighting(Unit unit)
{
	bool result = false;
	if (unit->isUnderAttack()) {
		result = true;
	} else if (unit->isAttacking()) {
		const auto target_can_attack = [](Unit target){
			return target != nullptr && target->exists() && can_attack(target);
		};
		result = (target_can_attack(unit->getOrderTarget()) || target_can_attack(unit->getTarget()));
	}
	return result;
}

bool MicroManager::plague(Unit defiler_unit)
{
	const int range = WeaponTypes::Plague.maxRange();
	const int radius = kPlagueRadius;
	const int threshold = should_emergency_plague(defiler_unit) ? kPlagueThreshold : kPlagueThresholdUnderAttack;
	Position position = defiler_unit->getPosition();
	
	std::vector<std::pair<Unit,int>> unit_scores;
	int sum_positive_scores = 0;
	for (auto& unit : Broodwar->getAllUnits()) {
		if (!unit->exists() || !unit->isVisible() || !unit->isCompleted() || unit->isStasised() || unit->isBurrowed()) continue;
		if (unit == defiler_unit) continue;
		UnitType unit_type = unit->getType();
		
		if (unit->getPlayer()->isNeutral()) continue;
		
		Position unit_position = unit->getPosition();
		if (unit_position.x < position.x - range - radius - unit_type.dimensionRight() ||
			unit_position.x > position.x + range + radius + unit_type.dimensionLeft() ||
			unit_position.y < position.y - range - radius - unit_type.dimensionDown() ||
			unit_position.y > position.y + range + radius + unit_type.dimensionUp()) {
			continue;
		}
		
		if (unit->getPlayer()->isAlly(Broodwar->self())) {
			unit_scores.emplace_back(unit, -1);
		} else if (!unit_type.isBuilding() || can_attack(unit)) {
			int score = std::min(unit->getHitPoints() - 1, kPlagueMaxDamage);
			if (unit->isPlagued()) {
				int damage_left = int((unit->getPlagueTimer() / 8.0) * 3.95 + 0.5);
				int score_left = std::min(unit->getHitPoints() - 1, damage_left);
				score -= score_left;
			}
			if (score > 0) {
				sum_positive_scores += score;
				unit_scores.emplace_back(unit, score);
			}
		}
	}
	
	key_value_vector<Position,std::pair<int,int>> scores;
	if (sum_positive_scores >= threshold) {
		for (int y = position.y - range; y <= position.y + range; y += 32) {
			for (int x = position.x - range; x <= position.x + range; x += 32) {
				Position candidate = Position(x, y);
				int distance = defiler_unit->getPosition().getApproxDistance(candidate);
				if (distance < range) {
					std::pair<int,int> score = plague_score(candidate, unit_scores);
					if (score.first >= threshold) scores.emplace_back(candidate, score);
				}
			}
		}
	}
	Position target = key_with_largest_value(scores, Positions::None);
	if (target.isValid()) {
		defiler_unit->useTech(TechTypes::Plague, target);
		int latency_frames = Broodwar->getRemainingLatencyFrames() + 24;
		tentative_plagues_.push_back(TentativePlague(defiler_unit, Broodwar->getFrameCount() + latency_frames, target));
		return true;
	} else {
		return false;
	}
}

std::pair<int,int> MicroManager::plague_score(Position position,const std::vector<std::pair<Unit,int>>& unit_scores)
{
	const int radius = kPlagueRadius;
	int result = 0;
	int max_border_distance = 0;
	
	for (auto [unit,score] : unit_scores)
	{
		Position unit_position = predict_position(unit);
		int border_distance = radius - chebyshev_norm(edge_to_point_delta(unit->getType(), unit_position, position));
		if (border_distance >= 0) {
			if (score < 0) {
				return std::make_pair(INT_MIN, INT_MIN);
			}
			if (score > 0) max_border_distance = std::max(max_border_distance, border_distance);
			result += score;
		}
	}
	
	return std::make_pair(result, max_border_distance);
}

bool MicroManager::should_emergency_plague(Unit defiler_unit)
{
	bool result = false;
	if (defiler_unit->isUnderAttack()) {
		double frames_to_live = calculate_frames_to_live(defiler_unit);
		result = (frames_to_live <= Broodwar->getRemainingLatencyFrames() + 12);
	}
	return result;
}

bool MicroManager::emp(Unit science_vessel_unit)
{
	const int range = WeaponTypes::EMP_Shockwave.maxRange();
	const int radius = WeaponTypes::EMP_Shockwave.outerSplashRadius();
	bool emergency = should_emergency_emp(science_vessel_unit);
	Position position = science_vessel_unit->getPosition();
	
	std::vector<std::tuple<Unit,int,bool>> unit_scores;
	bool positive_score = false;
	for (auto& unit : Broodwar->getAllUnits()) {
		if (!unit->exists() || !unit->isVisible() || !unit->isCompleted() || unit->isStasised() || unit->isBurrowed()) continue;
		if (unit == science_vessel_unit) continue;
		UnitType unit_type = unit->getType();
		
		if (unit->getPlayer()->isNeutral()) continue;
		
		Position unit_position = unit->getPosition();
		if (unit_position.x < position.x - range - radius - unit_type.dimensionRight() ||
			unit_position.x > position.x + range + radius + unit_type.dimensionLeft() ||
			unit_position.y < position.y - range - radius - unit_type.dimensionDown() ||
			unit_position.y > position.y + range + radius + unit_type.dimensionUp()) {
			continue;
		}
		
		if (unit->getPlayer()->isAlly(Broodwar->self()) &&
			(unit->getType().maxEnergy() > 0 ||
			 unit->getType().maxShields() > 0)) {
			unit_scores.emplace_back(unit, -1, false);
		} else {
			int score = 0;
			bool high_value_target = false;
			if ((unit_type == UnitTypes::Protoss_Arbiter ||
				 unit_type == UnitTypes::Protoss_High_Templar ||
				 unit_type == UnitTypes::Protoss_Archon ||
				 unit_type == UnitTypes::Protoss_Dark_Archon) &&
				unit->getShields() >= unit_type.maxShields() / 2) {
				score = 10;
				high_value_target = true;
			} else if (unit->getShields() >= 10) {
				score = 1;
			}
			
			if (score > 0) {
				positive_score = true;
				unit_scores.emplace_back(unit, score, high_value_target);
			}
		}
	}
	
	key_value_vector<Position,std::pair<int,int>> scores;
	if (positive_score) {
		for (int y = position.y - range; y <= position.y + range; y += 32) {
			for (int x = position.x - range; x <= position.x + range; x += 32) {
				Position candidate = Position(x, y);
				int distance = science_vessel_unit->getPosition().getApproxDistance(candidate);
				if (distance < range) {
					std::pair<int,int> score = emp_score(candidate, unit_scores, emergency);
					if (score.first > 0) scores.emplace_back(candidate, score);
				}
			}
		}
	}
	Position target = key_with_largest_value(scores, Positions::None);
	if (target.isValid()) {
		science_vessel_unit->useTech(TechTypes::EMP_Shockwave, target);
		int latency_frames = Broodwar->getRemainingLatencyFrames() + 24;
		tentative_emps_.emplace_back(science_vessel_unit, Broodwar->getFrameCount() + latency_frames, target);
		return true;
	} else {
		return false;
	}
}

std::pair<int,int> MicroManager::emp_score(Position position,const std::vector<std::tuple<Unit,int,bool>> unit_scores,bool emergency)
{
	const int radius = WeaponTypes::EMP_Shockwave.outerSplashRadius();
	int result = 0;
	int max_border_distance = 0;
	bool has_high_value_target = false;
	
	for (auto [unit,score,high_value_target] : unit_scores)
	{
		Position unit_position = predict_position(unit);
		int border_distance = radius - chebyshev_norm(edge_to_point_delta(unit->getType(), unit_position, position));
		if (border_distance >= 0) {
			if (score < 0) {
				return std::make_pair(INT_MIN, INT_MIN);
			}
			if (score > 0) max_border_distance = std::max(max_border_distance, border_distance);
			result += score;
			if (high_value_target) has_high_value_target = true;
		}
	}
	
	if (!emergency && !has_high_value_target) {
		return std::make_pair(INT_MIN, INT_MIN);
	}
	
	return std::make_pair(result, max_border_distance);
}

bool MicroManager::should_emergency_emp(Unit science_vessel_unit)
{
	return (science_vessel_unit->getEnergy() >= 2 * TechTypes::EMP_Shockwave.energyCost() ||
			calculate_frames_to_live(science_vessel_unit) <= Broodwar->getRemainingLatencyFrames() + 12);
}

void MicroManager::mine(Unit vulture_unit,Position position)
{
	vulture_unit->useTech(TechTypes::Spider_Mines, position);
	int latency_frames = Broodwar->getRemainingLatencyFrames() + 24;
	tentative_mines_.emplace_back(vulture_unit, Broodwar->getFrameCount() + latency_frames, position);
}

std::vector<Position> MicroManager::list_existing_scan_positions()
{
	std::vector<Position> result;
	for (auto& unit : Broodwar->self()->getUnits()) {
		if (unit->getType() == UnitTypes::Spell_Scanner_Sweep) result.push_back(unit->getPosition());
	}
	for (TentativeScan tentative_scan : tentative_scans_) result.push_back(tentative_scan.position);
	return result;
}

std::vector<Position> MicroManager::list_existing_storm_positions()
{
	std::vector<Position> existing_storms;
	for (Bullet bullet : Broodwar->getBullets()) {
		if (bullet->getType() == BulletTypes::Psionic_Storm) existing_storms.push_back(bullet->getPosition());
	}
	for (TentativeStorm tentative_storm : tentative_storms_) existing_storms.push_back(tentative_storm.position);
	return existing_storms;
}

bool MicroManager::position_list_intersects(const std::vector<Position>& positions,Position candidate,int size)
{
	return any_of(positions.begin(), positions.end(), [candidate,size](Position position) {
		return candidate.getApproxDistance(position) <= 2 * size;
	});
}

std::vector<Position> MicroManager::list_existing_dark_swarm_positions()
{
	std::vector<Position> result;
	for (auto& unit : Broodwar->getNeutralUnits()) {
		if (unit->getType() == UnitTypes::Spell_Dark_Swarm) result.push_back(unit->getPosition());
	}
	for (TentativeDarkSwarm tentative_dark_swarm : tentative_dark_swarms_) result.push_back(tentative_dark_swarm.position);
	return result;
}

bool MicroManager::position_list_intersects_square(const std::vector<Position>& positions,Position candidate,int size)
{
	return any_of(positions.begin(), positions.end(), [candidate,size](Position position) {
		return chebyshev_distance(candidate, position) <= 2 * size;
	});
}


Position MicroManager::calculate_single_target_position()
{
	Position first_target_position = Positions::None;
	for (auto& entry : combat_state_) {
		if (entry.second.target_position().isValid()) {
			first_target_position = entry.second.target_position();
			break;
		}
	}
	Position target_position = Positions::None;
	if (first_target_position.isValid()) {
		bool all_match = std::all_of(combat_state_.begin(), combat_state_.end(), [first_target_position](auto& entry){
			Position position = entry.second.target_position();
			return !position.isValid() || position == first_target_position;
		});
		if (all_match) target_position = first_target_position;
	}
	return target_position;
}

void MicroManager::update_run_by()
{
	if (run_by_defense_.empty()) {
		if (!opponent_model.non_basic_combat_unit_seen() &&
			opponent_model.enemy_opening() != EnemyOpening::P_CannonRush) {
			// @ Run-by now only works when there is a single target position, possibly not sufficient.
			Position target_position = calculate_single_target_position();
			
			if (target_position.isValid()) {
				std::vector<Unit> defense = determine_defense_for_run_by(target_position);
				
				if (!defense.empty()) {
					std::set<Unit> units_near_defense = determine_run_by_units_near_defense(defense, target_position);
					if (check_run_by_damage(defense, units_near_defense)) {
						run_by_defense_ = defense;
						run_by_target_position_ = target_position;
						for (auto& unit : units_near_defense) running_by_.insert(unit);
					}
				}
			}
		}
	} else {
		remove_elements_in_place(run_by_defense_, [](auto& defense_unit) {
			return !contains(information_manager.all_units(), defense_unit);
		});
		if (run_by_defense_.empty()) {
			end_run_by();
		} else {
			std::set<Unit> units_near_defense = determine_run_by_units_near_defense(run_by_defense_, run_by_target_position_);
			if ((running_by_.empty() && check_run_by_damage(run_by_defense_, units_near_defense)) || !running_by_.empty()) {
				for (auto& unit : units_near_defense) running_by_.insert(unit);
			}
			
			std::vector<Unit> remove_from_running_by;
			for (auto& unit : running_by_) {
				if (units_near_defense.count(unit) == 0 && connectivity_grid.component_for_position(unit->getPosition()) != 0) {
					remove_from_running_by.push_back(unit);
					desperados_.insert(unit);
				} else if (!unit->exists() || unit->getPlayer() != Broodwar->self()) {
					remove_from_running_by.push_back(unit);
				}
			}
			for (Unit unit : remove_from_running_by) running_by_.erase(unit);
			
			std::vector<Unit> remove_from_desperados;
			for (auto& unit : desperados_) {
				if (!unit->exists() || unit->getPlayer() != Broodwar->self()) {
					remove_from_desperados.push_back(unit);
				}
			}
			for (Unit unit : remove_from_desperados) desperados_.erase(unit);
			
			if (running_by_.empty() && desperados_.empty()) end_run_by();
		}
	}
}

void MicroManager::end_run_by()
{
	run_by_defense_.clear();
	run_by_target_position_ = Positions::None;
	running_by_.clear();
	desperados_.clear();
}

std::pair<UnitType,TilePosition> MicroManager::determine_building_at_position(Position position)
{
	UnitType target_unit_type = UnitTypes::None;
	TilePosition target_tile_position = TilePositions::None;
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->type.isBuilding() && !enemy_unit->flying && enemy_unit->position == position) {
			target_unit_type = enemy_unit->type;
			target_tile_position = enemy_unit->tile_position();
			break;
		}
	}
	return std::make_pair(target_unit_type, target_tile_position);
}

std::set<Unit> MicroManager::determine_run_by_units_near_defense(std::vector<Unit> defense,Position target_position)
{
	UnitType target_unit_type;
	TilePosition target_tile_position;
	std::tie(target_unit_type, target_tile_position) = determine_building_at_position(target_position);
	
	std::set<Unit> result;
	for (auto& unit : combat_units_) {
		if ((unit->getType() == UnitTypes::Protoss_Zealot || unit->getType() == UnitTypes::Protoss_Dragoon)) {
			bool defender_can_attack = std::any_of(defense.begin(), defense.end(), [unit](auto& defense_unit){
				const InformationUnit& enemy_defense_unit = information_manager.all_units().at(defense_unit);
				return can_attack_in_range_at_positions(enemy_defense_unit.type, enemy_defense_unit.position, enemy_defense_unit.player, unit->getType(), unit->getPosition());
			});
			if (defender_can_attack) {
				int component = connectivity_grid.component_for_position(unit->getPosition());
				bool reachable;
				if (target_unit_type != UnitTypes::None) {
					reachable = connectivity_grid.building_has_component(target_unit_type, target_tile_position, component);
				} else {
					reachable = (connectivity_grid.component_for_position(target_position) == component);
				}
				if (reachable) result.insert(unit);
			}
		}
	}
	return result;
}

bool MicroManager::runby_possible()
{
	std::set<Unit> runby_units;
	for (auto& unit : Broodwar->self()->getUnits()) {
		if (unit->isCompleted() && unit->isVisible() && !is_disabled(unit) && !unit->isLoaded() &&
			is_runby_unit_type(unit->getType())) {
			runby_units.insert(unit);
		}
	}
	return runby_possible_with_units(runby_units);
}

bool MicroManager::runby_possible(const std::set<Unit>& units)
{
	std::set<Unit> runby_units;
	for (auto& unit : units) {
		if (unit->isCompleted() && unit->isVisible() && !is_disabled(unit) && !unit->isLoaded() &&
			is_runby_unit_type(unit->getType())) {
			runby_units.insert(unit);
		}
	}
	return runby_possible_with_units(runby_units);
}

bool MicroManager::is_runby_unit_type(UnitType type)
{
	return (type == UnitTypes::Protoss_Zealot ||
			type == UnitTypes::Protoss_Dragoon ||
			type == UnitTypes::Terran_Vulture ||
			type == UnitTypes::Zerg_Zergling ||
			type == UnitTypes::Zerg_Hydralisk);
}

bool MicroManager::runby_possible_with_units(const std::set<Unit>& runby_units)
{
	bool result = false;
	Position target_position = tactics_manager.enemy_start_position();
	if (!opponent_model.non_basic_combat_unit_seen() && target_position.isValid()) {
		std::vector<Unit> defense = determine_defense_for_run_by(target_position);
		if (!defense.empty()) {
			result = check_run_by_damage(defense, runby_units);
		}
	}
	return result;
}

std::vector<Unit> MicroManager::determine_defense_for_run_by(Position target_position)
{
	std::vector<const InformationUnit*> potential_units;
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if ((enemy_unit->type == UnitTypes::Terran_Bunker ||
			 enemy_unit->type == UnitTypes::Protoss_Photon_Cannon ||
			 enemy_unit->type == UnitTypes::Zerg_Sunken_Colony) &&
			enemy_unit->is_completed()) {
			potential_units.push_back(enemy_unit);
		}
	}
	
	std::vector<Unit> result;
	if (potential_units.size() == 1 &&
		potential_units[0]->type == UnitTypes::Terran_Bunker) {
		int bunker_marine_range = weapon_max_range(WeaponTypes::Gauss_Rifle, potential_units[0]->player) + 64;
		if (calculate_distance(UnitTypes::Terran_Marine, potential_units[0]->position, target_position) > bunker_marine_range) {
			result.push_back(potential_units[0]->unit);
		}
	} else if (potential_units.size() == 1) {
		int range = weapon_max_range(potential_units[0]->type.groundWeapon(), potential_units[0]->player);
		if (calculate_distance(potential_units[0]->type, potential_units[0]->position, target_position) > range) {
			result.push_back(potential_units[0]->unit);
		}
	} else if (potential_units.size() == 2 &&
			   potential_units[0]->type == potential_units[1]->type &&
			   potential_units[0]->type != UnitTypes::Terran_Bunker) {
		int range = weapon_max_range(potential_units[0]->type.groundWeapon(), potential_units[0]->player);
		if (calculate_distance(potential_units[0]->type, potential_units[0]->position, target_position) > range &&
			calculate_distance(potential_units[1]->type, potential_units[1]->position, target_position) > range &&
			calculate_distance(potential_units[0]->type, potential_units[0]->position, potential_units[1]->type, potential_units[1]->position) <= range) {
			result.push_back(potential_units[0]->unit);
			result.push_back(potential_units[1]->unit);
		}
	}
	return result;
}

bool MicroManager::check_run_by_damage(std::vector<Unit> defense,const std::set<Unit>& units_near_defense)
{
	if (units_near_defense.empty()) return false;
	
	int hp_sum = 0;
	double speed_sum = 0.0;
	for (auto& unit : units_near_defense) {
		int hp_and_shields = unit->getHitPoints() + unit->getShields();
		hp_sum += hp_and_shields;
		speed_sum += unit->getType().topSpeed();
	}
	double speed = speed_sum / units_near_defense.size();
	
	double damage = 0.0;
	for (auto& defense_unit : defense) {
		const InformationUnit& enemy_defense_unit = information_manager.all_units().at(defense_unit);
		// @ Base distance on how close the unit is already to the defense
		int range = weapon_max_range(enemy_defense_unit.type, enemy_defense_unit.player, false);
		int distance = (3 * range / 2) + std::max(enemy_defense_unit.type.width(), enemy_defense_unit.type.height());
		double dpf_sum = 0.0;
		for (auto& unit : units_near_defense) {
			int hp_and_shields = unit->getHitPoints() + unit->getShields();
			double hp_fract = double(unit->getHitPoints()) / double(hp_and_shields);
			double shield_fract = double(unit->getShields()) / double(hp_and_shields);
			int bunker_marines_loaded = 0;
			if (enemy_defense_unit.type == UnitTypes::Terran_Bunker) {
				bunker_marines_loaded = information_manager.bunker_marines_loaded(defense_unit);
			}
			DPF dpf = calculate_damage_per_frame(enemy_defense_unit.type, enemy_defense_unit.player, unit->getType(), Broodwar->self(), bunker_marines_loaded);
			dpf_sum += (hp_fract * dpf.hp + shield_fract * dpf.shield);
		}
		double dpf = dpf_sum / units_near_defense.size();
		double frames = distance / speed;
		damage += (frames * dpf);
	}
	bool non_bunker_defense = std::any_of(defense.begin(), defense.end(), [](Unit defense_unit){
		const InformationUnit& enemy_defense_unit = information_manager.all_units().at(defense_unit);
		return enemy_defense_unit.type != UnitTypes::Terran_Bunker;
	});
	double factor = non_bunker_defense ? 0.4 : 0.5;
	return (damage <= factor * hp_sum);
}

bool MicroManager::MinePlacementCheck::is_near_chokepoint(Position position)
{
	for (auto& area : bwem_map.Areas()) {
		for (auto& cp : area.ChokePoints()) {
			if (cp->GetAreas().first == &area) {
				if (!cp->Blocked()) {
					auto ends = chokepoint_ends(cp);
					if (point_to_line_segment_distance(center_position(ends.first), center_position(ends.second), position) <= 4) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

bool MicroManager::MinePlacementCheck::allow_mine_at(Position position,int min_distance)
{
	initialize();
	
	for (auto mine_position : mines_) {
		int distance = calculate_distance(UnitTypes::Terran_Vulture_Spider_Mine, mine_position,
										  UnitTypes::Terran_Vulture_Spider_Mine, position);
		if (distance < min_distance) {
			return false;
		}
	}
	
	for (auto information_unit : resources_) {
		int distance = calculate_distance(information_unit->type, information_unit->position,
										  UnitTypes::Terran_Vulture_Spider_Mine, position);
		if (distance < 64) {
			return false;
		}
	}
	
	for (auto base : base_state.controlled_and_planned_bases()) {
		int distance = calculate_distance(Broodwar->self()->getRace().getResourceDepot(), base->Center(),
										  UnitTypes::Terran_Vulture_Spider_Mine, position);
		if (distance < 64) {
			return false;
		}
	}
	
	return true;
}

void MicroManager::MinePlacementCheck::initialize()
{
	if (!initialized_) {
		for (auto& information_unit : information_manager.my_units()) {
			if (information_unit->type == UnitTypes::Terran_Vulture_Spider_Mine) {
				mines_.push_back(information_unit->position);
			} else if (information_unit->type.isRefinery()) {
				resources_.push_back(information_unit);
			}
		}
		for (auto& tentative_mine : micro_manager.tentative_mines()) {
			mines_.push_back(tentative_mine.position);
		}
		
		for (auto& information_unit : information_manager.neutral_units()) {
			if (information_unit->type.isMineralField() ||
				information_unit->type == UnitTypes::Resource_Vespene_Geyser) {
				resources_.push_back(information_unit);
			}
		}
		
		initialized_ = true;
	}
}

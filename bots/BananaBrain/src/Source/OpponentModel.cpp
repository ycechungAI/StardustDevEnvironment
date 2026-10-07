#include "BananaBrain.h"

void OpponentModel::UnitSightingCollector::add_if_needed(Unit unit)
{
	UnitSighting& unit_sighting = unit_sightings_[unit];
	if (unit_sighting.frame == -1) {
		unit_sighting.position = unit->getPosition();
		unit_sighting.frame = Broodwar->getFrameCount();
		unit_sighting.type = unit->getType();
	}
}

std::vector<int> OpponentModel::UnitSightingCollector::calculate_and_sort_creation_frames() const
{
	std::vector<int> result;
	
	std::vector<const BWEM::Base*> bases = tactics_manager.possible_enemy_start_bases();
	if (!bases.empty()) {
		std::vector<InformationUnit*> buildings;
		for (auto& information_unit : information_manager.enemy_units()) {
			if (information_unit->type == building_type_ &&
				information_unit->position.isValid() &&
				!information_unit->flying) {
				buildings.push_back(information_unit);
			}
		}
		
		for (auto& entry : unit_sightings_) {
			int min_distance = INT_MAX;
			for (auto& information_unit : buildings) {
				FastPosition position = information_unit->position + FastPosition(0, building_type_.dimensionDown());
				int distance = ground_distance(entry.second.position, position);
				if (distance > 0) {
					min_distance = std::min(min_distance, distance);
				}
			}
			if (min_distance == INT_MAX) {
				for (auto& base : bases) {
					int distance = ground_distance(entry.second.position, base->Center());
					if (distance > 0) {
						min_distance = std::min(min_distance, distance);
					}
				}
			}
			if (min_distance < INT_MAX) {
				int depart_frame = int(entry.second.frame - min_distance / entry.second.type.topSpeed() + 0.5);
				result.push_back(depart_frame);
			}
		}
	}
	
	std::sort(result.begin(), result.end());
	return result;
}

void OpponentModel::init()
{
	Race::set races = Broodwar->enemies().getRaces();
	if (races.size() == 1) {
		Race race = *races.begin();
		if (race_known(race)) initial_enemy_race_ = enemy_race_ = race;
	}
}

std::string OpponentModel::enemy_opening_info() const
{
	return enemy_opening_info(enemy_opening_);
}

std::string OpponentModel::enemy_opening_info(EnemyOpening enemy_opening) const
{
	switch (enemy_opening) {
		// Zerg
		case EnemyOpening::Z_4_5Pool:
			return "Z_4/5pool";
		case EnemyOpening::Z_9Pool:
			return "Z_9pool";
		case EnemyOpening::Z_9PoolSpeed:
			return "Z_9poolspeed";
		case EnemyOpening::Z_OverPool:
			return "Z_overpool";
		case EnemyOpening::Z_12Pool:
			return "Z_12pool";
		case EnemyOpening::Z_10Hatch:
			return "Z_10hatch";
		case EnemyOpening::Z_12Hatch:
			return "Z_12hatch";
		case EnemyOpening::Z_12HatchMain:
			return "Z_12hatchmain";
		case EnemyOpening::Z_WorkerRush:
			return "Z_workerrush";
			
		// Terran
		case EnemyOpening::T_BBS:
			return "T_bbs";
		case EnemyOpening::T_2Rax:
			return "T_2rax";
		case EnemyOpening::T_ProxyRax:
			return "T_proxyrax";
		case EnemyOpening::T_1Fact:
			return "T_1fact";
		case EnemyOpening::T_2Fact:
			return "T_2fact";
		case EnemyOpening::T_FastExpand:
			return "T_fastexpand";
		case EnemyOpening::T_1RaxFE:
			return "T_1raxfe";
		case EnemyOpening::T_1FactFE:
			return "T_1factfe";
		case EnemyOpening::T_WallIn:
			return "T_wallin";
		case EnemyOpening::T_WorkerRush:
			return "T_workerrush";
			
		// Protoss
		case EnemyOpening::P_1GateCore:
			return "P_1gatecore";
		case EnemyOpening::P_4GateGoon:
			return "P_4gategoon";
		case EnemyOpening::P_2Gate:
			return "P_2gate";
		case EnemyOpening::P_2GateFast:
			return "P_2gatefast";
		case EnemyOpening::P_ProxyGate:
			return "P_proxygate";
		case EnemyOpening::P_FastExpand:
			return "P_fastexpand";
		case EnemyOpening::P_ForgeFastExpand:
			return "P_ffe";
		case EnemyOpening::P_CannonRush:
			return "P_cannonrush";
		case EnemyOpening::P_CannonTurtle:
			return "P_cannonturtle";
		case EnemyOpening::P_WorkerRush:
			return "P_workerrush";
	}
	switch (enemy_race_) {
		case Races::Zerg:
			return "Z_unknown";
		case Races::Terran:
			return "T_unknown";
		case Races::Protoss:
			return "P_unknown";
		default:
			return "unknown";
	}
}

int OpponentModel::enemy_earliest_expansion_frame()
{
	if (enemy_race_ == Races::Protoss &&
		natural_pylon_seen_) {
		return kFFEForgeFrame;
	} else {
		return enemy_latest_expansion_frame();
	}
}

int OpponentModel::enemy_latest_expansion_frame()
{
	int result;
	
	switch (enemy_race_) {
		case Races::Zerg:
			result = k12HatchHatcheryFrame;
			break;
		case Races::Terran:
			result = k14CCCommandCenterFrame;
			break;
		case Races::Protoss:
			result = k12NexusNexusFrame;
			break;
		default:
			result = std::max(k12HatchHatcheryFrame, std::max(k14CCCommandCenterFrame, k12NexusNexusFrame));
			break;
	}
	
	return result;
}

bool OpponentModel::cloaked_or_mine_present() const
{
	return cloaked_present_ || information_manager.enemy_seen(UnitTypes::Terran_Vulture_Spider_Mine);
}

bool OpponentModel::position_not_in_main(Position enemy_position)
{
	if (!enemy_position.isValid()) return false;
	
	const BWEM::Area* area = area_at(enemy_position);
	if (area != nullptr) {
		for (auto& base : area->Bases()) {
			if (base.Starting()) return false;
		}
		for (auto& base : base_state.bases()) {
			if (base->Starting() &&
				base_state.extension_area_for_start_base(base) == area) {
				return false;
			}
		}
	}
	
	return true;
}

bool OpponentModel::position_in_enemy_main(Position enemy_position)
{
	if (!enemy_position.isValid()) return false;
	
	const BWEM::Area* area = area_at(enemy_position);
	if (area != nullptr) {
		for (auto& base : area->Bases()) {
			if (base.Starting() &&
				!contains(base_state.controlled_and_planned_bases(), &base)) return true;
		}
		for (auto& base : base_state.bases()) {
			if (base->Starting() &&
				!contains(base_state.controlled_and_planned_bases(), base) &&
				base_state.extension_area_for_start_base(base) == area) {
				return true;
			}
		}
	}
	
	return false;
}

bool OpponentModel::position_in_main_near_choke(Position enemy_position,int max_distance)
{
	bool result = false;
	const BWEM::Area* area = area_at(enemy_position);
	std::vector<const BWEM::Base*> potential_start_bases = tactics_manager.possible_enemy_start_bases();
	for (auto& potential_start_base : potential_start_bases) {
		const BWEM::Area* base_area = base_state.extension_area_for_start_base(potential_start_base);
		if (base_area == nullptr) {
			base_area = potential_start_base->GetArea();
		}
		
		if (base_area == area) {
			for (auto& cp : base_area->ChokePoints()) {
				if (!cp->Blocked()) {
					auto [end1,end2] = chokepoint_ends(cp);
					if (point_to_line_segment_distance(center_position(end1), center_position(end2), enemy_position) <= max_distance) {
						result = true;
						break;
					}
				}
			}
			if (result) break;
		}
	}
	return result;
}

bool OpponentModel::position_in_or_near_potential_natural(Position enemy_position,int max_distance)
{
	bool result = false;
	const BWEM::Area* area = area_at(enemy_position);
	std::vector<const BWEM::Base*> potential_start_bases = tactics_manager.possible_enemy_start_bases();
	for (auto& potential_start_base : potential_start_bases) {
		const BWEM::Base* potential_natural = base_state.natural_base_for_start_base(potential_start_base);
		if (potential_natural != nullptr) {
			const BWEM::Area* base_area = potential_natural->GetArea();
			if (base_area == area) {
				result = true;
				break;
			}
			
			for (auto& cp : base_area->ChokePoints()) {
				if (!cp->Blocked()) {
					auto [end1,end2] = chokepoint_ends(cp);
					if (point_to_line_segment_distance(center_position(end1), center_position(end2), enemy_position) <= max_distance) {
						result = true;
						break;
					}
				}
			}
		}
	}
	return result;
}

bool OpponentModel::is_non_start_hatchery(Unit unit)
{
	bool result = false;
	
	if (unit->getType() == UnitTypes::Zerg_Hatchery) {
		const BWEM::Base* base = base_state.base_for_tile_position(unit->getTilePosition());
		result = (base == nullptr || !base->Starting());
	}
	
	return result;
}

void OpponentModel::update_state_for_unit(Unit unit)
{
	UnitType type = unit->getType();
	if (!cloaked_present_ && is_cloaked(unit)) cloaked_present_ = true;
	if (dark_templar_frame_ == -1 && type == UnitTypes::Protoss_Dark_Templar) {
		dark_templar_frame_ = Broodwar->getFrameCount();
		dark_templar_position_ = unit->getPosition();
	}
	if (mutalisk_frame_ == -1 && type == UnitTypes::Zerg_Mutalisk) {
		mutalisk_frame_ = Broodwar->getFrameCount();
		mutalisk_position_ = unit->getPosition();
	}
	if (lurker_frame_ == -1 && type == UnitTypes::Zerg_Lurker_Egg) {
		lurker_frame_ = Broodwar->getFrameCount() + UnitTypes::Zerg_Lurker.buildTime();
	}
	if ((lurker_frame_ == -1 || lurker_frame_ > Broodwar->getFrameCount()) && type == UnitTypes::Zerg_Lurker) {
		lurker_frame_ = Broodwar->getFrameCount();
	}
	if (enemy_opening_ == EnemyOpening::Unknown) {
		if (unit->getType() == UnitTypes::Zerg_Spawning_Pool) {
			update_state_for_spawning_pool(unit);
		} else if (unit->getType() == UnitTypes::Zerg_Hatchery) {
			update_state_for_hatchery(unit);
		} else if (unit->getType() == UnitTypes::Zerg_Zergling) {
			update_state_for_zergling(unit);
		} else if (is_proxy_gate(unit)) {
			enemy_opening_ = EnemyOpening::P_ProxyGate;
		} else if (is_proxy_rax(unit)) {
			enemy_opening_ = EnemyOpening::T_ProxyRax;
		}
	}
	if ((enemy_opening_ == EnemyOpening::Unknown ||
		 enemy_opening_ == EnemyOpening::P_ProxyGate) &&
		is_cannon_rush(unit)) {
		enemy_opening_ = EnemyOpening::P_CannonRush;
	}
	if (enemy_opening_ == EnemyOpening::Z_9Pool && unit->getType() == UnitTypes::Zerg_Extractor && !unit->isCompleted()) {
		int start_frame = estimate_building_start_frame_based_on_hit_points(unit);
		if (start_frame <= k9PoolSpeedExtractorFrame && unit->getHitPoints() > UnitTypes::Zerg_Extractor.maxHitPoints() / 3) {
			enemy_opening_ = EnemyOpening::Z_9PoolSpeed;
		}
	}
	if ((enemy_opening_ == EnemyOpening::Unknown || enemy_opening_ == EnemyOpening::Z_9Pool) &&
		unit->getType() == UnitTypes::Zerg_Zergling &&
		Broodwar->getFrameCount() <= k9PoolSpeedMetabolicBoostFrame + UpgradeTypes::Metabolic_Boost.upgradeTime() + 500 &&
		information_manager.upgrade_level(unit->getPlayer(), UpgradeTypes::Metabolic_Boost) > 0) {
		enemy_opening_ = EnemyOpening::Z_9PoolSpeed;
	}
	if (enemy_opening_ == EnemyOpening::Z_12Hatch &&
		unit->getType() == UnitTypes::Zerg_Hatchery &&
		!unit->isCompleted() &&
		is_non_start_hatchery(unit) &&
		position_in_enemy_main(unit->getPosition()) &&
		estimate_building_start_frame_based_on_hit_points(unit) <= (k12HatchHatcheryFrame + kOverPoolHatcheryFrame) / 2) {
		enemy_opening_ = EnemyOpening::Z_12HatchMain;
	}
	if (!non_basic_combat_unit_seen_ && !type.isBuilding() && !type.isWorker() &&
		type != UnitTypes::Terran_Marine && type != UnitTypes::Zerg_Zergling && type != UnitTypes::Protoss_Zealot &&
		type != UnitTypes::Zerg_Overlord && type != UnitTypes::Zerg_Larva && type != UnitTypes::Zerg_Egg) {
		non_basic_combat_unit_seen_ = true;
	}
	if (Broodwar->getFrameCount() <= kTerranUnitSightingCutoffFrame &&
		type == UnitTypes::Terran_Marine) {
		marine_collector_.add_if_needed(unit);
	}
	if (Broodwar->getFrameCount() <= kProtossUnitSightingCutoffFrame &&
		type == UnitTypes::Protoss_Zealot) {
		zealot_collector_.add_if_needed(unit);
	}
	if (Broodwar->getFrameCount() <= k4GateGoonUnitSightingCutoffFrame &&
		type == UnitTypes::Protoss_Dragoon) {
		dragoon_collector_.add_if_needed(unit);
	}
	if (unit->getType() == UnitTypes::Protoss_Pylon &&
		position_not_in_main(unit->getPosition())) {
		natural_pylon_seen_ = true;
	}
}

bool OpponentModel::is_cloaked(Unit unit)
{
	UnitType type = unit->getType();
	return (type != UnitTypes::Terran_Vulture_Spider_Mine &&
			type != UnitTypes::Protoss_Observer &&
			(unit->isCloaked() ||
			 type.hasPermanentCloak() ||
			 type == UnitTypes::Zerg_Lurker ||
			 type == UnitTypes::Zerg_Lurker_Egg));
}

void OpponentModel::update_state_for_spawning_pool(Unit unit)
{
	int start_frame = estimate_building_start_frame_based_on_hit_points(unit);
	
	if (start_frame < (k5PoolSpawningPoolFrame + k9PoolSpawningPoolFrame) / 2) {
		enemy_opening_ = EnemyOpening::Z_4_5Pool;
	} else if (!unit->isCompleted()) {
		if (start_frame < (k9PoolSpawningPoolFrame + kOverPoolSpawningPoolFrame) / 2) {
			enemy_opening_ = EnemyOpening::Z_9Pool;
		} else if (start_frame < (kOverPoolSpawningPoolFrame + k12PoolSpawningPoolFrame) / 2) {
			enemy_opening_ = EnemyOpening::Z_OverPool;
		} else if (start_frame < (k12PoolSpawningPoolFrame + k10HatchSpawningPoolFrame) / 2) {
			enemy_opening_ = EnemyOpening::Z_12Pool;
		} else if (start_frame < (k10HatchSpawningPoolFrame + k12HatchSpawningPoolFrame) / 2) {
			enemy_opening_ = EnemyOpening::Z_10Hatch;
		} else if (start_frame < k12HatchSpawningPoolFrame + 500) {
			enemy_opening_ = EnemyOpening::Z_12Hatch;
		}
	}
}

void OpponentModel::update_state_for_hatchery(Unit unit)
{
	int start_frame = estimate_building_start_frame_based_on_hit_points(unit);
	
	if (is_non_start_hatchery(unit) && start_frame <= (k10HatchHatcheryFrame + k12HatchHatcheryFrame) / 2) {
		enemy_opening_ = EnemyOpening::Z_10Hatch;
	} else if (!unit->isCompleted()) {
		if (position_not_in_main(unit->getPosition())) {
			if (start_frame <= (k12HatchHatcheryFrame + kOverPoolHatcheryFrame) / 2) {
				enemy_opening_ = EnemyOpening::Z_12Hatch;
			}
		} else if (is_non_start_hatchery(unit) &&
				   information_manager.enemy_count(UnitTypes::Zerg_Drone) > 9) {
			if (start_frame <= (k12HatchHatcheryFrame + kOverPoolHatcheryFrame) / 2) {
				enemy_opening_ = EnemyOpening::Z_12HatchMain;
			}
		}
	}
}

void OpponentModel::update_state_for_zergling(Unit unit)
{
	std::vector<const BWEM::Base*> bases = tactics_manager.possible_enemy_start_bases();
	int min_distance = INT_MAX;
	for (auto& base : bases) {
		int distance = ground_distance(unit->getPosition(), base->Center());
		if (distance > 0) min_distance = std::min(min_distance, distance);
	}
	if (min_distance < INT_MAX) {
		int depart_frame = int(Broodwar->getFrameCount() - min_distance / UnitTypes::Zerg_Zergling.topSpeed() + 0.5);
		if (depart_frame <= k9PoolZerglingFrame - 50) {
			enemy_opening_ = EnemyOpening::Z_4_5Pool;
		} else if (depart_frame <= (k9PoolZerglingFrame + kOverPoolZerglingFrame) / 2) {
			enemy_opening_ = EnemyOpening::Z_9Pool;
		} else if (depart_frame <= (kOverPoolZerglingFrame + k12PoolZerglingFrame) / 2) {
			enemy_opening_ = EnemyOpening::Z_OverPool;
		}
	}
}

bool OpponentModel::is_cannon_rush(Unit unit)
{
	return (unit->getType() == UnitTypes::Protoss_Photon_Cannon &&
			estimate_building_start_frame_based_on_hit_points(unit) <= 15 * UnitTypes::Protoss_Probe.buildTime() &&
			information_manager.all_units().at(unit).base_distance <= 768);
}

bool OpponentModel::is_proxy_gate(Unit unit)
{
	return (unit->getType() == UnitTypes::Protoss_Gateway &&
			estimate_building_start_frame_based_on_hit_points(unit) <= k99GateSecondGatewayFrame + UnitTypes::Protoss_Gateway.buildTime() &&
			!position_in_enemy_main(unit->getPosition()) &&
			!position_in_or_near_potential_natural(unit->getPosition()));
}

bool OpponentModel::is_proxy_rax(Unit unit)
{
	return (unit->getType() == UnitTypes::Terran_Barracks &&
			estimate_building_start_frame_based_on_hit_points(unit) <= kBBSFirstBarracksFrame + UnitTypes::Terran_Barracks.buildTime() &&
			!position_in_enemy_main(unit->getPosition()) &&
			!position_in_or_near_potential_natural(unit->getPosition()));
}

void OpponentModel::update_state_for_unit(const InformationUnit& unit)
{
	if ((enemy_opening_ == EnemyOpening::Unknown || enemy_opening_ == EnemyOpening::P_CannonRush) &&
		is_cannon_turtle(unit)) {
		enemy_opening_ = EnemyOpening::P_CannonTurtle;
	}
	if ((enemy_opening_ == EnemyOpening::Unknown || enemy_opening_ == EnemyOpening::P_FastExpand) && is_forge_fast_expand(unit)) {
		enemy_opening_ = EnemyOpening::P_ForgeFastExpand;
	}
	if (enemy_opening_ == EnemyOpening::Unknown && unit.type.isResourceDepot() && !unit.flying) {
		update_state_for_resource_depot(unit);
	}
	if ((enemy_opening_ == EnemyOpening::Unknown || enemy_opening_ == EnemyOpening::T_1Fact) && unit.type == UnitTypes::Terran_Bunker) {
		update_state_for_bunker(unit);
	}
}

void OpponentModel::update_state_for_resource_depot(const InformationUnit& unit)
{
	const BWEM::Base* base = base_state.base_for_tile_position(unit.tile_position());
	if (base == nullptr || !base->Starting()) {
		if (unit.type.getRace() == Races::Terran) {
			if (unit.start_frame <= (k14CCCommandCenterFrame + k1RaxFECommandCenterFrame) / 2) {
				enemy_opening_ = EnemyOpening::T_FastExpand;
			} else if (unit.start_frame <= (k1RaxFECommandCenterFrame + k1FactFECommandCenterFrame) / 2) {
				enemy_opening_ = EnemyOpening::T_1RaxFE;
			} else if (unit.start_frame <= k1FactFECommandCenterFrame + 500) {
				enemy_opening_ = EnemyOpening::T_1FactFE;
			}
		} else if (unit.type.getRace() == Races::Protoss && unit.start_frame <= k12NexusNexusFrame + 500) {
			enemy_opening_ = EnemyOpening::P_FastExpand;
		}
	}
}

void OpponentModel::update_state_for_bunker(const InformationUnit& unit)
{
	if (unit.base_distance > 768 &&
		position_not_in_main(unit.position) &&
		position_in_or_near_potential_natural(unit.position)) {
		if (unit.start_frame <= (k1FactFEBunkerFrame + k1RaxFEBunkerFrame) / 2) {
			enemy_opening_ = EnemyOpening::T_1FactFE;
		} else if (unit.start_frame <= k1RaxFEBunkerFrame + 500 &&
				   !unit.is_completed()) {
			enemy_opening_ = (enemy_opening_ == EnemyOpening::T_1Fact) ? EnemyOpening::T_1FactFE : EnemyOpening::T_1RaxFE;
		}
	}
}

bool OpponentModel::is_cannon_turtle(const InformationUnit& unit)
{
	return (unit.type == UnitTypes::Protoss_Photon_Cannon &&
			!enemy_natural_sufficiently_scouted_ &&
			position_in_main_near_choke(unit.position, WeaponTypes::STS_Photon_Cannon.maxRange()));
}

bool OpponentModel::is_forge_fast_expand(const InformationUnit& unit)
{
	return ((unit.type == UnitTypes::Protoss_Photon_Cannon || unit.type == UnitTypes::Protoss_Forge) &&
			!enemy_natural_sufficiently_scouted_ &&
			unit.base_distance > 768 &&
			position_not_in_main(unit.position) &&
			position_in_or_near_potential_natural(unit.position));
}

void OpponentModel::update_emp_seen()
{
	if (!emp_seen_) {
		emp_seen_ = std::any_of(Broodwar->getBullets().begin(), Broodwar->getBullets().end(), [](auto& bullet){
			return bullet->getType() == BulletTypes::EMP_Missile;
		});
	}
}

void OpponentModel::update_blocked_expansion_seen()
{
	if (!blocked_expansion_seen_) {
		blocked_expansion_seen_ = std::any_of(worker_manager.worker_map().begin(),
											  worker_manager.worker_map().end(),
											  [](auto& entry) {
												  const Worker& worker = entry.second;
												  return worker.order()->need_detection();
											  });
	}
}

void OpponentModel::update()
{
	update_emp_seen();
	update_blocked_expansion_seen();
	
	for (auto& unit : Broodwar->enemies().getUnits()) {
		if (unit->exists()) {
			if (!enemy_race_known() &&
				Broodwar->enemies().size() == 1 &&
				race_known(unit->getType().getRace())) {
				enemy_race_ = unit->getType().getRace();
			}
			update_state_for_unit(unit);
		}
	}
	for (auto& enemy_unit : information_manager.enemy_units()) {
		update_state_for_unit(*enemy_unit);
	}
	
	update_state_for_unit_sightings();
	update_enemy_base_sufficiently_scouted();
	
	if (enemy_opening_ == EnemyOpening::Unknown && Broodwar->getFrameCount() <= kWorkerRushDetectUntilFrame) {
		int enemy_workers_in_base = 0;
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->type.isWorker() &&
				enemy_unit->base_distance == 0) {
				enemy_workers_in_base++;
			}
		}
		if (enemy_workers_in_base >= 3) {
			switch (enemy_race_) {
				case Races::Zerg:
					enemy_opening_ = EnemyOpening::Z_WorkerRush;
					break;
				case Races::Terran:
					enemy_opening_ = EnemyOpening::T_WorkerRush;
					break;
				case Races::Protoss:
					enemy_opening_ = EnemyOpening::P_WorkerRush;
					break;
			}
		}
	}
	
	if (enemy_opening_ == EnemyOpening::Unknown && enemy_race_ == Races::Protoss) {
		int gateway_99_count = 0;
		int gateway_1012_count = 0;
		int gateway_count = 0;
		int cybercore_count = 0;
		int assimilator_count = 0;
		int forge_count = 0;
		int forge_cannon_rush_count = 0;
		int forge_main_count = 0;
		int cannon_main_count = 0;
		
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->type == UnitTypes::Protoss_Gateway) {
				if (enemy_unit->start_frame <= (k99GateSecondGatewayFrame + k1012GateSecondGatewayFrame) / 2) gateway_99_count++;
				if (enemy_unit->start_frame <= k1012GateSecondGatewayFrame + 500) gateway_1012_count++;
				gateway_count++;
			}
			if (enemy_unit->type == UnitTypes::Protoss_Cybernetics_Core) cybercore_count++;
			if (enemy_unit->type == UnitTypes::Protoss_Assimilator) assimilator_count++;
			if (enemy_unit->type == UnitTypes::Protoss_Forge) {
				if (position_in_enemy_main(enemy_unit->position)) {
					if (enemy_unit->start_frame <= kCannonRushForgeFrame + 500) forge_cannon_rush_count++;
					forge_main_count++;
				}
				forge_count++;
			}
			if (enemy_unit->type == UnitTypes::Protoss_Photon_Cannon && position_in_enemy_main(enemy_unit->position)) {
				cannon_main_count++;
			}
		}
		
		if (gateway_99_count >= 2) {
			enemy_opening_ = EnemyOpening::P_2GateFast;
		} else if (gateway_1012_count >= 2) {
			enemy_opening_ = EnemyOpening::P_2Gate;
		} else if (gateway_count == 1 && (assimilator_count > 0 || cybercore_count > 0) &&
				   Broodwar->getFrameCount() < k1012GateSecondGatewayFrame) {
			enemy_opening_ = EnemyOpening::P_1GateCore;
		} else if (enemy_base_sufficiently_scouted() && gateway_count == 1 &&
				   Broodwar->getFrameCount() >= k1012GateSecondGatewayFrame + 500) {
			enemy_opening_ = EnemyOpening::P_1GateCore;
		} else if (enemy_base_sufficiently_scouted_ && enemy_natural_sufficiently_scouted_ &&
				   Broodwar->getFrameCount() >= k1012GateFirstGatewayFrame + UnitTypes::Protoss_Gateway.buildTime() &&
				   gateway_count == 0 && forge_count == 0 && cybercore_count == 0 && assimilator_count == 0) {
			enemy_opening_ = EnemyOpening::P_ProxyGate;
		} else if ((forge_cannon_rush_count >= 1 && cannon_main_count == 0 && Broodwar->getFrameCount() >= kCannonRushForgeFrame + UnitTypes::Protoss_Forge.buildTime() + 120) ||
				   (enemy_base_sufficiently_scouted_ && gateway_count == 0 && forge_main_count >= 1 && cannon_main_count == 0 && cybercore_count == 0 && assimilator_count == 0)) {
			enemy_opening_ = EnemyOpening::P_CannonRush;
		}
	}

	if ((enemy_opening_ == EnemyOpening::Unknown ||
		 enemy_opening_ == EnemyOpening::P_1GateCore ||
		 enemy_opening_ == EnemyOpening::P_2Gate ||
		 enemy_opening_ == EnemyOpening::P_2GateFast)
		&& enemy_race_ == Races::Protoss) {
		int gateway_count = 0;
		int assimilator_count = 0;
		int cybercore_count = 0;
		
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->start_frame <= k4GateGoonBuildingsFrame) {
				if (enemy_unit->type == UnitTypes::Protoss_Gateway) gateway_count++;
				if (enemy_unit->type == UnitTypes::Protoss_Assimilator) assimilator_count++;
				if (enemy_unit->type == UnitTypes::Protoss_Cybernetics_Core) cybercore_count++;
			}
		}
		
		if (gateway_count >= 4 && assimilator_count >= 1 && cybercore_count >= 1) {
			enemy_opening_ = EnemyOpening::P_4GateGoon;
		}
	}

	if (enemy_opening_ == EnemyOpening::Unknown && enemy_race_ == Races::Terran) {
		int barracks_bbs_count = 0;
		int barracks_2rax_count = 0;
		int barracks_count = 0;
		int factory_count = 0;
		int refinery_count = 0;
		
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->type == UnitTypes::Terran_Barracks) {
				if (enemy_unit->start_frame <= (kBBSFirstBarracksFrame + k2RaxSecondBarracksFrame) / 2) barracks_bbs_count++;
				if (enemy_unit->start_frame <= k2RaxSecondBarracksFrame + 500) barracks_2rax_count++;
				barracks_count++;
			} else if (enemy_unit->type == UnitTypes::Terran_Factory) {
				if (enemy_unit->start_frame <= k2FactSecondFactoryFrame + 500) factory_count++;
			} else if (enemy_unit->type == UnitTypes::Terran_Refinery) {
				refinery_count++;
			}
		}
		
		if (barracks_bbs_count >= 2) {
			enemy_opening_ = EnemyOpening::T_BBS;
		} else if (barracks_2rax_count >= 2) {
			enemy_opening_ = EnemyOpening::T_2Rax;
		} else if (factory_count >= 2) {
			enemy_opening_ = EnemyOpening::T_2Fact;
		} else if (enemy_base_sufficiently_scouted_ &&
				   factory_count >= 1 &&
				   Broodwar->getFrameCount() >= k2FactSecondFactoryFrame + 500) {
			enemy_opening_ = EnemyOpening::T_1Fact;
		} else if (!proxyrax_base_check_ &&
				   enemy_base_sufficiently_scouted_ && enemy_natural_sufficiently_scouted_ &&
				   Broodwar->getFrameCount() >= kStandardFirstBarracksFrame + 500) {
			if (barracks_count == 0 && refinery_count == 0) {
				enemy_opening_ = EnemyOpening::T_ProxyRax;
			}
			proxyrax_base_check_ = true;
		}
	}
	
	if (enemy_opening_ == EnemyOpening::Unknown &&
		enemy_race_ == Races::Terran &&
		!base_state.is_island_map() &&
		Broodwar->getFrameCount() <= kTerranWallInDetectUntilFrame) {
		std::set<int> components_start;
		for (auto& information_unit : information_manager.my_units()) {
			if (information_unit->type.isBuilding()) {
				connectivity_grid.building_components(components_start, information_unit->type, information_unit->tile_position());
			}
		}
		for (auto& base : tactics_manager.possible_enemy_start_bases()) {
			if (!common_elements(components_start, connectivity_grid.building_components(UnitTypes::Terran_Command_Center, base->Location()))) {
				enemy_opening_ = EnemyOpening::T_WallIn;
				break;
			}
		}
	}
}

void OpponentModel::update_state_for_unit_sightings()
{
	if (Broodwar->getFrameCount() <= kTerranUnitSightingCutoffFrame &&
		enemy_opening_ == EnemyOpening::Unknown &&
		enemy_race_ == Races::Terran) {
		std::vector<int> frames = marine_collector_.calculate_and_sort_creation_frames();
		if ((frames.size() >= 1 && frames[0] <= std::min(k1RaxFirstMarineFrame, kBBSFirstMarineFrame) - 100) ||
			(frames.size() >= 2 && frames[1] <= std::min(k1RaxSecondMarineFrame, kBBSSecondMarineFrame) - 100) ||
			(frames.size() >= 3 && frames[2] <= kBBSThirdMarineFrame - 100) ||
			(frames.size() >= 4 && frames[3] <= kBBSFourthMarineFrame - 100) ||
			(frames.size() >= 5 && frames[4] <= kBBSFifthMarineFrame - 100)) {
			enemy_opening_ = EnemyOpening::T_ProxyRax;
		} else if ((frames.size() >= 3 && frames[2] <= (kBBSThirdMarineFrame + k2RaxThirdMarineFrame) / 2) ||
				   (frames.size() >= 4 && frames[3] <= (kBBSFourthMarineFrame + k2RaxFourthMarineFrame) / 2) ||
				   (frames.size() >= 5 && frames[4] <= (kBBSFifthMarineFrame + k2RaxFifthMarineFrame) / 2)) {
			enemy_opening_ = EnemyOpening::T_BBS;
		} else if ((frames.size() >= 3 && frames[2] <= k2RaxThirdMarineFrame + 200) ||
				   (frames.size() >= 4 && frames[3] <= k2RaxFourthMarineFrame + 200) ||
				   (frames.size() >= 5 && frames[4] <= k2RaxFifthMarineFrame + 200)) {
			enemy_opening_ = EnemyOpening::T_2Rax;
		}
	}
	if (Broodwar->getFrameCount() <= kProtossUnitSightingCutoffFrame &&
		enemy_opening_ == EnemyOpening::Unknown &&
		enemy_race_ == Races::Protoss) {
		std::vector<int> frames = zealot_collector_.calculate_and_sort_creation_frames();
		if ((frames.size() >= 1 && frames[0] <= k99GateFirstZealotFrame - 100) ||
			(frames.size() >= 2 && frames[1] <= k99GateSecondZealotFrame - 100) ||
			(frames.size() >= 3 && frames[2] <= k99GateThirdZealotFrame - 100)) {
			enemy_opening_ = EnemyOpening::P_ProxyGate;
		} else if (frames.size() >= 3) {
			if (frames[2] <= (k99GateThirdZealotFrame + k1012GateThirdZealotFrame) / 2) {
				enemy_opening_ = EnemyOpening::P_2GateFast;
			} else if (frames[2] <= k1012GateThirdZealotFrame + 200) {
				enemy_opening_ = EnemyOpening::P_2Gate;
			}
		}
	}
	if (Broodwar->getFrameCount() <= k4GateGoonUnitSightingCutoffFrame &&
		enemy_race_ == Races::Protoss &&
		(enemy_opening_ == EnemyOpening::Unknown ||
		 enemy_opening_ == EnemyOpening::P_1GateCore ||
		 enemy_opening_ == EnemyOpening::P_2Gate ||
		 enemy_opening_ == EnemyOpening::P_2GateFast)) {
		std::vector<int> frames = dragoon_collector_.calculate_and_sort_creation_frames();
		if ((frames.size() >= 7 && frames[6] <= k4GateGoon7Frame) ||
			(frames.size() >= 11 && frames[10] <= k4GateGoon11Frame) ||
			(frames.size() >= 15 && frames[14] <= k4GateGoon15Frame)) {
			enemy_opening_ = EnemyOpening::P_4GateGoon;
		}
	}
}

void OpponentModel::update_enemy_base_sufficiently_scouted()
{
	if (!enemy_base_sufficiently_scouted_) {
		if (worker_manager.is_scouting() &&
			tactics_manager.enemy_start_base() != nullptr &&
			Broodwar->getFrameCount() - tactics_manager.enemy_start_base_found_at_frame() > 320 &&
			resources_explored(tactics_manager.enemy_start_base())) {
			enemy_base_sufficiently_scouted_ = true;
		}
	}
	
	if (enemy_natural_sufficiently_scouted_start_frame_ == -1) {
		if (tactics_manager.enemy_start_base() != nullptr && tactics_manager.enemy_natural_base() == nullptr) {
			enemy_natural_sufficiently_scouted_ = true;
		} else if (tactics_manager.enemy_natural_base() != nullptr) {
			int expansion_frame = enemy_latest_expansion_frame();
			int seen_frame = base_state.base_last_seen(tactics_manager.enemy_natural_base());
			if (seen_frame > expansion_frame + 240) {
				enemy_natural_sufficiently_scouted_start_frame_ = Broodwar->getFrameCount();
			}
		}
	} else if (enemy_natural_sufficiently_scouted_start_frame_ + 100 >= Broodwar->getFrameCount()) {
		enemy_natural_sufficiently_scouted_ = true;
	}
}

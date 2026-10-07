#include "BananaBrain.h"

int MineralGas::can_pay_times(const MineralGas& o) const
{
	if (o.minerals <= 0) return 0;
	int times = minerals / o.minerals;
	if (o.gas <= 0) {
		return std::max(0, times);
	} else {
		return std::max(0, std::min(times, gas / o.gas));
	}
}

bool TrainDistribution::is_nonempty() const
{
	for (auto& entry : weights_) {
		if (entry.second > 0.0) return true;
	}
	return false;
}

double TrainDistribution::sum() const
{
	double result = 0.0;
	for (auto& entry : weights_) {
		result += entry.second;
	}
	return result;
}

UnitType TrainDistribution::sample() const
{
	std::vector<std::pair<UnitType,double>> distribution;
	double sum = 0.0;
	for (auto& entry : weights_) {
		if (entry.second > 0.0) {
			sum += entry.second;
			distribution.push_back(std::pair<UnitType,double>(entry.first, sum));
		}
	}
	
	if (distribution.empty()) return UnitTypes::None;
	if (distribution.size() == 1) return distribution[0].first;
	
	std::uniform_real_distribution<double> dist(0.0, sum);
	double r = dist(random_generator());
	for (size_t i = 0; i < distribution.size() - 1; i++) {
		if (r < distribution[i].second) return distribution[i].first;
	}
	return distribution[distribution.size() - 1].first;
}

MineralGas TrainDistribution::weighted_cost() const
{
	MineralGas result;
	
	if (is_nonempty()) {
		double sum = 0.0;
		double minerals = 0.0;
		double gas = 0.0;
		for (auto& entry : weights_) {
			UnitType type = entry.first;
			double weight = entry.second;
			sum += entry.second;
			MineralGas cost(type);
			minerals += (weight * cost.minerals);
			gas += (weight * cost.gas);
		}
		result.minerals = int(minerals / sum + 0.5);
		result.gas = int(gas / sum + 0.5);
	}
	
	return result;
}

CostPerMinute TrainDistribution::cost_per_minute(int producers) const
{
	if (is_empty() || producers == 0) return CostPerMinute();
	
	CostPerMinute result;
	double sum = 0.0;
	for (auto& entry : weights_) {
		if (entry.second > 0.0) {
			result += entry.second * cost_per_minute(entry.first);
			sum += entry.second;
		}
	}
	return (producers / sum) * result;
}

CostPerMinute TrainDistribution::cost_per_minute(UnitType unit_type)
{
	int frames = (unit_type.getRace() == Races::Zerg) ? kLarvaSpawnFrames : unit_type.buildTime();
	double units_per_minute = 24.0 * 60.0 / frames;
	int supply_required = unit_type.supplyRequired();
	if (unit_type.isTwoUnitsInOneEgg()) supply_required *= 2;
	return CostPerMinute(unit_type.mineralPrice() * units_per_minute,
						 unit_type.gasPrice() * units_per_minute,
						 supply_required * units_per_minute);
}

int TrainDistribution::additional_producers() const
{
	if (is_empty()) return 0;
	
	int times1 = spending_manager.spendable().can_pay_times(MineralGas(builder_type_));
	if (times1 <= 0) return 0;
	
	MineralGas remainder = spending_manager.remainder() + spending_manager.spendable();
	MineralGas spend = MineralGas(builder_type_) + MineralGas(cost_per_minute());
	int times2 = remainder.can_pay_times(spend);
	
	return std::min(times1, times2);
}

void ResourceCounter::init(int value)
{
	for (size_t i = 0; i < values_.size(); i++) values_[i] = value;
}

void ResourceCounter::process_value(int value)
{
	int difference = value - values_[index_];
	values_[index_] = value;
	index_ = (index_ + 1) % values_.size();
	per_minute_ = 24.0 * 60.0 * difference / (double)values_.size();
}

void BuildingManager::update_supply_requests()
{
	if (automatic_supply_) {
		Player self = Broodwar->self();
		for (Race race : { Races::Protoss, Races::Terran } ) {
			if (self->supplyUsed(race) > 0 && self->supplyTotal(race) < TrainingManager::kSupplyCap) {
				const UnitType supply_building = race.getSupplyProvider();
				int supply_building_travel_frame_estimation = estimate_worker_travel_frames_for_place_building(supply_building).second;
				double supply_build_minutes = (supply_building_travel_frame_estimation + supply_building.buildTime() + building_extra_frames(supply_building)) / (60.0 * 24.0);
				double supply_needed_per_minute = spending_manager.training_cost_per_minute(race).supply + spending_manager.worker_training_cost_per_minute(race).supply;
				double supply_requirement = supply_build_minutes * supply_needed_per_minute;
				double supply_shortage = self->supplyUsed(race) + supply_requirement - self->supplyTotal(race);
				int supply_building_shortage = int(ceil(supply_shortage / supply_building.supplyProvided()));
				set_requested_building_count_at_least(supply_building, building_count(supply_building) + supply_building_shortage, true);
			}
		}
	}
}

void BuildingManager::init_building_count_map()
{
	building_count_.clear();
	base_request_.clear();
	
	std::set<TilePosition> building_positions_seen;
	for (auto& unit : Broodwar->self()->getUnits()) {
		if (unit->getType().isBuilding()) {
			building_positions_seen.insert(unit->getTilePosition());
			UnitType unit_type = unit->getType();
			BuildingCount& building_count = building_count_[unit_type];
			if (unit->isCompleted()) {
				building_count.actual++;
			} else {
				building_count.planned++;
				building_count.warping++;
			}
			if (unit_type.getRace() == Races::Zerg) {
				UnitType current_type = unit_type;
				while (current_type.whatBuilds().first.isBuilding()) {
					UnitType parent_type = current_type.whatBuilds().first;
					building_count_[parent_type].actual++;
					current_type = parent_type;
				}
			}
		}
	}
	
	for (auto& entry : worker_manager.worker_map()) {
		const Worker& worker = entry.second;
		const WorkerOrder* order = worker.order();
		UnitType unit_type = order->building_type();
		if (unit_type != UnitTypes::None &&
			!contains(building_positions_seen, order->building_position())) {
			building_count_[unit_type].planned++;
		}
	}
}

void BuildingManager::init_base_defense_map()
{
	const auto is_defense_building = [](UnitType type){
		return (type == UnitTypes::Protoss_Photon_Cannon ||
				type == UnitTypes::Protoss_Pylon ||
				type == UnitTypes::Terran_Missile_Turret ||
				type == UnitTypes::Zerg_Creep_Colony ||
				type == UnitTypes::Zerg_Sunken_Colony ||
				type == UnitTypes::Zerg_Spore_Colony);
	};
	
	base_defense_.clear();
	
	std::map<UnitType,std::set<TilePosition>> planned_positions;
	std::map<UnitType,std::set<TilePosition>> completed_positions;
	
	for (auto& entry : worker_manager.worker_map()) {
		const Worker& worker = entry.second;
		UnitType type = worker.order()->building_type();
		if (is_defense_building(type)) {
			planned_positions[type].insert(worker.order()->building_position());
		}
	}
	for (auto& unit : Broodwar->self()->getUnits()) {
		UnitType type = unit->getType();
		if (is_defense_building(type)) {
			if (unit->isCompleted()) {
				completed_positions[type].insert(unit->getTilePosition());
			} else {
				planned_positions[type].insert(unit->getTilePosition());
			}
		}
	}
	
	for (auto& entry : building_placement_manager.photon_cannon_positions()) {
		const BWEM::Base* base = entry.first;
		const std::vector<TilePosition>& positions = entry.second;
		for (auto& position : positions) {
			if (contains(completed_positions[UnitTypes::Protoss_Photon_Cannon], position)) {
				base_defense_[base].cannons.actual++;
			} else if (contains(planned_positions[UnitTypes::Protoss_Photon_Cannon], position)) {
				base_defense_[base].cannons.planned++;
			}
		}
		
		for (auto& pylon_position : completed_positions[UnitTypes::Protoss_Pylon]) {
			bool all_powered = true;
			for (auto& position : positions) {
				if (!building_placement_manager.check_power(pylon_position, UnitTypes::Protoss_Photon_Cannon, position)) {
					all_powered = false;
					break;
				}
			}
			if (all_powered) {
				base_defense_[base].pylon.exists = true;
			}
		}
		for (auto& pylon_position : planned_positions[UnitTypes::Protoss_Pylon]) {
			bool all_powered = true;
			for (auto& position : positions) {
				if (!building_placement_manager.check_power(pylon_position, UnitTypes::Protoss_Photon_Cannon, position)) {
					all_powered = false;
					break;
				}
			}
			if (all_powered) {
				base_defense_[base].pylon.planned = true;
			}
		}
	}
	
	for (auto& entry : building_placement_manager.missile_turret_positions()) {
		const BWEM::Base* base = entry.first;
		const std::vector<TilePosition>& positions = entry.second;
		for (auto& position : positions) {
			if (contains(completed_positions[UnitTypes::Terran_Missile_Turret], position)) {
				base_defense_[base].turrets.actual++;
			} else if (contains(planned_positions[UnitTypes::Terran_Missile_Turret], position)) {
				base_defense_[base].turrets.planned++;
			}
		}
	}
	
	for (auto& entry : building_placement_manager.creep_colony_positions()) {
		const BWEM::Base* base = entry.first;
		TilePosition position = entry.second;
		if (contains(completed_positions[UnitTypes::Zerg_Creep_Colony], position)) {
			base_defense_[base].creep_colony.exists = true;
		} else if (contains(planned_positions[UnitTypes::Zerg_Creep_Colony], position)) {
			base_defense_[base].creep_colony.planned = true;
		}
		if (contains(completed_positions[UnitTypes::Zerg_Sunken_Colony], position)) {
			base_defense_[base].creep_colony.exists = true;
			base_defense_[base].sunken_colony.exists = true;
		} else if (contains(planned_positions[UnitTypes::Zerg_Sunken_Colony], position)) {
			base_defense_[base].creep_colony.exists = true;
			base_defense_[base].sunken_colony.planned = true;
		}
		if (contains(completed_positions[UnitTypes::Zerg_Spore_Colony], position)) {
			base_defense_[base].creep_colony.exists = true;
			base_defense_[base].spore_colony.exists = true;
		} else if (contains(planned_positions[UnitTypes::Zerg_Spore_Colony], position)) {
			base_defense_[base].creep_colony.exists = true;
			base_defense_[base].spore_colony.planned = true;
		}
	}
}

void BuildingManager::init_upgrade_and_research()
{
	upgrade_request_.clear();
	research_request_.clear();
}

void BuildingManager::set_requested_building_count_at_least(UnitType unit_type,int count,bool important)
{
	BuildingCount& building_count = building_count_[unit_type];
	int additional = std::max(0, count - building_count.actual - building_count.planned);
	building_count.additional = std::max(building_count.additional, additional);
	if (important) {
		building_count.additional_important = std::max(building_count.additional_important, additional);
	}
}

void BuildingManager::request_base(const BWEM::Base* base,bool important)
{
	if (base_state.controlled_and_planned_bases().count(base) == 0) {
		UnitType unit_type = Broodwar->self()->getRace().getResourceDepot();
		base_request_[base].resource_depot_type = unit_type;
		base_request_[base].important |= important;
		building_count_[unit_type];
	}
}

bool BuildingManager::request_next_base(bool important)
{
	if (base_state.next_available_bases().empty()) {
		return false;
	} else {
		if (building_count_[Broodwar->self()->getRace().getResourceDepot()].planned == 0) {
			request_base(base_state.next_available_bases()[0], important);
		}
		return true;
	}
}

bool BuildingManager::request_bases(int count)
{
	if (base_state.next_available_bases().empty()) {
		return false;
	} else {
		if (int(base_state.controlled_and_planned_bases().size()) < count) {
			request_base(base_state.next_available_bases()[0]);
		}
		return true;
	}
}

void BuildingManager::request_base_defense_pylon(const BWEM::Base* base)
{
	BaseDefense& base_defense = base_defense_[base];
	if (!base_defense.pylon.exists && !base_defense.pylon.planned) {
		base_defense.pylon.add = true;
	}
	building_count_[UnitTypes::Protoss_Pylon];
}

void BuildingManager::set_requested_base_defense_cannon_count_at_least(const BWEM::Base* base,int count)
{
	BaseDefense& base_defense = base_defense_[base];
	int additional = std::max(0, count - base_defense.cannons.actual - base_defense.cannons.planned);
	base_defense.cannons.additional = std::max(base_defense.cannons.additional, additional);
	building_count_[UnitTypes::Protoss_Photon_Cannon];
}

void BuildingManager::set_requested_base_defense_turret_count_at_least(const BWEM::Base* base,int count)
{
	BaseDefense& base_defense = base_defense_[base];
	int additional = std::max(0, count - base_defense.turrets.actual - base_defense.turrets.planned);
	base_defense.turrets.additional = std::max(base_defense.turrets.additional, additional);
	building_count_[UnitTypes::Terran_Missile_Turret];
}

void BuildingManager::request_base_defense_creep_colony(const BWEM::Base* base)
{
	BaseDefense& base_defense = base_defense_[base];
	if (!base_defense.creep_colony.exists && !base_defense.creep_colony.planned) {
		base_defense.creep_colony.add = true;
	}
	building_count_[UnitTypes::Zerg_Creep_Colony];
}

void BuildingManager::request_base_defense_sunken_colony(const BWEM::Base* base)
{
	BaseDefense& base_defense = base_defense_[base];
	if (!base_defense.sunken_colony.exists && !base_defense.sunken_colony.planned) {
		base_defense.sunken_colony.add = true;
	}
	building_count_[UnitTypes::Zerg_Sunken_Colony];
	request_base_defense_creep_colony(base);
}

void BuildingManager::request_base_defense_spore_colony(const BWEM::Base* base)
{
	BaseDefense& base_defense = base_defense_[base];
	if (!base_defense.spore_colony.exists && !base_defense.spore_colony.planned) {
		base_defense.spore_colony.add = true;
	}
	building_count_[UnitTypes::Zerg_Spore_Colony];
	request_base_defense_creep_colony(base);
}

bool BuildingManager::required_buildings_exist_for_building(UnitType unit_type)
{
	for (auto& entry : unit_type.requiredUnits()) {
		UnitType required_type = entry.first;
		if (required_type.isBuilding() && building_count_[required_type].actual == 0) return false;
	}
	return true;
}

void BuildingManager::update_requested_building_count_for_pre_upgrade()
{
	set_requested_building_count_at_least(UnitTypes::Zerg_Creep_Colony, building_count_[UnitTypes::Zerg_Sunken_Colony].requested() + building_count_[UnitTypes::Zerg_Spore_Colony].requested());
	set_requested_building_count_at_least(UnitTypes::Zerg_Creep_Colony, building_count_[UnitTypes::Zerg_Sunken_Colony].requested(true) + building_count_[UnitTypes::Zerg_Spore_Colony].requested(true), true);
}

void BuildingManager::apply_building_requests(bool important)
{
	bool no_workers_for_building = false;
	
	for (auto& entry : building_count_) {
		UnitType unit_type = entry.first;
		if (!required_buildings_exist_for_building(unit_type)) continue;
		MineralGas unit_cost = MineralGas(unit_type);
		BuildingCount& building_count = entry.second;
		
		int additional_buildings_requested;
		if (important) {
			additional_buildings_requested = building_count.additional_important;
		} else {
			additional_buildings_requested = building_count.additional - building_count.additional_important;
		}
		
		if (important && unit_type == UnitTypes::Protoss_Pylon) {
			for (auto& entry : base_defense_) {
				int& retry_frame = base_defense_retry_frame_[entry.first];
				if (no_workers_for_building || retry_frame > Broodwar->getFrameCount()) break;
				if (entry.second.pylon.add) {
					TilePosition tile_position = building_placement_manager.place_base_defense_pylon(entry.first);
					int frame_estimation = worker_manager.estimate_worker_travel_frames_for_building_request(unit_type, tile_position);
					if (tile_position.isValid() && spending_manager.try_spend(unit_cost, frame_estimation)) {
						bool success = worker_manager.assign_building_request(unit_type, tile_position);
						if (success) {
							entry.second.pylon.planned = true;
							if (additional_buildings_requested > 0) additional_buildings_requested--;
						}
						if (!success) no_workers_for_building = true;
					}
					if (!tile_position.isValid()) {
						retry_frame = Broodwar->getFrameCount() + kBuildingPlacementFailedRetryFrames;
						if (configuration.draw_enabled()) Broodwar << Broodwar->getFrameCount() << ": Failed to place " << unit_type.getName() << std::endl;
					}
				}
			}
		}
		
		if (important && unit_type == UnitTypes::Protoss_Photon_Cannon) {
			for (auto& entry : base_defense_) {
				int& retry_frame = base_defense_retry_frame_[entry.first];
				for (int i = 0; i < entry.second.cannons.additional && !no_workers_for_building && retry_frame <= Broodwar->getFrameCount(); i++) {
					TilePosition tile_position = building_placement_manager.place_base_defense_photon_cannon(entry.first);
					int frame_estimation = worker_manager.estimate_worker_travel_frames_for_building_request(unit_type, tile_position);
					if (tile_position.isValid() && spending_manager.try_spend(unit_cost, frame_estimation)) {
						bool success = worker_manager.assign_building_request(unit_type, tile_position);
						if (success) {
							entry.second.cannons.planned++;
							if (additional_buildings_requested > 0) additional_buildings_requested--;
						}
						if (!success) no_workers_for_building = true;
					}
					if (!tile_position.isValid()) {
						retry_frame = Broodwar->getFrameCount() + kBuildingPlacementFailedRetryFrames;
						if (configuration.draw_enabled()) Broodwar << Broodwar->getFrameCount() << ": Failed to place " << unit_type.getName() << std::endl;
					}
				}
			}
		}
		
		if (important && unit_type == UnitTypes::Terran_Missile_Turret) {
			for (auto& entry : base_defense_) {
				int& retry_frame = base_defense_retry_frame_[entry.first];
				for (int i = 0; i < entry.second.turrets.additional && !no_workers_for_building && retry_frame <= Broodwar->getFrameCount(); i++) {
					TilePosition tile_position = building_placement_manager.place_base_defense_missile_turret(entry.first);
					int frame_estimation = worker_manager.estimate_worker_travel_frames_for_building_request(unit_type, tile_position);
					if (tile_position.isValid() && spending_manager.try_spend(unit_cost, frame_estimation)) {
						bool success = worker_manager.assign_building_request(unit_type, tile_position);
						if (success) {
							entry.second.turrets.planned++;
							if (additional_buildings_requested > 0) additional_buildings_requested--;
						}
						if (!success) no_workers_for_building = true;
					}
					if (!tile_position.isValid()) {
						retry_frame = Broodwar->getFrameCount() + kBuildingPlacementFailedRetryFrames;
						if (configuration.draw_enabled()) Broodwar << Broodwar->getFrameCount() << ": Failed to place " << unit_type.getName() << std::endl;
					}
				}
			}
		}
		
		if (important && unit_type == UnitTypes::Zerg_Creep_Colony) {
			for (auto& entry : base_defense_) {
				int& retry_frame = base_defense_retry_frame_[entry.first];
				if (no_workers_for_building || retry_frame > Broodwar->getFrameCount()) break;
				if (entry.second.creep_colony.add) {
					TilePosition tile_position = building_placement_manager.place_base_defense_creep_colony(entry.first);
					int frame_estimation = worker_manager.estimate_worker_travel_frames_for_building_request(unit_type, tile_position);
					if (tile_position.isValid() && spending_manager.try_spend(unit_cost, frame_estimation)) {
						bool success = worker_manager.assign_building_request(unit_type, tile_position);
						if (success) {
							entry.second.creep_colony.planned = true;
							if (additional_buildings_requested > 0) additional_buildings_requested--;
						}
						if (!success) no_workers_for_building = true;
					}
					if (!tile_position.isValid()) {
						retry_frame = Broodwar->getFrameCount() + kBuildingPlacementFailedRetryFrames;
						if (configuration.draw_enabled()) Broodwar << Broodwar->getFrameCount() << ": Failed to place " << unit_type.getName() << std::endl;
					}
				}
			}
		}
		
		if (important && (unit_type == UnitTypes::Zerg_Sunken_Colony ||
						  unit_type == UnitTypes::Zerg_Spore_Colony)) {
			for (auto& entry : base_defense_) {
				const BWEM::Base* base = entry.first;
				const BaseDefense& base_defense = base_defense_[base];
				bool add = (unit_type == UnitTypes::Zerg_Sunken_Colony) ? base_defense.sunken_colony.add : base_defense.spore_colony.add;
				if (add && base_defense.creep_colony.exists) {
					auto it = building_placement_manager.creep_colony_positions().find(base);
					if (it != building_placement_manager.creep_colony_positions().end()) {
						TilePosition tile_position = it->second;
						const InformationUnit* information_unit = unit_grid.building_in_tile(tile_position);
						if (information_unit != nullptr &&
							information_unit->type == UnitTypes::Zerg_Creep_Colony &&
							information_unit->player == Broodwar->self() &&
							spending_manager.try_spend(unit_cost)) {
							information_unit->unit->morph(unit_type);
							additional_buildings_requested--;
						}
					}
				}
			}
		}
		
		if (unit_type.isResourceDepot()) {
			for (auto& entry : base_request_) {
				if (entry.second.resource_depot_type == unit_type && entry.second.important == important) {
					int frame_estimation = worker_manager.estimate_worker_travel_frames_for_building_request(unit_type, entry.first->Location());
					if (spending_manager.try_spend(unit_cost, frame_estimation)) {
						bool success = worker_manager.assign_building_request(unit_type, entry.first->Location());
						if (success) {
							building_count.planned++;
							if (additional_buildings_requested > 0) additional_buildings_requested--;
						}
					}
				}
			}
		}
		
		if (additional_buildings_requested > 0) {
			if (unit_type.isAddon()) {
				for (auto& information_unit : information_manager.my_units()) {
					Unit unit = information_unit->unit;
					if (additional_buildings_requested <= 0) break;
					if (unit->isCompleted() && unit_type.whatBuilds().first == unit->getType() && unit->getAddon() == nullptr && spending_manager.try_spend(unit_cost)) {
						unit->buildAddon(unit_type);
						additional_buildings_requested--;
					}
				}
			} else if (unit_type.getRace() == Races::Zerg && unit_type.whatBuilds().first.isBuilding()) {
				for (auto& information_unit : information_manager.my_units()) {
					Unit unit = information_unit->unit;
					if (additional_buildings_requested <= 0) break;
					if (unit->isCompleted() && unit_type.whatBuilds().first == unit->getType() && spending_manager.try_spend(unit_cost)) {
						unit->morph(unit_type);
						additional_buildings_requested--;
					}
				}
			} else if (unit_type.isRefinery()) {
				std::vector<TilePosition> available_geyser_tile_positions = determine_available_geyser_tile_position();
				for (int i = 0; i < std::min<int>(available_geyser_tile_positions.size(), additional_buildings_requested); i++) {
					int frame_estimation = worker_manager.estimate_worker_travel_frames_for_building_request(unit_type, available_geyser_tile_positions[i]);
					if (spending_manager.try_spend(unit_cost, frame_estimation)) {
						bool success = worker_manager.assign_building_request(unit_type, available_geyser_tile_positions[i]);
						if (success) building_count.planned++;
					}
				}
			} else {
				int& retry_frame = (unit_type == UnitTypes::Protoss_Pylon) ? pylon_retry_frame_ : non_pylon_building_retry_frame_;
				
				for (int i = 0; i < additional_buildings_requested && !no_workers_for_building && retry_frame <= Broodwar->getFrameCount(); i++) {
					std::pair<int,int>& frame_estimation_cache = estimate_worker_travel_frames_for_place_building(unit_type);
					int frame_estimation = frame_estimation_cache.second;
					if (spending_manager.try_spend(unit_cost, frame_estimation)) {
						TilePosition tile_position = building_placement_manager.place_building(unit_type);
						if (tile_position.isValid()) {
							bool success = worker_manager.assign_building_request(unit_type, tile_position);
							frame_estimation_cache.first = 0;
							if (success) building_count.planned++;
							if (!success) no_workers_for_building = true;
						}
						if (!tile_position.isValid()) {
							retry_frame = Broodwar->getFrameCount() + kBuildingPlacementFailedRetryFrames;
							if (configuration.draw_enabled()) Broodwar << Broodwar->getFrameCount() << ": Failed to place " << unit_type.getName() << std::endl;
						}
					}
				}
			}
		}
	}
}

std::pair<int,int>& BuildingManager::estimate_worker_travel_frames_for_place_building(UnitType unit_type)
{
	std::pair<int,int>& result = estimated_travel_frame_cache_[unit_type];
	if (result.first < Broodwar->getFrameCount()) {
		TilePosition tile_position = building_placement_manager.place_building(unit_type);
		int frame_estimation = -1;
		if (tile_position.isValid()) {
			frame_estimation = worker_manager.estimate_worker_travel_frames_for_building_request(unit_type, tile_position);
		}
		result.first = Broodwar->getFrameCount() + kBuildingPlacementEstimateWorkerTravelFramesCacheFrames;
		result.second = frame_estimation;
	}
	return result;
}

std::vector<TilePosition> BuildingManager::determine_available_geyser_tile_position() const
{
	std::set<TilePosition> all_positions;
	for (auto& base : base_state.controlled_bases()) {
		for (auto& geyser : base->Geysers()) {
			if (is_safe_to_place_refinery(geyser->TopLeft())) {
				all_positions.insert(geyser->TopLeft());
			}
		}
	}
	
	for (auto& unit : Broodwar->getAllUnits()) if (unit->exists() && unit->getType().isRefinery()) all_positions.erase(unit->getTilePosition());
	
	for (auto& entry : worker_manager.worker_map()) {
		auto& worker = entry.second;
		if (worker.order()->building_type().isRefinery()) all_positions.erase(worker.order()->building_position());
	}
	
	std::vector<TilePosition> result;
	for (auto& position : all_positions) result.push_back(position);
	
	struct GeyserOrder
	{
		bool operator()(TilePosition a,TilePosition b)
		{
			UnitType depot_type = Broodwar->self()->getRace().getResourceDepot();
			Position start_position = center_position_for(depot_type, base_state.start_base()->Location());
			Position a_position = center_position_for(depot_type, a);
			Position b_position = center_position_for(depot_type, b);
			return std::make_tuple(start_position.getApproxDistance(a_position), a) < std::make_tuple(start_position.getApproxDistance(b_position), b);
		}
	};
	std::sort(result.begin(), result.end(), GeyserOrder());
	
	return result;
}

bool BuildingManager::is_safe_to_place_refinery(FastTilePosition tile_position)
{
	UnitType type = UnitTypes::Resource_Vespene_Geyser;
	for (int dy = 0; dy < type.tileHeight(); dy++) {
		for (int dx = 0; dx < type.tileWidth(); dx++) {
			if (threat_grid.ground_threat_excluding_workers(tile_position + FastTilePosition(dx, dy))) {
				return false;
			}
		}
	}
	return true;
}

void BuildingManager::repair_damaged_buildings()
{
	const auto is_wall_building = [](InformationUnit* unit){
		if (building_placement_manager.has_active_wall(true)) {
			UnitType type = unit->type;
			if (type.isBuilding()) {
				TilePosition tile_position = unit->tile_position();
				auto& terran_wall_position = building_placement_manager.terran_wall_position();
				if (terran_wall_position) {
					if (type == UnitTypes::Terran_Barracks &&
						terran_wall_position->barracks_position == tile_position) return true;
					if (type == UnitTypes::Terran_Supply_Depot &&
						(terran_wall_position->first_supply_depot_position == tile_position ||
						 terran_wall_position->second_supply_depot_position == tile_position)) return true;
					if (type == UnitTypes::Terran_Factory &&
						terran_wall_position->factory_position == tile_position) return true;
				}
				auto& terran_natural_wall_position = building_placement_manager.terran_natural_wall_position();
				if (terran_natural_wall_position) {
					if (type == UnitTypes::Terran_Barracks &&
						terran_natural_wall_position->barracks_position == tile_position) return true;
					if (type == UnitTypes::Terran_Supply_Depot &&
						(terran_natural_wall_position->first_supply_depot_position == tile_position ||
						 terran_natural_wall_position->second_supply_depot_position == tile_position)) return true;
				}
			}
		}
		return false;
	};
	
	const auto is_safe_to_repair = [](InformationUnit* unit){
		UnitType type = unit->type;
		FastTilePosition tile_position = unit->tile_position();
		for (int dy = -1; dy < type.tileHeight() + 1; dy++) {
			for (int dx = -1; dx < type.tileWidth() + 1; dx++) {
				FastTilePosition current_tile_position = tile_position + FastTilePosition(dx, dy);
				if (threat_grid.ground_threat_excluding_workers(current_tile_position)) {
					return false;
				}
			}
		}
		return true;
	};
	
	std::vector<std::pair<Unit,bool>> damaged_buildings;
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		if (information_unit->type.isBuilding() &&
			information_unit->type.getRace() == Races::Terran &&
			unit->isCompleted()) {
			bool repair_always = (information_unit->type == UnitTypes::Terran_Bunker ||
								  information_unit->type == UnitTypes::Terran_Missile_Turret ||
								  is_wall_building(information_unit));
			if ((repair_always &&
				 unit->getHitPoints() < information_unit->type.maxHitPoints()) ||
				(unit->getHitPoints() <= information_unit->type.maxHitPoints() / 3 &&
				 is_safe_to_repair(information_unit))) {
				damaged_buildings.emplace_back(unit, repair_always);
			}
		}
	}
	std::sort(damaged_buildings.begin(), damaged_buildings.end(), [](const auto& a,const auto& b) {
		return std::make_tuple(hitpoint_fraction(a.first), a.first->getID()) < std::make_tuple(hitpoint_fraction(b.first), b.first->getID());
	});
	for (auto [unit,repair_always] : damaged_buildings) {
		int worker_count = 1;
		if (repair_always) {
			int bunker_marines_loaded = 0;
			if (unit->getType() == UnitTypes::Terran_Bunker) {
				bunker_marines_loaded = information_manager.bunker_marines_loaded(unit);
			}
			double repair_hitpoints_per_frame = calculate_repair_hitpoints_per_frame(unit->getType());
			DPF dpf;
			for (InformationUnit* information_unit : information_manager.enemy_units()) {
				if (information_unit->position.isValid() &&
					!information_unit->is_disabled() &&
					can_attack_in_range_at_positions(information_unit->type, information_unit->position, information_unit->player, unit->getType(), unit->getPosition(), 32)) {
					dpf += calculate_damage_per_frame(information_unit->type, information_unit->player, unit->getType(), unit->getPlayer(), bunker_marines_loaded);
				}
			}
			worker_count = clamp(1, int(ceil(dpf.hp / repair_hitpoints_per_frame)), 6);
		}
		worker_manager.repair_target(unit, worker_count);
	}
}

void BuildingManager::continue_unfinished_buildings_without_worker()
{
	std::vector<Unit> unfinished_buildings_without_worker;
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		if (unit->getType().isBuilding() && unit->getType().getRace() == Races::Terran && !unit->isCompleted() &&
			!unit->getType().isAddon() && unit->getBuildUnit() == nullptr) {
			unfinished_buildings_without_worker.push_back(unit);
		}
	}
	for (auto& unit : unfinished_buildings_without_worker) {
		worker_manager.continue_unfinished_building(unit);
	}
}
void BuildingManager::apply_upgrades(bool important)
{
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		if (unit->isPowered() && unit->isIdle()) {
			for (auto [upgrade_type,upgrade_important] : upgrade_request_) {
                if (important == upgrade_important) {
                    int current_level = Broodwar->self()->getUpgradeLevel(upgrade_type);
                    if (current_level < upgrade_type.maxRepeats() &&
                        unit->getType().isSuccessorOf(upgrade_type.whatUpgrades()) &&
                        spending_manager.try_spend(MineralGas(upgrade_type, Broodwar->self()->getUpgradeLevel(upgrade_type) + 1))) {
                        unit->upgrade(upgrade_type);
                    }
                }
			}
		}
	}
}

void BuildingManager::apply_research(bool important)
{
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		if (unit->isPowered() && unit->isIdle()) {
			for (auto [tech_type,tech_important] : research_request_) {
				if (important == tech_important &&
					!Broodwar->self()->hasResearched(tech_type) &&
					unit->isIdle() &&
					tech_type.whatResearches() == unit->getType() &&
					spending_manager.try_spend(MineralGas(tech_type))) {
					unit->research(tech_type);
				}
			}
		}
	}
}

void BuildingManager::cancel_building(Unit building)
{
	building->cancelConstruction();
	worker_manager.cancel_building_construction(building->getTilePosition());
}

void BuildingManager::cancel_buildings_of_type(UnitType building_type)
{
	for (auto& unit : Broodwar->self()->getUnits()) {
		if (unit->getType() == building_type && !unit->isCompleted()) unit->cancelConstruction();
	}
	worker_manager.cancel_building_construction_for_type(building_type);
}

void BuildingManager::cancel_extra_buildings_of_type(UnitType building_type,int keep_including_warping,int keep_including_planned)
{
	int count = building_count(building_type);
	keep_including_warping = std::max(0, keep_including_warping - count);
	keep_including_planned = std::max(0, keep_including_planned - count);
	
	std::vector<InformationUnit*> incomplete_buildings;
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		if (unit->getType() == building_type && !unit->isCompleted()) {
			incomplete_buildings.push_back(information_unit);
		}
	}
	
	if (int(incomplete_buildings.size()) > keep_including_warping) {
		int cancel_count = int(incomplete_buildings.size()) - std::max(0, keep_including_warping);
		
		struct Order
		{
			inline bool operator()(const InformationUnit* a,const InformationUnit* b)
			{
				return std::make_tuple(a->unit->getHitPoints(), -a->unit->getID()) < std::make_tuple(b->unit->getHitPoints(), -b->unit->getID());
			}
		};
		std::sort(incomplete_buildings.begin(), incomplete_buildings.end(), Order());
		
		for (int i = 0; i < cancel_count; i++) {
			worker_manager.cancel_building_construction(incomplete_buildings[i]->unit->getTilePosition());
			incomplete_buildings[i]->unit->cancelConstruction();
		}
	}
	
	worker_manager.cancel_extra_planned_building_construction_for_type(building_type, keep_including_planned - int(incomplete_buildings.size()));
}

void BuildingManager::cancel_expansion_hatcheries()
{
	if (building_exists(UnitTypes::Zerg_Hatchery)) {
		for (auto& information_unit : information_manager.my_units()) {
			Unit unit = information_unit->unit;
			if (unit->getType() == UnitTypes::Zerg_Hatchery &&
				!unit->isCompleted() &&
				base_state.base_for_tile_position(unit->getTilePosition()) != nullptr) {
				unit->cancelConstruction();
			}
		}
		worker_manager.cancel_expansion_hatcheries();
	}
}

void BuildingManager::cancel_doomed_buildings()
{
	for (Unit unit : Broodwar->self()->getUnits()) {
		if (unit->getType().isBuilding() && !unit->isCompleted() && unit->isUnderAttack()) {
			double frames_to_live = calculate_frames_to_live(unit);
			if (frames_to_live < 24.0) unit->cancelConstruction();
		}
	}
}

void BuildingManager::onUnitLost(Unit unit)
{
	if (unit->getPlayer() == Broodwar->self() &&
		unit->getType().isBuilding()) {
		lost_building_count_++;
	}
}

MineralGas SpendingManager::income_per_minute() const
{
	return MineralGas(int(mineral_counter_.per_minute()),
					  int(gas_counter_.per_minute()));
}

CostPerMinute SpendingManager::training_cost_per_minute() const
{
	return training_cost_per_minute(Races::Protoss) + training_cost_per_minute(Races::Terran) + training_cost_per_minute(Races::Zerg);
}

CostPerMinute SpendingManager::training_cost_per_minute(Race race) const
{
	CostPerMinute result;
	switch (race) {
		case Races::Protoss:
			result = (training_cost_per_minute(training_manager.gateway_train_distribution()) +
					  training_cost_per_minute(training_manager.robotics_facility_train_distribution()) +
					  training_cost_per_minute(training_manager.stargate_train_distribution()));
			break;
		case Races::Terran:
			result = (training_cost_per_minute(training_manager.barracks_train_distribution()) +
					  training_cost_per_minute(training_manager.factory_train_distribution()) +
					  training_cost_per_minute(training_manager.starport_train_distribution()));
			break;
		case Races::Zerg: {
			int hatchery_count = building_manager.building_count(UnitTypes::Zerg_Hatchery);
			result = training_manager.larva_train_distribution().cost_per_minute(hatchery_count);
			if (training_manager.requested_lurker_count() < 0) {
				double hydra = training_manager.larva_train_distribution().get(UnitTypes::Zerg_Hydralisk);
				if (hydra > 0.0) {
					double factor = hatchery_count * hydra / training_manager.larva_train_distribution().sum();
					result += factor * TrainDistribution::cost_per_minute(UnitTypes::Zerg_Lurker);
				}
			}
			break; }
		default:
			break;
	}
	return result;
}

CostPerMinute SpendingManager::worker_training_cost_per_minute(Race race) const
{
	if (race != Races::Zerg &&
		training_manager.worker_count_including_unfinished() < worker_manager.determine_max_workers() &&
		training_manager.worker_production()) {
		int count = 0;
		for (Unit unit : Broodwar->self()->getUnits()) {
			if (unit->isCompleted() && unit->getType().isResourceDepot() &&
				(race == Races::None || unit->getType().getRace() == race) &&
				unit->getType().getRace() != Races::Zerg) {
				const BWEM::Base* base = base_state.base_for_tile_position(unit->getTilePosition());
				if (worker_manager.is_safe_base(base)) count++;
			}
		}
		
		return TrainDistribution::cost_per_minute(Broodwar->self()->getRace().getWorker()) * count;
	} else {
		return CostPerMinute();
	}
}

CostPerMinute SpendingManager::total_cost_per_minute() const
{
	return training_cost_per_minute() + worker_training_cost_per_minute();
}

MineralGas SpendingManager::remainder() const
{
	return income_per_minute() - MineralGas(total_cost_per_minute());
}

CostPerMinute SpendingManager::training_cost_per_minute(const TrainDistribution& train_queue)
{
	int active_building_count = 0;
	for (auto& building  : Broodwar->self()->getUnits()) {
		if (building->isCompleted() && building->isPowered() && building->getType() == train_queue.builder_type()) {
			active_building_count++;
		}
	}
	
	return train_queue.cost_per_minute(active_building_count);
}

void SpendingManager::init_resource_counters()
{
	mineral_counter_.init(Broodwar->self()->gatheredMinerals());
	gas_counter_.init(Broodwar->self()->gatheredGas());
}

void SpendingManager::init_spendable()
{
	spendable_ = MineralGas(Broodwar->self()) - worker_manager.worker_reserved_mineral_gas();
	spendable_out_of_minerals_first_ = false;
	spendable_out_of_gas_first_ = false;
	mineral_counter_.process_value(Broodwar->self()->gatheredMinerals());
	gas_counter_.process_value(Broodwar->self()->gatheredGas());
	spend(reserved_resources_);
	reserved_resources_ = MineralGas();
}

bool SpendingManager::try_spend(const MineralGas& mineral_gas)
{
	bool result = spendable_.can_pay(mineral_gas);
	spend(mineral_gas);
	return result;
}

bool SpendingManager::try_spend(const MineralGas& mineral_gas,int frames_ahead)
{
	MineralGas available = spendable_;
	MineralGas income = income_per_minute();
	if (frames_ahead > 0) {
		available.minerals += frames_ahead * income.minerals / (24 * 60);
		available.gas += frames_ahead * income.gas / (24 * 60);
	}
	bool result = available.can_pay(mineral_gas);
	spend(mineral_gas);
	return result;
}

void SpendingManager::spend(const MineralGas& mineral_gas)
{
	spendable_ -= mineral_gas;
	if (!spendable_out_of_minerals_first_ && !spendable_out_of_gas_first_) {
		if (spendable_.minerals < 0) spendable_out_of_minerals_first_ = true;
		if (spendable_.gas < 0) spendable_out_of_gas_first_ = true;
	}
}

int TrainingManager::unit_count_built_or_in_progress(UnitType unit_type)
{
	auto& count_record = unit_count_[unit_type];
	int training_count = count_record.count - count_record.count_completed;
	return unit_count_built(unit_type) + training_count;
}

void TrainingManager::apply_worker_train_orders()
{
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		if (unit->isCompleted() && (unit->getType() == UnitTypes::Protoss_Nexus || unit->getType() == UnitTypes::Terran_Command_Center)) {
			const BWEM::Base* base = base_state.base_for_tile_position(unit->getTilePosition());
			if (is_training_queue_empty(unit) &&
				worker_count_including_unfinished() < worker_manager.determine_max_workers() &&
				worker_manager.is_safe_base(base) &&
				can_train(unit->getType().getRace().getWorker())) {
				unit->train(unit->getType().getRace().getWorker());
			}
		}
	}
}

void TrainingManager::apply_train_orders()
{
	MineralGas reserved;
	
	apply_interceptor_train_orders();
	apply_scarab_train_orders();
	apply_lurker_morph_orders();
	apply_train_orders(robotics_facility_train_distribution_, reserved);
	apply_train_orders(stargate_train_distribution_, reserved);
	apply_train_orders(gateway_train_distribution_, reserved);
	apply_train_orders(starport_train_distribution_, reserved);
	apply_train_orders(factory_train_distribution_, reserved);
	apply_train_orders(barracks_train_distribution_, reserved);
	apply_train_orders(larva_train_distribution_, reserved);
	
	spending_manager.try_spend(reserved);
}

void TrainingManager::apply_interceptor_train_orders()
{
	const int max_interceptor_count = (Broodwar->self()->getUpgradeLevel(UpgradeTypes::Carrier_Capacity) == 0) ? 4 : 8;
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		if (unit->isCompleted() &&
			!is_disabled(unit) &&
			unit->getType() == UnitTypes::Protoss_Carrier &&
			unit->getInterceptorCount() < max_interceptor_count &&
			is_training_queue_empty(unit) &&
			can_train(UnitTypes::Protoss_Interceptor)) {
			unit->train(UnitTypes::Protoss_Interceptor);
		}
	}
}

void TrainingManager::apply_scarab_train_orders()
{
	const int max_scarab_count = 5;
	micro_manager.reset_drop_reaver_to_build_scarab();
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		if (unit->isCompleted() &&
			!is_disabled(unit) &&
			unit->getType() == UnitTypes::Protoss_Reaver &&
			unit->getScarabCount() < max_scarab_count &&
			is_training_queue_empty(unit) &&
			can_train(UnitTypes::Protoss_Scarab)) {
			if (!unit->isLoaded()) {
				unit->train(UnitTypes::Protoss_Scarab);
			} else {
				micro_manager.add_drop_reaver_to_build_scarab(unit);
			}
		}
	}
}

void TrainingManager::apply_lurker_morph_orders()
{
	if (requested_lurker_count_ < 0 || unit_count(UnitTypes::Zerg_Lurker) < requested_lurker_count_) {
		std::vector<const InformationUnit*> morphable_hydralisks;
		for (auto& information_unit : information_manager.my_units()) {
			Unit unit = information_unit->unit;
			if (unit->isCompleted() &&
				!is_disabled(unit) &&
				information_unit->type == UnitTypes::Zerg_Hydralisk) {
				morphable_hydralisks.push_back(information_unit);
			}
		}
		
		struct MorphableHydraliskSortOrder {
			bool operator()(const InformationUnit* a,const InformationUnit* b) const {
				return std::make_tuple(a->first_frame, a->unit->getID()) < std::make_tuple(b->first_frame, b->unit->getID());
			}
		};
		std::sort(morphable_hydralisks.begin(), morphable_hydralisks.end(), MorphableHydraliskSortOrder());
		
		int max_lurker_morph = int(morphable_hydralisks.size());
		if (requested_lurker_count_ > 0) {
			max_lurker_morph = requested_lurker_count_ - unit_count(UnitTypes::Zerg_Lurker);
		}
		for (auto& hydralisk_unit : morphable_hydralisks) {
			if (max_lurker_morph <= 0) break;
			max_lurker_morph--;
			if (can_train(UnitTypes::Zerg_Lurker)) {
				hydralisk_unit->unit->morph(UnitTypes::Zerg_Lurker);
			}
		}
	}
}

void TrainingManager::apply_train_orders(TrainDistribution& train_queue,MineralGas& reserved)
{
	MineralGas weighted_cost = train_queue.weighted_cost();
	bool inhibit_train_orders = false;
	
	for (auto& information_unit : information_manager.my_units()) {
		Unit building = information_unit->unit;
		if (building->isCompleted() && building->isPowered() && building->getType() == train_queue.builder_type()) {
			if (train_queue.is_nonempty()) {
				if (is_training_queue_empty(building)) {
					bool issued_train_order = false;
					if (!inhibit_train_orders) {
						UnitType unit_type = train_queue.sample();
						if (can_train(unit_type)) {
							building->train(unit_type);
							issued_train_order = true;
							if (train_queue.builder_type() == UnitTypes::Zerg_Larva) inhibit_train_orders = true;
						}
					}
					if (!issued_train_order) {
						reserved += weighted_cost;
					}
				} else {
					UnitType unit_type = building->getTrainingQueue()[0];
					double fraction_remaining = (double)building->getRemainingBuildTime() / (double)unit_type.buildTime();
					double fraction_finished = 1.0 - fraction_remaining;
					int minerals = int(fraction_finished * weighted_cost.minerals + 0.5);
					int gas = int(fraction_finished * weighted_cost.gas + 0.5);
					reserved += MineralGas(minerals, gas);
				}
			}
		}
	}
}

bool TrainingManager::is_training_queue_empty(Unit unit)
{
	return (unit->getTrainingQueue().empty() ||
			unit->getTrainingQueue().size() == 1 && unit->getRemainingTrainTime() == Broodwar->getRemainingLatencyFrames());
}

bool TrainingManager::can_train(UnitType type)
{
	Player self = Broodwar->self();
	int supply_left = self->supplyTotal(type.getRace()) - self->supplyUsed(type.getRace());
	
	bool enough_supply = (supply_left >= type.supplyRequired() || type.supplyRequired() == 0);
	bool enough_resources = spending_manager.try_spend(MineralGas(type));
	
	return enough_supply && enough_resources;
}

int TrainingManager::worker_count_including_unfinished()
{
	int result = 0;
	for (auto unit : Broodwar->self()->getUnits()) {
		if (unit->getType().isWorker()) result++;
	}
	return result;
}

void TrainingManager::update_overlord_training()
{
	if (automatic_supply_) {
		Player self = Broodwar->self();
		if (self->supplyTotal(Races::Zerg) < TrainingManager::kSupplyCap) {
			double supply_build_minutes = UnitTypes::Zerg_Overlord.buildTime() / (60.0 * 24.0);
			double supply_needed_per_minute = spending_manager.training_cost_per_minute(Races::Zerg).supply;
			double supply_requirement = supply_build_minutes * supply_needed_per_minute;
			if (requested_lurker_count_ != 0) {
				int max_lurker_morph = unit_count_completed(UnitTypes::Zerg_Hydralisk);
				if (requested_lurker_count_ > 0) {
					max_lurker_morph = clamp(0, max_lurker_morph, requested_lurker_count_ - unit_count(UnitTypes::Zerg_Lurker));
				}
				supply_requirement += max_lurker_morph * UnitTypes::Zerg_Lurker.supplyRequired();
			}
			double supply_shortage = self->supplyUsed(Races::Zerg) + supply_requirement - self->supplyTotal(Races::Zerg);
			int supply_building_shortage = int(ceil(supply_shortage / UnitTypes::Zerg_Overlord.supplyProvided()));
			if (unit_count(UnitTypes::Zerg_Overlord) < unit_count_completed(UnitTypes::Zerg_Overlord) + supply_building_shortage) {
				larva_train_distribution_.clear();
				larva_train_distribution_.set(UnitTypes::Zerg_Overlord, 1.0);
			}
		}
	}
}

void TrainingManager::init_unit_count_map()
{
	unit_count_.clear();
	for (Unit unit : Broodwar->self()->getUnits()) {
		if ((unit->getType() == UnitTypes::Zerg_Larva ||
			 unit->getType() == UnitTypes::Zerg_Egg ||
			 unit->getType() == UnitTypes::Zerg_Lurker_Egg ||
			 unit->getType() == UnitTypes::Zerg_Cocoon) &&
			unit->getBuildType() != UnitTypes::None) {
			unit_count_[unit->getBuildType()].count++;
			if (unit->getBuildType().isTwoUnitsInOneEgg()) unit_count_[unit->getBuildType()].count++;
		} else if (!unit->getType().isBuilding()) {
			unit_count_[unit->getType()].count++;
			if (unit->isCompleted()) unit_count_[unit->getType()].count_completed++;
			if (unit->isLoaded()) unit_count_[unit->getType()].count_loaded++;
		}
	}
	if (second_zerglings_expected_ > 0) unit_count_[UnitTypes::Zerg_Zergling].count += second_zerglings_expected_;
	if (second_scourges_expected_ > 0) unit_count_[UnitTypes::Zerg_Scourge].count += second_scourges_expected_;
}

void TrainingManager::onUnitMorph(Unit unit)
{
	if (unit->getPlayer() == Broodwar->self()) {
		if (unit->getType() == UnitTypes::Zerg_Zergling) second_zerglings_expected_++;
		if (unit->getType() == UnitTypes::Zerg_Scourge) second_scourges_expected_++;
	}
}

void TrainingManager::onUnitCreate(Unit unit)
{
	if (unit->getPlayer() == Broodwar->self()) {
		if (unit->getType() == UnitTypes::Zerg_Zergling) second_zerglings_expected_--;
		if (unit->getType() == UnitTypes::Zerg_Scourge) second_scourges_expected_--;
	}
}

void TrainingManager::onUnitComplete(Unit unit)
{
	if (unit->getPlayer() == Broodwar->self()) {
		if (unit->getType() == UnitTypes::Terran_Siege_Tank_Tank_Mode) {
			if (!contains(completed_tanks_, unit)) {
				units_built_[unit->getType()]++;
				completed_tanks_.insert(unit);
			}
		} else if (unit->getType() != UnitTypes::Terran_Siege_Tank_Siege_Mode) {
			units_built_[unit->getType()]++;
		}
	}
}

void TrainingManager::onUnitLost(Unit unit)
{
	if (unit->getPlayer() == Broodwar->self() &&
		unit->isCompleted() &&
		!unit->getType().isBuilding() &&
		!unit->getType().isWorker()) {
		lost_unit_count_++;
		lost_unit_supply_ += unit->getType().supplyRequired();
	}
	if (is_siege_tank(unit->getType())) {
		completed_tanks_.erase(unit);
	}
}

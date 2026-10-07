#include "BananaBrain.h"

void BuildingPlacementManager::init()
{
	init_power_grid();
	if (Broodwar->self()->getRace() == Races::Protoss) init_photon_cannon_positions();
	if (Broodwar->self()->getRace() == Races::Terran) init_missile_turret_positions();
	if (Broodwar->self()->getRace() == Races::Zerg) init_creep_colony_positions();
}

void BuildingPlacementManager::init_power_grid()
{
	init_power_grid(power_grid_2x2_, -4, -6, 5);
	init_power_grid(power_grid_2x2_, -3, -7, 6);
	init_power_grid(power_grid_2x2_, -2, -7, 7);
	init_power_grid(power_grid_2x2_, -1, -7, 7);
	init_power_grid(power_grid_2x2_, 0, -7, 7);
	init_power_grid(power_grid_2x2_, 1, -7, 7);
	init_power_grid(power_grid_2x2_, 2, -7, 6);
	init_power_grid(power_grid_2x2_, 3, -6, 5);
	init_power_grid(power_grid_2x2_, 4, -3, 2);
	
	init_power_grid(power_grid_3x2_, -4, -6, 5);
	init_power_grid(power_grid_3x2_, -3, -7, 6);
	init_power_grid(power_grid_3x2_, -2, -8, 7);
	init_power_grid(power_grid_3x2_, -1, -8, 7);
	init_power_grid(power_grid_3x2_, 0, -8, 7);
	init_power_grid(power_grid_3x2_, 1, -8, 7);
	init_power_grid(power_grid_3x2_, 2, -7, 6);
	init_power_grid(power_grid_3x2_, 3, -6, 5);
	init_power_grid(power_grid_3x2_, 4, -3, 2);
	
	init_power_grid(power_grid_4x3_, -5, -4, 1);
	init_power_grid(power_grid_4x3_, -4, -7, 4);
	init_power_grid(power_grid_4x3_, -3, -8, 5);
	init_power_grid(power_grid_4x3_, -2, -8, 6);
	init_power_grid(power_grid_4x3_, -1, -8, 6);
	init_power_grid(power_grid_4x3_, 0, -8, 6);
	init_power_grid(power_grid_4x3_, 1, -8, 6);
	init_power_grid(power_grid_4x3_, 2, -8, 5);
	init_power_grid(power_grid_4x3_, 3, -7, 4);
	init_power_grid(power_grid_4x3_, 4, -4, 1);
}

void BuildingPlacementManager::init_power_grid(std::set<TilePosition>& grid,int y,int xmin,int xmax)
{
	for (int x = xmin; x <= xmax; x++) grid.insert(TilePosition(x, y));
}

void BuildingPlacementManager::init_photon_cannon_positions()
{
	bool outside_placement = (opponent_model.enemy_race() == Races::Protoss);
	for (auto& base : base_state.bases()) {
		Position defense_center = calculate_defense_center(base);
		if (defense_center.isValid()) {
			Position base_center = base->Center();
			TilePosition base_position = base->Location();
			std::vector<TilePosition>& positions = photon_cannon_positions_[base];
			
			if (outside_placement) {
				if (base_center.y > defense_center.y) {
					if (base_center.x > defense_center.x) {
						positions.push_back(base_position + TilePosition(4, 0));
						positions.push_back(base_position + TilePosition(0, 3));
						positions.push_back(base_position + TilePosition(4, 3));
						pylon_positions_[base] = base_position + TilePosition(2, 3);
					} else {
						positions.push_back(base_position + TilePosition(-2, 3));
						positions.push_back(base_position + TilePosition(-2, 0));
						positions.push_back(base_position + TilePosition(2, 3));
						pylon_positions_[base] = base_position + TilePosition(0, 3);
					}
				} else {
					if (base_center.x > defense_center.x) {
						positions.push_back(base_position + TilePosition(4, -2));
						positions.push_back(base_position + TilePosition(0, -2));
						positions.push_back(base_position + TilePosition(4, 1));
						pylon_positions_[base] = base_position + TilePosition(2, -2);
					} else {
						positions.push_back(base_position + TilePosition(-2, -2));
						positions.push_back(base_position + TilePosition(2, -2));
						positions.push_back(base_position + TilePosition(-2, 1));
						pylon_positions_[base] = base_position + TilePosition(0, -2);
					}
				}
			} else {
				if (base_center.y > defense_center.y) {
					if (base_center.x > defense_center.x) {
						positions.push_back(base_position + TilePosition(-2, -2));
						positions.push_back(base_position + TilePosition(2, -2));
						positions.push_back(base_position + TilePosition(-2, 1));
						pylon_positions_[base] = base_position + TilePosition(4, 1);
					} else {
						positions.push_back(base_position + TilePosition(4, -2));
						positions.push_back(base_position + TilePosition(0, -2));
						positions.push_back(base_position + TilePosition(4, 1));
						pylon_positions_[base] = base_position + TilePosition(-2, 1);
					}
				} else {
					if (base_center.x > defense_center.x) {
						positions.push_back(base_position + TilePosition(-2, 3));
						positions.push_back(base_position + TilePosition(-2, 0));
						positions.push_back(base_position + TilePosition(2, 3));
						pylon_positions_[base] = base_position + TilePosition(4, 1);
					} else {
						positions.push_back(base_position + TilePosition(4, 0));
						positions.push_back(base_position + TilePosition(0, 3));
						positions.push_back(base_position + TilePosition(4, 3));
						pylon_positions_[base] = base_position + TilePosition(-2, 1);
					}
				}
			}
		}
	}
}

void BuildingPlacementManager::init_missile_turret_positions()
{
	bool inside_placement = (opponent_model.enemy_race() != Races::Terran && opponent_model.enemy_race() != Races::Protoss);
	for (auto& base : base_state.bases()) {
		Position defense_center = calculate_defense_center(base);
		if (defense_center.isValid()) {
			Position base_center = base->Center();
			TilePosition base_position = base->Location();
			std::vector<TilePosition>& positions = missile_turret_positions_[base];
			
			bool yflag = (base_center.y > defense_center.y);
			bool xflag = (base_center.x > defense_center.x);
			if (inside_placement) {
				yflag = !yflag;
				xflag = !xflag;
			}
			
			if (yflag) {
				if (xflag) {
					positions.push_back(base_position + TilePosition(4, -1));
					positions.push_back(base_position + TilePosition(0, 3));
					positions.push_back(base_position + TilePosition(4, 3));
				} else {
					positions.push_back(base_position + TilePosition(-2, 3));
					positions.push_back(base_position + TilePosition(-2, 0));
					positions.push_back(base_position + TilePosition(2, 3));
				}
			} else {
				if (xflag) {
					positions.push_back(base_position + TilePosition(4, -2));
					positions.push_back(base_position + TilePosition(0, -2));
					positions.push_back(base_position + TilePosition(4, 3));
				} else {
					positions.push_back(base_position + TilePosition(-2, -2));
					positions.push_back(base_position + TilePosition(2, -2));
					positions.push_back(base_position + TilePosition(-2, 1));
				}
			}
		}
	}
}

void BuildingPlacementManager::init_creep_colony_positions()
{
	for (auto& base : base_state.bases()) {
		std::set<const BWEM::Area*> areas;
		areas.insert(base->GetArea());
		
		TileGrid<bool> blocked;
		insert_unbuildable_nonarea_locations(blocked, areas);
		insert_building_locations(blocked);
		insert_base_locations(blocked);
		
		FastTilePosition base_tile_position = base->Location();
		for (int dx = 0; dx < UnitTypes::Zerg_Hatchery.tileWidth(); dx++) {
			blocked.set(base_tile_position + FastTilePosition(dx, UnitTypes::Zerg_Hatchery.tileHeight()), true);
		}
		for (int dy = 0; dy < UnitTypes::Zerg_Hatchery.tileHeight() + 1; dy++) {
			blocked.set(base_tile_position + FastTilePosition(-1, dy), true);
			blocked.set(base_tile_position + FastTilePosition(UnitTypes::Zerg_Hatchery.tileWidth(), dy), true);
		}
		
		FastPosition base_position = base->Center();
		FastPosition sum(0, 0);
		int count = 0;
		
		for (auto& mineral : base->Minerals()) {
			sum += mineral->Pos();
			sum += base_position;
			count += 2;
		}
		FastPosition center_position;
		if (count == 0) {
			center_position = base_position;
		} else {
			center_position = sum / count;
		}
		
		FastTilePosition best_tile_position = TilePositions::None;
		int best_distance = INT_MAX;
		for (int y = 0; y < Broodwar->mapHeight(); y++) {
			for (int x = 0; x < Broodwar->mapWidth(); x++) {
				FastTilePosition candidate_tile_position(x, y);
				if (rectangle_buildable_for_building_with_clearance(candidate_tile_position, UnitTypes::Zerg_Creep_Colony, blocked, 0)) {
					FastPosition candidate_position = center_position_for(UnitTypes::Zerg_Creep_Colony, candidate_tile_position);
					int distance = center_position.getApproxDistance(candidate_position);
					if (distance < best_distance) {
						best_tile_position = candidate_tile_position;
						best_distance = distance;
					}
				}
			}
		}
		
		if (best_tile_position.isValid()) {
			creep_colony_positions_[base] = best_tile_position;
		}
	}
}

Position BuildingPlacementManager::calculate_defense_center(const BWEM::Base* base)
{
	Position mineral_center;
	int mineral_center_count = 0;
	for (auto& mineral : base->Minerals()) {
		mineral_center += mineral->Pos();
		mineral_center_count++;
	}
	
	Position defense_center;
	int defense_center_count = 0;
	if (mineral_center_count > 0) {
		defense_center = mineral_center / mineral_center_count;
		defense_center_count++;
	}
	for (auto& geyser : base->Geysers()) {
		defense_center += geyser->Pos();
		defense_center_count++;
	}
	
	return (defense_center_count > 0) ? (defense_center / defense_center_count) : Positions::None;
}

TilePosition BuildingPlacementManager::place_building(UnitType unit_type)
{
	TileGrid<bool> blocked;
	insert_building_locations(blocked);
	insert_threatened_ground_locations(blocked);
	
	TileGrid<bool> resource_blocked;
	insert_resource_locations(resource_blocked);
	
	if (specific_positions_.count(unit_type) > 0) {
		TileGrid<bool> power_grid = determine_power_grid(unit_type);
		for (auto& tile_position : specific_positions_[unit_type]) {
			if (test_power_requirement(power_grid, unit_type, tile_position) &&
				test_building_location(blocked, resource_blocked, unit_type, tile_position, false)) {
				return tile_position;
			}
		}
	}
	
	insert_specific_building_locations(blocked);
	
	if (unit_type == UnitTypes::Terran_Missile_Turret) {
		struct PotentialLocationForBase {
			int count = 0;
			TilePosition tile_position = TilePositions::None;
		};
		
		std::map<const BWEM::Base*,PotentialLocationForBase> potential_locations;
		for (auto& entry : missile_turret_positions_) {
			const BWEM::Base* base = entry.first;
			for (auto& tile_position : entry.second) {
				if (test_building_location(blocked, resource_blocked, UnitTypes::Terran_Missile_Turret, tile_position, false)) {
					PotentialLocationForBase& potential = potential_locations[base];
					potential.count++;
					if (potential.count == 1) potential.tile_position = tile_position;
				}
			}
		}
		
		if (!potential_locations.empty()) {
			auto it = std::min_element(potential_locations.begin(), potential_locations.end(), [](auto& entry1,auto& entry2){
				return entry1.second.count < entry2.second.count;
			});
			return (*it).second.tile_position;
		}
	}
	
	if (unit_type == UnitTypes::Protoss_Photon_Cannon) {
		TileGrid<bool> power_grid = determine_power_grid(UnitTypes::Protoss_Photon_Cannon);
		
		struct PotentialLocationForBase {
			int count = 0;
			TilePosition tile_position = TilePositions::None;
		};
		
		std::map<const BWEM::Base*,PotentialLocationForBase> potential_locations;
		for (auto& entry : photon_cannon_positions_) {
			const BWEM::Base* base = entry.first;
			for (auto& tile_position : entry.second) {
				if (test_power_requirement(power_grid, UnitTypes::Protoss_Photon_Cannon, tile_position) &&
					test_building_location(blocked, resource_blocked, UnitTypes::Protoss_Photon_Cannon, tile_position, false)) {
					PotentialLocationForBase& potential = potential_locations[base];
					potential.count++;
					if (potential.count == 1) potential.tile_position = tile_position;
				}
			}
		}
		
		if (!potential_locations.empty()) {
			auto it = std::min_element(potential_locations.begin(), potential_locations.end(), [](auto& entry1,auto& entry2){
				return entry1.second.count < entry2.second.count;
			});
			return (*it).second.tile_position;
		}
	}
	
	insert_base_locations(blocked);
	insert_padded_resource_locations(resource_blocked, unit_type.isResourceDepot());
	insert_base_defense_photon_cannon_locations(blocked);
	insert_base_defense_missile_turret_locations(blocked);
	insert_base_defense_creep_colony_locations(blocked);
	
	if (unit_type == UnitTypes::Protoss_Pylon) {
		std::set<const BWEM::Base*> bases(base_state.controlled_and_planned_bases());
		
		TilePosition result = TilePositions::None;
		
		const BWEM::Base* start_base = base_state.start_base();
		if (bases.count(start_base) > 0) {
			result = place_pylon(blocked, resource_blocked, pylon_placement_start_location(start_base));
			bases.erase(start_base);
		}
		
		for (auto& base : bases) {
			if (result.isValid()) break;
			result = place_pylon(blocked, resource_blocked, pylon_placement_start_location(base));
		}
		
		return result;
	} else if (unit_type.requiresPsi()) {
		std::vector<TilePosition> pylon_positions = determine_completed_pylon_positions();
		TilePosition start_position = Broodwar->self()->getStartLocation();
		std::sort(pylon_positions.begin(), pylon_positions.end(), [start_position](TilePosition a,TilePosition b){
			return start_position.getApproxDistance(a) < start_position.getApproxDistance(b);
		});
		for (TilePosition pylon_position : pylon_positions) {
			const BWEM::Area* pylon_area = bwem_map.GetArea(pylon_position);
			for (int dy = -5; dy <= 4; dy++) {
				for (int dx = -8; dx <= 8; dx++) {
					TilePosition candidate = pylon_position + TilePosition(dx, dy);
					if (!candidate.isValid()) continue;
					if (!tile_in_controlled_area(candidate, pylon_area)) continue;
					if (check_power(pylon_position, unit_type, candidate) &&
						test_building_location(blocked, resource_blocked, unit_type, candidate)) return candidate;
				}
			}
		}
		return TilePositions::None;
	} else {
		std::set<const BWEM::Base*> bases(base_state.controlled_and_planned_bases());
		
		TilePosition result = TilePositions::None;
		
		const BWEM::Base* start_base = base_state.start_base();
		if (bases.count(start_base) > 0) {
			result = place_other_building(blocked, resource_blocked, start_base->Location(), unit_type);
			bases.erase(start_base);
		}
		
		for (auto& base : bases) {
			if (result.isValid()) break;
			result = place_other_building(blocked, resource_blocked, base->Location(), unit_type);
		}
		
		return result;
	}
}

TilePosition BuildingPlacementManager::place_base_defense_pylon(const BWEM::Base* base)
{
	TileGrid<bool> blocked;
	insert_building_locations(blocked);
	insert_base_locations(blocked);
	insert_specific_building_locations(blocked);
	insert_base_defense_photon_cannon_locations(blocked);
	insert_threatened_ground_locations(blocked);
	
	TileGrid<bool> resource_blocked;
	insert_resource_locations(resource_blocked);
	
	TilePosition base_pylon_position = pylon_positions_[base];
	if (test_building_location(blocked, resource_blocked, UnitTypes::Protoss_Pylon, base_pylon_position, false)) {
		return base_pylon_position;
	}
	
	int min_distance = INT_MAX;
	TilePosition pylon_tile_positions = TilePositions::None;
	for (int dy = -7; dy <= 7; dy++) {
		for (int dx = 7; dx <= 7; dx++) {
			if (dx == 0 && dy == 0) continue;
			TilePosition delta{dx, dy};
			TilePosition tile_position = base_pylon_position + delta;
			if (test_building_location(blocked, resource_blocked, UnitTypes::Protoss_Pylon, tile_position, false)) {
				bool all_powered = true;
				for (auto& cannon_position : photon_cannon_positions_[base]) {
					if (!check_power(tile_position, UnitTypes::Protoss_Photon_Cannon, cannon_position)) {
						all_powered = false;
						break;
					}
				}
				if (all_powered) {
					int distance = center_position(base_pylon_position).getApproxDistance(center_position(tile_position));
					if (distance < min_distance) {
						min_distance = distance;
						pylon_tile_positions = tile_position;
					}
				}
			}
		}
	}
	
	return pylon_tile_positions;
}

TilePosition BuildingPlacementManager::place_base_defense_photon_cannon(const BWEM::Base* base)
{
	TileGrid<bool> blocked;
	insert_building_locations(blocked);
	insert_base_locations(blocked);
	insert_specific_building_locations(blocked);
	insert_threatened_ground_locations(blocked);
	
	TileGrid<bool> resource_blocked;
	insert_resource_locations(resource_blocked);
	
	TileGrid<bool> power_grid = determine_power_grid(UnitTypes::Protoss_Photon_Cannon);
	
	for (auto& tile_position : photon_cannon_positions_[base]) {
		if (test_power_requirement(power_grid, UnitTypes::Protoss_Photon_Cannon, tile_position) &&
			test_building_location(blocked, resource_blocked, UnitTypes::Protoss_Photon_Cannon, tile_position, false)) {
			return tile_position;
		}
	}
	return TilePositions::None;
}

TilePosition BuildingPlacementManager::place_base_defense_missile_turret(const BWEM::Base* base)
{
	TileGrid<bool> blocked;
	insert_building_locations(blocked);
	insert_base_locations(blocked);
	insert_specific_building_locations(blocked);
	insert_threatened_ground_locations(blocked);
	
	TileGrid<bool> resource_blocked;
	insert_resource_locations(resource_blocked);
	
	for (auto& tile_position : missile_turret_positions_[base]) {
		if (test_building_location(blocked, resource_blocked, UnitTypes::Terran_Missile_Turret, tile_position, false)) {
			return tile_position;
		}
	}
	return TilePositions::None;
}

TilePosition BuildingPlacementManager::place_base_defense_creep_colony(const BWEM::Base* base)
{
	TileGrid<bool> blocked;
	insert_building_locations(blocked);
	insert_base_locations(blocked);
	insert_specific_building_locations(blocked);
	insert_threatened_ground_locations(blocked);
	
	TileGrid<bool> resource_blocked;
	insert_resource_locations(resource_blocked);
	
	auto it = creep_colony_positions_.find(base);
	if (it != creep_colony_positions_.end()) {
		TilePosition tile_position = it->second;
		if (test_building_location(blocked, resource_blocked, UnitTypes::Zerg_Creep_Colony, tile_position, false)) {
			return tile_position;
		}
	}
	return TilePositions::None;
}

TilePosition BuildingPlacementManager::pylon_placement_start_location(const BWEM::Base* base)
{
	TilePosition tile_position = pylon_positions_[base];
	for (int dy = 0; dy < UnitTypes::Protoss_Pylon.tileHeight(); dy++) {
		for (int dx = 0; dx < UnitTypes::Protoss_Pylon.tileWidth(); dx++) {
			if (!Broodwar->isBuildable(tile_position + TilePosition(dx, dy))) return base->Location();
		}
	}
	return tile_position;
}

TilePosition BuildingPlacementManager::place_pylon(const TileGrid<bool>& blocked,const TileGrid<bool>& resource_blocked,TilePosition start_location)
{
	const BWEM::Area* start_area = bwem_map.GetArea(start_location);
	std::vector<TilePosition> pylon_positions = determine_pylon_positions();
	
	std::queue<TilePosition> queue;
	queue.push(start_location);
	std::set<TilePosition> seen;
	seen.insert(start_location);
	
	const auto blocked_by_base_defense_pylon_positions = [this](const TilePosition& candidate){
		for (auto& entry : pylon_positions_) {
			TilePosition delta = entry.second - candidate;
			if (delta != TilePosition(0, 0) &&
				std::abs(delta.x) < UnitTypes::Protoss_Pylon.tileWidth() &&
				std::abs(delta.y) < UnitTypes::Protoss_Pylon.tileHeight()) {
				return true;
			}
		}
		return false;
	};
	
	while (!queue.empty()) {
		TilePosition candidate = queue.front();
		queue.pop();
		seen.insert(candidate);
		
		if (!candidate.isValid()) continue;
		if (!tile_in_controlled_area(candidate, start_area)) continue;
		
		if (test_building_location(blocked, resource_blocked, UnitTypes::Protoss_Pylon, candidate) &&
			test_distance_from_existing_pylons(pylon_positions, candidate) &&
			!blocked_by_base_defense_pylon_positions(candidate)) return candidate;
		
		for (TilePosition delta : { TilePosition(-1, 0), TilePosition(1, 0), TilePosition(0, -1), TilePosition(0, 1) }) {
			TilePosition next = candidate + delta;
			if (seen.count(next) == 0) {
				queue.push(next);
				seen.insert(next);
			}
		}
	}
	
	return TilePositions::None;
}

TilePosition BuildingPlacementManager::place_other_building(const TileGrid<bool>& blocked,const TileGrid<bool>& resource_blocked,TilePosition start_location,UnitType type)
{
	const BWEM::Area* start_area = bwem_map.GetArea(start_location);
	
	std::queue<TilePosition> queue;
	queue.push(start_location);
	std::set<TilePosition> seen;
	seen.insert(start_location);
	
	while (!queue.empty()) {
		TilePosition candidate = queue.front();
		queue.pop();
		seen.insert(candidate);
		
		if (!candidate.isValid()) continue;
		if (!tile_in_controlled_area(candidate, start_area)) continue;
		
		if (test_building_location(blocked, resource_blocked, type, candidate)) return candidate;
		
		for (TilePosition delta : { TilePosition(-1, 0), TilePosition(1, 0), TilePosition(0, -1), TilePosition(0, 1) }) {
			TilePosition next = candidate + delta;
			if (seen.count(next) == 0) {
				queue.push(next);
				seen.insert(next);
			}
		}
	}
	
	return TilePositions::None;
}

std::vector<TilePosition> BuildingPlacementManager::determine_pylon_positions()
{
	std::vector<TilePosition> result;
	
	for (auto& unit : Broodwar->self()->getUnits()) {
		if (unit->getType() == UnitTypes::Protoss_Pylon) result.push_back(unit->getTilePosition());
	}
	
	for (auto& entry : worker_manager.worker_map()) {
		const Worker& worker = entry.second;
		if (worker.order()->building_type() == UnitTypes::Protoss_Pylon) {
			result.push_back(worker.order()->building_position());
		}
	}
	
	return result;
}

std::vector<TilePosition> BuildingPlacementManager::determine_completed_pylon_positions()
{
	std::vector<TilePosition> result;
	
	for (auto& unit : Broodwar->self()->getUnits()) {
		if (unit->getType() == UnitTypes::Protoss_Pylon && unit->isCompleted()) result.push_back(unit->getTilePosition());
	}
	
	return result;
}

bool BuildingPlacementManager::test_distance_from_existing_pylons(const std::vector<TilePosition>& pylon_positions,TilePosition tile_position)
{
	for (auto& pylon_position : pylon_positions) {
		int dx = tile_position.x - pylon_position.x;
		int dy = tile_position.y - pylon_position.y;
		if (abs(dx) < 6 && abs(dy) < 5) return false;
	}
	return true;
}

bool BuildingPlacementManager::tile_in_controlled_area(TilePosition tile_position,const BWEM::Area* start_area)
{
	const std::set<const BWEM::Area*>& areas = base_state.controlled_areas();
	
	const BWEM::Area* area = bwem_map.GetArea(tile_position);
	if (area != nullptr) {
		return area == start_area || areas.count(area) > 0;
	} else {
		WalkPosition walk_position(tile_position);
		for (int dy = 0; dy < 4; dy++) {
			for (int dx = 0; dx < 4; dx++) {
				area = bwem_map.GetArea(walk_position + WalkPosition(dx, dy));
				if (area != nullptr && (area == start_area || areas.count(area) > 0)) return true;
			}
		}
		return false;
	}
}

void BuildingPlacementManager::insert_building_location(TileGrid<bool>& blocked,UnitType unit_type,TilePosition base_position)
{
	for (int dy = 0; dy < unit_type.tileHeight(); dy++) {
		for (int dx = 0; dx < unit_type.tileWidth(); dx++) {
			blocked.set(base_position + TilePosition(dx, dy), true);
		}
	}
	if (unit_type.canBuildAddon()) {
		for (int dy = 0; dy < 2; dy++) {
			for (int dx = 0; dx < 2; dx++) {
				blocked.set(base_position + TilePosition(unit_type.tileWidth() + dx, unit_type.tileHeight() - 2 + dy), true);
			}
		}
	}
}

void BuildingPlacementManager::remove_building_location(TileGrid<bool>& blocked,UnitType unit_type,TilePosition base_position)
{
	for (int dy = 0; dy < unit_type.tileHeight(); dy++) {
		for (int dx = 0; dx < unit_type.tileWidth(); dx++) {
			blocked.set(base_position + TilePosition(dx, dy), false);
		}
	}
}

void BuildingPlacementManager::insert_building_location_with_padding(TileGrid<bool>& blocked,UnitType unit_type,TilePosition base_position,int padding)
{
	for (int dy = -padding; dy < unit_type.tileHeight() + padding; dy++) {
		for (int dx = -padding; dx < unit_type.tileWidth() + padding; dx++) {
			blocked.set(base_position + TilePosition(dx, dy), true);
		}
	}
}

bool BuildingPlacementManager::test_power_requirement(const TileGrid<bool>& power_grid,UnitType unit_type,TilePosition base_position)
{
	return !unit_type.requiresPsi() || power_grid.get(base_position);
}

bool BuildingPlacementManager::test_building_location(const TileGrid<bool>& blocked,const TileGrid<bool>& resource_blocked,UnitType unit_type,TilePosition base_position,bool group_check)
{
	for (int dy = 0; dy < unit_type.tileHeight(); dy++) {
		for (int dx = 0; dx < unit_type.tileWidth(); dx++) {
			TilePosition tile_position = base_position + TilePosition(dx, dy);
			if (!Broodwar->isBuildable(tile_position) ||
				(Broodwar->hasCreep(tile_position) != unit_type.requiresCreep() && !unit_type.isResourceDepot())) return false;
			if (blocked.get(tile_position) || resource_blocked.get(tile_position)) return false;
		}
	}
	if (unit_type.canBuildAddon()) {
		for (int dy = 0; dy < 2; dy++) {
			for (int dx = 0; dx < 2; dx++) {
				TilePosition tile_position = base_position + TilePosition(unit_type.tileWidth() + dx, unit_type.tileHeight() - 2 + dy);
				if (!Broodwar->isBuildable(tile_position) ||
					(Broodwar->hasCreep(tile_position) != unit_type.requiresCreep() && !unit_type.isResourceDepot())) return false;
				if (blocked.get(tile_position) || resource_blocked.get(tile_position)) return false;
			}
		}
	}
	
	if (group_check) {
		std::set<TilePosition> group = determine_connected_tiles(blocked, unit_type, base_position);
		if (unit_type != UnitTypes::Protoss_Photon_Cannon && group.size() >= 8 * 6) return false;
		std::set<TilePosition> border = determine_border(group);
		if (!is_connected(border)) return false;
		for (auto location : border) if (!walkability_grid.is_terrain_walkable(location)) return false;
	}
	
	return true;
}

std::set<TilePosition> BuildingPlacementManager::determine_border(const std::set<TilePosition>& positions)
{
	std::set<TilePosition> result;
	
	for (TilePosition position : positions) {
		for (TilePosition delta : { TilePosition(-1, -1), TilePosition(0, -1), TilePosition(1, -1),
			TilePosition(-1, 0), TilePosition(1, 0),
			TilePosition(-1, 1), TilePosition(0, 1), TilePosition(1, 1) }) {
				TilePosition tile_position = position + delta;
				if (positions.count(tile_position) == 0) result.insert(tile_position);
			}
	}
	
	return result;
}

std::set<TilePosition> BuildingPlacementManager::determine_connected_tiles(const TileGrid<bool>& blocked,UnitType unit_type,TilePosition base_position)
{
	std::set<TilePosition> done;
	std::queue<TilePosition> queue;
	for (int dy = 0; dy < unit_type.tileHeight(); dy++) {
		for (int dx = 0; dx < unit_type.tileWidth(); dx++) {
			TilePosition tile_position = base_position + TilePosition(dx, dy);
			queue.push(tile_position);
			done.insert(tile_position);
		}
	}
	if (unit_type.canBuildAddon()) {
		for (int dy = 0; dy < 2; dy++) {
			for (int dx = 0; dx < 2; dx++) {
				TilePosition tile_position = base_position + TilePosition(unit_type.tileWidth() + dx, unit_type.tileHeight() - 2 + dy);
				queue.push(tile_position);
				done.insert(tile_position);
			}
		}
	}
	
	while (!queue.empty()) {
		TilePosition current = queue.front();
		queue.pop();
		
		for (TilePosition delta : { TilePosition(-1, -1), TilePosition(0, -1), TilePosition(1, -1),
			TilePosition(-1, 0), TilePosition(1, 0),
			TilePosition(-1, 1), TilePosition(0, 1), TilePosition(1, 1) }) {
				TilePosition tile_position = current + delta;
				if (blocked.get(tile_position) && done.count(tile_position) == 0) {
					queue.push(tile_position);
					done.insert(tile_position);
				}
			}
	}
	
	return done;
}

bool BuildingPlacementManager::is_connected(const std::set<TilePosition>& positions)
{
	if (positions.empty()) return true;
	std::set<TilePosition> done;
	std::queue<TilePosition> queue;
	TilePosition first = *(positions.begin());
	done.insert(first);
	queue.push(first);
	
	while (!queue.empty()) {
		TilePosition current = queue.front();
		queue.pop();
		
		for (TilePosition delta : { TilePosition(-1, -1), TilePosition(0, -1), TilePosition(1, -1),
			TilePosition(-1, 0), TilePosition(1, 0),
			TilePosition(-1, 1), TilePosition(0, 1), TilePosition(1, 1) }) {
				TilePosition tile_position = current + delta;
				if (positions.count(tile_position) > 0 && done.count(tile_position) == 0) {
					queue.push(tile_position);
					done.insert(tile_position);
				}
			}
	}
	
	return done.size() == positions.size();
}

void BuildingPlacementManager::draw()
{
	for (auto& entry : pylon_positions_) {
		Rect rect(UnitTypes::Protoss_Pylon, entry.second, true);
		rect.draw(Colors::Orange);
	}
	for (auto& entry : photon_cannon_positions_) {
		for (auto& tile_position : entry.second) {
			Rect rect(UnitTypes::Protoss_Photon_Cannon, tile_position, true);
			rect.draw(Colors::Orange);
		}
	}
	for (auto& entry : missile_turret_positions_) {
		for (auto& tile_position : entry.second) {
			Rect rect(UnitTypes::Terran_Missile_Turret, tile_position, true);
			rect.draw(Colors::Orange);
		}
	}
	for (auto& entry : creep_colony_positions_) {
		Rect rect(UnitTypes::Zerg_Creep_Colony, entry.second, true);
		rect.draw(Colors::Orange);
	}
	for (auto& entry : specific_positions_) {
		UnitType type = entry.first;
		for (auto& tile_position : entry.second) {
			Rect rect(type, tile_position, true);
			rect.draw(Colors::Orange);
		}
	}
}

void BuildingPlacementManager::insert_building_locations(TileGrid<bool>& blocked)
{
	for (Unit unit : Broodwar->self()->getUnits()) {
		UnitType unit_type = unit->getType();
		if (unit_type.isBuilding()) {
			if (unit->isLifted()) {
				if (unit->getOrder() == Orders::BuildingLand) {
					Position position = unit->getOrderTargetPosition();
					TilePosition tile_position = TilePosition(Position(position.x - unit->getType().tileWidth() * 16,
																	   position.y - unit->getType().tileHeight() * 16));
					insert_building_location(blocked, unit->getType(), tile_position);
				}
			} else {
				insert_building_location(blocked, unit_type, unit->getTilePosition());
			}
		}
		if (unit_type.isResourceDepot() && unit_type.getRace() == Races::Zerg) {
			for (int dx = 0; dx < unit_type.tileWidth(); dx++) {
				blocked.set(unit->getTilePosition() + TilePosition(dx, unit_type.tileHeight()), true);
			}
			for (int dy = 0; dy < unit_type.tileHeight() + 1; dy++) {
				blocked.set(unit->getTilePosition() + TilePosition(-1, dy), true);
				blocked.set(unit->getTilePosition() + TilePosition(unit_type.tileWidth(), dy), true);
			}
		}
	}
	
	for (auto& neutral_unit : information_manager.neutral_units()) {
		if (!neutral_unit->flying && !neutral_unit->type.isMineralField() && neutral_unit->type != UnitTypes::Resource_Vespene_Geyser) {
			insert_building_location(blocked, neutral_unit->type, neutral_unit->tile_position());
		}
	}
	
	for (auto& entry : worker_manager.worker_map()) {
		const Worker& worker = entry.second;
		UnitType unit_type = worker.order()->building_type();
		if (unit_type != UnitTypes::None) {
			insert_building_location(blocked, unit_type, worker.order()->building_position());
		}
	}
}

void BuildingPlacementManager::insert_base_locations(TileGrid<bool>& blocked)
{
	for (auto& base : base_state.bases()) {
		insert_building_location(blocked, UnitTypes::Protoss_Nexus, base->Location());
	}
}

void BuildingPlacementManager::insert_padded_resource_locations(TileGrid<bool>& blocked,bool resource_depot)
{
	int padding = resource_depot ? 3 : 2;
	
	for (Unit unit : Broodwar->self()->getUnits()) {
		UnitType unit_type = unit->getType();
		if (unit_type.isRefinery()) {
			insert_building_location_with_padding(blocked, unit_type, unit->getTilePosition(), padding);
		}
	}
	
	for (auto& neutral_unit : information_manager.neutral_units()) {
		if (neutral_unit->type.isMineralField() || neutral_unit->type == UnitTypes::Resource_Vespene_Geyser) {
			insert_building_location_with_padding(blocked, neutral_unit->type, neutral_unit->tile_position(), padding);
		}
	}
}

void BuildingPlacementManager::insert_resource_locations_pad_minerals(TileGrid<bool>& blocked)
{
	for (Unit unit : Broodwar->self()->getUnits()) {
		UnitType unit_type = unit->getType();
		if (unit_type.isRefinery()) {
			insert_building_location(blocked, unit_type, unit->getTilePosition());
		}
	}
	
	for (auto& neutral_unit : information_manager.neutral_units()) {
		if (neutral_unit->type.isMineralField()) {
			insert_building_location_with_padding(blocked, neutral_unit->type, neutral_unit->tile_position(), 2);
		} else if (neutral_unit->type == UnitTypes::Resource_Vespene_Geyser) {
			insert_building_location(blocked, neutral_unit->type, neutral_unit->tile_position());
		}
	}
}

void BuildingPlacementManager::insert_resource_locations(TileGrid<bool>& blocked)
{
	for (Unit unit : Broodwar->self()->getUnits()) {
		UnitType unit_type = unit->getType();
		if (unit_type.isRefinery()) {
			insert_building_location(blocked, unit_type, unit->getTilePosition());
		}
	}
	
	for (auto& neutral_unit : information_manager.neutral_units()) {
		if (neutral_unit->type.isMineralField() || neutral_unit->type == UnitTypes::Resource_Vespene_Geyser) {
			insert_building_location(blocked, neutral_unit->type, neutral_unit->tile_position());
		}
	}
}

void BuildingPlacementManager::insert_specific_building_locations(TileGrid<bool>& blocked)
{
	for (auto& entry : specific_positions_) {
		UnitType unit_type = entry.first;
		for (auto& tile_position : entry.second) {
			insert_building_location(blocked, unit_type, tile_position);
		}
	}
}

void BuildingPlacementManager::insert_base_defense_photon_cannon_locations(TileGrid<bool>& blocked)
{
	for (auto& entry : photon_cannon_positions_) {
		for (auto& tile_position : entry.second) {
			insert_building_location(blocked, UnitTypes::Protoss_Photon_Cannon, tile_position);
		}
	}
}

void BuildingPlacementManager::insert_base_defense_missile_turret_locations(TileGrid<bool>& blocked)
{
	for (auto& entry : missile_turret_positions_) {
		for (auto& tile_position : entry.second) {
			insert_building_location(blocked, UnitTypes::Terran_Missile_Turret, tile_position);
		}
	}
}

void BuildingPlacementManager::insert_base_defense_creep_colony_locations(TileGrid<bool>& blocked)
{
	for (auto& entry : creep_colony_positions_) {
		insert_building_location(blocked, UnitTypes::Zerg_Creep_Colony, entry.second);
	}
}

void BuildingPlacementManager::insert_threatened_ground_locations(TileGrid<bool>& blocked)
{
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			TilePosition tile_position(x, y);
			if (threat_grid.ground_threat(tile_position)) blocked.set(tile_position, true);
		}
	}
}

void BuildingPlacementManager::insert_neutral_creep(TileGrid<bool>& blocked)
{
	for (auto& neutral_unit : information_manager.neutral_units()) {
		std::unordered_set<FastTilePosition> creep_tiles = creep_spread(neutral_unit->type, neutral_unit->position);
		for (FastTilePosition tile_position : creep_tiles) {
			blocked.set(tile_position, true);
		}
	}
}

TileGrid<bool> BuildingPlacementManager::determine_power_grid(UnitType unit_type)
{
	TileGrid<bool> result;
	
	if (unit_type.requiresPsi()) {
		const std::set<TilePosition>* grid = nullptr;
		if (unit_type.tileWidth() == 2 && unit_type.tileHeight() == 2) {
			grid = &power_grid_2x2_;
		} else if (unit_type.tileWidth() == 3 && unit_type.tileHeight() == 2) {
			grid = &power_grid_3x2_;
		} else if (unit_type.tileWidth() == 4 && unit_type.tileHeight() == 3) {
			grid = &power_grid_4x3_;
		}
		
		if (grid != nullptr) {
			for (Unit unit : Broodwar->self()->getUnits()) {
				if (unit->getType() == UnitTypes::Protoss_Pylon && unit->isCompleted()) {
					for (TilePosition tile_position : *grid) {
						result.set(tile_position + unit->getTilePosition(), true);
					}
				}
			}
		}
	}
	
	return result;
}

bool BuildingPlacementManager::check_power(TilePosition pylon_position,UnitType building_type,TilePosition building_position)
{
	if (!building_type.requiresPsi()) return true;
	const std::set<TilePosition>* grid = nullptr;
	if (building_type.tileWidth() == 2 && building_type.tileHeight() == 2) {
		grid = &power_grid_2x2_;
	} else if (building_type.tileWidth() == 3 && building_type.tileHeight() == 2) {
		grid = &power_grid_3x2_;
	} else if (building_type.tileWidth() == 4 && building_type.tileHeight() == 3) {
		grid = &power_grid_4x3_;
	}
	if (grid == nullptr) return false;
	return grid->count(building_position - pylon_position) > 0;
}

TilePosition BuildingPlacementManager::calculate_proxy_barracks_position()
{
	if (proxy_barracks_position_.isValid() && proxy_second_barracks_position_.isValid()) return TilePositions::None;
	
	struct ProxyPosition
	{
		TilePosition barracks_position;
		int distance = 0;
	};
	
	std::set<const BWEM::Area*> non_start_areas;
	for (auto& base : base_state.bases()) if (!base->Starting()) non_start_areas.insert(base->GetArea());
	
	TileGrid<bool> blocked;
	insert_unbuildable_nonarea_locations(blocked, non_start_areas);
	insert_building_locations(blocked);
	insert_resource_locations(blocked);
	insert_neutral_creep(blocked);
	if (proxy_barracks_position_.isValid()) {
		insert_building_location(blocked, UnitTypes::Terran_Barracks, proxy_barracks_position_);
	}
	insert_scout_path_on_two_player_maps(blocked);
	
	std::vector<ProxyPosition> candidate_proxy_positions;
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			TilePosition tile_position(x, y);
			if (rectangle_buildable_for_building_with_clearance(tile_position, UnitTypes::Terran_Barracks, blocked)) {
				Position position = center_position_for(UnitTypes::Terran_Barracks, tile_position);
				int distance = maximum_distance_from_possible_opponent_starting_bases(position);
				if (distance > 0) {
					ProxyPosition candidate { tile_position, distance };
					candidate_proxy_positions.push_back(candidate);
				}
			}
		}
	}
	
	ProxyPosition proxy_position = smallest_priority(candidate_proxy_positions, [](const ProxyPosition& proxy_position){
		return proxy_position.distance;
	});
	
	if (proxy_position.distance > 0) {
		specific_positions_[UnitTypes::Terran_Barracks].push_back(proxy_position.barracks_position);
		if (!proxy_barracks_position_.isValid()) {
			proxy_barracks_position_ = proxy_position.barracks_position;
		} else {
			proxy_second_barracks_position_ = proxy_position.barracks_position;
		}
		return proxy_position.barracks_position;
	} else {
		return TilePositions::None;
	}
}

TilePosition BuildingPlacementManager::calculate_proxy_gateway_position()
{
	const int gateway_width = UnitTypes::Protoss_Gateway.tileWidth();
	const int gateway_height = UnitTypes::Protoss_Gateway.tileHeight();
	const int pylon_width = UnitTypes::Protoss_Pylon.tileWidth();
	const int pylon_height = UnitTypes::Protoss_Pylon.tileHeight();
	
	struct ProxyPosition
	{
		TilePosition pylon_position;
		TilePosition first_gateway_position;
		TilePosition second_gateway_position;
		int distance = 0;
	};
	
	std::set<const BWEM::Area*> non_start_areas;
	for (auto& base : base_state.bases()) if (!base->Starting()) non_start_areas.insert(base->GetArea());
	
	TileGrid<bool> blocked;
	insert_unbuildable_nonarea_locations(blocked, non_start_areas);
	insert_building_locations(blocked);
	insert_resource_locations(blocked);
	insert_neutral_creep(blocked);
	insert_scout_path_on_two_player_maps(blocked);
	
	std::vector<ProxyPosition> deltas{
		{ TilePosition(gateway_width, 0), TilePosition(0, 0), TilePosition(gateway_width + pylon_width, 0) },
		{ TilePosition(0, gateway_height), TilePosition(0, 0), TilePosition(0, gateway_height + pylon_height) },
		{ TilePosition(gateway_width - pylon_width, gateway_height - pylon_height), TilePosition(0, gateway_height), TilePosition(gateway_width, 0) },
		{ TilePosition(gateway_width, gateway_height), TilePosition(0, gateway_height), TilePosition(gateway_width, 0) },
		{ TilePosition(gateway_width, gateway_height), TilePosition(0, 0), TilePosition(gateway_width + pylon_width, gateway_height + pylon_height) },
		{ TilePosition(gateway_width, gateway_height), TilePosition(gateway_width + pylon_width, 0), TilePosition(0, gateway_height + pylon_height) }
	};
	
	std::vector<ProxyPosition> candidate_proxy_positions;
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			TilePosition tile_position(x, y);
			for (auto& delta : deltas) {
				ProxyPosition candidate{
					tile_position + delta.pylon_position,
					tile_position + delta.first_gateway_position,
					tile_position + delta.second_gateway_position
				};
				if (rectangle_buildable_for_building_with_clearance(candidate.pylon_position, UnitTypes::Protoss_Pylon, blocked) &&
					rectangle_buildable_for_building_with_clearance(candidate.first_gateway_position, UnitTypes::Protoss_Gateway, blocked) &&
					rectangle_buildable_for_building_with_clearance(candidate.second_gateway_position, UnitTypes::Protoss_Gateway, blocked)) {
					Position position = center_position_for(UnitTypes::Protoss_Pylon, candidate.pylon_position);
					int distance = maximum_distance_from_possible_opponent_starting_bases(position);
					if (distance > 0) {
						candidate.distance = distance;
						candidate_proxy_positions.push_back(candidate);
					}
				}
			}
		}
	}
	
	ProxyPosition proxy_position = smallest_priority(candidate_proxy_positions, [](const ProxyPosition& proxy_position){
		return proxy_position.distance;
	});
	if (proxy_position.distance > 0) {
		specific_positions_[UnitTypes::Protoss_Pylon].push_back(proxy_position.pylon_position);
		specific_positions_[UnitTypes::Protoss_Gateway].push_back(proxy_position.first_gateway_position);
		specific_positions_[UnitTypes::Protoss_Gateway].push_back(proxy_position.second_gateway_position);
		proxy_pylon_position_ = proxy_position.pylon_position;
		return proxy_position.pylon_position;
	} else {
		return TilePositions::None;
	}
}

void BuildingPlacementManager::calculate_gateway_near_choke_position()
{
	struct GatePosition
	{
		TilePosition gateway_position;
		int distance = 0;
		
		bool operator<(const GatePosition& o) const { return std::make_tuple(distance, gateway_position) < std::make_tuple(o.distance, o.gateway_position); }
	};
	
	std::set<const BWEM::Area*> component_areas = base_state.connected_areas(base_state.main_base()->GetArea(), base_state.controlled_and_planned_areas());
	Border border(component_areas);
	if (border.chokepoints().size() == 1) {
		const BWEM::ChokePoint* chokepoint = chokepoint = *(border.chokepoints().begin());
		Position choke_center = chokepoint_center(chokepoint);
		
		TileGrid<bool> blocked;
		insert_unbuildable_nonarea_locations(blocked, base_state.controlled_and_planned_areas());
		insert_building_locations(blocked);
		insert_base_locations(blocked);
		insert_base_defense_photon_cannon_locations(blocked);
		insert_resource_locations(blocked);
		insert_neutral_creep(blocked);
		
		std::vector<GatePosition> candidate_positions;
		for (int y = 0; y < Broodwar->mapHeight(); y++) {
			for (int x = 0; x < Broodwar->mapWidth(); x++) {
				TilePosition gateway_position{x, y};
				if (rectangle_buildable_for_building_with_clearance(gateway_position, UnitTypes::Protoss_Gateway, blocked, 2)) {
					int distance = calculate_distance(UnitTypes::Protoss_Gateway, center_position_for(UnitTypes::Protoss_Gateway, gateway_position), choke_center);
					candidate_positions.push_back(GatePosition{gateway_position, distance});
				}
			}
		}
		std::sort(candidate_positions.begin(), candidate_positions.end());
		
		for (auto& candidate_position : candidate_positions) {
			TilePosition gateway_position = candidate_position.gateway_position;
			int min_distance = INT_MAX;
			TilePosition best_pylon_position = TilePositions::None;
			for (int y = 0; y < Broodwar->mapHeight(); y++) {
				for (int x = 0; x < Broodwar->mapWidth(); x++) {
					TilePosition pylon_position{x, y};
					if (rectangle_buildable_for_building_with_clearance(pylon_position, UnitTypes::Protoss_Pylon, blocked) &&
						check_power(pylon_position, UnitTypes::Protoss_Gateway, gateway_position)) {
						int distance = calculate_distance(UnitTypes::Protoss_Pylon, center_position_for(UnitTypes::Protoss_Pylon, pylon_position),
														  UnitTypes::Protoss_Nexus, base_state.start_base()->Center());
						if (distance < min_distance) {
							min_distance = distance;
							best_pylon_position = pylon_position;
						}
					}
				}
			}
			if (best_pylon_position.isValid()) {
				specific_positions_[UnitTypes::Protoss_Pylon].push_back(best_pylon_position);
				specific_positions_[UnitTypes::Protoss_Gateway].push_back(gateway_position);
				return;
			}
		}
	}
}

void BuildingPlacementManager::calculate_two_gateways_near_choke_position(bool include_cannon)
{
	const int gateway_width = UnitTypes::Protoss_Gateway.tileWidth();
	const int gateway_height = UnitTypes::Protoss_Gateway.tileHeight();
	const int pylon_width = UnitTypes::Protoss_Pylon.tileWidth();
	const int pylon_height = UnitTypes::Protoss_Pylon.tileHeight();
	
	struct TwoGatePosition
	{
		TilePosition pylon_position;
		TilePosition first_gateway_position;
		TilePosition second_gateway_position;
		TilePosition cannon_position;
		int distance = 0;
	};
	
	std::set<const BWEM::Area*> component_areas = base_state.connected_areas(base_state.main_base()->GetArea(), base_state.controlled_and_planned_areas());
	Border border(component_areas);
	if (border.chokepoints().size() == 1) {
		const BWEM::ChokePoint* chokepoint = chokepoint = *(border.chokepoints().begin());
		Position choke_center = chokepoint_center(chokepoint);
		
		TileGrid<bool> blocked;
		insert_unbuildable_nonarea_locations(blocked, base_state.controlled_and_planned_areas());
		insert_building_locations(blocked);
		insert_base_locations(blocked);
		insert_base_defense_photon_cannon_locations(blocked);
		insert_resource_locations(blocked);
		insert_neutral_creep(blocked);
		
		std::vector<TwoGatePosition> deltas{
			{ TilePosition(gateway_width, 0), TilePosition(0, 0), TilePosition(gateway_width + pylon_width, 0) },
			{ TilePosition(0, gateway_height), TilePosition(0, 0), TilePosition(0, gateway_height + pylon_height) },
			{ TilePosition(gateway_width - pylon_width, gateway_height - pylon_height), TilePosition(0, gateway_height), TilePosition(gateway_width, 0) },
			{ TilePosition(gateway_width, gateway_height), TilePosition(0, gateway_height), TilePosition(gateway_width, 0) },
			{ TilePosition(gateway_width, gateway_height), TilePosition(0, 0), TilePosition(gateway_width + pylon_width, gateway_height + pylon_height) },
			{ TilePosition(gateway_width, gateway_height), TilePosition(gateway_width + pylon_width, 0), TilePosition(0, gateway_height + pylon_height) }
		};
		
		std::vector<TwoGatePosition> candidate_positions;
		for (int y = 0; y < Broodwar->mapHeight(); y++) {
			for (int x = 0; x < Broodwar->mapWidth(); x++) {
				TilePosition tile_position(x, y);
				for (auto& delta : deltas) {
					TwoGatePosition candidate{
						tile_position + delta.pylon_position,
						tile_position + delta.first_gateway_position,
						tile_position + delta.second_gateway_position,
						TilePositions::None
					};
					if (rectangle_buildable_for_building_with_clearance(candidate.pylon_position, UnitTypes::Protoss_Pylon, blocked, 2) &&
						rectangle_buildable_for_building_with_clearance(candidate.first_gateway_position, UnitTypes::Protoss_Gateway, blocked, 2) &&
						rectangle_buildable_for_building_with_clearance(candidate.second_gateway_position, UnitTypes::Protoss_Gateway, blocked, 2)) {
						int first_gateway_distance = calculate_distance(UnitTypes::Protoss_Gateway, center_position_for(UnitTypes::Protoss_Gateway, candidate.first_gateway_position), choke_center);
						int second_gateway_distance = calculate_distance(UnitTypes::Protoss_Gateway, center_position_for(UnitTypes::Protoss_Gateway, candidate.second_gateway_position), choke_center);
						candidate.distance = first_gateway_distance + second_gateway_distance;
						if (!include_cannon) {
							candidate_positions.push_back(candidate);
						} else if (first_gateway_distance >= 128 &&
							second_gateway_distance >= 128) {
							TileGrid<bool> blocked_inner(blocked);
							TilePosition pylon_position = candidate.pylon_position;
							insert_building_location(blocked_inner, UnitTypes::Protoss_Pylon, pylon_position);
							insert_building_location(blocked_inner, UnitTypes::Protoss_Gateway, candidate.first_gateway_position);
							insert_building_location(blocked_inner, UnitTypes::Protoss_Gateway, candidate.second_gateway_position);
							std::vector<TilePosition> candidate_cannon_positions;
							for (int yc = pylon_position.y - 8; yc <= pylon_position.y + 8; yc++) {
								for (int xc = pylon_position.x - 8; xc <= pylon_position.x + 8; xc++) {
									TilePosition cannon_position{xc, yc};
									if (check_power(pylon_position, UnitTypes::Protoss_Photon_Cannon, cannon_position) &&
										rectangle_buildable_for_building_with_clearance(cannon_position, UnitTypes::Protoss_Photon_Cannon, blocked_inner) &&
										calculate_distance(UnitTypes::Protoss_Photon_Cannon, center_position_for(UnitTypes::Protoss_Photon_Cannon, cannon_position), choke_center) >= 128) {
										candidate_cannon_positions.push_back(cannon_position);
									}
								}
							}
							candidate.cannon_position = smallest_priority(candidate_cannon_positions, [choke_center](auto& cannon_position){
								return calculate_distance(UnitTypes::Protoss_Photon_Cannon, center_position_for(UnitTypes::Protoss_Photon_Cannon, cannon_position), choke_center);
							});
							if (candidate.cannon_position.isValid()) {
								candidate_positions.push_back(candidate);
							}
						}
					}
				}
			}
		}
		
		TwoGatePosition two_gate_position = smallest_priority(candidate_positions, [](const TwoGatePosition& two_gate_position){
			return two_gate_position.distance;
		});
		if (two_gate_position.distance > 0) {
			specific_positions_[UnitTypes::Protoss_Pylon].push_back(two_gate_position.pylon_position);
			specific_positions_[UnitTypes::Protoss_Gateway].push_back(two_gate_position.first_gateway_position);
			specific_positions_[UnitTypes::Protoss_Gateway].push_back(two_gate_position.second_gateway_position);
			if (two_gate_position.cannon_position.isValid()) {
				specific_positions_[UnitTypes::Protoss_Photon_Cannon].push_back(two_gate_position.cannon_position);
			}
		}
	}
}

void BuildingPlacementManager::calculate_shield_battery_near_choke_position()
{
	struct ShieldBatteryPosition
	{
		TilePosition shield_battery_position;
		int distance = 0;
		
		bool operator<(const ShieldBatteryPosition& o) const { return std::make_tuple(distance, shield_battery_position) < std::make_tuple(o.distance, o.shield_battery_position); }
	};
	
	std::set<const BWEM::Area*> component_areas = base_state.connected_areas(base_state.main_base()->GetArea(), base_state.controlled_and_planned_areas());
	Border border(component_areas);
	if (border.chokepoints().size() == 1) {
		const BWEM::ChokePoint* chokepoint = chokepoint = *(border.chokepoints().begin());
		Position choke_center = chokepoint_center(chokepoint);
		
		TileGrid<bool> blocked;
		insert_unbuildable_nonarea_locations(blocked, base_state.controlled_and_planned_areas());
		insert_building_locations(blocked);
		insert_base_locations(blocked);
		insert_base_defense_photon_cannon_locations(blocked);
		insert_resource_locations(blocked);
		insert_neutral_creep(blocked);
		
		TileGrid<bool> power_grid = determine_power_grid(UnitTypes::Protoss_Shield_Battery);
		
		std::vector<ShieldBatteryPosition> candidate_positions;
		for (int y = 0; y < Broodwar->mapHeight(); y++) {
			for (int x = 0; x < Broodwar->mapWidth(); x++) {
				TilePosition shield_battery_position{x, y};
				if (power_grid.get(shield_battery_position) &&
					rectangle_buildable_for_building_with_clearance(shield_battery_position, UnitTypes::Protoss_Shield_Battery, blocked, 0)) {
					int distance = calculate_distance(UnitTypes::Protoss_Shield_Battery, center_position_for(UnitTypes::Protoss_Shield_Battery, shield_battery_position), choke_center);
					candidate_positions.push_back(ShieldBatteryPosition{shield_battery_position, distance});
				}
			}
		}
		
		ShieldBatteryPosition shield_battery_position = smallest_priority(candidate_positions, [](const ShieldBatteryPosition& shield_battery_position){
			return shield_battery_position.distance;
		});
		
		if (shield_battery_position.distance > 0) {
			specific_positions_[UnitTypes::Protoss_Shield_Battery].push_back(shield_battery_position.shield_battery_position);
		}
	}
}

TilePosition BuildingPlacementManager::calculate_bunker_near_choke_position(bool include_natural)
{
	std::set<const BWEM::Area*> areas = base_state.controlled_and_planned_areas();
	if (include_natural && base_state.natural_base() != nullptr) areas.insert(base_state.natural_base()->GetArea());
	std::set<const BWEM::Area*> component_areas = base_state.connected_areas(base_state.main_base()->GetArea(), areas);
	Border border(component_areas);
	if (border.chokepoints().size() >= 1) {
		key_value_vector<const BWEM::ChokePoint*,int> chokepoint_width_map;
		for (auto& chokepoint : border.chokepoints()) {
			chokepoint_width_map.emplace_back(chokepoint, chokepoint_width(chokepoint));
		}
		
		const BWEM::ChokePoint* chokepoint = key_with_largest_value(chokepoint_width_map);
		Position choke_position = chokepoint_center(chokepoint);
		
		TileGrid<bool> blocked;
		insert_unbuildable_nonarea_locations(blocked, areas);
		insert_building_locations(blocked);
		insert_padded_resource_locations(blocked);
		insert_base_locations(blocked);
		insert_base_defense_missile_turret_locations(blocked);
		
		struct BunkerPosition
		{
			FastTilePosition bunker_position;
			int distance = 0;
		};
		
		std::vector<BunkerPosition> candidate_bunker_positions;
		
		for (int y = 0; y < Broodwar->mapHeight(); y++) {
			for (int x = 0; x < Broodwar->mapWidth(); x++) {
				FastTilePosition bunker_position(x, y);
				if (rectangle_buildable_for_building_with_clearance(bunker_position, UnitTypes::Terran_Bunker, blocked)) {
					int distance = calculate_distance(UnitTypes::Terran_Bunker, center_position_for(UnitTypes::Terran_Bunker, bunker_position), choke_position);
					if (distance >= 48 && distance <= 320) {
						candidate_bunker_positions.push_back(BunkerPosition{bunker_position, distance});
					}
				}
			}
		}
		
		BunkerPosition bunker_position = smallest_priority(candidate_bunker_positions, [](const BunkerPosition& bunker_position){
			return bunker_position.distance;
		});
		
		if (bunker_position.distance > 0) {
			specific_positions_[UnitTypes::Terran_Bunker].push_back(bunker_position.bunker_position);
			return bunker_position.bunker_position;
		}
	}
	
	return TilePositions::None;
}

TilePosition BuildingPlacementManager::calculate_bunker_near_mineral_line_position()
{
	const BWEM::Base* base = base_state.start_base();
	std::set<const BWEM::Area*> areas;
	areas.insert(base->GetArea());
	
	Border border(areas);
	const BWEM::ChokePoint* chokepoint = border.largest_chokepoint_with_area(base->GetArea());
	FastPosition choke_center = (chokepoint != nullptr) ? chokepoint_center(chokepoint) : Positions::None;
	FastPosition base_center = base->Center();
	int choke_base_distance = (chokepoint != nullptr) ? choke_center.getApproxDistance(base_center) : 0;
	
	FastPosition sum(0, 0);
	int count = 0;
	for (auto& mineral : base->Minerals()) {
		sum += mineral->Pos();
		sum += base_center;
		count += 2;
	}
	FastPosition center_position;
	if (count == 0) {
		center_position = base_center;
	} else {
		center_position = sum / count;
	}
	
	TileGrid<bool> blocked;
	insert_unbuildable_nonarea_locations(blocked, areas);
	insert_building_locations(blocked);
	insert_resource_locations_pad_minerals(blocked);
	insert_base_locations(blocked);
	
	FastTilePosition best_tile_position = TilePositions::None;
	int best_distance = INT_MAX;
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			FastTilePosition tile_position(x, y);
			if (rectangle_buildable_for_building_with_clearance(tile_position, UnitTypes::Terran_Bunker, blocked, 0)) {
				FastPosition position = center_position_for(UnitTypes::Terran_Bunker, tile_position);
				int distance = position.getApproxDistance(center_position);
				if ((!choke_center.isValid() || position.getApproxDistance(choke_center) < choke_base_distance) &&
					distance < best_distance) {
					best_tile_position = tile_position;
					best_distance = distance;
				}
			}
		}
	}
	
	if (best_tile_position.isValid()) {
		specific_positions_[UnitTypes::Terran_Bunker].push_back(best_tile_position);
		return best_tile_position;
	}
	
	return TilePositions::None;
}

TilePosition BuildingPlacementManager::calculate_barracks_near_bunker_position(FastTilePosition bunker_tile_position,FastTilePosition original_barracks_tile_position)
{
	const BWEM::Base* base = base_state.start_base();
	std::set<const BWEM::Area*> areas;
	areas.insert(base->GetArea());
	
	TileGrid<bool> blocked;
	insert_unbuildable_nonarea_locations(blocked, areas);
	insert_building_locations(blocked);
	insert_resource_locations_pad_minerals(blocked);
	insert_base_locations(blocked);
	insert_specific_building_locations(blocked);
	
	FastPosition bunker_position = center_position_for(UnitTypes::Terran_Bunker, bunker_tile_position);
	FastPosition original_barracks_position = center_position_for(UnitTypes::Terran_Barracks, original_barracks_tile_position);
	int bunker_original_barracks_distance = calculate_distance(UnitTypes::Terran_Bunker, bunker_position, original_barracks_position);
	
	FastTilePosition best_tile_position = TilePositions::None;
	int best_distance = INT_MAX;
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			FastTilePosition tile_position(x, y);
			if (rectangle_buildable_for_building_with_clearance(tile_position, UnitTypes::Terran_Barracks, blocked, 0)) {
				FastPosition position = center_position_for(UnitTypes::Terran_Barracks, tile_position);
				int barracks_move_distance = original_barracks_position.getApproxDistance(position);
				if (barracks_move_distance <= bunker_original_barracks_distance) {
					int distance = calculate_distance(UnitTypes::Terran_Bunker, bunker_position, UnitTypes::Terran_Barracks, position);
					if (distance < best_distance) {
						best_tile_position = tile_position;
						best_distance = distance;
					}
				}
			}
		}
	}
	
	if (best_tile_position.isValid()) {
		specific_positions_[UnitTypes::Terran_Barracks].push_back(best_tile_position);
		return best_tile_position;
	}
	
	return TilePositions::None;
}

TilePosition BuildingPlacementManager::calculate_command_center_in_main_position()
{
	TilePosition result = TilePositions::None;
	if (base_state.natural_base() != nullptr) {
		TileGrid<bool> blocked;
		insert_unbuildable_nonarea_locations(blocked, base_state.controlled_areas());
		insert_building_locations(blocked);
		insert_padded_resource_locations(blocked);
		insert_base_locations(blocked);
		insert_base_defense_missile_turret_locations(blocked);
		
		std::set<const BWEM::Area*> component_areas = base_state.connected_areas(base_state.main_base()->GetArea(), base_state.controlled_and_planned_areas());
		Border border(component_areas);
		std::vector<FastPosition> choke_centers;
		for (auto chokepoint : border.chokepoints()) {
			choke_centers.push_back(chokepoint_center(chokepoint));
		}
		
		int min_distance = INT_MAX;
		for (int y = 0; y < Broodwar->mapHeight(); y++) {
			for (int x = 0; x < Broodwar->mapWidth(); x++) {
				TilePosition command_center_position{x, y};
				if (rectangle_buildable_for_building_with_clearance(command_center_position, UnitTypes::Terran_Command_Center, blocked)) {
					bool far_enough_from_chokepoints = std::all_of(choke_centers.begin(), choke_centers.end(),
																   [command_center_position](auto choke_center){
																	   int distance = calculate_distance(UnitTypes::Terran_Command_Center,
																										 center_position_for(UnitTypes::Terran_Command_Center, command_center_position),
																										 choke_center);
																	   return distance >= 128;
																   });
					if (far_enough_from_chokepoints) {
						Position position = center_position_for(UnitTypes::Terran_Command_Center, command_center_position);
						int distance = position.getApproxDistance(base_state.natural_base()->Center());
						if (distance < min_distance) {
							min_distance = distance;
							result = command_center_position;
						}
					}
				}
			}
		}
	}
	
	if (result.isValid()) {
		specific_positions_[UnitTypes::Terran_Command_Center].push_back(result);
	}
	return result;
}

bool BuildingPlacementManager::calculate_forge_fast_expand_position()
{
	WallPlacement::FFEPlacer placer;
	return placer.place_ffe();
}

int BuildingPlacementManager::calculate_defense_creep_colony_positions(const BWEM::Base* base,int count_requested)
{
	std::set<const BWEM::Area*> component_areas = base_state.connected_areas(base->GetArea(), base_state.controlled_and_planned_areas());
	Border border(component_areas);
	const BWEM::ChokePoint* chokepoint = border.largest_chokepoint_with_area(base->GetArea());
	
	FastPosition choke_center = (chokepoint != nullptr) ? chokepoint_center(chokepoint) : Positions::None;
	FastPosition base_center = base->Center();
	int choke_base_distance = (chokepoint != nullptr) ? choke_center.getApproxDistance(base_center) : 0;
	
	std::set<const BWEM::Area*> base_area_set;
	base_area_set.insert(base->GetArea());
	
	TileGrid<bool> blocked;
	insert_unbuildable_nonarea_locations(blocked, base_area_set);
	insert_building_locations(blocked);
	insert_padded_resource_locations(blocked);
	insert_base_locations(blocked);
	
	struct ColonyPosition
	{
		FastTilePosition tile_position;
		int distance;
		
		bool operator<(const ColonyPosition& o) const
		{
			return std::make_tuple(distance, tile_position) < std::make_tuple(o.distance, o.tile_position);
		}
	};
	
	std::vector<ColonyPosition> candidate_positions;
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			FastTilePosition tile_position(x, y);
			if (rectangle_buildable_for_building_with_clearance(tile_position, UnitTypes::Zerg_Creep_Colony, blocked, 0)) {
				FastPosition position = center_position_for(UnitTypes::Zerg_Creep_Colony, tile_position);
				int base_distance = position.getApproxDistance(base_center);
				if (base_distance >= 128 &&
					(!choke_center.isValid() || position.getApproxDistance(choke_center) < choke_base_distance)) {
					candidate_positions.push_back(ColonyPosition{ tile_position, base_distance });
				}
			}
		}
	}
	
	std::sort(candidate_positions.begin(), candidate_positions.end());
	
	TileGrid<bool> building_blocked;
	insert_building_locations(building_blocked);
	insert_base_locations(building_blocked);
	
	int count_placed = 0;
	for (auto& candidate_position : candidate_positions) {
		if (count_placed >= count_requested) break;
		if (test_building_location(building_blocked, blocked, UnitTypes::Zerg_Creep_Colony, candidate_position.tile_position)) {
			specific_positions_[UnitTypes::Zerg_Creep_Colony].push_back(candidate_position.tile_position);
			insert_building_location(blocked, UnitTypes::Zerg_Creep_Colony, candidate_position.tile_position);
			insert_building_location(building_blocked, UnitTypes::Zerg_Creep_Colony, candidate_position.tile_position);
			count_placed++;
		}
	}
	return count_placed;
}

bool BuildingPlacementManager::calculate_macro_hatch_position(const BWEM::Base* base)
{
	std::set<const BWEM::Area*> component_areas = base_state.connected_areas(base->GetArea(), base_state.controlled_and_planned_areas());
	Border border(component_areas);
	const BWEM::ChokePoint* chokepoint = border.largest_chokepoint_with_area(base->GetArea());
	FastPosition near_position;
	if (chokepoint != nullptr) {
		near_position = chokepoint_center(chokepoint);
	} else {
		near_position = base->Center();
	}
	
	std::set<const BWEM::Area*> base_area_set;
	base_area_set.insert(base->GetArea());
	
	TileGrid<bool> blocked;
	insert_unbuildable_nonarea_locations(blocked, base_area_set);
	insert_building_locations(blocked);
	insert_padded_resource_locations(blocked, true);
	insert_base_locations(blocked);
	
	struct HatcheryPosition
	{
		FastTilePosition tile_position;
		int distance = 0;
	};
	
	std::vector<HatcheryPosition> candidate_positions;
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			FastTilePosition tile_position(x, y);
			if (rectangle_buildable_for_building_with_clearance(tile_position, UnitTypes::Zerg_Hatchery, blocked)) {
				FastPosition position = center_position_for(UnitTypes::Zerg_Hatchery, tile_position);
				int distance = position.getApproxDistance(near_position);
				if (chokepoint == nullptr || distance >= 128) {
					candidate_positions.push_back(HatcheryPosition{ tile_position, distance });
				}
			}
		}
	}
	
	HatcheryPosition hatchery_position = smallest_priority(candidate_positions, [](auto& hatchery_position){
		return hatchery_position.distance;
	});
	
	bool result = false;
	if (hatchery_position.distance > 0) {
		specific_positions_[UnitTypes::Zerg_Hatchery].push_back(hatchery_position.tile_position);
	}
	return result;
}

bool BuildingPlacementManager::calculate_tvz_wall_position(bool require_placable_machine_shop)
{
	WallPlacement::TvZWallPlacer placer(require_placable_machine_shop);
	return placer.place_wall();
}

bool BuildingPlacementManager::calculate_tvp_wall_position(bool require_placable_machine_shop)
{
	WallPlacement::TvPWallPlacer placer(require_placable_machine_shop);
	return placer.place_wall();
}

bool BuildingPlacementManager::calculate_tvz_natural_wall_position(bool place_bunker)
{
	WallPlacement::TvZNaturalWallPlacer placer(place_bunker);
	return placer.place_wall();
}

bool BuildingPlacementManager::calculate_tvt_natural_wall_position(bool place_bunker)
{
	WallPlacement::TvTNaturalWallPlacer placer(place_bunker);
	return placer.place_wall();
}

bool BuildingPlacementManager::calculate_tvp_natural_wall_position(bool place_bunker)
{
	WallPlacement::TvPNaturalWallPlacer placer(place_bunker);
	return placer.place_wall();
}

std::vector<TilePosition> BuildingPlacementManager::determine_second_gateway_positions(const TileGrid<bool>& blocked,TilePosition first_gateway_position)
{
	std::vector<TilePosition> all_positions;
	
	int x1 = first_gateway_position.x - UnitTypes::Protoss_Gateway.tileWidth();
	int y1 = first_gateway_position.y - UnitTypes::Protoss_Gateway.tileHeight();
	int x2 = first_gateway_position.x + UnitTypes::Protoss_Gateway.tileWidth();
	int y2 = first_gateway_position.y + UnitTypes::Protoss_Gateway.tileHeight();
	
	for (int x = x1 + 1; x <= x2 - 1; x++) {
		all_positions.push_back(TilePosition(x, y1));
		all_positions.push_back(TilePosition(x, y2));
	}
	for (int y = y1 + 1; y <= y2 - 1; y++) {
		all_positions.push_back(TilePosition(x1, y));
		all_positions.push_back(TilePosition(x2, y));
	}
	
	std::vector<TilePosition> result;
	for (TilePosition tile_position : all_positions) {
		if (rectangle_buildable_for_building_with_clearance(tile_position, UnitTypes::Protoss_Gateway, blocked)) {
			result.push_back(tile_position);
		}
	}
	
	return result;
}

void BuildingPlacementManager::insert_unbuildable_nonarea_locations(TileGrid<bool>& blocked,const std::set<const BWEM::Area*> areas)
{
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			TilePosition tile_position(x, y);
			if (!Broodwar->isBuildable(tile_position) || !tile_inside_areas(tile_position, areas)) blocked.set(tile_position, true);
		}
	}
}

void BuildingPlacementManager::insert_scout_path_on_two_player_maps(TileGrid<bool>& blocked)
{
	std::vector<const BWEM::Base*> start_bases;
	for (auto& base : base_state.bases()) {
		if (base->Starting()) {
			start_bases.push_back(base);
		}
	}
	if (start_bases.size() != 2) return;
	
	const auto grid = [](unsigned int x,unsigned int y){
		return walkability_grid.is_terrain_walkable(FastTilePosition(x, y));
	};
	JPS::PathVector path;
	bool found = JPS::findPath(path, grid, start_bases[0]->Location().x, start_bases[0]->Location().y, start_bases[1]->Location().x, start_bases[1]->Location().y, 1);
	if (found) {
		for (auto& element : path) {
			FastTilePosition tile_position(element.x, element.y);
			for (int dy = -10; dy <= 10; dy++) {
				for (int dx = -10; dx <= 10; dx++) {
					blocked[tile_position + FastTilePosition(dx, dy)] = true;
				}
			}
		}
	}
}

bool BuildingPlacementManager::rectangle_buildable_for_building_with_clearance(TilePosition tile_position,UnitType unit_type,const TileGrid<bool>& blocked,int clearance)
{
	int x = tile_position.x;
	int y = tile_position.y;
	int w = unit_type.tileWidth();
	int h = unit_type.tileHeight();
	
	for (int dy = 0; dy < h; dy++) {
		for (int dx = 0; dx < w; dx++) {
			TilePosition tile_position(x + dx, y + dy);
			if (blocked.get(tile_position)) return false;
		}
	}
	
	for (int nx = x - clearance; nx < x + w + clearance; nx++) {
        for (int i = 0; i < clearance; i++) {
            TilePosition top_tile_position(nx, y - clearance + i);
            if (!walkability_grid.is_walkable(top_tile_position)) return false;
            TilePosition bottom_tile_position(nx, y + h + i);
            if (!walkability_grid.is_walkable(bottom_tile_position)) return false;
        }
	}
	
	for (int ny = y; ny < y + h; ny++) {
        for (int i = 0; i < clearance; i++) {
            TilePosition left_tile_position(x - clearance + i, ny);
            if (!walkability_grid.is_walkable(left_tile_position)) return false;
            TilePosition right_tile_position(x + w + i, ny);
            if (!walkability_grid.is_walkable(right_tile_position)) return false;
        }
	}
	
	return true;
}

bool BuildingPlacementManager::tile_inside_areas(TilePosition tile_position,const std::set<const BWEM::Area*>& areas)
{
	const BWEM::Area* area = bwem_map.GetArea(tile_position);
	if (area != nullptr) {
		return areas.count(area) > 0;
	} else {
		WalkPosition walk_position(tile_position);
		bool at_least_one_match = false;
		for (int dy = 0; dy < 4; dy++) {
			for (int dx = 0; dx < 4; dx++) {
				area = bwem_map.GetArea(walk_position + WalkPosition(dx, dy));
				if (area != nullptr) {
					bool match = (areas.count(area) > 0);
					if (!match) return false;
					at_least_one_match = true;
				}
			}
		}
		return at_least_one_match;
	}
}

int BuildingPlacementManager::maximum_distance_from_possible_opponent_starting_bases(Position position)
{
	int maximum = -1;
	for (auto& base : tactics_manager.possible_enemy_start_bases()) {
		Position base_position = center_position_for(UnitTypes::Protoss_Nexus, base->Location());
		int distance = ground_distance(position, base_position);
		maximum = std::max(maximum, distance);
	}
	return maximum;
}

void BuildingPlacementManager::onUnitDestroy(Unit unit)
{
	if (proxy_pylon_position_.isValid() &&
		unit->getType() == UnitTypes::Protoss_Pylon &&
		unit->getTilePosition() == proxy_pylon_position_) {
		proxy_pylon_position_ = TilePositions::None;
	}
	if (proxy_barracks_position_.isValid() &&
		unit->getType() == UnitTypes::Terran_Barracks &&
		unit->getTilePosition() == proxy_barracks_position_) {
		proxy_barracks_position_ = TilePositions::None;
	}
	if (proxy_second_barracks_position_.isValid() &&
		unit->getType() == UnitTypes::Terran_Barracks &&
		unit->getTilePosition() == proxy_second_barracks_position_) {
		proxy_second_barracks_position_ = TilePositions::None;
	}
}

void BuildingPlacementManager::apply_specific_positions(const WallPlacement::FFEPosition& ffe)
{
	specific_positions_[UnitTypes::Protoss_Gateway].push_back(ffe.gateway_position);
	specific_positions_[UnitTypes::Protoss_Forge].push_back(ffe.forge_position);
	specific_positions_[UnitTypes::Protoss_Pylon].push_back(ffe.pylon_position);
	const BWEM::Base* wall_base = base_state.is_backdoor_natural() ? base_state.start_base() : base_state.natural_base();
	ffe_saved_natural_base_defense_cannon_positions_ = photon_cannon_positions_[wall_base];
	ffe_saved_natural_base_defense_pylon_position_ = pylon_positions_[wall_base];
	photon_cannon_positions_[wall_base].clear();
	for (TilePosition cannon_position : ffe.cannon_positions) {
		photon_cannon_positions_[wall_base].push_back(cannon_position);
	}
	pylon_positions_[wall_base] = ffe.pylon_position;
	ffe_position_ = ffe;
}

void BuildingPlacementManager::restore_saved_ffe_natural_base_defense_positions()
{
	if (!ffe_saved_natural_base_defense_cannon_positions_.empty() && ffe_saved_natural_base_defense_pylon_position_.isValid()) {
		const BWEM::Base* wall_base = base_state.is_backdoor_natural() ? base_state.start_base() : base_state.natural_base();
		photon_cannon_positions_[wall_base] = ffe_saved_natural_base_defense_cannon_positions_;
		pylon_positions_[wall_base] = ffe_saved_natural_base_defense_pylon_position_;
		ffe_saved_natural_base_defense_cannon_positions_.clear();
		ffe_saved_natural_base_defense_pylon_position_ = TilePositions::None;
	}
}

void BuildingPlacementManager::apply_specific_positions(const WallPlacement::TerranWallPosition& terran_wall_position)
{
	specific_positions_[UnitTypes::Terran_Barracks].push_back(terran_wall_position.barracks_position);
	if (terran_wall_position.first_supply_depot_position.isValid()) {
		specific_positions_[UnitTypes::Terran_Supply_Depot].push_back(terran_wall_position.first_supply_depot_position);
	}
	if (terran_wall_position.second_supply_depot_position.isValid()) {
		specific_positions_[UnitTypes::Terran_Supply_Depot].push_back(terran_wall_position.second_supply_depot_position);
	}
	if (terran_wall_position.factory_position.isValid()) {
		specific_positions_[UnitTypes::Terran_Factory].push_back(terran_wall_position.factory_position);
	}
	terran_wall_position_ = terran_wall_position;
}

void BuildingPlacementManager::apply_specific_positions(const WallPlacement::TerranNaturalWallPosition& terran_wall_position)
{
	specific_positions_[UnitTypes::Terran_Barracks].push_back(terran_wall_position.barracks_position);
	specific_positions_[UnitTypes::Terran_Supply_Depot].push_back(terran_wall_position.first_supply_depot_position);
	if (terran_wall_position.second_supply_depot_position.isValid()) {
		specific_positions_[UnitTypes::Terran_Supply_Depot].push_back(terran_wall_position.second_supply_depot_position);
	}
	if (terran_wall_position.bunker_position.isValid()) {
		specific_positions_[UnitTypes::Terran_Bunker].push_back(terran_wall_position.bunker_position);
	}
	terran_natural_wall_position_ = terran_wall_position;
}

bool BuildingPlacementManager::has_active_ffe_wall() const
{
	return (ffe_position_ &&
			(!ffe_position_->pylon_part_of_wall || unit_grid.is_friendly_building_in_tile(UnitTypes::Protoss_Pylon, ffe_position_->pylon_position)) &&
			unit_grid.is_friendly_building_in_tile(UnitTypes::Protoss_Gateway, ffe_position_->gateway_position) &&
			unit_grid.is_friendly_building_in_tile(UnitTypes::Protoss_Forge, ffe_position_->forge_position) &&
			contains(base_state.border().inside_areas(), ffe_position_->base->GetArea()));
}

bool BuildingPlacementManager::has_active_wall(bool allow_gap) const
{
	if (terran_wall_position_ &&
		unit_grid.is_friendly_building_in_tile(UnitTypes::Terran_Barracks, terran_wall_position_->barracks_position) &&
		(!terran_wall_position_->first_supply_depot_position.isValid() || unit_grid.is_friendly_building_in_tile(UnitTypes::Terran_Supply_Depot, terran_wall_position_->first_supply_depot_position)) &&
		(!terran_wall_position_->second_supply_depot_position.isValid() || unit_grid.is_friendly_building_in_tile(UnitTypes::Terran_Supply_Depot, terran_wall_position_->second_supply_depot_position)) &&
		(!terran_wall_position_->factory_position.isValid() || unit_grid.is_friendly_building_in_tile(UnitTypes::Terran_Factory, terran_wall_position_->factory_position))) {
		return true;
	}
	
	if (terran_natural_wall_position_ &&
		(allow_gap || terran_natural_wall_position_->gap_unit_count == 0) &&
		unit_grid.is_friendly_building_in_tile(UnitTypes::Terran_Barracks, terran_natural_wall_position_->barracks_position) &&
		unit_grid.is_friendly_building_in_tile(UnitTypes::Terran_Supply_Depot, terran_natural_wall_position_->first_supply_depot_position) &&
		(!terran_natural_wall_position_->second_supply_depot_position.isValid() || unit_grid.is_friendly_building_in_tile(UnitTypes::Terran_Supply_Depot, terran_natural_wall_position_->second_supply_depot_position))) {
		return true;
	}
	
	return false;
}

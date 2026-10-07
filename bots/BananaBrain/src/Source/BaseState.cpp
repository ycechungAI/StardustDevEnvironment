#include "BananaBrain.h"

Border::Border(const std::set<const BWEM::Area*>& areas)
{
	for (auto area : areas) {
		for (auto neighbour : area->AccessibleNeighbours()) {
			if (areas.count(neighbour) == 0) {
				inside_areas_.insert(area);
				outside_areas_.insert(neighbour);
			}
		}
	}
	
	for (auto border_area : inside_areas_) {
		for (auto neighbour : border_area->AccessibleNeighbours()) {
			if (areas.count(neighbour) == 0) {
				for (auto& cp : border_area->ChokePoints(neighbour)) if (!cp.Blocked()) chokepoints_.insert(&cp);
			}
		}
	}
}

std::vector<const BWEM::ChokePoint*> Border::chokepoints_with_area(const BWEM::Area* area)
{
	std::vector<const BWEM::ChokePoint*> result;
	for (auto& cp : chokepoints_) {
		if (cp->GetAreas().first == area || cp->GetAreas().second == area) {
			result.push_back(cp);
		}
	}
	return result;
}

const BWEM::ChokePoint* Border::largest_chokepoint_with_area(const BWEM::Area* area)
{
	std::vector<const BWEM::ChokePoint*> chokepoints = chokepoints_with_area(area);
	key_value_vector<const BWEM::ChokePoint*,int> chokepoint_width_map;
	for (auto& cp : chokepoints) chokepoint_width_map.emplace_back(cp, chokepoint_width(cp));
	return key_with_largest_value(chokepoint_width_map);
}

void BaseState::init_bases()
{
	for (auto& area : bwem_map.Areas()) {
		for (auto& base : area.Bases()) {
			bases_.push_back(&base);
		}
	}
	
	for (auto& base : bases_) {
		base_map_[base->Location()] = base;
		if (base->Starting()) {
			const BWEM::Base* natural_base = determine_natural(base);
			start_to_natural_map_[base] = natural_base;
			start_extension_map_[base] = determine_start_extension(base, natural_base);
		}
	}
	
	update_base_information();
	start_base_ = get_or_default(base_map_, Broodwar->self()->getStartLocation());
	natural_base_ = start_to_natural_map_[start_base_];
	
	backdoor_natural_ = natural_base_ != nullptr && contains(enclosed_areas({start_base_->GetArea()}), natural_base_->GetArea());
	
	island_map_ = std::any_of(bases_.begin(), bases_.end(), [this](auto& base){
		return base->Starting() && !base->GetArea()->AccessibleFrom(start_base_->GetArea());
	});
}

void BaseState::update_base_information()
{
	update_controlled_bases();
	controlled_areas_ = controlled_areas_from_bases(controlled_bases_, determine_pylon_and_bunker_areas(true));
	controlled_and_planned_areas_ = controlled_areas_from_bases(controlled_and_planned_bases_, determine_pylon_and_bunker_areas(false));
	update_opponent_bases();
	update_border();
	update_next_available_bases();
	update_unexplored_start_bases();
	update_base_last_seen();
}

void BaseState::update_controlled_bases()
{
	controlled_bases_.clear();
	controlled_and_planned_bases_.clear();
	
	for (auto& unit : Broodwar->self()->getUnits()) {
		if (unit->getType().isResourceDepot()) {
			const BWEM::Base* base = nullptr;
			if (!unit->isFlying() && contains(base_map_, unit->getTilePosition())) {
				base = base_map_[unit->getTilePosition()];
			} else if (unit->isFlying()) {
				Position position = unit->getOrderTargetPosition();
				TilePosition tile_position = TilePosition(Position(position.x - unit->getType().tileWidth() * 16,
																   position.y - unit->getType().tileHeight() * 16));
				if (contains(base_map_, tile_position)) {
					base = base_map_[tile_position];
				}
			}
			if (base != nullptr) {
				if (unit->isCompleted() ||
					unit->getType() == UnitTypes::Zerg_Lair ||
					unit->getType() == UnitTypes::Zerg_Hive) {
					controlled_bases_.insert(base);
				}
				controlled_and_planned_bases_.insert(base);
			}
		}
	}
	
	for (auto& entry : worker_manager.worker_map()) {
		const Worker& worker = entry.second;
		if (worker.order()->building_type().isResourceDepot() && base_map_.count(worker.order()->building_position()) > 0) {
			const BWEM::Base* base = base_map_[worker.order()->building_position()];
			controlled_and_planned_bases_.insert(base);
		}
	}
}

std::set<const BWEM::Area*> BaseState::determine_pylon_and_bunker_areas(bool completed)
{
	std::set<const BWEM::Area*> result;
	for (auto& unit : Broodwar->self()->getUnits()) {
		if (unit->getType() == UnitTypes::Protoss_Pylon &&
			(!completed || unit->isCompleted()) &&
			building_placement_manager.proxy_pylon_position() != unit->getTilePosition()) {
			const BWEM::Area* area = is_ffe_pylon(unit->getTilePosition()) ? natural_base_->GetArea() : area_at(unit->getPosition());
			if (area != nullptr) result.insert(area);
		} else if (unit->getType() == UnitTypes::Terran_Bunker &&
			(!completed || unit->isCompleted())) {
			const BWEM::Area* area = area_at(unit->getPosition());
			if (area != nullptr) result.insert(area);
		}
	}
	if (!completed) {
		for (auto& entry : worker_manager.worker_map()) {
			const Worker& worker = entry.second;
			if (worker.order()->building_type() == UnitTypes::Protoss_Pylon &&
				building_placement_manager.proxy_pylon_position() != worker.order()->building_position()) {
				const BWEM::Area* area = area_at(center_position_for(UnitTypes::Protoss_Pylon, worker.order()->building_position()));
				if (area != nullptr) result.insert(area);
			} else if (worker.order()->building_type() == UnitTypes::Terran_Bunker) {
				const BWEM::Area* area = area_at(center_position_for(UnitTypes::Terran_Bunker, worker.order()->building_position()));
				if (area != nullptr) result.insert(area);
			}
		}
	}
	return result;
}

bool BaseState::is_ffe_pylon(FastTilePosition tile_position)
{
	auto& ffe_position = building_placement_manager.ffe_position();
	return ffe_position && ffe_position->pylon_position == tile_position;
}

void BaseState::update_opponent_bases()
{
	opponent_bases_.clear();
	for (auto& enemy_building : information_manager.enemy_units()) {
		if (enemy_building->type.isResourceDepot()) {
			for (auto& base : bases_) {
				int dx = base->Location().x - enemy_building->tile_position().x;
				int dy = base->Location().y - enemy_building->tile_position().y;
				if (abs(dx) <= 3 && abs(dy) <= 3) opponent_bases_.insert(base);
			}
		}
	}
}

void BaseState::update_border()
{
	border_ = Border(controlled_and_planned_areas_);
}

void BaseState::update_next_available_bases()
{
	std::vector<const BWEM::Base*> available_bases;
	for (auto base : bases_) {
		if (controlled_and_planned_bases_.count(base) == 0 &&
			opponent_bases_.count(base) == 0 &&
			!base->Minerals().empty() &&
			(!skip_mineral_only_ || !base->Geysers().empty())) {
			available_bases.push_back(base);
		}
	}
	UnitType center_type = Broodwar->self()->getRace().getResourceDepot();
	
	std::map<const BWEM::Base*,int> available_base_to_distance_map;
	for (auto& available_base : available_bases) {
		int safety_distance = 0;
		if (controlled_and_planned_areas_.count(available_base->GetArea()) == 0) {
			safety_distance = ground_distance(center_position_for(center_type, available_base->Location()),
											  center_position_for(center_type, Broodwar->self()->getStartLocation()));
			if (safety_distance < 0) continue;
		}
		
		std::vector<const BWEM::Base*> enemy_bases;
		if (opponent_bases_.empty()) {
			for (auto& base : bases_) {
				if (controlled_and_planned_bases_.count(base) == 0 && base->Starting()) enemy_bases.push_back(base);
			}
		} else {
			for (auto& base : opponent_bases_) enemy_bases.push_back(base);
		}
		
		int enemy_distance = INT_MAX;
		for (auto& enemy_base : enemy_bases) {
			int distance = ground_distance(center_position_for(center_type, available_base->Location()),
										   center_position_for(center_type, enemy_base->Location()));
			if (distance >= 0 && distance < enemy_distance) enemy_distance = distance;
		}
		
		available_base_to_distance_map[available_base] = safety_distance - enemy_distance;
	}
	
	next_available_bases_ = keys_sorted(available_base_to_distance_map);
	
	if (controlled_and_planned_bases_.size() == 1 &&
		*(controlled_and_planned_bases_.begin()) == start_base_) {
		auto it = std::find(next_available_bases_.begin(), next_available_bases_.end(), natural_base_);
		if (it != next_available_bases_.end()) {
			std::rotate(next_available_bases_.begin(), it, it + 1);
		}
	}
}

const BWEM::Base* BaseState::determine_natural(const BWEM::Base* start_base) const
{
	UnitType center_type = Broodwar->self()->getRace().getResourceDepot();
	std::map<const BWEM::Base*,int> distance_map;
	for (auto& base : bases_) {
		if (!base->Starting()) {
			int distance = ground_distance(center_position_for(center_type, base->Location()),
										   center_position_for(center_type, start_base->Location()));
			if (distance >= 0) distance_map[base] = distance;
		}
	}
	
	std::vector<const BWEM::Base*> bases = keys_sorted(distance_map);
	const BWEM::Base* result = bases.empty() ? nullptr : bases[0];
	if (bases.size() >= 2 &&
		!is_base_with_both_minerals_and_gas(bases[0]) &&
		is_base_with_both_minerals_and_gas(bases[1]) &&
		is_base_enclosed(start_base, bases[1], bases[0])) {
		result = bases[1];
	}
	return result;
}

const BWEM::Area* BaseState::determine_start_extension(const BWEM::Base* start_base,const BWEM::Base* natural_base) const
{
	const BWEM::Area* start_area = start_base->GetArea();
	const BWEM::Area* natural_area = natural_base->GetArea();
	std::set<const BWEM::Area*> start_and_natural_areas;
	start_and_natural_areas.insert(start_area);
	start_and_natural_areas.insert(natural_area);
	
	std::vector<const BWEM::Area*> result;
	for (auto area : start_area->AccessibleNeighbours()) {
		std::set<const BWEM::Area*> areas(area->AccessibleNeighbours().begin(), area->AccessibleNeighbours().end());
		if (areas == start_and_natural_areas) {
			result.push_back(area);
		}
	}
	
	return result.size() == 1 ? result[0] : nullptr;
}

bool BaseState::is_base_enclosed(const BWEM::Base* start_base,const BWEM::Base* natural_base,const BWEM::Base* other_base) const
{
	std::set<const BWEM::Area*> areas;
	areas.insert(start_base->GetArea());
	areas.insert(other_base->GetArea());
	std::set<const BWEM::Area*> enclosed = enclosed_areas(areas);
	return enclosed.find(other_base->GetArea()) != enclosed.end();
}

bool BaseState::is_base_with_both_minerals_and_gas(const BWEM::Base* base)
{
	return !base->Minerals().empty() && !base->Geysers().empty();
}

void BaseState::update_unexplored_start_bases()
{
	unexplored_start_bases_.clear();
	for (auto base : bases_) {
		if (base->Starting() && !building_explored(Broodwar->self()->getRace().getResourceDepot(), base->Location())) {
			unexplored_start_bases_.push_back(base);
		}
	}
}

void BaseState::update_base_last_seen()
{
	for (auto base : bases_) {
		if (building_visible(Broodwar->self()->getRace().getResourceDepot(), base->Location())) {
			base_last_seen_[base] = Broodwar->getFrameCount();
		}
	}
}

const BWEM::Base* BaseState::main_base() const
{
	const BWEM::Base* base = nullptr;
	if (controlled_bases_.size() == 1) {
		base = *controlled_bases_.begin();
	} else if (controlled_bases_.count(start_base_) > 0) {
		base = start_base_;
	}
	return base;
}

int BaseState::controlled_geyser_count() const
{
	int result = 0;
	for (const BWEM::Base* base : controlled_bases_) {
		result += base->Geysers().size();
	}
	return result;
}

int BaseState::mining_base_count() const
{
	int result = 0;
	for (const BWEM::Base* base : controlled_bases_) {
		if (!base->Minerals().empty()) result++;
	}
	return result;
}

int BaseState::mineable_mineral_count() const
{
	int result = 0;
	for (const BWEM::Base* base : controlled_and_planned_bases_) {
		for (auto& mineral : base->Minerals()) {
			result += mineral->Unit()->getResources();
		}
	}
	return result;
}

std::set<const BWEM::Base*> BaseState::undiscovered_starting_bases(bool overlord) const
{
	std::set<const BWEM::Base*> result;
	for (auto base : base_state.unexplored_start_bases()) result.insert(base);
	for (auto& entry : worker_manager.worker_map()) {
		const BWEM::Base* base = entry.second.order()->scout_base();
		if (base != nullptr) result.erase(base);
	}
	std::set<const BWEM::Base*> result1(result);
	for (auto& entry : micro_manager.overlord_state()) {
		if (entry.second.command == OverlordCommand::InitialScout) result.erase(entry.second.base);
	}
	return (!overlord && result.empty()) ? result1 : result;
}

std::set<const BWEM::Area*> BaseState::controlled_areas_from_bases(const std::set<const BWEM::Base*>& controlled_bases,const std::set<const BWEM::Area*> pylon_areas)
{
	std::set<const BWEM::Area*> controlled_base_areas;
	for (auto& base : controlled_bases) {
		const BWEM::Area* area = base->GetArea();
		if (base->Starting() || !is_large_area(area)) {
			controlled_base_areas.insert(area);
			const BWEM::Area* extension_area = start_extension_map_[base];
			if (extension_area != nullptr) controlled_base_areas.insert(extension_area);
		}
	}
	for (auto& area : pylon_areas) {
		if (!is_large_area(area)) controlled_base_areas.insert(area);
	}
	
	return enclosed_areas(controlled_base_areas);
}

std::set<const BWEM::Area*> BaseState::enclosed_areas(const std::set<const BWEM::Area*>& areas) const
{
	std::set<const BWEM::Area*> uncontrolled_base_areas;
	for (auto& base : bases_) {
		if (areas.count(base->GetArea()) == 0 &&
			opponent_bases_.empty() ? base->Starting() : opponent_bases_.count(base) > 0) {
			uncontrolled_base_areas.insert(base->GetArea());
		}
	}
	
	std::set<const BWEM::Area*> result;
	result.insert(areas.cbegin(), areas.cend());
	for (auto& area : bwem_map.Areas()) {
		std::set<const BWEM::Area*> reachable = reachable_areas(&area);
		std::set<const BWEM::Area*> reachable_blocked = reachable_areas(&area, areas);
		if (common_elements(reachable, areas) && !common_elements(reachable_blocked, uncontrolled_base_areas)) result.insert(&area);
	}
	
	return result;
}

std::set<const BWEM::Area*> BaseState::reachable_areas(const BWEM::Area* area,const std::set<const BWEM::Area*>& blocked_areas)
{
	std::set<const BWEM::Area*> result;
	std::queue<const BWEM::Area*> queue;
	std::set<const BWEM::Area*> done(blocked_areas);
	queue.push(area);
	done.insert(area);
	
	while (!queue.empty()) {
		const BWEM::Area* current = queue.front();
		queue.pop();
		result.insert(current);
		
		const std::vector<const BWEM::Area*>& neighbours = current->AccessibleNeighbours();
		for (auto neighbour : neighbours) {
			if (done.count(neighbour) == 0) {
				queue.push(neighbour);
				done.insert(neighbour);
			}
		}
	}
	
	return result;
}

std::set<const BWEM::Area*> BaseState::connected_areas(const BWEM::Area* area,const std::set<const BWEM::Area*>& allowed_areas)
{
	std::set<const BWEM::Area*> result;
	
	if (allowed_areas.count(area) > 0) {
		std::queue<const BWEM::Area*> queue;
		std::set<const BWEM::Area*> done;
		queue.push(area);
		done.insert(area);
		
		while (!queue.empty()) {
			const BWEM::Area* current = queue.front();
			queue.pop();
			result.insert(current);
			
			const std::vector<const BWEM::Area*>& neighbours = current->AccessibleNeighbours();
			for (auto neighbour : neighbours) {
				if (done.count(neighbour) == 0 && allowed_areas.count(neighbour) > 0) {
					queue.push(neighbour);
					done.insert(neighbour);
				}
			}
		}
	}
	
	return result;
}

bool BaseState::is_large_area(const BWEM::Area* area)
{
	return area->MaxAltitude() >= kLargeAreaAltitude;
}

void BaseState::draw()
{
	draw_bases();
	draw_areas();
}

void BaseState::draw_bases()
{
	UnitType center_type = Broodwar->self()->getRace().getResourceDepot();
	
	int base_number = 1;
	for (auto& base : bases_) {
		draw_unit_rectangle(base->Location(), center_type, base->Starting() ? Colors::Green : Colors::Blue);
		Broodwar->drawTextMap(Position(base->Location()), "Base %d", base_number);
		
		for (auto& mineral : base->Minerals()) {
			draw_unit_rectangle(mineral->TopLeft(), UnitTypes::Resource_Mineral_Field, Colors::Blue);
			Broodwar->drawTextMap(Position(mineral->TopLeft()), "%d", base_number);
		}
		
		for (auto& geyser : base->Geysers()) {
			draw_unit_rectangle(geyser->TopLeft(), UnitTypes::Resource_Vespene_Geyser, Colors::Orange);
			Broodwar->drawTextMap(Position(geyser->TopLeft()), "%d", base_number);
		}
		
		if (controlled_bases_.count(base) > 0) {
			Position position(base->Location());
			Broodwar->drawTextMap(position.x, position.y + 10, "Controlled");
		} else if (controlled_and_planned_bases_.count(base) > 0) {
			Position position(base->Location());
			Broodwar->drawTextMap(position.x, position.y + 10, "Planned");
		}
		
		if (opponent_bases_.count(base) > 0) {
			Position position(base->Location());
			Broodwar->drawTextMap(position.x, position.y + 10, "Opponent");
		}
		
		base_number++;
	}
	
	int next_base_index = 1;
	for (auto& base : next_available_bases_) {
		Position position(base->Location());
		Broodwar->drawTextMap(position.x, position.y + 10, "Next %d", next_base_index++);
	}
}

void BaseState::draw_areas()
{
	for (auto& area : bwem_map.Areas()) {
		Position position = center_position(area.Top());
		if (controlled_areas_.count(&area) > 0) {
			draw_filled_diamond_map(position, 20, Colors::Green);
		} else if (controlled_and_planned_areas_.count(&area) > 0) {
			draw_diamond_map(position, 20, Colors::Green);
		}
	}
	
	for (auto& area : bwem_map.Areas()) {
		for (auto& cp : area.ChokePoints()) {
			if (cp->GetAreas().first == &area) {
				if (!cp->Blocked()) {
					Gap gap(cp);
					gap.draw_line(Colors::Red);
				} else {
					draw_cross_map(chokepoint_center(cp), 3, Colors::Red);
				}
			}
		}
	}
}

void BaseState::draw_unit_rectangle(TilePosition tile_position,UnitType unit_type,Color color)
{
	Position top_left_position(tile_position);
	Position bottom_right_position(top_left_position.x + unit_type.tileWidth() * 32,
								   top_left_position.y + unit_type.tileHeight() * 32);
	Broodwar->drawBoxMap(top_left_position, bottom_right_position, color);
}

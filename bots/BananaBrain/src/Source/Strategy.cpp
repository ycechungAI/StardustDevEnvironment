#include "BananaBrain.h"

int Strategy::opening_supply_count()
{
	int half_supply = (Broodwar->self()->supplyUsed() + 1) / 2;
	if (worker_manager.lost_worker_count() > 0) half_supply++;
	return half_supply;
}

int Strategy::opening_worker_count()
{
	int result = training_manager.unit_count(Broodwar->self()->getRace().getWorker());
	if (worker_manager.lost_worker_count() > 0) result++;
	return result;
}

bool Strategy::opening_lost_too_many_workers()
{
	int allowed_lost_worker_count = 0;
	if (worker_manager.sent_initial_scout()) {
		allowed_lost_worker_count++;
	}
	return (worker_manager.lost_worker_count() > allowed_lost_worker_count);
}

bool Strategy::is_defending_rush()
{
	int current_army_supply_without_air_to_air = tactics_manager.army_supply() - tactics_manager.air_to_air_supply();
	return current_army_supply_without_air_to_air < 2 * 20 && current_army_supply_without_air_to_air <= tactics_manager.max_enemy_army_supply();
}

bool Strategy::is_contained()
{
	int supply = tactics_manager.army_supply();
	int air_to_air_supply = tactics_manager.air_to_air_supply();
	if (air_to_air_supply > tactics_manager.enemy_air_supply()) {
		supply -= (air_to_air_supply - tactics_manager.enemy_air_supply());
	}
	return tactics_manager.enemy_offense_supply() > supply;
}

bool Strategy::is_enemy_offense_larger_than_defense()
{
	int supply = tactics_manager.defense_supply();
	int air_to_air_supply = tactics_manager.air_to_air_supply();
	if (air_to_air_supply > tactics_manager.enemy_offense_air_supply()) {
		supply -= (air_to_air_supply - tactics_manager.enemy_offense_air_supply());
	}
	return tactics_manager.enemy_offense_supply() > supply;
}

bool Strategy::dark_templars_without_mobile_detection()
{
	return (information_manager.enemy_count(UnitTypes::Protoss_Dark_Templar) >= 1 &&
			!tactics_manager.mobile_detection_exists());
}

bool Strategy::expect_lurkers()
{
	bool hydralisk_den_exists = information_manager.enemy_exists(UnitTypes::Zerg_Hydralisk_Den) || information_manager.enemy_seen(UnitTypes::Zerg_Hydralisk);
	bool lair_or_hive_exists = information_manager.enemy_exists(UnitTypes::Zerg_Lair) || information_manager.enemy_exists(UnitTypes::Zerg_Hive);
	return (hydralisk_den_exists && lair_or_hive_exists);
}

bool Strategy::expect_dark_templars()
{
	return (information_manager.enemy_exists(UnitTypes::Protoss_Templar_Archives) ||
			(information_manager.enemy_exists(UnitTypes::Protoss_Citadel_of_Adun) &&
			 information_manager.no_enemy_has_upgrade(UpgradeTypes::Singularity_Charge) &&
			 information_manager.no_enemy_has_upgrade(UpgradeTypes::Leg_Enhancements) &&
			 opponent_model.enemy_opening() != EnemyOpening::P_4GateGoon));
}

bool Strategy::is_gas_stolen()
{
	return building_manager.building_count_including_planned(Broodwar->self()->getRace().getRefinery()) == 0 && building_manager.can_not_place_refinery();
}

bool Strategy::is_scouting_worker_in_base()
{
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->position.isValid() &&
			enemy_unit->base_distance == 0 &&
			enemy_unit->type.isWorker()) {
			scouting_worker_in_base_frame_ = Broodwar->getFrameCount();
			break;
		}
	}
	return (scouting_worker_in_base_frame_ >= 0 &&
			scouting_worker_in_base_frame_ >= Broodwar->getFrameCount() - 50);
}

void Strategy::scout_for_cannon_rush_if_needed()
{
	if (Broodwar->getFrameCount() == 2624 &&
		opponent_model.enemy_opening() == EnemyOpening::Unknown) {
		std::string cannon_rush_strategy = opponent_model.enemy_opening_info(EnemyOpening::P_CannonRush);
		bool cannon_rush_occured = result_store.historical_exists([&cannon_rush_strategy](auto& result){
			return result.opponent_strategy == cannon_rush_strategy;
		}, 15);
		if (cannon_rush_occured) {
			worker_manager.send_proxy_scout();
		}
	}
}

void Strategy::update_maxed_out()
{
	int supply = Broodwar->self()->supplyUsed();
	if (supply >= 2 * 192) {
		maxed_out_ = true;
	} else if (supply < 2 * 160) {
		maxed_out_ = false;
	}
}

void Strategy::attack_check_condition()
{
	if (Broodwar->getFrameCount() >= attack_frame_) {
		int supply = tactics_manager.army_supply();
		int air_to_air_supply = tactics_manager.air_to_air_supply();
		if (air_to_air_supply > tactics_manager.enemy_air_supply()) {
			supply -= (air_to_air_supply - tactics_manager.enemy_air_supply());
		}
		
		int enemy_supply = tactics_manager.enemy_army_supply();
		if (supply >= std::max(attack_minimum_, 5 * enemy_supply / 4)) {
			attacking_ = true;
		} else if (supply <= 4 * enemy_supply / 5) {
			attacking_ = false;
		}
	} else {
		attacking_ = false;
	}
	
	if (initial_dark_templar_attack_ &&
		training_manager.unit_count_completed(UnitTypes::Protoss_Dark_Templar) == 0) {
		initial_dark_templar_attack_ = false;
	}
}

const BWEM::Base* Strategy::stage_base() const
{
	const BWEM::Base* base = base_state.main_base();
	if (base == nullptr) {
		key_value_vector<const BWEM::Base*,int> distance_from_center;
		for (auto& base : base_state.controlled_bases()) {
			distance_from_center.emplace_back(base, bwem_map.Center().getApproxDistance(base->Center()));
		}
		base = key_with_largest_value(distance_from_center);
	}
	return base;
}

void Strategy::update_stage_chokepoint()
{
	stage_chokepoint_ = nullptr;
	stage_component_areas_.clear();
	
	const BWEM::Base* base = stage_base();
	
	if (base != nullptr) {
		std::set<const BWEM::Area*> component_areas = base_state.connected_areas(base->GetArea(), base_state.controlled_and_planned_areas());
		Border border(component_areas);
		Position center = base->Center();
		key_value_vector<const BWEM::Area*,int> border_distances;
		for (auto& area : border.inside_areas()) {
			int distance = ground_distance(center, Position(area->Top()));
			if (distance >= 0) border_distances.emplace_back(area, distance);
		}
		const BWEM::Area* border_area = key_with_smallest_value(border_distances);
		if (border_area != nullptr) {
			std::vector<const BWEM::ChokePoint*> chokepoints;
			for (auto& area : border_area->AccessibleNeighbours()) {
				if (component_areas.count(area) == 0) {
					for (auto& cp : border_area->ChokePoints(area)) if (!cp.Blocked()) chokepoints.push_back(&cp);
				}
			}
			
			key_value_vector<const BWEM::ChokePoint*,int> chokepoint_width_map;
			for (auto& cp : chokepoints) chokepoint_width_map.emplace_back(cp, chokepoint_width(cp));
			
			stage_chokepoint_ = key_with_largest_value(chokepoint_width_map);
			stage_component_areas_ = component_areas;
		}
	}
}

void Strategy::update_stage_position_and_type()
{
	FastPosition proxy_stage_position;
	if (building_placement_manager.proxy_pylon_position().isValid()) {
		Position pylon_position = center_position_for(UnitTypes::Protoss_Pylon, building_placement_manager.proxy_pylon_position());
		proxy_stage_position = walkability_grid.walkable_tile_near(pylon_position, 256);
	}
	if (!proxy_stage_position.isValid() &&
		building_placement_manager.proxy_barracks_position().isValid()) {
		Position barracks_position = center_position_for(UnitTypes::Terran_Barracks, building_placement_manager.proxy_barracks_position());
		proxy_stage_position = walkability_grid.walkable_tile_near(barracks_position, 256);
	}
	if (!proxy_stage_position.isValid() &&
		building_placement_manager.proxy_second_barracks_position().isValid()) {
		Position barracks_position = center_position_for(UnitTypes::Terran_Barracks, building_placement_manager.proxy_second_barracks_position());
		proxy_stage_position = walkability_grid.walkable_tile_near(barracks_position, 256);
	}
	if (proxy_stage_position.isValid()) {
		stage_type_ = StageType::Proxy;
		stage_position_ = proxy_stage_position;
		return;
	}
	
	stage_type_ = StageType::Minerals;
	stage_position_ = Positions::None;
	
	const BWEM::Base* base = stage_base();
	
	if (stage_chokepoint_ != nullptr) {
		const BWEM::ChokePoint* chokepoint = stage_chokepoint_;
		Position choke_center_position = chokepoint_center(chokepoint);
		Position walkable_choke_center_position = walkability_grid.walkable_tile_near(choke_center_position, 128);
		if (walkable_choke_center_position.isValid()) choke_center_position = walkable_choke_center_position;
		
		if (choke_center_position.isValid()) {
			stage_type_ = determine_defense_decision(stage_component_areas_, chokepoint);
			if (stage_type_ == StageType::Minerals) {
				stage_position_ = calculate_mineral_center_base_stage_point(base);
			} else {
				FastPosition wall_stage_position = determine_terran_wall_stage_position(choke_center_position);
				if (wall_stage_position.isValid()) {
					stage_type_ = StageType::Wall;
					stage_position_ = wall_stage_position;
				} else {
					stage_position_ = choke_center_position;
				}
			}
		}
	}
	
	if (!stage_position_.isValid()) {
		if (base == nullptr) base = base_state.start_base();
		stage_position_ = calculate_mineral_center_base_stage_point(base);
	}
}

FastPosition Strategy::determine_terran_wall_stage_position(FastPosition choke_center_position)
{
	auto& terran_wall_position = building_placement_manager.terran_wall_position();
	if (terran_wall_position &&
		terran_wall_position->stage_position.isValid() &&
		building_placement_manager.has_active_wall() &&
		contains(base_state.border().inside_areas(), terran_wall_position->base->GetArea())) {
		return walkability_grid.walkable_tile_near(terran_wall_position->stage_position, 128);
	}
	auto& terran_natural_wall_position = building_placement_manager.terran_natural_wall_position();
	if (terran_natural_wall_position &&
		terran_natural_wall_position->stage_position.isValid() &&
		contains(base_state.border().inside_areas(), terran_natural_wall_position->base->GetArea())) {
		return walkability_grid.walkable_tile_near(terran_natural_wall_position->stage_position, 128);
	}
	if (building_manager.building_count_including_warping(UnitTypes::Terran_Bunker) >= 1) {
		for (auto& information_unit : information_manager.my_units()) {
			if (information_unit->type == UnitTypes::Terran_Bunker &&
				information_unit->base_distance == 0 &&
				calculate_distance(UnitTypes::Terran_Bunker, information_unit->position, choke_center_position) <= 320) {
				Gap gap(Rect(UnitTypes::Terran_Bunker, information_unit->position), Rect(choke_center_position, choke_center_position));
				FastPosition delta = gap.b() - gap.a();
				int dx = 64 * sign(delta.x);
				int dy = 52 * sign(delta.y);
				return walkability_grid.walkable_tile_near(information_unit->position - FastPosition(dx, dy), 128);
			}
		}
	}
	return Positions::None;
}

Strategy::StageType Strategy::determine_defense_decision(const std::set<const BWEM::Area*>& component_areas, const BWEM::ChokePoint* chokepoint)
{
	Position choke_center_position = chokepoint_center(chokepoint);
	Gap gap(chokepoint);
	auto choke_line = gap.line();
	const auto only_static_detection = [](){
		if (building_manager.building_count(UnitTypes::Protoss_Photon_Cannon) > 0 &&
			training_manager.unit_count_completed(UnitTypes::Protoss_Observer) == 0) {
			return true;
		}
		if (building_manager.building_count(UnitTypes::Terran_Missile_Turret) > 0 &&
			training_manager.unit_count_completed(UnitTypes::Terran_Science_Vessel) == 0) {
			bool comsat_with_energy = std::any_of(information_manager.my_units().begin(),
												  information_manager.my_units().end(),
												  [](const auto& information_unit) {
													  return (information_unit->type == UnitTypes::Terran_Comsat_Station &&
															  information_unit->unit->getEnergy() >= TechTypes::Scanner_Sweep.energyCost());
												  });
			if (!comsat_with_energy) return true;
		}
		return false;
	};
	
	const auto dark_templar_or_lurker_exists = [](){
		return std::any_of(information_manager.enemy_units().begin(), information_manager.enemy_units().end(),
						   [](const auto& enemy_unit) {
							   return (enemy_unit->type == UnitTypes::Protoss_Dark_Templar ||
									   enemy_unit->type == UnitTypes::Zerg_Lurker);
						   });
	};
	
	const auto cannon_or_turret_at_choke = [&](){
		if (Broodwar->self()->getRace() != Races::Protoss &&
			Broodwar->self()->getRace() != Races::Terran) {
			return false;
		}
		for (auto& information_unit : information_manager.my_units()) {
			if ((information_unit->type == UnitTypes::Protoss_Photon_Cannon ||
				 information_unit->type == UnitTypes::Terran_Missile_Turret) &&
				information_unit->is_completed()) {
				int distance = calculate_distance(information_unit->type, information_unit->position, choke_center_position);
				if (distance <= information_unit->detection_range()) {
					return true;
				}
			}
		}
		return false;
	};
	
	if (only_static_detection() &&
		dark_templar_or_lurker_exists() &&
		!cannon_or_turret_at_choke()) {
		return StageType::Minerals;
	}
	
	int worker_count = 0;
	bool at_minerals = false;
	bool block_melee = (opponent_model.enemy_race() == Races::Zerg && Broodwar->self()->getRace() != Races::Terran);
	bool dark_templar_outside = false;
	bool ffe_wall = building_placement_manager.has_active_ffe_wall();
	
	const auto consider_unit = [&](auto enemy_unit){
		if (!enemy_unit->position.isValid()) {
			return false;
		}
		if (enemy_unit->unit->exists() &&
			enemy_unit->unit->isVisible() &&
			enemy_unit->type != UnitTypes::Terran_Medic &&
			(can_attack(enemy_unit->unit, false) || is_spellcaster(enemy_unit->type)) &&
			!(ffe_wall && is_near_ffe_building_or_gap(enemy_unit))) {
			return true;
		}
		if (enemy_unit->type == UnitTypes::Zerg_Lurker &&
			enemy_unit->burrowed &&
			!(ffe_wall && is_near_ffe_building_or_gap(enemy_unit))) {
			return true;
		}
		if (enemy_unit->unit->exists() &&
			(enemy_unit->type == UnitTypes::Protoss_Shuttle ||
			 enemy_unit->type == UnitTypes::Terran_Dropship ||
			 (enemy_unit->type == UnitTypes::Zerg_Overlord &&
			  information_manager.upgrade_level(enemy_unit->player, UpgradeTypes::Ventral_Sacs)))) {
			return true;
		}
		return false;
	};
	
	for (auto enemy_unit : information_manager.enemy_units()) {
		if (consider_unit(enemy_unit)) {
			const BWEM::Area* area = area_at(enemy_unit->position);
			if (area != nullptr && component_areas.count(area) > 0) {
				if (enemy_unit->type.isWorker()) {
					worker_count++;
					if (worker_count >= 3) at_minerals = true;
				} else {
					at_minerals = true;
				}
			} else {
				if (enemy_unit->type != UnitTypes::Terran_SCV && (!block_melee || !is_melee(enemy_unit->type))) {
					int range = offense_max_range(enemy_unit->type, enemy_unit->player, false);
					if (range >= 0 &&
						(calculate_distance(enemy_unit->type, enemy_unit->position, choke_center_position) < 128 + range ||
						 point_to_line_segment_distance(choke_line.first, choke_line.second, enemy_unit->position) < 32 + range)) {
						at_minerals = true;
					}
				}
				if (enemy_unit->type == UnitTypes::Protoss_Dark_Templar) {
					dark_templar_outside = true;
				}
			}
		}
	}
	
	if (dark_templar_outside &&
		!tactics_manager.mobile_detection_exists() &&
		!cannon_or_turret_at_choke()) {
		return StageType::BlockChokePointDarkTemplar;
	}
	
	return (at_minerals ? StageType::Minerals : StageType::BlockChokePoint);
}

bool Strategy::is_near_ffe_building_or_gap(const InformationUnit* enemy_unit)
{
	auto& ffe_position = building_placement_manager.ffe_position();
	if (ffe_position) {
		if ((ffe_position->pylon_part_of_wall && calculate_distance(UnitTypes::Protoss_Pylon, center_position_for(UnitTypes::Protoss_Pylon, ffe_position->pylon_position), enemy_unit->type, enemy_unit->position) <= 48) ||
			calculate_distance(UnitTypes::Protoss_Gateway, center_position_for(UnitTypes::Protoss_Gateway, ffe_position->gateway_position), enemy_unit->type, enemy_unit->position) <= 48 ||
			calculate_distance(UnitTypes::Protoss_Forge, center_position_for(UnitTypes::Protoss_Forge, ffe_position->forge_position), enemy_unit->type, enemy_unit->position) <= 48) {
			return true;
		}
		for (auto& gap : ffe_position->gaps) {
			FastPosition center = (gap.a() + gap.b()) / 2;
			int distance = calculate_distance(enemy_unit->type, enemy_unit->position, center);
			if (distance <= gap.size() / 2 + 32) {
				return true;
			}
		}
	}
	return false;
}

Position Strategy::calculate_mineral_center_base_stage_point(const BWEM::Base* base)
{
	Position base_center = base->Center();
	Position mineral_center;
	int mineral_center_count = 0;
	for (auto& mineral : base->Minerals()) {
		mineral_center += edge_position(mineral->Type(), mineral->Pos(), base_center);
		mineral_center_count++;
	}
	
	Position result = Positions::None;
	if (mineral_center_count > 1) {
		mineral_center = mineral_center / mineral_center_count;
		Position base_edge_position = edge_position(Broodwar->self()->getRace().getResourceDepot(), base_center, mineral_center);
		Position candidate_position = (mineral_center + base_edge_position) / 2;
		result = walkability_grid.walkable_tile_near(candidate_position, 128);
	}
	
	if (!result.isValid()) result = walkability_grid.walkable_tile_near(base_center, 256);
	if (!result.isValid()) result = base_center;
	
	return result;
}

void Strategy::apply_stage_positions()
{
	update_stage_chokepoint();
	update_stage_position_and_type();
	
	for (auto& entry : micro_manager.combat_state()) {
		Position position = stage_position_;
		if (type_specific_stage_.count(entry.first->getType()) > 0) {
			position = type_specific_stage_.at(entry.first->getType());
		}
		entry.second.set_stage_position(position);
		entry.second.set_block_chokepoint(false);
	}
	
	if (stage_position_.isValid() &&
		stage_chokepoint_ != nullptr &&
		(stage_type_ == StageType::BlockChokePoint || stage_type_ == StageType::BlockChokePointDarkTemplar)) {
		if (!apply_stage_block_ffe()) {
			apply_stage_block_chokepoint();
		}
	}
}

void Strategy::apply_stage_block_chokepoint()
{
	Gap gap(stage_chokepoint_);
	if (!ground_enemy_near_stage(gap)) {
		return;
	}
	int max_distance = gap.size();
	if (stage_type_ == StageType::BlockChokePointDarkTemplar) {
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->type == UnitTypes::Protoss_Dark_Templar &&
				enemy_unit->base_distance > 0) {
				max_distance = std::min(max_distance, enemy_unit->base_distance - 1);
			}
		}
	}
	auto line = gap.line();
	std::vector<std::pair<Unit,int>> unit_distance_pairs;
	for (auto& entry : micro_manager.combat_state()) {
		Unit unit = entry.first;
		int distance = point_to_line_segment_distance(line.first, line.second, unit->getPosition());
		if (unit->exists() &&
			(is_melee_or_worker(unit->getType()) || stage_type_ == StageType::BlockChokePointDarkTemplar) &&
			!contains(type_specific_stage_, unit->getType()) &&
			distance <= max_distance) {
			if (information_manager.all_units().at(unit).base_distance > 0) distance = -distance;
			unit_distance_pairs.emplace_back(unit, distance);
		}
	}
	
	UnitType tightness_unit_type;
	switch (opponent_model.enemy_race()) {
		case Races::Zerg:
			tightness_unit_type = UnitTypes::Zerg_Zergling;
			break;
		case Races::Terran:
			tightness_unit_type = UnitTypes::Terran_Marine;
			break;
		case Races::Protoss:
			tightness_unit_type = UnitTypes::Protoss_Zealot;
			break;
		default:
			tightness_unit_type = UnitTypes::Zerg_Zergling;
			break;
	}
	
	std::sort(unit_distance_pairs.begin(),
			  unit_distance_pairs.end(),
			  [](const auto& a,const auto& b){
				  return std::make_tuple(!is_melee_or_worker(a.first->getType()), a.second, a.first->getID()) < std::make_tuple(!is_melee_or_worker(b.first->getType()), b.second, b.first->getID());
			  });
	std::vector<UnitType> types;
	types.reserve(unit_distance_pairs.size());
	for (auto& entry : unit_distance_pairs) types.push_back(entry.first->getType());
	std::vector<Position> positions = gap.block_positions(types, tightness_unit_type, false, false);
	
	if (!positions.empty()) {
		Position d1 = line.second - line.first;
		
		std::sort(unit_distance_pairs.begin(),
				  unit_distance_pairs.begin() + positions.size(),
				  [&d1](const auto& a,const auto& b){
					  const auto inner_product = [&d1](const auto& a)
					  {
						  return d1.x * a.x + d1.y * a.y;
					  };
					  
					  return std::make_tuple(inner_product(a.first->getPosition()), a.first->getID()) < std::make_tuple(inner_product(b.first->getPosition()), b.first->getID());
				  });
		
		Position d(-d1.y, d1.x);
		Position p1 = scale_line_segment(stage_position_, stage_position_ - d, 64);
		Position p2 = scale_line_segment(stage_position_, stage_position_ + d, 64);
		Position near_stage_position = Positions::None;
		if (contains(base_state.controlled_and_planned_areas(), area_at(p1))) {
			near_stage_position = p1;
		} else {
			near_stage_position = p2;
		}
		if (near_stage_position.isValid()) {
			Position walkable_near_stage = walkability_grid.walkable_tile_near(near_stage_position, 64);
			if (walkable_near_stage.isValid()) near_stage_position = walkable_near_stage;
			
			for (auto& entry : micro_manager.combat_state()) {
				if (type_specific_stage_.count(entry.first->getType()) == 0) {
					entry.second.set_stage_position(near_stage_position);
					entry.second.set_block_chokepoint(false);
				}
			}
		}
		
		for (size_t i = 0; i < positions.size(); i++) {
			CombatState& combat_state = micro_manager.combat_state().at(unit_distance_pairs[i].first);
			combat_state.set_stage_position(positions[i]);
			combat_state.set_block_chokepoint(true);
		}
	}
}

bool Strategy::apply_stage_block_ffe()
{
	bool result = false;
	// @
	/*if (building_placement_manager.ffe_position()) {
		for (auto& gap : building_placement_manager.ffe_position()->gaps) {
			gap.draw_line(Colors::White);
			Broodwar->drawTextMap((gap.a() + gap.b()) / 2, "%d", gap.size());
		}
	}*/
	// /@
	if (building_placement_manager.has_active_ffe_wall()) {
		auto& ffe_position = building_placement_manager.ffe_position();
		/*
		 Algorithm:
		 - Sort gaps descending by size.
		 - Assign units required for mimimal blocking to each gap from the units near the gap.
		 - If there is a gap that does not have the minimal number of units required for blocking, assign leftover to this gap and stop.
		 - Assign units that fit in a gap from the units near the gap.
		 - Assign leftover units to the first gap they will fit in. If none, assign them to the first gap.
		 */
		struct GapData {
			Gap gap;
			int size;
			FastPosition a;
			FastPosition b;
			FastPosition center;
			std::vector<std::pair<Unit,int>> unit_distance_pairs;
			std::vector<UnitType> types;
			std::vector<Position> positions;
			
			GapData(const Gap& gap) : gap(gap), size(gap.size())
			{
				auto line = gap.line();
				a = line.first;
				b = line.second;
				center = (a + b) / 2;
			}
			
			bool operator<(const GapData& other) const
			{
				return size > other.size;
			}
			
			bool is_position_outside(FastPosition position) const
			{
				FastPosition natural_center = building_placement_manager.ffe_position()->base->Center();
				auto& ffe_position = building_placement_manager.ffe_position();
				bool result;
				FastPosition d1 = b - a;
				FastPosition d(-d1.y, d1.x);
				FastPosition u = position - a;
				int u_sign = sign(u.x * d.x + u.y * d.y);
				FastPosition n = natural_center - a;
				if (ffe_position->multichoke_wall) {
					int cross = n.x * d.y - n.y * d.x;	// cross<0:d=clockwise, cross>0:d=counterclockwise
					if (ffe_position->multichoke_clockwise) cross = -cross;	// cross<0:d=inside, cross>0:d=outside
					result = (sign(cross) * u_sign > 0);
				} else {
					int n_sign = sign(n.x * d.x + n.y * d.y);
					result = (n_sign * u_sign < 0);
				}
				return result;
			}
			
			void sort_unit_distance_pairs()
			{
				std::sort(unit_distance_pairs.begin(),
						  unit_distance_pairs.end(),
						  [](const auto& a,const auto& b){
							  return std::make_tuple(!is_melee(a.first->getType()), a.second, a.first->getID()) < std::make_tuple(!is_melee(b.first->getType()), b.second, b.first->getID());
						  });
			}
			
			void assign_units(bool use_minimum_number_of_units)
			{
				types.clear();
				for (auto& entry : unit_distance_pairs) types.push_back(entry.first->getType());
				positions = gap.block_positions(types, UnitTypes::Zerg_Zergling, use_minimum_number_of_units);
			}
			
			bool check_additional_unit_fits(UnitType type)
			{
				bool result = false;
				if (unit_distance_pairs.size() == positions.size()) {
					std::vector<UnitType> types_copy(types.begin(), types.end());
					types_copy.push_back(type);
					std::vector<Position> positions_copy = gap.block_positions(types_copy, UnitTypes::Zerg_Zergling);
					if (positions_copy.size() == types_copy.size()) result = true;
				}
				return result;
			}
			
			void position_units(std::set<Unit>& unassigned_units,bool all_gaps_closed=false)
			{
				FastPosition d1 = b - a;
				
				std::sort(unit_distance_pairs.begin(),
						  unit_distance_pairs.begin() + positions.size(),
						  [&d1](const auto& a,const auto& b){
							  const auto inner_product = [&d1](const auto& a)
							  {
								  return d1.x * a.x + d1.y * a.y;
							  };
							  
							  return std::make_tuple(inner_product(a.first->getPosition()), a.first->getID()) < std::make_tuple(inner_product(b.first->getPosition()), b.first->getID());
						  });
				for (size_t i = 0; i < positions.size(); i++) {
					CombatState& combat_state = micro_manager.combat_state().at(unit_distance_pairs[i].first);
					combat_state.set_stage_position(positions[i]);
					combat_state.set_block_chokepoint(all_gaps_closed);
					unassigned_units.erase(unit_distance_pairs[i].first);
				}
			}
		};
		
		std::vector<GapData> gaps;
		for (auto& gap : ffe_position->gaps) {
			if (gap.fits_through(UnitTypes::Zerg_Zergling)) {
				gaps.emplace_back(gap);
			}
		}
		std::sort(gaps.begin(), gaps.end());
		
		std::set<Unit> unassigned_units;
		for (auto& entry : micro_manager.combat_state()) {
			Unit unit = entry.first;
			if (unit->exists() &&
				!contains(type_specific_stage_, unit->getType())) {
				unassigned_units.insert(unit);
				if (is_melee(unit->getType())) {
					for (auto& gap : gaps) {
						int distance = point_to_line_segment_distance(gap.a, gap.b, unit->getPosition());
						if (distance <= gap.size) {
							if (gap.is_position_outside(unit->getPosition())) distance = -distance;
							gap.unit_distance_pairs.emplace_back(unit, distance);
							// @
							//Broodwar->drawTextMap(unit->getPosition(), "%d", distance);
							// /@
							break;
						}
					}
				}
			}
		}
		
		GapData* first_unsatisfied_gap = nullptr;
		for (auto& gap : gaps) {
			gap.sort_unit_distance_pairs();
			gap.assign_units(true);
			if (gap.positions.empty() && first_unsatisfied_gap == nullptr) {
				first_unsatisfied_gap = &gap;
			}
		}
		if (first_unsatisfied_gap != nullptr) {
			for (auto& gap : gaps) {
				gap.position_units(unassigned_units);
			}
			for (auto& unit : unassigned_units) {
				CombatState& combat_state = micro_manager.combat_state().at(unit);
				combat_state.set_stage_position(first_unsatisfied_gap->center);
				combat_state.set_block_chokepoint(false);
			}
		} else {
			for (auto& gap : gaps) {
				gap.assign_units(false);
			}
			std::map<UnitType,FastPosition> first_possible_gap_map;
			for (auto& gap : gaps) {
				gap.position_units(unassigned_units, true);
			}
			for (auto& unit : unassigned_units) {
				UnitType type = unit->getType();
				auto& position = first_possible_gap_map[unit->getType()];
				if (!position.isValid()) {
					for (auto& gap : gaps) {
						if (gap.check_additional_unit_fits(unit->getType())) {
							position = gap.center;
						}
					}
					if (!position.isValid()) {
						if (ffe_position->multichoke_wall) {
							position = calculate_mineral_center_base_stage_point(ffe_position->base);
						} else {
							GapData& gap = gaps[0];
							FastPosition d1 = gap.b - gap.a;
							FastPosition d(-d1.y, d1.x);
							FastPosition p1 = scale_line_segment(gap.center, gap.center - d, 64);
							FastPosition p2 = scale_line_segment(gap.center, gap.center + d, 64);
							position = gap.is_position_outside(p2) ? p1 : p2;
							
							Position walkable_position = walkability_grid.walkable_tile_near(position, 64);
							if (walkable_position.isValid()) position = walkable_position;
						}
					}
				}
				CombatState& combat_state = micro_manager.combat_state().at(unit);
				combat_state.set_stage_position(position);
				combat_state.set_block_chokepoint(false);
			}
		}
		result = true;
		
		// @
		/*for (auto& gap : gaps) {
			Broodwar->drawTextMap(gap.center + Position(20, 0), "%c%d%s", Text::Red, int(gap.number_assigned), (&gap == first_unsatisfied_gap) ? "*" : "");
		}*/
		// /@
	}
	
	return result;
}

bool Strategy::ground_enemy_near_stage(const Gap& gap)
{
	auto line = gap.line();
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->is_current() &&
			!enemy_unit->flying &&
			!enemy_unit->type.isSpell() &&
			point_to_line_segment_distance(line.first, line.second, enemy_unit->position) <= 320) {
			return true;
		}
	}
	return false;
}

void Strategy::attack_enemy()
{
	bool detection_missing = opponent_model.cloaked_or_mine_present() && !tactics_manager.mobile_detection_exists();
	bool desperation_attack = desperation_attack_condition();
	
	Position attack_position = Positions::None;
	bool always_advance = maxed_out_ || desperation_attack;
	
	if ((attacking_ && !detection_missing) || always_advance) {
		if (micro_manager.runby_possible()) {
			attack_position = tactics_manager.enemy_start_position();
		}
		if (!attack_position.isValid()) {
			attack_position = tactics_manager.enemy_base_attack_position();
		}
	}
	
	for (auto& entry : micro_manager.combat_state()) {
		entry.second.set_target_position(attack_position);
		entry.second.set_always_advance(always_advance);
		entry.second.set_near_target_only(attack_near_target_only_);
	}
	
	if (initial_dark_templar_attack_) {
		Position dark_templar_attack_position = tactics_manager.enemy_start_position();
		if (!dark_templar_attack_position.isValid()) {
			dark_templar_attack_position = tactics_manager.enemy_base_attack_position();
		}
		for (auto& entry : micro_manager.combat_state()) {
			if (entry.first->getType() == UnitTypes::Protoss_Dark_Templar) {
				entry.second.set_target_position(dark_templar_attack_position);
			}
		}
	}
	
	if (attack_with_vultures_ &&
		!attack_position.isValid() &&
		!detection_missing) {
		Position vulture_attack_position = tactics_manager.enemy_base_attack_position();
		for (auto& entry : micro_manager.combat_state()) {
			if (entry.first->getType() == UnitTypes::Terran_Vulture) {
				entry.second.set_target_position(vulture_attack_position);
			}
		}
	}
	
	if (!attacking_) attack_near_target_only_ = false;
	if (attack_near_target_only_ && attack_position.isValid()) {
		if (Broodwar->isVisible(TilePosition(attack_position))) {
			bool unit_near_target = false;
			for (auto& enemy_unit : information_manager.enemy_units()) {
				if (enemy_unit->position.isValid() && attack_position.getDistance(enemy_unit->position) <= 320) {
					unit_near_target = true;
				}
			}
			if (!unit_near_target) attack_near_target_only_ = false;
		}
	}
}

bool Strategy::desperation_attack_condition()
{
	bool no_workers = worker_manager.worker_map().empty();
	bool supply_blocked = Broodwar->self()->supplyUsed() >= Broodwar->self()->supplyTotal();
	bool can_not_build_worker = base_state.controlled_and_planned_bases().empty() || !MineralGas(Broodwar->self()).can_pay(Broodwar->self()->getRace().getWorker());
	
	return no_workers && (supply_blocked || can_not_build_worker);
}

void Strategy::apply_result(bool win)
{
	result_store.apply_result(opening(), late_game_strategy(), opponent_model.enemy_opening_info(), win);
}

void Strategy::frame()
{
	base_state.set_skip_mineral_only(false);
	worker_manager.set_limit_refinery_workers(-1);
	worker_manager.set_force_refinery_workers(false);
	building_manager.set_automatic_supply(true);
	training_manager.set_automatic_supply(true);
	training_manager.set_worker_production(true);
	training_manager.set_worker_cut(false);
	micro_manager.set_prevent_high_templar_from_archon_warp_count(0);
	micro_manager.set_force_archon_warp(false);
	type_specific_stage_.clear();
	
	frame_inner();
	
	apply_stage_positions();
	update_maxed_out();
	attack_enemy();
}

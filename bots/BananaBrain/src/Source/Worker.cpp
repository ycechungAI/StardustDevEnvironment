#include "BananaBrain.h"

WorkerAllocation::WorkerAllocation(const std::vector<std::pair<Unit,const BWEM::Base*>>& minerals,
								   const std::vector<std::pair<Unit,const BWEM::Base*>>& refineries,
								   const std::map<Unit,Worker,CompareUnitByID>& worker_map)
{
	for (auto& entry : minerals) {
		Unit unit = entry.first;
		mineral_map_[unit] = 0;
		base_map_[unit] = entry.second;
	}
	for (auto& entry : refineries) {
		Unit unit = entry.first;
		refinery_map_[unit] = 0;
		base_map_[unit] = entry.second;
	}
	
	for (auto& entry : worker_map) {
		const Worker& worker = entry.second;
		Unit gather_target = worker.order()->gather_target();
		if (mineral_map_.count(gather_target) > 0) mineral_map_[gather_target]++;
		if (refinery_map_.count(gather_target) > 0) refinery_map_[gather_target]++;
	}
}

Unit WorkerAllocation::pick_mineral_for_worker(const Worker& worker)
{
	Unit worker_unit = worker.unit();
	Unit result = closest_unit(minerals_with_worker_count(0, worker_unit), worker_unit);
	if (result == nullptr) result = closest_unit(minerals_with_worker_count(1, worker_unit), worker_unit);
	if (result == nullptr) result = closest_unit(minerals_with_worker_count(2, worker_unit), worker_unit);
	if (result != nullptr) mineral_map_[result]++;
	return result;
}

std::vector<Unit> WorkerAllocation::minerals_with_worker_count(int worker_count,Unit worker_unit)
{
	std::vector<Unit> result;
	for (auto& entry : mineral_map_) {
		if (entry.second <= worker_count &&
			worker_manager.is_worker_mining_allowed_from_base(base_map_[entry.first], worker_unit)) result.push_back(entry.first);
	}
	return result;
}

Unit WorkerAllocation::pick_refinery_for_worker(const Worker& worker)
{
	Unit worker_unit = worker.unit();
	Unit result = closest_unit(unsaturated_refineries(worker_unit), worker_unit);
	if (result != nullptr) refinery_map_[result]++;
	return result;
}

std::vector<Unit> WorkerAllocation::unsaturated_refineries(Unit worker_unit)
{
	std::vector<Unit> result;
	for (auto& entry : refinery_map_) {
		if (entry.second < 3 &&
			worker_manager.is_worker_mining_allowed_from_base(base_map_[entry.first], worker_unit)) result.push_back(entry.first);
	}
	return result;
}

Unit WorkerAllocation::closest_unit(const std::vector<Unit>& units,Unit target_unit)
{
	key_value_vector<Unit,int> distances;
	for (auto unit : units) {
		if (has_area(FastWalkPosition(unit->getPosition()))) {
			int distance = ground_distance(target_unit->getPosition(), unit->getPosition());
			if (distance > 0) distances.emplace_back(unit, distance);
		} else {
			// Workaround that allows mining of minerals of natural bases on Great Barrier Reef
			distances.emplace_back(unit, target_unit->getDistance(unit));
		}
	}
	return key_with_smallest_value(distances);
}

int WorkerAllocation::max_workers() const
{
	int mineral_count = int(mineral_map_.size());
	int refinery_count = int(refinery_map_.size());
	return 3 * (mineral_count + refinery_count);
}

double WorkerAllocation::average_workers_per_mineral() const
{
	if (mineral_map_.empty()) return INFINITY;
	double sum = 0.0;
	for (auto& entry : mineral_map_) sum += entry.second;
	return sum / mineral_map_.size();
}

int WorkerAllocation::count_refinery_workers() const
{
	int count = 0;
	for (auto& entry : refinery_map_) count += entry.second;
	return count;
}

Worker::Worker(Unit unit) : unit_(unit), order_(new IdleWorkerOrder(*this))
{
}

void Worker::apply_orders()
{
	order_->apply_orders();
}

void Worker::draw()
{
	order_->draw();
}

void Worker::idle()
{
	order_.reset(new IdleWorkerOrder(*this));
}

void Worker::flee()
{
	order_.reset(new FleeWorkerOrder(*this));
}

void Worker::gather(Unit gather_target)
{
	order_.reset(new GatherWorkerOrder(*this, gather_target));
}

void Worker::build(UnitType building_type,TilePosition building_position)
{
	order_.reset(new BuildWorkerOrder(*this, building_type, building_position));
}

void Worker::combat()
{
	order_.reset(new CombatWorkerOrder(*this));
}

void Worker::continue_build(Unit building)
{
	order_.reset(new ContinueBuildWorkerOrder(*this, building));
}

void Worker::defend_base(const BWEM::Base *base,bool fight_to_the_death)
{
	order_.reset(new DefendBaseOrder(*this, base, fight_to_the_death));
}

void Worker::defend_building(Unit building_unit)
{
	order_.reset(new DefendBuildingOrder(*this, building_unit));
}

void Worker::defend_cannon_rush()
{
	order_.reset(new DefendCannonRushOrder(*this));
}

void Worker::scout_for_proxies()
{
	order_.reset(new ScoutForProxies(*this));
}

void Worker::scout()
{
	order_.reset(new ScoutWorkerOrder(*this));
}

void Worker::repair(Unit repair_target)
{
	order_.reset(new RepairWorkerOrder(*this, repair_target));
}

void Worker::wait_at_proxy_location(Position position)
{
	order_.reset(new WaitAtProxyLocationWorkerOrder(*this, position));
}

bool WorkerOrder::repel_enemy_units() const
{
	std::vector<Unit> all_enemy_units;
	for (auto& unit : Broodwar->enemies().getUnits()) {
		if (unit->isVisible() && unit->isCompleted()) all_enemy_units.push_back(unit);
	}
	bool order_applied = unit_potential(unit_, [&all_enemy_units](UnitPotential& potential){
		potential.repel_units(all_enemy_units);
	});
	return order_applied;
}

bool WorkerOrder::move_to_closest_safe_base() const
{
	Position position = Positions::None;
	key_value_vector<const BWEM::Base*,int> distance_map;
	for (auto base : base_state.controlled_bases()) {
		if (worker_manager.is_safe_base(base)) {
			Position center = center_position_for(Broodwar->self()->getRace().getResourceDepot(),
												  base->Location());
			int distance = ground_distance(center, unit_->getPosition());
			if (distance >= 0) distance_map.emplace_back(base, distance);
		}
	}
	const BWEM::Base* base = key_with_smallest_value(distance_map);
	if (base != nullptr) {
		position = center_position_for(Broodwar->self()->getRace().getResourceDepot(),
									   base->Location());
	}
	bool result = false;
	if (position.isValid()) {
		if (calculate_distance(unit_->getType(), unit_->getPosition(),
							   Broodwar->self()->getRace().getResourceDepot(), position) <= 32) {
			if (!unit_->isHoldingPosition()) unit_->holdPosition();
		} else {
			path_finder.execute_path(unit_, position, [this,position](){
				unit_move(unit_, position);
			});
		}
		result = true;
	}
	return result;
}

bool BuildWorkerOrder::is_done() const
{
	return (failed_ ||
			(constructing_seen_true_ && (building_type_.getRace() != Races::Terran || !unit_->isConstructing()) && building_exists()));
}

void BuildWorkerOrder::apply_orders()
{
	if (!constructing_seen_true_ && unit_->isConstructing()) {
		constructing_seen_true_ = true;
	}
	
	if (building_type_.getRace() == Races::Terran && building_ == nullptr && unit_->getBuildUnit() != nullptr) {
		building_ = unit_->getBuildUnit();
	}
	
	std::vector<Unit> colliding_mines;
	std::vector<Unit> colliding_visible;
	std::vector<Unit> colliding_invisible;
	determine_colliding_units(colliding_mines, colliding_visible, colliding_invisible);
	
	Unit closest_mine = smallest_priority(colliding_mines, [this](Unit mine){ return unit_->getDistance(mine); });
	if (closest_mine != nullptr) {
		unit_attack(unit_, closest_mine);
	} else {
		if (!constructing_seen_true_) {
			Position center = center_position_for(building_type_, building_position_);
			int distance = unit_->getDistance(center);
			if (distance > unit_->getType().sightRange() / 2) {
				failed_ = !path_finder.execute_path(unit_, center, [this,center]() {
					unit_move(unit_, center);
				});
			} else {
				bool unobstructed = colliding_mines.empty() && colliding_visible.empty() && colliding_invisible.empty();
				bool enough_resources = MineralGas(Broodwar->self()).can_pay(MineralGas(building_type_));
				
				if (unobstructed && enough_resources) {
					unit_->build(building_type_, building_position_);
				}
				
				if (unobstructed) {
					build_timeout_frame_ = -1;
				} else if (is_not_build_base()) {
					if (build_timeout_frame_ == -1) {
						build_timeout_frame_ = Broodwar->getFrameCount() + kBuildTimeoutFrames;
					} else {
						if (Broodwar->getFrameCount() >= build_timeout_frame_) failed_ = true;
					}
				}
				
				if (enough_resources) {
					resource_timeout_frame_ = -1;
				} else {
					if (resource_timeout_frame_ == -1) {
						resource_timeout_frame_ = Broodwar->getFrameCount() + kResourceTimeoutFrames;
					} else {
						if (Broodwar->getFrameCount() >= resource_timeout_frame_) failed_ = true;
					}
				}
			}
		}
		
		if (constructing_seen_true_ && !unit_->isConstructing() && !building_exists()) {
			if (colliding_visible.empty() || !colliding_invisible.empty() || !colliding_mines.empty()) {
				need_detection_counter_++;
				if (need_detection_counter_ >= 3) need_detection_ = true;
			}
			constructing_seen_true_ = false;
		}
		if (need_detection_ &&
			building_exists()) {
			need_detection_ = false;
		}
		if (need_detection_ &&
			(!colliding_visible.empty() || !colliding_invisible.empty())) {
			move_around();
		}
	}
}

bool BuildWorkerOrder::is_not_build_base()
{
	return !(building_type_.isResourceDepot() &&
			 base_state.base_for_tile_position(building_position_) != nullptr);
}

void BuildWorkerOrder::move_around()
{
	Position center_position = center_position_for(building_type_, building_position_);
	Position current_position = unit_->getPosition();
	if (center_position == current_position) current_position += Position{0, 10};
	Position move_position = scale_line_segment(center_position, current_position, 12 * 8);
	for (int i = 12; i > 0; i--) {
		Position position = scale_line_segment(center_position, current_position, i * 8);
		if (walkable_line(position)) {
			move_position = position;
			break;
		}
	}
	
	Position target_position = rotate_around(center_position, move_position, clockwise_ ? kMoveAroundAngle : -kMoveAroundAngle);
	if (walkable_line(target_position)) {
		unit_move(unit_, target_position);
	} else {
		clockwise_ = !clockwise_;
	}
}

bool BuildWorkerOrder::walkable_line(Position position)
{
	return position.isValid() && ::walkable_line(FastWalkPosition(unit_->getPosition()), FastWalkPosition(position));
}

void BuildWorkerOrder::draw() const
{
	Position position(building_position_);
	Broodwar->drawBoxMap(position.x, position.y, position.x + building_type_.tileWidth() * 32, position.y + building_type_.tileHeight() * 32, Colors::White);
}

MineralGas BuildWorkerOrder::reserved_resources() const
{
	return building_ != nullptr ? MineralGas() : MineralGas(building_type_);
}

void BuildWorkerOrder::determine_colliding_units(std::vector<Unit>& colliding_mines,
							std::vector<Unit>& colliding_visible,
							std::vector<Unit>& colliding_invisible) const
{
	if (!building_type_.isRefinery()) {
		Position center = center_position_for(building_type_, building_position_);
		auto units = Broodwar->getUnitsInRectangle(Position(building_position_),
												   Position(building_position_ + building_type_.tileSize()),
												   !Filter::IsFlying &&
												   !Filter::IsLoaded &&
												   !Filter::IsSpell &&
												   [this](Unit u){ return u != unit_; } &&
												   [this](Unit u){ return u->getType() != building_type_ || u->getTilePosition() != building_position_; } &&
												   Filter::GetLeft <= center.x + building_type_.dimensionRight() &&
												   Filter::GetTop <= center.y + building_type_.dimensionDown() &&
												   Filter::GetRight >= center.x - building_type_.dimensionLeft() &&
												   Filter::GetBottom >= center.y - building_type_.dimensionUp());
		for (Unit unit : units) {
			if (unit->getType() == UnitTypes::Terran_Vulture_Spider_Mine) {
				colliding_mines.push_back(unit);
			} else {
				if (not_cloaked(unit)) {
					colliding_visible.push_back(unit);
				} else {
					colliding_invisible.push_back(unit);
				}
			}
		}
	}
}

bool BuildWorkerOrder::building_exists() const
{
	return unit_grid.is_friendly_building_in_tile(building_type_, building_position_);
}

bool ContinueBuildWorkerOrder::is_done() const
{
	return failed_ || (building_ != nullptr && unit_->getBuildUnit() == nullptr);
}

void ContinueBuildWorkerOrder::apply_orders()
{
	if (!build_unit_seen_ && unit_->getBuildUnit() == building_) {
		build_unit_seen_ = true;
	}
	
	if (!build_unit_seen_) {
		if (!building_->exists()) {
			failed_ = true;
		} else {
			Position position = center_position_for(building_->getType(), building_->getTilePosition());
			failed_ = !path_finder.execute_path(unit_, position, [this](){
				unit_right_click(unit_, building_);
			});
		}
	}
}

void ContinueBuildWorkerOrder::draw() const
{
	Position position(building_->getTilePosition());
	UnitType type = building_->getType();
	Broodwar->drawBoxMap(position.x, position.y, position.x + type.tileWidth() * 32, position.y + type.tileHeight() * 32, Colors::White);
}

void DefendBaseOrder::apply_orders()
{
	if (!fight_to_the_death_ && !is_less_than_half_damaged(unit_)) {
		done_ = true;
		return;
	}
	
	Position center = base_->Center();
	Unitset unit_set = Broodwar->getUnitsInRadius(center, 320);
	
	key_value_vector<Unit,int> distance_map;
	std::vector<Unit> static_defense_buildings;
	for (auto& target : unit_set) {
		if (target->getPlayer()->isEnemy(Broodwar->self()) &&
			!target->isFlying() &&
			not_cloaked(target) &&
			(can_attack(target, false) || is_spellcaster(target->getType()))) {
			distance_map.emplace_back(target, unit_->getDistance(target));
		}
		if (target->getPlayer() == Broodwar->self() &&
			target->getType().isBuilding() &&
			target->isCompleted() &&
			target->isPowered() &&
			can_attack(target, false) &&
			center.getApproxDistance(target->getPosition()) < 160) {
			static_defense_buildings.push_back(target);
		}
	}
	
	if (!static_defense_buildings.empty()) {
		for (auto& target : unit_set) {
			if (target->getPlayer()->isEnemy(Broodwar->self())) {
				bool enemy_can_hit_static_defense = std::any_of(static_defense_buildings.begin(),
																static_defense_buildings.end(),
																[target](auto static_defense) {
																	return can_attack_in_range(target, static_defense);
																});
				if (enemy_can_hit_static_defense) {
					static_defense_buildings.clear();
					break;
				}
			}
		}
	}
	
	Unit target = key_with_smallest_value(distance_map);
	if (target != nullptr) {
		if (static_defense_buildings.empty()) {
			unit_attack(unit_, target);
		} else {
			bool static_defense_can_hit_enemy = std::any_of(static_defense_buildings.begin(),
															static_defense_buildings.end(),
															[target](auto static_defense) {
																return can_attack_in_range(static_defense, target);
															});
			if (static_defense_can_hit_enemy) {
				unit_attack(unit_, target);
			} else {
				Unit static_defense = smallest_priority(static_defense_buildings,
														[&](auto static_defense) {
															return -unit_->getDistance(static_defense);
														});
				if (unit_->getDistance(static_defense) < 32) {
					done_ = true;
				} else {
					unit_move(unit_, static_defense->getPosition());
				}
			}
		}
	} else {
		done_ = true;
	}
}

void DefendBuildingOrder::apply_orders()
{
	key_value_vector<Unit,int> distance_map;
	for (auto& enemy_unit : Broodwar->enemies().getUnits()) {
		if (building_unit_->getDistance(enemy_unit) <= 224 &&
			!enemy_unit->isFlying() &&
			not_cloaked(enemy_unit) &&
			(can_attack(enemy_unit, false) || is_spellcaster(enemy_unit->getType()))) {
			distance_map.emplace_back(enemy_unit, unit_->getDistance(enemy_unit));
		}
	}
	Unit target = key_with_smallest_value(distance_map);
	if (target != nullptr) {
		unit_attack(unit_, target);
	} else {
		done_ = true;
	}
}

void DefendCannonRushOrder::apply_orders()
{
	Position center = center_position_for(Broodwar->self()->getRace().getResourceDepot(), Broodwar->self()->getStartLocation());
	std::vector<const InformationUnit*> possible_targets;
	bool completed_cannon_found = false;
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (should_apply_cannon_rush_defense(*enemy_unit, true)) {
			possible_targets.push_back(enemy_unit);
			if (enemy_unit != nullptr &&
				enemy_unit->type == UnitTypes::Protoss_Photon_Cannon &&
				enemy_unit->is_completed() &&
				calculate_distance(enemy_unit->type, enemy_unit->position, unit_->getType(), unit_->getPosition()) > 32) {
				completed_cannon_found = true;
			}
		}
	}
	if (completed_cannon_found) {
		done_ = true;
		return;
	}
	const InformationUnit* target = smallest_priority(possible_targets, [this,center](auto& enemy_unit){
		return std::make_tuple(enemy_unit->type != UnitTypes::Protoss_Photon_Cannon,
							   !enemy_unit->is_completed(),
							   enemy_unit->is_completed() ? 0 : enemy_unit->start_frame,
							   center.getApproxDistance(enemy_unit->position),
							   unit_->getOrderTarget() == enemy_unit->unit ? INT_MIN : enemy_unit->unit->getID());
	});
	if (target != nullptr) {
		path_finder.execute_path(unit_, target->position, [&,target]{
			if (target->unit->exists()) {
				unit_attack(unit_, target->unit);
			} else {
				unit_move(unit_, target->position);
			}
		});
	} else {
		done_ = true;
	}
}

bool DefendCannonRushOrder::should_apply_cannon_rush_defense(const InformationUnit& information_unit,bool allow_pylons_and_probes)
{
	return ((information_unit.type == UnitTypes::Protoss_Photon_Cannon ||
			 (allow_pylons_and_probes && (information_unit.type == UnitTypes::Protoss_Pylon ||
										  information_unit.type == UnitTypes::Protoss_Probe))) &&
			information_unit.base_distance <= 768);
}

bool FleeWorkerOrder::is_done() const
{
	return timeout_frame_ >= 0 && Broodwar->getFrameCount() >= timeout_frame_;
}

void FleeWorkerOrder::apply_orders()
{
	bool order_applied = repel_enemy_units();
	if (order_applied || timeout_frame_ < 0) timeout_frame_ = Broodwar->getFrameCount() + 96;
	if (!order_applied) order_applied = move_to_closest_safe_base();
	if (!order_applied) random_move(unit_);
}

bool GatherWorkerOrder::is_done() const
{
	return (!gather_target_->exists() || return_started_ || !is_base_valid()) && !is_carrying();
}

bool GatherWorkerOrder::is_pullable() const
{
	return !is_carrying() && unit_->getOrder() != Orders::HarvestGas;
}

void GatherWorkerOrder::apply_orders()
{
	if (!gather_target_->getType().isMineralField() || !optimal_mineral_mining()) {
		if (!is_carrying()) {
			if (!is_gathering() || !unit_has_target(unit_, gather_target_)) {
				path_finder.execute_path(unit_, gather_target_->getPosition(), [this](){
					if (!unit_has_target(unit_, gather_target_)) unit_->gather(gather_target_);
				});
			}
		} else {
			if (!is_gathering()) {
				unit_->returnCargo();
			}
			return_started_ = true;
		}
	}
	if (unit_->getType() == UnitTypes::Zerg_Drone && unit_->isUnderAttack()) worker_.flee();
}

bool GatherWorkerOrder::is_carrying() const
{
	return unit_->isCarryingGas() || unit_->isCarryingMinerals();
}

bool GatherWorkerOrder::is_gathering() const
{
	return unit_->isGatheringGas() || unit_->isGatheringMinerals();
}

bool GatherWorkerOrder::is_base_valid() const
{
	for (auto base : base_state.controlled_bases()) {
		if (worker_manager.is_worker_mining_allowed_from_base(base, unit_)) {
			bool mineral_found = std::any_of(base->Minerals().begin(), base->Minerals().end(), [this](auto mineral){
				return mineral->Unit() == gather_target_;
			});
			if (mineral_found) return true;
			bool geyser_found = std::any_of(base->Geysers().begin(), base->Geysers().end(), [this](auto geyser){
				return geyser->TopLeft() == gather_target_->getTilePosition();
			});
			if (geyser_found) return true;
		}
	}
	return false;
}

bool GatherWorkerOrder::optimal_mineral_mining()
{
	bool order_issued = false;
	int distance = unit_->getDistance(gather_target_);
	if (distance <= 100) {
		std::set<WorkerPositionAndVelocity>& optimal_order_positions = worker_manager.optimal_mining_set_for_mineral(gather_target_);
		if (distance == 0) {
			int frame = Broodwar->getFrameCount() - Broodwar->getLatencyFrames() - 10;
			auto it = position_history_.find(frame);
			if (it != position_history_.end()) {
				for (auto& entry : position_history_) {
					if (entry.first > frame + 3) optimal_order_positions.erase(entry.second);
				}
				optimal_order_positions.insert(it->second);
			}
			position_history_.clear();
		} else {
			WorkerPositionAndVelocity current = { unit_->getPosition(), int(unit_->getVelocityX() * 100.0), int(unit_->getVelocityY() * 100.0) };
			if (unit_->getOrder() == Orders::MoveToMinerals &&
				optimal_order_positions.find(current) != optimal_order_positions.end()) {
				unit_->gather(gather_target_);
				order_issued = true;
			}
			position_history_.emplace(Broodwar->getFrameCount(), current);
		}
	}
	return order_issued;
}

void IdleWorkerOrder::apply_orders()
{
	bool order_applied = repel_enemy_units();
	if (!order_applied) order_applied = move_to_closest_safe_base();
	if (!order_applied) random_move(unit_);
}

void ScoutForProxies::apply_orders()
{
	remove_visible_positions();
	if (positions_.empty()) {
		if (pass_ < 3) {
			pass_++;
			fill_positions();
			remove_visible_positions();
		}
		if (positions_.empty()) {
			done_ = true;
			return;
		}
	}
	
	int best_distance = INT_MAX;
	FastPosition best_position;
	for (auto tile_position : positions_) {
		FastPosition position = center_position(tile_position);
		int distance = unit_->getDistance(position);
		if (distance < best_distance) {
			best_distance = distance;
			best_position = position;
		}
	}
	if (best_position.isValid()) {
		unit_move(unit_, best_position);
	}
}

bool ScoutForProxies::is_done() const
{
	return done_ || enemy_opening_known();
}

bool ScoutForProxies::enemy_opening_known() const
{
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		for (auto enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->type == UnitTypes::Protoss_Photon_Cannon &&
				enemy_unit->base_distance <= 768) {
				return true;
			}
		}
		return false;
	}
	return (opponent_model.enemy_opening() != EnemyOpening::Unknown);
}

void ScoutForProxies::fill_positions()
{
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			FastTilePosition tile_position(x, y);
			const BWEM::Area* area = bwem_map.GetArea(tile_position);
			if (contains(base_state.controlled_areas(), area)) {
				positions_.insert(tile_position);
			}
		}
	}
}

void ScoutForProxies::remove_visible_positions()
{
	std::set<FastTilePosition> new_positions;
	for (auto tile_position : positions_) {
		if (!Broodwar->isVisible(tile_position)) {
			new_positions.insert(tile_position);
		}
	}
	std::swap(positions_, new_positions);
}

ScoutWorkerOrder::ScoutWorkerOrder(Worker& worker) : WorkerOrder(worker)
{
	if (worker_manager.should_scout_map_center()) {
		mode_ = Mode::LookAtMapCenter;
	} else {
		current_target_base_ = closest_undiscovered_starting_base();
		mode_ = (current_target_base_ == nullptr) ? Mode::Done : Mode::SearchBase;
	}
}

void ScoutWorkerOrder::apply_orders()
{
	switch (mode_) {
		case Mode::LookAtMapCenter: {
			FastPosition center = bwem_map.Center();
			const auto can_build_at = [](FastTilePosition tile_position){
				for (int dy = 0; dy < UnitTypes::Terran_Barracks.tileHeight(); dy++) {
					for (int dx = 0; dx < UnitTypes::Terran_Barracks.tileWidth(); dx++) {
						FastTilePosition delta(dx, dy);
						if (!Broodwar->isBuildable(tile_position + delta)) {
							return false;
						}
					}
				}
				return true;
			};
			std::vector<FastPosition> tiles;
			for (int dy = -8; dy <= 8; dy++) {
				for (int dx = -8; dx <= 8; dx++) {
					FastTilePosition delta(dx, dy);
					FastTilePosition tile_position = center + delta;
					if (!Broodwar->isExplored(tile_position) &&
						can_build_at(tile_position)) {
						tiles.push_back(center_position(tile_position));
					}
				}
			}
			if (tiles.empty()) {
				current_target_base_ = closest_undiscovered_starting_base();
				mode_ = Mode::SearchBase;
			} else {
				FastPosition position = smallest_priority(tiles, [this](FastPosition candidate){
					return unit_->getPosition().getApproxDistance(candidate);
				});
				path_finder.execute_path(unit_, position, [this,position](){
					unit_move(unit_, position);
				});
			}
			
			break; }
		case Mode::SearchBase:
			if (current_target_base_ == tactics_manager.enemy_start_base()) {
				mode_ = Mode::MoveAround;
			} else {
				if (!contains(base_state.unexplored_start_bases(), current_target_base_)) {
					current_target_base_ = closest_undiscovered_starting_base();
				}
				
				if (current_target_base_ != nullptr) {
					Position position = center_position_for(Broodwar->self()->getRace().getResourceDepot(),
															current_target_base_->Location());
					path_finder.execute_path(unit_, position, [this,position](){
						unit_move(unit_, position);
					});
				} else {
					mode_ = Mode::Done;
				}
			}
			break;
		case Mode::MoveAround:
			move_around();
			if (should_look_at_natural()) mode_ = Mode::LookAtNatural;
			if (!is_less_than_half_damaged(unit_)) mode_ = Mode::WaitAtNatural;
			break;
		case Mode::LookAtNatural: {
			Position position = tactics_manager.enemy_natural_base()->Center();
			Position near_walkable = walkability_grid.walkable_tile_near(position, 128);
			if (near_walkable.isValid()) position = near_walkable;
			path_finder.execute_path(unit_, position, [this,position](){
				unit_move(unit_, position);
			});
			if (unit_->getDistance(position) <= 8 &&
				opponent_model.enemy_natural_sufficiently_scouted()) {
				mode_ = Mode::MoveAroundFromNatural;
			}
			if (opponent_model.enemy_opening() != EnemyOpening::Unknown) mode_ = Mode::MoveAroundFromNatural;
			if (!is_less_than_half_damaged(unit_)) mode_ = Mode::WaitAtNatural;
			break; }
		case Mode::MoveAroundFromNatural: {
			Position position = tactics_manager.enemy_start_base()->Center();
			path_finder.execute_path(unit_, position, [this,position](){
				unit_move(unit_, position);
			});
			if (calculate_distance(opponent_model.enemy_race().getResourceDepot(), position, unit_->getType(), unit_->getPosition()) <= unit_->getType().sightRange()) {
				mode_ = Mode::MoveAround;
			}
			if (!is_less_than_half_damaged(unit_)) mode_ = Mode::WaitAtNatural;
			break; }
		case Mode::WaitAtNatural: {
			Position position = determine_wait_at_natural_position();
			int distance = ground_distance(unit_->getPosition(), position);
			if (worker_manager.scout_wait_at_natural() &&
				position.isValid() &&
				distance >= 0 &&
				Broodwar->getFrameCount() + int(distance / unit_->getType().topSpeed() + 0.5) < kWaitAtNaturalLimitFrame &&
				!army_sees_worker() &&
				information_manager.all_units().at(unit_).base_distance > 320) {
				
				bool order_issued = false;
				if (!order_issued) {
					order_issued = safe_move(position);
				}
				if (!order_issued) {
					order_issued = safe_move(base_state.start_base()->Center());
				}
				if (!order_issued) {
					position = base_state.start_base()->Center();
					path_finder.execute_path(unit_, position, [this,position](){
						unit_move(unit_, position);
					});
				}
			} else {
				mode_ = Mode::Done;
			}
			break; }
		case Mode::Done:
			break;
	}
}

Position ScoutWorkerOrder::determine_wait_at_natural_position() const
{
	auto const enclosed_areas = [](const std::set<const BWEM::Area*>& areas){
		std::set<const BWEM::Area*> uncontrolled_base_areas;
		for (auto& base : base_state.bases()) {
			if (!contains(areas, base->GetArea()) &&
				contains(base_state.controlled_bases(), base)) {
				uncontrolled_base_areas.insert(base->GetArea());
			}
		}
		
		std::set<const BWEM::Area*> result;
		result.insert(areas.cbegin(), areas.cend());
		for (auto& area : bwem_map.Areas()) {
			std::set<const BWEM::Area*> reachable = BaseState::reachable_areas(&area);
			std::set<const BWEM::Area*> reachable_blocked = BaseState::reachable_areas(&area, areas);
			if (common_elements(reachable, areas) && !common_elements(reachable_blocked, uncontrolled_base_areas)) result.insert(&area);
		}
		
		return result;
	};
	
	const BWEM::Area* enemy_start_area = tactics_manager.enemy_start_base()->GetArea();
	const BWEM::Area* enemy_natural_area = tactics_manager.enemy_natural_base() != nullptr ? tactics_manager.enemy_natural_base()->GetArea() : nullptr;
	bool backdoor_or_no_natural = enemy_natural_area == nullptr || contains(enclosed_areas({enemy_start_area}), enemy_natural_area);
	Position result = Positions::None;
	
	if (!backdoor_or_no_natural && !contains(base_state.opponent_bases(), tactics_manager.enemy_natural_base())) {
		result = tactics_manager.enemy_natural_base()->Center();
	} else {
		std::set<const BWEM::Area*> areas;
		areas.insert(enemy_start_area);
		if (enemy_natural_area != nullptr) {
			areas.insert(enemy_natural_area);
		}
		areas = enclosed_areas(areas);
		const BWEM::Area* border_area = backdoor_or_no_natural ? enemy_start_area : enemy_natural_area;
		std::vector<const BWEM::ChokePoint*> chokepoints;
		for (auto& area : border_area->AccessibleNeighbours()) {
			if (!contains(areas, area)) {
				for (auto& cp : border_area->ChokePoints(area)) {
					if (!cp.Blocked()) {
						chokepoints.push_back(&cp);
					}
				}
			}
		}
		
		key_value_vector<const BWEM::ChokePoint*,int> chokepoint_width_map;
		for (auto& cp : chokepoints) chokepoint_width_map.emplace_back(cp, chokepoint_width(cp));
		
		const BWEM::ChokePoint* chokepoint = key_with_largest_value(chokepoint_width_map);
		if (chokepoint != nullptr) {
			result = chokepoint_center(chokepoint);
		}
	}
	
	return result;
}

bool ScoutWorkerOrder::army_sees_worker() const
{
	for (auto& information_unit : information_manager.my_units()) {
		Unit unit = information_unit->unit;
		if (!information_unit->type.isWorker() &&
			can_attack(unit, false) &&
			unit->getDistance(unit_) <= information_unit->type.sightRange()) {
			return true;
		}
	}
	return false;
}

bool ScoutWorkerOrder::should_look_at_natural() const
{
	return (tactics_manager.enemy_natural_base() != nullptr &&
			opponent_model.enemy_opening() == EnemyOpening::Unknown &&
			opponent_model.enemy_base_sufficiently_scouted() &&
			!opponent_model.enemy_natural_sufficiently_scouted() &&
			information_manager.enemy_count(UnitTypes::Terran_Barracks) <= 1 &&
			!information_manager.enemy_exists(UnitTypes::Terran_Factory) &&
			!information_manager.enemy_exists(UnitTypes::Protoss_Gateway) &&
			!information_manager.enemy_exists(UnitTypes::Protoss_Forge) &&
			!information_manager.enemy_exists(UnitTypes::Protoss_Cybernetics_Core) &&
			!information_manager.enemy_exists(UnitTypes::Zerg_Spawning_Pool) &&
			!enemy_has_non_start_resource_depot() &&
			Broodwar->getFrameCount() >= opponent_model.enemy_earliest_expansion_frame());
}

bool ScoutWorkerOrder::enemy_has_non_start_resource_depot() const
{
	bool result = false;
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->type.isResourceDepot()) {
			const BWEM::Base* base = base_state.base_for_tile_position(enemy_unit->tile_position());
			if (base == nullptr || !base->Starting()) {
				result = true;
				break;
			}
		}
	}
	return result;
}

bool ScoutWorkerOrder::is_done() const
{
	return (mode_ == Mode::Done ||
			other_scout_found_base() ||
			opponent_model.enemy_opening() == EnemyOpening::Z_4_5Pool ||
			opponent_model.enemy_opening() == EnemyOpening::Z_9PoolSpeed ||
			opponent_model.enemy_opening() == EnemyOpening::T_ProxyRax ||
			opponent_model.enemy_opening() == EnemyOpening::T_BBS ||
			opponent_model.enemy_opening() == EnemyOpening::P_ProxyGate ||
			opponent_model.enemy_opening() == EnemyOpening::P_2GateFast);
}

bool ScoutWorkerOrder::other_scout_found_base() const
{
	const BWEM::Base* enemy_start_base = tactics_manager.enemy_start_base();
	return (enemy_start_base != nullptr && current_target_base_ != enemy_start_base);
}

void ScoutWorkerOrder::move_around()
{
	if (current_target_base_ == nullptr) return;
	
	if (configuration.draw_enabled()) Broodwar->drawTextMap(unit_->getPosition(), clockwise_ ? "CW" : "CCW");
	
	Position center_position = current_target_base_->Center();
	Position current_position = unit_->getPosition();
	
	Position move_position = scale_line_segment(center_position, current_position, 40 * 8);
	for (int i = 40; i > 0; i--) {
		Position position = scale_line_segment(center_position, current_position, i * 8);
		if (walkable_line(position)) {
			move_position = position;
			break;
		}
	}
	
	Position target_position = rotate_around(center_position, move_position, clockwise_ ? kMoveAroundAngle : -kMoveAroundAngle);
	bool found = false;
	int minimum_danger_distance = 8;
	
	for (int i = 0; i < 40; i++) {
		Position position = scale_line_segment(target_position, center_position, -i * 8);
		if (!position.isValid()) continue;
		auto [danger_unit, danger_distance] = determine_danger(position);
		if (danger_unit != nullptr && is_defense_building(danger_unit)) {
			minimum_danger_distance = 96;
		}
		if (danger_distance > minimum_danger_distance &&
			walkable_line(position) &&
			check_collision(unit_, position, 8)) {
			target_position = position;
			found = true;
			break;
		}
	}
	
	if (!found) {
		for (int i = 1; i <= 20; i++) {
			Position position = scale_line_segment(target_position, center_position, i * 8);
			if (!position.isValid()) continue;
			auto [danger_unit, danger_distance] = determine_danger(position);
			if (danger_unit != nullptr && is_defense_building(danger_unit)) {
				minimum_danger_distance = 96;
			}
			if (danger_distance > minimum_danger_distance &&
				walkable_line(position) &&
				check_collision(unit_, position, 8)) {
				target_position = position;
				found = true;
				break;
			}
		}
	}
	
	if (found) {
		if (!safe_move_around(target_position)) {
			unit_move(unit_, target_position);
		}
	} else {
		clockwise_ = !clockwise_;
	}
}

bool ScoutWorkerOrder::walkable_line(Position position)
{
	return position.isValid() && ::walkable_line(FastWalkPosition(unit_->getPosition()), FastWalkPosition(position));
}

std::tuple<const InformationUnit*,int> ScoutWorkerOrder::determine_danger(Position position)
{
	const InformationUnit* danger_unit = nullptr;
	int danger_distance = INT_MAX;
	
	for (auto& enemy_unit : information_manager.enemy_units()) {
		int max_range = weapon_max_range(enemy_unit->type, enemy_unit->player, false);
		if (max_range >= 0 && enemy_unit->is_completed()) {
			int distance = calculate_distance(unit_->getType(), position, enemy_unit->type, enemy_unit->position) - max_range;
			if (distance < danger_distance) {
				danger_unit = enemy_unit;
				danger_distance = distance;
			}
		}
	}
	
	return std::make_tuple(danger_unit, danger_distance);
}

bool ScoutWorkerOrder::is_defense_building(const InformationUnit* information_unit)
{
	UnitType type = information_unit->type;
	return (type == UnitTypes::Terran_Bunker ||
			type == UnitTypes::Protoss_Photon_Cannon ||
			type == UnitTypes::Zerg_Sunken_Colony);
}

bool ScoutWorkerOrder::safe_move_around(Position target_position)
{
	const int danger_margin = 64;
	if (configuration.draw_enabled()) Broodwar->drawCircleMap(target_position, 5, Colors::White, true);
	
	auto [danger_unit, danger_distance] = determine_danger(target_position);
	
	if (danger_distance > danger_margin || danger_unit == nullptr) {
		unit_move(unit_, target_position);
		return true;
	}
	
	Position a = danger_unit->position - unit_->getPosition();
	Position b = target_position - unit_->getPosition();
	int inner_product = a.x * b.x + a.y * b.y;
	
	if (inner_product < 0) {
		unit_move(unit_, target_position);
		return true;
	}
	
	int max_range = weapon_max_range(danger_unit->type, danger_unit->player, false);
	int back_distance = danger_margin + max_range + max_unit_dimension(unit_->getType()) + max_unit_dimension(danger_unit->type);
	Position back_position = scale_line_segment(danger_unit->position, unit_->getPosition(), back_distance);
	Position move_position = rotate_around(danger_unit->position, back_position, clockwise_ ? kSafeMoveAroundAngle : -kSafeMoveAroundAngle);
	if (!walkable_line(move_position) || !check_collision(unit_, move_position)) return false;
	
	unit_move(unit_, move_position);
	if (configuration.draw_enabled()) {
		Broodwar->drawCircleMap(move_position, 5, Colors::Red, true);
		Broodwar->drawCircleMap(back_position, 5, Colors::Yellow, true);
	}
	return true;
}

bool ScoutWorkerOrder::safe_move(Position target_position)
{
	struct Grid
	{
		FastTilePosition start_tile_position;
		
		Grid(FastTilePosition start_tile_position) : start_tile_position(start_tile_position) {}
		
		inline bool operator()(unsigned int x,unsigned int y) const
		{
			FastTilePosition tile_position(x, y);
			return (start_tile_position == tile_position ||
					walkability_grid.is_terrain_walkable(tile_position));
		}
	};
	JPS::PathVector path;
	FastTilePosition start_tile_position(unit_->getPosition());
	FastTilePosition target_tile_position(target_position);
	Grid grid(start_tile_position);
	bool found = JPS::findPath(path, grid, start_tile_position.x, start_tile_position.y, target_tile_position.x, target_tile_position.y, 1);
	Position next_position = Positions::None;
	if (found) {
		next_position = unit_->getPosition();
		for (auto position : path) {
			next_position = center_position(FastTilePosition(position.x, position.y));
			if (unit_->getPosition().getApproxDistance(next_position) >= 96) break;
		}
	}
	if (!next_position.isValid()) {
		return false;
	}
	if (unit_->getPosition().getApproxDistance(next_position) >= 96) {
		next_position = scale_line_segment(unit_->getPosition(), next_position, 96);
	}
	
	const int danger_margin = 64;
	if (configuration.draw_enabled()) Broodwar->drawCircleMap(next_position, 5, Colors::White, true);
	
	auto [danger_unit, danger_distance] = determine_danger(next_position);
	
	if (danger_distance > danger_margin || danger_unit == nullptr) {
		path_finder.execute_path(unit_, target_position, [this,target_position](){
			unit_move(unit_, target_position);
		});
		return true;
	}
	
	Position a = danger_unit->position - unit_->getPosition();
	Position b = next_position - unit_->getPosition();
	int inner_product = a.x * b.x + a.y * b.y;
	
	if (inner_product < 0) {
		path_finder.execute_path(unit_, target_position, [this,target_position](){
			unit_move(unit_, target_position);
		});
		return true;
	}
	
	int max_range = weapon_max_range(danger_unit->type, danger_unit->player, false);
	int back_distance = danger_margin + max_range + max_unit_dimension(unit_->getType()) + max_unit_dimension(danger_unit->type);
	Position back_position = scale_line_segment(danger_unit->position, unit_->getPosition(), back_distance);
	Position best_move_position = Positions::None;
	int best_danger_distance = INT_MIN;
	
	for (int i = -20; i <= 20; i++) {
		double angle = i * (kSafeMoveAngle / 20.0);
		Position move_position = rotate_around(danger_unit->position, back_position, angle);
		if (walkable_line(move_position) &&
			check_collision(unit_, move_position)) {
			auto [current_danger_unit, current_danger_distance] = determine_danger(move_position);
			if (current_danger_distance > best_danger_distance) {
				best_move_position = move_position;
				best_danger_distance = current_danger_distance;
			}
		}
	}
	
	if (!best_move_position.isValid()) {
		return false;
	}
	unit_move(unit_, best_move_position);
	if (configuration.draw_enabled()) {
		Broodwar->drawCircleMap(best_move_position, 5, Colors::Red, true);
		Broodwar->drawCircleMap(back_position, 5, Colors::Yellow, true);
	}
	return true;
}

const BWEM::Base* ScoutWorkerOrder::closest_undiscovered_starting_base() const
{
	UnitType center_type = Broodwar->self()->getRace().getResourceDepot();
	key_value_vector<const BWEM::Base*,int> base_distance;
	for (auto& base : base_state.undiscovered_starting_bases()) {
		int distance = ground_distance(center_position_for(center_type, base->Location()), unit_->getPosition());
		if (distance >= 0) base_distance.emplace_back(base, distance);
	}
	return key_with_smallest_value(base_distance);
}

bool RepairWorkerOrder::is_done() const
{
	return (!repair_target_->exists() ||
			repair_target_->getHitPoints() >= repair_target_->getType().maxHitPoints() ||
			(repairing_seen_ && !unit_->isRepairing()));
}

void RepairWorkerOrder::apply_orders()
{
	if (repair_target_->exists()) {
		path_finder.execute_path(unit_, repair_target_->getPosition(), [this](){
			if (!unit_has_target(unit_, repair_target_) ||
				!unit_->isRepairing()) {
				unit_->repair(repair_target_);
			}
		});
	}
	if (unit_->isRepairing()) repairing_seen_ = true;
}

void WaitAtProxyLocationWorkerOrder::apply_orders()
{
	path_finder.execute_path(unit_, proxy_location_, [this](){
		unit_move(unit_, proxy_location_);
	});
}

void WorkerManager::before()
{
	register_new_workers();
	done_workers_to_idle();
}

void WorkerManager::after()
{
	update_base_unsafety();
	update_worker_defend_orders();
	update_worker_allocation();
}

void WorkerManager::register_new_workers()
{
	for (Unit unit : Broodwar->self()->getUnits()) {
		if (unit->getType().isWorker() && unit->isCompleted() && worker_map_.count(unit) == 0) {
			worker_map_.emplace(unit, unit);
		}
	}
}

void WorkerManager::done_workers_to_idle()
{
	for (auto &entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->is_done()) worker.idle();
	}
}

void WorkerManager::update_base_unsafety()
{
	for (auto& base : base_state.controlled_bases()) {
		Position center = center_position_for(Broodwar->self()->getRace().getResourceDepot(), base->Location());
		Unitset unit_set = Broodwar->getUnitsInRadius(center, 320);
		bool under_attack_found = false;
		bool dangerous_enemy_found = false;
		for (auto& unit : unit_set) {
			if (unit->getPlayer() == Broodwar->self() && unit->isUnderAttack() &&
				(unit->getType().isWorker() || !is_less_than_half_damaged(unit))) under_attack_found = true;
			if (unit->getPlayer()->isEnemy(Broodwar->self()) &&
				!unit->getType().isWorker() &&
				((can_attack(unit, false) || is_spellcaster(unit->getType())) && unit->getType() != UnitTypes::Terran_Medic)) {
				if (is_dangerous_unit(unit)) dangerous_enemy_found = true;
			}
		}
		if (under_attack_found || dangerous_enemy_found) {
			base_unsafe_until_frame_[base] = Broodwar->getFrameCount() + 96;
		}
		if (under_attack_found) {
			base_under_attack_until_frame_[base] = Broodwar->getFrameCount() + 96;
		}
	}
	
	remove_expired(base_unsafe_until_frame_);
	remove_expired(base_under_attack_until_frame_);
}

void WorkerManager::remove_expired(std::map<const BWEM::Base*,int>& base_frame_map)
{
	std::vector<const BWEM::Base*> expired_bases;
	for (auto& entry : base_frame_map) {
		if (entry.second <= Broodwar->getFrameCount()) {
			expired_bases.push_back(entry.first);
		}
	}
	for (auto& base : expired_bases) {
		base_frame_map.erase(base);
	}
}

void WorkerManager::update_worker_allocation()
{
	std::map<TilePosition,Unit> tile_position_to_refinery_map;
	for (auto& unit : Broodwar->self()->getUnits()) {
		if (unit->getType().isRefinery() && unit->isCompleted()) {
			tile_position_to_refinery_map[unit->getTilePosition()] = unit;
		}
	}
	
	std::vector<std::pair<Unit,const BWEM::Base*>> minerals;
	std::vector<std::pair<Unit,const BWEM::Base*>> refineries;
	for (auto& base : base_state.controlled_bases()) {
		for (auto& mineral : base->Minerals()) {
			Unit unit = mineral->Unit();
			if (unit->exists() && !mineral_near_planned_base_defense_creep_colony(base, unit)) minerals.emplace_back(unit, base);
		}
		for (auto& geyser : base->Geysers()) {
			auto it = tile_position_to_refinery_map.find(geyser->TopLeft());
			if (it != tile_position_to_refinery_map.end()) refineries.emplace_back(it->second, base);
		}
	}
	worker_allocation_ = WorkerAllocation(minerals, refineries, worker_map_);
}

bool WorkerManager::mineral_near_planned_base_defense_creep_colony(const BWEM::Base* base,Unit mineral_unit)
{
	bool result = false;
	if (building_manager.base_defense_creep_colony_planned_or_exists(base) &&
		!building_manager.base_defense_creep_colony_exists(base)) {
		auto it = building_placement_manager.creep_colony_positions().find(base);
		if (it != building_placement_manager.creep_colony_positions().end()) {
			TilePosition tile_position = it->second;
			if (calculate_distance(UnitTypes::Zerg_Creep_Colony, center_position_for(UnitTypes::Zerg_Creep_Colony, tile_position),
								   mineral_unit->getType(), mineral_unit->getPosition()) <= 64) {
				const InformationUnit* information_unit = unit_grid.building_in_tile(tile_position);
				if (information_unit == nullptr ||
					information_unit->type != UnitTypes::Zerg_Creep_Colony ||
					information_unit->player != Broodwar->self()) {
					result = true;
				}
			}
				
		}
	}
	return result;
}

void WorkerManager::update_worker_gather_orders()
{
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->is_idle()) {
			Unit resource;
			if (assign_worker_to_gas()) {
				resource = worker_allocation_.pick_refinery_for_worker(worker);
				if (resource == nullptr) resource = worker_allocation_.pick_mineral_for_worker(worker);
			} else {
				resource = worker_allocation_.pick_mineral_for_worker(worker);
				if (resource == nullptr) resource = worker_allocation_.pick_refinery_for_worker(worker);
			}
			
			if (resource != nullptr) worker.gather(resource);
		}
	}
}

void WorkerManager::update_worker_defend_orders()
{
	defend_base_with_workers_if_needed();
	defend_cannon_rush();
}

void WorkerManager::defend_base_with_workers_if_needed()
{
	const BWEM::Base* main_base = base_state.main_base();
	
	for (auto base : base_state.controlled_bases()) {
		if (is_base_under_attack(base)) {
			Position center = center_position_for(Broodwar->self()->getRace().getResourceDepot(), base->Location());
			Unitset unit_set = Broodwar->getUnitsInRadius(center, 320);
			int hostile_workers = 0;
			int hostile_attackable_combat_units = 0;
			int hostile_unattackable_combat_units = 0;
			Position hostile_position_sum = Position{0, 0};
			int hostile_position_count = 0;
			bool dangerous_worker_found = false;
			bool dangerous_enemy_found = false;
			bool under_attack_found = false;
			for (auto& unit : unit_set) {
				if (unit->getPlayer() == Broodwar->self() && unit->isUnderAttack() &&
					(unit->getType().isWorker() ||
					 !is_less_than_half_damaged(unit))) {
					under_attack_found = true;
				}
				if (unit->getPlayer()->isEnemy(Broodwar->self()) &&
					((can_attack(unit, false) || is_spellcaster(unit->getType())) && unit->getType() != UnitTypes::Terran_Medic)) {
					if (unit->getType().isWorker()) {
						if (is_dangerous_worker(unit)) dangerous_worker_found = true;
						hostile_workers++;
						hostile_position_sum += unit->getPosition();
						hostile_position_count++;
					} else if (unit->getType() != UnitTypes::Protoss_Photon_Cannon) {
						if (is_dangerous_unit(unit)) dangerous_enemy_found = true;
						if (!unit->isFlying() && not_cloaked(unit) &&
								   (is_melee(unit->getType()) || unit->getPlayer()->topSpeed(unit->getType()) <= Broodwar->self()->topSpeed(Broodwar->self()->getRace().getWorker()))) {
							hostile_attackable_combat_units++;
							hostile_position_sum += unit->getPosition();
							hostile_position_count++;
						} else {
							hostile_unattackable_combat_units++;
						}
					}
				}
			}
			
			bool defend_with_workers;
			bool retreat_workers;
			
			if (base_state.controlled_bases().size() == 1) {
				defend_with_workers = (dangerous_worker_found || dangerous_enemy_found);
				retreat_workers = false;
			} else if (base == main_base) {
				defend_with_workers = (dangerous_worker_found || (dangerous_enemy_found && hostile_attackable_combat_units > 0));
				retreat_workers = (dangerous_enemy_found && hostile_unattackable_combat_units > 0);
			} else {
				defend_with_workers = dangerous_worker_found;
				retreat_workers = dangerous_enemy_found;
			}
			
			if (defend_with_workers && under_attack_found) {
				int max_defending_workers = std::min(8, 2 * hostile_attackable_combat_units + (hostile_workers > 0 ? std::max(4, 1 + hostile_workers) : 0));
				int defending_workers = 0;
				for (auto& entry : worker_map_) {
					Worker& worker = entry.second;
					if (unit_set.count(worker.unit()) > 0 &&
						worker.order()->is_defending()) defending_workers++;
				}
				std::vector<Worker*> available_workers;
				for (auto& entry : worker_map_) {
					Worker& worker = entry.second;
					if (unit_set.count(worker.unit()) > 0 &&
						!worker.order()->is_defending() &&
						worker.order()->is_pullable()) {
						available_workers.push_back(&worker);
					}
				}
				if (hostile_position_count != 0) {
					Position hostile_position_average = Position{hostile_position_sum.x / hostile_position_count, hostile_position_sum.y / hostile_position_count};
					std::sort(available_workers.begin(), available_workers.end(), [hostile_position_average](const auto& a,const auto& b){
						return std::make_tuple(!is_less_than_half_damaged(a->unit()), a->unit()->getDistance(hostile_position_average)) < std::make_tuple(!is_less_than_half_damaged(b->unit()), b->unit()->getDistance(hostile_position_average));
					});
				}
				for (auto& worker : available_workers) {
					if (defending_workers >= max_defending_workers) break;
					worker->defend_base(base, !is_less_than_half_damaged(worker->unit()));
					defending_workers++;
				}
			}
			if (!retreat_workers) {
				base_defended_until_frame_[base] = Broodwar->getFrameCount() + 32;
			}
		}
	}
	
	std::vector<const BWEM::Base*> expired_base_defense;
	for (auto& entry : base_defended_until_frame_) {
		base_unsafe_until_frame_.erase(entry.first);
		base_under_attack_until_frame_.erase(entry.first);
		if (entry.second <= Broodwar->getFrameCount()) expired_base_defense.push_back(entry.first);
	}
	for (auto& base : expired_base_defense) base_defended_until_frame_.erase(base);
}

void WorkerManager::defend_cannon_rush()
{
	int current_army_supply_without_air_to_air = tactics_manager.army_supply() - tactics_manager.air_to_air_supply();
	if (current_army_supply_without_air_to_air <= 2 * 2 &&
		Broodwar->getFrameCount() <= 5 * UnitTypes::Protoss_Probe.buildTime() + UnitTypes::Protoss_Gateway.buildTime() + 2 * UnitTypes::Protoss_Zealot.buildTime()) {
		int proxy_count = 0;
		bool completed_cannon_found = false;
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (DefendCannonRushOrder::should_apply_cannon_rush_defense(*enemy_unit)) {
				proxy_count++;
				if (enemy_unit->type == UnitTypes::Protoss_Photon_Cannon &&
					enemy_unit->is_completed()) {
					completed_cannon_found = true;
				}
			}
		}
		
		if (proxy_count > 0 && !completed_cannon_found) {
			Position center = center_position_for(Broodwar->self()->getRace().getResourceDepot(), Broodwar->self()->getStartLocation());
			Unitset unit_set = Broodwar->getUnitsInRadius(center, 320);
			
			int max_defending_workers = std::min(8, 4 * proxy_count);
			int defending_workers = 0;
			for (auto& entry : worker_map_) {
				Worker& worker = entry.second;
				if (worker.order()->is_defending()) defending_workers++;
			}
			for (auto& entry : worker_map_) {
				Worker& worker = entry.second;
				if (!worker.order()->is_defending() && worker.order()->is_pullable() && unit_set.contains(worker.unit())) {
					worker.defend_cannon_rush();
					defending_workers++;
				}
				if (defending_workers >= max_defending_workers) break;
			}
		}
	}
}

bool WorkerManager::assign_worker_to_gas()
{
	if (limit_refinery_workers_ >= 0 && worker_allocation_.count_refinery_workers() >= limit_refinery_workers_) return false;
	if (force_refinery_workers_ || worker_allocation_.average_workers_per_mineral() >= 1.0) return true;
	if (spending_manager.spendable_out_of_minerals_first() ||
		spending_manager.spendable_out_of_gas_first()) {
		return !spending_manager.spendable_out_of_minerals_first();
	}
	return false;
}

bool WorkerManager::assign_building_request(UnitType building_type,TilePosition building_position)
{
	Worker* worker = pick_worker_for_task(center_position_for(building_type, building_position));
	if (worker != nullptr) {
		worker->build(building_type, building_position);
		return true;
	} else {
		return false;
	}
}

int WorkerManager::estimate_worker_travel_frames_for_building_request(UnitType building_type,TilePosition building_position)
{
	Position position = center_position_for(building_type, building_position);
	int min_distance = INT_MAX;
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->is_pullable()) {
			int distance = ground_distance(position, worker.unit()->getPosition());
			if (distance >= 0) min_distance = std::min(min_distance, distance);
		}
	}
	int frames = -1;
	if (min_distance < INT_MAX) frames = int(1.3 * min_distance / Broodwar->self()->getRace().getWorker().topSpeed());
	return frames;
}

void WorkerManager::repair_target(Unit unit,int worker_count)
{
	for (auto& entry : worker_map_) {
		if (worker_count <= 0) return;
		if (entry.second.order()->repair_target() == unit) worker_count--;
	}
	
	while (worker_count > 0) {
		Worker* worker = pick_worker_for_task(unit->getType(), unit->getTilePosition());
		if (worker != nullptr) {
			worker->repair(unit);
			worker_count--;
		} else {
			break;
		}
	}
}

void WorkerManager::continue_unfinished_building(Unit unit)
{
	for (auto& entry : worker_map_) {
		if (entry.second.order()->building() == unit) return;
	}
	
	Worker* worker = pick_worker_for_task(unit->getType(), unit->getTilePosition());
	if (worker != nullptr) worker->continue_build(unit);
}

void WorkerManager::send_initial_scout()
{
	if (!sent_initial_scout_) {
		sent_initial_scout_ = send_scout();
	}
}

void WorkerManager::send_second_scout()
{
	if (!sent_second_scout_) {
		sent_second_scout_ = send_scout();
	}
}

bool WorkerManager::send_scout()
{
	bool result = false;
	UnitType center_type = Broodwar->self()->getRace().getResourceDepot();
	Position start_position = center_position_for(center_type, Broodwar->self()->getStartLocation());
	key_value_vector<Position,int> undiscovered_base_distance_map;
	for (auto& base : base_state.undiscovered_starting_bases()) {
		Position position = center_position_for(center_type, base->Location());
		int distance = ground_distance(position, start_position);
		if (distance >= 0) undiscovered_base_distance_map.emplace_back(position, distance);
	}
	Position position = key_with_smallest_value(undiscovered_base_distance_map, Positions::None);
	if (position.isValid()) {
		Worker* worker = pick_worker_for_task(position);
		if (worker != nullptr) {
			worker->scout();
			result = true;
		}
	}
	return result;
}

void WorkerManager::stop_scouting()
{
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->is_scouting()) worker.idle();
	}
}

bool WorkerManager::is_scouting()
{
	if (!sent_initial_scout_) return false;
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->is_scouting()) return true;
	}
	return false;
}

void WorkerManager::send_proxy_scout()
{
	if (!sent_proxy_scout_) {
		Worker* worker = pick_worker_for_task();
		if (worker != nullptr) {
			worker->scout_for_proxies();
			sent_proxy_scout_ = true;
		}
	}
}

void WorkerManager::send_proxy_builder(Position proxy_position,int arrive_frame)
{
	bool already_assigned = std::any_of(worker_map_.begin(), worker_map_.end(), [proxy_position](auto& entry){
		return entry.second.order()->wait_at_proxy_location() == proxy_position;
	});
	if (already_assigned) return;
	
	key_value_vector<Worker*,int> distances;
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (!worker.order()->wait_at_proxy_location().isValid() &&
			(worker.order()->is_pullable() ||
			 worker.order()->building_position().isValid())) {
			int distance = ground_distance(proxy_position, worker.unit()->getPosition());
			if (distance >= 0) distances.emplace_back(&worker, distance);
		}
	}
	Worker* worker = key_with_smallest_value(distances);
	if (worker != nullptr &&
		!worker->order()->building_position().isValid() &&
		check_proxy_builder_time(worker->unit()->getPosition(), proxy_position, arrive_frame)) {
		worker->wait_at_proxy_location(proxy_position);
	}
}

bool WorkerManager::check_proxy_builder_time(Position worker_position,Position proxy_position,int arrive_frame)
{
	int current_frame = Broodwar->getFrameCount();
	if (current_frame >= arrive_frame) return true;
	
	int distance = ground_distance(worker_position, proxy_position);
	if (distance < 0) return false;
	
	int travel_frames = int(1.15 * distance / UnitTypes::Protoss_Probe.topSpeed());
	return current_frame + travel_frames >= arrive_frame;
}

void WorkerManager::defend_building(Unit building_unit,int max_defending_workers)
{
	Unitset unit_set = Broodwar->getUnitsInRadius(building_unit->getPosition(), 320);
	int defending_workers = 0;
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (unit_set.count(worker.unit()) > 0 &&
			worker.order()->is_defending()) defending_workers++;
	}
	std::vector<Worker*> available_workers;
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (unit_set.count(worker.unit()) > 0 &&
			!worker.order()->is_defending() &&
			worker.order()->is_pullable()) {
			available_workers.push_back(&worker);
		}
	}
	for (auto& worker : available_workers) {
		if (defending_workers >= max_defending_workers) break;
		worker->defend_building(building_unit);
		defending_workers++;
	}
}

void WorkerManager::combat(int worker_count)
{
	for (auto& entry : worker_map_) {
		if (worker_count <= 0) return;
		Worker& worker = entry.second;
		if (worker.order()->is_combat()) {
			worker_count--;
		}
	}
	for (auto& entry : worker_map_) {
		if (worker_count <= 0) return;
		Worker& worker = entry.second;
		if (worker.order()->is_idle()) {
			worker.combat();
			worker_count--;
		}
	}
	for (auto& entry : worker_map_) {
		if (worker_count <= 0) return;
		Worker& worker = entry.second;
		if (worker.order()->is_pullable()) {
			worker.combat();
			worker_count--;
		}
	}
}

void WorkerManager::combat(int worker_count,Position position)
{
	if (!position.isValid()) {
		combat(worker_count);
		return;
	}
	
	for (auto& entry : worker_map_) {
		if (worker_count <= 0) return;
		Worker& worker = entry.second;
		if (worker.order()->is_combat()) {
			worker_count--;
		}
	}
	
	key_value_vector<Worker*,int> distances;
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->is_pullable()) {
			int distance = ground_distance(position, worker.unit()->getPosition());
			if (distance >= 0) {
				distances.emplace_back(&worker, distance);
			}
		}
	}
	
	std::sort(distances.begin(), distances.end(), [](const auto& a,const auto &b){
		return a.second < b.second;
	});
	for (size_t i = 0; i < std::min(size_t(worker_count), distances.size()); i++) {
		distances[i].first->combat();
	}
}

void WorkerManager::stop_combat()
{
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->is_combat()) {
			worker.idle();
		}
	}
}

bool WorkerManager::is_combat(Unit worker)
{
	bool result = false;
	auto it = worker_map_.find(worker);
	if (it != worker_map_.end()) {
		result = it->second.order()->is_combat();
	}
	return result;
}

Worker* WorkerManager::pick_worker_for_task()
{
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->is_idle()) return &worker;
	}
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->is_pullable()) return &worker;
	}
	return nullptr;
}

Worker* WorkerManager::pick_worker_for_task(Position position)
{
	bool need_connectivity_check = building_placement_manager.has_active_wall();
	int component = connectivity_grid.component_for_position(position);
	key_value_vector<Worker*,int> distances;
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->is_pullable()) {
			bool connectivity_check = false;
			if (!need_connectivity_check) {
				connectivity_check = true;
			} else if (component == 0) {
				connectivity_check = true;
			} else {
				int worker_component = connectivity_grid.component_for_position(worker.unit()->getPosition());
				if (worker_component == 0 || component == worker_component) {
					connectivity_check = true;
				}
			}
			if (connectivity_check) {
				int distance = ground_distance(position, worker.unit()->getPosition());
				if (distance >= 0) distances.emplace_back(&worker, distance);
			}
		}
	}
	return key_with_smallest_value(distances);
}

Worker* WorkerManager::pick_worker_for_task(UnitType unit_type,FastTilePosition tile_position)
{
	key_value_vector<Worker*,int> distances;
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->is_pullable()) {
			int worker_component = connectivity_grid.component_for_position(worker.unit()->getPosition());
			if (worker_component == 0 || connectivity_grid.building_has_component(unit_type, tile_position, worker_component)) {
				int distance = ground_distance(center_position_for(unit_type, tile_position), worker.unit()->getPosition());
				if (distance >= 0) distances.emplace_back(&worker, distance);
			}
		}
	}
	return key_with_smallest_value(distances);
}

void WorkerManager::apply_worker_orders()
{
	update_worker_gather_orders();
	
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		worker.apply_orders();
	}
}

void WorkerManager::draw_for_workers()
{
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		worker.draw();
	}
}

bool WorkerManager::should_scout_map_center() {
	if (!scout_map_center_) {
		return false;
	}
	FastTilePosition center = FastPosition(bwem_map.Center());
	const auto can_build_at = [](FastTilePosition tile_position){
		for (int dy = 0; dy < UnitTypes::Terran_Barracks.tileHeight(); dy++) {
			for (int dx = 0; dx < UnitTypes::Terran_Barracks.tileWidth(); dx++) {
				FastTilePosition delta(dx, dy);
				if (!Broodwar->isBuildable(tile_position + delta)) {
					return false;
				}
			}
		}
		return true;
	};
	for (int dy = -8; dy <= 8; dy++) {
		for (int dx = -8; dx <= 8; dx++) {
			FastTilePosition delta(dx, dy);
			if (!Broodwar->isExplored(center + delta) && can_build_at(center + delta)) {
				return true;
			}
		}
	}
	return false;
}

void WorkerManager::onUnitLost(Unit unit)
{
	if (worker_map_.count(unit) > 0) {
		const WorkerOrder* order = worker_map_.at(unit).order();
		if (order->is_scouting()) {
			initial_scout_death_position_ = unit->getPosition();
		}
		if (order->building_type() != UnitTypes::Zerg_Extractor) {
			lost_worker_count_++;
			for (auto &base : base_state.controlled_bases()) {
				Position center = center_position_for(Broodwar->self()->getRace().getResourceDepot(), base->Location());
				int distance = unit->getPosition().getApproxDistance(center);
				if (distance <= 256) {
					base_unsafe_until_frame_[base] = Broodwar->getFrameCount() + 96;
					if (order->gather_target() != nullptr && order->gather_target()->getDistance(unit) <= 256) {
						base_unsafe_until_frame_[base] = Broodwar->getFrameCount() + 96;
					}
				}
			}
			if (unit->getType() == UnitTypes::Zerg_Drone) {
				for (auto& entry : worker_map_) {
					if (entry.first != unit &&
						entry.second.order()->gather_target() != nullptr &&
						unit->getPosition().getApproxDistance(entry.first->getPosition()) <= 64) {
						entry.second.flee();
					}
				}
			}
		}
		worker_map_.erase(unit);
	}
}

void WorkerManager::onUnitMorph(Unit unit)
{
	if (worker_map_.count(unit) > 0 && !unit->getType().isWorker()) {
		worker_map_.erase(unit);
	}
}

MineralGas WorkerManager::worker_reserved_mineral_gas() const
{
	MineralGas result;
	for (auto& entry : worker_map_) {
		const Worker& worker = entry.second;
		result += worker.order()->reserved_resources();
	}
	return result;
}

bool WorkerManager::is_worker_mining_allowed_from_base(const BWEM::Base* base,Unit worker_unit) const
{
	return (base_unsafe_until_frame_.count(base) == 0 ||
			worker_unit->getDistance(base->Center()) <= 320);
}

void WorkerManager::cancel_building_construction(TilePosition building_position)
{
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->building_position() == building_position) {
			worker.idle();
		}
	}
}

void WorkerManager::cancel_building_construction_for_type(UnitType building_type)
{
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->building_type() == building_type) {
			worker.idle();
		}
	}
}

void WorkerManager::cancel_extra_planned_building_construction_for_type(UnitType building_type,int keep_count)
{
	key_value_vector<Worker*,int> workers;
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->building_type() == building_type && worker.order()->building() == nullptr) {
			workers.emplace_back(&worker, 0);
		}
	}
	
	if (int(workers.size()) > keep_count) {
		int cancel_count = int(workers.size()) - std::max(0, keep_count);
		
		for (auto& entry : workers) {
			Worker* worker = entry.first;
			entry.second = calculate_distance(worker->unit()->getType(),
											   worker->unit()->getPosition(),
											   worker->order()->building_type(),
											   center_position(worker->order()->building_position()));
		}
		
		struct Order
		{
			inline bool operator()(const std::pair<Worker*,int>& p1,const std::pair<Worker*,int>& p2)
			{
				return std::make_tuple(p1.second, p1.first->unit()->getID()) > std::make_tuple(p2.second, p2.first->unit()->getID());
			}
		};
		std::sort(workers.begin(), workers.end(), Order());
		
		for (int i = 0; i < cancel_count; i++) {
			workers[i].first->idle();
		}
	}
}

void WorkerManager::cancel_expansion_hatcheries()
{
	for (auto& entry : worker_map_) {
		Worker& worker = entry.second;
		if (worker.order()->building_type() == UnitTypes::Zerg_Hatchery &&
			worker.order()->building() == nullptr &&
			base_state.base_for_tile_position(worker.order()->building_position()) != nullptr) {
			worker.idle();
		}
	}
}

bool WorkerManager::is_dangerous_worker(Unit enemy_unit)
{
	const EnemyCluster* cluster = tactics_manager.cluster_for_unit(enemy_unit);
	if (cluster == nullptr) return false;
	return std::none_of(cluster->front_units().begin(), cluster->front_units().end(), [cluster,enemy_unit](Unit front_unit){
		return !front_unit->getType().isWorker() && (can_attack(front_unit, enemy_unit) || (front_unit->getType().canMove() && can_attack(front_unit, false)));
	});
}

bool WorkerManager::is_dangerous_unit(Unit enemy_unit)
{
	const EnemyCluster* cluster = tactics_manager.cluster_for_unit(enemy_unit);
	if (cluster == nullptr) return false;
	return std::none_of(cluster->front_units().begin(), cluster->front_units().end(), [cluster](Unit front_unit){
		return cluster->expect_win(front_unit);
	});
}

void WorkerManager::init_optimal_mining_data()
{
	char filename[512];
	FILE *f;
	sprintf_s(filename, sizeof(filename), "bwapi-data\\read\\OptimalMining_%s.txt", Broodwar->mapHash().c_str());
	int err = fopen_s(&f, filename, "r");
	if (err != 0) {
		sprintf_s(filename, sizeof(filename), "bwapi-data\\write\\OptimalMining_%s.txt", Broodwar->mapHash().c_str());
		err = fopen_s(&f, filename, "r");
	}
	if (err != 0) {
		sprintf_s(filename, sizeof(filename), "bwapi-data\\AI\\OptimalMining_%s.txt", Broodwar->mapHash().c_str());
		err = fopen_s(&f, filename, "r");
	}
	if (err == 0) {
		while (true) {
			int mineral_x;
			int mineral_y;
			int position_x;
			int position_y;
			int velocity_x;
			int velocity_y;
			int ret = fscanf(f, "%d,%d,%d,%d,%d,%d", &mineral_x, &mineral_y, &position_x, &position_y, &velocity_x, &velocity_y);
			if (ret == EOF) break;
			if (ret == 6) {
				FastTilePosition mineral_position(mineral_x, mineral_y);
				WorkerPositionAndVelocity value = {FastPosition(position_x, position_y), velocity_x, velocity_y};
				optimal_mining_map_[mineral_position].insert(value);
			}
		}
		fclose(f);
	}
}

void WorkerManager::store_optimal_mining_data()
{
	char filename[512];
	FILE *f;
	sprintf_s(filename, sizeof(filename), "bwapi-data\\write\\OptimalMining_%s.txt", Broodwar->mapHash().c_str());
	int err = fopen_s(&f, filename, "w");
	if (err == 0) {
		for (auto& entry : optimal_mining_map_) {
			const auto& mineral_position = entry.first;
			for (auto& value : entry.second) {
				fprintf(f, "%d,%d,%d,%d,%d,%d\n", mineral_position.x, mineral_position.y, value.position.x, value.position.y, value.velocity_x, value.velocity_y);
			}
		}
		fclose(f);
	}
}

#include "BananaBrain.h"

EnemyCluster::EnemyCluster(std::vector<const InformationUnit*>& units) : units_(units)
{
	calculate_engagement_distances();
	determine_friendly_units();
	determine_front();
	calculate_defense_supply();
	if (!mobile_detection_in_front()) {
		bool undetected_dark_templar_or_lurker = false;
		bool detected_dark_templar_or_lurker = false;
		
		for (auto& information_unit : units_) {
			UnitType type = information_unit->type;
			Unit unit = information_unit->unit;
			if (type == UnitTypes::Protoss_Dark_Templar &&
				unit->exists()) {
				if (!unit->isDetected()) {
					undetected_dark_templar_or_lurker = true;
				} else {
					detected_dark_templar_or_lurker = true;
				}
			}
			if (type == UnitTypes::Zerg_Lurker &&
				information_unit->burrowed) {
				if (!unit->exists() || !unit->isDetected()) {
					undetected_dark_templar_or_lurker = true;
				} else {
					detected_dark_templar_or_lurker = true;
				}
			}
		}
		
		if (undetected_dark_templar_or_lurker && !detected_dark_templar_or_lurker) {
			front_supply_ = 0.0;
			front_ground_supply_ = 0.0;
			front_ranged_ground_supply_ = 0.0;
		}
	}
	determine_push_through();
	determine_terrain_influence();
}

bool EnemyCluster::is_nearly_engaged() const
{
	return (!friendly_units_ascending_distance_.empty() &&
			friendly_units_ascending_distance_[0].second <= kEngagementDistance);
}

bool EnemyCluster::expect_win(double enemy_supply) const
{
	double front_supply = front_supply_;
	if (front_air_to_air_supply_ > defense_air_supply_) front_supply -= (front_air_to_air_supply_ - defense_air_supply_);
	return expect_win(enemy_supply, front_supply);
}

bool EnemyCluster::expect_win(double enemy_supply,double front_supply) const
{
	bool result;
	
	if (enemy_supply == 0.0) {
		result = true;
	} else {
		if (is_nearly_engaged()) {
			result = (front_supply > (4.0 / 5.0) * enemy_supply);
		} else {
			result = (front_supply > enemy_supply);
		}
	}
	
	return result;
}

bool EnemyCluster::expect_win(Unit unit) const
{
	bool result;
	if (push_through_) {
		result = true;
	} else if (unit->isFlying() && can_attack(unit, false) && expect_win(defense_air_attacking_supply_, front_air_to_ground_supply_)) {
		result = true;
	} else if (unit->getType() == UnitTypes::Zerg_Scourge) {
		result = expect_win(defense_air_attacking_supply_ - (2.0 / 3.0) * defense_air_attacking_air_supply_);
	} else if (!unit->isFlying() && expect_win(defense_supply_ - defense_air_attacking_air_supply_, front_ground_supply_)) {
		result = true;
	} else {
		result = expect_win(defense_supply_);
	}
	return result;
}

bool EnemyCluster::is_engaged(Unit unit) const
{
	bool result = false;
	auto it = friendly_units_distance_.find(unit);
	if (it != friendly_units_distance_.end() && it->second == 0) {
		result = true;
	}
	return result;
}

bool EnemyCluster::is_nearly_engaged(Unit unit) const
{
	bool result = false;
	auto it = friendly_units_distance_.find(unit);
	if (it != friendly_units_distance_.end() && it->second <= kEngagementDistance) {
		result = true;
	}
	return result;
}

bool EnemyCluster::in_front(Unit unit) const
{
	bool result = false;
	auto it = friendly_units_distance_.find(unit);
	if (it != friendly_units_distance_.end() && it->second < kFrontDistance) {
		result = true;
	}
	return result;
}

bool EnemyCluster::near_front(Unit unit) const
{
	bool result = false;
	auto it = friendly_units_distance_.find(unit);
	if (it != friendly_units_distance_.end() && it->second >= kFrontDistance - 32 && it->second < kFrontDistance) {
		result = true;
	}
	return result;
}

bool EnemyCluster::stim_allowed(Unit unit) const
{
	return (front_stim_allowed_ &&
			contains(front_units_, unit));
}

bool EnemyCluster::in_front_with_supply_at_least(Unit unit,int supply) const
{
	return in_front(unit) && front_supply_ >= supply;
}

void EnemyCluster::calculate_engagement_distances()
{
	int minimum_vanguard_engagement_distance = INT_MAX;
	
	for (auto unit : Broodwar->self()->getUnits()) {
		if (unit->isCompleted() && unit->isVisible() && !is_disabled(unit)) {
			Position unit_position = (unit->isLoaded() ? unit->getTransport()->getPosition() : unit->getPosition());
			bool unit_flying = (unit->isLoaded() ? unit->getTransport()->isFlying() : unit->isFlying());
			int min_engagement_distance = INT_MAX;
			for (auto& enemy_unit : units_) {
				if (!enemy_unit->position.isValid()) continue;
				int distance = calculate_distance(unit->getType(), unit_position, enemy_unit->type, enemy_unit->position);
				int enemy_unit_range = offense_max_range(enemy_unit->type, enemy_unit->player, unit_flying);
				if (enemy_unit_range >= 0) {
					int engagement_distance = std::max(0, distance - enemy_unit_range);
					min_engagement_distance = std::min(min_engagement_distance, engagement_distance);
					if (!unit_flying && !enemy_unit->flying && engagement_distance < minimum_vanguard_engagement_distance && can_attack(unit)) {
						minimum_vanguard_engagement_distance = engagement_distance;
						enemy_vanguard_ = enemy_unit->position;
					}
				}
			}
			if (min_engagement_distance != INT_MAX) {
				engagament_distance_[unit] = min_engagement_distance;
			}
		}
	}
}

void EnemyCluster::determine_friendly_units()
{
	for (auto unit : Broodwar->self()->getUnits()) {
		int engagement_distance = get_or_default(engagament_distance_, unit, INT_MAX);
		if (engagement_distance < INT_MAX) {
			friendly_units_ascending_distance_.push_back(std::make_pair(unit, engagement_distance));
			friendly_units_distance_[unit] = engagement_distance;
		}
	}
	std::sort(friendly_units_ascending_distance_.begin(), friendly_units_ascending_distance_.end(), [](const auto& a,const auto& b){
		return (std::make_tuple(a.second, a.first->getID()) <
				std::make_tuple(b.second, b.first->getID()));
	});
}

void EnemyCluster::determine_front()
{
	std::vector<std::pair<Unit,int>> units;
	for (auto& entry : friendly_units_ascending_distance_) {
		if (is_spellcaster(entry.first->getType()) ||
			entry.first->getType() == UnitTypes::Protoss_Observer ||
			entry.first->getType() == UnitTypes::Zerg_Overlord ||
			can_attack(entry.first)) {
			units.push_back(entry);
		}
	}
	
	size_t start = 0;
	while (start < units.size() && units[start].first->getType().isBuilding()) start++;
	if (start == units.size()) start = 0;
	
	size_t end = start;
	if (start < units.size()) {
		int first_distance = units[start].second;
		if (first_distance < kFrontDistance) {
			int max_stride = 64;
			for (size_t i = start + 1; i <= units.size(); i++) {
				int distance = units[i - 1].second;
				int stride = distance - first_distance;
				if (stride <= max_stride) end = i;
				max_stride = std::min(max_stride + 32, kMaxFrontStride);
			}
			while (start > 0 && units[start].second - units[start - 1].second <= 64) start--;
		}
	}
	
	double sum_supply = 0.0;
	double air_to_ground_supply = 0.0;
	double air_to_air_supply = 0.0;
	double ground_supply = 0.0;
	double ranged_ground_supply = 0.0;
	int healable_hitpoints = 0;
	int stimmable_missing_hitpoints = 0;
	int medic_energy = 0;
	int unstimmed_infantry_missing_hitpoints = 0;
	for (size_t i = start; i < end; i++) {
		Unit unit = units[i].first;
		if (!(unit->getType().isBuilding() && can_attack(unit)) ||
			contains(tactics_manager.included_static_defense(), unit)) {
			front_units_.insert(unit);
			double supply = unit->getType().supplyRequired() + TacticsManager::defense_supply_equivalent(unit);
			if (unit->getType() == UnitTypes::Protoss_Carrier) supply = TacticsManager::carrier_supply_equivalent(unit);
			double hp = 3.0 * unit->getHitPoints() + unit->getShields();
			double max_hp = 3.0 * unit->getType().maxHitPoints() + unit->getType().maxShields();
			supply *= (hp / max_hp);
			if (!unit->getType().isWorker() || worker_manager.is_combat(unit)) {
				sum_supply += supply;
				if (!unit->isFlying()) {
					ground_supply += supply;
				}
				if (!is_melee_or_worker(unit->getType()) && !unit->isFlying()) {
					ranged_ground_supply += supply;
				}
			}
			if (unit->isFlying()) {
				if (can_attack(unit, false)) {
					air_to_ground_supply += supply;
				} else if (can_attack(unit, true)) {
					air_to_air_supply += supply;
				}
			}
			if (unit->getType() == UnitTypes::Terran_Medic) {
				healable_hitpoints += (2 * unit->getEnergy());
			}
			if (is_stimmable(unit->getType())) {
				unstimmed_infantry_missing_hitpoints += (unit->getType().maxHitPoints() - unit->getHitPoints());
				if (!unit->isStimmed() &&
					unit->getHitPoints() >= 20) {
					unstimmed_infantry_missing_hitpoints += 10;
				}
			}
		}
	}
	front_supply_ = sum_supply;
	front_air_to_ground_supply_ = air_to_ground_supply;
	front_air_to_air_supply_ = air_to_air_supply;
	front_ground_supply_ = ground_supply;
	front_ranged_ground_supply_ = ranged_ground_supply;
	front_stim_allowed_ = (healable_hitpoints > stimmable_missing_hitpoints);
}

void EnemyCluster::determine_terrain_influence()
{
	if (defense_ranged_ground_supply_ > 0.0 &&
		front_ranged_ground_supply_ > 0.0) {
		FastPosition sum(0, 0);
		int count = 0;
		for (auto& unit : front_units_) {
			UnitType type = unit->getType();
			if (!type.isWorker() && !is_melee(type) && !type.isFlyer()) {
				sum += unit->getPosition();
				count++;
			}
		}
		FastPosition front_centroid = sum / count;
		if (front_centroid.isValid() && !has_area(FastWalkPosition(front_centroid))) {
			FastPosition closest_position;
			int closest_distance = INT_MAX;
			for (auto& unit : front_units_) {
				UnitType type = unit->getType();
				if (!type.isWorker() && !is_melee(type) && !type.isFlyer()) {
					int distance = unit->getDistance(front_centroid);
					if (distance < closest_distance) {
						closest_position = unit->getPosition();
						closest_distance = distance;
					}
				}
			}
			front_centroid = closest_position;
		}
		FastPosition front_far = Positions::None;
		if (enemy_vanguard_.isValid() && has_area(FastWalkPosition(enemy_vanguard_))) {
			int front_far_distance = INT_MAX;
			for (auto& unit : front_units_) {
				UnitType type = unit->getType();
				if (!type.isWorker() && !is_melee(type) && !type.isFlyer()) {
					int distance = unit->getDistance(enemy_vanguard_);
					if (distance < front_far_distance) {
						front_far = unit->getPosition();
						front_far_distance = distance;
					}
				}
			}
		}
		if (front_centroid.isValid() &&
			enemy_vanguard_.isValid() &&
			has_area(FastWalkPosition(front_centroid)) &&
			has_area(FastWalkPosition(enemy_vanguard_))) {
			int elevation_difference = ground_height(front_centroid) - ground_height(enemy_vanguard_);
			const auto is_narrow_choke = [](FastPosition a,FastPosition b){
				if (ground_height(a) > ground_height(b)) return false;
				int distance = -1;
				const BWEM::CPPath& path = bwem_map.GetPath(a, b, &distance);
				return (distance >= 0 && std::any_of(path.begin(), path.end(), [](auto& cp){ return chokepoint_width(cp) <= 96; }));
			};
			bool narrow_choke = is_narrow_choke(front_centroid, enemy_vanguard_);
			bool narrow_choke_far = (front_far.isValid() && is_narrow_choke(front_far, enemy_vanguard_));
			
			double front_fraction = double(front_ranged_ground_supply_) / double(front_supply_);
			double defense_fraction = double(defense_ranged_ground_supply_) / double(defense_supply_);
			double fraction = front_fraction * defense_fraction;
			double factor = 1.0;
			
			if (narrow_choke || narrow_choke_far) {
				factor *= clamp(0.5, 1.0 - (front_ranged_ground_supply_ - 4.0) / 16.0, 1.0);
			}
			if (elevation_difference > 0) {
				factor *= 1.5;
			} else if (elevation_difference < 0) {
				factor /= 2.0;
			}
			
			if (factor > 1.0) {
				front_supply_ = augment_supply(front_supply_, factor, fraction);
			} else if (factor < 1.0) {
				defense_supply_ = augment_supply(defense_supply_, 1.0 / factor, fraction);
			}
			
			// @
			if (configuration.draw_enabled()) {
				Broodwar->drawLineMap(front_centroid, enemy_vanguard_, factor < 1.0 ? Colors::Red : factor > 1.0 ? Colors::Green : Colors::White);
				FastPosition center_position = (front_centroid + enemy_vanguard_) / 2;
				Broodwar->drawTextMap(center_position, "%.2f", factor);
			}
			// /@
		} else {
			// @
			if (configuration.draw_enabled() && front_centroid.isValid() && enemy_vanguard_.isValid()) {
				Broodwar->drawLineMap(front_centroid, enemy_vanguard_, Colors::Purple);
				FastPosition center_position = (front_centroid + enemy_vanguard_) / 2;
				draw_cross_map(center_position, 10, Colors::Purple);
			}
			// /@
		}
	}
}

double EnemyCluster::augment_supply(double supply,double factor,double fraction)
{
	return (1.0 - fraction) * supply + fraction * factor * supply;
}

void EnemyCluster::calculate_defense_supply()
{
	bool runby_possible = micro_manager.runby_possible(front_units_);
	double defense_supply = 0.0;
	double defense_ranged_ground_supply = 0.0;
	double defense_air_attacking_supply = 0.0;
	double defense_air_attacking_air_supply = 0.0;
	double defense_air_supply = 0.0;
	for (auto& enemy_unit : units_) {
		if (enemy_unit->is_completed() && enemy_unit->type.maxHitPoints() > 0) {
			double supply = enemy_unit->type.supplyRequired() + TacticsManager::defense_supply_equivalent(*enemy_unit, runby_possible);
			double hp = 3.0 * enemy_unit->expected_hitpoints() + enemy_unit->expected_shields();
			double max_hp = 3.0 * enemy_unit->type.maxHitPoints() + enemy_unit->type.maxShields();
			supply *= (hp / max_hp);
			if (!enemy_unit->type.isWorker()) {
				defense_supply += supply;
				if (!is_melee(enemy_unit->type) && !enemy_unit->flying) {
					defense_ranged_ground_supply += supply;
				}
			}
			if (can_attack(enemy_unit->type, true)) {
				defense_air_attacking_supply += supply;
				if (enemy_unit->flying) {
					defense_air_attacking_air_supply += supply;
				}
			}
			if (enemy_unit->flying) {
				defense_air_supply += supply;
			}
		}
	}
	defense_supply_ = defense_supply;
	defense_ranged_ground_supply_ = defense_ranged_ground_supply;
	defense_air_attacking_supply_ = defense_air_attacking_supply;
	defense_air_attacking_air_supply_ = defense_air_attacking_air_supply;
	defense_air_supply_ = defense_air_supply;
}

void EnemyCluster::determine_push_through()
{
	std::vector<Unit> sieged_tanks;
	int tank_half_range = (UnitTypes::Terran_Siege_Tank_Siege_Mode.groundWeapon().minRange() +
						   UnitTypes::Terran_Siege_Tank_Siege_Mode.groundWeapon().maxRange()) / 2;
	for (auto information_unit : units_) {
		if (information_unit->unit->exists() &&
			information_unit->type == UnitTypes::Terran_Siege_Tank_Siege_Mode &&
			!information_unit->is_disabled()) {
			sieged_tanks.push_back(information_unit->unit);
		}
	}
	
	for (auto [unit,distance] : friendly_units_distance_) {
		if (distance < kEngagementDistance) {
			if (unit->getType() == UnitTypes::Zerg_Zergling || unit->getType() == UnitTypes::Protoss_Zealot) {
				bool needs_push_through = std::any_of(units_.begin(), units_.end(), [unit](auto information_unit) {
					bool result = false;
					Unit enemy_unit = information_unit->unit;
					if (enemy_unit->exists() &&
						!is_melee(enemy_unit->getType()) &&
						can_attack(enemy_unit, unit) &&
						can_attack_in_range(unit, enemy_unit, 32) &&
						unit->getPlayer()->topSpeed(unit->getType()) > 1.1 * enemy_unit->getPlayer()->topSpeed(enemy_unit->getType())) {
						result = true;
					}
					return result;
				});
				if (needs_push_through) {
					push_through_ = true;
					break;
				}
			} else if (can_attack(unit, false) &&
					   !is_melee_or_worker(unit->getType())) {
				bool needs_push_through = std::any_of(sieged_tanks.begin(), sieged_tanks.end(), [unit,tank_half_range](auto enemy_unit){
					int distance = calculate_distance(unit->getType(), unit->getPosition(), enemy_unit->getType(), enemy_unit->getPosition());
					return (distance <= tank_half_range);
				});
				if (needs_push_through) {
					push_through_ = true;
					break;
				}
			}
		}
	}
	
	if (opponent_model.enemy_opening() == EnemyOpening::P_CannonRush) {
		for (auto unit : front_units_) {
			if (unit->getType() == UnitTypes::Protoss_Reaver || is_siege_tank(unit->getType())) {
				bool no_invalid_present = true;
				bool cannon_present = false;
				for (auto enemy_unit : units_) {
					UnitType type = enemy_unit->type;
					if (type != UnitTypes::Protoss_Photon_Cannon &&
						!type.isWorker() &&
						type != UnitTypes::Protoss_Zealot &&
						can_attack(type, false)) {
						no_invalid_present = false;
						break;
					}
					if (type == UnitTypes::Protoss_Photon_Cannon) cannon_present = true;
				}
				if (no_invalid_present && cannon_present) {
					push_through_ = true;
				}
				break;
			}
		}
	}
}

bool EnemyCluster::mobile_detection_in_front()
{
	for (Unit unit : front_units_) {
		UnitType type = unit->getType();
		if (type == UnitTypes::Protoss_Observer ||
			type == UnitTypes::Terran_Science_Vessel ||
			type == UnitTypes::Zerg_Overlord) {
			return true;
		}
	}
	return false;
}

void TacticsManager::update()
{
	update_enemy_base();
	update_supply();
	update_enemy_supply();
	update_included_static_defense();
	update_clusters();
}

void TacticsManager::draw()
{
	int cluster_index = 0;
	for (auto& cluster : clusters_) {
		cluster_index++;
		FastPosition center_position(0, 0);
		for (auto& enemy_unit : cluster.units_) {
			Broodwar->drawTextMap(enemy_unit->position + Position(0, 10), "C%d", cluster_index);
			center_position += enemy_unit->position;
		}
		center_position /= cluster.units_.size();
		Broodwar->drawBoxMap(center_position - FastPosition(1, 1), center_position + FastPosition(41, 41), Colors::White, false);
		Broodwar->drawBoxMap(center_position, center_position + FastPosition(40, 40), Colors::Black, true);
		Broodwar->drawTextMap(center_position, "C%d\n%c%.2f\n%c%.2f",
							  cluster_index,
							  Text::Red, cluster.defense_supply_ * 0.5,
							  Text::Green, cluster.front_supply_ * 0.5);
		Broodwar->drawCircleMap(cluster.enemy_vanguard_, 10, Colors::Red, true);
	}
	
	Position position = enemy_start_position();
	if (position.isValid()) Broodwar->drawCircleMap(position, 20, Colors::White, true);
	if (enemy_natural_base_ != nullptr) draw_filled_diamond_map(enemy_natural_base_->Center(), 20, Colors::White);
}

void TacticsManager::update_enemy_base()
{
	if (enemy_start_base_ == nullptr) {
		for (auto& unit : Broodwar->enemies().getUnits()) {
			if (unit->getType().isResourceDepot()) {
				const BWEM::Base* base = base_state.base_for_tile_position(unit->getTilePosition());
				if (base != nullptr && base->Starting()) {
					enemy_start_base_ = base;
					enemy_start_base_found_at_frame_ = Broodwar->getFrameCount();
					enemy_natural_base_ = base_state.natural_base_for_start_base(base);
				}
			}
		}
	}
}

void TacticsManager::update_supply()
{
	int worker_supply = 0;
	int army_supply = 0;
	int defense_supply = 0;
	int anti_air_supply = 0;
	for (auto& unit : Broodwar->self()->getUnits()) {
		if (unit->isCompleted() && !is_disabled(unit) && !is_inside_bunker(unit)) {
			int supply = unit->getType().supplyRequired();
			if (unit->getType() == UnitTypes::Protoss_Carrier) supply = carrier_supply_equivalent(unit);
			if (unit->getType().isWorker()) worker_supply += supply; else army_supply += supply;
			int supply_defense_equivalent = defense_supply_equivalent(unit->getType());
			defense_supply += supply_defense_equivalent;
			if (can_attack(unit, true)) {
				anti_air_supply += (supply + supply_defense_equivalent);
			}
		}
	}
	worker_supply_ = worker_supply;
	army_supply_ = army_supply;
	defense_supply_ = army_supply + defense_supply;
	anti_air_supply_ = anti_air_supply;
}

void TacticsManager::update_enemy_supply()
{
	struct EnemySupplyValues
	{
		int worker_supply = 0;
		int army_supply = 0;
		int air_supply = 0;
		int defense_supply = 0;
		int offense_worker_supply = 0;
		int offense_army_supply = 0;
		int offense_air_supply = 0;
		
		void add(const InformationUnit* enemy_unit,int defense_supply_equivalent)
		{
			int supply = enemy_unit->type.supplyRequired();
			if (enemy_unit->type.isWorker()) worker_supply += supply; else army_supply += supply;
			if (enemy_unit->type.isFlyer()) air_supply += supply;
			if (enemy_unit->base_distance <= 320) {
				if (enemy_unit->type.isWorker()) offense_worker_supply += supply; else offense_army_supply += supply;
				if (enemy_unit->type.isFlyer()) offense_air_supply += supply;
			}
			defense_supply += defense_supply_equivalent;
		}
		
		void combine(const EnemySupplyValues& o)
		{
			worker_supply = std::max(worker_supply, o.worker_supply);
			army_supply = std::max(army_supply, o.army_supply);
			air_supply = std::max(air_supply, o.air_supply);
			defense_supply = std::max(defense_supply, o.defense_supply);
			offense_worker_supply = std::max(offense_worker_supply, o.offense_worker_supply);
			offense_army_supply = std::max(offense_army_supply, o.offense_army_supply);
			offense_air_supply  = std::max(offense_air_supply, o.offense_air_supply);
		}
	};
	
	bool runby_possible = micro_manager.runby_possible();
	EnemySupplyValues values;
	if (!is_ffa_) {
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->is_completed() && !enemy_unit->is_disabled()) {
				values.add(enemy_unit, defense_supply_equivalent(*enemy_unit, runby_possible));
			}
		}
	} else {
		std::map<Player,EnemySupplyValues> value_map;
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->is_completed() && !enemy_unit->is_disabled()) {
				value_map[enemy_unit->player].add(enemy_unit, defense_supply_equivalent(*enemy_unit, runby_possible));
			}
		}
		for (auto& entry : value_map) {
			values.combine(entry.second);
		}
	}
	enemy_worker_supply_ = values.worker_supply;
	enemy_army_supply_ = values.army_supply;
	enemy_defense_supply_ = values.army_supply + values.defense_supply;
	enemy_air_supply_ = values.air_supply;
	enemy_offense_supply_ = values.offense_army_supply + std::max(0, values.offense_worker_supply - 2);
	enemy_offense_air_supply_ = values.offense_air_supply;
	max_enemy_army_supply_ = std::max(max_enemy_army_supply_, enemy_army_supply_);
}

void TacticsManager::update_included_static_defense()
{
	std::set<Unit> original_included_static_defense;
	std::swap(included_static_defense_, original_included_static_defense);
	
	for (auto& information_unit : information_manager.my_units()) {
		if (information_unit->type.isBuilding() &&
			can_attack(information_unit->unit)) {
			Unit unit = information_unit->unit;
			int margin = contains(original_included_static_defense, unit) ? 0 : -32;
			bool in_range = false;
			for (auto& enemy_unit : information_manager.enemy_units()) {
				if (enemy_unit->unit->exists() &&
					can_attack_in_range(unit, enemy_unit->unit, margin)) {
					in_range = true;
					break;
				}
			}
			if (in_range) {
				included_static_defense_.insert(unit);
			}
		}
	}
}

void TacticsManager::update_clusters()
{
	clusters_.clear();
	cluster_map_.clear();
	
	std::set<const InformationUnit*> unassigned;
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->position.isValid() && !enemy_unit->is_stasised()) {
			unassigned.insert(enemy_unit);
		}
	}
	
	while (!unassigned.empty()) {
		const InformationUnit* first = *(unassigned.begin());
		unassigned.erase(first);
		
		std::vector<const InformationUnit*> cluster_units;
		std::set<const InformationUnit*> todo;
		todo.insert(first);
		unassigned.erase(first);
		cluster_units.push_back(first);
		
		while (!todo.empty()) {
			const InformationUnit* current = *(todo.begin());
			todo.erase(current);
			
			size_t cluster_units_original_size = cluster_units.size();
			for (auto& candidate : unassigned) {
				if (current->position.getApproxDistance(candidate->position) <= kClusterDistance) {
					todo.insert(candidate);
					cluster_units.push_back(candidate);
				}
			}
			for (size_t i = cluster_units_original_size; i < cluster_units.size(); i++) {
				const InformationUnit* candidate = cluster_units[i];
				unassigned.erase(candidate);
			}
		}
		
		clusters_.emplace_back(cluster_units);
	}
	
	for (auto& cluster : clusters_) {
		for (auto& enemy_unit : cluster.units()) {
			cluster_map_[enemy_unit->unit] = &cluster;
		}
	}
}

EnemyCluster* TacticsManager::cluster_at_position(Position position)
{
	int min_distance = INT_MAX;
	EnemyCluster* result = nullptr;
	
	for (auto& cluster : clusters_) {
		for (auto& enemy_unit : cluster.units_) {
			int distance = position.getApproxDistance(enemy_unit->position);
			if (distance < min_distance) {
				min_distance = distance;
				result = &cluster;
			}
		}
	}
	
	return (min_distance <= kClusterDistance) ? result : nullptr;
}

int TacticsManager::defense_supply_equivalent(Unit unit)
{
	int result;
	if (unit->getType() == UnitTypes::Terran_Bunker) {
		result = 4 + 2 * information_manager.bunker_marines_loaded(unit);
	} else {
		result = defense_supply_equivalent(unit->getType());
	}
	return result;
}

int TacticsManager::defense_supply_equivalent(const InformationUnit& information_unit,bool runby_possible)
{
	int result;
	if (information_unit.type == UnitTypes::Terran_Bunker) {
		result = (runby_possible ? 0 : 4) + 2 * information_manager.bunker_marines_loaded(information_unit.unit);
	} else {
		result = defense_supply_equivalent(information_unit.type, runby_possible);
	}
	return result;
}

int TacticsManager::defense_supply_equivalent(UnitType type,bool runby_possible)
{
	int result;
	switch (type) {
		case UnitTypes::Protoss_Photon_Cannon:
			result = runby_possible ? 1 : 3;
			break;
		case UnitTypes::Zerg_Sunken_Colony:
			result = runby_possible ? 1 : 3;
			break;
		case UnitTypes::Terran_Bunker:
			result = runby_possible ? 2 : 6;
			break;
		default:
			result = 0;
			break;
	}
	return result * 2;
}

int TacticsManager::carrier_supply_equivalent(Unit unit)
{
	int interceptor_count = unit->getInterceptorCount();
	return (interceptor_count * 3) / 2;
}

Position TacticsManager::enemy_start_position() const
{
	if (enemy_start_base_ == nullptr) {
		Position position = deduce_enemy_start_position();
		if (position.isValid()) return position;
	} else {
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->type.isResourceDepot() && !enemy_unit->flying &&
				enemy_unit->tile_position() == enemy_start_base_->Location()) {
				return enemy_unit->position;
			}
		}
	}
	
	return Positions::None;
}

Position TacticsManager::enemy_start_drop_position() const
{
	Position position = Positions::None;
	const BWEM::Base* base = (enemy_start_base_ == nullptr) ? deduce_enemy_start_base() : enemy_start_base_;
	if (base != nullptr) {
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
			position = (mineral_center + base_edge_position) / 2;
		}
	}
	return position;
}

Position TacticsManager::enemy_base_attack_position(bool guess_start_location) const
{
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->type.isResourceDepot() && !enemy_unit->flying) return enemy_unit->position;
	}
	
	if (enemy_start_base_ == nullptr) {
		Position position = deduce_enemy_start_position();
		if (position.isValid()) return position;
	}
	
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->type.isBuilding() && !enemy_unit->flying) return enemy_unit->position;
	}
	
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->type.isBuilding()) return center_position(TilePosition(enemy_unit->position));
	}
	
	if (enemy_start_base_ == nullptr && guess_start_location) {
		for (auto& base : base_state.unexplored_start_bases()) {
			if (base_state.controlled_and_planned_bases().count(base) == 0) {
				return base->Center();
			}
		}
	}
	
	return Positions::Unknown;
}

std::vector<const BWEM::Base*> TacticsManager::possible_enemy_start_bases() const
{
	std::vector<const BWEM::Base*> result;
	
	if (enemy_start_base_ != nullptr) {
		result.push_back(enemy_start_base_);
	} else {
		for (auto& base : base_state.unexplored_start_bases()) {
			if (base_state.controlled_and_planned_bases().count(base) == 0) {
				result.push_back(base);
			}
		}
	}
	
	return result;
}

Position TacticsManager::deduce_enemy_start_position() const
{
	const BWEM::Base* base = deduce_enemy_start_base();
	return (base != nullptr) ? base->Center() : Positions::None;
}

const BWEM::Base* TacticsManager::deduce_enemy_start_base() const
{
	std::vector<const BWEM::Base*> start_bases;
	for (auto& base : base_state.bases()) {
		if (base->Starting()) {
			start_bases.push_back(base);
		}
	}
	
	std::set<const BWEM::Base*> candidate_bases;
	for (auto& base : base_state.unexplored_start_bases()) {
		if (!contains(base_state.controlled_and_planned_bases(), base)) {
			candidate_bases.insert(base);
		}
	}
	
	if (candidate_bases.empty()) return nullptr;
	if (candidate_bases.size() == 1) return *candidate_bases.begin();
	
	Position initial_scout_death_position = worker_manager.initial_scout_death_position();
	if (initial_scout_death_position.isValid()) {
		const BWEM::Base* base = smallest_priority(start_bases, [initial_scout_death_position](auto& base){
			return initial_scout_death_position.getApproxDistance(base->Center());
		});
		if (base != nullptr && contains(candidate_bases, base)) return base;
	}
	
	key_value_vector<const BWEM::Base*,int> distances;
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (enemy_unit->type.isResourceDepot() && !enemy_unit->flying) {
			Position position = enemy_unit->position;
			const BWEM::Base* base = smallest_priority(start_bases, [position](auto& base){
				return position.getApproxDistance(base->Center());
			});
			if (base != nullptr && contains(candidate_bases, base)) {
				distances.emplace_back(base, position.getApproxDistance(base->Center()));
			}
		}
	}
	if (distances.empty()) {
		for (auto& enemy_unit : information_manager.enemy_units()) {
			if (enemy_unit->type.isBuilding() && !enemy_unit->flying) {
				Position position = enemy_unit->position;
				const BWEM::Base* base = smallest_priority(start_bases, [position](auto& base){
					return position.getApproxDistance(base->Center());
				});
				if (base != nullptr && contains(candidate_bases, base)) {
					distances.emplace_back(base, position.getApproxDistance(base->Center()));
				}
			}
		}
	}
	return key_with_smallest_value(distances);
}

bool TacticsManager::mobile_detection_exists()
{
	return (training_manager.unit_count_completed(UnitTypes::Protoss_Observer) >= 1 ||
			training_manager.unit_count_completed(UnitTypes::Terran_Science_Vessel) >= 1 ||
			training_manager.unit_count_completed(UnitTypes::Zerg_Overlord) >= 1 ||
			(opponent_model.enemy_race() == Races::Terran &&
			 training_manager.unit_count_completed(UnitTypes::Terran_Comsat_Station) >= 1));
}

int TacticsManager::air_to_air_supply()
{
	return (UnitTypes::Protoss_Corsair.supplyRequired() * training_manager.unit_count_completed(UnitTypes::Protoss_Corsair) +
			UnitTypes::Terran_Valkyrie.supplyRequired() * training_manager.unit_count_completed(UnitTypes::Terran_Valkyrie) +
			UnitTypes::Zerg_Devourer.supplyRequired() * training_manager.unit_count_completed(UnitTypes::Zerg_Devourer));
}

bool TacticsManager::is_opponent_army_too_large()
{
	return is_opponent_army_too_large(spending_manager.training_cost_per_minute().supply);
}

bool TacticsManager::is_opponent_army_too_large(double build_supply_per_minute)
{
	struct IncomingUnits {
		std::vector<std::pair<double,int>> incoming_units;
		
		void add(InformationUnit* enemy_unit)
		{
			UnitType type = enemy_unit->type;
			if (type == UnitTypes::Terran_Siege_Tank_Siege_Mode) {
				type = UnitTypes::Terran_Siege_Tank_Tank_Mode;
			} else if (type == UnitTypes::Zerg_Lurker_Egg) {
				type = UnitTypes::Zerg_Lurker;
			}
			if (type.supplyRequired() > 0 && type.canMove()) {
				double frame = enemy_unit->base_distance / top_speed(type, enemy_unit->player);
				incoming_units.emplace_back(frame, type.supplyRequired());
			}
		}
		
		bool check(int army_supply,double build_supply_per_frame)
		{
			std::sort(incoming_units.begin(), incoming_units.end());
			int supply_sum = 0;
			for (auto& entry : incoming_units) {
				double frame = entry.first;
				supply_sum += entry.second;
				if (supply_sum >= 20) {
					double our_supply = army_supply + build_supply_per_frame * frame;
					if (supply_sum > our_supply) return true;
				}
			}
			return false;
		}
	};
	
	if (build_supply_per_minute <= 0.0) return false;
	double build_supply_per_frame = build_supply_per_minute / (60.0 * 24.0);
	
	bool result = false;
	
	if (!is_ffa_) {
		IncomingUnits incoming_units;
		for (auto& enemy_unit : information_manager.enemy_units()) {
			incoming_units.add(enemy_unit);
		}
		result = incoming_units.check(army_supply_, build_supply_per_frame);
	} else {
		std::map<Player,IncomingUnits> map;
		for (auto& enemy_unit : information_manager.enemy_units()) {
			map[enemy_unit->player].add(enemy_unit);
		}
		for (auto [player, incoming_units] : map) {
			if (incoming_units.check(army_supply_, build_supply_per_frame)) {
				result = true;
				break;
			}
		}
	}
	
	return result;
}

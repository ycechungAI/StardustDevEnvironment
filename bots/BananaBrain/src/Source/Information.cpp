#include "BananaBrain.h"

int InformationUnit::complete_frame() const
{
	if (!type.isBuilding()) {
		return start_frame;
	} else {
		return start_frame + type.buildTime() + building_extra_frames(type);
	}
}

bool InformationUnit::is_completed() const
{
	if (unit->exists()) {
		return unit->isCompleted();
	} else {
		return complete_frame() <= Broodwar->getFrameCount();
	}
}

bool InformationUnit::is_stasised() const
{
	if (unit->exists()) {
		return unit->isStasised();
	} else {
		return stasis_end_frame > Broodwar->getFrameCount();
	}
}

bool InformationUnit::is_disabled() const
{
	if (unit->exists()) {
		return ::is_disabled(unit);
	} else {
		return (stasis_end_frame > Broodwar->getFrameCount() ||
				lockdown_end_frame > Broodwar->getFrameCount() ||
				maelstrom_end_frame > Broodwar->getFrameCount());
	}
}

int InformationUnit::expected_hitpoints() const
{
	int result;
	if (unit->exists()) {
		result = hitpoints;
	} else {
		int frame_delta = Broodwar->getFrameCount() - frame;
		if (is_zerg_healing(type)) {
			result = std::min(hitpoints + (4 * frame_delta) / 256, type.maxHitPoints());
		} else if (type == UnitTypes::Terran_Bunker) {
			result = type.maxHitPoints();
		} else {
			result = hitpoints;
		}
	}
	return result;
}

int InformationUnit::expected_shields() const
{
	int result;
	if (unit->exists() || type.getRace() != Races::Protoss) {
		result = shields;
	} else {
		int frame_delta = Broodwar->getFrameCount() - frame;
		result = std::min(shields + (7 * frame_delta) / 256, type.maxShields());
	}
	return result;
}

TilePosition InformationUnit::tile_position() const
{
	return TilePosition(Position(position.x - type.tileWidth() * 16,
								 position.y - type.tileHeight() * 16));
}

int InformationUnit::detection_range() const
{
	int result = -1;
	if (type.isDetector() && !is_disabled()) {
		result = type.isBuilding() ? 7 * 32 : sight_range(type, player);
	}
	return result;
}

void InformationUnit::update(Unit a_unit)
{
	unit = a_unit;
	player = a_unit->getPlayer();
	if (first_frame == -1) first_frame = Broodwar->getFrameCount();
	if (start_frame == -1 || type != a_unit->getType()) start_frame = estimate_building_start_frame_based_on_hit_points(unit);
	frame = Broodwar->getFrameCount();
	type = a_unit->getType();
	position = a_unit->getPosition();
	flying = a_unit->isFlying();
	burrowed = a_unit->isBurrowed();
	stasis_end_frame = Broodwar->getFrameCount() + a_unit->getStasisTimer();
	lockdown_end_frame = Broodwar->getFrameCount() + a_unit->getLockdownTimer();
	maelstrom_end_frame = Broodwar->getFrameCount() + a_unit->getMaelstromTimer();
	hitpoints = a_unit->getHitPoints();
	shields = a_unit->getShields();
	area = area_at(position);
	base_distance = determine_base_distance();
}

int InformationUnit::determine_base_distance()
{
	int result = INT_MAX;
	
	if (area != nullptr && base_state.controlled_and_planned_areas().count(area) > 0) {
		result = 0;
	} else {
		for (auto& cp : base_state.border().chokepoints()) {
			auto [end1,end2] = chokepoint_ends(cp);
			int distance = point_to_line_segment_distance(center_position(end1), center_position(end2), position);
			result = std::min(result, distance);
		}
	}
	
	return result;
}

void InformationUnit::clean_outdated_unit_position()
{
	if (!type.isBuilding() && !is_current() && position.isValid() &&
		Broodwar->isVisible(TilePosition(position)) &&
		Broodwar->getFrameCount() >= burrowed_and_should_be_detected_frame) {
		position = Positions::Unknown;
		area = nullptr;
		base_distance = INT_MAX;
	}
}

InformationManager::MarineCountRingBuffer::MarineCountRingBuffer()
{
	values_.fill(0);
}

void InformationManager::MarineCountRingBuffer::push(int value)
{
	values_[counter_++] = value;
	if (counter_ == values_.size()) counter_ = 0;
}

int InformationManager::MarineCountRingBuffer::max() const
{
	return *std::max_element(values_.begin(), values_.end());
}

void InformationManager::MarineCountRingBuffer::decrement()
{
	for (auto& element : values_) if (element > 0) --element;
}

void InformationManager::MarineCountRingBuffer::increment()
{
	for (auto& element : values_) if (element < 4) ++element;
}

void InformationManager::mark_neutral_for_destruction(Unit unit,bool destroy)
{
	auto it = all_units_.find(unit);
	if (it != all_units_.end()) {
		it->second.destroy_neutral = destroy;
	}
}

void InformationManager::update_units_and_buildings()
{
	for (auto& unit : Broodwar->getAllUnits()) {
		if (unit->exists()) {
			std::pair<UnitType,Unit> type_and_unit(unit->getType(), unit);
			if (!contains(enemy_seen_by_type_, type_and_unit) && unit->getPlayer()->isEnemy(Broodwar->self())) {
				enemy_seen_by_type_.insert(type_and_unit);
				enemy_seen_count_[unit->getType()]++;
			}
			all_units_[unit].update(unit);
		}
	}
	clean_buildings_and_spells();
	update_burrowed_and_should_be_detected_frame();
	clean_outdated_unit_positions();
	
	my_units_.clear();
	neutral_units_.clear();
	enemy_units_.clear();
	for (auto& entry : all_units_) {
		Player player = entry.second.player;
		if (player == Broodwar->self()) {
			my_units_.push_back(&entry.second);
		} else if (player->isNeutral()) {
			if (entry.second.destroy_neutral) {
				enemy_units_.push_back(&entry.second);
			} else {
				neutral_units_.push_back(&entry.second);
			}
		} else if (player->isEnemy(Broodwar->self())) {
			enemy_units_.push_back(&entry.second);
		}
	}
}

void InformationManager::clean_buildings_and_spells()
{
	std::vector<Unit> units_to_remove;
	for (auto& entry : all_units_) {
		Unit unit = entry.first;
		InformationUnit& information_unit = entry.second;
		
		if (information_unit.type.isBuilding() && !unit->exists() &&
			building_visible(information_unit.type, information_unit.tile_position())) {
			units_to_remove.push_back(unit);
			if (information_unit.player->isNeutral()) bwem_handle_destroy_safe(unit);
		} else if (information_unit.type == UnitTypes::Spell_Scanner_Sweep &&
			Broodwar->getFrameCount() >= information_unit.first_frame + 262) {
			units_to_remove.push_back(unit);
		}
	}
	for (auto& unit : units_to_remove) {
		all_units_.erase(unit);
	}
}

void InformationManager::update_burrowed_and_should_be_detected_frame()
{
	for (auto it = all_units_.begin(); it != all_units_.end(); ++it) {
		InformationUnit& unit = (*it).second;
		if (unit.position.isValid() && unit.player->isEnemy(Broodwar->self()) && unit.burrowed) {
			std::vector<const InformationUnit*> detectors;
			for (auto entry : all_units_) {
				const InformationUnit& other_unit = entry.second;
				if (other_unit.player == Broodwar->self() && other_unit.type.isDetector()) detectors.push_back(&other_unit);
			}
			for (; it != all_units_.end(); ++it) {
				InformationUnit& unit = (*it).second;
				if (unit.position.isValid() && unit.player->isEnemy(Broodwar->self()) && unit.burrowed) {
					bool detected = std::any_of(detectors.begin(),
												detectors.end(),
												[&unit](auto detector){
													int range = detector->detection_range();
													int distance;
													if (range < 11 * 32) {
														distance = calculate_distance(detector->type, detector->position, unit.type, unit.position);
													} else {
														distance = detector->position.getApproxDistance(unit.position);
													}
													return distance <= range;
												});
					if (detected &&
						(unit.burrowed_and_should_be_detected_frame == -1 || unit.burrowed_and_should_be_detected_frame == INT_MAX)) {
						unit.burrowed_and_should_be_detected_frame = Broodwar->getFrameCount() + 60;
					} else if (!detected) {
						unit.burrowed_and_should_be_detected_frame = INT_MAX;
					}
				} else {
					unit.burrowed_and_should_be_detected_frame = -1;
				}
			}
			break;
		}
		unit.burrowed_and_should_be_detected_frame = -1;
	}
}

void InformationManager::clean_outdated_unit_positions()
{
	for (auto& entry : all_units_) {
		InformationUnit& information_unit = entry.second;
		information_unit.clean_outdated_unit_position();
	}
}

void InformationManager::update_information()
{
	update_enemy_count();
	update_upgrades();
	update_bullet_timestamps();
	update_bunker_marines_loaded();
	update_bunker_marine_range();
}

void InformationManager::update_upgrades()
{
	for (auto player : Broodwar->getPlayers()) {
		auto& upgrades = upgrades_[player];
		for (auto upgrade_type : UpgradeTypes::allUpgradeTypes()) {
			auto& level = upgrades[upgrade_type];
			level = std::max(level, player->getUpgradeLevel(upgrade_type));
		}
	}
}

void InformationManager::update_enemy_count()
{
	enemy_count_.clear();
	enemy_completed_count_.clear();
	for (auto& enemy_unit : enemy_units_) {
		enemy_count_[enemy_unit->type]++;
		if (enemy_unit->is_completed()) enemy_completed_count_[enemy_unit->type]++;
		enemy_seen_.insert(enemy_unit->type);
	}
}

void InformationManager::draw()
{
	for (auto& neutral_unit : neutral_units_) {
		UnitType type = neutral_unit->type;
		if (!type.isMineralField() && type != UnitTypes::Resource_Vespene_Geyser && !neutral_unit->is_current()) {
			TilePosition tile_position = neutral_unit->tile_position();
			Broodwar->drawBoxMap(tile_position.x * 32,
								 tile_position.y * 32,
								 (tile_position.x + type.tileWidth()) * 32,
								 (tile_position.y + type.tileHeight()) * 32,
								 Colors::Blue);
			Broodwar->drawTextMap(Position(tile_position), "%s", type.c_str());
		}
	}
	
	for (auto& enemy_unit : enemy_units_) {
		UnitType type = enemy_unit->type;
		if (type.isBuilding()) {
			if (!enemy_unit->is_current()) {
				TilePosition tile_position = enemy_unit->tile_position();
				Broodwar->drawBoxMap(tile_position.x * 32,
									 tile_position.y * 32,
									 (tile_position.x + type.tileWidth()) * 32,
									 (tile_position.y + type.tileHeight()) * 32,
									 Colors::Orange);
				Broodwar->drawTextMap(Position(tile_position), "%s", type.c_str());
			}
		} else {
			Broodwar->drawBoxMap(enemy_unit->position - Position(enemy_unit->type.dimensionLeft(), enemy_unit->type.dimensionUp()),
								 enemy_unit->position + Position(enemy_unit->type.dimensionRight(), enemy_unit->type.dimensionDown()),
								 Colors::Red);
			Broodwar->drawTextMap(enemy_unit->position, "%s", enemy_unit->type.c_str());
		}
	}
	
	for (auto& entry : bunker_marines_loaded_) {
		Broodwar->drawTextMap(entry.first->getPosition() - Position(30, 0), "%d", entry.second.max());
	}
}

void InformationManager::onUnitDestroy(Unit unit)
{
	all_units_.erase(unit);
}

void InformationManager::onUnitDiscover(Unit unit)
{
	if (unit->getType() == UnitTypes::Terran_Marine) {
		Position position = unit->getPosition();
		for (auto& entry : bunker_marines_loaded_) {
			Unit bunker = entry.first;
			MarineCountRingBuffer& count = entry.second;
			Rect bunker_rect(UnitTypes::Terran_Bunker, bunker->getPosition());
			Rect below_rect(bunker_rect.left - 10 - UnitTypes::Terran_Marine.dimensionRight(),
							bunker_rect.bottom,
							bunker_rect.right + 10 + UnitTypes::Terran_Marine.dimensionLeft(),
							bunker_rect.bottom + 10 + UnitTypes::Terran_Marine.dimensionUp());
			if (below_rect.is_inside(position)) {
				count.decrement();
				bunker_marines_delay_update_until_timestamp_[bunker] = Broodwar->getFrameCount() + WeaponTypes::Gauss_Rifle.damageCooldown() + 1;
			}
		}
	}
}

void InformationManager::onUnitEvade(Unit unit)
{
	if (all_units_.count(unit) > 0) {
		InformationUnit& information_unit = all_units_.at(unit);
		if (information_unit.player->isEnemy(Broodwar->self()) &&
			information_unit.type == UnitTypes::Terran_Marine) {
			Position position = information_unit.position;
			for (auto& entry : bunker_marines_loaded_) {
				Unit bunker = entry.first;
				MarineCountRingBuffer& count = entry.second;
				int distance = calculate_distance(UnitTypes::Terran_Bunker, bunker->getPosition(), UnitTypes::Terran_Marine, position);
				if (distance == 0) count.increment();
			}
		}
	}
}

int InformationManager::upgrade_level(Player player,UpgradeType upgrade_type) const
{
	int level = 0;
	
	if (upgrades_.count(player) > 0) {
		auto& upgrades = upgrades_.at(player);
		if (upgrades.count(upgrade_type) > 0) {
			return upgrades.at(upgrade_type);
		}
	}
	
	return level;
}

bool InformationManager::enemy_has_upgrade(UpgradeType upgrade_type) const
{
	for (auto& entry : upgrades_) {
		if (entry.first->isEnemy(Broodwar->self())) {
			auto& upgrades = entry.second;
			auto it = upgrades.find(upgrade_type);
			if (it != upgrades.end() && it->second > 0) {
				return true;
			}
		}
	}
	
	return false;
}

void InformationManager::update_bullet_timestamps()
{
	std::set<int> existing_ids;
	for (auto& bullet : Broodwar->getBullets()) {
		if (bullet_timestamps_.count(bullet->getID()) == 0) {
			bullet_timestamps_[bullet->getID()] = Broodwar->getFrameCount();
		}
		existing_ids.insert(bullet->getID());
	}
	
	std::vector<int> remove_ids;
	for (auto& entry : bullet_timestamps_) if (existing_ids.count(entry.first) == 0) remove_ids.push_back(entry.first);
	for (auto& id : remove_ids) bullet_timestamps_.erase(id);
}

void InformationManager::update_bunker_marines_loaded()
{
	std::vector<Unit> bunkers;
	std::map<Unit,int> bunker_marines_loaded;
	
	for (auto& enemy_unit : Broodwar->enemies().getUnits()) {
		if (enemy_unit->isCompleted() && enemy_unit->getType() == UnitTypes::Terran_Bunker) {
			bool bunker_can_attack_us = false;
			for (auto& unit : Broodwar->self()->getUnits()) {
				if (not_incomplete(unit) && not_cloaked(unit)) {
					int distance = calculate_distance(UnitTypes::Terran_Marine, enemy_unit->getPosition(), unit->getType(), unit->getPosition());
					int bunker_marine_range = weapon_max_range(WeaponTypes::Gauss_Rifle, enemy_unit->getPlayer()) + 64;
					if (distance <= bunker_marine_range) bunker_can_attack_us = true;
				}
			}
			if (bunker_can_attack_us) {
				bunkers.push_back(enemy_unit);
				bunker_marines_loaded[enemy_unit] = 0;
			}
		}
	}
	
	if (!bunkers.empty()) {
		for (auto& bullet : Broodwar->getBullets()) {
			if (bullet->getType() == BulletTypes::Gauss_Rifle_Hit) {
				int timestamp = bullet_timestamps_.at(bullet->getID());
				if (bullet->getSource() == nullptr &&
					bullet->getTarget() != nullptr &&
					bullet->getTarget()->getPlayer() == Broodwar->self() &&
					Broodwar->getFrameCount() - timestamp < WeaponTypes::Gauss_Rifle.damageCooldown()) {
					key_value_vector<Unit,int> distance_map;
					for (auto& bunker : bunkers) {
						int distance = calculate_distance(UnitTypes::Terran_Marine, bunker->getPosition(), bullet->getPosition());
						int bunker_marine_range = weapon_max_range(WeaponTypes::Gauss_Rifle, bunker->getPlayer()) + 64;
						if (distance <= bunker_marine_range && bunker_marines_loaded[bunker] < 4) {
							distance_map.emplace_back(bunker, distance);
						}
					}
					Unit bunker = key_with_smallest_value(distance_map);
					if (bunker != nullptr) bunker_marines_loaded[bunker]++;
				}
			}
		}
	}
	
	for (auto& entry : bunker_marines_loaded) {
		Unit unit = entry.first;
		if (bunker_marines_delay_update_until_timestamp_[unit] <= Broodwar->getFrameCount()) {
			int current_loaded = entry.second;
			bunker_marines_loaded_[unit].push(current_loaded);
		}
	}
	
	std::vector<Unit> remove_bunkers;
	for (auto& entry : bunker_marines_loaded_) if (!entry.first->exists()) remove_bunkers.push_back(entry.first);
	for (auto& bunker : remove_bunkers) bunker_marines_loaded_.erase(bunker);
}

void InformationManager::update_bunker_marine_range()
{
	for (auto& bullet : Broodwar->getBullets()) {
		if (bullet->getType() != BulletTypes::Gauss_Rifle_Hit) continue;
		int timestamp = bullet_timestamps_.at(bullet->getID());
		if (timestamp + 1 != Broodwar->getFrameCount()) continue;
		if (bullet->getSource() != nullptr || bullet->getTarget() == nullptr || !bullet->getTarget()->exists()) continue;
		Unit target = bullet->getTarget();
		key_value_vector<Unit,int> distance_map;
		for (auto& unit : Broodwar->enemies().getUnits()) {
			if (unit->isCompleted() && unit->getType() == UnitTypes::Terran_Bunker) {
				int distance = calculate_distance(UnitTypes::Terran_Marine, unit->getPosition(), bullet->getPosition());
				distance_map.emplace_back(unit, distance);
			}
		}
		Unit bunker = key_with_smallest_value(distance_map);
		if (bunker != nullptr) {
			int distance = calculate_distance(UnitTypes::Terran_Marine, bunker->getPosition(), bullet->getPosition());
			if (distance >= 214 && distance < 246) {
				if (configuration.draw_enabled() &&
					upgrades_[bunker->getPlayer()][UpgradeTypes::U_238_Shells] == 0) Broodwar << "Detected U238 (bunker marine range)" << std::endl;
				upgrades_[bunker->getPlayer()][UpgradeTypes::U_238_Shells] = 1;
			}
		}
	}
}

bool InformationManager::enemy_building_seen() const
{
	return std::any_of(enemy_units_.begin(), enemy_units_.end(), [](auto enemy_unit) {
		return enemy_unit->type.isBuilding() && !enemy_unit->flying;
	});
}

int InformationManager::bunker_marines_loaded(Unit bunker) const
{
	int result = 4;
	if (bunker->getPlayer() == Broodwar->self()) {
		result = int(bunker->getLoadedUnits().size());
	} else {
		auto it = bunker_marines_loaded_.find(bunker);
		if (it != bunker_marines_loaded_.end()) result = it->second.max();
	}
	return result;
}

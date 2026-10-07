#include "BananaBrain.h"

void WalkabilityGrid::init()
{
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			FastTilePosition tile_position(x, y);
			if (tile_fully_walkable(tile_position)) terrain_walkable_[tile_position] = true;
		}
	}
}

bool WalkabilityGrid::tile_fully_walkable(FastTilePosition tile_position)
{
	FastWalkPosition walk_position(tile_position);
	for (int dy = 0; dy < 4; dy++) {
		for (int dx = 0; dx < 4; dx++) {
			if (!Broodwar->isWalkable(walk_position + FastWalkPosition(dx, dy))) return false;
		}
	}
	return true;
}

void WalkabilityGrid::update()
{
	walkable_ = terrain_walkable_;
	
	for (auto& entry : information_manager.all_units()) {
		const InformationUnit& information_unit = entry.second;
		if (information_unit.type.isBuilding() && !information_unit.flying) {
			FastTilePosition tile_position(information_unit.tile_position());
			for (int dy = 0; dy < information_unit.type.tileHeight(); dy++) {
				for (int dx = 0; dx < information_unit.type.tileWidth(); dx++) {
					walkable_[tile_position + FastTilePosition(dx, dy)] = false;
				}
			}
		}
	}
	
	for (auto& entry : worker_manager.worker_map()) {
		const Worker& worker = entry.second;
		const WorkerOrder* order = worker.order();
		UnitType unit_type = order->building_type();
		if (unit_type != UnitTypes::None &&
			order->building() == nullptr) {
			FastTilePosition tile_position = order->building_position();
			for (int dy = -1; dy < unit_type.tileHeight() + 1; dy++) {
				for (int dx = -1; dx < unit_type.tileWidth() + 1; dx++) {
					walkable_[tile_position + FastTilePosition(dx, dy)] = false;
				}
			}
		}
	}
}

FastPosition WalkabilityGrid::walkable_tile_near(FastPosition position,int range) const
{
	FastTilePosition tile_position(position);
	if (is_walkable(tile_position)) return position;
	FastPosition result;
	int result_distance = range + 1;
	int range_tiles = (range + 31) / 32;
	for (int y = tile_position.y - range_tiles; y <= tile_position.y + range_tiles; y++) {
		for (int x = tile_position.x - range_tiles; x <= tile_position.x + range_tiles; x++) {
			FastTilePosition current_tile(x, y);
			if (is_walkable(current_tile)) {
				FastPosition current = center_position(current_tile);
				int distance = current.getApproxDistance(position);
				if (current.getApproxDistance(position) < result_distance) {
					result = current;
					result_distance = distance;
				}
			}
		}
	}
	return result;
}

void ConnectivityGrid::update()
{
	if (valid_) return;
	
	component_.clear();
	int component_counter = 1;
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			FastTilePosition tile_position(x, y);
			if (!walkability_grid.is_walkable(tile_position) || component_[tile_position] > 0) continue;
			
			int component = component_counter++;
			std::queue<FastTilePosition> queue;
			queue.push(tile_position);
			component_[tile_position] = component;
			
			while (!queue.empty()) {
				FastTilePosition tile_position = queue.front();
				queue.pop();
				
				for (auto& delta : { FastTilePosition(-1, 0), FastTilePosition(1, 0), FastTilePosition(0, -1), FastTilePosition(0, 1) }) {
					FastTilePosition new_tile_position = tile_position + delta;
					if (walkability_grid.is_walkable(new_tile_position) && component_[new_tile_position] == 0) {
						queue.push(new_tile_position);
						component_[new_tile_position] = component;
					}
				}
			}
		}
	}
	
	valid_ = true;
}

std::pair<int,FastTilePosition> ConnectivityGrid::component_and_tile_for_position(FastPosition position)
{
	FastTilePosition tile_position(position);
	if (component_.get(tile_position) > 0) return std::make_pair(component_[tile_position], tile_position);
	
	int component = 0;
	int smallest_distance = INT_MAX;
	FastTilePosition result_tile_position = tile_position;
	for (auto& delta : { FastTilePosition(-1, -1), FastTilePosition(0, -1), FastTilePosition(1, -1), FastTilePosition(-1, 0), FastTilePosition(1, 0), FastTilePosition(-1, 1), FastTilePosition(0, 1), FastTilePosition(1, 1) }) {
		FastTilePosition other_tile_position = tile_position + delta;
		int other_component = component_.get(other_tile_position);
		if (other_component != 0) {
			int distance = position.getApproxDistance(center_position(other_tile_position));
			if (distance < smallest_distance) {
				component = other_component;
				smallest_distance = distance;
				result_tile_position = other_tile_position;
			}
		}
	}
	
	return std::make_pair(component, result_tile_position);
}

bool ConnectivityGrid::check_reachability(Unit combat_unit,Unit enemy_unit)
{
	bool result = true;
	
	if (is_melee(combat_unit->getType())) {
		int component = component_for_position(combat_unit->getPosition());
		result = check_reachability_melee(component, enemy_unit);
	} else if (!combat_unit->isFlying()) {
		int component = component_for_position(combat_unit->getPosition());
		int range = weapon_max_range(combat_unit, enemy_unit->isFlying());
		result = check_reachability_ranged(component, range, enemy_unit);
	}
	
	return result;
}

bool ConnectivityGrid::check_reachability_melee(int component,Unit enemy_unit)
{
	if (component == 0) return true;
	
	UnitType unit_type = enemy_unit->getType();
	bool result;
	if (unit_type.isBuilding()) {
		result = building_has_component(unit_type, enemy_unit->getTilePosition(), component);
	} else {
		result = (component_for_position(enemy_unit->getPosition()) == component);
	}
	return result;
}

bool ConnectivityGrid::check_reachability_ranged(int component,int range,Unit enemy_unit)
{
	if (component == 0) return true;
	
	UnitType unit_type = enemy_unit->getType();
	if (!unit_type.isBuilding() && component_for_position(enemy_unit->getPosition()) == component) return true;
	
	int left = enemy_unit->getPosition().x - unit_type.dimensionLeft() - range;
	int top = enemy_unit->getPosition().y - unit_type.dimensionUp() - range;
	int right = enemy_unit->getPosition().x + unit_type.dimensionRight() + range;
	int bottom = enemy_unit->getPosition().y + unit_type.dimensionDown() + range;
	
	FastTilePosition top_left(FastPosition(left, top));
	FastTilePosition bottom_right(FastPosition(right, bottom));
	
	for (int y = top_left.y; y <= bottom_right.y; y++) {
		for (int x = top_left.x; x <= bottom_right.x; x++) {
			FastTilePosition tile_position(x, y);
			if (component_.get(tile_position) == component) {
				FastPosition position = center_position(tile_position);
				int distance = enemy_unit->getDistance(position);
				if (distance <= range) return true;
			}
		}
	}
	
	return false;
}

bool ConnectivityGrid::building_has_component(UnitType unit_type,FastTilePosition tile_position,int component)
{
	int left = tile_position.x - 1;
	int right = tile_position.x + unit_type.tileWidth();
	int top = tile_position.y - 1;
	int bottom = tile_position.y + unit_type.tileHeight();
	
	for (int x = left; x <= right; x++) {
		if (component_.get(FastTilePosition(x, top)) == component) return true;
		if (component_.get(FastTilePosition(x, bottom)) == component) return true;
	}
	for (int y = top; y <= bottom; y++) {
		if (component_.get(FastTilePosition(left, y)) == component) return true;
		if (component_.get(FastTilePosition(right, y)) == component) return true;
	}
	return false;
}

std::set<int> ConnectivityGrid::building_components(UnitType unit_type,FastTilePosition tile_position)
{
	std::set<int> result;
	building_components(result, unit_type, tile_position);
	return result;
}

void ConnectivityGrid::building_components(std::set<int>& result,UnitType unit_type,FastTilePosition tile_position)
{
	int left = tile_position.x - 1;
	int right = tile_position.x + unit_type.tileWidth();
	int top = tile_position.y - 1;
	int bottom = tile_position.y + unit_type.tileHeight();
	const auto add_component_at_position = [&](int x,int y){
		int component = component_.get(FastTilePosition(x, y));
		if (component != 0) result.insert(component);
	};
	for (int x = left; x <= right; x++) {
		add_component_at_position(x, top);
		add_component_at_position(x, bottom);
	}
	for (int y = top; y <= bottom; y++) {
		add_component_at_position(left, y);
		add_component_at_position(right, y);
	}
}

bool ConnectivityGrid::is_wall_building(Unit unit)
{
	UnitType unit_type = unit->getType();
	if (!unit_type.isBuilding()) return false;
	
	int left = unit->getTilePosition().x - 1;
	int right = unit->getTilePosition().x + unit_type.tileWidth();
	int top = unit->getTilePosition().y - 1;
	int bottom = unit->getTilePosition().y + unit_type.tileHeight();
	
	int component_seen = 0;
	
	for (int x = left; x <= right; x++) {
		int component;
		
		component = component_.get(FastTilePosition(x, top));
		if (component != 0) {
			if (component_seen == 0) component_seen = component; else if (component != component_seen) return true;
		}
		
		component = component_.get(FastTilePosition(x, bottom));
		if (component != 0) {
			if (component_seen == 0) component_seen = component; else if (component != component_seen) return true;
		}
	}
	
	for (int y = top; y <= bottom; y++) {
		int component;
		
		component = component_.get(FastTilePosition(left, y));
		if (component != 0) {
			if (component_seen == 0) component_seen = component; else if (component != component_seen) return true;
		}
		
		component = component_.get(FastTilePosition(right, y));
		if (component != 0) {
			if (component_seen == 0) component_seen = component; else if (component != component_seen) return true;
		}
	}
	
	return false;
}

int ConnectivityGrid::wall_building_perimeter(Unit unit,int component)
{
	UnitType unit_type = unit->getType();
	if (!unit_type.isBuilding()) return false;
	
	int left = unit->getTilePosition().x - 1;
	int right = unit->getTilePosition().x + unit_type.tileWidth();
	int top = unit->getTilePosition().y - 1;
	int bottom = unit->getTilePosition().y + unit_type.tileHeight();
	
	int count = 0;
	
	for (int x = left; x <= right; x++) {
		if (component_.get(FastTilePosition(x, top)) == component) count++;
		if (component_.get(FastTilePosition(x, bottom)) == component) count++;
	}
	
	for (int y = top + 1; y <= bottom - 1; y++) {
		if (component_.get(FastTilePosition(left, y)) == component) count++;
		if (component_.get(FastTilePosition(right, y)) == component) count++;
	}
	
	return count;
}

void ThreatGrid::update()
{
	ground_threat_.clear();
	air_threat_.clear();
	ground_threat_excluding_workers_.clear();
	ground_splash_distance_.clear();
	
	TileGrid<bool> detected;
	for (auto& enemy_unit : information_manager.enemy_units()) {
		if (!enemy_unit->type.isBuilding() || enemy_unit->is_completed()) {
			apply_weapon(ground_threat_, *enemy_unit, false);
			apply_weapon(air_threat_, *enemy_unit, true);
			if (!enemy_unit->type.isWorker()) apply_weapon(ground_threat_excluding_workers_, *enemy_unit, false);
			apply_detection(detected, *enemy_unit);
		}
		WeaponType ground_weapon = (enemy_unit->type == UnitTypes::Protoss_Reaver) ? WeaponTypes::Scarab : enemy_unit->type.groundWeapon();
		if (ground_weapon.explosionType() == ExplosionTypes::Radial_Splash ||
			ground_weapon.explosionType() == ExplosionTypes::Enemy_Splash) {
			apply_splash(ground_splash_distance_, *enemy_unit, ground_weapon);
		}
	}
	
	calculate_component([this](FastTilePosition tile_position){
		return !walkability_grid.is_walkable(tile_position) || ground_threat_[tile_position];
	}, ground_component_.component_grid());
	calculate_component([this](FastTilePosition tile_position){
		return air_threat_[tile_position];
	}, air_component_.component_grid());
	calculate_component([this,&detected](FastTilePosition tile_position){
		return air_threat_[tile_position] && detected[tile_position];
	}, cloaked_air_threat_component_.component_grid());
	calculate_component([this](FastTilePosition tile_position){
		for (auto delta : { FastTilePosition(-1, -1), FastTilePosition(0, -1), FastTilePosition(-1, 0), FastTilePosition(0, 0) }) {
			if (air_threat_.get(tile_position + delta)) return true;
		}
		return false;
	}, air_cross_component_.component_grid());
}

const ThreatGrid::ThreatComponentGrid& ThreatGrid::component_grid(UnitType type) const
{
	if (type == UnitTypes::Protoss_Observer) {
		return cloaked_air_threat_component_;
	} else if (type.isFlyer()) {
		if (type.width() > 32 && type.height() > 32) {
			return air_cross_component_;
		} else {
			return air_component_;
		}
	} else {
		return ground_component_;
	}
}

int ThreatGrid::CrossThreatComponentGrid::component(FastTilePosition tile_position) const
{
	if (tile_position.x == Broodwar->mapWidth()) {
		tile_position.x = Broodwar->mapWidth() - 1;
	}
	if (tile_position.y == Broodwar->mapHeight()) {
		tile_position.y = Broodwar->mapHeight() - 1;
	}
	return component_grid_.get(tile_position);
}

void ThreatGrid::apply_weapon(TileGrid<bool>& grid,const InformationUnit& information_unit,bool air)
{
	int range = weapon_max_range(information_unit.type, information_unit.player, air);
	apply(grid, information_unit, range);
}

void ThreatGrid::apply_detection(TileGrid<bool>& grid,const InformationUnit& information_unit)
{
	int range = information_unit.detection_range();
	apply(grid, information_unit, range);
}

void ThreatGrid::apply(TileGrid<bool>& grid,const InformationUnit& information_unit,int range)
{
	if (range >= 0) {
		if (information_unit.type.canMove()) range += (24 + 1); else range += (8 + 1);
		FastPosition position = information_unit.position;
		UnitType type = information_unit.type;
		FastTilePosition top_left(position - FastPosition(range + type.dimensionLeft(), range + type.dimensionUp()));
		FastTilePosition bottom_right(position + FastPosition(range + type.dimensionRight(), range + type.dimensionDown()));
		top_left.makeValid();
		bottom_right.makeValid();
		
		for (int y = top_left.y; y <= bottom_right.y; y++) {
			for (int x = top_left.x; x <= bottom_right.x; x++) {
				FastTilePosition candidate_tile_position(x, y);
				FastPosition candidate_position(candidate_tile_position);
				FastPosition closest_position(clamp(candidate_position.x, position.x, candidate_position.x + 31),
											  clamp(candidate_position.y, position.y, candidate_position.y + 31));
				int distance = calculate_distance(type, position, closest_position);
				if (distance < range) {
					grid[candidate_tile_position] = true;
				}
			}
		}
	}
}

void ThreatGrid::apply_splash(TileGrid<int>& grid,const InformationUnit& information_unit,WeaponType weapon_type)
{
	int range = weapon_max_range(information_unit.type, information_unit.player, false);
	int min_range = weapon_min_range(information_unit.type);
	int splash_range = weapon_type.outerSplashRadius();
	if (range >= 0) {
		if (information_unit.type.canMove()) range += (24 + 1); else range += (8 + 1);
		FastPosition position = information_unit.position;
		UnitType type = information_unit.type;
		FastTilePosition top_left(position - FastPosition(range + type.dimensionLeft(), range + type.dimensionUp()));
		FastTilePosition bottom_right(position + FastPosition(range + type.dimensionRight(), range + type.dimensionDown()));
		top_left.makeValid();
		bottom_right.makeValid();
		
		for (int y = top_left.y; y <= bottom_right.y; y++) {
			for (int x = top_left.x; x <= bottom_right.x; x++) {
				FastTilePosition candidate_tile_position(x, y);
				FastPosition candidate_position(candidate_tile_position);
				FastPosition closest_position(clamp(candidate_position.x, position.x, candidate_position.x + 31),
											  clamp(candidate_position.y, position.y, candidate_position.y + 31));
				int distance = calculate_distance(type, position, closest_position);
				if (distance <= range && distance >= min_range) {
					int& grid_tile = grid[candidate_tile_position];
					grid_tile = std::max(grid_tile, splash_range);
				}
			}
		}
	}
}

void ThreatGrid::draw(bool air) const
{
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			FastTilePosition tile_position(x, y);
			if (threat(tile_position, air)) {
				FastPosition position(tile_position);
				Broodwar->drawLineMap(position, position + FastPosition(31, 31), Colors::Red);
				Broodwar->drawLineMap(position + FastPosition(0, 31), position + FastPosition(31, 0), Colors::Red);
			}
		}
	}
}

UnitGrid::UnitGrid()
{
	for (int y = 0; y < 256; y++) {
		for (int x = 0; x < 256; x++) {
			unit_collision_grid_[y * 256 + x].reserve(4);
		}
	}
}

void UnitGrid::update()
{
	for (int y = 0; y < Broodwar->mapHeight(); y++) {
		for (int x = 0; x < Broodwar->mapWidth(); x++) {
			unit_collision_grid_[y * 256 + x].clear();
		}
	}
	building_grid_.clear();
	
	for (auto& entry : information_manager.all_units()) {
		const InformationUnit* information_unit = &entry.second;
		Unit unit = information_unit->unit;
		if (information_unit->position.isValid() &&
			!(unit->exists() && unit->isLoaded()) &&
			!information_unit->flying &&
			!information_unit->burrowed) {
			int left = information_unit->left();
			int top = information_unit->top();
			int right = information_unit->right();
			int bottom = information_unit->bottom();
			FastTilePosition top_left(FastPosition(left, top));
			FastTilePosition bottom_right(FastPosition(right, bottom));
			for (int y = top_left.y; y <= bottom_right.y; y++) {
				for (int x = top_left.x; x <= bottom_right.x; x++) {
					unit_collision_grid_[y * 256 + x].push_back(information_unit);
				}
			}
			if (information_unit->type.isBuilding()) {
				FastTilePosition building_tile_position = information_unit->tile_position();
				for (int dy = 0; dy < information_unit->type.tileHeight(); dy++) {
					for (int dx = 0; dx < information_unit->type.tileWidth(); dx++) {
						FastTilePosition tile_position = building_tile_position + FastTilePosition(dx, dy);
						building_grid_[tile_position] = information_unit;
					}
				}
			}
		}
	}
}

bool UnitGrid::is_friendly_building_in_tile(UnitType type,FastTilePosition tile_position) const
{
	const InformationUnit* information_unit = building_in_tile(tile_position);
	return (information_unit != nullptr &&
			information_unit->player == Broodwar->self() &&
			information_unit->type == type);
}

bool UnitGrid::is_completed_friendly_building_in_tile(UnitType type,FastTilePosition tile_position) const
{
	const InformationUnit* information_unit = building_in_tile(tile_position);
	return (information_unit != nullptr &&
			information_unit->player == Broodwar->self() &&
			information_unit->type == type &&
			information_unit->is_completed());
}

void RoomGrid::init()
{
	int width = 4 * Broodwar->mapWidth();
	int height = 4 * Broodwar->mapHeight();
	
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			room_[FastWalkPosition(x, y)] = INT_MAX;
		}
	}
	
	for (int x = 0; x < width; x++) {
		int y = 0;
		while (y < height) {
			while (y < height && !Broodwar->isWalkable(x, y)) y++;
			while (y < height) {
				int ystart = y;
				int value = distance_to_terrain(FastWalkPosition(x, y));
				y++;
				while (y < height && Broodwar->isWalkable(x, y)) {
					int new_value = distance_to_terrain(FastWalkPosition(x, y));
					if (new_value > value) {
						value = new_value;
					} else if (new_value < value) {
						break;
					}
					y++;
				}
				int prev_value = distance_to_terrain(FastWalkPosition(x, y));
				y++;
				while (y < height && Broodwar->isWalkable(x, y)) {
					int new_value = distance_to_terrain(FastWalkPosition(x, y));
					if (new_value > prev_value) break;
					y++;
					prev_value = new_value;
				}
				for (int y1 = ystart; y1 < y; y1++) room_[FastWalkPosition(x, y1)] = 2 * value;
			}
		}
	}
	
	for (int y = 0; y < height; y++) {
		int x = 0;
		while (x < width) {
			while (x < width && !Broodwar->isWalkable(x, y)) x++;
			while (x < width) {
				int xstart = x;
				int value = distance_to_terrain(FastWalkPosition(x, y));
				x++;
				while (x < width && Broodwar->isWalkable(x, y)) {
					int new_value = distance_to_terrain(FastWalkPosition(x, y));
					if (new_value > value) {
						value = new_value;
					} else if (new_value < value) {
						break;
					}
					x++;
				}
				int prev_value = distance_to_terrain(FastWalkPosition(x, y));
				x++;
				while (x < width && Broodwar->isWalkable(x, y)) {
					int new_value = distance_to_terrain(FastWalkPosition(x, y));
					if (new_value > prev_value) break;
					x++;
					prev_value = new_value;
				}
				for (int x1 = xstart; x1 < x; x1++) {
					int& i = room_[FastWalkPosition(x1, y)];
					i = std::min(i, 2 * value);
				}
			}
		}
	}
}

void RoomGrid::draw() const
{
	for (int y = 0; y < 4 * Broodwar->mapHeight(); y++) {
		for (int x = 0; x < 4 * Broodwar->mapWidth(); x++) {
			FastWalkPosition walk_position(x, y);
			int d = room_[walk_position] / 32;
			Color color = Colors::Black;
			switch (d) {
				case 0: color = BWAPI::Colors::Grey;   break;
				case 1: color = BWAPI::Colors::Brown;  break;
				case 2: color = BWAPI::Colors::Purple; break;
				case 3: color = BWAPI::Colors::Blue;   break;
				case 4: color = BWAPI::Colors::Green;  break;
				case 5: color = BWAPI::Colors::Orange; break;
				case 6: color = BWAPI::Colors::Yellow; break;
				case 7: color = BWAPI::Colors::White;  break;
			}
			Broodwar->drawCircleMap(center_position(walk_position), 1, color, true);
		}
	}
}

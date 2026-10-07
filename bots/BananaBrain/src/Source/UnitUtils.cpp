#include "BananaBrain.h"

std::string frame_to_string(int frame)
{
	constexpr double fps = 1000. / 42.0;
	int gameSeconds = int(frame / fps);
	std::stringstream stream;
	stream << (gameSeconds / 60) << ':';
	stream.width(2);
	stream.fill('0');
	stream << (gameSeconds % 60);
	return stream.str();
}

std::default_random_engine& random_generator()
{
	static std::unique_ptr<std::default_random_engine> generator_ptr;
	if (!generator_ptr) {
		auto seed = std::chrono::system_clock::now().time_since_epoch().count();
		generator_ptr.reset(new std::default_random_engine((unsigned int)seed));
	}
	return *generator_ptr.get();
}

void draw_diamond_map(Position position,int size,Color color)
{
	Broodwar->drawLineMap(position - Position(size, 0), position - Position(0, size), color);
	Broodwar->drawLineMap(position - Position(0, size), position + Position(size, 0), color);
	Broodwar->drawLineMap(position + Position(size, 0), position + Position(0, size), color);
	Broodwar->drawLineMap(position + Position(0, size), position - Position(size, 0), color);
}

void draw_filled_diamond_map(Position position,int size,Color color)
{
	Broodwar->drawTriangleMap(position - Position(size, 0), position - Position(0, size), position + Position(size, 0), color, true);
	Broodwar->drawTriangleMap(position - Position(size, 0), position + Position(0, size), position + Position(size, 0), color, true);
}

void draw_cross_map(Position position,int size,Color color)
{
	Broodwar->drawLineMap(position - Position(size, size), position + Position(size, size), color);
	Broodwar->drawLineMap(position - Position(size, -size), position + Position(size, -size), color);
}

bool building_visible(UnitType building_type,TilePosition tile_position)
{
	for (int y = tile_position.y; y < tile_position.y + building_type.tileHeight(); y++) {
		for (int x = tile_position.x; x < tile_position.x + building_type.tileWidth(); x++) {
			TilePosition current_tile_position{x, y};
			if (current_tile_position.isValid() && Broodwar->isVisible(current_tile_position)) return true;
		}
	}
	return false;
}

bool building_explored(UnitType building_type,TilePosition tile_position)
{
	for (int y = tile_position.y; y < tile_position.y + building_type.tileHeight(); y++) {
		for (int x = tile_position.x; x < tile_position.x + building_type.tileWidth(); x++) {
			TilePosition current_tile_position{x, y};
			if (current_tile_position.isValid() && Broodwar->isExplored(current_tile_position)) return true;
		}
	}
	return false;
}

const BWEM::Area* area_at(FastPosition position)
{
	return area_at(FastWalkPosition(position));
}

const BWEM::Area* area_at(FastWalkPosition walk_position)
{
	const BWEM::Area* area = bwem_map.GetArea(walk_position);
	if (area != nullptr) return area;
	
	for (FastWalkPosition delta : { FastWalkPosition{1, 0}, FastWalkPosition{-1, 0}, FastWalkPosition{0, -1}, FastWalkPosition{0, 1},
		FastWalkPosition{1, 1}, FastWalkPosition{-1, 1}, FastWalkPosition{-1, -1}, FastWalkPosition{1, -1} }) {
		FastWalkPosition other_position = walk_position + delta;
		if (other_position.isValid()) {
			area = bwem_map.GetArea(other_position);
			if (area != nullptr) return area;
		}
	}
	
	return nullptr;
}

bool has_area(FastWalkPosition walk_position)
{
	return area_at(walk_position) != nullptr;
}

bool resources_explored(const BWEM::Base* base)
{
	for (auto& mineral : base->Minerals()) {
		if (!building_explored(mineral->Type(), mineral->TopLeft())) return false;
	}
	for (auto& geyser : base->Geysers()) {
		if (!building_explored(geyser->Type(), geyser->TopLeft())) return false;
	}
	return true;
}

bool walkable_line(FastWalkPosition a,FastWalkPosition b)
{
	int w = std::abs(a.x - b.x);
	int h = std::abs(a.y - b.y);
	bool result = true;
	
	if (w >= h) {
		if (a.x > b.x) std::swap(a, b);
		for (int x = a.x; result && x <= b.x; x++) {
			double t = double(x - a.x) / double(b.x - a.x);
			FastWalkPosition walk_position(x, int(a.y + t * (b.y - a.y) + 0.5));
			if (!Broodwar->isWalkable(walk_position)) result = false;
		}
	} else {
		if (a.y > b.y) std::swap(a, b);
		for (int y = a.y; result && y <= b.y; y++) {
			double t = double(y - a.y) / double(b.y - a.y);
			FastWalkPosition walk_position(int(a.x + t * (b.x - a.x) + 0.5), y);
			if (!Broodwar->isWalkable(walk_position)) result = false;
		}
	}
	
	return result;
}

std::unordered_set<FastTilePosition> creep_spread(UnitType type,FastPosition position)
{
	std::unordered_set<FastTilePosition> result;
	
	if (type.producesCreep()) {
		auto const neighbour_set = [&](FastTilePosition tile_position){
			for (TilePosition delta : { TilePosition(-1, -1), TilePosition(0, -1), TilePosition(1, -1),
				TilePosition(-1, 0), TilePosition(1, 0),
				TilePosition(-1, 1), TilePosition(0, 1), TilePosition(1, 1) }) {
				if (contains(result, tile_position + delta)) return true;
			}
			return false;
		};
		
		FastTilePosition tile_position = FastTilePosition(FastPosition(position.x - type.tileWidth() * 16,
																	   position.y - type.tileHeight() * 16));
		for (int dy = 0; dy < type.tileHeight(); dy++) {
			for (int dx = 0; dx < type.tileWidth(); dx++) {
				result.insert(FastTilePosition(tile_position.x + dx, tile_position.y + dy));
			}
		}
		
		int xt_min = std::max((position.x - 320) / 32, 0);
		int xt_max = std::min((position.x + 320) / 32, Broodwar->mapWidth() - 1);
		int yt_min = std::max((position.y - 200) / 32, 0);
		int yt_max = std::min((position.y + 200) / 32, Broodwar->mapHeight() - 2);
		
		bool changed;
		do {
			changed = false;
			int dy = yt_min * 32 - position.y + 16;
			for (int y = yt_min; y <= yt_max; y++, dy += 32) {
				int dx = xt_min * 32 - position.x + 16;
				for (int x = xt_min; x <= xt_max; x++, dx += 32) {
					FastTilePosition current(x, y);
					if (!contains(result, current) &&
						Broodwar->isBuildable(current) &&
						Broodwar->isBuildable(current + FastTilePosition(0, 1)) &&
						dx * dx * 25 + dy * dy * 64 <= 320 * 320 * 25 &&
						neighbour_set(current)) {
						result.insert(current);
						changed = true;
					}
				}
			}
		} while (changed);
	}
	
	return result;
}

Position center_position_for(UnitType unit_type,TilePosition tile_position)
{
	Position top_left_position(tile_position);
	return Position(top_left_position.x + unit_type.tileWidth() * 16,
					top_left_position.y + unit_type.tileHeight() * 16);
}

Position center_position(WalkPosition walk_position)
{
	return Position(walk_position) + Position(4, 4);
}

Position center_position(TilePosition tile_position)
{
	return Position(tile_position) + Position(16, 16);
}

int max_unit_dimension(UnitType type)
{
	return std::max(std::max(type.dimensionLeft(), type.dimensionRight()),
					std::max(type.dimensionUp(), type.dimensionDown()));
}

int euclidian_remainder(int a,int b)
{
	int r = a % b;
	return (r >= 0) ? r : (r + std::abs(b));
}

Position lever(Position a,Position b,double t)
{
	double s = 1.0 - t;
	double x = s * a.x + t * b.x;
	double y = s * a.y + t * b.y;
	return Position(int(x + 0.5), int(y + 0.5));
}

Position scale_line_segment(Position a,Position b,int requested_length)
{
	return lever(a, b, double(requested_length) / a.getDistance(b));
}

int line_segment_length_within_circle(FastPosition a,FastPosition b,FastPosition center,int radius)
{
	return line_segment_length_within_circle(a - center, b - center, radius);
}

int line_segment_length_within_circle(FastPosition a,FastPosition b,int radius)
{
	double dx = b.x - a.x;
	double dy = b.y - a.y;
	double dr2 = dx * dx + dy * dy;
	double D = a.x * b.y - b.x * a.y;
	double delta = radius * radius * dr2 - D * D;
	int result = 0;
	if (delta > 0) {
		int sgn_dy = dy < 0 ? -1 : 1;
		double delta_sqrt = std::sqrt(double(delta));
		double x1 = (D * dy + sgn_dy * dx * delta_sqrt) / dr2;
		double y1 = (-D * dx + std::abs(dy) * delta_sqrt) / dr2;
		double x2 = (D * dy - sgn_dy * dx * delta_sqrt) / dr2;
		double y2 = (-D * dx - std::abs(dy) * delta_sqrt) / dr2;
		double t1;
		double t2;
		if (std::abs(dx) > std::abs(dy)) {
			t1 = (x1 - a.x) / dx;
			t2 = (x2 - a.x) / dx;
		} else {
			t1 = (y1 - a.y) / dy;
			t2 = (y2 - a.y) / dy;
		}
		t1 = clamp(0.0, t1, 1.0);
		t2 = clamp(0.0, t2, 1.0);
		x1 = (1.0 - t1) * a.x + t1 * b.x;
		y1 = (1.0 - t1) * a.y + t1 * b.y;
		x2 = (1.0 - t2) * a.x + t2 * b.x;
		y2 = (1.0 - t2) * a.y + t2 * b.y;
		double xx = x1 - x2;
		double yy = y1 - y2;
		double length = std::sqrt(xx * xx + yy * yy);
		result = int(length + 0.5);
	}
	return result;
}

Position rotate_around(Position center,Position position,double angle)
{
	double x_in = position.x - center.x;
	double y_in = position.y - center.y;
	double c = cos(angle);
	double s = sin(angle);
	double x_out = x_in * c - y_in * s;
	double y_out = x_in * s + y_in * c;
	return Position{ center.x + int(x_out + 0.5), center.y + int(y_out + 0.5) };
}

int point_to_line_segment_distance(FastPosition a,FastPosition b,FastPosition x)
{
	FastPosition ax = x - a;
	FastPosition ab = b - a;
	int dot = ax.x * ab.x + ax.y * ab.y;
	int len_sq = ab.x * ab.x + ab.y * ab.y;
	double param = -1.0;
	if (len_sq > 0) param = double(dot) / double(len_sq);
	FastPosition c;
	if (param <= 0.0) {
		c = a;
	} else if (param >= 1.0) {
		c = b;
	} else {
		c.x = int(a.x + param * ab.x + 0.5);
		c.y = int(a.y + param * ab.y + 0.5);
	}
	return int(x.getDistance(c) + 0.5);
}

Position chokepoint_center(const BWEM::ChokePoint* cp)
{
	return center_position(cp->Pos(BWEM::ChokePoint::middle));
}

std::pair<WalkPosition,WalkPosition> chokepoint_ends(const BWEM::ChokePoint* cp)
{
	const auto correction = [](FastWalkPosition walk_position,FastWalkPosition other_walk_position){
		const auto sign = [](int x) { return (x > 0) - (x < 0); };
		int h = sign(walk_position.x - other_walk_position.x);
		int v = sign(walk_position.y - other_walk_position.y);
		
		for (FastWalkPosition delta : { FastWalkPosition(h, v), FastWalkPosition(h, 0), FastWalkPosition(0, v) } ) {
			if (delta.x != 0 || delta.y != 0) {
				FastWalkPosition test_position = walk_position + delta;
				if (test_position.isValid() && Broodwar->isWalkable(walk_position)) {
					return test_position;
				}
			}
		}
		
		return walk_position;
	};
	
	WalkPosition wa = cp->Pos(BWEM::ChokePoint::end1);
	WalkPosition wb = cp->Pos(BWEM::ChokePoint::end2);
	return std::make_pair(correction(wa, wb), correction(wb, wa));
}

int chokepoint_width(const BWEM::ChokePoint* cp)
{
	Position a = center_position(cp->Pos(BWEM::ChokePoint::end1));
	Position b = center_position(cp->Pos(BWEM::ChokePoint::end2));
	return a.getApproxDistance(b);
}

bool unit_has_pending_command(Unit unit,UnitCommandType command_type)
{
	return (unit->getLastCommand().getType() == command_type &&
			unit->getLastCommandFrame() + Broodwar->getRemainingLatencyFrames() > Broodwar->getFrameCount());
}

bool unit_has_target(Unit unit,Position target)
{
	return unit->getOrderTargetPosition() == target || unit->getTargetPosition() == target;
}

bool unit_has_target(Unit unit,Unit target)
{
	return unit->getOrderTarget() == target || unit->getTarget() == target;
}

bool not_incomplete(Unit unit)
{
	return unit->isCompleted() || unit->getType().isBuilding();
}

bool not_cloaked(Unit unit)
{
	return ((!unit->isCloaked() && !unit->isBurrowed() && !unit->getType().hasPermanentCloak()) ||
			(unit->getPlayer()->isEnemy(Broodwar->self()) && unit->isDetected()));
}

bool is_disabled(Unit unit)
{
	return unit->isStasised() || unit->isLockedDown() || unit->isMaelstrommed();
}

bool is_inside_bunker(Unit unit)
{
	return (unit->isLoaded() && unit->getTransport() != nullptr && unit->getTransport()->getType() == UnitTypes::Terran_Bunker);
}

bool can_attack(Unit attacking_unit)
{
	return ((attacking_unit->getType() == UnitTypes::Terran_Bunker && information_manager.bunker_marines_loaded(attacking_unit) > 0) ||
			attacking_unit->getType() == UnitTypes::Protoss_Carrier ||
			attacking_unit->getType() == UnitTypes::Protoss_Reaver ||
			attacking_unit->getType().groundWeapon() != WeaponTypes::None ||
			attacking_unit->getType().airWeapon() != WeaponTypes::None);
}

bool can_attack(Unit attacking_unit,bool defender_flying)
{
	return ((attacking_unit->getType() == UnitTypes::Terran_Bunker && information_manager.bunker_marines_loaded(attacking_unit) > 0) ||
			attacking_unit->getType() == UnitTypes::Protoss_Carrier ||
			(!defender_flying && attacking_unit->getType() == UnitTypes::Protoss_Reaver) ||
			(defender_flying ? attacking_unit->getType().airWeapon() : attacking_unit->getType().groundWeapon()) != WeaponTypes::None);
}

bool can_attack(UnitType attacking_unit_type,bool defender_flying)
{
	return (attacking_unit_type == UnitTypes::Terran_Bunker ||
			attacking_unit_type == UnitTypes::Protoss_Carrier ||
			(!defender_flying && attacking_unit_type == UnitTypes::Protoss_Reaver) ||
			(defender_flying ? attacking_unit_type.airWeapon() : attacking_unit_type.groundWeapon()) != WeaponTypes::None);
}

bool can_attack(Unit attacking_unit,Unit defending_unit)
{
	if (attacking_unit->getType() == UnitTypes::Terran_Bunker) {
		return information_manager.bunker_marines_loaded(attacking_unit) > 0;
	} else if (attacking_unit->getType() == UnitTypes::Protoss_Carrier) {
		return true;
	} else if (attacking_unit->getType() == UnitTypes::Protoss_Reaver) {
		return !defending_unit->isFlying();
	} else if (defending_unit->isFlying()) {
		return attacking_unit->getType().airWeapon() != WeaponTypes::None;
	} else {
		return attacking_unit->getType().groundWeapon() != WeaponTypes::None;
	}
}

bool can_attack_in_range(Unit attacking_unit,Unit defending_unit,int margin)
{
	if (!attacking_unit->isCompleted() || !attacking_unit->isPowered()) return false;
	if (attacking_unit->getType() == UnitTypes::Terran_Bunker) {
		if (information_manager.bunker_marines_loaded(attacking_unit) == 0) return false;
		int distance = calculate_distance(UnitTypes::Terran_Marine, attacking_unit->getPosition(), defending_unit->getType(), defending_unit->getPosition());
		return distance <= weapon_max_range(WeaponTypes::Gauss_Rifle, attacking_unit->getPlayer()) + 64 + margin;
	}
	int distance = defending_unit->getDistance(attacking_unit);
	int max_range = weapon_max_range(attacking_unit, defending_unit->isFlying());
	int min_range = weapon_min_range(attacking_unit);
	return min_range <= distance && distance <= max_range + margin;
}

bool can_attack_in_range_at_positions(Unit attacking_unit,Position attacking_position,Unit defending_unit,Position defending_position)
{
	if (!attacking_unit->isCompleted() || !attacking_unit->isPowered()) return false;
	if (attacking_unit->getType() == UnitTypes::Terran_Bunker) {
		if (information_manager.bunker_marines_loaded(attacking_unit) == 0) return false;
		int distance = calculate_distance(UnitTypes::Terran_Marine, attacking_position, defending_unit->getType(), defending_position);
		return distance <= weapon_max_range(WeaponTypes::Gauss_Rifle, attacking_unit->getPlayer()) + 64;
	}
	int distance = calculate_distance(defending_unit->getType(), defending_position, attacking_unit->getType(), attacking_position);
	int max_range = weapon_max_range(attacking_unit, defending_unit->isFlying());
	int min_range = weapon_min_range(attacking_unit);
	return min_range <= distance && distance <= max_range;
}

bool can_attack_in_range_at_positions(UnitType attacking_unit_type,Position attacking_position,Player attacking_player,UnitType defending_unit_type,Position defending_position,int margin)
{
	if (attacking_unit_type == UnitTypes::Terran_Bunker) {
		int distance = calculate_distance(UnitTypes::Terran_Marine, attacking_position, defending_unit_type, defending_position);
		return distance <= weapon_max_range(WeaponTypes::Gauss_Rifle, attacking_player) + 64;
	}
	int distance = calculate_distance(defending_unit_type, defending_position, attacking_unit_type, attacking_position);
	int max_range = weapon_max_range(attacking_unit_type, attacking_player, defending_unit_type.isFlyer());
	int min_range = weapon_min_range(attacking_unit_type);
	return min_range <= distance && distance <= max_range + margin;
}

bool can_attack_in_range_with_prediction(Unit attacking_unit,Unit defending_unit)
{
	if (!attacking_unit->isCompleted() || !attacking_unit->isPowered()) return false;
	return can_attack_in_range_at_positions(attacking_unit, predict_position(attacking_unit), defending_unit, predict_position(defending_unit));
}

bool can_attack_in_range_at_position_with_prediction(Unit attacking_unit,Position attacking_position,Unit defending_unit)
{
	Position defending_position = predict_position(defending_unit);
	return can_attack_in_range_at_positions(attacking_unit, attacking_position, defending_unit, defending_position);
}

bool is_melee(UnitType unit_type)
{
	return (unit_type == UnitTypes::Protoss_Zealot ||
			unit_type == UnitTypes::Protoss_Dark_Templar ||
			unit_type == UnitTypes::Zerg_Zergling ||
			unit_type == UnitTypes::Zerg_Ultralisk);
}

bool is_melee_or_worker(UnitType unit_type)
{
	return (unit_type.isWorker() ||
			is_melee(unit_type));
}

bool is_siege_tank(UnitType unit_type)
{
	return (unit_type == UnitTypes::Terran_Siege_Tank_Siege_Mode ||
			unit_type == UnitTypes::Terran_Siege_Tank_Tank_Mode);
}

bool is_stimmable(UnitType unit_type)
{
	return (unit_type == UnitTypes::Terran_Marine ||
			unit_type == UnitTypes::Terran_Firebat);
}

bool is_suicidal(UnitType unit_type)
{
	return (unit_type == UnitTypes::Protoss_Scarab ||
			unit_type == UnitTypes::Zerg_Infested_Terran ||
			unit_type == UnitTypes::Zerg_Scourge ||
			unit_type == UnitTypes::Terran_Vulture_Spider_Mine);
}

bool is_spellcaster(UnitType unit_type)
{
	return unit_type.isSpellcaster() && !unit_type.isBuilding();
}

bool is_zerg_healing(UnitType unit_type)
{
	return (unit_type.getRace() == Races::Zerg &&
			unit_type != UnitTypes::Zerg_Egg &&
			unit_type != UnitTypes::Zerg_Lurker_Egg &&
			unit_type != UnitTypes::Zerg_Cocoon);
}

bool is_low_priority_target(Unit unit)
{
	UnitType unit_type = unit->getType();
	return (unit_type == UnitTypes::Protoss_Interceptor ||
			unit_type == UnitTypes::Zerg_Egg ||
			unit_type == UnitTypes::Zerg_Lurker_Egg ||
			unit_type == UnitTypes::Zerg_Larva ||
			unit->isDefenseMatrixed());
}

bool is_undamaged(Unit unit)
{
	UnitType unit_type = unit->getType();
	return unit->getHitPoints() == unit_type.maxHitPoints() && unit->getShields() == unit_type.maxShields();
}

bool is_less_than_half_damaged(Unit unit)
{
	UnitType unit_type = unit->getType();
	if (unit_type.maxShields() > 0) {
		return unit->getHitPoints() == unit_type.maxHitPoints();
	} else {
		return unit->getHitPoints() >= (unit_type.maxHitPoints() / 2);
	}
}

bool is_hp_undamaged(Unit unit)
{
	UnitType unit_type = unit->getType();
	return unit->getHitPoints() == unit->getType().maxHitPoints();
}

bool is_on_cooldown(Unit unit,bool flying)
{
	if (unit->getType() == UnitTypes::Protoss_Reaver && unit->getScarabCount() == 0) return true;
	if (unit->getType() == UnitTypes::Protoss_Carrier && unit->getInterceptorCount() == 0) return true;
	int cooldown = flying ? unit->getAirWeaponCooldown() : unit->getGroundWeaponCooldown();
	return cooldown > Broodwar->getRemainingLatencyFrames();
}

bool is_running_away(Unit combat_unit,Unit enemy_unit)
{
	Position delta = enemy_unit->getPosition() - combat_unit->getPosition();
	double inner_product = delta.x * enemy_unit->getVelocityX() + delta.y * enemy_unit->getVelocityY();
	return inner_product > 0.0;
}

bool is_facing(Unit unit,Unit target_unit)
{
	Position delta = target_unit->getPosition() - unit->getPosition();
	double dx = cos(unit->getAngle());
	double dy = sin(unit->getAngle());
	double inner_product = dx * delta.x + dy * delta.y;
	return inner_product > 0.0;
}

int turn_frames_needed(Unit unit,Unit target_unit)
{
	Position delta = target_unit->getPosition() - unit->getPosition();
	double dx = cos(unit->getAngle());
	double dy = sin(unit->getAngle());
	double inner_product = (dx * delta.x + dy * delta.y) / std::sqrt(delta.x * delta.x + delta.y * delta.y);
	double angle_radians = std::acos(inner_product);
	double angle_256 = angle_radians / M_PI * 128.0;
	double frames = angle_256 / unit->getType().turnRadius();
	return (int)ceil(frames);
}

int ground_height(Position position)
{
	int ground_height = Broodwar->getGroundHeight(TilePosition(position));
	return ground_height / 2;
}

int distance_to_terrain(WalkPosition walk_position)
{
	return Broodwar->isWalkable(walk_position) ? bwem_map.GetMiniTile(walk_position).Altitude() : 0;
}

int distance_to_terrain(Position position)
{
	WalkPosition base_walk_position(position - Position(4, 4));
	Position base_position = Position(base_walk_position) + Position(4, 4);
	double x1 = double(position.x - base_position.x) / 8.0;
	double y1 = double(position.y - base_position.y) / 8.0;
	double x2 = 1.0 - x1;
	double y2 = 1.0 - y1;
	double result = (y2 * (x2 * distance_to_terrain(base_walk_position) +
						   x1 * distance_to_terrain(base_walk_position + WalkPosition(1, 0))) +
					 y1 * (x2 * distance_to_terrain(base_walk_position + WalkPosition(0, 1)) +
						   x1 * distance_to_terrain(base_walk_position + WalkPosition(1, 1))));
	return int(result + 0.5);
}

int ground_distance(Position a,Position b)
{
	int distance = -1;
	if (a.isValid() && has_area(WalkPosition(a)) &&
		b.isValid() && has_area(WalkPosition(b))) bwem_map.GetPath(a, b, &distance);
	return distance;
}

int ground_distance_to_bwcircle(FastPosition center,int radius,FastPosition other_position)
{
	int smallest_distance = INT_MAX;
	if (center.isValid() &&
		other_position.isValid() &&
		has_area(FastWalkPosition(other_position))) {
		const int d = int(kBWCircle45DegreesFactor * radius + 0.5);
		for (FastPosition delta : {
			FastPosition(radius, 0),
			FastPosition(0, radius / 4),
			FastPosition(d, d),
			FastPosition(radius / 4, radius)
		}) {
			for (int i = 0; i < 4; i++) {
				FastPosition position = center + delta;
				if (position.isValid() && has_area(FastWalkPosition(position))) {
					int distance = ground_distance(position, other_position);
					if (distance >= 0) smallest_distance = std::min(distance, smallest_distance);
				}
				if (i < 3) delta = FastPosition(-delta.y, delta.x);
			}
		}
		if (smallest_distance == INT_MAX &&
			has_area(FastWalkPosition(center))) {
			int distance = ground_distance(center, other_position);
			if (distance >= 0) {
				smallest_distance = std::max(0, distance - radius);
			}
		}
	}
	return smallest_distance;
}

double distance_to_bwcircle(FastPosition delta,int radius)
{
	double distance = INFINITY;
	if (delta.isValid()) {
		int dx = std::abs(delta.x);
		int dy = std::abs(delta.y);
		double l = std::min(dx, dy);
		double h = std::max(dx, dy);
		
		double r = radius;
		double q = 0.25 * radius;
		double d = kBWCircle45DegreesFactor * radius;
		
		bool outside = (h > radius || l * (r - d) + h * (d - q) > d * (r - q));
		if (outside) {
			bool l1 = (l * (d - q) + h * (d - r) <= q * (d - q) + r * (d - r));
			bool l2 = (l <= q);
			bool l3 = (l * (d - q) + h * (d - r) <= d * (2 * d - q - r));
			if (l2) {
				distance = h - radius;
			} else if (l1) {
				distance = std::sqrt((l - q) * (l - q) + (h - r) * (h - r));
			} else if (l3) {
				distance = (l * (r - d) + h * (d - q) - (r * d - q * d)) / std::sqrt((r - d) * (r - d) + (d - q) * (d - q));
			} else {
				distance = std::sqrt((l - d) * (l - d) + (h - d) * (h - d));
			}
		} else {
			double distance1 = h - radius;
			double distance2 = (l * (r - d) + h * (d - q) - (r * d - q * d)) / std::sqrt((r - d) * (r - d) + (d - q) * (d - q));
			distance = std::max(distance1, distance2);
		}
	}
	return distance;
}

double distance_to_square(FastPosition delta,int half_side)
{
	double distance = INFINITY;
	if (delta.isValid()) {
		double dx = std::abs(delta.x) - half_side;
		double dy = std::abs(delta.y) - half_side;
		if (dx > 0.0 && dy > 0.0) {
			distance = std::sqrt(dx * dx + dy * dy);
		} else if (std::abs(dx) <= std::abs(dy)) {
			distance = dx;
		} else {
			distance = dy;
		}
	}
	return distance;
}

int chebyshev_distance(FastPosition a,FastPosition b)
{
	return chebyshev_norm(a - b);
}

int chebyshev_norm(FastPosition delta)
{
	return std::max(std::abs(delta.x), std::abs(delta.y));
}

int offense_max_range(UnitType attacking_unit_type,Player attacking_player,bool defender_flying)
{
	int result = weapon_max_range(attacking_unit_type, attacking_player, defender_flying);
	if (result == -1) {
		if (is_spellcaster(attacking_unit_type) &&
			attacking_unit_type != UnitTypes::Terran_Medic) result = 256;	// @ Actual spell range
	}
	return result;
}

int offense_max_range(Unit attacking_unit,bool defender_flying)
{
	return offense_max_range(attacking_unit->getType(), attacking_unit->getPlayer(), defender_flying);
}

int weapon_min_range(UnitType attacking_unit_type)
{
	WeaponType weapon_type = attacking_unit_type.groundWeapon();
	return weapon_type == WeaponTypes::None ? 0 : weapon_type.minRange();
}

int weapon_min_range(Unit attacking_unit)
{
	return weapon_min_range(attacking_unit->getType());
}

int weapon_max_range(WeaponType weapon_type,Player player)
{
	int result = weapon_type.maxRange();
	
	switch (weapon_type) {
		case WeaponTypes::Gauss_Rifle:
			if (information_manager.upgrade_level(player, UpgradeTypes::U_238_Shells)) result += 32;
			break;
		case WeaponTypes::Hellfire_Missile_Pack:
			if (information_manager.upgrade_level(player, UpgradeTypes::Charon_Boosters)) result += 96;
			break;
		case WeaponTypes::Needle_Spines:
			if (information_manager.upgrade_level(player, UpgradeTypes::Grooved_Spines)) result += 32;
			break;
		case WeaponTypes::Phase_Disruptor:
			if (information_manager.upgrade_level(player, UpgradeTypes::Singularity_Charge)) result += 64;
			break;
		default:
			break;
	}
	
	return result;
}

int weapon_max_range(UnitType attacking_unit_type,Player attacking_player,bool defender_flying)
{
	if (attacking_unit_type == UnitTypes::Terran_Bunker) {	// @ Assumes there are marines in the bunker
		return weapon_max_range(WeaponTypes::Gauss_Rifle, attacking_player) + 56;
	} else if (attacking_unit_type == UnitTypes::Protoss_Carrier) {	// @ Assumes carrier has interceptors
		return 8 * 32;
	} else if (attacking_unit_type == UnitTypes::Protoss_Reaver && !defender_flying) {	// @ Assumes reaver has scarabs
		return 8 * 32;
	}
	
	WeaponType weapon_type = defender_flying ? attacking_unit_type.airWeapon() : attacking_unit_type.groundWeapon();
	if (weapon_type == WeaponTypes::None) return -1;	// Defender can not be attacked by attacking_unit
	return weapon_max_range(weapon_type, attacking_player);
}

int weapon_max_range(Unit attacking_unit,bool defender_flying)
{
	if (attacking_unit->getType() == UnitTypes::Terran_Bunker && information_manager.bunker_marines_loaded(attacking_unit) == 0) return -1;
	return weapon_max_range(attacking_unit->getType(), attacking_unit->getPlayer(), defender_flying);
}

double top_speed(UnitType unit_type,Player player)
{
	double result = unit_type.topSpeed();
	if ((unit_type == UnitTypes::Terran_Vulture && information_manager.upgrade_level(player, UpgradeTypes::Ion_Thrusters) > 0) ||
		(unit_type == UnitTypes::Zerg_Overlord && information_manager.upgrade_level(player, UpgradeTypes::Pneumatized_Carapace) > 0) ||
		(unit_type == UnitTypes::Zerg_Zergling && information_manager.upgrade_level(player, UpgradeTypes::Metabolic_Boost) > 0) ||
		(unit_type == UnitTypes::Zerg_Hydralisk && information_manager.upgrade_level(player, UpgradeTypes::Muscular_Augments) > 0) ||
		(unit_type == UnitTypes::Protoss_Zealot && information_manager.upgrade_level(player, UpgradeTypes::Leg_Enhancements) > 0) ||
		(unit_type == UnitTypes::Protoss_Shuttle && information_manager.upgrade_level(player, UpgradeTypes::Gravitic_Drive) > 0) ||
		(unit_type == UnitTypes::Protoss_Observer && information_manager.upgrade_level(player, UpgradeTypes::Gravitic_Boosters) > 0) ||
		(unit_type == UnitTypes::Protoss_Scout && information_manager.upgrade_level(player, UpgradeTypes::Gravitic_Thrusters) > 0) ||
		(unit_type == UnitTypes::Zerg_Ultralisk && information_manager.upgrade_level(player, UpgradeTypes::Anabolic_Synthesis) > 0)) {
		result = (unit_type == UnitTypes::Protoss_Scout) ? (result + 427.0 / 256.0) : (result * 1.5);
		result = std::max(result, 853.0 / 256.0);
	}
	return result;
}

int sight_range(UnitType unit_type,Player player)
{
	int result = unit_type.sightRange();
	if ((unit_type == UnitTypes::Terran_Ghost && information_manager.upgrade_level(player, UpgradeTypes::Ocular_Implants) > 0) ||
		(unit_type == UnitTypes::Zerg_Overlord && information_manager.upgrade_level(player, UpgradeTypes::Antennae) > 0) ||
		(unit_type == UnitTypes::Protoss_Observer && information_manager.upgrade_level(player, UpgradeTypes::Sensor_Array) > 0) ||
		(unit_type == UnitTypes::Protoss_Scout && information_manager.upgrade_level(player, UpgradeTypes::Apial_Sensors) > 0)) {
		result = 11 * 32;
	}
	return result;
}

static int weapon_damage(WeaponType weapon_type,Player player)
{
	int base_damage = weapon_type.damageAmount() + weapon_type.damageBonus() * information_manager.upgrade_level(player, weapon_type.upgradeType());
	return base_damage * weapon_type.damageFactor();
}

static int weapon_cooldown(WeaponType weapon_type,Player player)
{
	int result = weapon_type.damageCooldown();
	if (weapon_type == WeaponTypes::Claws && information_manager.upgrade_level(player, UpgradeTypes::Adrenal_Glands)) result /= 2;
	return result;
}

static double damage_type_factor(DamageType damage_type,UnitSizeType unit_size_type)
{
	double result = 1.0;
	
	if (damage_type == DamageTypes::Explosive) {
		if (unit_size_type == UnitSizeTypes::Small) result = 0.5;
		else if (unit_size_type == UnitSizeTypes::Medium) result = 0.75;
	} else if (damage_type == DamageTypes::Concussive) {
		if (unit_size_type == UnitSizeTypes::Medium) result = 0.5;
		else if (unit_size_type == UnitSizeTypes::Large) result = 0.25;
	}
	
	return result;
}

static int unit_shields(UnitType unit_type,Player player)
{
	int result = 0;
	if (unit_type.maxShields() > 0) {
		result = information_manager.upgrade_level(player, UpgradeTypes::Protoss_Plasma_Shields);
	}
	return result;
}

static int unit_armor(UnitType unit_type,Player player)
{
	int result = unit_type.armor() + information_manager.upgrade_level(player, unit_type.armorUpgrade());
	if (unit_type == UnitTypes::Zerg_Ultralisk && information_manager.upgrade_level(player, UpgradeTypes::Chitinous_Plating) > 0) result += 2;
	if (unit_type == UnitTypes::Hero_Torrasque) result += 2;
	return result;
}

DPF calculate_damage_per_frame(Unit source,Unit destination)
{
	int bunker_marines_loaded = 0;
	if (source->getType() == UnitTypes::Terran_Bunker) {
		bunker_marines_loaded = information_manager.bunker_marines_loaded(source);
	}
	return calculate_damage_per_frame(source->getType(), source->getPlayer(), destination->getType(), destination->getPlayer(), bunker_marines_loaded);
}

DPF calculate_damage_per_frame(UnitType source_type,Player source_player,UnitType destination_type,Player destination_player,int source_bunker_marines_loaded)
{
	WeaponType weapon_type;
	if (source_type == UnitTypes::Protoss_Reaver && !destination_type.isFlyer()) {
		weapon_type = WeaponTypes::Scarab;
	} else if (source_type == UnitTypes::Protoss_Carrier) {
		weapon_type = WeaponTypes::Pulse_Cannon;
	} else if (source_type == UnitTypes::Terran_Bunker) {
		weapon_type = WeaponTypes::Gauss_Rifle;
	} else {
		weapon_type = destination_type.isFlyer() ? source_type.airWeapon() : source_type.groundWeapon();
	}
	if (weapon_type == WeaponTypes::None || weapon_type == WeaponTypes::Unknown) return DPF();
	
	double base_damage = weapon_damage(weapon_type, source_player);
	double shield_base_damage = base_damage;
	double hp_base_damage = base_damage;
	if (weapon_type.damageType() != DamageTypes::Ignore_Armor) {
		shield_base_damage -= unit_shields(destination_type, destination_player);
		hp_base_damage -= unit_armor(destination_type, destination_player);
	}
	if (shield_base_damage < 0.5) shield_base_damage = 0.5;
	if (hp_base_damage < 0.5) hp_base_damage = 0.5;
	
	if (source_type != UnitTypes::Protoss_Reaver && source_type != UnitTypes::Protoss_Carrier && source_type != UnitTypes::Terran_Bunker) {
		int hits = destination_type.isFlyer() ? source_type.maxAirHits() : source_type.maxGroundHits();
		shield_base_damage *= hits;
		hp_base_damage *= hits;
	}
	if (source_type == UnitTypes::Protoss_Carrier) {
		int interceptor_count = information_manager.upgrade_level(source_player, UpgradeTypes::Carrier_Capacity) ? 8 : 4;
		shield_base_damage *= interceptor_count;
		hp_base_damage *= interceptor_count;
	}
	if (source_type == UnitTypes::Terran_Bunker) {
		shield_base_damage *= source_bunker_marines_loaded;
		hp_base_damage *= source_bunker_marines_loaded;
	}
	hp_base_damage *= damage_type_factor(weapon_type.damageType(), destination_type.size());
	
	int cooldown;
	if (source_type == UnitTypes::Protoss_Reaver) {
		cooldown = 60;
	} else if (source_type == UnitTypes::Protoss_Carrier) {
		cooldown = 37;
	} else {
		cooldown = weapon_cooldown(weapon_type, source_player);
	}
	return DPF(shield_base_damage / cooldown, hp_base_damage / cooldown);
}

double calculate_frames_to_live(Unit unit)
{
	std::vector<Unit> enemy_units;
	for (Unit enemy_unit : Broodwar->enemies().getUnits()) {
		if (enemy_unit->isAttacking() &&
			enemy_unit->getOrderTarget() == unit &&
			can_attack_in_range(enemy_unit, unit)) {
			enemy_units.push_back(enemy_unit);
		}
	}
	
	return calculate_frames_to_live(unit, enemy_units);
}

double calculate_frames_to_live(Unit unit,const std::vector<Unit> enemy_units)
{
	DPF dpf;
	for (Unit enemy_unit : enemy_units) {
		if (!is_suicidal(enemy_unit->getType()) && enemy_unit->getType() != UnitTypes::Protoss_Interceptor) {
			dpf += calculate_damage_per_frame(enemy_unit, unit);
		}
	}
	return dpf ? (unit->getShields() / dpf.shield + unit->getHitPoints() / dpf.hp) : INFINITY;
}

double calculate_repair_hitpoints_per_frame(UnitType type)
{
	return std::ceil(0.9 * type.maxHitPoints() * 256.0 / type.buildTime()) / 256.0;
}

FastPosition edge_position(UnitType unit_type,FastPosition unit_position,FastPosition other_position)
{
	return FastPosition(clamp(unit_position.x - unit_type.dimensionLeft(), other_position.x, unit_position.x + unit_type.dimensionRight()),
						clamp(unit_position.y - unit_type.dimensionUp(), other_position.y, unit_position.y + unit_type.dimensionDown()));
}

FastPosition edge_to_point_delta(UnitType unit_type,FastPosition unit_position,FastPosition other_position)
{
	if (!unit_position.isValid() || !other_position.isValid()) return Positions::None;
	
	int left = unit_position.x - unit_type.dimensionLeft();
	int top = unit_position.y - unit_type.dimensionUp();
	int right = unit_position.x + unit_type.dimensionRight() + 1;	// @ Why the +1's? Does not seem logical, check this
	int bottom = unit_position.y + unit_type.dimensionDown() + 1;
	
	int x_dist = left - other_position.x;
	if (x_dist < 0) x_dist = std::max(0, other_position.x - right);
	
	int y_dist = top - other_position.y;
	if (y_dist < 0) y_dist = std::max(0, other_position.y - bottom);
	
	return FastPosition{x_dist, y_dist};
}

int calculate_distance(UnitType unit_type,FastPosition unit_position,FastPosition other_position)
{
	if (!unit_position.isValid() || !other_position.isValid()) return std::numeric_limits<int>::max();
	return FastPosition{0, 0}.getApproxDistance(edge_to_point_delta(unit_type, unit_position, other_position));
}

FastPosition edge_to_edge_delta(UnitType unit_type1,FastPosition unit_position1,UnitType unit_type2,FastPosition unit_position2)
{
	if (!unit_position1.isValid() || !unit_position2.isValid()) return Positions::None;
	
	int left = unit_position2.x - unit_type2.dimensionLeft() - 1;
	int top = unit_position2.y - unit_type2.dimensionUp() - 1;
	int right = unit_position2.x + unit_type2.dimensionRight() + 1;
	int bottom = unit_position2.y + unit_type2.dimensionDown() + 1;
	
	int x_dist = unit_position1.x - unit_type1.dimensionLeft() - right;
	if (x_dist < 0) x_dist = std::max(0, left - unit_position1.x - unit_type1.dimensionRight());
	
	int y_dist = unit_position1.y - unit_type1.dimensionUp() - bottom;
	if (y_dist < 0) y_dist = std::max(0, top - unit_position1.y - unit_type1.dimensionDown());
	
	return FastPosition(x_dist, y_dist);
}

int calculate_distance(UnitType unit_type1,FastPosition unit_position1,UnitType unit_type2,FastPosition unit_position2)
{
	if (!unit_position1.isValid() || !unit_position2.isValid()) return std::numeric_limits<int>::max();
	return FastPosition{0, 0}.getApproxDistance(edge_to_edge_delta(unit_type1, unit_position1, unit_type2, unit_position2));
}

Position predict_position(Unit unit)
{
	return predict_position(unit, Broodwar->getRemainingLatencyFrames());
}

Position predict_position(Unit unit,int frames)
{
	double vx = unit->getVelocityX();
	double vy = unit->getVelocityY();
	if ((vx != 0.0 || vy != 0.0) && !unit->isFlying() && unit->getType().acceleration() == 1) {
		// Use average speed instead of current speed for iscript units.
		double factor = unit->getType().topSpeed() / std::sqrt(vx * vx + vy * vy);
		vx *= factor;
		vy *= factor;
	}
	int dx = (int)(frames * vx + 0.5);
	int dy = (int)(frames * vy + 0.5);
	Position result = unit->getPosition() + Position(dx, dy);
	return result.makeValid();
}

int building_extra_frames(UnitType type)
{
	int extra_frames = 0;
	if (type.isBuilding()) {
		Race race = type.getRace();
		if (race == Races::Terran) {
			extra_frames = 2;
		} else if (race == Races::Protoss) {
			extra_frames = 72;
		} else if (race == Races::Zerg) {
			extra_frames = 9;
		}
	}
	return extra_frames;
}

int estimate_building_start_frame_based_on_hit_points(Unit unit)
{
	UnitType type = unit->getType();
	int result;
	if (!unit->exists() || unit->isCompleted() || !type.isBuilding()) {
		result = Broodwar->getFrameCount() - type.buildTime() - building_extra_frames(type);
	} else if (type.getRace() == Races::Zerg && type.whatBuilds().first.isBuilding()) {
		result = Broodwar->getFrameCount();
	} else {
		double total_hit_points = double(type.maxHitPoints());
		double ratio = (unit->getHitPoints() - 0.1 * total_hit_points) / (0.9 * total_hit_points);
		result = Broodwar->getFrameCount() - int(ratio * type.buildTime() + 0.5);
	}
	return result;
}

bool check_terrain_collision(UnitType type,FastPosition position)
{
	FastWalkPosition top_left(FastPosition(position.x - type.dimensionLeft(), position.y - type.dimensionUp()));
	FastWalkPosition bottom_right(FastPosition(position.x + type.dimensionRight(), position.y + type.dimensionDown()));
	for (int x = top_left.x; x <= bottom_right.x; x++) {
		for (int y = top_left.y; y <= bottom_right.y; y++) {
			if (!Broodwar->isWalkable(x, y)) return false;
		}
	}
	return true;
}

static bool is_our_mining_worker(Unit unit)
{
	return unit->exists() && unit->getPlayer() == Broodwar->self() && (unit->isGatheringMinerals() || unit->isGatheringGas());
}

std::vector<const InformationUnit*> colliding_units_sorted(Unit unit,FastPosition position)
{
	Rect r(unit->getType(), position);
	std::vector<const InformationUnit*> result;
	
	FastTilePosition top_left(r.top_left());
	FastTilePosition bottom_right(r.bottom_right());
	for (int y = top_left.y; y <= bottom_right.y; y++) {
		for (int x = top_left.x; x <= bottom_right.x; x++) {
			FastTilePosition tile_position(x, y);
			if (tile_position.isValid()) {
				for (auto& other_unit : unit_grid.collision_units_in_tile(tile_position)) {
					if (other_unit->unit != unit && !is_our_mining_worker(other_unit->unit)) {
						Rect r2(other_unit->type, other_unit->position);
						if (r.overlaps(r2)) result.push_back(other_unit);
					}
				}
			}
		}
	}
	
	remove_duplicates(result);
	return result;
}

bool check_unit_collision(Unit unit,FastPosition position,const InformationUnit* other_unit,int unit_margin)
{
	bool result = true;
	Rect r(unit->getType(), position);
	r.inset(-unit_margin);
	if (other_unit->unit != unit &&
		!other_unit->flying &&
		!other_unit->burrowed &&
		!is_our_mining_worker(other_unit->unit)) {
		Rect r2(other_unit->type, other_unit->position);
		if (r.overlaps(r2)) result = false;
	}
	return result;
}

static bool check_unit_collision(Unit unit,UnitType type,FastPosition position,int unit_margin)
{
	Rect r(type, position);
	r.inset(-unit_margin);
	
	FastTilePosition top_left(r.top_left());
	FastTilePosition bottom_right(r.bottom_right());
	for (int y = top_left.y; y <= bottom_right.y; y++) {
		for (int x = top_left.x; x <= bottom_right.x; x++) {
			FastTilePosition tile_position(x, y);
			if (tile_position.isValid()) {
				for (auto& other_unit : unit_grid.collision_units_in_tile(tile_position)) {
					if (other_unit->unit != unit && !is_our_mining_worker(other_unit->unit)) {
						Rect r2(other_unit->type, other_unit->position);
						if (r.overlaps(r2)) return false;
					}
				}
			}
		}
	}
	
	return true;
}

bool check_collision(Unit unit,UnitType type,FastPosition position,int unit_margin)
{
	return type.isFlyer() || (check_terrain_collision(type, position) && check_unit_collision(unit, type, position, unit_margin));
}

bool check_collision(Unit unit,FastPosition position,int unit_margin)
{
	return unit->isFlying() || (check_terrain_collision(unit->getType(), position) && check_unit_collision(unit, unit->getType(), position, unit_margin));
}

int manhattan_distance(TilePosition a,TilePosition b)
{
	return abs(a.x - b.x) + abs(a.y - b.y);
}

void bwem_handle_destroy_safe(Unit unit)
{
	if (unit->getType().isMineralField()) {
		if (bwem_map.GetMineral(unit) != nullptr) bwem_map.OnMineralDestroyed(unit);
	} else if (unit->getType().isSpecialBuilding()) {
		bool found = std::any_of(bwem_map.StaticBuildings().begin(), bwem_map.StaticBuildings().end(), [unit](const std::unique_ptr<BWEM::StaticBuilding>& static_building){
			return static_building->Unit() == unit;
		});
		if (found) bwem_map.OnStaticBuildingDestroyed(unit);
	}
}

int bwem_max_altitude()
{
	int result = INT_MIN;
	for (auto& area : bwem_map.Areas()) {
		result = std::max(result, (int)area.MaxAltitude());
	}
	return result;
}

bool done_or_in_progress(UpgradeType upgrade_type,int level)
{
	int current_level = Broodwar->self()->getUpgradeLevel(upgrade_type);
	return current_level >= level || (current_level == level - 1 && Broodwar->self()->isUpgrading(upgrade_type));
}

bool done_or_in_progress(TechType tech_type)
{
	return Broodwar->self()->hasResearched(tech_type) || Broodwar->self()->isResearching(tech_type);
}

void random_move(Unit unit)
{
	if (!unit->isMoving()) {
		std::uniform_int_distribution<int> xdist(0, Broodwar->mapWidth() * 32 - 1);
		std::uniform_int_distribution<int> ydist(0, Broodwar->mapHeight() * 32 - 1);
		std::default_random_engine& generator = random_generator();
		Position position(xdist(generator), ydist(generator));
		path_finder.execute_path(unit, position, [unit,position](){
			unit_move(unit, position);
		});
	}
}

void unit_move(Unit unit,Position position)
{
	if (!unit->isMoving() || !unit_has_target(unit, position)) unit->move(position);
}

void unit_attack(Unit unit,Unit target)
{
	if (unit->getOrder() != Orders::AttackUnit || !unit_has_target(unit, target)) unit->attack(target);
}

void unit_right_click(Unit unit,Unit target)
{
	if (!unit_has_target(unit, target)) unit->rightClick(target);
}

Rect::Rect(FastWalkPosition walk_position)
{
	left = walk_position.x * 8;
	right = left + 7;
	top = walk_position.y * 8;
	bottom = top + 7;
}

Rect::Rect(UnitType type,FastPosition position)
{
	left = position.x - type.dimensionLeft();
	top = position.y - type.dimensionUp();
	right = position.x + type.dimensionRight();
	bottom = position.y + type.dimensionDown();
}

Rect::Rect(UnitType type,FastTilePosition tile_position,bool tile_size)
{
	if (!tile_size) {
		FastPosition position = center_position_for(type, tile_position);
		left = position.x - type.dimensionLeft();
		top = position.y - type.dimensionUp();
		right = position.x + type.dimensionRight();
		bottom = position.y + type.dimensionDown();
	} else {
		FastPosition position(tile_position);
		left = position.x;
		top = position.y;
		right = left + 32 * type.tileWidth() - 1;
		bottom = top + 32 * type.tileHeight() - 1;
	}
}

bool Rect::is_inside(FastPosition position) const
{
	return (position.x >= left && position.x <= right &&
			position.y >= top && position.y <= bottom);
}

bool Rect::overlaps(const Rect& o) const
{
	return (ranges_overlap(left, right, o.left, o.right) &&
			ranges_overlap(top, bottom, o.top, o.bottom));
}

int Rect::distance(const Rect& o) const
{
	const auto component_distance = [](int a1,int a2,int b1,int b2) {
		if (a2 < b1) return b1 - a2 - 1;
		else if (a1 > b2) return b2 - a1 - 1;
		else return 0;
	};
	
	int dx = component_distance(left, right, o.left, o.right);
	int dy = component_distance(top, bottom, o.top, o.bottom);
	return Positions::Origin.getApproxDistance(Position{dx, dy});
}

void Rect::draw(Color color) const
{
	Broodwar->drawBoxMap(left, top, right + 1, bottom + 1, color);
}

void Rect::inset(int d)
{
	left += d;
	top += d;
	right -= d;
	bottom -= d;
}

bool Rect::in_range(int value,int low,int high)
{
	return value >= low && value <= high;
}

bool Rect::ranges_overlap(int low1,int high1,int low2,int high2)
{
	return in_range(low1, low2, high2) || in_range(high1, low2, high2) || in_range(low2, low1, high1) || in_range(high2, low1, high1);
}

Gap::Gap(const BWEM::ChokePoint* chokepoint)
{
	WalkPosition wa;
	WalkPosition wb;
	std::tie(wa, wb) = chokepoint_ends(chokepoint);
	
	if (wa.y == wb.y) {
		type_ = Type::Horizontal;
		if (wa.x > wb.x) std::swap(wa, wb);
		a_ = Position{ wa.x * 8, wa.y * 8 };
		b_ = Position{ wb.x * 8 + 7, wb.y * 8 + 7 };
	} else if (wa.x == wb.x) {
		type_ = Type::Vertical;
		if (wa.y > wb.y) std::swap(wa, wb);
		a_ = Position{ wa.x * 8, wa.y * 8 };
		b_ = { wb.x * 8 + 7, wb.y * 8 + 7 };
	} else {
		auto pos = [](int c,int c_other){
			return c < c_other ? c * 8 : c * 8 + 7;
		};
		type_ = Type::Diagonal;
		a_ = Position{ pos(wa.x, wb.x), pos(wa.y, wb.y) };
		b_ = Position{ pos(wb.x, wa.x), pos(wb.y, wa.y) };
	}
}

Gap::Gap(const Rect& rect1,const Rect& rect2)
{
	const auto classify_interval = [](int a1,int a2,int b1,int b2) {
		if (a2 < b1) return -1;
		else if (a1 > b2) return 1;
		else return 0;
	};
	
	int h = classify_interval(rect1.left, rect1.right, rect2.left, rect2.right);
	int x1;
	int x2;
	if (h != 0) {
		x1 = (h < 0) ? rect1.right + 1 : rect1.left - 1;
		x2 = (h < 0) ? rect2.left - 1 : rect2.right + 1;
	} else {
		x1 = std::max(rect1.left, rect2.left);
		x2 = std::min(rect1.right, rect2.right);
	}
	bool h_none = (h < 0 && x1 > x2) || (h > 0 && x1 < x2);
	
	int v = classify_interval(rect1.top, rect1.bottom, rect2.top, rect2.bottom);
	int y1;
	int y2;
	if (v != 0) {
		y1 = (v < 0) ? rect1.bottom + 1 : rect1.top - 1;
		y2 = (v < 0) ? rect2.top - 1 : rect2.bottom + 1;
	} else {
		y1 = std::max(rect1.top, rect2.top);
		y2 = std::min(rect1.bottom, rect2.bottom);
	}
	bool v_none = (v < 0 && y1 > y2) || (v > 0 && y1 < y2);
	
	if (h == 0 && v == 0) type_ = Type::Invalid;
	else if (h != 0 && v != 0) type_ = (h_none && v_none) ? Type::None : Type::Diagonal;
	else if (h != 0) type_ = h_none ? Type::None : Type::Horizontal;
	else if (v != 0) type_ = v_none ? Type::None : Type::Vertical;
	else type_ = Type::Invalid;
	
	a_ = Position{x1, y1};
	b_ = Position{x2, y2};
}

int Gap::size() const
{
	switch (type_) {
		case Type::None:
			return 0;
		case Type::Horizontal:
			return std::abs(b_.x - a_.x) + 1;
		case Type::Vertical:
			return std::abs(b_.y - a_.y) + 1;
		case Type::Diagonal:
			return std::max(std::abs(b_.x - a_.x), std::abs(b_.y - a_.y)) + 1;
		default:
			return INT_MAX;
	}
}

bool Gap::fits_through(UnitType unit_type) const
{
	switch (type_) {
		case Type::None:
			return false;
		case Type::Horizontal:
			return std::abs(b_.x - a_.x) > unit_type.width();
		case Type::Vertical:
			return std::abs(b_.y - a_.y) > unit_type.height();
		case Type::Diagonal:
			return std::abs(b_.x - a_.x) > unit_type.width() || std::abs(b_.y - a_.y) > unit_type.height();
		default:
			return false;
	}
}

std::vector<Position> Gap::block_positions(UnitType unit_type,UnitType tight_for_unit_type) const
{
	std::vector<Position> result;
	
	const auto horizontal_count = [&](){
		int w = std::abs(b_.x - a_.x) + 1;
		int u = unit_type.width();
		int t = tight_for_unit_type.width() - 1;
		return (w + u - 1) / (u + t);
	};
	
	const auto horizontal_block_positions = [&](){
		int n = horizontal_count();
		int current = std::min(a_.x, b_.x);
		int gap_left = std::abs(b_.x - a_.x) + 1 - n * unit_type.width();
		int y = (a_.y + b_.y) / 2;
		for (int i = n; i > 0; i--) {
			int gap = gap_left / (i + 1);
			current += gap;
			gap_left -= gap;
			result.push_back(Position{ current + unit_type.dimensionLeft(), y });
			current += unit_type.width();
		}
	};
	
	const auto vertical_count = [&](){
		int h = std::abs(b_.y - a_.y) + 1;
		int u = unit_type.height();
		int t = tight_for_unit_type.height() - 1;
		return (h + u - 1) / (u + t);
	};
	
	const auto vertical_block_positions = [&](){
		int n = vertical_count();
		int current = std::min(a_.y, b_.y);
		int gap_left = std::abs(b_.y - a_.y) + 1 - n * unit_type.height();
		int x = (a_.x + b_.x) / 2;
		for (int i = n; i > 0; i--) {
			int gap = gap_left / (i + 1);
			current += gap;
			gap_left -= gap;
			result.push_back(Position{ x, current + unit_type.dimensionUp() });
			current += unit_type.height();
		}
	};
	
	switch (type_) {
		case Type::Horizontal:
			horizontal_block_positions();
			break;
		case Type::Vertical:
			vertical_block_positions();
			break;
		case Type::Diagonal: {
			int nh = horizontal_count();
			int nv = vertical_count();
			int x0 = a_.x;
			int x1 = b_.x;
			int y0 = a_.y;
			int y1 = b_.y;
			if (nh >= nv) {
				horizontal_block_positions();
				if (std::abs(y0 - y1) >= unit_type.width()) {
					for (auto& position : result) {
						double t = double(position.x - x0) / double(x1 - x0);
						position.y = int(y0 + t * (y1 - y0) + 0.5);
					}
				}
			} else {
				vertical_block_positions();
				if (std::abs(x0 - y1) >= unit_type.height()) {
					for (auto& position : result) {
						double t = double(position.y - y0) / double(y1 - y0);
						position.x = int(x0 + t * (x1 - x0) + 0.5);
					}
				}
			}
			break; }
		default:
			break;
	}
	
	return result;
}

std::vector<Position> Gap::block_positions(const std::vector<UnitType>& unit_types,UnitType tight_for_unit_type,bool use_minimum_number_of_units,bool require_block) const
{
	std::vector<Position> result;
	
	switch (type_) {
		case Type::Horizontal: {
			HorizontalPlacer horizontal_placer(a_, b_, unit_types, tight_for_unit_type, use_minimum_number_of_units);
			if (!require_block || horizontal_placer.is_blocked()) {
				horizontal_placer.place(result);
			}
			break; }
		case Type::Vertical: {
			VerticalPlacer vertical_placer(a_, b_, unit_types, tight_for_unit_type, use_minimum_number_of_units);
			if (!require_block || vertical_placer.is_blocked()) {
				vertical_placer.place(result);
			}
			break; }
		case Type::Diagonal: {
			HorizontalPlacer horizontal_placer(a_, b_, unit_types, tight_for_unit_type, use_minimum_number_of_units);
			VerticalPlacer vertical_placer(a_, b_, unit_types, tight_for_unit_type, use_minimum_number_of_units);
			if (!require_block || (horizontal_placer.is_blocked() && vertical_placer.is_blocked())) {
				if (horizontal_placer.n() > vertical_placer.n()) {
					horizontal_placer.place(result);
					for (auto& position : result) {
						double t = double(position.x - a_.x) / double(b_.x - a_.x);
						position.y = int(a_.y + t * (b_.y - a_.y) + 0.5);
					}
				} else {
					vertical_placer.place(result);
					for (auto& position : result) {
						double t = double(position.y - a_.y) / double(b_.y - a_.y);
						position.x = int(a_.x + t * (b_.x - a_.x) + 0.5);
					}
				}
			}
			break; }
		default:
			break;
	}
	
	return result;
}

std::pair<Position,Position> Gap::line() const
{
	switch (type_) {
		case Type::Horizontal: {
			int y = (a_.y + b_.y) / 2;
			return std::make_pair(Position(a_.x, y), Position(b_.x, y));
		}
		case Type::Vertical: {
			int x = (a_.x + b_.x) / 2;
			return std::make_pair(Position(x, a_.y), Position(x, b_.y));
		}
		case Type::Diagonal:
			return std::make_pair(a_, b_);
		default:
			return std::make_pair(Positions::None, Positions::None);
	}
}

void Gap::draw_box(Color color) const
{
	if (type_ == Type::None || type_ == Type::Invalid) return;
	Broodwar->drawLineMap(a_.x, a_.y, b_.x, a_.y, color);
	Broodwar->drawLineMap(a_.x, b_.y, b_.x, b_.y, color);
	Broodwar->drawLineMap(a_.x, a_.y, a_.x, b_.y, color);
	Broodwar->drawLineMap(b_.x, a_.y, b_.x, b_.y, color);
	draw_line(color);
}

void Gap::draw_line(Color color) const
{
	if (type_ == Type::None || type_ == Type::Invalid) return;
	auto line_val = line();
	if (line_val.first.isValid()) Broodwar->drawLineMap(line_val.first, line_val.second, color);
}

Gap::Placer::Placer(int a,int b,int middle,const std::vector<UnitType>& unit_types,int tight_dimension) : a_(a), b_(b), middle_(middle), unit_types_(unit_types), tight_dimension_(tight_dimension)
{
}

void Gap::Placer::init(bool use_minimum_number_of_units)
{
	int l = std::abs(b_ - a_) + 1;
	for (UnitType unit_type : unit_types_) {
		if (use_minimum_number_of_units && s_ + (n_ * 1) * (tight_dimension_ - 1) >= l) break;
		int s2 = s_ + unit_type_dimension(unit_type);
		if (s2 > l) break;
		s_ = s2;
		n_++;
	}
}

bool Gap::Placer::is_blocked()
{
	return determine_initial_gap_left() <= (n_ + 1) * (tight_dimension_ - 1);
}

void Gap::Placer::place(std::vector<Position>& result)
{
	int current = std::min(a_, b_);
	int gap_left = determine_initial_gap_left();
	for (int i = n_; i > 0; i--) {
		int gap = gap_left / (i + 1);
		current += gap;
		gap_left -= gap;
		UnitType unit_type = unit_types_[n_ - i];
		add_result(result, current, unit_type);
		current += unit_type_dimension(unit_type);
	}
	if (a_ > b_) std::reverse(result.begin(), result.end());
}

int Gap::Placer::determine_initial_gap_left()
{
	return std::abs(b_ - a_) + 1 - s_;
}

Gap::HorizontalPlacer::HorizontalPlacer(Position a,Position b,const std::vector<UnitType>& unit_types,UnitType tight_for_unit_type,bool use_minimum_number_of_units) : Placer(a.x, b.x, (a.y + b.y) / 2, unit_types, tight_for_unit_type.width())
{
	init(use_minimum_number_of_units);
}

int Gap::HorizontalPlacer::unit_type_dimension(UnitType unit_type)
{
	return unit_type.width();
}

void Gap::HorizontalPlacer::add_result(std::vector<Position>& result,int current,UnitType unit_type)
{
	result.push_back(Position{ current + unit_type.dimensionLeft(), middle_ });
}

Gap::VerticalPlacer::VerticalPlacer(Position a,Position b,const std::vector<UnitType>& unit_types,UnitType tight_for_unit_type,bool use_minimum_number_of_units) : Placer(a.y, b.y, (a.x + b.x) / 2, unit_types, tight_for_unit_type.height())
{
	init(use_minimum_number_of_units);
}

int Gap::VerticalPlacer::unit_type_dimension(UnitType unit_type)
{
	return unit_type.height();
}

void Gap::VerticalPlacer::add_result(std::vector<Position>& result,int current,UnitType unit_type)
{
	result.push_back(Position{ middle_, current + unit_type.dimensionLeft() });
}

DamageModel::DamageModel(UnitType type,Player player,int count)
{
	initial_shields_ = shields_ = count * type.maxShields();
	initial_hp_ = hp_ = count * type.maxHitPoints();
	shield_armor_ = unit_shields(type, player);
	armor_ = unit_armor(type, player);
	size_ = type.size();
	flying_ = type.isFlyer();
}

DamageModel::DamageModel(Unit target)
{
	initial_shields_ = shields_ = target->getShields();
	initial_hp_ = hp_ = target->getHitPoints();
	shield_armor_ = unit_shields(target->getType(), target->getPlayer());
	armor_ = unit_armor(target->getType(), target->getPlayer());
	size_ = target->getType().size();
	flying_ = target->isFlying();
}

void DamageModel::apply_damage(Unit source_unit,int damage_divisor)
{
	int bunker_marines_loaded = 0;
	if (source_unit->getType() == UnitTypes::Terran_Bunker) {
		bunker_marines_loaded = information_manager.bunker_marines_loaded(source_unit);
	}
	apply_damage(source_unit->getType(), source_unit->getPlayer(), bunker_marines_loaded, damage_divisor);
}

void DamageModel::apply_damage(UnitType source_type,Player source_player,int source_bunker_marines_loaded,int damage_divisor)
{
	WeaponType weapon_type;
	if (source_type == UnitTypes::Protoss_Reaver && !flying_) {
		weapon_type = WeaponTypes::Scarab;
	} else if (source_type == UnitTypes::Protoss_Carrier) {
		weapon_type = WeaponTypes::Pulse_Cannon;
	} else if (source_type == UnitTypes::Terran_Bunker) {
		weapon_type = WeaponTypes::Gauss_Rifle;
	} else {
		weapon_type = flying_ ? source_type.airWeapon() : source_type.groundWeapon();
	}
	if (weapon_type != WeaponTypes::None && weapon_type != WeaponTypes::Unknown) {
		double original_damage = weapon_damage(weapon_type, source_player);
		if (damage_divisor > 1) {
			original_damage /= damage_divisor;
		}
		int hits = 1;
		if (source_type != UnitTypes::Protoss_Reaver && source_type != UnitTypes::Protoss_Carrier && source_type != UnitTypes::Terran_Bunker) {
			hits = flying_ ? source_type.maxAirHits() : source_type.maxGroundHits();
		}
		if (source_type == UnitTypes::Protoss_Carrier) {
			hits = information_manager.upgrade_level(source_player, UpgradeTypes::Carrier_Capacity) ? 8 : 4;
		}
		if (source_type == UnitTypes::Terran_Bunker) {
			hits = source_bunker_marines_loaded;
		}
		for (int i = 0; i < hits; i++) {
			double damage = original_damage;
			double shield_damage = 0.0;
			if (shields_ >= 1.0) {
				if (weapon_type.damageType() != DamageTypes::Ignore_Armor) {
					damage = std::max(0.5, damage - shield_armor_);
				}
				shield_damage = damage;
				if (shield_damage > shields_) {
					shield_damage = shields_;
				}
				damage -= shield_damage;
				shields_ -= shield_damage;
			}
			if (weapon_type.damageType() != DamageTypes::Ignore_Armor) {
				damage = std::max(0.0, damage - armor_);
			}
			damage *= damage_type_factor(weapon_type.damageType(), size_);
			if (shield_damage == 0.0 && damage < 0.5) {
				damage = 0.5;
			}
			hp_ -= damage;
		}
	}
}

void DamageModel::apply_damage_for_frames(UnitType source_type,Player source_player,int frames,int source_bunker_marines_loaded)
{
	WeaponType weapon_type = flying_ ? source_type.airWeapon() : source_type.groundWeapon();
	if (weapon_type != WeaponTypes::None && weapon_type != WeaponTypes::Unknown) {
		int cooldown;
		if (source_type == UnitTypes::Protoss_Reaver) {
			cooldown = 60;
		} else if (source_type == UnitTypes::Protoss_Carrier) {
			cooldown = 37;
		} else {
			cooldown = weapon_cooldown(weapon_type, source_player);
		}
		int count = (frames + cooldown - 1) / cooldown;
		for (int i = 0; i < count; i++) {
			apply_damage(source_type, source_player, source_bunker_marines_loaded);
		}
	}
}

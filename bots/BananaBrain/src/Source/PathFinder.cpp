#include "BananaBrain.h"

void PathFinder::init()
{
	init_ramps();
	
	// Workaround for CarthageIII.
	if (Broodwar->mapHash() == "fe0a18342c797203d585dc2a3241f3974b765e0b") {
		std::vector<const BWEM::ChokePoint*> chokepoints;
		for (auto& area : bwem_map.Areas()) {
			for (auto& cp : area.ChokePoints()) {
				if (!cp->Blocked() && cp->GetAreas().first == &area) {
					if (chokepoint_center(cp) == Position(1756, 1812)) {
						chokepoints.push_back(cp);
					}
				}
			}
		}
		if (!chokepoints.empty()) {
			bwem_map.BlockChokepoints(chokepoints);
		}
	}
}

void PathFinder::init_ramps()
{
	std::set<const BWEM::ChokePoint*> chokepoints;
	for (auto& area : bwem_map.Areas()) {
		for (auto& chokepoint : area.ChokePoints()) {
			chokepoints.insert(chokepoint);
		}
	}
	
	for (const BWEM::ChokePoint* chokepoint : chokepoints) {
		if (chokepoint_width(chokepoint) < 128) {
			int height1 = Broodwar->getGroundHeight(FastTilePosition(chokepoint->GetAreas().first->Top()));
			int height2 = Broodwar->getGroundHeight(FastTilePosition(chokepoint->GetAreas().second->Top()));
			if (height1 != height2) {
				FastPosition center = chokepoint_center(chokepoint);
				int low_ground_height = std::min(height1, height2);
				int high_ground_height = std::max(height1, height2);
				
				std::set<FastTilePosition> low_ground_tiles;
				for (int y = -5; y <= 5; y++) {
					for (int x = -5; x <= 5; x++) {
						FastTilePosition tile_position = FastTilePosition(center) + FastTilePosition(x, y);
						if (tile_position.isValid() &&
							Broodwar->getGroundHeight(tile_position) == low_ground_height &&
							(Broodwar->getGroundHeight(tile_position + FastTilePosition(1, 0)) == low_ground_height ||
							 Broodwar->getGroundHeight(tile_position + FastTilePosition(-1, 0)) == low_ground_height ||
							 Broodwar->getGroundHeight(tile_position + FastTilePosition(0, 1)) == low_ground_height ||
							 Broodwar->getGroundHeight(tile_position + FastTilePosition(0, -1)) == low_ground_height)) {
							low_ground_tiles.insert(tile_position);
						}
					}
				}
				
				Gap gap(chokepoint);
				FastTilePosition best_tile_position;
				int best_distance = INT_MAX;
				for (int y = -64; y <= 64; y++) {
					for (int x = -64; x <= 64; x++) {
						FastPosition position = center + FastPosition(x, y);
						if (!position.isValid()) continue;
						int xx = position.x % 32;
						int yy = position.y % 32;
						if (xx > 0 && xx < 31 && yy > 0 && yy < 31) continue;
						if (!Broodwar->isWalkable(FastWalkPosition(position))) continue;
						FastTilePosition tile_position(position);
						if (Broodwar->getGroundHeight(tile_position) != high_ground_height) continue;
						if (!contains(low_ground_tiles, tile_position + FastTilePosition(1, 0)) &&
							!contains(low_ground_tiles, tile_position + FastTilePosition(-1, 0)) &&
							!contains(low_ground_tiles, tile_position + FastTilePosition(0, 1)) &&
							!contains(low_ground_tiles, tile_position + FastTilePosition(0, -1))) continue;
						if (std::abs(gap.a().getDistance(position) - gap.b().getDistance(position)) > 2.0) continue;
						int distance = int(position.getDistance(center) + 0.5);
						if (distance < best_distance) {
							best_tile_position = tile_position;
							best_distance = distance;
						}
					}
				}
				
				if (best_tile_position.isValid()) {
					ramp_high_ground_[chokepoint] = best_tile_position;
				}
			}
		}
	}
}

Position PathFinder::first_common_path_position(Position position,std::vector<Position> destinations)
{
	if (destinations.empty()) return position;
	
	std::vector<const BWEM::CPPath*> paths;
	for (Position destination : destinations) {
		int distance = -1;
		const BWEM::CPPath& path = bwem_map.GetPath(position, destination, &distance);
		if (distance < 0) return position;
		paths.push_back(&path);
	}
	
	Position result = position;
	const BWEM::CPPath& path0 = *paths[0];
	for (size_t i = 0; i < path0.size(); i++) {
		bool match = true;
		for (size_t j = 1; j < paths.size(); j++) {
			const BWEM::CPPath& path = *paths[j];
			if (i >= path.size() || path0[i] != path[i]) {
				match = false;
				break;
			}
		}
		if (match) {
			const BWEM::ChokePoint* cp = path0[i];
			result = center_position(cp->Center());
		} else {
			break;
		}
	}
	return result;
}

bool PathFinder::execute_path(Unit unit,Position position,std::function<void(void)> command)
{
	// @
	/*for (auto [chokepoint,tile_position] : ramp_high_ground_) {
		Broodwar->drawCircleMap(center_position(tile_position), 16, Colors::Yellow, true);
		Broodwar->drawLineMap(chokepoint_center(chokepoint), center_position(tile_position), Colors::Yellow);
		for (int dy = -8; dy <= 8; dy++) {
			for (int dx = -8; dx <= 8; dx++) {
				FastTilePosition t = tile_position + FastTilePosition(dx, dy);
				if (t.isValid()) {
					//if (Broodwar->getGroundHeight(t) >= Broodwar->getGroundHeight(tile_position)) {
					//	Broodwar->drawCircleMap(center_position(t), 8, Colors::Blue, true);
					//}
					Broodwar->drawTextMap(center_position(t), "%d", Broodwar->getGroundHeight(t));
				}
			}
		}
	}*/
	// /@
	const auto on_low_ground_side = [unit](const BWEM::ChokePoint* choke){
		const BWEM::Area* area = area_at(unit->getPosition());
		if (area == nullptr) return false;
		int height1 = Broodwar->getGroundHeight(FastTilePosition(choke->GetAreas().first->Top()));
		int height2 = Broodwar->getGroundHeight(FastTilePosition(choke->GetAreas().second->Top()));
		int low_ground_height = std::min(height1, height2);
		return (low_ground_height == Broodwar->getGroundHeight(FastTilePosition(area->Top())));
	};
	
	const auto visible_around_tile = [](FastTilePosition center_tile_position){
		for (int dy = -1; dy <= 1; dy++) {
			for (int dx = -1; dx <= 1; dx++) {
				FastTilePosition tile_position = center_tile_position + FastTilePosition(dx, dy);
				if (tile_position.isValid() && !Broodwar->isVisible(tile_position)) {
					return false;
				}
			}
		}
		return true;
	};
	
	if (!unit->isFlying() &&
		has_area(WalkPosition(unit->getPosition())) &&
		has_area(WalkPosition(position))) {
		int distance = -1;
		const BWEM::CPPath& path = bwem_map.GetPath(unit->getPosition(), position, &distance);
		if (distance < 0) return false;
		
		for (auto& choke : path) {
			auto it = ramp_high_ground_.find(choke);
			if (it != ramp_high_ground_.end() &&
				on_low_ground_side(choke) &&
				(unit->getDistance(position) > 320 || !Broodwar->isVisible(TilePosition(position))) &&
				!visible_around_tile(it->second)) {
				unit_move(unit, center_position(it->second));
				return true;
			}
			
			if (distance_to_chokepoint(unit->getPosition(), choke) > 320) {
				unit_move(unit, center_position(choke->Center()));
				return true;
			}
		}
	}
	
	command();
	return true;
}

int PathFinder::distance_to_chokepoint(Position position,const BWEM::ChokePoint* choke)
{
	auto [end1,end2] = chokepoint_ends(choke);
	return point_to_line_segment_distance(center_position(end1), center_position(end2), position);
}

void PathFinder::close_small_chokepoints_if_needed()
{
	if (!small_chokepoints_closed_ &&
		(training_manager.unit_count_completed(UnitTypes::Protoss_Dragoon) > 0 ||
		 training_manager.unit_count_completed(UnitTypes::Protoss_Reaver) > 0 ||
		 training_manager.unit_count_completed(UnitTypes::Protoss_Archon) > 0 ||
		 training_manager.unit_count_completed(UnitTypes::Protoss_Dark_Archon) > 0 ||
		 training_manager.unit_count_completed(UnitTypes::Terran_Vulture) > 0 ||
		 training_manager.unit_count_completed(UnitTypes::Terran_Siege_Tank_Tank_Mode) > 0 ||
		 training_manager.unit_count_completed(UnitTypes::Terran_Siege_Tank_Siege_Mode) > 0 ||
		 training_manager.unit_count_completed(UnitTypes::Terran_Goliath) > 0 ||
		 training_manager.unit_count_completed(UnitTypes::Zerg_Lurker) > 0 ||
		 training_manager.unit_count_completed(UnitTypes::Zerg_Ultralisk) > 0)) {
			std::vector<const BWEM::ChokePoint*> chokepoints;
			for (auto& area : bwem_map.Areas()) {
				for (auto& cp : area.ChokePoints()) {
					if (!cp->Blocked() && cp->GetAreas().first == &area) {
						Gap gap(cp);
						if (!gap.fits_through(UnitTypes::Protoss_Dragoon) ||
							!gap.fits_through(UnitTypes::Protoss_Reaver)) {
							chokepoints.push_back(cp);
						}
					}
				}
			}
			if (!chokepoints.empty()) {
				bwem_map.BlockChokepoints(chokepoints);
				if (configuration.draw_enabled()) {
					Broodwar << "Closed " << (int)chokepoints.size() << " chokepoints" << std::endl;
				}
			}
			small_chokepoints_closed_ = true;
		}
}

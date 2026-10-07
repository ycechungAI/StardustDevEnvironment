#include "BananaBrain.h"

namespace WallPlacement
{
	Graph::Graph(const GraphBase& graph_base,UnitType basic_combat_unit_type,UnitType tight_for_unit_type) : basic_combat_unit_type_(basic_combat_unit_type), tight_for_unit_type_(tight_for_unit_type)
	{
		add_wall_and_neutrals(graph_base);
	}
	
	void Graph::add_wall_and_neutrals(const GraphBase& graph_base)
	{
		if (graph_base.wall2_ == nullptr) {
			start_node_ = add_base_node(graph_base.base_);
			end_node_ = add_wall_node(graph_base.wall1_);
		} else {
			start_node_ = add_wall_node(graph_base.wall1_);
			end_node_ = add_wall_node(graph_base.wall2_);
			add_base_node(graph_base.base_);
		}
		add_neutral_building_nodes(graph_base.neutral_units_);
	}
	
	size_t Graph::add_wall_node(const std::vector<WalkPosition>* wall)
	{
		size_t result = nodes_.size();
		nodes_.emplace_back(wall);
		return result;
	}
	
	size_t Graph::add_base_node(const BWEM::Base* base)
	{
		size_t result = nodes_.size();
		nodes_.emplace_back(UnitTypes::Protoss_Nexus, base->Location());
		return result;
	}
	
	void Graph::add_neutral_building_nodes(const std::vector<const InformationUnit*>& neutral_units)
	{
		for (auto& information_unit : neutral_units) {
			nodes_.emplace_back(information_unit->type, information_unit->tile_position());
		}
	}
	
	void Graph::add_neighbours()
	{
		for (size_t i = 0; i < nodes_.size(); i++) {
			for (size_t j = i + 1; j < nodes_.size(); j++) {
				Gap gap = nodes_[i].determine_gap(nodes_[j]);
				if (gap.is_valid() &&
					(nodes_[i].terrain() == nullptr || nodes_[j].terrain() == nullptr) &&
					gap.block_positions(basic_combat_unit_type_, tight_for_unit_type_).size() <= 2) {
					nodes_[i].add_neighbour(j, gap);
					nodes_[j].add_neighbour(i, gap);
				}
			}
		}
	}
	
	bool Graph::determine_gaps()
	{
		struct Vertex {
			int distance;
			size_t predecessor;
			size_t index;
			bool in_Q;
		};
		
		struct VertexOrder {
			bool operator()(const Vertex* a,const Vertex* b) const {
				return std::make_tuple(a->distance, a->index) < std::make_tuple(b->distance, b->index);
			}
		};
		
		std::vector<Vertex> d(nodes_.size());
		for (size_t i = 0; i < d.size(); i++) {
			d[i].distance = INT_MAX;
			d[i].index = i;
			d[i].in_Q = true;
		}
		d[start_node_].distance = 0;
		
		std::set<Vertex*,VertexOrder> Q;
		for (size_t i = 0; i < d.size(); i++) Q.insert(&d[i]);
		
		while (true) {
			auto it = Q.begin();
			size_t u = (*it)->index;
			Q.erase(it);
			if (u == end_node_ || d[u].distance == INT_MAX) break;
			d[u].in_Q = false;
			
			for (auto& ve : nodes_[u].neighbours()) {
				size_t v = ve.first;
				if (d[v].in_Q) {
					int alt_distance = d[u].distance + (int)ve.second.block_positions(basic_combat_unit_type_, tight_for_unit_type_).size();
					if (alt_distance < d[v].distance) {
						Q.erase(&d[v]);
						d[v].distance = alt_distance;
						d[v].predecessor = u;
						Q.insert(&d[v]);
					}
				}
			}
		}
		
		if (d[end_node_].distance == INT_MAX) return false;
		
		size_t current_index = end_node_;
		size_t gap_unit_count = 0;
		while (current_index != start_node_) {
			size_t predecessor_index = d[current_index].predecessor;
			const Gap& gap = nodes_[current_index].neighbours().at(predecessor_index);
			gaps_.push_back(gap);
			gap_unit_count += gap.block_positions(basic_combat_unit_type_, tight_for_unit_type_).size();
			current_index = predecessor_index;
		}
		gap_unit_count_ = gap_unit_count;
		
		return true;
	}
	
	bool Graph::passable()
	{
		std::set<size_t> visited;
		visited.insert(start_node_);
		return passable(visited, start_node_);
	}
	
	bool Graph::passable(std::set<size_t>& visited,size_t current_index)
	{
		if (current_index == end_node_) return false;
		const Node& current_node = nodes_[current_index];
		for (auto& entry : current_node.neighbours()) {
			size_t next_index = entry.first;
			if (visited.count(next_index) == 0) {
				const Gap& gap = entry.second;
				if (!gap.fits_through(UnitTypes::Protoss_Dragoon)) {
					visited.insert(next_index);
					if (!passable(visited, next_index)) return false;
					visited.erase(next_index);
				}
			}
		}
		return true;
	}
	
	Gap Graph::Node::determine_gap(const Node& node) const
	{
		if (terrain_ == nullptr) {
			if (node.terrain_ == nullptr) {
				return determine_gap(unit_type_, tile_position_, node.unit_type_, node.tile_position_);
			} else {
				return determine_gap(unit_type_, node.tile_position_, node.terrain_);
			}
		} else {
			return determine_gap(node.unit_type_, node.tile_position_, terrain_);
		}
	}
	
	Gap Graph::Node::determine_gap(UnitType type,TilePosition position,const std::vector<WalkPosition>* wall_positions) const
	{
		Rect rect1(type, position);
		Gap result;
		int min_size = INT_MAX;
		for (auto& walk_position : *wall_positions) {
			Rect rect2(walk_position);
			Gap gap(rect1, rect2);
			if (gap.is_valid() && gap.size() < min_size) {
				min_size = gap.size();
				result = gap;
			}
		}
		return result;
	}
	
	Gap Graph::Node::determine_gap(UnitType type1,TilePosition position1,UnitType type2,TilePosition position2) const
	{
		Rect rect1(type1, position1);
		Rect rect2(type2, position2);
		return Gap(rect1, rect2);
	}
	
	FFEGraph::FFEGraph(const GraphBase& graph_base,const FFEPosition& ffe_position) : Graph(graph_base, UnitTypes::Protoss_Zealot, UnitTypes::Zerg_Zergling)
	{
		add_ffe_position(ffe_position);
		add_neighbours();
		if (passable() && determine_gaps()) valid_ = true;
	}
	
	void FFEGraph::add_ffe_position(const FFEPosition& ffe_position)
	{
		if (ffe_position.gateway_position.isValid()) nodes_.emplace_back(UnitTypes::Protoss_Gateway, ffe_position.gateway_position);
		if (ffe_position.forge_position.isValid()) nodes_.emplace_back(UnitTypes::Protoss_Forge, ffe_position.forge_position);
		if (ffe_position.pylon_position.isValid()) nodes_.emplace_back(UnitTypes::Protoss_Pylon, ffe_position.pylon_position);
		for (auto& cannon_position : ffe_position.cannon_positions) {
			nodes_.emplace_back(UnitTypes::Protoss_Photon_Cannon, cannon_position);
		}
	}
	
	TerranWallGraph::TerranWallGraph(const GraphBase& graph_base,const TerranWallPosition& terran_wall_position,UnitType tight_for_unit_type) : Graph(graph_base, UnitTypes::Terran_Marine, tight_for_unit_type)
	{
		add_terran_wall_position(terran_wall_position);
		add_neighbours();
		if (determine_gaps() && gap_unit_count_ == 0) valid_ = true;
	}
	
	void TerranWallGraph::add_terran_wall_position(const TerranWallPosition& terran_wall_position)
	{
		if (terran_wall_position.barracks_position.isValid()) nodes_.emplace_back(UnitTypes::Terran_Barracks, terran_wall_position.barracks_position);
		if (terran_wall_position.first_supply_depot_position.isValid()) nodes_.emplace_back(UnitTypes::Terran_Supply_Depot, terran_wall_position.first_supply_depot_position);
		if (terran_wall_position.second_supply_depot_position.isValid()) nodes_.emplace_back(UnitTypes::Terran_Supply_Depot, terran_wall_position.second_supply_depot_position);
		if (terran_wall_position.factory_position.isValid()) nodes_.emplace_back(UnitTypes::Terran_Factory, terran_wall_position.factory_position);
	}
	
	TerranNaturalWallGraph::TerranNaturalWallGraph(const GraphBase& graph_base,const TerranNaturalWallPosition& terran_natural_wall_position,UnitType tight_for_unit_type) : Graph(graph_base, UnitTypes::Terran_Marine, tight_for_unit_type)
	{
		add_terran_natural_wall_position(terran_natural_wall_position);
		add_neighbours();
		if (determine_gaps()) valid_ = true;
	}
	
	void TerranNaturalWallGraph::add_terran_natural_wall_position(const TerranNaturalWallPosition& terran_natural_wall_position)
	{
		if (terran_natural_wall_position.barracks_position.isValid()) nodes_.emplace_back(UnitTypes::Terran_Barracks, terran_natural_wall_position.barracks_position);
		if (terran_natural_wall_position.first_supply_depot_position.isValid()) nodes_.emplace_back(UnitTypes::Terran_Supply_Depot, terran_natural_wall_position.first_supply_depot_position);
		if (terran_natural_wall_position.second_supply_depot_position.isValid()) nodes_.emplace_back(UnitTypes::Terran_Supply_Depot, terran_natural_wall_position.second_supply_depot_position);
		if (terran_natural_wall_position.bunker_position.isValid()) nodes_.emplace_back(UnitTypes::Terran_Bunker, terran_natural_wall_position.bunker_position);
	}
	
	MultipleChokePlacer::MultipleChokePlacer(const Chokepoints& chokepoints)
	{
		if (chokepoints.inside_chokepoints.size() >= 1 && chokepoints.outside_chokepoints.size() >= 1) {
			create_nodes(chokepoints);
			std::stable_sort(nodes_.begin(), nodes_.end());
			determine_nonwall_node_indexes();
			determine_wall_position();
		}
	}
	
	void MultipleChokePlacer::create_nodes(const Chokepoints& chokepoints)
	{
		const BWEM::Base* base = chokepoints.base;
		const BWEM::Area* area = base->GetArea();
		
		for (auto& mineral : base->Minerals()) {
			nodes_.push_back(Node { NodeType::Mineral, calculate_angle(base, mineral->Pos()) });
		}
		for (auto& chokepoint : chokepoints.inside_chokepoints) {
			nodes_.push_back(Node { NodeType::InsideChoke, calculate_angle(base, chokepoint_center(chokepoint)) });
		}
		for (auto& chokepoint : chokepoints.outside_chokepoints) {
			nodes_.push_back(Node { NodeType::OutsideChoke, calculate_angle(base, chokepoint_center(chokepoint)) });
		}
		
		FastWalkPosition top_left = FastWalkPosition(area->TopLeft()) - FastWalkPosition(1, 1);
		FastWalkPosition bottom_right = FastWalkPosition(area->BottomRight()) + FastWalkPosition(4, 4);
		for (int y = top_left.y; y <= bottom_right.y; y++) {
			for (int x = top_left.x; x <= bottom_right.x; x++) {
				FastWalkPosition walk_position(x, y);
				if (wall_of_areas(walk_position, area, area)) {
					nodes_.push_back(Node { NodeType::Wall, calculate_angle(base, center_position(walk_position)), walk_position });
				}
			}
		}
	}
	
	void MultipleChokePlacer::determine_nonwall_node_indexes()
	{
		for (size_t i = 0; i < nodes_.size(); i++) {
			if (nodes_[i].type != NodeType::Wall) nonwall_node_indexes_.push_back(i);
		}
		
		// Repeat first index, to make it easier to look at all neighbouring pairs
		nonwall_node_indexes_.push_back(nonwall_node_indexes_[0]);
	}
	
	void MultipleChokePlacer::determine_wall_position()
	{
		for (size_t i = 0; i < nonwall_node_indexes_.size() - 1; i++) {
			const Node& node1 = nodes_[nonwall_node_indexes_[i]];
			const Node& node2 = nodes_[nonwall_node_indexes_[i + 1]];
			if ((node1.type == NodeType::InsideChoke && node2.type == NodeType::OutsideChoke) ||
				(node1.type == NodeType::OutsideChoke && node2.type == NodeType::InsideChoke)) {
				wall_position_.push_back(std::make_pair(nonwall_node_indexes_[i], nonwall_node_indexes_[i + 1]));
			}
		}
		
		if (wall_position_.size() == 1) {
			size_t i = wall_position_[0].first + 1;
			if (i == nodes_.size()) i = 0;
			size_t end = wall_position_[0].second;
			while (i != end) {
				wall_walk_positions_.push_back(nodes_[i].wall_position);
				i++;
				if (i == nodes_.size()) i = 0;
			}
			clockwise_ = (nodes_[end].type == NodeType::OutsideChoke);
		}
	}
	
	double MultipleChokePlacer::calculate_angle(const BWEM::Base* base,Position position)
	{
		Position delta = position - base->Center();
		return atan2(delta.y, delta.x);
	}
	
	bool FFEPlacer::place_ffe()
	{
		std::optional<FFEPosition> result;
		Chokepoints chokepoints = determine_natural_inside_and_outside_chokepoints();
		if (chokepoints.outside_chokepoints.size() == 1) {
			const BWEM::ChokePoint* cp = chokepoints.outside_chokepoints[0];
			if (configuration.draw_enabled()) Broodwar << "Single chokepoint found" << std::endl;
			
			WalkPosition end1_walk;
			WalkPosition end2_walk;
			std::tie(end1_walk, end2_walk) = chokepoint_ends(cp);
			
			TilePosition end1(end1_walk);
			TilePosition end2(end2_walk);
			TilePosition gateway_size1{(UnitTypes::Protoss_Gateway.tileWidth() + 1) / 2, (UnitTypes::Protoss_Gateway.tileHeight() + 1) / 2};
			TilePosition gateway_size2(2 * UnitTypes::Protoss_Gateway.tileWidth(), 2 * UnitTypes::Protoss_Gateway.tileHeight());
			for (TilePosition gateway_size : { gateway_size1, gateway_size2 }) {
				TilePosition top_left = TilePosition(std::min(end1.x, end2.x), std::min(end1.y, end2.y)) - gateway_size;
				TilePosition bottom_right = TilePosition(std::max(end1.x, end2.x), std::max(end1.y, end2.y)) + gateway_size;
				top_left.makeValid();
				bottom_right.makeValid();
				
				TilePosition forge_size{UnitTypes::Protoss_Forge.tileWidth(),UnitTypes::Protoss_Forge.tileHeight()};
				auto [wall1,wall2] = determine_walls(top_left - forge_size, bottom_right + forge_size, cp);
				
				// @
				/*for (auto& w : wall1) {
					Rect r(w);
					Broodwar->drawBoxMap(r.left, r.top, r.right, r.bottom, Colors::Red);
				 }
				 for (auto& w : wall2) {
					Rect r(w);
					Broodwar->drawBoxMap(r.left, r.top, r.right, r.bottom, Colors::Green);
				 }*/
				// /@
				
				TileGrid<bool> tiles = determine_tiles_around_chokepoint(cp, chokepoints.base);
				std::vector<const InformationUnit*> neutral_units = determine_neutral_units(top_left, bottom_right);
				
				// @
				/*Broodwar->drawCircleMap(chokepoints.base->Center(), 20, Colors::White, true);
				 Broodwar->drawCircleMap(chokepoint_center(cp), 20, Colors::Green, true);
				 Broodwar->drawBoxMap(Position(top_left), Position(bottom_right) + Position(31, 31), Colors::White);
				for (int y = 0; y < Broodwar->mapHeight(); y++) {
					for (int x = 0; x < Broodwar->mapWidth(); x++) {
						FastTilePosition tile_position(x, y);
						if (tiles[tile_position]) {
							FastPosition position(tile_position);
							Broodwar->drawLineMap(position, position + FastPosition(31, 31), Colors::Red);
							Broodwar->drawLineMap(position + FastPosition(0, 31), position + FastPosition(31, 0), Colors::Red);
						}
					}
				}*/
				// /@
				
				if (configuration.draw_enabled()) Broodwar << "#neutrals: " << (int)neutral_units.size() << std::endl;
				
				GraphBase graph_base(chokepoints.base, &wall1, &wall2, neutral_units);
				
				result = place_using_graph_base(tiles, graph_base, top_left, bottom_right);
				if (result) break;
			}
		}
		
		if (!result && chokepoints.inside_chokepoints.size() >= 1 && chokepoints.outside_chokepoints.size() >= 1) {
			MultipleChokePlacer multiple_choke_placer(chokepoints);
			if (!multiple_choke_placer.wall_walk_positions().empty()) {
				if (configuration.draw_enabled()) Broodwar << "Multiple chokepoint wall found" << std::endl;
				UnitType center_type = Broodwar->self()->getRace().getResourceDepot();
				FastTilePosition top_left = chokepoints.base->Location();
				FastTilePosition bottom_right = chokepoints.base->Location() + FastTilePosition(center_type.tileWidth() - 1, center_type.tileHeight() - 1);
				
				for (WalkPosition walk_position : multiple_choke_placer.wall_walk_positions()) {
					TilePosition tile_position(walk_position);
					top_left.x = std::min(top_left.x, tile_position.x);
					top_left.y = std::min(top_left.y, tile_position.y);
					bottom_right.x = std::max(bottom_right.x, tile_position.x);
					bottom_right.y = std::max(bottom_right.y, tile_position.y);
				}
				
				TilePosition gateway_size((UnitTypes::Protoss_Gateway.tileWidth() + 1) / 2, (UnitTypes::Protoss_Gateway.tileHeight() + 1) / 2);
				top_left = top_left - gateway_size;
				top_left.makeValid();
				bottom_right = bottom_right + gateway_size;
				bottom_right.makeValid();
				
				TileGrid<bool> tiles = determine_tiles_in_range(top_left, bottom_right, chokepoints.base);
				std::vector<const InformationUnit*> neutral_units = determine_neutral_units(top_left, bottom_right);
				
				if (configuration.draw_enabled()) Broodwar << "#neutrals: " << (int)neutral_units.size() << std::endl;
				
				GraphBase graph_base(chokepoints.base, &multiple_choke_placer.wall_walk_positions(), neutral_units);
				
				result = place_using_graph_base(tiles, graph_base, top_left, bottom_right);
				if (result) {
					result->multichoke_wall = true;
					result->multichoke_clockwise = multiple_choke_placer.clockwise();
				}
			}
		}
		
		if (configuration.draw_enabled() && result) Broodwar << "FFE positioning found" << std::endl;
		
		if (result) building_placement_manager.apply_specific_positions(result.value());
		return result.has_value();
	}
	
	std::optional<FFEPosition> FFEPlacer::place_using_graph_base(const TileGrid<bool>& tiles,const GraphBase& graph_base,FastTilePosition top_left,FastTilePosition bottom_right)
	{
		std::vector<FFEPosition> ffe_positions = place_gateway_forge(tiles, top_left, bottom_right, graph_base);
		
		struct FFEPositionOrder {
			bool operator()(const FFEPosition& a,const FFEPosition& b) const {
				return (std::make_tuple(a.gap_unit_count, a.pylon_part_of_wall, a.box_size, a.gateway_position, a.forge_position, a.pylon_position) <
						std::make_tuple(b.gap_unit_count, b.pylon_part_of_wall, b.box_size, b.gateway_position, b.forge_position, b.pylon_position));
			}
		};
		std::sort(ffe_positions.begin(), ffe_positions.end(), FFEPositionOrder());
		
		// @
		if (configuration.draw_enabled() && !ffe_positions.empty()) {
			Broodwar << "#gap units: " << (int)ffe_positions[0].gap_unit_count << ", #solutions: " << (int)ffe_positions.size() << std::endl;
			/*for (auto& gap : ffe_positions[0].gaps) {
			 gap.draw(Colors::Blue);
				}*/
		}
		// /@
		
		std::optional<FFEPosition> result;
		for (auto& ffe_position : ffe_positions) {
			place_pylon_and_cannons(ffe_position, tiles, graph_base);
			if (ffe_position.pylon_position.isValid()) {
				result = ffe_position;
				break;
			}
		}
		return result;
	}
	
	std::vector<FFEPosition> FFEPlacer::place_gateway_forge(const TileGrid<bool>& in_tiles,TilePosition top_left,TilePosition bottom_right,const GraphBase& graph_base)
	{
		std::vector<FFEPosition> result;
		TileGrid<bool> tiles(in_tiles);
		
		for (int y = top_left.y; y <= bottom_right.y + 1 - UnitTypes::Protoss_Gateway.tileHeight(); y++) {
			for (int x = top_left.x; x <= bottom_right.x + 1 - UnitTypes::Protoss_Gateway.tileWidth(); x++) {
				TilePosition gateway_position(x, y);
				if (can_place_building_at(tiles, UnitTypes::Protoss_Gateway, gateway_position)) {
					set_building_at_tile_grid(tiles, UnitTypes::Protoss_Gateway, gateway_position, false);
					for (TilePosition forge_position : determine_forge_positions(tiles, gateway_position)) {
						set_building_at_tile_grid(tiles, UnitTypes::Protoss_Forge, forge_position, false);
						
						FFEPosition ffe_position;
						ffe_position.base = graph_base.base();
						ffe_position.gateway_position = gateway_position;
						ffe_position.forge_position = forge_position;
						FFEGraph graph(graph_base, ffe_position);
						if (graph.is_valid()) {
							ffe_position.gaps = graph.gaps();
							ffe_position.gap_unit_count = graph.gap_unit_count();
							ffe_position.box_size = calculate_box_size(ffe_position);
							result.push_back(ffe_position);
						}
						
						set_building_at_tile_grid(tiles, UnitTypes::Protoss_Forge, forge_position, true);
					}
					set_building_at_tile_grid(tiles, UnitTypes::Protoss_Gateway, gateway_position, true);
				}
			}
		}
		
		for (int y = top_left.y; y <= bottom_right.y + 1 - UnitTypes::Protoss_Pylon.tileHeight(); y++) {
			for (int x = top_left.x; x <= bottom_right.x + 1 - UnitTypes::Protoss_Pylon.tileWidth(); x++) {
				TilePosition pylon_position(x, y);
				if (can_place_building_at(tiles, UnitTypes::Protoss_Pylon, pylon_position)) {
					set_building_at_tile_grid(tiles, UnitTypes::Protoss_Pylon, pylon_position, false);
					for (TilePosition forge_position : determine_forge_positions_near_pylon(tiles, pylon_position)) {
						set_building_at_tile_grid(tiles, UnitTypes::Protoss_Forge, forge_position, false);
						for (TilePosition gateway_position : determine_gateway_positions_above_forge(tiles, pylon_position, forge_position)) {
							set_building_at_tile_grid(tiles, UnitTypes::Protoss_Gateway, gateway_position, false);
							
							FFEPosition ffe_position;
							ffe_position.base = graph_base.base();
							ffe_position.pylon_position = pylon_position;
							ffe_position.gateway_position = gateway_position;
							ffe_position.forge_position = forge_position;
							ffe_position.pylon_part_of_wall = true;
							FFEGraph graph(graph_base, ffe_position);
							if (graph.is_valid()) {
								ffe_position.gaps = graph.gaps();
								ffe_position.gap_unit_count = graph.gap_unit_count();
								ffe_position.box_size = calculate_box_size(ffe_position);
								result.push_back(ffe_position);
							}
							
							set_building_at_tile_grid(tiles, UnitTypes::Protoss_Gateway, gateway_position, true);
						}
						set_building_at_tile_grid(tiles, UnitTypes::Protoss_Forge, forge_position, true);
					}
					set_building_at_tile_grid(tiles, UnitTypes::Protoss_Pylon, pylon_position, true);
				}
			}
		}
		
		return result;
	}
	
	int FFEPlacer::calculate_box_size(FFEPosition& ffe_position)
	{
		int left = std::min(ffe_position.gateway_position.x, ffe_position.forge_position.x);
		int top = std::min(ffe_position.gateway_position.y, ffe_position.forge_position.y);
		int right = std::max(ffe_position.gateway_position.x + UnitTypes::Protoss_Gateway.tileWidth(), ffe_position.forge_position.x + UnitTypes::Protoss_Forge.tileWidth());
		int bottom = std::max(ffe_position.gateway_position.y + UnitTypes::Protoss_Gateway.tileHeight(), ffe_position.forge_position.y + UnitTypes::Protoss_Forge.tileHeight());
		if (ffe_position.pylon_position.isValid()) {
			left = std::min(left, ffe_position.pylon_position.x);
			top = std::min(top, ffe_position.pylon_position.y);
			right = std::max(right, ffe_position.pylon_position.x + UnitTypes::Protoss_Pylon.tileWidth());
			bottom = std::max(bottom, ffe_position.pylon_position.y + UnitTypes::Protoss_Pylon.tileHeight());
		}
		return (right - left) * (bottom - top);
	}
	
	void FFEPlacer::place_pylon_and_cannons(FFEPosition& ffe_position,const TileGrid<bool>& in_tiles,const GraphBase& graph_base)
	{
		TileGrid<bool> tiles(in_tiles);
		set_building_at_tile_grid(tiles, UnitTypes::Protoss_Gateway, ffe_position.gateway_position, false);
		set_building_at_tile_grid(tiles, UnitTypes::Protoss_Forge, ffe_position.forge_position, false);
		if (ffe_position.pylon_position.isValid()) set_building_at_tile_grid(tiles, UnitTypes::Protoss_Pylon, ffe_position.pylon_position, false);
		
		if (!ffe_position.pylon_position.isValid()) {
			std::vector<TilePosition> potential_pylon_positions;
			
			for (int y = ffe_position.gateway_position.y - 8; y <= ffe_position.gateway_position.y + 8; y++) {
				for (int x = ffe_position.gateway_position.x - 8; x <= ffe_position.gateway_position.x + 8; x++) {
					TilePosition pylon_position(x, y);
					if (building_placement_manager.check_power(pylon_position, UnitTypes::Protoss_Gateway, ffe_position.gateway_position) &&
						building_placement_manager.check_power(pylon_position, UnitTypes::Protoss_Forge, ffe_position.forge_position) &&
						can_place_building_at(tiles, UnitTypes::Protoss_Pylon, pylon_position) &&
						check_building_inside(ffe_position, UnitTypes::Protoss_Pylon, pylon_position, graph_base.base()) &&
						check_building_neutral_unit_collision(graph_base, UnitTypes::Protoss_Pylon, pylon_position)) {
						set_building_at_tile_grid(tiles, UnitTypes::Protoss_Pylon, pylon_position, false);
						ffe_position.pylon_position = pylon_position;
						FFEGraph graph(graph_base, ffe_position);
						if (graph.is_valid()) {
							potential_pylon_positions.push_back(pylon_position);
						}
						ffe_position.pylon_position = TilePositions::None;
						set_building_at_tile_grid(tiles, UnitTypes::Protoss_Pylon, pylon_position, true);
					}
				}
			}
			
			TilePosition pylon_position = smallest_priority(potential_pylon_positions, [&ffe_position](TilePosition tile_position){
				int gateway_distance = calculate_distance(UnitTypes::Protoss_Gateway,
														  center_position_for(UnitTypes::Protoss_Gateway, ffe_position.gateway_position),
														  UnitTypes::Protoss_Pylon,
														  center_position_for(UnitTypes::Protoss_Pylon, tile_position));
				int forge_distance = calculate_distance(UnitTypes::Protoss_Forge,
														center_position_for(UnitTypes::Protoss_Forge, ffe_position.forge_position),
														UnitTypes::Protoss_Pylon,
														center_position_for(UnitTypes::Protoss_Pylon, tile_position));
				int distance = (gateway_distance + forge_distance) / 2;
				return std::make_tuple(-distance, tile_position);
			}, TilePositions::None);
			
			if (pylon_position.isValid()) {
				ffe_position.pylon_position = pylon_position;
				set_building_at_tile_grid(tiles, UnitTypes::Protoss_Pylon, ffe_position.pylon_position, false);
				place_cannons(ffe_position, tiles, graph_base);
			}
		} else {
			place_cannons(ffe_position, tiles, graph_base);
		}
	}
	
	void FFEPlacer::place_cannons(FFEPosition& ffe_position,TileGrid<bool>& tiles,const GraphBase& graph_base)
	{
		TilePosition pylon_position = ffe_position.pylon_position;
		
		for (int i = 0; i < 4; i++) {
			std::vector<TilePosition> potential_cannon_positions;
			for (int y = pylon_position.y - 8; y <= pylon_position.y + 8; y++) {
				for (int x = pylon_position.x - 8; x <= pylon_position.x + 8; x++) {
					TilePosition cannon_position(x, y);
					if (building_placement_manager.check_power(pylon_position, UnitTypes::Protoss_Photon_Cannon, cannon_position) &&
						can_place_building_at(tiles, UnitTypes::Protoss_Photon_Cannon, cannon_position) &&
						check_building_neutral_unit_collision(graph_base, UnitTypes::Protoss_Photon_Cannon, cannon_position) &&
						(graph_base.is_single_wall() || check_building_inside(ffe_position, UnitTypes::Protoss_Photon_Cannon, cannon_position, graph_base.base()))) {
						set_building_at_tile_grid(tiles, UnitTypes::Protoss_Photon_Cannon, cannon_position, false);
						ffe_position.cannon_positions.push_back(cannon_position);
						FFEGraph graph(graph_base, ffe_position);
						if (graph.is_valid()) {
							potential_cannon_positions.push_back(cannon_position);
						}
						ffe_position.cannon_positions.pop_back();
						set_building_at_tile_grid(tiles, UnitTypes::Protoss_Photon_Cannon, cannon_position, true);
					}
				}
			}
			TilePosition cannon_position;
			if (!graph_base.is_single_wall()) {
				cannon_position = smallest_priority(potential_cannon_positions, [&ffe_position](TilePosition tile_position){
					Position photon_cannon_position = center_position_for(UnitTypes::Protoss_Photon_Cannon, tile_position);
					int gateway_distance = calculate_distance(UnitTypes::Protoss_Gateway,
															  center_position_for(UnitTypes::Protoss_Gateway, ffe_position.gateway_position),
															  UnitTypes::Protoss_Photon_Cannon,
															  photon_cannon_position);
					int forge_distance = calculate_distance(UnitTypes::Protoss_Forge,
															center_position_for(UnitTypes::Protoss_Forge, ffe_position.forge_position),
															UnitTypes::Protoss_Photon_Cannon,
															photon_cannon_position);
					int distance = std::min(gateway_distance, forge_distance);
					return std::make_tuple(distance, tile_position);
				}, TilePositions::None);
			} else {
				Position base_position = graph_base.base()->Center();
				cannon_position = smallest_priority(potential_cannon_positions, [base_position](TilePosition tile_position){
					Position photon_cannon_position = center_position_for(UnitTypes::Protoss_Photon_Cannon, tile_position);
					int distance = calculate_distance(UnitTypes::Protoss_Nexus, base_position,
													  UnitTypes::Protoss_Photon_Cannon, photon_cannon_position);
					return std::make_tuple(distance, tile_position);
				}, TilePositions::None);
			}
			if (cannon_position.isValid()) {
				ffe_position.cannon_positions.push_back(cannon_position);
				set_building_at_tile_grid(tiles, UnitTypes::Protoss_Photon_Cannon, cannon_position, false);
			} else {
				ffe_position.pylon_position = TilePositions::None;
				break;
			}
		}
	}
	
	bool FFEPlacer::check_building_neutral_unit_collision(const GraphBase& graph_base,UnitType unit_type,TilePosition building_tile_position)
	{
		Rect rect(unit_type, building_tile_position);
		for (auto& neutral_unit : graph_base.neutral_units()) {
			if (!neutral_unit->type.isBuilding()) {
				Rect neutral_rect(neutral_unit->type, neutral_unit->position);
				if (rect.overlaps(neutral_rect)) {
					return false;
				}
			}
		}
		return true;
	}
	
	bool FFEPlacer::check_building_inside(const FFEPosition& ffe_position,UnitType unit_type,TilePosition building_tile_position,const BWEM::Base* base)
	{
		Position base_position = base->Center();
		int gateway_distance = calculate_distance(UnitTypes::Protoss_Gateway,
												  center_position_for(UnitTypes::Protoss_Gateway, ffe_position.gateway_position),
												  UnitTypes::Protoss_Nexus,
												  base_position);
		int forge_distance = calculate_distance(UnitTypes::Protoss_Forge,
												center_position_for(UnitTypes::Protoss_Forge, ffe_position.forge_position),
												UnitTypes::Protoss_Nexus,
												base_position);
		int building_distance = calculate_distance(unit_type,
												   center_position_for(unit_type, building_tile_position),
												   UnitTypes::Protoss_Nexus,
												   base_position);
		return building_distance <= gateway_distance && building_distance <= forge_distance;
	}
	
	std::vector<TilePosition> FFEPlacer::determine_forge_positions(const TileGrid<bool>& tiles,TilePosition gateway_position)
	{
		std::vector<TilePosition> all_positions;
		
		int x1 = gateway_position.x - UnitTypes::Protoss_Forge.tileWidth();
		int y1 = gateway_position.y - UnitTypes::Protoss_Forge.tileHeight();
		int x2 = gateway_position.x + UnitTypes::Protoss_Gateway.tileWidth();
		int y2 = gateway_position.y + UnitTypes::Protoss_Gateway.tileHeight();
		
		for (int x = x1 + 1; x <= x2 - 1; x++) {
			all_positions.push_back(TilePosition(x, y1));
			all_positions.push_back(TilePosition(x, y2));
			all_positions.push_back(TilePosition(x, y1 - 1));
			all_positions.push_back(TilePosition(x, y2 + 1));
		}
		for (int y = y1 + 1; y <= y2 - 1; y++) {
			all_positions.push_back(TilePosition(x1, y));
			all_positions.push_back(TilePosition(x2, y));
			all_positions.push_back(TilePosition(x1 - 1, y));
			all_positions.push_back(TilePosition(x2 + 1, y));
		}
		
		std::vector<TilePosition> result;
		for (TilePosition tile_position : all_positions) {
			if (can_place_building_at(tiles, UnitTypes::Protoss_Forge, tile_position)) {
				result.push_back(tile_position);
			}
		}
		
		return result;
	}
	
	std::vector<TilePosition> FFEPlacer::determine_forge_positions_near_pylon(const TileGrid<bool>& tiles,TilePosition pylon_position)
	{
		std::vector<TilePosition> result;
		
		int x1 = pylon_position.x - UnitTypes::Protoss_Forge.tileWidth() + 1;
		int x2 = pylon_position.x + UnitTypes::Protoss_Pylon.tileWidth() - 1;
		int y = pylon_position.y - UnitTypes::Protoss_Forge.tileHeight();
		
		for (int x = x1; x <= x2; x++) {
			TilePosition tile_position(x, y);
			if (can_place_building_at(tiles, UnitTypes::Protoss_Forge, tile_position) &&
				building_placement_manager.check_power(pylon_position, UnitTypes::Protoss_Forge, tile_position)) {
				result.push_back(tile_position);
			}
		}
		
		return result;
	}
	
	std::vector<TilePosition> FFEPlacer::determine_gateway_positions_above_forge(const TileGrid<bool>& tiles,TilePosition pylon_position,TilePosition forge_position)
	{
		std::vector<TilePosition> result;
		
		int x1 = forge_position.x - UnitTypes::Protoss_Gateway.tileWidth() + 1;
		int x2 = forge_position.x + UnitTypes::Protoss_Forge.tileWidth() - 1;
		int y = forge_position.y - UnitTypes::Protoss_Gateway.tileHeight();
		
		for (int x = x1; x <= x2; x++) {
			TilePosition tile_position(x, y);
			if (can_place_building_at(tiles, UnitTypes::Protoss_Gateway, tile_position) &&
				building_placement_manager.check_power(pylon_position, UnitTypes::Protoss_Gateway, tile_position)) {
				result.push_back(tile_position);
			}
		}
		
		return result;
	}
	
	Chokepoints FFEPlacer::determine_natural_inside_and_outside_chokepoints()
	{
		Chokepoints result;
		
		if (base_state.natural_base() != nullptr) {
			const BWEM::Area* start_area = base_state.start_base()->GetArea();
			const BWEM::Area* natural_area = base_state.natural_base()->GetArea();
			
			std::set<const BWEM::Area*> blocked_areas;
			blocked_areas.insert(start_area);
			blocked_areas.insert(natural_area);
			
			const BWEM::Area* wall_area = base_state.is_backdoor_natural() ? start_area : natural_area;
			result.base = base_state.is_backdoor_natural() ? base_state.start_base() : base_state.natural_base();
			
			for (auto& area : wall_area->AccessibleNeighbours()) {
				bool outside = false;
				
				if (base_state.controlled_areas().count(area) == 0) {
					std::set<const BWEM::Area*> reachable = base_state.reachable_areas(area, blocked_areas);
					outside = std::any_of(reachable.begin(), reachable.end(), [](const BWEM::Area* area){
						return std::any_of(area->Bases().begin(), area->Bases().end(), [](const BWEM::Base& base){
							return base.Starting();
						});
					});
				}
				
				if (outside) {
					for (auto& cp : wall_area->ChokePoints(area)) {
						if (!cp.Blocked()) result.outside_chokepoints.push_back(&cp);
					}
				} else {
					for (auto& cp : wall_area->ChokePoints(area)) {
						if (!cp.Blocked()) result.inside_chokepoints.push_back(&cp);
					}
				}
			}
		}
		
		return result;
	}
	
	TileGrid<bool> FFEPlacer::determine_tiles_in_range(FastTilePosition top_left,FastTilePosition bottom_right,const BWEM::Base* base)
	{
		TileGrid<bool> result;
		const BWEM::Area* area = base->GetArea();
		
		for (int y = top_left.y; y <= bottom_right.y; y++) {
			for (int x = top_left.x; x <= bottom_right.x; x++) {
				TilePosition tile_position{x, y};
				if (tile_fully_walkable_in_areas(tile_position, area, area)) result.set(tile_position, true);
			}
		}
		
		remove_base_and_neutrals(result, base);
		remove_neutral_creep(result);
		
		return result;
	}
	
	bool TerranWallPlacer::place_wall()
	{
		bool result = false;
		const BWEM::ChokePoint* chokepoint = determine_chokepoint();
		if (chokepoint != nullptr) {
			auto[end1_walk, end2_walk] = chokepoint_ends(chokepoint);
			
			TilePosition end1(end1_walk);
			TilePosition end2(end2_walk);
			
			TilePosition barracks_size{ 2 * UnitTypes::Terran_Barracks.tileWidth(), 2 * UnitTypes::Terran_Barracks.tileHeight() };
			TilePosition top_left = TilePosition(std::min(end1.x, end2.x), std::min(end1.y, end2.y)) - barracks_size;
			TilePosition bottom_right = TilePosition(std::max(end1.x, end2.x), std::max(end1.y, end2.y)) + barracks_size;
			top_left.makeValid();
			bottom_right.makeValid();
			
			TilePosition supply_depot_size{ UnitTypes::Terran_Supply_Depot.tileWidth(), UnitTypes::Terran_Supply_Depot.tileHeight() };
			auto [wall1,wall2] = determine_walls(top_left - supply_depot_size, bottom_right + supply_depot_size, chokepoint);
			
			// @
			/*for (auto& w : wall1) {
				Rect r(w);
				Broodwar->drawBoxMap(r.left, r.top, r.right, r.bottom, Colors::Red);
			 }
			 for (auto& w : wall2) {
				Rect r(w);
				Broodwar->drawBoxMap(r.left, r.top, r.right, r.bottom, Colors::Green);
			 }*/
			// /@
			
			TileGrid<bool> tiles = determine_tiles_around_chokepoint(chokepoint, base_state.start_base());
			std::vector<const InformationUnit*> neutral_units = determine_neutral_units(top_left, bottom_right);
			
			GraphBase graph_base(base_state.start_base(), &wall1, &wall2, neutral_units);
			result = place_using_graph_base(tiles, graph_base, top_left, bottom_right);
		}
		return result;
	}
	
	const BWEM::ChokePoint* TerranWallPlacer::determine_chokepoint()
	{
		const auto chokepoint_for_base = [](const BWEM::Area* start_area){
			std::set<const BWEM::Area*> blocked_areas;
			blocked_areas.insert(start_area);
			
			std::vector<const BWEM::ChokePoint*> chokepoints;
			for (auto& area : start_area->AccessibleNeighbours()) {
				bool outside = false;
				
				if (base_state.controlled_areas().count(area) == 0) {
					std::set<const BWEM::Area*> reachable = base_state.reachable_areas(area, blocked_areas);
					outside = std::any_of(reachable.begin(), reachable.end(), [](const BWEM::Area* area){
						return std::any_of(area->Bases().begin(), area->Bases().end(), [](const BWEM::Base& base){
							return base.Starting();
						});
					});
				}
				
				if (outside) {
					for (auto& cp : start_area->ChokePoints(area)) {
						if (!cp.Blocked()) chokepoints.push_back(&cp);
					}
				}
			}
			
			return (chokepoints.size() == 1) ? chokepoints[0] : nullptr;
		};
		
		const BWEM::Area* start_area = base_state.start_base()->GetArea();
		const BWEM::ChokePoint* result = chokepoint_for_base(start_area);
		if (result == nullptr) {
			const BWEM::Area* extension_area = base_state.extension_area_for_start_base(base_state.start_base());
			if (extension_area != nullptr) {
				result = chokepoint_for_base(extension_area);
			}
		}
		
		return result;
	}
	
	bool TerranWallPlacer::place_using_graph_base(const TileGrid<bool>& tiles,const GraphBase& graph_base,FastTilePosition top_left,FastTilePosition bottom_right)
	{
		std::vector<TerranWallPosition> terran_wall_positions = place_barracks_and_supply_depots(tiles, top_left, bottom_right, graph_base, false);
		if (terran_wall_positions.empty()) {
			terran_wall_positions = place_barracks_and_supply_depots(tiles, top_left, bottom_right, graph_base, true);
			std::vector<TerranWallPosition> additional_terran_wall_positions = place_barracks_and_factory(tiles, top_left, bottom_right, graph_base);
			std::copy(additional_terran_wall_positions.begin(), additional_terran_wall_positions.end(), std::back_inserter(terran_wall_positions));
		}
		
		struct TerranWallPositionOrder {
			bool operator()(const TerranWallPosition& a,const TerranWallPosition& b) const {
				return (std::make_tuple(a.factory_position.isValid(), !a.machine_shop_placable, a.first_supply_depot_position.isValid(), a.barracks_position, a.first_supply_depot_position, a.second_supply_depot_position, a.factory_position) <
						std::make_tuple(b.factory_position.isValid(), !b.machine_shop_placable, b.first_supply_depot_position.isValid(), b.barracks_position, b.first_supply_depot_position, b.second_supply_depot_position, b.factory_position));
			}
		};
		std::sort(terran_wall_positions.begin(), terran_wall_positions.end(), TerranWallPositionOrder());
		
		bool result = false;
		if (!terran_wall_positions.empty()) {
			place_stage_point(terran_wall_positions[0], graph_base);
			building_placement_manager.apply_specific_positions(terran_wall_positions[0]);
			result = true;
		}
		
		return result;
	}
	
	std::vector<TerranWallPosition> TerranWallPlacer::place_barracks_and_supply_depots(const TileGrid<bool>& in_tiles,TilePosition top_left,TilePosition bottom_right,const GraphBase& graph_base,bool place_second_depot)
	{
		std::vector<TerranWallPosition> result;
		TileGrid<bool> tiles(in_tiles);
		
		for (int y = top_left.y; y <= bottom_right.y + 1 - UnitTypes::Terran_Supply_Depot.tileHeight(); y++) {
			for (int x = top_left.x; x <= bottom_right.x + 1 - UnitTypes::Terran_Supply_Depot.tileWidth(); x++) {
				TilePosition first_supply_depot_position(x, y);
				if (can_place_building_at(tiles, UnitTypes::Terran_Supply_Depot, first_supply_depot_position)) {
					set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, first_supply_depot_position, false);
					for (TilePosition barracks_position : determine_positions_around(tiles, UnitTypes::Terran_Supply_Depot, first_supply_depot_position, UnitTypes::Terran_Barracks)) {
						if (contains(base_state.controlled_areas(), area_at(center_position_for(UnitTypes::Terran_Supply_Depot, first_supply_depot_position))) ||
							contains(base_state.controlled_areas(), area_at(center_position_for(UnitTypes::Terran_Barracks, barracks_position)))) {
							set_building_at_tile_grid(tiles, UnitTypes::Terran_Barracks, barracks_position, false);
							TerranWallPosition terran_wall_position;
							terran_wall_position.base = graph_base.base();
							terran_wall_position.barracks_position = barracks_position;
							terran_wall_position.first_supply_depot_position = first_supply_depot_position;
							if (!place_second_depot) {
								if (check_positioning(graph_base, terran_wall_position)) {
									result.push_back(terran_wall_position);
								}
							} else {
								for (TilePosition second_supply_depot_position : determine_positions_around(tiles, UnitTypes::Terran_Barracks, barracks_position, UnitTypes::Terran_Supply_Depot)) {
									set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, second_supply_depot_position, false);
									terran_wall_position.second_supply_depot_position = second_supply_depot_position;
									if (check_positioning(graph_base, terran_wall_position)) {
										result.push_back(terran_wall_position);
									}
									set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, second_supply_depot_position, true);
								}
								for (TilePosition second_supply_depot_position : determine_positions_around(tiles, UnitTypes::Terran_Supply_Depot, first_supply_depot_position, UnitTypes::Terran_Supply_Depot)) {
									set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, second_supply_depot_position, false);
									terran_wall_position.second_supply_depot_position = second_supply_depot_position;
									if (check_positioning(graph_base, terran_wall_position)) {
										result.push_back(terran_wall_position);
									}
									set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, second_supply_depot_position, true);
								}
								terran_wall_position.second_supply_depot_position = TilePositions::None;
								for (TilePosition factory_position : determine_positions_around(tiles, UnitTypes::Terran_Barracks, barracks_position, UnitTypes::Terran_Factory)) {
									set_building_at_tile_grid(tiles, UnitTypes::Terran_Factory, factory_position, false);
									terran_wall_position.factory_position = factory_position;
									terran_wall_position.machine_shop_placable = can_place_building_at(tiles, UnitTypes::Terran_Machine_Shop, TilePosition{ factory_position.x + UnitTypes::Terran_Factory.tileWidth(), factory_position.y + UnitTypes::Terran_Factory.tileHeight() - UnitTypes::Terran_Machine_Shop.tileHeight() });
									if (check_positioning(graph_base, terran_wall_position)) {
										result.push_back(terran_wall_position);
									}
									set_building_at_tile_grid(tiles, UnitTypes::Terran_Factory, factory_position, true);
								}
								for (TilePosition factory_position : determine_positions_around(tiles, UnitTypes::Terran_Supply_Depot, first_supply_depot_position, UnitTypes::Terran_Factory)) {
									set_building_at_tile_grid(tiles, UnitTypes::Terran_Factory, factory_position, false);
									terran_wall_position.factory_position = factory_position;
									terran_wall_position.machine_shop_placable = can_place_building_at(tiles, UnitTypes::Terran_Machine_Shop, TilePosition{ factory_position.x + UnitTypes::Terran_Factory.tileWidth(), factory_position.y + UnitTypes::Terran_Factory.tileHeight() - UnitTypes::Terran_Machine_Shop.tileHeight() });
									if (check_positioning(graph_base, terran_wall_position)) {
										result.push_back(terran_wall_position);
									}
									set_building_at_tile_grid(tiles, UnitTypes::Terran_Factory, factory_position, true);
								}
								terran_wall_position.factory_position = TilePositions::None;
								terran_wall_position.machine_shop_placable = true;
							}
							set_building_at_tile_grid(tiles, UnitTypes::Terran_Barracks, barracks_position, true);
						}
					}
					set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, first_supply_depot_position, true);
				}
			}
		}
		
		return result;
	}
	
	std::vector<TerranWallPosition> TerranWallPlacer::place_barracks_and_factory(const TileGrid<bool>& in_tiles,TilePosition top_left,TilePosition bottom_right,const GraphBase& graph_base)
	{
		std::vector<TerranWallPosition> result;
		TileGrid<bool> tiles(in_tiles);
		
		for (int y = top_left.y; y <= bottom_right.y + 1 - UnitTypes::Terran_Barracks.tileHeight(); y++) {
			for (int x = top_left.x; x <= bottom_right.x + 1 - UnitTypes::Terran_Barracks.tileWidth(); x++) {
				TilePosition barracks_position(x, y);
				if (can_place_building_at(tiles, UnitTypes::Terran_Barracks, barracks_position)) {
					set_building_at_tile_grid(tiles, UnitTypes::Terran_Barracks, barracks_position, false);
					TerranWallPosition terran_wall_position;
					terran_wall_position.base = graph_base.base();
					terran_wall_position.barracks_position = barracks_position;
					for (TilePosition factory_position : determine_positions_around(tiles, UnitTypes::Terran_Barracks, barracks_position, UnitTypes::Terran_Factory)) {
						if (contains(base_state.controlled_areas(), area_at(center_position_for(UnitTypes::Terran_Barracks, barracks_position))) ||
							contains(base_state.controlled_areas(), area_at(center_position_for(UnitTypes::Terran_Factory, factory_position)))) {
							set_building_at_tile_grid(tiles, UnitTypes::Terran_Factory, factory_position, false);
							terran_wall_position.factory_position = factory_position;
							terran_wall_position.machine_shop_placable = can_place_building_at(tiles, UnitTypes::Terran_Machine_Shop, TilePosition{ factory_position.x + UnitTypes::Terran_Factory.tileWidth(), factory_position.y + UnitTypes::Terran_Factory.tileHeight() - UnitTypes::Terran_Machine_Shop.tileHeight() });
							if (check_positioning(graph_base, terran_wall_position)) {
								result.push_back(terran_wall_position);
							}
							set_building_at_tile_grid(tiles, UnitTypes::Terran_Factory, factory_position, true);
						}
					}
					set_building_at_tile_grid(tiles, UnitTypes::Terran_Barracks, barracks_position, true);
				}
			}
		}
		
		return result;
	}

	void TerranWallPlacer::place_stage_point(TerranWallPosition& wall_position,const GraphBase& graph_base)
	{
		std::vector<std::pair<UnitType,TilePosition>> parts;
		parts.emplace_back(UnitTypes::Terran_Supply_Depot, wall_position.first_supply_depot_position);
		parts.emplace_back(UnitTypes::Terran_Supply_Depot, wall_position.second_supply_depot_position);
		parts.emplace_back(UnitTypes::Terran_Barracks, wall_position.barracks_position);
		parts.emplace_back(UnitTypes::Terran_Factory, wall_position.factory_position);
		int max_distance = INT_MIN;
		Rect rbase(UnitTypes::Protoss_Nexus, graph_base.base()->Location());
		for (auto& [type,tile_position] : parts) {
			if (tile_position.isValid()) {
				Rect r(type, tile_position);
				Gap gap(rbase, r);
				if (gap.is_valid()) {
					int distance = gap.size();
					if (distance > max_distance) {
						max_distance = distance;
						wall_position.stage_position = scale_line_segment(gap.b(), gap.a(), 40);
					}
				}
			}
		}
	}
	
	bool TerranWallPlacer::check_positioning(const GraphBase& graph_base,const TerranWallPosition& terran_wall_position)
	{
		bool result = false;
		if (!require_placable_machine_shop_ || terran_wall_position.machine_shop_placable) {
			TerranWallGraph graph(graph_base, terran_wall_position, tight_for_unit_type_);
			if (graph.is_valid()) {
				TerranWallPosition terran_wall_position_without_barracks = terran_wall_position;
				terran_wall_position_without_barracks.barracks_position = TilePositions::None;
				TerranWallGraph graph_without_barracks(graph_base, terran_wall_position_without_barracks, tight_for_unit_type_);
				result = !graph_without_barracks.is_valid() && graph_without_barracks.is_open();
			}
		}
		return result;
	}
	
	bool TerranNaturalWallPlacer::place_wall()
	{
		bool result = false;
		auto[chokepoint,base] = determine_chokepoint_and_base();
		if (chokepoint != nullptr) {
			auto[end1_walk, end2_walk] = chokepoint_ends(chokepoint);
			
			TilePosition end1(end1_walk);
			TilePosition end2(end2_walk);
			
			TilePosition barracks_size1 { (UnitTypes::Terran_Barracks.tileWidth() + 1) / 2, (UnitTypes::Terran_Barracks.tileHeight() + 1) / 2 };
			TilePosition barracks_size2 { 2 * UnitTypes::Terran_Barracks.tileWidth(), 2 * UnitTypes::Terran_Barracks.tileHeight() };
			for (TilePosition barracks_size : { barracks_size1, barracks_size2 }) {
				TilePosition top_left = TilePosition(std::min(end1.x, end2.x), std::min(end1.y, end2.y)) - barracks_size;
				TilePosition bottom_right = TilePosition(std::max(end1.x, end2.x), std::max(end1.y, end2.y)) + barracks_size;
				top_left.makeValid();
				bottom_right.makeValid();
				
				TilePosition supply_depot_size{ UnitTypes::Terran_Supply_Depot.tileWidth(), UnitTypes::Terran_Supply_Depot.tileHeight() };
				auto [wall1,wall2] = determine_walls(top_left - supply_depot_size, bottom_right + supply_depot_size, chokepoint);
				
				// @
				/*for (auto& w : wall1) {
					Rect r(w);
					Broodwar->drawBoxMap(r.left, r.top, r.right, r.bottom, Colors::Red);
				 }
				 for (auto& w : wall2) {
					Rect r(w);
					Broodwar->drawBoxMap(r.left, r.top, r.right, r.bottom, Colors::Green);
				 }
				 Broodwar->drawCircleMap(base->Center(), 20, Colors::White, true);
				 Broodwar->drawCircleMap(chokepoint_center(chokepoint), 20, Colors::Green, true);
				 Broodwar->drawBoxMap(Position(top_left), Position(bottom_right) + Position(31, 31), Colors::White);
				*/
				// /@
				
				TileGrid<bool> tiles = determine_tiles_around_chokepoint(chokepoint, base);
				std::vector<const InformationUnit*> neutral_units = determine_neutral_units(top_left, bottom_right);
				
				GraphBase graph_base(base, &wall1, &wall2, neutral_units);
				result = place_using_graph_base(tiles, graph_base, top_left, bottom_right);
				if (result) break;
			}
		}
		
		return result;
	}
	
	std::pair<const BWEM::ChokePoint*,const BWEM::Base*> TerranNaturalWallPlacer::determine_chokepoint_and_base()
	{
		const BWEM::ChokePoint* result_chokepoint = nullptr;
		const BWEM::Base* result_base = nullptr;
		
		if (base_state.natural_base() != nullptr) {
			const BWEM::Area* start_area = base_state.start_base()->GetArea();
			const BWEM::Area* natural_area = base_state.natural_base()->GetArea();
			
			std::vector<const BWEM::ChokePoint*> chokepoints;
			std::set<const BWEM::Area*> blocked_areas;
			blocked_areas.insert(start_area);
			blocked_areas.insert(natural_area);
			
			const BWEM::Area* wall_area = base_state.is_backdoor_natural() ? start_area : natural_area;
			result_base = base_state.is_backdoor_natural() ? base_state.start_base() : base_state.natural_base();
			
			for (auto& area : wall_area->AccessibleNeighbours()) {
				bool outside = false;
				
				if (base_state.controlled_areas().count(area) == 0) {
					std::set<const BWEM::Area*> reachable = base_state.reachable_areas(area, blocked_areas);
					outside = std::any_of(reachable.begin(), reachable.end(), [](const BWEM::Area* area){
						return std::any_of(area->Bases().begin(), area->Bases().end(), [](const BWEM::Base& base){
							return base.Starting();
						});
					});
				}
				
				if (outside) {
					for (auto& cp : wall_area->ChokePoints(area)) {
						if (!cp.Blocked()) chokepoints.push_back(&cp);
					}
				}
			}
			
			if (chokepoints.size() == 1) result_chokepoint = chokepoints[0];
		}
		
		return std::make_pair(result_chokepoint, result_base);
	}
	
	bool TerranNaturalWallPlacer::place_using_graph_base(const TileGrid<bool>& tiles,const GraphBase& graph_base,FastTilePosition top_left,FastTilePosition bottom_right)
	{
		std::vector<TerranNaturalWallPosition> terran_wall_positions = place_barracks_and_supply_depots(tiles, top_left, bottom_right, graph_base);
		
		struct TerranNaturalWallPositionOrder {
			bool operator()(const TerranNaturalWallPosition& a,const TerranNaturalWallPosition& b) const {
				return (std::make_tuple(a.gap_unit_count, a.box_size, a.second_supply_depot_position.isValid(), a.barracks_position, a.first_supply_depot_position, a.second_supply_depot_position) <
						std::make_tuple(b.gap_unit_count, b.box_size, b.second_supply_depot_position.isValid(), b.barracks_position, b.first_supply_depot_position, b.second_supply_depot_position));
			}
		};
		std::sort(terran_wall_positions.begin(), terran_wall_positions.end(), TerranNaturalWallPositionOrder());
		
		bool result = false;
		if (!place_bunker_) {
			if (!terran_wall_positions.empty()) {
				place_stage_point(terran_wall_positions[0], graph_base);
				building_placement_manager.apply_specific_positions(terran_wall_positions[0]);
				result = true;
			}
		} else {
			for (auto& wall_position : terran_wall_positions) {
				place_bunker(wall_position, tiles, graph_base);
				if (wall_position.bunker_position.isValid()) {
					building_placement_manager.apply_specific_positions(wall_position);
					result = true;
					break;
				}
			}
		}
		
		return result;
	}
	
	std::vector<TerranNaturalWallPosition> TerranNaturalWallPlacer::place_barracks_and_supply_depots(const TileGrid<bool>& in_tiles,TilePosition top_left,TilePosition bottom_right,const GraphBase& graph_base)
	{
		std::vector<TerranNaturalWallPosition> result;
		TileGrid<bool> tiles(in_tiles);
		
		for (int y = top_left.y; y <= bottom_right.y + 1 - UnitTypes::Terran_Supply_Depot.tileHeight(); y++) {
			for (int x = top_left.x; x <= bottom_right.x + 1 - UnitTypes::Terran_Supply_Depot.tileWidth(); x++) {
				TilePosition first_supply_depot_position(x, y);
				if (can_place_building_at(tiles, UnitTypes::Terran_Supply_Depot, first_supply_depot_position)) {
					set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, first_supply_depot_position, false);
					for (TilePosition barracks_position : determine_positions_around(tiles, UnitTypes::Terran_Supply_Depot, first_supply_depot_position, UnitTypes::Terran_Barracks)) {
						set_building_at_tile_grid(tiles, UnitTypes::Terran_Barracks, barracks_position, false);
						TerranNaturalWallPosition terran_wall_position;
						terran_wall_position.base = graph_base.base();
						terran_wall_position.barracks_position = barracks_position;
						terran_wall_position.first_supply_depot_position = first_supply_depot_position;
						add_positioning_if_valid(graph_base, terran_wall_position, result);
						for (TilePosition second_supply_depot_position : determine_positions_around(tiles, UnitTypes::Terran_Barracks, barracks_position, UnitTypes::Terran_Supply_Depot)) {
							set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, second_supply_depot_position, false);
							terran_wall_position.second_supply_depot_position = second_supply_depot_position;
							add_positioning_if_valid(graph_base, terran_wall_position, result);
							set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, second_supply_depot_position, true);
						}
						for (TilePosition second_supply_depot_position : determine_positions_around(tiles, UnitTypes::Terran_Supply_Depot, first_supply_depot_position, UnitTypes::Terran_Supply_Depot)) {
							set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, second_supply_depot_position, false);
							terran_wall_position.second_supply_depot_position = second_supply_depot_position;
							add_positioning_if_valid(graph_base, terran_wall_position, result);
							set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, second_supply_depot_position, true);
						}
						terran_wall_position.second_supply_depot_position = TilePositions::None;
						set_building_at_tile_grid(tiles, UnitTypes::Terran_Barracks, barracks_position, true);
					}
					set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, first_supply_depot_position, true);
				}
			}
		}
		
		return result;
	}

	void TerranNaturalWallPlacer::place_stage_point(TerranNaturalWallPosition& wall_position,const GraphBase& graph_base)
	{
		std::vector<std::pair<UnitType,TilePosition>> parts;
		parts.emplace_back(UnitTypes::Terran_Supply_Depot, wall_position.first_supply_depot_position);
		parts.emplace_back(UnitTypes::Terran_Supply_Depot, wall_position.second_supply_depot_position);
		parts.emplace_back(UnitTypes::Terran_Barracks, wall_position.barracks_position);
		int max_distance = INT_MIN;
		Rect rbase(UnitTypes::Protoss_Nexus, graph_base.base()->Location());
		for (auto& [type,tile_position] : parts) {
			if (tile_position.isValid()) {
				Rect r(type, tile_position);
				Gap gap(rbase, r);
				if (gap.is_valid()) {
					int distance = gap.size();
					if (distance > max_distance) {
						max_distance = distance;
						wall_position.stage_position = scale_line_segment(gap.b(), gap.a(), 40);
					}
				}
			}
		}
	}
	
	void TerranNaturalWallPlacer::place_bunker(TerranNaturalWallPosition& wall_position,const TileGrid<bool>& in_tiles,const GraphBase& graph_base)
	{
		TileGrid<bool> tiles(in_tiles);
		set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, wall_position.first_supply_depot_position, false);
		set_building_at_tile_grid(tiles, UnitTypes::Terran_Barracks, wall_position.barracks_position, false);
		if (wall_position.second_supply_depot_position.isValid()) set_building_at_tile_grid(tiles, UnitTypes::Terran_Supply_Depot, wall_position.second_supply_depot_position, false);
		
		key_value_vector<TilePosition,int> potential_bunker_positions;
		for (int y = wall_position.barracks_position.y - 8; y <= wall_position.barracks_position.y + 8; y++) {
			for (int x = wall_position.barracks_position.x - 8; x <= wall_position.barracks_position.x + 8; x++) {
				TilePosition bunker_position(x, y);
				wall_position.bunker_position = bunker_position;
				if (can_place_building_at(tiles, UnitTypes::Terran_Bunker, bunker_position) &&
					check_positioning(graph_base, wall_position)) {
					int bunker_distance = calculate_distance(UnitTypes::Terran_Bunker,
															 center_position_for(UnitTypes::Terran_Bunker, bunker_position),
															 UnitTypes::Terran_Command_Center,
															 graph_base.base()->Center());
					bool inside = false;
					int first_supply_depot_distance = calculate_distance(UnitTypes::Terran_Supply_Depot,
																		 center_position_for(UnitTypes::Terran_Supply_Depot, wall_position.first_supply_depot_position),
																		 UnitTypes::Terran_Command_Center,
																		 graph_base.base()->Center());
					if (bunker_distance < first_supply_depot_distance) {
						inside = true;
					} else {
						int barracks_distance = calculate_distance(UnitTypes::Terran_Barracks,
																   center_position_for(UnitTypes::Terran_Barracks, wall_position.barracks_position),
																   UnitTypes::Terran_Command_Center,
																   graph_base.base()->Center());
						if (bunker_distance < barracks_distance) {
							inside = true;
						} else if (wall_position.second_supply_depot_position.isValid()) {
							int second_supply_depot_distance = calculate_distance(UnitTypes::Terran_Supply_Depot,
																				  center_position_for(UnitTypes::Terran_Supply_Depot, wall_position.first_supply_depot_position),
																				  UnitTypes::Terran_Command_Center,
																				  graph_base.base()->Center());
							if (bunker_distance < second_supply_depot_distance) {
								inside = true;
							}
						}
					}
					if (inside) {
						int distance = std::min(calculate_distance(UnitTypes::Terran_Bunker,
																   center_position_for(UnitTypes::Terran_Bunker, bunker_position),
																   UnitTypes::Terran_Supply_Depot,
																   center_position_for(UnitTypes::Terran_Supply_Depot, wall_position.first_supply_depot_position)),
												calculate_distance(UnitTypes::Terran_Bunker,
																   center_position_for(UnitTypes::Terran_Bunker, bunker_position),
																   UnitTypes::Terran_Barracks,
																   center_position_for(UnitTypes::Terran_Barracks, wall_position.barracks_position)));
						if (wall_position.second_supply_depot_position.isValid()) {
							distance = std::min(distance,
												calculate_distance(UnitTypes::Terran_Bunker,
																   center_position_for(UnitTypes::Terran_Bunker, bunker_position),
																   UnitTypes::Terran_Supply_Depot,
																   center_position_for(UnitTypes::Terran_Supply_Depot, wall_position.second_supply_depot_position)));
						}
						potential_bunker_positions.emplace_back(bunker_position, distance);
					}
				}
				wall_position.bunker_position = TilePositions::None;
			}
		}
		
		wall_position.bunker_position = key_with_smallest_value(potential_bunker_positions, TilePositions::None);
		if (wall_position.bunker_position.isValid()) {
			wall_position.stage_position = center_position_for(UnitTypes::Terran_Bunker, wall_position.bunker_position);
		}
	}
	
	bool TerranNaturalWallPlacer::check_positioning(const GraphBase& graph_base,TerranNaturalWallPosition& terran_wall_position)
	{
		bool result = false;
		TerranNaturalWallGraph graph(graph_base, terran_wall_position, tight_for_unit_type_);
		if (graph.is_valid()) {
			TerranNaturalWallPosition terran_wall_position_without_barracks = terran_wall_position;
			terran_wall_position_without_barracks.barracks_position = TilePositions::None;
			TerranNaturalWallGraph graph_without_barracks(graph_base, terran_wall_position_without_barracks, tight_for_unit_type_);
			if (graph_without_barracks.is_open()) {
				terran_wall_position.gap_unit_count = graph.gap_unit_count();
				terran_wall_position.box_size = calculate_box_size(terran_wall_position);
				result = true;
			}
		}
		return result;
	}
	
	void TerranNaturalWallPlacer::add_positioning_if_valid(const GraphBase& graph_base,TerranNaturalWallPosition& terran_wall_position,std::vector<TerranNaturalWallPosition>& result)
	{
		/*TerranNaturalWallGraph graph(graph_base, terran_wall_position, tight_for_unit_type_);
		if (graph.is_valid()) {
			TerranNaturalWallPosition terran_wall_position_without_barracks = terran_wall_position;
			terran_wall_position_without_barracks.barracks_position = TilePositions::None;
			TerranNaturalWallGraph graph_without_barracks(graph_base, terran_wall_position_without_barracks, tight_for_unit_type_);
			if (graph_without_barracks.is_open()) {
				terran_wall_position.gap_unit_count = graph.gap_unit_count();
				terran_wall_position.box_size = calculate_box_size(terran_wall_position);
				result.push_back(terran_wall_position);
			}
		}*/
		if (check_positioning(graph_base, terran_wall_position)) result.push_back(terran_wall_position);
	}
	
	int TerranNaturalWallPlacer::calculate_box_size(TerranNaturalWallPosition& wall_position)
	{
		int left = std::min(wall_position.barracks_position.x, wall_position.first_supply_depot_position.x);
		int top = std::min(wall_position.barracks_position.y, wall_position.first_supply_depot_position.y);
		int right = std::max(wall_position.barracks_position.x + UnitTypes::Terran_Barracks.tileWidth(), wall_position.first_supply_depot_position.x + UnitTypes::Terran_Supply_Depot.tileWidth());
		int bottom = std::max(wall_position.barracks_position.y + UnitTypes::Terran_Barracks.tileHeight(), wall_position.first_supply_depot_position.y + UnitTypes::Terran_Supply_Depot.tileHeight());
		if (wall_position.second_supply_depot_position.isValid()) {
			left = std::min(left, wall_position.second_supply_depot_position.x);
			top = std::min(top, wall_position.second_supply_depot_position.y);
			right = std::max(right, wall_position.second_supply_depot_position.x + UnitTypes::Terran_Supply_Depot.tileWidth());
			bottom = std::max(bottom, wall_position.second_supply_depot_position.y + UnitTypes::Terran_Supply_Depot.tileHeight());
		}
		return (right - left) * (bottom - top);
	}
	
	std::vector<TilePosition> determine_positions_around(const TileGrid<bool>& tiles,UnitType type,TilePosition position,UnitType place_type)
	{
		std::vector<TilePosition> all_positions;
		
		int x1 = position.x - UnitTypes::Terran_Supply_Depot.tileWidth();
		int y1 = position.y - UnitTypes::Terran_Supply_Depot.tileHeight();
		int x2 = position.x + type.tileWidth();
		int y2 = position.y + type.tileHeight();
		
		for (int x = x1 + 1; x <= x2 - 1; x++) {
			all_positions.push_back(TilePosition(x, y1));
			all_positions.push_back(TilePosition(x, y2));
		}
		for (int y = y1; y <= y2; y++) {
			all_positions.push_back(TilePosition(x1, y));
			all_positions.push_back(TilePosition(x2, y));
		}
		
		std::vector<TilePosition> result;
		for (TilePosition tile_position : all_positions) {
			if (can_place_building_at(tiles, place_type, tile_position)) {
				result.push_back(tile_position);
			}
		}
		
		return result;
	}
	
	std::vector<const InformationUnit*> determine_neutral_units(FastTilePosition top_left,FastTilePosition bottom_right)
	{
		Rect search_rect(FastPosition(top_left), FastPosition(bottom_right) + FastPosition(31, 31));
		std::vector<const InformationUnit*> result;
		for (auto& neutral_unit : information_manager.neutral_units()) {
			if (!neutral_unit->flying) {
				Rect neutral_rect(neutral_unit->type, neutral_unit->tile_position());
				if (search_rect.distance(neutral_rect) <= 128) result.push_back(neutral_unit);
			}
		}
		return result;
	}
	
	bool can_place_building_at(const TileGrid<bool>& tiles,UnitType unit_type,TilePosition building_tile_position)
	{
		for (int y = 0; y < unit_type.tileHeight(); y++) {
			for (int x = 0; x < unit_type.tileWidth(); x++) {
				TilePosition tile_position = building_tile_position + TilePosition(x, y);
				if (!tile_position.isValid() ||
					!Broodwar->isBuildable(tile_position) ||
					!tiles.get(tile_position)) {
					return false;
				}
			}
		}
		return true;
	}
	
	void set_building_at_tile_grid(TileGrid<bool>& tiles,UnitType unit_type,TilePosition building_tile_position,bool value)
	{
		if (building_tile_position.isValid()) {
			for (int y = 0; y < unit_type.tileHeight(); y++) {
				for (int x = 0; x < unit_type.tileWidth(); x++) {
					TilePosition tile_position = building_tile_position + TilePosition(x, y);
					tiles.set(tile_position, value);
				}
			}
		}
	}
	
	TileGrid<bool> determine_tiles_around_chokepoint(const BWEM::ChokePoint* cp,const BWEM::Base* base)
	{
		const BWEM::Area* area1 = cp->GetAreas().first;
		const BWEM::Area* area2 = cp->GetAreas().second;
		TileGrid<bool> result;
		
		for (auto& area : {area1, area2} ) {
			int x1 = area->TopLeft().x;
			int y1 = area->TopLeft().y;
			int x2 = area->BottomRight().x;
			int y2 = area->BottomRight().y;
			for (int y = y1; y <= y2; y++) {
				for (int x = x1; x <= x2; x++) {
					TilePosition tile_position{x, y};
					if (tile_fully_walkable_in_areas(tile_position, area1, area2)) result.set(tile_position, true);
				}
			}
		}
		
		remove_base_and_neutrals(result, base);
		remove_neutral_creep(result);
		
		return result;
	}
	
	void remove_base_and_neutrals(TileGrid<bool>& tiles,const BWEM::Base* base)
	{
		set_building_at_tile_grid(tiles, UnitTypes::Protoss_Nexus, base->Location(), false);
		for (auto& neutral_unit : information_manager.neutral_units()) {
			if (neutral_unit->type.isBuilding()) {
				set_building_at_tile_grid(tiles, neutral_unit->type, neutral_unit->tile_position(), false);
			}
		}
		for (auto& mineral : base->Minerals()) remove_line(tiles, base, mineral);
		for (auto& geyser : base->Geysers()) remove_line(tiles, base, geyser);
	}
	
	void remove_neutral_creep(TileGrid<bool>& tiles)
	{
		for (auto& neutral_unit : information_manager.neutral_units()) {
			std::unordered_set<FastTilePosition> creep_tiles = creep_spread(neutral_unit->type, neutral_unit->position);
			for (FastTilePosition tile_position : creep_tiles) {
				tiles.set(tile_position, false);
			}
		}
	}
	
	void remove_line(TileGrid<bool>& tiles,const BWEM::Base* base,const BWEM::Neutral* neutral)
	{
		UnitType center_type = Broodwar->self()->getRace().getResourceDepot();
		Gap gap(Rect(center_type, base->Location(), true), Rect(neutral->Type(), neutral->TopLeft()));
		auto line = gap.line();
		for (auto position : Line<TilePosition>(TilePosition(line.first), TilePosition(line.second))) {
			tiles.set(position, false);
		}
	}
	
	bool tile_fully_walkable_in_areas(TilePosition tile_position,const BWEM::Area* area1,const BWEM::Area* area2)
	{
		const BWEM::Area* area = bwem_map.GetArea(tile_position);
		if (area != nullptr) {
			return area == area1 || area == area2;
		} else {
			WalkPosition walk_position(tile_position);
			for (int dy = 0; dy < 4; dy++) {
				for (int dx = 0; dx < 4; dx++) {
					area = bwem_map.GetArea(walk_position + WalkPosition(dx, dy));
					if (area != area1 && area != area2) return false;
				}
			}
			return true;
		}
	}
	
	std::pair<std::vector<WalkPosition>,std::vector<WalkPosition>> determine_walls(TilePosition top_left,TilePosition bottom_right,const BWEM::ChokePoint* chokepoint)
	{
		auto[end1_walk, end2_walk] = chokepoint_ends(chokepoint);
		std::vector<WalkPosition> wall1;
		std::vector<WalkPosition> wall2;
		std::vector<WalkPosition> wall_other;
		std::vector<WalkPosition> wall_next;
		for (int y = top_left.y * 4; y < bottom_right.y * 4 + 3; y++) {
			for (int x = top_left.x * 4; x < bottom_right.x * 4 + 3; x++) {
				WalkPosition walk_position{x, y};
				if (wall_of_areas(walk_position, chokepoint->GetAreas().first, chokepoint->GetAreas().second)) {
					int distance1 = center_position(walk_position).getApproxDistance(center_position(end1_walk));
					int distance2 = center_position(walk_position).getApproxDistance(center_position(end2_walk));
					if (distance1 <= 32 || distance2 <= 32) {
						if (distance1 < distance2) wall1.push_back(walk_position); else wall2.push_back(walk_position);
					} else {
						wall_other.push_back(walk_position);
					}
				}
			}
		}
		bool changes;
		do {
			changes = false;
			for (auto& walk_position : wall_other) {
				int distance1 = INT_MAX;
				for (auto& other_walk_position : wall1) {
					int distance = center_position(walk_position).getApproxDistance(center_position(other_walk_position));
					distance1 = std::min(distance1, distance);
				}
				int distance2 = INT_MAX;
				for (auto& other_walk_position : wall2) {
					int distance = center_position(walk_position).getApproxDistance(center_position(other_walk_position));
					distance2 = std::min(distance2, distance);
				}
				if (distance1 <= 32 || distance2 <= 32) {
					if (distance1 < distance2) wall1.push_back(walk_position); else wall2.push_back(walk_position);
				} else {
					wall_next.push_back(walk_position);
				}
			}
			changes = (wall_next.size() != wall_other.size());
			wall_other.clear();
			std::swap(wall_other, wall_next);
		} while (changes);
		for (auto& walk_position : wall_other) {
			int distance1 = INT_MAX;
			for (auto& other_walk_position : wall1) {
				int distance = center_position(walk_position).getApproxDistance(center_position(other_walk_position));
				distance1 = std::min(distance1, distance);
			}
			int distance2 = INT_MAX;
			for (auto& other_walk_position : wall2) {
				int distance = center_position(walk_position).getApproxDistance(center_position(other_walk_position));
				distance2 = std::min(distance2, distance);
			}
			if (distance1 < distance2) wall1.push_back(walk_position); else wall2.push_back(walk_position);
		}
		return std::make_pair(wall1, wall2);
	}
	
	bool wall_of_areas(FastWalkPosition walk_position,const BWEM::Area* area1,const BWEM::Area* area2)
	{
		bool result = false;
		if (!walk_position.isValid() || !Broodwar->isWalkable(walk_position)) {
			for (FastWalkPosition delta : { FastWalkPosition(-1, -1), FastWalkPosition(0, -1), FastWalkPosition(1, -1),
				FastWalkPosition(-1, 0), FastWalkPosition(1, 0),
				FastWalkPosition(-1, 1), FastWalkPosition(0, 1), FastWalkPosition(1, 1) }) {
					FastWalkPosition neighbouring_walk_position = walk_position + delta;
					if (neighbouring_walk_position.isValid()) {
						const BWEM::Area* area = area_at(neighbouring_walk_position);
						if (area == area1 || area == area2) {
							result = true;
							break;
						}
					}
				}
		}
		return result;
	}
};

#pragma once

namespace WallPlacement
{
	struct FFEPosition
	{
		const BWEM::Base* base = nullptr;
		TilePosition gateway_position = TilePositions::None;
		TilePosition forge_position = TilePositions::None;
		TilePosition pylon_position = TilePositions::None;
		std::vector<TilePosition> cannon_positions;
		std::vector<Gap> gaps;
		size_t gap_unit_count = 0;
		int box_size = 0;
		bool pylon_part_of_wall = false;
		bool multichoke_wall = false;
		bool multichoke_clockwise = false;
	};
	
	struct TerranWallPosition
	{
		const BWEM::Base* base = nullptr;
		TilePosition barracks_position = TilePositions::None;
		TilePosition first_supply_depot_position = TilePositions::None;
		TilePosition second_supply_depot_position = TilePositions::None;
		TilePosition factory_position = TilePositions::None;
		Position stage_position = Positions::None;
		bool machine_shop_placable = true;
	};
	
	struct TerranNaturalWallPosition
	{
		const BWEM::Base* base = nullptr;
		TilePosition barracks_position = TilePositions::None;
		TilePosition first_supply_depot_position = TilePositions::None;
		TilePosition second_supply_depot_position = TilePositions::None;
		TilePosition bunker_position = TilePositions::None;
		Position stage_position = Positions::None;
		size_t gap_unit_count = 0;
		int box_size = 0;
	};
	
	class GraphBase
	{
	public:
		GraphBase(const BWEM::Base* base,const std::vector<WalkPosition>* wall1,const std::vector<WalkPosition>* wall2,const std::vector<const InformationUnit*>& neutral_units) : base_(base), wall1_(wall1), wall2_(wall2), neutral_units_(neutral_units) {}
		GraphBase(const BWEM::Base* base,const std::vector<WalkPosition>* wall,const std::vector<const InformationUnit*>& neutral_units) : base_(base), wall1_(wall), wall2_(nullptr), neutral_units_(neutral_units) {}
		
		const BWEM::Base* base() const { return base_; }
		bool is_single_wall() const { return wall2_ == nullptr; }
		const std::vector<const InformationUnit*>& neutral_units() const { return neutral_units_; }
		
	private:
		const BWEM::Base* base_;
		const std::vector<WalkPosition>* wall1_;
		const std::vector<WalkPosition>* wall2_;
		const std::vector<const InformationUnit*>& neutral_units_;
		
		friend class Graph;
	};
	
	class Graph
	{
	public:
		Graph(const GraphBase& graph_base,UnitType basic_combat_unit_type,UnitType tight_for_unit_type);
		
		bool is_valid() const { return valid_; }
		const std::vector<Gap>& gaps() const { return gaps_; }
		size_t gap_unit_count() const { return gap_unit_count_; }
	protected:
		class Node
		{
		public:
			Node() {}
			Node(const std::vector<WalkPosition>* terrain) : terrain_(terrain) {}
			Node(UnitType unit_type,TilePosition tile_position) : unit_type_(unit_type), tile_position_(tile_position) {}
			
			const std::vector<WalkPosition>* terrain() const { return terrain_; }
			UnitType unit_type() const { return unit_type_; }
			TilePosition tile_position() const { return tile_position_; }
			const std::map<size_t,Gap>& neighbours() const { return neighbours_; }
			void add_neighbour(size_t index,const Gap& gap) { neighbours_[index] = gap; }
			Gap determine_gap(const Node& node) const;
			
		private:
			const std::vector<WalkPosition>* terrain_ = nullptr;
			UnitType unit_type_ = UnitTypes::None;
			TilePosition tile_position_ = TilePositions::None;
			
			std::map<size_t,Gap> neighbours_;
			
			Gap determine_gap(UnitType type,TilePosition position,const std::vector<WalkPosition>* wall_positions) const;
			Gap determine_gap(UnitType type1,TilePosition position1,UnitType type2,TilePosition position2) const;
		};
		
		std::vector<Node> nodes_;
		size_t start_node_;
		size_t end_node_;
		
		std::vector<Gap> gaps_;
		size_t gap_unit_count_;
		bool valid_ = false;
		
		UnitType basic_combat_unit_type_;
		UnitType tight_for_unit_type_;
		
		void add_wall_and_neutrals(const GraphBase& graph_base);
		size_t add_wall_node(const std::vector<WalkPosition>* wall);
		size_t add_base_node(const BWEM::Base* base);
		void add_neutral_building_nodes(const std::vector<const InformationUnit*>& neutral_units);
		void add_neighbours();
		bool determine_gaps();
		bool passable();
		bool passable(std::set<size_t>& visited,size_t current_index);
	};
	
	class FFEGraph : public Graph
	{
	public:
		FFEGraph(const GraphBase& graph_base,const FFEPosition& ffe_position);
	private:
		void add_ffe_position(const FFEPosition& ffe_position);
		
	};
	
	class TerranWallGraph : public Graph
	{
	public:
		TerranWallGraph(const GraphBase& graph_base,const TerranWallPosition& terran_wall_position,UnitType tight_for_unit_type);
		bool is_open() { return passable(); }
	private:
		void add_terran_wall_position(const TerranWallPosition& terran_wall_position);
	};
	
	class TerranNaturalWallGraph : public Graph
	{
	public:
		TerranNaturalWallGraph(const GraphBase& graph_base,const TerranNaturalWallPosition& terran_natural_wall_position,UnitType tight_for_unit_type);
		bool is_open() { return passable(); }
	private:
		void add_terran_natural_wall_position(const TerranNaturalWallPosition& terran_natural_wall_position);
	};
	
	struct Chokepoints
	{
		const BWEM::Base* base;
		std::vector<const BWEM::ChokePoint*> inside_chokepoints;
		std::vector<const BWEM::ChokePoint*> outside_chokepoints;
	};
	
	class MultipleChokePlacer
	{
	public:
		MultipleChokePlacer(const Chokepoints& chokepoints);
		const std::vector<WalkPosition>& wall_walk_positions() const { return wall_walk_positions_; }
		bool clockwise() const { return clockwise_; }
		
	private:
		enum class NodeType
		{
			Mineral,
			InsideChoke,
			OutsideChoke,
			Wall
		};
		
		struct Node
		{
			NodeType type;
			double angle;
			FastWalkPosition wall_position;
			
			bool operator<(const Node& o) const { return angle < o.angle; }
		};
		
		std::vector<Node> nodes_;
		std::vector<size_t> nonwall_node_indexes_;
		std::vector<std::pair<size_t,size_t>> wall_position_;
		std::vector<WalkPosition> wall_walk_positions_;
		bool clockwise_ = false;
		
		void create_nodes(const Chokepoints& chokepoints);
		void determine_nonwall_node_indexes();
		void determine_wall_position();
		double calculate_angle(const BWEM::Base* base,Position position);
	};
	
	class FFEPlacer
	{
	public:
		bool place_ffe();
	private:
		std::optional<FFEPosition> place_using_graph_base(const TileGrid<bool>& tiles,const GraphBase& graph_base,FastTilePosition top_left,FastTilePosition bottom_right);
		std::vector<FFEPosition> place_gateway_forge(const TileGrid<bool>& in_tiles,TilePosition top_left,TilePosition bottom_right,const GraphBase& graph_base);
		int calculate_box_size(FFEPosition& ffe_position);
		void place_pylon_and_cannons(FFEPosition& ffe_position,const TileGrid<bool>& in_tiles,const GraphBase& graph_base);
		void place_cannons(FFEPosition& ffe_position,TileGrid<bool>& tiles,const GraphBase& graph_base);
		bool check_building_neutral_unit_collision(const GraphBase& graph_base,UnitType unit_type,TilePosition building_tile_position);
		bool check_building_inside(const FFEPosition& ffe_position,UnitType unit_type,TilePosition building_tile_position,const BWEM::Base* base);
		Chokepoints determine_natural_inside_and_outside_chokepoints();
		TileGrid<bool> determine_tiles_in_range(FastTilePosition top_left,FastTilePosition bottom_right,const BWEM::Base* base);
		std::vector<TilePosition> determine_forge_positions(const TileGrid<bool>& tiles,TilePosition gateway_position);
		std::vector<TilePosition> determine_forge_positions_near_pylon(const TileGrid<bool>& tiles,TilePosition pylon_position);
		std::vector<TilePosition> determine_gateway_positions_above_forge(const TileGrid<bool>& tiles,TilePosition pylon_position,TilePosition forge_position);
	};
	
	class TerranWallPlacer
	{
	public:
		bool place_wall();
	protected:
		TerranWallPlacer(UnitType tight_for_unit_type,bool require_placable_machine_shop) : tight_for_unit_type_(tight_for_unit_type), require_placable_machine_shop_(require_placable_machine_shop) {}
	private:
		UnitType tight_for_unit_type_;
		bool require_placable_machine_shop_;
		
		const BWEM::ChokePoint* determine_chokepoint();
		bool place_using_graph_base(const TileGrid<bool>& tiles,const GraphBase& graph_base,FastTilePosition top_left,FastTilePosition bottom_right);
		std::vector<TerranWallPosition> place_barracks_and_supply_depots(const TileGrid<bool>& in_tiles,TilePosition top_left,TilePosition bottom_right,const GraphBase& graph_base,bool place_second_depot);
		std::vector<TerranWallPosition> place_barracks_and_factory(const TileGrid<bool>& in_tiles,TilePosition top_left,TilePosition bottom_right,const GraphBase& graph_base);
		void place_stage_point(TerranWallPosition& wall_position,const GraphBase& graph_base);
		bool check_positioning(const GraphBase& graph_base,const TerranWallPosition& terran_wall_position);
	};
	
	class TvZWallPlacer : public TerranWallPlacer
	{
	public:
		TvZWallPlacer(bool require_placable_machine_shop) : TerranWallPlacer(UnitTypes::Zerg_Zergling, require_placable_machine_shop) {}
	};
	
	class TvPWallPlacer : public TerranWallPlacer
	{
	public:
		TvPWallPlacer(bool require_placable_machine_shop) : TerranWallPlacer(UnitTypes::Protoss_Zealot, require_placable_machine_shop) {}
	};
	
	class TerranNaturalWallPlacer
	{
	public:
		bool place_wall();
	protected:
		TerranNaturalWallPlacer(UnitType tight_for_unit_type,bool place_bunker) : tight_for_unit_type_(tight_for_unit_type), place_bunker_(place_bunker) {}
	private:
		UnitType tight_for_unit_type_;
		bool place_bunker_;
		
		std::pair<const BWEM::ChokePoint*,const BWEM::Base*> determine_chokepoint_and_base();
		bool place_using_graph_base(const TileGrid<bool>& tiles,const GraphBase& graph_base,FastTilePosition top_left,FastTilePosition bottom_right);
		std::vector<TerranNaturalWallPosition> place_barracks_and_supply_depots(const TileGrid<bool>& in_tiles,TilePosition top_left,TilePosition bottom_right,const GraphBase& graph_base);
		void place_stage_point(TerranNaturalWallPosition& wall_position,const GraphBase& graph_base);
		void place_bunker(TerranNaturalWallPosition& wall_position,const TileGrid<bool>& in_tiles,const GraphBase& graph_base);
		bool check_positioning(const GraphBase& graph_base,TerranNaturalWallPosition& terran_wall_position);
		void add_positioning_if_valid(const GraphBase& graph_base,TerranNaturalWallPosition& terran_wall_position,std::vector<TerranNaturalWallPosition>& result);
		int calculate_box_size(TerranNaturalWallPosition& wall_position);
	};
	
	class TvZNaturalWallPlacer : public TerranNaturalWallPlacer
	{
	public:
		TvZNaturalWallPlacer(bool place_bunker) : TerranNaturalWallPlacer(UnitTypes::Zerg_Zergling, place_bunker) {}
	};
	
	class TvTNaturalWallPlacer : public TerranNaturalWallPlacer
	{
	public:
		TvTNaturalWallPlacer(bool place_bunker) : TerranNaturalWallPlacer(UnitTypes::Terran_Marine, place_bunker) {}
	};
	
	class TvPNaturalWallPlacer : public TerranNaturalWallPlacer
	{
	public:
		TvPNaturalWallPlacer(bool place_bunker) : TerranNaturalWallPlacer(UnitTypes::Protoss_Zealot, place_bunker) {}
	};
	
	std::vector<TilePosition> determine_positions_around(const TileGrid<bool>& tiles,UnitType type,TilePosition position,UnitType place_type);
	std::vector<const InformationUnit*> determine_neutral_units(FastTilePosition top_left,FastTilePosition bottom_right);
	bool can_place_building_at(const TileGrid<bool>& tiles,UnitType unit_type,TilePosition building_tile_position);
	void set_building_at_tile_grid(TileGrid<bool>& tiles,UnitType unit_type,TilePosition building_tile_position,bool value);
	TileGrid<bool> determine_tiles_around_chokepoint(const BWEM::ChokePoint* cp,const BWEM::Base* base);
	void remove_base_and_neutrals(TileGrid<bool>& tiles,const BWEM::Base* base);
	void remove_neutral_creep(TileGrid<bool>& tiles);
	void remove_line(TileGrid<bool>& tiles,const BWEM::Base* base,const BWEM::Neutral* neutral);
	bool tile_fully_walkable_in_areas(TilePosition tile_position,const BWEM::Area* area1,const BWEM::Area* area2);
	std::pair<std::vector<WalkPosition>,std::vector<WalkPosition>> determine_walls(TilePosition top_left,TilePosition bottom_right,const BWEM::ChokePoint* chokepoint);
	bool wall_of_areas(FastWalkPosition walk_position,const BWEM::Area* area1,const BWEM::Area* area2);
}

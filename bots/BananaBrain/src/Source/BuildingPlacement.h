class BuildingPlacementManager : public Singleton<BuildingPlacementManager>
{
public:
	void init();
	TilePosition place_building(UnitType unit_type);
	TilePosition place_base_defense_pylon(const BWEM::Base* base);
	TilePosition place_base_defense_photon_cannon(const BWEM::Base* base);
	TilePosition place_base_defense_missile_turret(const BWEM::Base* base);
	TilePosition place_base_defense_creep_colony(const BWEM::Base* base);
	const std::map<const BWEM::Base*,std::vector<TilePosition>>& photon_cannon_positions() const { return photon_cannon_positions_; }
	const std::map<const BWEM::Base*,std::vector<TilePosition>>& missile_turret_positions() const { return missile_turret_positions_; }
	const std::map<const BWEM::Base*,TilePosition>& creep_colony_positions() const { return creep_colony_positions_; }
	void draw();
	bool check_power(TilePosition pylon_position,UnitType building_type,TilePosition building_position);
	TilePosition calculate_proxy_barracks_position();
	TilePosition calculate_proxy_gateway_position();
	void calculate_gateway_near_choke_position();
	void calculate_two_gateways_near_choke_position(bool include_cannon=false);
	void calculate_shield_battery_near_choke_position();
	TilePosition calculate_bunker_near_choke_position(bool include_natural=false);
	TilePosition calculate_bunker_near_mineral_line_position();
	TilePosition calculate_barracks_near_bunker_position(FastTilePosition bunker_tile_position,FastTilePosition original_barracks_tile_position);
	TilePosition calculate_command_center_in_main_position();
	bool calculate_forge_fast_expand_position();
	int calculate_defense_creep_colony_positions(const BWEM::Base* base,int count_requested);
	bool calculate_macro_hatch_position(const BWEM::Base* base);
	std::vector<TilePosition>& specific_positions_for_type(UnitType type) { return specific_positions_[type]; }
	bool calculate_tvz_wall_position(bool require_placable_machine_shop=false);
	bool calculate_tvp_wall_position(bool require_placable_machine_shop=false);
	bool calculate_tvz_natural_wall_position(bool place_bunker=false);
	bool calculate_tvt_natural_wall_position(bool place_bunker=false);
	bool calculate_tvp_natural_wall_position(bool place_bunker=false);
	void clear_specific_positions() { specific_positions_.clear(); }
	void clear_specific_positions_for_type(UnitType type) { specific_positions_.erase(type); }
	TilePosition proxy_pylon_position() const { return proxy_pylon_position_; }
	void clear_proxy_pylon_position() { proxy_pylon_position_ = TilePositions::None; }
	TilePosition proxy_barracks_position() const { return proxy_barracks_position_; }
	TilePosition proxy_second_barracks_position() const { return proxy_second_barracks_position_; }
	void clear_proxy_barracks_position() { proxy_barracks_position_ = proxy_second_barracks_position_ = TilePositions::None; }
	void apply_specific_positions(const WallPlacement::FFEPosition& ffe);
	const std::optional<WallPlacement::FFEPosition>& ffe_position() const { return ffe_position_; }
	void restore_saved_ffe_natural_base_defense_positions();
	void apply_specific_positions(const WallPlacement::TerranWallPosition& terran_wall_position);
	void apply_specific_positions(const WallPlacement::TerranNaturalWallPosition& terran_wall_position);
	const std::optional<WallPlacement::TerranWallPosition>& terran_wall_position() const { return terran_wall_position_; }
	const std::optional<WallPlacement::TerranNaturalWallPosition>& terran_natural_wall_position() const { return terran_natural_wall_position_; }
	bool has_active_ffe_wall() const;
	bool has_active_wall(bool allow_gap=false) const;
	void onUnitDestroy(Unit unit);
	
private:
	std::set<TilePosition> power_grid_2x2_;
	std::set<TilePosition> power_grid_3x2_;
	std::set<TilePosition> power_grid_4x3_;
	std::map<const BWEM::Base*,std::vector<TilePosition>> photon_cannon_positions_;
	std::map<const BWEM::Base*,std::vector<TilePosition>> missile_turret_positions_;
	std::map<const BWEM::Base*,TilePosition> creep_colony_positions_;
	std::map<const BWEM::Base*,TilePosition> pylon_positions_;
	std::map<UnitType,std::vector<TilePosition>> specific_positions_;
	TilePosition proxy_pylon_position_ = TilePositions::None;
	TilePosition proxy_barracks_position_ = TilePositions::None;
	TilePosition proxy_second_barracks_position_ = TilePositions::None;
	std::optional<WallPlacement::FFEPosition> ffe_position_;
	std::vector<TilePosition> ffe_saved_natural_base_defense_cannon_positions_;
	TilePosition ffe_saved_natural_base_defense_pylon_position_ = TilePositions::None;
	std::optional<WallPlacement::TerranWallPosition> terran_wall_position_;
	std::optional<WallPlacement::TerranNaturalWallPosition> terran_natural_wall_position_;
	
	void init_power_grid();
	void init_power_grid(std::set<TilePosition>& grid,int y,int xmin,int xmax);
	void init_photon_cannon_positions();
	void init_missile_turret_positions();
	void init_creep_colony_positions();
	Position calculate_defense_center(const BWEM::Base* base);
	TilePosition pylon_placement_start_location(const BWEM::Base* base);
	TilePosition place_pylon(const TileGrid<bool>& blocked,const TileGrid<bool>& resource_blocked,TilePosition start_location);
	TilePosition place_other_building(const TileGrid<bool>& blocked,const TileGrid<bool>& resource_blocked,TilePosition start_location,UnitType type);
	std::vector<TilePosition> determine_pylon_positions();
	std::vector<TilePosition> determine_completed_pylon_positions();
	bool test_distance_from_existing_pylons(const std::vector<TilePosition>& pylon_positions,TilePosition tile_position);
	bool tile_in_controlled_area(TilePosition tile_position,const BWEM::Area* start_area);
	void insert_building_location(TileGrid<bool>& blocked,UnitType unit_type,TilePosition base_position);
	void remove_building_location(TileGrid<bool>& blocked,UnitType unit_type,TilePosition base_position);
	void insert_building_location_with_padding(TileGrid<bool>& blocked,UnitType unit_type,TilePosition base_position,int padding);
	bool test_power_requirement(const TileGrid<bool>& power_grid,UnitType unit_type,TilePosition base_position);
	bool test_building_location(const TileGrid<bool>& blocked,const TileGrid<bool>& resource_blocked,UnitType unit_type,TilePosition base_position,bool group_check=true);
	std::set<TilePosition> determine_border(const std::set<TilePosition>& positions);
	std::set<TilePosition> determine_connected_tiles(const TileGrid<bool>& blocked,UnitType unit_type,TilePosition base_position);
	bool is_connected(const std::set<TilePosition>& positions);
	void insert_building_locations(TileGrid<bool>& blocked);
	void insert_base_locations(TileGrid<bool>& blocked);
	void insert_resource_locations(TileGrid<bool>& blocked);
	void insert_padded_resource_locations(TileGrid<bool>& blocked,bool resource_depot=false);
	void insert_resource_locations_pad_minerals(TileGrid<bool>& blocked);
	void insert_specific_building_locations(TileGrid<bool>& blocked);
	void insert_base_defense_photon_cannon_locations(TileGrid<bool>& blocked);
	void insert_base_defense_missile_turret_locations(TileGrid<bool>& blocked);
	void insert_base_defense_creep_colony_locations(TileGrid<bool>& blocked);
	void insert_threatened_ground_locations(TileGrid<bool>& blocked);
	void insert_neutral_creep(TileGrid<bool>& blocked);
	TileGrid<bool> determine_power_grid(UnitType unit_type);
	std::vector<TilePosition> determine_second_gateway_positions(const TileGrid<bool>& blocked,TilePosition first_gateway_position);
	void insert_unbuildable_nonarea_locations(TileGrid<bool>& blocked,const std::set<const BWEM::Area*> areas);
	void insert_scout_path_on_two_player_maps(TileGrid<bool>& blocked);
	bool rectangle_buildable_for_building_with_clearance(TilePosition tile_position,UnitType unit_type,const TileGrid<bool>& blocked,int clearance=1);
	bool tile_inside_areas(TilePosition tile_position,const std::set<const BWEM::Area*>& areas);
	int maximum_distance_from_possible_opponent_starting_bases(Position position);
};

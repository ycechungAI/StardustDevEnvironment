#pragma once

class Border
{
public:
	Border() {}
	Border(const std::set<const BWEM::Area*>& areas);
	
	const std::set<const BWEM::Area*>& inside_areas() const { return inside_areas_; }
	const std::set<const BWEM::Area*>& outside_areas() const { return outside_areas_; }
	const std::set<const BWEM::ChokePoint*>& chokepoints() const { return chokepoints_; }
	
	std::vector<const BWEM::ChokePoint*> chokepoints_with_area(const BWEM::Area* area);
	const BWEM::ChokePoint* largest_chokepoint_with_area(const BWEM::Area* area);
private:
	std::set<const BWEM::Area*> inside_areas_;
	std::set<const BWEM::Area*> outside_areas_;
	std::set<const BWEM::ChokePoint*> chokepoints_;
};

class BaseState : public Singleton<BaseState>
{
public:
	static constexpr int kLargeAreaAltitude = 640;
	
	void init_bases();
	void update_base_information();
	
	const std::vector<const BWEM::Base*>& bases() const { return bases_; }
	const BWEM::Base* base_for_tile_position(TilePosition tile_position) const { return get_or_default(base_map_, tile_position); }
	const BWEM::Base* natural_base_for_start_base(const BWEM::Base* start_base) const { return get_or_default(start_to_natural_map_, start_base); }
	const BWEM::Area* extension_area_for_start_base(const BWEM::Base* start_base) const { return get_or_default(start_extension_map_, start_base); }
	const std::set<const BWEM::Base*>& controlled_bases() const { return controlled_bases_; }
	const std::set<const BWEM::Base*>& controlled_and_planned_bases() const { return controlled_and_planned_bases_; }
	const std::set<const BWEM::Area*>& controlled_areas() const { return controlled_areas_; }
	const std::set<const BWEM::Area*>& controlled_and_planned_areas() const { return controlled_and_planned_areas_; }
	const Border& border() const { return border_; }
	const std::set<const BWEM::Base*>& opponent_bases() const { return opponent_bases_; }
	const std::vector<const BWEM::Base*>& unexplored_start_bases() const { return unexplored_start_bases_; }
	int base_last_seen(const BWEM::Base* base) { return get_or_default(base_last_seen_, base, -1); }
	const std::vector<const BWEM::Base*>& next_available_bases() const { return next_available_bases_; }
	const BWEM::Base* start_base() const { return start_base_; }
	const BWEM::Base* natural_base() const { return natural_base_; }
	bool is_backdoor_natural() { return backdoor_natural_; }
	bool is_island_map() { return island_map_; }
	const BWEM::Base* main_base() const;
	int start_base_count() { return (int)start_to_natural_map_.size(); }
	int controlled_geyser_count() const;
	int mining_base_count() const;
	int mineable_mineral_count() const;
	std::set<const BWEM::Base*> undiscovered_starting_bases(bool overlord=false) const;
	void draw();
	
	bool skip_mineral_only() const { return skip_mineral_only_; }
	void set_skip_mineral_only(bool skip_mineral_only) { skip_mineral_only_ = skip_mineral_only; }
	
	std::set<const BWEM::Area*> enclosed_areas(const std::set<const BWEM::Area*>& areas) const;
	static std::set<const BWEM::Area*> reachable_areas(const BWEM::Area* area,const std::set<const BWEM::Area*>& blocked_areas = std::set<const BWEM::Area*>());
	static std::set<const BWEM::Area*> connected_areas(const BWEM::Area* area,const std::set<const BWEM::Area*>& allowed_areas);
	bool is_base_enclosed(const BWEM::Base* start_base,const BWEM::Base* natural_base,const BWEM::Base* other_base) const;
	
private:
	std::vector<const BWEM::Base*> bases_;
	std::map<TilePosition,const BWEM::Base*> base_map_;
	std::map<const BWEM::Base*,const BWEM::Base*> start_to_natural_map_;
	std::map<const BWEM::Base*,const BWEM::Area*> start_extension_map_;
	std::set<const BWEM::Base*> controlled_bases_;
	std::set<const BWEM::Base*> controlled_and_planned_bases_;
	std::set<const BWEM::Area*> controlled_areas_;
	std::set<const BWEM::Area*> controlled_and_planned_areas_;
	std::set<const BWEM::Base*> opponent_bases_;
	Border border_;
	std::vector<const BWEM::Base*> next_available_bases_;
	std::vector<const BWEM::Base*> unexplored_start_bases_;
	std::map<const BWEM::Base*,int> base_last_seen_;
	const BWEM::Base* start_base_;
	const BWEM::Base* natural_base_;
	bool backdoor_natural_ = false;
	bool island_map_ = false;
	bool skip_mineral_only_ = false;
	
	void update_controlled_bases();
	void update_opponent_bases();
	void update_border();
	void update_next_available_bases();
	void update_unexplored_start_bases();
	void update_base_last_seen();
	std::set<const BWEM::Area*> determine_pylon_and_bunker_areas(bool completed);
	static bool is_ffe_pylon(FastTilePosition tile_position);
	std::set<const BWEM::Area*> controlled_areas_from_bases(const std::set<const BWEM::Base*>& controlled_bases,const std::set<const BWEM::Area*> pylon_areas);
	const BWEM::Base* determine_natural(const BWEM::Base* start_base) const;
	const BWEM::Area* determine_start_extension(const BWEM::Base* start_base,const BWEM::Base* natural_base) const;
	static bool is_base_with_both_minerals_and_gas(const BWEM::Base* base);
	static bool is_large_area(const BWEM::Area* area);
	void draw_bases();
	void draw_areas();
	void draw_unit_rectangle(TilePosition tile_position,UnitType unit_type,Color color);
};

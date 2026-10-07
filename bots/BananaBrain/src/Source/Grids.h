#pragma once

template <typename T>
class TileGrid
{
public:
	TileGrid() { clear(); }
	
	void clear()
	{
		data_.fill(T());
	}
	
	T& operator [](FastTilePosition tile_position)
	{
		return data_[tile_position.y * 256 + tile_position.x];
	}
	
	const T& operator [](FastTilePosition tile_position) const
	{
		return data_[tile_position.y * 256 + tile_position.x];
	}
	
	T get(FastTilePosition tile_position) const
	{
		if (tile_position.isValid()) {
			return data_[tile_position.y * 256 + tile_position.x];
		} else {
			return T();
		}
	}
	
	void set(FastTilePosition tile_position,T value)
	{
		if (tile_position.isValid()) data_[tile_position.y * 256 + tile_position.x] = value;
	}
private:
	std::array<T,256*256> data_;
};

template <typename T>
class WalkGrid
{
public:
	WalkGrid() { clear(); }
	
	void clear()
	{
		data_.fill(T());
	}
	
	T& operator [](FastWalkPosition walk_position)
	{
		return data_[walk_position.y * 4 * 256 + walk_position.x];
	}
	
	const T& operator [](FastWalkPosition walk_position) const
	{
		return data_[walk_position.y * 4 * 256 + walk_position.x];
	}
	
	T get(FastWalkPosition walk_position) const
	{
		if (walk_position.isValid()) {
			return data_[walk_position.y * 4 * 256 + walk_position.x];
		} else {
			return T();
		}
	}
	
	void set(FastWalkPosition walk_position,T value)
	{
		if (walk_position.isValid()) data_[walk_position.y * 4 * 256 + walk_position.x] = value;
	}
private:
	std::array<T,4*256*4*256> data_;
};

template <int Size,int Granularity,typename T>
class SparsePositionGrid
{
public:
	SparsePositionGrid(FastPosition center) : center_(center) { clear(); }
	
	static constexpr int Count = (Size / Granularity) * 2 + 1;
	
	void clear()
	{
		data_.fill(T());
	}
	
	FastPosition center() const
	{
		return center_;
	}
	
	T& operator [](FastPosition position)
	{
		int x = (position.x - center_.x + Size) / Granularity;
		int y = (position.y - center_.y + Size) / Granularity;
		return data_[y * Count + x];
	}
	
	const T& operator [](FastPosition position) const
	{
		int x = (position.x - center_.x + Size) / Granularity;
		int y = (position.y - center_.y + Size) / Granularity;
		return data_[y * Count + x];
	}
	
	bool is_valid(FastPosition position) const
	{
		return (position.isValid() &&
				position.x >= center_.x - Size &&
				position.y >= center_.y - Size &&
				position.x <= center_.x + Size &&
				position.y <= center_.y + Size);
	}
	
	FastPosition snap_to_grid(FastPosition position) const
	{
		FastPosition result = position;
		result.x -= euclidian_remainder(position.x - center_.x, Granularity);
		result.y -= euclidian_remainder(position.y - center_.y, Granularity);
		return result;
	}
	
	class Iterator;
	
	Iterator begin() const {
		return Iterator(*this, FastPosition(center_.x - Size, center_.y - Size));
	}
	
	Iterator end() const {
		return Iterator(*this, FastPosition(center_.x + Size, center_.y + Size));
	}
public:
	class Iterator
	{
	public:
		Iterator(const SparsePositionGrid& grid,FastPosition position) : grid_(grid), position_(position) {}
		Iterator& operator++() {
			if (position_.x == grid_.center_.x + Size) {
				position_.x = grid_.center_.x - Size;
				position_.y += Granularity;
			} else {
				position_.x += Granularity;
			}
			return *this;
		}
		
		FastPosition operator*() { return position_; }
		bool operator==(const Iterator& o) const { return position_ == o.position_; }
		bool operator!=(const Iterator& o) const { return position_ != o.position_; }
	private:
		const SparsePositionGrid& grid_;
		FastPosition position_;
	};
private:
	FastPosition center_;
	std::array<T,Count*Count> data_;
};

class WalkabilityGrid : public Singleton<WalkabilityGrid>
{
public:
	void init();
	void update();
	
	bool is_terrain_walkable(FastTilePosition tile_position) const { return terrain_walkable_.get(tile_position); }
	bool is_walkable(FastTilePosition tile_position) const { return walkable_.get(tile_position); }
	FastPosition walkable_tile_near(FastPosition position,int range) const;
private:
	TileGrid<bool> terrain_walkable_;
	TileGrid<bool> walkable_;
	
	static bool tile_fully_walkable(FastTilePosition tile_position);
};

class ConnectivityGrid : public Singleton<ConnectivityGrid>
{
public:
	void update();
	void invalidate() { valid_ = false; }
	
	int component_for_position(FastPosition position) { return component_and_tile_for_position(position).first; }
	std::pair<int,FastTilePosition> component_and_tile_for_position(FastPosition position);
	int component_for_position(FastTilePosition tile_position) { return component_.get(tile_position); }
	bool check_reachability(Unit combat_unit,Unit enemy_unit);
	bool check_reachability_melee(int component,Unit enemy_unit);
	bool check_reachability_ranged(int component,int range,Unit enemy_unit);
	bool building_has_component(UnitType unit_type,FastTilePosition tile_position,int component);
	std::set<int> building_components(UnitType unit_type,FastTilePosition tile_position);
	void building_components(std::set<int>& result,UnitType unit_type,FastTilePosition tile_position);
	bool is_wall_building(Unit unit);
	int wall_building_perimeter(Unit unit,int component);
private:
	bool valid_ = false;
	TileGrid<int> component_;
};

class ThreatGrid : public Singleton<ThreatGrid>
{
public:
	class ThreatComponentGrid
	{
	public:
		virtual int component(FastTilePosition tile_position) const { return component_grid_.get(tile_position); }
		int component(FastPosition position) const { return component(to_tile_position(position)); }
		virtual FastTilePosition to_tile_position(FastPosition position) const = 0;
		virtual FastPosition to_position(FastTilePosition tile_position) const = 0;
		TileGrid<int>& component_grid() { return component_grid_; }
		
	protected:
		TileGrid<int> component_grid_;
	};
	
	void update();
	bool ground_threat(FastTilePosition tile_position) const { return ground_threat_.get(tile_position); }
	bool air_threat(FastTilePosition tile_position) const { return air_threat_.get(tile_position); }
	bool threat(FastTilePosition tile_position, bool air) const {
		return air ? air_threat_.get(tile_position) : ground_threat_.get(tile_position);
	}
	bool ground_threat_excluding_workers(FastTilePosition tile_position) const { return ground_threat_excluding_workers_.get(tile_position); }
	const ThreatComponentGrid& component_grid(UnitType type) const;
	int ground_splash_distance(FastTilePosition tile_position) const { return ground_splash_distance_.get(tile_position); }
	void draw(bool air) const;
private:
	class RegularThreatComponentGrid : public ThreatComponentGrid
	{
		FastTilePosition to_tile_position(FastPosition position) const { return FastTilePosition(position); }
		FastPosition to_position(FastTilePosition tile_position) const { return center_position(tile_position); }
	};
	
	class CrossThreatComponentGrid : public ThreatComponentGrid
	{
		int component(FastTilePosition tile_position) const;
		FastTilePosition to_tile_position(FastPosition position) const { return FastTilePosition(position + FastPosition(16, 16)); }
		FastPosition to_position(FastTilePosition tile_position) const { return FastPosition(tile_position); }
	};
	
	TileGrid<bool> ground_threat_;
	TileGrid<bool> air_threat_;
	TileGrid<bool> ground_threat_excluding_workers_;
	RegularThreatComponentGrid ground_component_;
	RegularThreatComponentGrid air_component_;
	RegularThreatComponentGrid cloaked_air_threat_component_;
	CrossThreatComponentGrid air_cross_component_;
	TileGrid<int> ground_splash_distance_;
	
	void apply_weapon(TileGrid<bool>& grid,const InformationUnit& information_unit,bool air);
	void apply_detection(TileGrid<bool>& grid,const InformationUnit& information_unit);
	void apply(TileGrid<bool>& grid,const InformationUnit& information_unit,int range);
	void apply_splash(TileGrid<int>& grid,const InformationUnit& information_unit,WeaponType weapon_type);
	
	template <typename Grid>
	void calculate_component(Grid&& threat_grid,TileGrid<int>& component_grid)
	{
		component_grid.clear();
		int component_counter = 1;
		for (int y = 0; y < Broodwar->mapHeight(); y++) {
			for (int x = 0; x < Broodwar->mapWidth(); x++) {
				FastTilePosition tile_position(x, y);
				if (component_grid[tile_position] > 0 ||
					threat_grid(tile_position)) continue;
				
				int component = component_counter++;
				std::queue<FastTilePosition> queue;
				queue.push(tile_position);
				component_grid[tile_position] = component;
				
				while (!queue.empty()) {
					FastTilePosition tile_position = queue.front();
					queue.pop();
					
					for (auto& delta : { FastTilePosition(-1, 0), FastTilePosition(1, 0), FastTilePosition(0, -1), FastTilePosition(0, 1) }) {
						FastTilePosition new_tile_position = tile_position + delta;
						if (new_tile_position.isValid() &&
							component_grid[new_tile_position] == 0 &&
							!threat_grid(new_tile_position)) {
							queue.push(new_tile_position);
							component_grid[new_tile_position] = component;
						}
					}
				}
			}
		}
	}
};

class UnitGrid : public Singleton<UnitGrid>
{
public:
	UnitGrid();
	void update();
	const std::vector<const InformationUnit*>& collision_units_in_tile(FastTilePosition tile_position) const { return unit_collision_grid_[tile_position.y * 256 + tile_position.x]; }
	const InformationUnit* building_in_tile(FastTilePosition tile_position) const { return building_grid_[tile_position]; }
	bool is_friendly_building_in_tile(UnitType type,FastTilePosition tile_position) const;
	bool is_completed_friendly_building_in_tile(UnitType type,FastTilePosition tile_position) const;
	
private:
	std::array<std::vector<const InformationUnit*>,256*256> unit_collision_grid_;
	TileGrid<const InformationUnit*> building_grid_;
};

class RoomGrid : public Singleton<RoomGrid>
{
public:
	void init();
	void draw() const;
	
	int room(FastWalkPosition walk_position) const { return room_.get(walk_position); }
private:
	WalkGrid<int> room_;
};

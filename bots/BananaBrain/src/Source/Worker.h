#pragma once

class Worker;
class WorkerOrder;

class WorkerAllocation
{
public:
	WorkerAllocation() {}
	WorkerAllocation(const std::vector<std::pair<Unit,const BWEM::Base*>>& minerals,
					 const std::vector<std::pair<Unit,const BWEM::Base*>>& refineries,
					 const std::map<Unit,Worker,CompareUnitByID>& worker_map);
	Unit pick_mineral_for_worker(const Worker& worker);
	std::vector<Unit> minerals_with_worker_count(int worker_count,Unit worker_unit);
	Unit pick_refinery_for_worker(const Worker& worker);
	std::vector<Unit> unsaturated_refineries(Unit worker_unit);
	Unit closest_unit(const std::vector<Unit>& units,Unit target_unit);
	int max_workers() const;
	double average_workers_per_mineral() const;
	int count_refinery_workers() const;
	
private:
	std::map<Unit,int> mineral_map_;
	std::map<Unit,int> refinery_map_;
	std::map<Unit,const BWEM::Base*> base_map_;
};

struct WorkerPositionAndVelocity
{
	FastPosition position;
	int velocity_x;
	int velocity_y;
	
	bool operator<(const WorkerPositionAndVelocity& o) const
	{
		return std::make_tuple(position, velocity_x, velocity_y) < std::make_tuple(o.position, o.velocity_x, o.velocity_y);
	}
};

class Worker {
public:
	Worker(Unit unit);
	
	Unit unit() const { return unit_; }
	const WorkerOrder* order() const { return order_.get(); }
	void apply_orders();
	void draw();
	
	void idle();
	void flee();
	void gather(Unit gather_target);
	void build(UnitType building_type,TilePosition building_position);
	void continue_build(Unit building);
	void defend_base(const BWEM::Base *base,bool fight_to_the_death);
	void defend_building(Unit building_unit);
	void defend_cannon_rush();
	void scout_for_proxies();
	void scout();
	void wait_at_proxy_location(Position position);
	void block_position(Position position);
	void repair(Unit repair_target);
	void combat();
private:
	Unit unit_;
	std::unique_ptr<WorkerOrder> order_;
};

class WorkerOrder {
public:
	WorkerOrder(Worker& worker) : worker_(worker), unit_(worker_.unit()) {}
	virtual ~WorkerOrder() {}
	
	virtual void apply_orders() = 0;
	virtual bool is_done() const = 0;
	virtual bool is_idle() const { return false; }
	virtual bool is_pullable() const { return false; }
	virtual bool is_defending() const { return false; }
	virtual bool is_scouting() const { return false; }
	virtual bool is_combat() const { return false; }
	virtual const BWEM::Base* scout_base() const { return nullptr; }
	virtual Unit gather_target() const { return nullptr; }
	virtual UnitType building_type() const { return UnitTypes::None; }
	virtual TilePosition building_position() const { return TilePositions::None; }
	virtual Unit building() const { return nullptr; }
	virtual Unit repair_target() const { return nullptr; }
	virtual void draw() const {}
	virtual MineralGas reserved_resources() const { return MineralGas(); }
	virtual bool need_detection() const { return false; }
	virtual Position wait_at_proxy_location() const { return Positions::None; }
	
protected:
	Worker& worker_;
	Unit unit_;
	
	bool repel_enemy_units() const;
	bool move_to_closest_safe_base() const;
};

class BuildWorkerOrder : public WorkerOrder {
public:
	static constexpr int kBuildTimeoutFrames = 240;
	static constexpr int kResourceTimeoutFrames = 240;
	
	BuildWorkerOrder(Worker& worker,UnitType building_type,TilePosition building_position) : WorkerOrder(worker), building_type_(building_type), building_position_(building_position) {}
	
	virtual UnitType building_type() const override { return building_type_; }
	virtual TilePosition building_position() const override { return building_position_; }
	virtual Unit building() const override { return building_; }
	virtual bool is_done() const override;
	virtual void apply_orders() override;
	virtual void draw() const override;
	virtual MineralGas reserved_resources() const override;
	virtual bool need_detection() const override { return need_detection_; }
	
private:
	UnitType building_type_;
	TilePosition building_position_;
	int build_timeout_frame_ = -1;
	int resource_timeout_frame_ = -1;
	int need_detection_counter_ = 0;
	bool constructing_seen_true_ = false;
	bool failed_ = false;
	bool need_detection_ = false;
	bool clockwise_ = true;
	Unit building_ = nullptr;
	std::vector<Unit> colliding_mines;
	std::vector<Unit> colliding_visible;
	std::vector<Unit> colliding_invisible;
	
	static constexpr double kMoveAroundAngle = (15.0 / 180.0) * M_PI;
	
	bool is_not_build_base();
	void move_around();
	bool walkable_line(Position position);
	void determine_colliding_units(std::vector<Unit>& colliding_mines,
								   std::vector<Unit>& colliding_visible,
								   std::vector<Unit>& colliding_invisible) const;
	bool building_exists() const;
};

class CombatWorkerOrder : public WorkerOrder {
public:
	CombatWorkerOrder(Worker& worker) : WorkerOrder(worker) {}
	
	virtual void apply_orders() override {}
	virtual bool is_done() const override { return false; }
	virtual bool is_combat() const override { return true; }
};

class ContinueBuildWorkerOrder : public WorkerOrder {
public:
	ContinueBuildWorkerOrder(Worker& worker,Unit building) : WorkerOrder(worker), build_unit_seen_(false), failed_(false), building_(building) {}
	
	virtual UnitType building_type() const override { return building_->getType(); }
	virtual TilePosition building_position() const override { return building_->getTilePosition(); }
	virtual Unit building() const override { return building_; }
	virtual bool is_done() const override;
	virtual void apply_orders() override;
	virtual void draw() const override;
	
private:
	bool build_unit_seen_;
	bool failed_;
	Unit building_;
};

class DefendBaseOrder : public WorkerOrder {
public:
	DefendBaseOrder(Worker& worker,const BWEM::Base* base,bool fight_to_the_death) : WorkerOrder(worker), base_(base), fight_to_the_death_(fight_to_the_death) {}
	
	virtual void apply_orders() override;
	virtual bool is_done() const override { return done_; }
	virtual bool is_defending() const override { return true; }
	
private:
	const BWEM::Base* base_;
	bool done_ = false;
	bool fight_to_the_death_;
};

class DefendBuildingOrder : public WorkerOrder {
public:
	DefendBuildingOrder(Worker& worker,Unit building_unit) : WorkerOrder(worker), building_unit_(building_unit) {}
	
	virtual void apply_orders() override;
	virtual bool is_done() const override { return done_; }
	virtual bool is_defending() const override { return true; }
	
private:
	Unit building_unit_;
	bool done_ = false;
};

class DefendCannonRushOrder : public WorkerOrder {
public:
	DefendCannonRushOrder(Worker& worker) : WorkerOrder(worker) {}
	
	virtual void apply_orders() override;
	virtual bool is_done() const override { return done_; }
	virtual bool is_defending() const override { return true; }
	
	static bool should_apply_cannon_rush_defense(const InformationUnit& information_unit,bool allow_pylons_and_probes=false);
private:
	bool done_ = false;
};

class FleeWorkerOrder : public WorkerOrder {
public:
	FleeWorkerOrder(Worker& worker) : WorkerOrder(worker) {}
	
	virtual bool is_done() const override;
	virtual void apply_orders() override;
	
private:
	int timeout_frame_ = -1;
};

class GatherWorkerOrder : public WorkerOrder {
public:
	GatherWorkerOrder(Worker& worker,Unit gather_target) : WorkerOrder(worker), gather_target_(gather_target) {}
	
	virtual Unit gather_target() const override { return gather_target_; }
	virtual bool is_done() const override;
	virtual bool is_pullable() const override;
	virtual void apply_orders() override;
	
private:
	Unit gather_target_;
	bool return_started_ = false;
	std::map<int,WorkerPositionAndVelocity> position_history_;
	
	bool is_carrying() const;
	bool is_gathering() const;
	bool is_base_valid() const;
	bool optimal_mineral_mining();
};

class IdleWorkerOrder : public WorkerOrder {
public:
	IdleWorkerOrder(Worker& worker) : WorkerOrder(worker) {}
	~IdleWorkerOrder() {}
	virtual void apply_orders() override;
	virtual bool is_done() const override { return false; }
	virtual bool is_idle() const override { return true; }
	virtual bool is_pullable() const override { return true; }
};

class RepairWorkerOrder : public WorkerOrder {
public:
	RepairWorkerOrder(Worker& worker,Unit repair_target) : WorkerOrder(worker), repair_target_(repair_target), repairing_seen_(false) {}
	
	virtual bool is_done() const override;
	virtual Unit repair_target() const override { return repair_target_; }
	virtual void apply_orders() override;
	
private:
	Unit repair_target_;
	bool repairing_seen_;
};

class ScoutForProxies : public WorkerOrder {
public:
	ScoutForProxies(Worker& worker) : WorkerOrder(worker) {}
	~ScoutForProxies() {}
	virtual void apply_orders() override;
	virtual bool is_done() const override;
private:
	bool done_ = false;
	int pass_ = 0;
	std::set<FastTilePosition> positions_;
	
	bool enemy_opening_known() const;
	void fill_positions();
	void remove_visible_positions();
};

class ScoutWorkerOrder : public WorkerOrder {
public:
	ScoutWorkerOrder(Worker& worker);
	~ScoutWorkerOrder() {}
	virtual void apply_orders() override;
	virtual bool is_done() const override;
	virtual bool is_scouting() const override { return true; }
	virtual const BWEM::Base* scout_base() const override { return current_target_base_; }
private:
	enum class Mode
	{
		LookAtMapCenter,
		SearchBase,
		MoveAround,
		LookAtNatural,
		MoveAroundFromNatural,
		WaitAtNatural,
		Done
	};
	
	static constexpr double kMoveAroundAngle = (15.0 / 180.0) * M_PI;
	static constexpr double kSafeMoveAroundAngle = (20.0 / 180.0) * M_PI;
	static constexpr double kSafeMoveAngle = (90.0 / 180.0) * M_PI;
	static constexpr int kWaitAtNaturalLimitFrame = 11428;
	
	const BWEM::Base* current_target_base_;
	Mode mode_;
	bool clockwise_ = true;
	
	Position determine_wait_at_natural_position() const;
	bool army_sees_worker() const;
	bool should_look_at_natural() const;
	bool enemy_has_non_start_resource_depot() const;
	bool other_scout_found_base() const;
	void move_around();
	bool walkable_line(Position position);
	std::tuple<const InformationUnit*,int> determine_danger(Position position);
	static bool is_defense_building(const InformationUnit* information_unit);
	bool safe_move_around(Position target_position);
	bool safe_move(Position target_position);
	const BWEM::Base* closest_undiscovered_starting_base() const;
};

class WaitAtProxyLocationWorkerOrder : public WorkerOrder {
public:
	WaitAtProxyLocationWorkerOrder(Worker& worker,Position proxy_location) : WorkerOrder(worker), proxy_location_(proxy_location) {}
	~WaitAtProxyLocationWorkerOrder() {}
	virtual void apply_orders() override;
	virtual bool is_done() const override { return false; }
	virtual bool is_pullable() const override { return true; }
	virtual Position wait_at_proxy_location() const override { return proxy_location_; }
	
private:
	Position proxy_location_;
};

class WorkerManager : public Singleton<WorkerManager>
{
public:
	static constexpr int kWorkerCap = 70;
	
	void before();
	void after();
	bool assign_building_request(UnitType building_type,TilePosition building_position);
	int estimate_worker_travel_frames_for_building_request(UnitType building_type,TilePosition building_position);
	void repair_target(Unit unit,int worker_count);
	void continue_unfinished_building(Unit unit);
	void send_initial_scout();
	void send_second_scout();
	void stop_scouting();
	bool is_scouting();
	void send_proxy_scout();
	void send_proxy_builder(Position proxy_position,int arrive_frame);
	void defend_building(Unit building_unit,int max_defending_workers);
	void combat(int worker_count);
	void combat(int worker_count,Position position);
	void stop_combat();
	bool is_combat(Unit worker);
	void apply_worker_orders();
	void draw_for_workers();
	bool should_scout_map_center();
	void onUnitLost(Unit unit);
	void onUnitMorph(Unit unit);
	void cancel_building_construction(TilePosition building_position);
	void cancel_building_construction_for_type(UnitType building_type);
	void cancel_extra_planned_building_construction_for_type(UnitType building_type,int keep_count);
	void cancel_expansion_hatcheries();
	
	const std::map<Unit,Worker,CompareUnitByID>& worker_map() { return worker_map_; }
	int determine_max_workers() const { return std::min(worker_allocation_.max_workers(), kWorkerCap); }
	double average_workers_per_mineral() const { return worker_allocation_.average_workers_per_mineral(); }
	MineralGas worker_reserved_mineral_gas() const;
	bool is_worker_mining_allowed_from_base(const BWEM::Base* base,Unit worker_unit) const;
	bool is_safe_base(const BWEM::Base* base) const { return !contains(base_unsafe_until_frame_, base); }
	bool is_base_under_attack(const BWEM::Base* base) const { return contains(base_under_attack_until_frame_, base); }
	bool scout_wait_at_natural() const { return scout_wait_at_natural_; }
	void set_scout_wait_at_natural(bool scout_wait_at_natural) { scout_wait_at_natural_ = scout_wait_at_natural; }
	bool scout_map_center() const { return scout_map_center_; }
	void set_scout_map_center(bool scout_map_center) { scout_map_center_ = scout_map_center; }
	Position initial_scout_death_position() const { return initial_scout_death_position_; }
	int limit_refinery_workers() const { return limit_refinery_workers_; }
	void set_limit_refinery_workers(int limit_refinery_workers) { limit_refinery_workers_ = limit_refinery_workers; }
	bool force_refinery_workers() const { return force_refinery_workers_; }
	void set_force_refinery_workers(bool force_refinery_workers) { force_refinery_workers_ = force_refinery_workers; }
	int lost_worker_count() const { return lost_worker_count_; }
	bool sent_initial_scout() const { return sent_initial_scout_; }
	
	void init_optimal_mining_data();
	void store_optimal_mining_data();
	std::set<WorkerPositionAndVelocity>& optimal_mining_set_for_mineral(Unit mineral) { return optimal_mining_map_[mineral->getInitialTilePosition()]; }
	
private:
	std::map<Unit,Worker,CompareUnitByID> worker_map_;
	WorkerAllocation worker_allocation_;
	bool sent_initial_scout_ = false;
	bool sent_second_scout_ = false;
	bool scout_wait_at_natural_ = true;
	bool scout_map_center_ = false;
	bool sent_proxy_scout_ = false;
	Position initial_scout_death_position_ = Positions::None;
	std::map<const BWEM::Base*,int> base_unsafe_until_frame_;
	std::map<const BWEM::Base*,int> base_under_attack_until_frame_;
	std::map<const BWEM::Base*,int> base_defended_until_frame_;
	int limit_refinery_workers_ = -1;
	bool force_refinery_workers_ = false;
	int lost_worker_count_ = 0;
	std::map<FastTilePosition,std::set<WorkerPositionAndVelocity>> optimal_mining_map_;
	
	void register_new_workers();
	void done_workers_to_idle();
	void update_base_unsafety();
	void remove_expired(std::map<const BWEM::Base*,int>& base_frame_map);
	void update_worker_allocation();
	bool mineral_near_planned_base_defense_creep_colony(const BWEM::Base* base,Unit mineral_unit);
	void update_worker_gather_orders();
	void update_worker_defend_orders();
	bool assign_worker_to_gas();
	void defend_base_with_workers_if_needed();
	void defend_cannon_rush();
	bool send_scout();
	bool check_proxy_builder_time(Position worker_position,Position proxy_position,int arrive_frame);
	Worker* pick_worker_for_task();
	Worker* pick_worker_for_task(Position position);
	Worker* pick_worker_for_task(UnitType unit_type,FastTilePosition tile_position);
	bool is_dangerous_worker(Unit enemy_unit);
	bool is_dangerous_unit(Unit unit);
};

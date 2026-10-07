#pragma once

struct TentativeStorm
{
	TentativeStorm() {}
	TentativeStorm(Unit unit,int expire_frame,Position position) : unit(unit), expire_frame(expire_frame), position(position) {}
	
	Unit unit;
	int expire_frame;
	Position position;
};

struct TentativeMindControl
{
	TentativeMindControl() {}
	TentativeMindControl(Unit unit,int expire_frame,Unit target) : unit(unit), expire_frame(expire_frame), target(target) {}
	
	Unit unit;
	int expire_frame;
	Unit target;
};

struct TentativeStasis
{
	TentativeStasis() {}
	TentativeStasis(Unit unit,int expire_frame,Position position) : unit(unit), expire_frame(expire_frame), position(position) {}
	
	Unit unit;
	int expire_frame;
	Position position;
};

struct TentativeDarkSwarm
{
	TentativeDarkSwarm() {}
	TentativeDarkSwarm(Unit unit,int expire_frame,Position position) : unit(unit), expire_frame(expire_frame), position(position) {}
	
	Unit unit;
	int expire_frame;
	Position position;
};

struct TentativePlague
{
	TentativePlague() {}
	TentativePlague(Unit unit,int expire_frame,Position position) : unit(unit), expire_frame(expire_frame), position(position) {}
	
	Unit unit;
	int expire_frame;
	Position position;
};

struct TentativeIrradiate
{
	TentativeIrradiate() {}
	TentativeIrradiate(Unit unit,int expire_frame,Unit target) : unit(unit), expire_frame(expire_frame), target(target) {}
	
	Unit unit;
	int expire_frame;
	Unit target;
};

struct TentativeEMP
{
	TentativeEMP() {}
	TentativeEMP(Unit unit,int expire_frame,Position position) : unit(unit), expire_frame(expire_frame), position(position) {}
	
	Unit unit;
	int expire_frame;
	Position position;
};

struct TentativeMine
{
	TentativeMine() {}
	TentativeMine(Unit unit,int expire_frame,Position position) : unit(unit), expire_frame(expire_frame), position(position) {}
	
	Unit unit;
	int expire_frame;
	Position position;
};

struct TentativeYamato
{
	TentativeYamato() {}
	TentativeYamato(Unit unit,int expire_frame,Unit target) : unit(unit), expire_frame(expire_frame), target(target) {}
	
	Unit unit;
	int expire_frame;
	Unit target;
};

struct TentativeLockdown
{
	TentativeLockdown() {}
	TentativeLockdown(Unit unit,int expire_frame,Unit target) : unit(unit), expire_frame(expire_frame), target(target) {}
	
	Unit unit;
	int expire_frame;
	Unit target;
};

struct TentativeScan
{
	TentativeScan() {}
	TentativeScan(Unit unit,int expire_frame,Position position) : unit(unit), expire_frame(expire_frame), position(position) {}
	
	Unit unit;
	int expire_frame;
	Position position;
};

enum class TransportCommand {
	Default,
	LoadForDropInEnemyBase,
	DropInEnemyBase,
	BulldogLoadZealots,
	BulldogApproach,
	BulldogDropZealots,
	ReaverMicro
};

struct TransportState
{
	TransportCommand command = TransportCommand::Default;
	Position position = Positions::None;
	Unit reaver_unit = nullptr;
	UnitType unit_type = UnitTypes::None;
};

enum class OverlordCommand {
	Default,
	InitialScout,
	ScoutMapCenter,
	WaitInBase,
	Detect,
	WorkerNeedsDetection
};

struct OverlordState
{
	OverlordCommand command = OverlordCommand::Default;
	const BWEM::Base* base = nullptr;
	Position position = Positions::None;
};

struct AirToAirTarget
{
	Position position;
	int expire_frame;
};

struct MutaliskDive
{
	Unit target;
	bool ready_seen;
	int expire_frame;
};

struct ObserverTarget
{
	bool scouting = true;
	Position position = Positions::None;
	int expire_frame;
};

struct SiegeTankState
{
	Position defense_position = Positions::None;
	int defense_frame_limit = -1;
};

struct VultureState
{
	Position defense_position = Positions::None;
	int defense_frame_limit = -1;
};

class DragoonState
{
public:
	static constexpr int kDragoonAttackFrames = 6;
	
	void update(Unit unit);
	bool is_busy();
private:
	Unit unit_ = nullptr;
	int last_attack_start_frame_ = -1;
};

class UnstickState
{
public:
	void update(Unit unit);
	bool is_stuck();
private:
	Unit unit_ = nullptr;
	int stuck_since_ = -1;
	Position last_position_ = Positions::None;
};

class CombatState
{
public:
	CombatState(Unit unit) : unit_(unit) {}
	Unit unit() const { return unit_; }
	const Position& target_position() const { return target_position_; }
	void set_target_position(const Position& target_position) { target_position_ = target_position; }
	const Position& stage_position() const { return stage_position_; }
	void set_stage_position(const Position& stage_position) { stage_position_ = stage_position; }
	bool always_advance() const { return always_advance_; }
	void set_always_advance(bool always_advance) { always_advance_ = always_advance; }
	bool block_chokepoint() const { return block_chokepoint_; }
	void set_block_chokepoint(bool block_chokepoint) { block_chokepoint_ = block_chokepoint; }
	bool near_target_only() const { return near_target_only_; }
	void set_near_target_only(bool near_target_only) { near_target_only_ = near_target_only; }
private:
	int last_controlled_frame_ = -1;
	Unit unit_;
	Position target_position_ = Positions::None;
	Position stage_position_ = Positions::None;
	bool always_advance_ = false;
	bool block_chokepoint_ = false;
	bool near_target_only_ = false;
	
	Unit shield_battery_ = nullptr;
	
	Unit shield_battery() const { return shield_battery_; }
	void set_shield_battery(Unit shield_battery) { shield_battery_ = shield_battery; }
	
	friend class MicroManager;
};

struct CombatUnitTarget
{
	static int constexpr kTargetUpdateFrames = 2;
	
	Unit unit = nullptr;
	int frame = -1;
	int last_switch_frame = -1;
	bool enable_advance = true;
	bool enable_retreat = true;
	
	bool should_update_target(Unit combat_unit) const;
};

class MicroManager : public Singleton<MicroManager>
{
public:
	void prepare_combat();
	void apply_combat_orders();
	void draw();
	void perform_drop(UnitType unit_type);
	void perform_bulldog_zealot_drop();
	bool is_load_bunkers() { return load_bunkers_; }
	void unload_bunkers();
	
	std::map<Unit,CombatState>& combat_state() { return combat_state_; }
	const std::map<Unit,OverlordState>& overlord_state() { return overlord_state_; }
	int prevent_high_templar_from_archon_warp_count() const { return prevent_high_templar_from_archon_warp_count_; }
	void set_prevent_high_templar_from_archon_warp_count(int prevent_high_templar_from_archon_warp_count) { prevent_high_templar_from_archon_warp_count_ = prevent_high_templar_from_archon_warp_count; }
	bool force_archon_warp() const { return force_archon_warp_; }
	void set_force_archon_warp(bool force_archon_warp) { force_archon_warp_ = force_archon_warp; }
	int requested_dark_archon_count() const { return requested_dark_archon_count_; }
	void set_requested_dark_archon_count(int requested_dark_archon_count) { requested_dark_archon_count_ = requested_dark_archon_count; }
	void reset_drop_reaver_to_build_scarab() { drop_reaver_to_build_scarab_.clear(); }
	void add_drop_reaver_to_build_scarab(Unit reaver_unit) { drop_reaver_to_build_scarab_.insert(reaver_unit); }
	void set_spot_with_barracks_and_engineering_bay(bool spot_with_barracks_and_engineering_bay) { spot_with_barracks_and_engineering_bay_ = spot_with_barracks_and_engineering_bay; }
	void set_overlord_scout_map_center(bool overlord_scout_map_center) { overlord_scout_map_center_ = overlord_scout_map_center; }
	
	std::vector<Position> list_existing_storm_positions();
	const std::vector<TentativeMine>& tentative_mines() const { return tentative_mines_; }
	
	static bool runby_possible();
	static bool runby_possible(const std::set<Unit>& runby_units);
private:
	struct DarkTemplarPathNearbyUnits
	{
		std::vector<std::pair<const InformationUnit*,int>> enemy_attack_units;
		std::vector<std::pair<const InformationUnit*,int>> enemy_detector_units;
		std::vector<std::pair<const InformationUnit*,int>> enemy_attackable_units;
		
		bool is_nonempty()
		{
			return !enemy_attack_units.empty() || !enemy_attackable_units.empty();
		}
	};
	
	static constexpr int kStormThreshold = 5;
	static constexpr int kStormThresholdUnderAttack = 1;
	static constexpr int kStasisRadius = 48;
	static constexpr int kStasisThreshold = 5;
	static constexpr int kDarkSwarmThreshold = 4;
	static constexpr int kDarkSwarmThresholdUnderAttack = 1;
	static constexpr int kPlagueRadius = 64;
	static constexpr int kPlagueMaxDamage = 296;
	static constexpr int kPlagueThreshold = 100;
	static constexpr int kPlagueThresholdUnderAttack = 1;
	static constexpr int kMedicHealRange = 2 * 32;
	static constexpr int kBlockadeBreakingRange = 4 * 32;
	static constexpr int kShieldBatteryRechargeRange = 4 * 32;
	static constexpr int kShieldBatterySeekRange = 10 * 32;
	static constexpr int kDarkTemplarPathMaxDistance = 320;
	static constexpr int kDarkTemplarPathStepSize = 8;
	static constexpr int kSiegeFrames = 70;
	static constexpr int kUnsiegeFrames = 65;
	static constexpr int kMutaliskBorderRepulseDistance = 64;
	static constexpr int kCarrierLeashRange = 10 * 32;
	
	std::map<Unit,CombatState> combat_state_;
	
	std::vector<Unit> all_enemy_units_;
	std::vector<Unit> harassable_enemy_units_;
	std::set<Unit> enemy_units_threatening_buildings_or_workers_;
	std::set<Unit> loading_units_;
	std::set<Unit> drop_reaver_to_build_scarab_;
	
	std::vector<Unit> transports_;
	std::vector<Unit> overlords_;
	std::vector<Unit> combat_units_;
	std::vector<Unit> dark_templars_;
	std::vector<Unit> lurkers_;
	std::vector<Unit> siege_tanks_;
	std::vector<Unit> air_to_air_units_;
	std::vector<Unit> mutalisks_;
	std::vector<Unit> observers_;
	std::vector<Unit> arbiters_;
	std::vector<Unit> science_vessels_;
	std::vector<Unit> high_templars_;
	std::vector<Unit> defilers_;
	std::vector<Unit> medics_;
	std::vector<Unit> dark_archons_;
	std::vector<Unit> comsat_stations_;
	std::vector<Unit> flying_buildings_;
	std::vector<Unit> extended_combat_units_;
	
	std::map<Unit,DragoonState> dragoon_state_;
	std::map<Unit,UnstickState> unstick_state_;
	std::vector<TentativeStorm> tentative_storms_;
	std::vector<TentativeMindControl> tentative_mind_controls_;
	std::vector<TentativeStasis> tentative_stasises_;
	std::vector<TentativeDarkSwarm> tentative_dark_swarms_;
	std::vector<TentativePlague> tentative_plagues_;
	std::vector<TentativeIrradiate> tentative_irradiates_;
	std::vector<TentativeEMP> tentative_emps_;
	std::vector<TentativeMine> tentative_mines_;
	std::vector<TentativeYamato> tentative_yamatoes_;
	std::vector<TentativeLockdown> tentative_lockdowns_;
	std::vector<TentativeScan> tentative_scans_;
	std::map<Unit,TransportState> transport_state_;
	std::map<Unit,OverlordState> overlord_state_;
	std::map<Unit,AirToAirTarget> air_to_air_targets_;
	std::map<Unit,MutaliskDive> mutalisk_dive_;
	std::set<Unit> mutalisk_runby_enemies_;
	bool mutalisk_defense_ = false;
	std::map<Unit,ObserverTarget> observer_targets_;
	std::map<Unit,SiegeTankState> siege_tank_state_;
	std::map<Unit,VultureState> vulture_state_;
	std::map<Unit,CombatUnitTarget> combat_unit_targets_;
	std::map<Unit,std::vector<Unit>> scourge_target_map_;
	std::set<Unit> dark_templar_turn_;
	int dark_templar_turn_last_reset_ = 0;
	int prevent_high_templar_from_archon_warp_count_ = -1;
	bool force_archon_warp_ = false;
	int requested_dark_archon_count_ = 0;
	bool scout_reached_base_ = false;
	bool load_bunkers_ = true;
	bool spot_with_barracks_and_engineering_bay_ = false;
	bool overlord_scout_map_center_ = false;
	bool overlord_scout_map_center_ordered_ = false;
	
	std::set<Unit> units_near_base_;
	std::set<Unit> units_near_main_base_;
	std::set<Unit> ignore_when_attacking_;
	
	std::vector<Unit> run_by_defense_;
	Position run_by_target_position_ = Positions::None;
	std::set<Unit> running_by_;
	std::set<Unit> desperados_;
	
	void update_units_lists();
	static int offense_distance_to_base(const EnemyCluster& cluster,const std::vector<Unit>& buildings_outside_base);
	static int offense_distance_to_base(const InformationUnit* enemy_unit,const std::vector<Unit>& buildings_outside_base);
	void update_units_near_base();
	void update_units_near_main_base();
	void update_ignore_when_attacking();
	void remove_nonexistant_units(std::set<Unit>& unit_set);
	bool no_cluster_units_near_base(const EnemyCluster& cluster);
	bool is_scouting_worker_cluster(const EnemyCluster& cluster);
	void apply_transport_orders();
	void apply_overlord_orders();
	void apply_combat_unit_orders();
	void apply_dark_templar_orders();
	void apply_lurker_orders();
	void apply_siege_tank_orders();
	void apply_air_to_air_unit_orders();
	void apply_mutalisk_orders();
	void apply_observer_orders();
	void apply_arbiter_orders();
	void apply_science_vessel_orders();
	void apply_high_templar_orders();
	void apply_defiler_orders();
	void apply_medic_orders();
	void apply_dark_archon_orders();
	void apply_comsat_station_orders();
	void apply_flying_building_orders();
	bool carrier_interceptors_attacking(Unit carrier_unit);
	Unit determine_carrier_target(Unit carrier_unit);
	bool should_carrier_retarget(Unit carrier_unit,Unit other_target_unit);
	Position determine_carrier_leash_position(Unit carrier_unit);
	Position determine_spot_position(Position start_position,Position target_position);
	void expire_tentative_abilities();
	void set_nearby_units_for_kiting(std::vector<Unit>& nearby_enemy_units,std::vector<Unit>& nearby_friendly_ground,Unit combat_unit);
	std::vector<std::pair<std::vector<Unit>,FastPosition>> group_mutalisks(const std::vector<Unit>& units);
	std::map<Unit,int> determine_units_per_group(const std::vector<Unit>& units);
	bool is_siege_allowed(Unit combat_unit);
	bool allow_overlord_wait_in_base();
	void order_overlord_wait_in_base(const BWEM::Base* base);
	void order_overlord_detect();
	void order_overlord_worker_need_detection();
	void order_observer_worker_need_detection();
	Position determine_worker_need_detection_position();
	static int distance_to_base(const BWEM::Base *base,const InformationUnit* enemy_unit);
	int distance_to_proxy(const InformationUnit* enemy_unit);
	Position calculate_interception_position(Unit combat_unit,Unit enemy_unit);
	bool unit_in_safe_location(Unit unit);
	TransportState* find_transport_without_command();
	Position determine_first_detector_location(Unit special_unit);
	Position closest_sieged_tank(Position position);
	bool melee_unit_near_sieged_tank(Position tank_position);
	bool melee_unit_near_sieged_tank(Unit unit);
	bool load_closest_unit_of_type(Unit shuttle_unit,UnitType load_type,bool ignore_distance=false);
	void load_unit_into_transport(Unit transport_unit,Unit load_unit);
	bool reaver_in_shuttle_can_attack(Unit shuttle_or_reaver_unit);
	std::set<Unit> determine_unpaired_reavers();
	Position pick_air_scout_location();
	bool move_flyer_near_safe(Unit unit,Position position);
	bool move_flyer_near_safe_approach_unsafe(Unit unit,Position position);
	FastPosition find_closest_safe_position_near_target(Unit unit,Position position);
	bool move_into_bunker(Unit combat_unit,Position stage_position);
	void move_retreat(Unit unit,Position target_position);
	bool move_safe(Unit unit,Position target_position);
	void move_runby(Unit unit,Position target_position);
	void move_with_blockade_breaking(Unit unit,Position target_position);
	bool recharge_at_shield_battery(Unit unit);
	bool recharge_at_shield_battery(Unit unit,Unit shield_battery_unit);
	Unit determine_nearby_sieged_tank(Unit combat_unit);
	Unit combat_unit_closest_to_position(Position target_position);
	Unit combat_unit_in_base_closest_to_position(Position position,int max_base_distance=0);
	Unit combat_unit_closest_to_special_unit(Unit special_unit);
	bool valid_target(Unit combat_unit,Unit enemy_unit);
	bool is_reaver_can_attack_in_range(Unit combat_unit,Unit enemy_unit);
	Unit determine_incoming_mine(Unit combat_unit);
	bool dark_templar_path_based_order(Unit combat_unit,const DarkTemplarPathNearbyUnits& nearby_units);
	std::tuple<Unit,bool,bool> select_enemy_unit_for_combat_unit(Unit combat_unit);
	std::tuple<Unit,bool,bool> select_enemy_unit_for_lurker(Unit combat_unit);
	std::tuple<std::vector<Unit>,bool,bool> determine_attackable_enemy_units(Unit combat_unit);
	std::tuple<bool,bool> determine_advance_retreat_for_special_unit(Unit special_unit);
	bool determine_allow_stim(Unit combat_unit);
	Unit select_enemy_unit_for_reaver(Unit combat_unit,const std::vector<Unit>& enemy_units);
	Unit select_enemy_unit_for_combat_unit(Unit combat_unit,const std::vector<Unit>& enemy_units);
	Unit select_enemy_unit_air_to_air(Unit combat_unit,const std::vector<Unit>& enemy_units);
	static int target_tie_breaker(Unit combat_unit,Unit target_unit);
	static double calculate_kill_time(Unit combat_unit,Unit enemy_unit);
	static double calculate_incoming_dpf_sum(Unit combat_unit,Position position);
	static double calculate_incoming_dpf(Unit combat_unit,Position position,const InformationUnit& enemy_unit);
	static double calculate_incoming_dpf_for_medic(Unit combat_unit,Position position,const InformationUnit& ememy_unit);
	static bool is_suicidal_with_target(const InformationUnit& enemy_unit);
	static double calculate_chance_to_hit(Unit attacking_unit,Unit defending_unit);
	static double calculate_chance_to_hit(UnitType attacking_unit_type,Position attacking_unit_position,Position defending_unit_position);
	static double calculate_splash_factor(Unit attacking_unit,Unit defending_unit);
	static double calculate_splash_factor(UnitType attacking_unit_type,bool defending_unit_flying);
	static std::map<Unit,std::pair<int,FastPosition>> calculate_approach_distances_and_positions(Unit combat_unit,std::vector<Unit>& enemy_units);
	Unit replace_by_repairing_scv(Unit combat_unit,Unit target);
	bool storm(Unit high_templar_unit);
	static bool should_emergency_storm(Unit high_templar_unit);
	static std::pair<int,int> storm_score(Position position,const std::vector<std::pair<Unit,int>>& unit_scores);
	bool scan_cloaked_unit(Unit comsat_unit);
	bool scan_base(Unit comsat_unit);
	bool irradiate(Unit science_vessel_unit);
	bool yamato(Unit battle_cruiser_unit);
	bool lockdown(Unit ghost_unit);
	bool should_lockdown_any_target(Unit ghost_unit);
	bool stasis(Unit arbiter_unit);
	std::tuple<int,int,int> stasis_score(Position arbiter_position,Position position,const std::vector<std::pair<Unit,int>>& unit_scores);
	bool dark_swarm(Unit defiler_unit);
	bool should_emergency_dark_swarm(Unit defiler_unit);
	std::pair<int,int> dark_swarm_score(Position position,const std::vector<std::pair<Unit,int>>& unit_scores);
	bool unit_is_fighting(Unit unit);
	bool plague(Unit defiler_unit);
	std::pair<int,int> plague_score(Position position,const std::vector<std::pair<Unit,int>>& unit_scores);
	bool should_emergency_plague(Unit defiler_unit);
	bool emp(Unit science_vessel_unit);
	std::pair<int,int> emp_score(Position position,const std::vector<std::tuple<Unit,int,bool>> unit_scores,bool emergency);
	bool should_emergency_emp(Unit science_vessel_unit);
	void mine(Unit vulture_unit,Position position);
	std::vector<Position> list_existing_scan_positions();
	static bool position_list_intersects(const std::vector<Position>& positions,Position candidate,int size);
	std::vector<Position> list_existing_dark_swarm_positions();
	bool position_list_intersects_square(const std::vector<Position>& positions,Position candidate,int size);
	Position calculate_single_target_position();
	void update_run_by();
	void end_run_by();
	static std::pair<UnitType,TilePosition> determine_building_at_position(Position target_position);
	std::set<Unit> determine_run_by_units_near_defense(std::vector<Unit> defense,Position target_position);
	static bool is_runby_unit_type(UnitType type);
	static bool runby_possible_with_units(const std::set<Unit>& runby_units);
	static std::vector<Unit> determine_defense_for_run_by(Position target_position);
	static bool check_run_by_damage(std::vector<Unit> defense,const std::set<Unit>& units_near_defense);
	
	template<typename Grid> bool line_safe(FastTilePosition a,FastTilePosition b,const Grid& grid) const
	{
		if (a.x == b.x) {
			int x = a.x;
			int y1 = std::min(a.y, b.y);
			int y2 = std::max(a.y, b.y);
			for (int y = y1; y <= y2; y++) if (!grid(x, y)) return false;
			return true;
		}
		
		if (a.y == b.y) {
			int y = a.y;
			int x1 = std::min(a.x, b.x);
			int x2 = std::max(a.x, b.x);
			for (int x = x1; x <= x2; x++) if (!grid(x, y)) return false;
			return true;
		}
		
		if (abs(a.x - b.x) >= abs(a.y - b.y)) {
			if (a.x > b.x) std::swap(a, b);
			int x1 = a.x;
			int x2 = b.x;
			int y1 = a.y;
			int y2 = b.y;
			double dx = x2 - x1;
			double dy = y2 - y1;
			double delta_error = std::abs(dy / dx);
			int ysign = (y2 > y1) ? 1 : -1;
			int y = y1;
			double error = 0.0;
			for (int x = x1; x <= x2; x++) {
				if (!grid(x, y)) return false;
				if (error < -1e-5 && !grid(x, y - 1)) return false;
				if (error > 1e-5 && !grid(x, y + 1)) return false;
				error += delta_error;
				if (error >= 0.5) {
					y += ysign;
					error -= 1.0;
				}
			}
			return true;
		} else {
			if (a.y > b.y) std::swap(a, b);
			int x1 = a.x;
			int x2 = b.x;
			int y1 = a.y;
			int y2 = b.y;
			double dx = x2 - x1;
			double dy = y2 - y1;
			double delta_error = std::abs(dx / dy);
			int xsign = (x2 > x1) ? 1 : -1;
			int x = x1;
			double error = 0.0;
			for (int y = y1; y <= y2; y++) {
				if (!grid(x, y)) return false;
				if (error < -1e-5 && !grid(x - 1, y)) return false;
				if (error > 1e-5 && !grid(x + 1, y)) return false;
				error += delta_error;
				if (error >= 0.5) {
					x += xsign;
					error -= 1.0;
				}
			}
			return true;
		}
	}
	
	template<typename Pred> FastTilePosition line_search(FastTilePosition a,FastTilePosition b,const Pred& pred) const
	{
		if (a.x == b.x) {
			int x = a.x;
			int y1 = std::min(a.y, b.y);
			int y2 = std::max(a.y, b.y);
			for (int y = y1; y <= y2; y++) if (pred(FastTilePosition(x, y))) return FastTilePosition(x, y);
			return TilePositions::None;
		}
		
		if (a.y == b.y) {
			int y = a.y;
			int x1 = std::min(a.x, b.x);
			int x2 = std::max(a.x, b.x);
			for (int x = x1; x <= x2; x++) if (pred(FastTilePosition(x, y))) return FastTilePosition(x, y);
			return TilePositions::None;
		}
		
		if (abs(a.x - b.x) >= abs(a.y - b.y)) {
			if (a.x > b.x) std::swap(a, b);
			int x1 = a.x;
			int x2 = b.x;
			int y1 = a.y;
			int y2 = b.y;
			double dx = x2 - x1;
			double dy = y2 - y1;
			double delta_error = std::abs(dy / dx);
			int ysign = (y2 > y1) ? 1 : -1;
			int y = y1;
			double error = 0.0;
			for (int x = x1; x <= x2; x++) {
				if (pred(FastTilePosition(x, y))) return FastTilePosition(x, y);
				error += delta_error;
				if (error >= 0.5) {
					y += ysign;
					error -= 1.0;
				}
			}
			return TilePositions::None;
		} else {
			if (a.y > b.y) std::swap(a, b);
			int x1 = a.x;
			int x2 = b.x;
			int y1 = a.y;
			int y2 = b.y;
			double dx = x2 - x1;
			double dy = y2 - y1;
			double delta_error = std::abs(dx / dy);
			int xsign = (x2 > x1) ? 1 : -1;
			int x = x1;
			double error = 0.0;
			for (int y = y1; y <= y2; y++) {
				if (pred(FastTilePosition(x, y))) return FastTilePosition(x, y);
				error += delta_error;
				if (error >= 0.5) {
					x += xsign;
					error -= 1.0;
				}
			}
			return TilePositions::None;
		}
	}
	
	class MinePlacementCheck
	{
	public:
		bool is_near_chokepoint(Position position);
		bool allow_mine_at(Position position,int min_distance);
	private:
		bool initialized_ = false;
		std::vector<Position> mines_;
		std::vector<InformationUnit*> resources_;
		
		void initialize();
	};
};

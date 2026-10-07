#pragma once

class EnemyCluster
{
public:
	static constexpr int kEngagementDistance = 32;
	static constexpr int kFrontDistance = 256;
	static constexpr int kMaxFrontStride = 512;
	
	EnemyCluster(std::vector<const InformationUnit*>& units);
	
	bool expect_win(Unit unit) const;
	bool is_engaged(Unit unit) const;
	bool is_nearly_engaged(Unit unit) const;
	bool in_front(Unit unit) const;
	bool near_front(Unit unit) const;
	bool stim_allowed(Unit unit) const;
	bool in_front_with_supply_at_least(Unit unit,int supply) const;
	
	const std::vector<const InformationUnit*>& units() const { return units_; }
	const std::set<Unit>& front_units() const { return front_units_; }
private:
	std::vector<const InformationUnit*> units_;
	std::map<Unit,int> engagament_distance_;
	std::vector<std::pair<Unit,int>> friendly_units_ascending_distance_;
	std::map<Unit,int> friendly_units_distance_;
	double defense_supply_;
	double defense_ranged_ground_supply_;
	double defense_air_attacking_supply_;
	double defense_air_attacking_air_supply_;
	double defense_air_supply_;
	std::set<Unit> front_units_;
	double front_supply_;
	double front_air_to_ground_supply_;
	double front_air_to_air_supply_;
	double front_ground_supply_;
	double front_ranged_ground_supply_;
	bool front_stim_allowed_;
	Position enemy_vanguard_ = Positions::None;
	bool push_through_ = false;
	
	bool is_nearly_engaged() const;
	bool expect_win(double enemy_supply) const;
	bool expect_win(double enemy_supply,double front_supply) const;
	void calculate_engagement_distances();
	void determine_friendly_units();
	void determine_front();
	void determine_terrain_influence();
	static double augment_supply(double supply,double factor,double fraction);
	void calculate_defense_supply();
	void determine_push_through();
	bool mobile_detection_in_front();
	
	friend class TacticsManager;
};

class TacticsManager : public Singleton<TacticsManager>
{
public:
	static constexpr int kClusterDistance = 512;
	
	void update();
	void draw();
	
	void set_is_ffa(bool is_ffa) { is_ffa_ = is_ffa; }
	
	int worker_supply() const { return worker_supply_; }
	int army_supply() const { return army_supply_; }
	int defense_supply() const { return defense_supply_; }
	int anti_air_supply() const { return anti_air_supply_; }
	int enemy_worker_supply() const { return enemy_worker_supply_; }
	int enemy_army_supply() const { return enemy_army_supply_; }
	int max_enemy_army_supply() const { return max_enemy_army_supply_; }
	int enemy_defense_supply() const { return enemy_defense_supply_; }
	int enemy_air_supply() const { return enemy_air_supply_; }
	int enemy_offense_supply() const { return enemy_offense_supply_; }
	int enemy_offense_air_supply() const { return enemy_offense_air_supply_; }
	const std::vector<EnemyCluster>& clusters() const { return clusters_; }
	const EnemyCluster* cluster_for_unit(Unit unit) const { return get_or_default(cluster_map_, unit); }
	EnemyCluster* cluster_at_position(Position position);
	
	static int defense_supply_equivalent(Unit unit);
	static int defense_supply_equivalent(const InformationUnit& information_unit,bool runby_possible=false);
	static int defense_supply_equivalent(UnitType type,bool runby_possible=false);
	static int carrier_supply_equivalent(Unit unit);
	
	const std::set<Unit>& included_static_defense() const { return included_static_defense_; }
	const BWEM::Base* enemy_start_base() const { return enemy_start_base_; }
	int enemy_start_base_found_at_frame() const { return enemy_start_base_found_at_frame_; }
	const BWEM::Base* enemy_natural_base() const { return enemy_natural_base_; }
	
	Position enemy_start_position() const;
	Position enemy_start_drop_position() const;
	Position enemy_base_attack_position(bool guess_start_location=true) const;
	std::vector<const BWEM::Base*> possible_enemy_start_bases() const;
	
	bool mobile_detection_exists();
	int air_to_air_supply();
	bool is_opponent_army_too_large();
	bool is_opponent_army_too_large(double build_supply_per_minute);
private:
	bool is_ffa_ = false;
	
	int worker_supply_ = 0;
	int army_supply_ = 0;
	int defense_supply_ = 0;
	int anti_air_supply_ = 0;
	int enemy_worker_supply_ = 0;
	int enemy_army_supply_ = 0;
	int max_enemy_army_supply_ = 0;
	int enemy_defense_supply_ = 0;
	int enemy_air_supply_ = 0;
	int enemy_offense_supply_ = 0;
	int enemy_offense_air_supply_ = 0;
	std::vector<EnemyCluster> clusters_;
	std::map<Unit,EnemyCluster*> cluster_map_;
	std::set<Unit> included_static_defense_;
	const BWEM::Base* enemy_start_base_ = nullptr;
	int enemy_start_base_found_at_frame_ = -1;
	const BWEM::Base* enemy_natural_base_ = nullptr;
	
	void update_enemy_base();
	void update_supply();
	void update_enemy_supply();
	void update_included_static_defense();
	void update_clusters();
	Position deduce_enemy_start_position() const;
	const BWEM::Base* deduce_enemy_start_base() const;
};

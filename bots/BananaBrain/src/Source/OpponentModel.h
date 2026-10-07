#pragma once

enum class EnemyOpening
{
	Unknown,
	
	// Zerg
	Z_4_5Pool,
	Z_9Pool,
	Z_9PoolSpeed,
	Z_OverPool,
	Z_12Pool,
	Z_10Hatch,
	Z_12Hatch,
	Z_12HatchMain,
	Z_WorkerRush,
	
	// Terran
	T_BBS,
	T_2Rax,
	T_ProxyRax,
	T_1Fact,
	T_2Fact,
	T_FastExpand,
	T_1RaxFE,
	T_1FactFE,
	T_WallIn,
	T_WorkerRush,
	
	// Protoss
	P_1GateCore,
	P_4GateGoon,
	P_2Gate,
	P_2GateFast,
	P_ProxyGate,
	P_FastExpand,
	P_ForgeFastExpand,
	P_CannonRush,
	P_CannonTurtle,
	P_WorkerRush
};

class OpponentModel : public Singleton<OpponentModel>
{
public:
	OpponentModel() : marine_collector_(UnitTypes::Terran_Barracks), zealot_collector_(UnitTypes::Protoss_Gateway), dragoon_collector_(UnitTypes::Protoss_Dragoon) {}
	
	void init();
	void update();
	EnemyOpening enemy_opening() const { return enemy_opening_; }
	std::string enemy_opening_info() const;
	std::string enemy_opening_info(EnemyOpening enemy_opening) const;
	bool emp_seen() const { return emp_seen_; }
	Race initial_enemy_race() const { return initial_enemy_race_; }
	bool enemy_race_known() const { return race_known(enemy_race_); }
	Race enemy_race() const { return enemy_race_; }
	int enemy_earliest_expansion_frame();
	int enemy_latest_expansion_frame();
	bool cloaked_present() const { return cloaked_present_; }
	bool cloaked_or_mine_present() const;
	int dark_templar_frame() const { return dark_templar_frame_; }
	FastPosition dark_templar_position() const { return dark_templar_position_; }
	int mutalisk_frame() const { return mutalisk_frame_; }
	FastPosition mutalisk_position() const { return mutalisk_position_; }
	int lurker_frame() const { return lurker_frame_; }
	bool blocked_expansion_seen() { return blocked_expansion_seen_; }
	bool non_basic_combat_unit_seen() const { return non_basic_combat_unit_seen_; }
	
	bool enemy_base_sufficiently_scouted() const { return enemy_base_sufficiently_scouted_; }
	bool enemy_natural_sufficiently_scouted() const { return enemy_natural_sufficiently_scouted_; }
	
private:
	static int constexpr k5PoolSpawningPoolFrame = 1071;
	static int constexpr k9PoolSpawningPoolFrame = 1619;
	static int constexpr kOverPoolSpawningPoolFrame = 1809;
	static int constexpr k12PoolSpawningPoolFrame = 2184;
	static int constexpr k10HatchSpawningPoolFrame = 2500;
	static int constexpr k12HatchSpawningPoolFrame = 2826;
	
	static int constexpr k10HatchHatcheryFrame = 2143;
	static int constexpr k12HatchHatcheryFrame = 2380;
	static int constexpr kOverPoolHatcheryFrame = 2976;
	static int constexpr k9PoolHatcheryFrame = 3333;
	
	static int constexpr k5PoolZerglingFrame = 2682;
	static int constexpr k9PoolZerglingFrame = 3167;
	static int constexpr kOverPoolZerglingFrame = 3453;
	static int constexpr k12PoolZerglingFrame = 4007;
	
	static int constexpr k9PoolSpeedExtractorFrame = 1905;
	static int constexpr k9PoolSpeedMetabolicBoostFrame = 3333;
	
	static int constexpr k1RaxFirstMarineFrame = 2976;
	static int constexpr k1RaxSecondMarineFrame = 3333;
	static int constexpr kBBSFirstBarracksFrame = 1905;
	static int constexpr kBBSFirstMarineFrame = 3333;
	static int constexpr kBBSSecondMarineFrame = 3405;
	static int constexpr kBBSThirdMarineFrame = 3714;
	static int constexpr kBBSFourthMarineFrame = 3786;
	static int constexpr kBBSFifthMarineFrame = 4048;
	static int constexpr kStandardFirstBarracksFrame = 2143;
	static int constexpr k2RaxSecondBarracksFrame = 2619;
	static int constexpr k2RaxThirdMarineFrame = 4143;
	static int constexpr k2RaxFourthMarineFrame = 4333;
	static int constexpr k2RaxFifthMarineFrame = 4500;
	static int constexpr k2FactSecondFactoryFrame = 4881;
	static int constexpr k14CCCommandCenterFrame = 3048;
	static int constexpr k1RaxFECommandCenterFrame = 3667;
	static int constexpr k1FactFECommandCenterFrame = 4524;
	static int constexpr k1FactFEBunkerFrame = 4476;
	static int constexpr k1RaxFEBunkerFrame = 5381;
	static int constexpr kTerranUnitSightingCutoffFrame = 10000;
	
	static int constexpr kCannonRushForgeFrame = 2024;
	static int constexpr kFFEForgeFrame = 2000;
	static int constexpr k99GateSecondGatewayFrame = 2262;
	static int constexpr k99GateFirstZealotFrame = 3404;
	static int constexpr k99GateSecondZealotFrame = 3786;
	static int constexpr k99GateThirdZealotFrame = 3976;
	static int constexpr k1012GateFirstGatewayFrame = 1905;
	static int constexpr k1012GateSecondGatewayFrame = 2500;
	static int constexpr k1012GateThirdZealotFrame = 4333;
	static int constexpr k12NexusNexusFrame = 2619;
	static int constexpr kProtossUnitSightingCutoffFrame = 10000;
	
	static int constexpr k4GateGoonBuildingsFrame = 8571;
	static int constexpr k4GateGoon7Frame = 8571;
	static int constexpr k4GateGoon11Frame = 9286;
	static int constexpr k4GateGoon15Frame = 10357;
	static int constexpr k4GateGoonUnitSightingCutoffFrame = 12857;
	
	static int constexpr kTerranWallInDetectUntilFrame = 5714;
	
	static int constexpr kWorkerRushDetectUntilFrame = 3571;
	
	class UnitSightingCollector
	{
	public:
		UnitSightingCollector(UnitType building_type) : building_type_(building_type) {}
		
		void add_if_needed(Unit unit);
		size_t size() const { return unit_sightings_.size(); }
		std::vector<int> calculate_and_sort_creation_frames() const;
		
	private:
		struct UnitSighting
		{
			FastPosition position;
			int frame = -1;
			UnitType type;
		};
		
		UnitType building_type_;
		std::map<Unit,UnitSighting> unit_sightings_;
	};
	
	Race initial_enemy_race_ = Races::Unknown;
	Race enemy_race_ = Races::Unknown;
	bool cloaked_present_ = false;
	int dark_templar_frame_ = -1;
	FastPosition dark_templar_position_;
	int mutalisk_frame_ = -1;
	FastPosition mutalisk_position_;
	int lurker_frame_ = -1;
	bool blocked_expansion_seen_ = false;
	bool natural_pylon_seen_ = false;
	EnemyOpening enemy_opening_ = EnemyOpening::Unknown;
	bool emp_seen_ = false;
	bool enemy_base_sufficiently_scouted_ = false;
	bool enemy_natural_sufficiently_scouted_ = false;
	int enemy_natural_sufficiently_scouted_start_frame_ = -1;
	bool non_basic_combat_unit_seen_ = false;
	UnitSightingCollector marine_collector_;
	UnitSightingCollector zealot_collector_;
	UnitSightingCollector dragoon_collector_;
	bool proxyrax_base_check_ = false;
	
	bool position_not_in_main(Position enemy_position);
	bool position_in_enemy_main(Position enemy_position);
	bool position_in_main_near_choke(Position enemy_position,int max_distance=128);
	bool position_in_or_near_potential_natural(Position enemy_position,int max_distance=128);
	bool is_non_start_hatchery(Unit unit);
	void update_state_for_unit(Unit unit);
	bool is_cloaked(Unit unit);
	void update_state_for_spawning_pool(Unit unit);
	void update_state_for_hatchery(Unit unit);
	void update_state_for_zergling(Unit unit);
	bool is_cannon_rush(Unit unit);
	bool is_proxy_gate(Unit unit);
	bool is_proxy_rax(Unit unit);
	void update_state_for_unit(const InformationUnit& unit);
	void update_state_for_resource_depot(const InformationUnit& unit);
	void update_state_for_bunker(const InformationUnit& unit);
	bool is_cannon_turtle(const InformationUnit& unit);
	bool is_forge_fast_expand(const InformationUnit& unit);
	void update_emp_seen();
	void update_blocked_expansion_seen();
	void update_state_for_unit_sightings();
	void update_enemy_base_sufficiently_scouted();
	
	static bool race_known(Race race) { return race == Races::Zerg || race == Races::Terran || race == Races::Protoss; }
};

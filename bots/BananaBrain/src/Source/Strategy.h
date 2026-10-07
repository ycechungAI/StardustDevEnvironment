#pragma once

class Strategy
{
public:
	virtual void pick_strategy(bool is_1v1) = 0;
	virtual std::string mode() const = 0;
	virtual std::string opening() const = 0;
	virtual std::string late_game_strategy() const = 0;
	virtual void frame_inner() = 0;
	
	void apply_result(bool win);
	void frame();
	
protected:
	enum class StageType
	{
		Minerals,
		BlockChokePoint,
		BlockChokePointDarkTemplar,
		Proxy,
		Wall
	};
	
	int opening_supply_count();
	int opening_worker_count();
	bool opening_lost_too_many_workers();
	bool is_defending_rush();
	bool is_contained();
	bool is_enemy_offense_larger_than_defense();
	bool dark_templars_without_mobile_detection();
	bool expect_lurkers();
	bool expect_dark_templars();
	bool is_gas_stolen();
	bool is_scouting_worker_in_base();
	void scout_for_cannon_rush_if_needed();
	void update_maxed_out();
	void attack_check_condition();
	const BWEM::Base* stage_base() const;
	void update_stage_chokepoint();
	void update_stage_position_and_type();
	FastPosition determine_terran_wall_stage_position(FastPosition choke_center_position);
	static StageType determine_defense_decision(const std::set<const BWEM::Area*>& component_areas, const BWEM::ChokePoint* chokepoint);
	static bool is_near_ffe_building_or_gap(const InformationUnit* enemy_unit);
	static Position calculate_mineral_center_base_stage_point(const BWEM::Base* base);
	void apply_stage_positions();
	void apply_stage_block_chokepoint();
	bool apply_stage_block_ffe();
	bool ground_enemy_near_stage(const Gap& gap);
	void attack_enemy();
	bool desperation_attack_condition();
	
	const BWEM::ChokePoint* stage_chokepoint_ = nullptr;
	std::set<const BWEM::Area*> stage_component_areas_;
	StageType stage_type_ = StageType::Minerals;
	Position stage_position_ = Positions::None;
	std::map<UnitType,Position> type_specific_stage_;
	int attack_minimum_ = 2 * 8;
	int attack_frame_ = 0;
	bool maxed_out_ = false;
	bool attacking_ = false;
	bool initial_dark_templar_attack_ = false;
	bool attack_with_vultures_ = false;
	bool attack_near_target_only_ = false;
	int scouting_worker_in_base_frame_ = -1;
};

class ProtossStrategy : public Strategy
{
public:
	// PvZ
	static constexpr char* kPvZ_SairDt = "PvZ_sairdt";
	static constexpr char* kPvZ_1BaseSpeedZeal = "PvZ_1basespeedzeal";
	static constexpr char* kPvZ_2BaseSpeedZeal = "PvZ_2basespeedzeal";
	static constexpr char* kPvZ_Bisu = "PvZ_bisu";
	static constexpr char* kPvZ_NeoBisu = "PvZ_neobisu";
	static constexpr char* kPvZ_4Gate2Archon = "PvZ_4gate2archon";
	static constexpr char* kPvZ_5GateGoon = "PvZ_5gategoon";
	static constexpr char* kPvZ_SairGoon = "PvZ_sairgoon";
	static constexpr char* kPvZ_SairReaver = "PvZ_sairreaver";
	static constexpr char* kPvZ_4GateGoon = "PvZ_4gategoon";
	static constexpr char* kPvZ_99Gate = "PvZ_9/9gate";
	static constexpr char* kPvZ_99ProxyGate = "PvZ_9/9proxygate";
	static constexpr char* kPvZ_1012Gate = "PvZ_10/12gate";
	
	// PvT
	static constexpr char* kPvT_2GateDt = "PvT_2gatedt";
	static constexpr char* kPvT_1GateDtExpo = "PvT_1gatedtexpo";
	static constexpr char* kPvT_2GateRngExpo = "PvT_2gaterngexpo";
	static constexpr char* kPvT_1GateReaver = "PvT_1gatereaver";
	static constexpr char* kPvT_1015Gate = "PvT_10/15gate";
	static constexpr char* kPvT_Bulldog = "PvT_bulldog";
	static constexpr char* kPvT_12Nexus = "PvT_12nexus";
	static constexpr char* kPvT_28Nexus = "PvT_28nexus";
	static constexpr char* kPvT_32Nexus = "PvT_32nexus";
	static constexpr char* kPvT_DtDrop = "PvT_dtdrop";
	static constexpr char* kPvT_4GateGoon = "PvT_4gategoon";
	static constexpr char* kPvT_99Gate = "PvT_9/9gate";
	static constexpr char* kPvT_99ProxyGate = "PvT_9/9proxygate";
	static constexpr char* kPvT_1012Gate = "PvT_10/12gate";
	
	// PvP
	static constexpr char* kPvP_NZCore = "PvP_nzcore";
	static constexpr char* kPvP_ZCore = "PvP_zcore";
	static constexpr char* kPvP_ZZCore = "PvP_zzcore";
	static constexpr char* kPvP_ZCoreZ = "PvP_zcorez";
	static constexpr char* kPvP_1012GateDt = "PvP_10/12gatedt";
	static constexpr char* kPvP_2GateDtExpo = "PvP_2gatedtexpo";
	static constexpr char* kPvP_2GateReaver = "PvP_2gatereaver";
	static constexpr char* kPvP_3GateRobo = "PvP_3gaterobo";
	static constexpr char* kPvP_3GateSpeedZeal = "PvP_3gatespeedzeal";
	static constexpr char* kPvP_12Nexus = "PvP_12nexus";
	static constexpr char* kPvP_4GateGoon = "PvP_4gategoon";
	static constexpr char* kPvP_99Gate = "PvP_9/9gate";
	static constexpr char* kPvP_99ProxyGate = "PvP_9/9proxygate";
	static constexpr char* kPvP_1012Gate = "PvP_10/12gate";
	
	// PvU
	static constexpr char* kPvU_FFE = "PvU_ffe";
	static constexpr char* kPvU_4GateGoon = "PvU_4gategoon";
	static constexpr char* kPvU_99Gate = "PvU_9/9gate";
	static constexpr char* kPvU_99ProxyGate = "PvU_9/9proxygate";
	static constexpr char* kPvU_1012Gate = "PvU_10/12gate";
	
	static constexpr int kLateGameCarrierMaxAltitude = 384;
	static constexpr int kLateGameArbiterMinAltitude = 512;
	
	enum class Mode
	{
		Opening,
		DefendFastPool,
		DefendFastPoolFFE,
		DefendProxyGate,
		DefendCannonRush,
		DefendFourGateGoon,
		DefendThreeHatchLingFFE,
		ReactiveFastExpand,
		Main
	};
	
	enum class LateGameStrategy
	{
		None,
		Arbiters,
		Carriers
	};
	
	void pick_strategy(bool is_1v1) override;
	std::string mode() const override;
	std::string opening() const override { return opening_; }
	std::string late_game_strategy() const override;
	void frame_inner() override;
	
private:
	Mode mode_ = Mode::Opening;
	std::string opening_;
	LateGameStrategy late_game_strategy_ = LateGameStrategy::None;
	bool opening_attack_started_ = false;
	bool opening_initial_zealots_done_ = false;
	bool opening_zealot_started_ = false;
	bool opening_scout_started_ = false;
	bool opening_blocking_probe_ = false;
	bool opening_setup_plasma_carriers_done_ = false;
	bool opening_setup_plasma_proxy_gate_done_ = false;
	TilePosition opening_proxy_location_ = TilePositions::Unknown;
	FastWalkPosition opening_stage_location_ = WalkPositions::Unknown;
	bool opening_gateway_near_choke_placed_ = false;
	bool opening_two_gateways_near_choke_placed_ = false;
	bool shield_battery_placed_ = false;
	bool opening_forge_fast_expand_completed_ = false;
	bool opening_forge_fast_expand_positioned_ = false;
	bool opening_forge_fast_expand_positioned_successfully_ = false;
	int opening_forge_fast_expand_cannons_ = 0;
	bool opening_forge_fast_expand_hydra_bust_ = false;
	bool handle_cloaked_attack_with_observers_ = false;
	bool fast_pool_handled_ = false;
	bool proxy_gate_handled_ = false;
	bool cannon_rush_handled_ = false;
	bool four_gate_goon_handled_ = false;
	bool three_hatch_ling_handled_ = false;
	bool reactive_fast_expand_handled_ = false;
	bool zerg_dragoon_strategy_ = false;
	std::optional<int> opponent_dark_templar_frame_;
	std::optional<int> opponent_lurker_frame_;
	
	LateGameStrategy determine_late_game_strategy();
	void opening_PvZ_SairDt();
	void opening_PvZ_1BaseSpeedZeal();
	void opening_PvZ_2BaseSpeedZeal();
	void opening_PvZ_Bisu();
	void opening_PvZ_NeoBisu();
	void opening_PvZ_4Gate2Archon();
	void opening_PvZ_5GateGoon();
	void opening_PvZ_SairGoon();
	void opening_PvZ_SairReaver();
	void opening_PvT_2GateDt();
	void opening_PvT_1GateDtExpo();
	void opening_PvT_2GateRngExpo();
	void opening_PvT_1GateReaver();
	void opening_PvT_1015Gate();
	void opening_PvT_Bulldog();
	void opening_PvT_12Nexus();
	void opening_PvT_28Nexus();
	void opening_PvT_32Nexus();
	void opening_PvT_DtDrop();
	void opening_PvP_NZCore();
	void opening_PvP_ZCore();
	void opening_PvP_ZZCore();
	void opening_PvP_ZCoreZ();
	void opening_PvP_1012GateDt();
	void opening_PvP_2GateDtExpo();
	void opening_PvP_2GateReaver();
	void opening_PvP_3GateRobo();
	void opening_PvP_3GateSpeedZeal();
	void opening_PvP_12Nexus();
	void opening_PvU_FFE();
	void opening_PvX_4GateGoon();
	void opening_PvX_99Gate();
	void opening_PvX_99ProxyGate();
	void opening_PvX_1012Gate();
	void opening_build_units(UnitType unit_type,int count,bool stop_probe_production=true);
	void opening_build_units_once(UnitType unit_type,int count,bool stop_probe_production=true);
	void opening_forge_fast_expand();
	void opening_forge_fast_expand_12pool();
	void opening_forge_fast_expand_12hatch();
	void opening_forge_fast_expand_overpool();
	void opening_forge_fast_expand_9pool();
	void opening_forge_fast_expand_build_cannons(int cannon_count);
	FastWalkPosition determine_start_base_hidden_corner();
	int templar_archives_remaining_time();
	
	void mode_opening();
	void mode_defend_fast_pool();
	void mode_defend_fast_pool_ffe();
	void mode_defend_proxy_gate();
	void mode_defend_cannon_rush();
	void mode_defend_four_gate_goon();
	void mode_defend_three_hatch_ling_ffe();
	void mode_reactive_fast_expand();
	void mode_main();
	
	void base_defense_cannons(bool cannons_in_all_bases);
	void base_defense_cannons_for_zerg_flyers();
	void tech_tree();
	void observer_tree(bool important=false);
	bool observer_tree_done();
	void robotics_facility_tree();
	bool robotics_facility_tree_done();
	void templar_tree();
	bool templar_tree_done();
	void forge_updates();
	void arbiter_tree();
	bool arbiter_tree_done();
	void carrier_tree();
	bool carrier_tree_done();
	
	void update_train_distributions();
	void update_stargate_train_distribution();
	void update_robotics_facility_train_distribution();
	void update_gateway_train_distribution();
	void update_gateway_train_distribution(bool dragoon,bool high_templar,bool dark_templar);
	void reserve_resources_for_emergency_cannons();
	bool want_reavers();
	bool want_high_templars();
	bool dragoons_verus_zerg();
	bool need_more_anti_air();
	bool hydralisks_too_fast();
	int zerg_flyer_count();
	bool expect_zerg_flyers();
	bool expect_cloaked_attack();
	bool need_counter_carriers();
	void counter_carriers();
	bool counter_carriers_done();
	int counter_carriers_requested_dark_archon_count();
	int potential_dark_archon_count();
	void additional_gateways();
};

class TerranStrategy : public Strategy
{
public:
	// TvZ
	static constexpr char* kTvZ_Fantasy = "TvZ_fantasy";
	static constexpr char* kTvZ_Sparks = "TvZ_sparks";
	static constexpr char* kTvZ_Ayumi = "TvZ_ayumi";
	static constexpr char* kTvZ_1RaxFE = "TvZ_1raxfe";
	static constexpr char* kTvZ_2Rax = "TvZ_2rax";
	static constexpr char* kTvZ_14CC = "TvZ_14cc";
	static constexpr char* kTvZ_5FactGoliath = "TvZ_5factgoliath";
	static constexpr char* kTvZ_2PortWraith = "TvZ_2portwraith";
	static constexpr char* kTvZ_8RaxMech = "TvZ_8raxmech";
	static constexpr char* kTvZ_BBS = "TvZ_bbs";
	static constexpr char* kTvZ_ProxyBBS = "TvZ_proxybbs";
	
	// TvT
	static constexpr char* kTvT_2FactVults = "TvT_2factvults";
	static constexpr char* kTvT_3FactVults = "TvT_3factvults";
	static constexpr char* kTvT_1FactFE = "TvT_1factfe";
	static constexpr char* kTvT_1FactFEBioMech = "TvT_1factfebiomech";
	static constexpr char* kTvT_1RaxFE = "TvT_1raxfe";
	static constexpr char* kTvT_14CC = "TvT_14cc";
	static constexpr char* kTvT_1PortWraith = "TvT_1portwraith";
	static constexpr char* kTvT_2PortWraith = "TvT_2portwraith";
	static constexpr char* kTvT_Proxy5Rax = "TvT_proxy5rax";
	static constexpr char* kTvT_8RaxMech = "TvT_8raxmech";
	static constexpr char* kTvT_BBS = "TvT_bbs";
	static constexpr char* kTvT_ProxyBBS = "TvT_proxybbs";
	
	// TvP
	static constexpr char* kTvP_2FactVults = "TvP_2factvults";
	static constexpr char* kTvP_GundamRush = "TvP_gundamrush";
	static constexpr char* kTvP_JoyORush = "TvP_joyorush";
	static constexpr char* kTvP_ShallowTwo = "TvP_shallowtwo";
	static constexpr char* kTvP_DeepSix = "TvP_deepsix";
	static constexpr char* kTvP_SiegeExpand = "TvP_siegeexpand";
	static constexpr char* kTvP_1FactFE = "TvP_1factfe";
	static constexpr char* kTvP_1RaxFE = "TvP_1raxfe";
	static constexpr char* KTvP_14CC = "TvP_14cc";
	static constexpr char* kTvP_StrongFD = "TvP_strongfd";
	static constexpr char* kTvP_101010FD = "TvP_101010fd";
	static constexpr char* kTvP_BBS = "TvP_bbs";
	static constexpr char* kTvP_ProxyBBS = "TvP_proxybbs";
	
	// TvU
	static constexpr char* kTvU_1Fact = "TvU_1fact";
	static constexpr char* kTvU_2Rax = "TvU_2rax";
	static constexpr char* kTvU_BBS = "TvU_bbs";
	static constexpr char* kTvU_ProxyBBS = "TvU_proxybbs";
	
	enum class Mode
	{
		Opening,
		MainMech,
		MainBio,
		MainBioMech,
		DefendFastPool,
		DefendCannonRush
	};
	
	void pick_strategy(bool is_1v1) override;
	std::string mode() const override;
	std::string opening() const override { return opening_; }
	std::string late_game_strategy() const override { return "none"; }
	void frame_inner() override;
	
private:
	Mode mode_ = Mode::Opening;
	std::string opening_;
	bool opening_attack_started_ = false;
	TilePosition opening_proxy_location_ = TilePositions::Unknown;
	TilePosition opening_second_proxy_location_ = TilePositions::Unknown;
	TilePosition opening_bunker_location_ = TilePositions::Unknown;
	TilePosition opening_barracks_location_ = TilePositions::Unknown;
	TilePosition opening_command_center_location_ = TilePositions::Unknown;
	bool wraith_strategy_ = false;
	bool opening_wall_positioned_ = false;
	bool opening_wall_positioned_successfully_ = false;
	bool opening_attack_with_workers_ = false;
	bool fast_pool_handled_ = false;
	bool gas_steal_handled_ = false;
	bool cannon_rush_handled_ = false;
	bool cannons_or_pylons_in_base_seen_ = false;
	
	void opening_TvZ_fantasy();
	void opening_TvZ_sparks();
	void opening_TvZ_ayumi();
	void opening_TvZ_1raxfe();
	void opening_TvZ_2rax();
	void opening_TvZ_14cc();
	void opening_TvZ_5factgoliath();
	void opening_TvT_3factvults();
	void opening_TvT_1factfe(bool biomech);
	void opening_TvT_1raxfe();
	void opening_TvT_14cc();
	void opening_TvT_1portwraith();
	void opening_TvT_proxy5rax();
	void opening_TvP_gundamrush();
	void opening_TvP_joyorush();
	void opening_TvP_shallowtwo();
	void opening_TvP_deepsix();
	void opening_TvP_siegeexpand();
	void opening_TvP_siegeexpand_start(bool biomech);
	void opening_TvP_1factfe();
	void opening_TvP_1raxfe();
	void opening_TvP_14cc();
	void opening_TvP_strongfd();
	void opening_TvP_101010fd();
	void opening_TvU_1fact();
	void opening_TvU_2rax();
	void opening_TvX_2factvults();
	void opening_TvX_2portwraith();
	void opening_TvX_8raxmech();
	void opening_TvX_bbs();
	void opening_TvX_proxybbs();
	
	void mode_opening();
	void mode_main_mech();
	void mode_main_bio(bool biomech);
	void mode_defend_fast_pool();
	void mode_defend_cannon_rush();
	
	void main_boilerplate();
	bool need_emergency_detection();
	void build_emergency_detection();
	void update_bio_train_distribution(bool biomech);
	void update_barracks_train_distribution(bool firebat,bool ghost,bool medic);
	void update_mech_train_distributions();
	void update_factory_train_distribution(bool siege_tank,bool goliath);
	void additional_barracks();
	void additional_factories();
	int tank_count();
	int tank_count_built_or_in_progress();
	bool need_counter_lurkers_with_tanks();
	bool need_additional_tanks_to_counter_lurkers();
	int anti_air_supply();
	int enemy_air_supply();
	int wraiths_needed_for_cloak();
	bool enemy_has_flying_building();
	bool need_more_anti_air();
	bool want_irradiate();
	int count_irradiate_targets();
	bool want_emp();
	int count_emp_targets();
	int count_lockdown_targets();
	bool expect_zerg_air_attack();
	bool allow_open_wall();
};

class ZergStrategy : public Strategy
{
public:
	// ZvZ
	static constexpr char* kZvZ_4Pool = "ZvZ_4pool";
	static constexpr char* kZvZ_5Pool = "ZvZ_5pool";
	static constexpr char* kZvZ_2HatchLing = "ZvZ_2hatchling";
	static constexpr char* kZvZ_3HatchLing = "ZvZ_3hatchling";
	static constexpr char* kZvZ_9HatchLing = "ZvZ_9hatchling";
	static constexpr char* kZvZ_10HatchLing = "ZvZ_10hatchling";
	static constexpr char* kZvZ_9PoolSpire = "ZvZ_9poolspire";
	static constexpr char* kZvZ_9Gas9Pool = "ZvZ_9gas9pool";
	static constexpr char* kZvZ_9Gas10Pool = "ZvZ_9gas10pool";
	static constexpr char* kZvZ_11Gas10Pool = "ZvZ_11gas10pool";
	static constexpr char* kZvZ_OverPool9Gas = "ZvZ_overpool9gas";
	static constexpr char* kZvZ_12Pool = "ZvZ_12pool";
	static constexpr char* KZvZ_12PoolMain = "ZvZ_12poolmain";
	
	// ZvT
	static constexpr char* kZvT_4Pool = "ZvT_4pool";
	static constexpr char* kZvT_5Pool = "ZvT_5pool";
	static constexpr char* kZvT_7Pool = "ZvT_7pool";
	static constexpr char* kZvT_2HatchLing = "ZvT_2hatchling";
	static constexpr char* kZvT_3HatchLing = "ZvT_3hatchling";
	static constexpr char* kZvT_9HatchLing = "ZvT_9hatchling";
	static constexpr char* kZvT_2HatchMuta_12Hatch = "ZvT_2hatchmuta_12hatch";
	static constexpr char* kZvT_2HatchMuta_12Pool = "ZvT_2hatchmuta_12pool";
	static constexpr char* kZvT_2_5HatchMuta = "ZvT_2.5hatchmuta";
	static constexpr char* kZvT_3HatchMuta = "ZvT_3hatchmuta";
	static constexpr char* kZvT_13PoolMuta = "ZvT_13poolmuta";
	static constexpr char* kZvT_MutaHydra_12Hatch = "ZvT_mutahydra_12hatch";
	static constexpr char* kZvT_MutaHydra_9Pool = "ZvT_mutahydra_9pool";
	static constexpr char* kZvT_3HatchLurker = "ZvT_3hatchlurker";
	
	// ZvP
	static constexpr char* kZvP_5Pool = "ZvP_5pool";
	static constexpr char* kZvP_2HatchLing = "ZvP_2hatchling";
	static constexpr char* kZvP_3HatchLing = "ZvP_3hatchling";
	static constexpr char* kZvP_9HatchLing = "ZvP_9hatchling";
	static constexpr char* kZvP_10HatchLing = "ZvP_10hatchling";
	static constexpr char* kZvP_2HatchMuta = "ZvP_2hatchmuta";
	static constexpr char* kZvP_3HatchMuta = "ZvP_3hatchmuta";
	static constexpr char* kZvP_2HatchHydra = "ZvP_2hatchhydra";
	static constexpr char* kZvP_9734 = "ZvP_9734";
	static constexpr char* kZvP_3HatchLurker = "ZvP_3hatchlurker";
	static constexpr char* kZvP_NeoSauron = "ZvP_neosauron";
	static constexpr char* kZvP_4HatchBeforeGas = "ZvP_4hatchbeforegas";
	static constexpr char* kZvP_5HatchBeforeGas = "ZvP_5hatchbeforegas";
	static constexpr char* kZvP_6Hatch = "ZvP_6hatch";
	
	// ZvU
	static constexpr char* kZvU_4Pool = "ZvU_4pool";
	static constexpr char* kZvU_5Pool = "ZvU_5pool";
	static constexpr char* kZvU_2HatchLing = "ZvU_2hatchling";
	static constexpr char* kZvU_3HatchLing = "ZvU_3hatchling";
	static constexpr char* kZvU_9HatchLing = "ZvU_9hatchling";
	static constexpr char* kZvU_10HatchLing = "ZvU_10hatchling";
	static constexpr char* kZvU_9PoolSpeed = "ZvU_9poolspeed";
	static constexpr char* kZvU_11Pool = "ZvU_11pool";
	
	static constexpr double kAverageWorkersPerMineral = 2.0;
	static constexpr double kExpandWorkersPerMineral = 1.0;
	
	enum class Mode
	{
		Opening,
		DefendOneBaseProtoss,
		DefendProxyRax,
		DefendFastPool,
		Main_MutaHydraLurkerLing,
		Main_HydraLurkerLing,
		Main_UltraLing,
		Main_ZvZ,
		Main_ZvZLateGame
	};
	
	void pick_strategy(bool is_1v1) override;
	std::string mode() const override;
	std::string opening() const override { return opening_; }
	std::string late_game_strategy() const override { return "none"; }
	void frame_inner() override;
	
private:
	Mode mode_ = Mode::Opening;
	std::string opening_;
	bool one_base_protoss_handled_ = false;
	bool proxy_rax_handled_ = false;
	bool fast_pool_handled_ = false;
	bool opening_attack_started_ = false;
	bool opening_extractor_trick_ = false;
	int fast_pool_sunken_count_ = 0;
	int opening_sunkens_placed_ = -1;
	int opening_spores_placed_ = -1;
	bool opening_macro_hatch_placed = false;
	FastPosition opening_stage_location_ = Positions::Unknown;
	std::map<const BWEM::Base*,int> creep_colonies_placed_;
	
	void opening_ZvZ_9poolspire();
	void opening_ZvZ_9gas9pool();
	void opening_ZvZ_9gas10pool();
	void opening_ZvZ_11gas10pool();
	void opening_ZvZ_overpool9gas();
	void opening_ZvZ_12pool();
	void opening_ZvZ_12poolmain();
	void opening_ZvT_7pool();
	void opening_ZvT_2hatchmuta(bool pool_first);
	void opening_ZvT_2_5hatchmuta();
	void opening_ZvT_3hatchmuta();
	void opening_ZvT_13poolmuta();
	void opening_ZvT_mutahydra(bool pool_first);
	void opening_ZvT_3hatchlurker();
	void opening_ZvP_2hatchmuta();
	void opening_ZvP_3hatchmuta();
	void opening_ZvP_2hatchhydra();
	void opening_ZvP_9734();
	void opening_ZvP_3hatchlurker();
	void opening_ZvP_neosauron();
	void opening_ZvP_4hatchbeforegas();
	void opening_ZvP_5hatchbeforegas();
	void opening_ZvP_6hatch();
	void opening_ZvU_9poolspeed();
	void opening_ZvU_11pool();
	void opening_ZvX_4pool();
	void opening_ZvX_5pool();
	void opening_ZvX_2hatchling();
	void opening_ZvX_3hatchling();
	void opening_ZvX_9hatchling();
	void opening_ZvX_10hatchling();
	
	bool opening_is_more_zerglings_needed();
	void drone_scout_for_zergling_rush();
	bool morphing_building_hp_at_least(UnitType building_type,int hp);
	bool counter_air_harass_with_spore();
	bool counter_mutalisks_with_spore();
	bool build_spore_in_each_base();
	bool mutalisk_transition_during_zergling_rush_in_progress();
	void build_mutalisks_if_needed_for_zergling_rush();
	void mode_opening();
	void mode_defend_one_base_protoss();
	void mode_defend_proxy_rax();
	void mode_defend_fast_pool();
	void mode_main_muta_hydra_lurker_ling(bool mutalisk_harass);
	void mode_main_ultra_ling();
	void mode_main_ZvZ(bool lategame=false);
	bool main_boilerplate(UnitType type);
	void set_extractor_workers(bool hydralisks=false);
	void border_sunken_colonies(int count);
	int place_defense_creep_colonies(const BWEM::Base* base,int requested_count);
	bool can_place_defense_creep_colony(FastTilePosition creep_colony_tile_position);
	void hydra_upgrades(bool lurker_aspect=false);
	void attack_and_carapace_upgrades(std::initializer_list<UpgradeType> upgrade_types);
	int bases_needed_for_tech_to_hive();
	void tech_to_hive();
	bool tech_to_hive_done();
	double calculate_mineral_gas_ratio();
	int calculate_number_of_scourges_required();
	bool opponent_only_has_flying_buildings();
	bool is_terran_bio();
	FastPosition determine_offensive_stage_position();
	void set_train_distribution(UnitType type);
	void set_train_distribution(TrainDistribution& train_distribution,UnitType type);
};

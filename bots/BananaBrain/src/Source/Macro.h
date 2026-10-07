#pragma once

struct CostPerMinute
{
	CostPerMinute() : minerals(0.0), gas(0.0), supply(0.0) {}
	CostPerMinute(double minerals,double gas,double supply) : minerals(minerals), gas(gas), supply(supply) {}
	
	CostPerMinute operator+(const CostPerMinute& o) const {
		return CostPerMinute(minerals + o.minerals, gas + o.gas, supply + o.supply);
	}
	
	CostPerMinute& operator +=(const CostPerMinute& o) {
		minerals += o.minerals;
		gas += o.gas;
		supply += o.supply;
		return *this;
	}
	
	friend CostPerMinute operator*(double scale,const CostPerMinute& o) {
		return CostPerMinute(scale * o.minerals, scale * o.gas, scale * o.supply);
	}
	
	CostPerMinute operator*(double scale) {
		return CostPerMinute(minerals * scale, gas * scale, supply * scale);
	}
	
	CostPerMinute& operator*=(double scale) {
		minerals *= scale;
		gas *= gas;
		supply *= supply;
		return *this;
	}
	
	double minerals;
	double gas;
	double supply;
};

struct MineralGas
{
	MineralGas() : minerals(0), gas(0) {}
	MineralGas(int minerals,int gas) : minerals(minerals), gas(gas) {}
	MineralGas(Player player) : minerals(player->minerals()), gas(player->gas()) {}
	MineralGas(UnitType type) : minerals(type.mineralPrice()), gas(type.gasPrice()) {}
	MineralGas(UpgradeType type,int level=1) : minerals(type.mineralPrice(level)), gas(type.gasPrice(level)) {}
	MineralGas(TechType type) : minerals(type.mineralPrice()), gas(type.gasPrice()) {}
	MineralGas(CostPerMinute cost) : minerals(int(cost.minerals)), gas(int(cost.gas)) {}
	
	MineralGas operator+(const MineralGas& o) const {
		return MineralGas(minerals + o.minerals, gas + o.gas);
	}
	
	MineralGas& operator+=(const MineralGas& o) {
		minerals += o.minerals;
		gas += o.gas;
		return *this;
	}
	
	MineralGas operator-(const MineralGas& o) const {
		return MineralGas(minerals - o.minerals, gas - o.gas);
	}
	
	MineralGas& operator-=(const MineralGas& o) {
		minerals -= o.minerals;
		gas -= o.gas;
		return *this;
	}
	
	MineralGas operator*(double scale) {
		return MineralGas(int(minerals * scale + 0.5), int(gas * scale + 0.5));
	}
	
	bool can_pay(const MineralGas& o) const {
		return minerals >= o.minerals && (o.gas <= 0 || gas >= o.gas);
	}
	
	int can_pay_times(const MineralGas& o) const;
	
	bool is_negative() const {
		return minerals < 0 || gas < 0;
	}
	
	bool is_non_negative() const {
		return minerals >= 0 && gas >= 0;
	}
	
	int minerals;
	int gas;
};

class ResourceCounter
{
public:
	ResourceCounter(size_t size) : values_(size) {}
	void init(int value);
	void process_value(int value);
	double per_minute() const { return per_minute_; }
private:
	std::vector<int> values_;
	size_t index_ = 0;
	double per_minute_ = 0.0;
};

class TrainDistribution
{
public:
	static constexpr int kLarvaSpawnFrames = 342;
	
	TrainDistribution(UnitType builder_type) : builder_type_(builder_type) {}
	
	void clear() { return weights_.clear(); }
	double get(UnitType unit_type) const { return get_or_default(weights_, unit_type); }
	void set(UnitType unit_type,double value) { weights_[unit_type] = value; }
	bool is_empty() const { return !is_nonempty(); }
	bool is_nonempty() const;
	double sum() const;
	UnitType sample() const;
	MineralGas weighted_cost() const;
	CostPerMinute cost_per_minute(int producers=1) const;
	static CostPerMinute cost_per_minute(UnitType unit_type);
	int additional_producers() const;
	void apply_train_orders() const;
	
	UnitType builder_type() const { return builder_type_; }
	
private:
	UnitType builder_type_;
	std::map<UnitType,double> weights_;
};

struct BuildingCount
{
	int actual = 0;
	int planned = 0;
	int warping = 0;
	int additional = 0;
	int additional_important = 0;
	
	int requested(bool important=false)
	{
		return actual + planned + (important ? additional_important : additional);
	}
};

struct BaseDefense
{
	struct Single
	{
		bool exists = false;
		bool planned = false;
		bool add = false;
	};
	
	struct Multi
	{
		int actual = 0;
		int planned = 0;
		int additional = 0;
	};
	
	Single pylon;
	Multi cannons;
	Multi turrets;
	Single creep_colony;
	Single sunken_colony;
	Single spore_colony;
};

class BuildingManager : public Singleton<BuildingManager>
{
public:
	static constexpr int kBuildingPlacementEstimateWorkerTravelFramesCacheFrames = 120;	// When caching the number of frames a worker needs to get to the build location, the cache is valid for this number of frames
	static constexpr int kBuildingPlacementFailedRetryFrames = 240;	// When building placement fails, wait at least this amount of frames before retrying
	
	void update_supply_requests();
	void init_building_count_map();
	void init_base_defense_map();
	void init_upgrade_and_research();
	void update_requested_building_count_for_pre_upgrade();
	void apply_building_requests(bool important);
	void repair_damaged_buildings();
	void continue_unfinished_buildings_without_worker();
	void apply_upgrades(bool important);
	void apply_research(bool important);
	void cancel_building(Unit building);
	void cancel_buildings_of_type(UnitType building_type);
	void cancel_extra_buildings_of_type(UnitType building_type,int keep_including_warping,int keep_including_planned);
	void cancel_expansion_hatcheries();
	void cancel_doomed_buildings();
	
	bool building_exists(UnitType unit_type) { return building_count_[unit_type].actual > 0; }
	int building_count(UnitType unit_type) { return building_count_[unit_type].actual; }
	int building_count_including_planned(UnitType unit_type) { return building_count_[unit_type].actual + building_count_[unit_type].planned; }
	int building_count_including_warping(UnitType unit_type) { return building_count_[unit_type].actual + building_count_[unit_type].warping; }
	int building_count_including_requested(UnitType unit_type,bool important=false) { return building_count_[unit_type].requested(important); }
	void set_requested_building_count_at_least(UnitType unit_type,int count,bool important=false);
	bool automatic_supply() const { return automatic_supply_; }
	void set_automatic_supply(bool automatic_supply) { automatic_supply_ = automatic_supply; }
	void request_upgrade(UpgradeType upgrade_type,bool important=false) { upgrade_request_[upgrade_type] |= important; }
	void request_research(TechType tech_type,bool important=false) { research_request_[tech_type] |= important; }
	void request_base(const BWEM::Base* base,bool important=false);
	bool request_next_base(bool important=false);
	bool request_bases(int count);
	bool base_defense_pylon_exists(const BWEM::Base* base) { return base_defense_[base].pylon.exists; }
	int base_defense_cannons_including_planned(const BWEM::Base* base) { return base_defense_[base].cannons.actual + base_defense_[base].cannons.planned; }
	bool base_defense_creep_colony_planned_or_exists(const BWEM::Base* base) { return base_defense_[base].creep_colony.planned || base_defense_[base].creep_colony.exists; }
	bool base_defense_creep_colony_exists(const BWEM::Base* base) { return base_defense_[base].creep_colony.exists; }
	bool base_defense_sunken_colony_planned_or_exists(const BWEM::Base* base) { return base_defense_[base].sunken_colony.planned || base_defense_[base].sunken_colony.exists; }
	bool base_defense_sunken_colony_exists(const BWEM::Base* base) { return base_defense_[base].sunken_colony.exists; }
	bool base_defense_spore_colony_planned_or_exists(const BWEM::Base* base) { return base_defense_[base].spore_colony.planned || base_defense_[base].spore_colony.exists; }
	bool base_defense_spore_colony_exists(const BWEM::Base* base) { return base_defense_[base].spore_colony.exists; }
	void request_base_defense_pylon(const BWEM::Base* base);
	void set_requested_base_defense_cannon_count_at_least(const BWEM::Base* base,int count);
	void set_requested_base_defense_turret_count_at_least(const BWEM::Base* base,int count);
	void request_base_defense_creep_colony(const BWEM::Base* base);
	void request_base_defense_sunken_colony(const BWEM::Base* base);
	void request_base_defense_spore_colony(const BWEM::Base* base);
	int lost_building_count() const { return lost_building_count_; }
	bool can_not_place_refinery() const { return determine_available_geyser_tile_position().empty(); }
	
	bool required_buildings_exist_for_building(UnitType unit_type);
	
	bool pylon_placement_failed() const { return pylon_retry_frame_ > Broodwar->getFrameCount(); }
	bool non_pylon_building_placement_failed() const { return non_pylon_building_retry_frame_ > Broodwar->getFrameCount(); }
	bool building_placement_failed() const { return pylon_placement_failed() || non_pylon_building_placement_failed(); }
	void onUnitLost(Unit unit);
	
private:
	struct BaseRequest
	{
		UnitType resource_depot_type;
		bool important = false;
	};
	
	std::map<UnitType,BuildingCount> building_count_;
	std::map<const BWEM::Base*,BaseDefense> base_defense_;
	std::map<UpgradeType,bool> upgrade_request_;
	std::map<TechType,bool> research_request_;
	std::map<const BWEM::Base*,BaseRequest> base_request_;
	bool automatic_supply_ = false;
	int pylon_retry_frame_ = 0;
	int non_pylon_building_retry_frame_ = 0;
	std::map<const BWEM::Base*,int> base_defense_retry_frame_;
	std::map<UnitType,std::pair<int,int>> estimated_travel_frame_cache_;
	int lost_building_count_ = 0;
	
	std::pair<int,int>& estimate_worker_travel_frames_for_place_building(UnitType unit_type);
	std::vector<TilePosition> determine_available_geyser_tile_position() const;
	static bool is_safe_to_place_refinery(FastTilePosition tile_position);
	static double hitpoint_fraction(Unit unit) { return (double)unit->getHitPoints() / (double)unit->getType().maxHitPoints(); }
};

class SpendingManager : public Singleton<SpendingManager>
{
public:
	static constexpr int kResourceCounterFrames = 480;
	
	SpendingManager() : mineral_counter_(kResourceCounterFrames), gas_counter_(kResourceCounterFrames) {}
	
	MineralGas income_per_minute() const;
	CostPerMinute training_cost_per_minute() const;
	CostPerMinute training_cost_per_minute(Race race) const;
	CostPerMinute worker_training_cost_per_minute(Race race=Races::None) const;
	CostPerMinute total_cost_per_minute() const;
	MineralGas remainder() const;
	
	void init_resource_counters();
	void init_spendable();
	bool try_spend(const MineralGas& mineral_gas);
	bool try_spend(const MineralGas& mineral_gas,int frames_ahead);
	void reserve_resources(const MineralGas& mineral_gas) { reserved_resources_ += mineral_gas; }
	
	const MineralGas& spendable() const { return spendable_; }
	bool spendable_out_of_minerals_first() const { return spendable_out_of_minerals_first_; }
	bool spendable_out_of_gas_first() const { return spendable_out_of_gas_first_; }
private:
	static CostPerMinute training_cost_per_minute(const TrainDistribution& train_queue);
	void spend(const MineralGas& mineral_gas);
	
	MineralGas spendable_;
	MineralGas reserved_resources_;
	bool spendable_out_of_minerals_first_ = false;
	bool spendable_out_of_gas_first_ = false;
	ResourceCounter mineral_counter_;
	ResourceCounter gas_counter_;
	
	friend class TrainDistribution;
};

struct UnitCount
{
	int count_completed = 0;
	int count = 0;
	int count_loaded = 0;
};

class TrainingManager : public Singleton<TrainingManager>
{
public:
	static constexpr int kSupplyCap = 400;
	
	TrainingManager() :
		gateway_train_distribution_(UnitTypes::Protoss_Gateway),
		robotics_facility_train_distribution_(UnitTypes::Protoss_Robotics_Facility),
		stargate_train_distribution_(UnitTypes::Protoss_Stargate),
		barracks_train_distribution_(UnitTypes::Terran_Barracks),
		factory_train_distribution_(UnitTypes::Terran_Factory),
		starport_train_distribution_(UnitTypes::Terran_Starport),
		larva_train_distribution_(UnitTypes::Zerg_Larva) {}
	
	int unit_count_completed(UnitType unit_type) { return unit_count_[unit_type].count_completed; }
	int unit_count(UnitType unit_type) { return unit_count_[unit_type].count; }
	int unit_count_loaded(UnitType unit_type) { return unit_count_[unit_type].count_loaded; }
	int lost_unit_count() const { return lost_unit_count_; }
	int lost_unit_supply() const { return lost_unit_supply_; }
	int unit_count_built(UnitType unit_type) { return get_or_default(units_built_, unit_type, 0); }
	int unit_count_built_or_in_progress(UnitType unit_type);
	
	TrainDistribution& gateway_train_distribution() { return gateway_train_distribution_; }
	TrainDistribution& robotics_facility_train_distribution() { return robotics_facility_train_distribution_; }
	TrainDistribution& stargate_train_distribution() { return stargate_train_distribution_; }
	TrainDistribution& barracks_train_distribution() { return barracks_train_distribution_; }
	TrainDistribution& factory_train_distribution() { return factory_train_distribution_; }
	TrainDistribution& starport_train_distribution() { return starport_train_distribution_; }
	TrainDistribution& larva_train_distribution() { return larva_train_distribution_; }
	
	bool prioritize_training() const { return prioritize_training_; }
	bool worker_production() const { return worker_production_; }
	bool worker_cut() const { return worker_cut_; }
	bool automatic_supply() const { return automatic_supply_; }
	int requested_lurker_count() const { return requested_lurker_count_; }
	void set_prioritize_training(bool prioritize_training) { prioritize_training_ = prioritize_training; }
	void set_worker_production(bool worker_production) { worker_production_ = worker_production; }
	void set_worker_cut(bool worker_cut) { worker_cut_ = worker_cut; }
	void set_automatic_supply(bool automatic_supply) { automatic_supply_ = automatic_supply; }
	void set_requested_lurker_count(int requested_lurker_count) { requested_lurker_count_ = requested_lurker_count; }
	
	void update_overlord_training();
	void apply_worker_train_orders();
	void apply_train_orders();
	int worker_count_including_unfinished();
	void init_unit_count_map();
	void onUnitMorph(Unit unit);
	void onUnitCreate(Unit unit);
	void onUnitComplete(Unit unit);
	void onUnitLost(Unit unit);
	
private:
	std::map<UnitType,UnitCount> unit_count_;
	std::map<UnitType,int> units_built_;
	int lost_unit_count_ = 0;
	int lost_unit_supply_ = 0;
	std::set<Unit> completed_tanks_;
	
	TrainDistribution gateway_train_distribution_;
	TrainDistribution robotics_facility_train_distribution_;
	TrainDistribution stargate_train_distribution_;
	TrainDistribution barracks_train_distribution_;
	TrainDistribution factory_train_distribution_;
	TrainDistribution starport_train_distribution_;
	TrainDistribution larva_train_distribution_;
	
	bool prioritize_training_ = false;
	bool worker_production_ = true;
	bool worker_cut_ = false;
	bool automatic_supply_ = false;
	int requested_lurker_count_ = 0;
	int second_zerglings_expected_ = 0;
	int second_scourges_expected_ = 0;
	
	void apply_interceptor_train_orders();
	void apply_scarab_train_orders();
	void apply_lurker_morph_orders();
	void apply_train_orders(TrainDistribution& train_queue,MineralGas& reserved);
	bool is_training_queue_empty(Unit unit);
	bool can_train(UnitType type);
};

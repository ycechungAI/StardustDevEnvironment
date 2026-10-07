#pragma once

struct InformationUnit
{
	Unit unit;
	Player player;
	int first_frame = -1;
	int start_frame = -1;
	int stasis_end_frame = -1;
	int lockdown_end_frame = -1;
	int maelstrom_end_frame = -1;
	int hitpoints;
	int shields;
	int frame;
	UnitType type = UnitTypes::None;
	Position position;
	bool flying;
	bool burrowed;
	int burrowed_and_should_be_detected_frame = INT_MAX;
	const BWEM::Area* area;
	int base_distance;	// Distance of this unit to a controlled or planned area, zero if inside
	bool destroy_neutral = false;
	
	bool is_current() const
	{
		return frame == Broodwar->getFrameCount();
	}
	
	int left() const
	{
		return position.x - type.dimensionLeft();
	}
	
	int top() const
	{
		return position.y - type.dimensionUp();
	}
	
	int right() const
	{
		return position.x + type.dimensionRight();
	}
	
	int bottom() const
	{
		return position.y + type.dimensionDown();
	}
	
	int complete_frame() const;
	bool is_completed() const;
	bool is_stasised() const;
	bool is_disabled() const;
	int expected_hitpoints() const;
	int expected_shields() const;
	TilePosition tile_position() const;
	int detection_range() const;
	
	void update(Unit a_unit);
	void clean_outdated_unit_position();
	
private:
	int determine_base_distance();
};

class InformationManager : public Singleton<InformationManager>
{
public:
	void update_units_and_buildings();
	void update_information();
	void draw();
	void onUnitDestroy(Unit unit);
	void onUnitDiscover(Unit unit);
	void onUnitEvade(Unit unit);
	
	const std::map<Unit,InformationUnit,CompareUnitByID>& all_units() const { return all_units_; }
	const std::vector<InformationUnit*>& my_units() const { return my_units_; }
	const std::vector<InformationUnit*>& neutral_units() const { return neutral_units_; }
	const std::vector<InformationUnit*>& enemy_units() const { return enemy_units_; }
	void mark_neutral_for_destruction(Unit unit,bool destroy=true);
	
	bool enemy_building_seen() const;
	int enemy_count(UnitType unit_type) const { return get_or_default(enemy_count_, unit_type); }
	bool enemy_seen(UnitType unit_type) const { return enemy_seen_.count(unit_type) > 0; }
	int enemy_seen_count(UnitType unit_type) const { return get_or_default(enemy_seen_count_, unit_type); }
	bool enemy_exists(UnitType unit_type) const { return get_or_default(enemy_count_, unit_type) > 0; }
	bool enemy_completed_exists(UnitType unit_type) const { return get_or_default(enemy_completed_count_, unit_type) > 0; }
	int upgrade_level(Player player,UpgradeType upgrade_type) const;
	bool enemy_has_upgrade(UpgradeType upgrade_type) const;
	bool no_enemy_has_upgrade(UpgradeType upgrade_type) const { return !enemy_has_upgrade(upgrade_type); };
	int bunker_marines_loaded(Unit bunker) const;
	
private:
	
	class MarineCountRingBuffer
	{
	public:
		MarineCountRingBuffer();
		void push(int value);
		int max() const;
		void decrement();
		void increment();
	private:
		std::array<int,24> values_;
		int counter_ = 0;
	};
	
	std::map<Unit,InformationUnit,CompareUnitByID> all_units_;
	std::vector<InformationUnit*> my_units_;
	std::vector<InformationUnit*> neutral_units_;
	std::vector<InformationUnit*> enemy_units_;
	std::map<UnitType,int> enemy_count_;
	std::map<UnitType,int> enemy_completed_count_;
	std::set<UnitType> enemy_seen_;
	std::set<std::pair<UnitType,Unit>> enemy_seen_by_type_;
	std::map<UnitType,int> enemy_seen_count_;
	std::map<Player,std::map<UpgradeType,int>> upgrades_;
	std::map<int,int> bullet_timestamps_;
	std::map<Unit,MarineCountRingBuffer> bunker_marines_loaded_;
	std::map<Unit,int> bunker_marines_delay_update_until_timestamp_;
	
	void clean_buildings_and_spells();
	void update_burrowed_and_should_be_detected_frame();
	void clean_outdated_unit_positions();
	void update_upgrades();
	void update_enemy_count();
	void update_bullet_timestamps();
	void update_bunker_marines_loaded();
	void update_bunker_marine_range();
};

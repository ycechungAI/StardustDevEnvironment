#pragma once

static constexpr double kBWCircle45DegreesFactor = 0.76316715428287563;

struct CompareUnitByID {
	bool operator()(const Unit& a,const Unit& b) const {
		return a->getID() < b->getID();
	}
};

struct DPF
{
	DPF() : shield(0.0), hp(0.0) {}
	DPF(double shield,double hp) : shield(shield), hp(hp) {}
	
	double shield;
	double hp;
	
	DPF operator+(const DPF& o) const {
		return DPF(shield + o.shield, hp + o.hp);
	}
	
	DPF& operator+=(const DPF& o) {
		shield += o.shield;
		hp += o.hp;
		return *this;
	}
	
	operator bool() const {
		return shield > 0.0 && hp > 0.0;
	}
};

struct InformationUnit;

std::string frame_to_string(int frame);
std::default_random_engine& random_generator();
void draw_diamond_map(Position position,int size,Color color);
void draw_filled_diamond_map(Position position,int size,Color color);
void draw_cross_map(Position position,int size,Color color);
bool building_visible(UnitType building_type,TilePosition tile_position);
bool building_explored(UnitType building_type,TilePosition tile_position);
const BWEM::Area* area_at(FastPosition position);
const BWEM::Area* area_at(FastWalkPosition walk_position);
bool has_area(FastWalkPosition walk_position);
bool resources_explored(const BWEM::Base* base);
bool walkable_line(FastWalkPosition a,FastWalkPosition b);
std::unordered_set<FastTilePosition> creep_spread(UnitType type,FastPosition position);
Position center_position_for(UnitType unit_type,TilePosition tile_position);
Position center_position(WalkPosition walk_position);
Position center_position(TilePosition tile_position);
int max_unit_dimension(UnitType type);
int euclidian_remainder(int a,int b);
Position lever(Position a,Position b,double t);
Position scale_line_segment(Position a,Position b,int requested_length);
int line_segment_length_within_circle(FastPosition a,FastPosition b,FastPosition center,int radius);
int line_segment_length_within_circle(FastPosition a,FastPosition b,int radius);
Position rotate_around(Position center,Position position,double angle);
int point_to_line_segment_distance(FastPosition a,FastPosition b,FastPosition x);
Position chokepoint_center(const BWEM::ChokePoint* cp);
std::pair<WalkPosition,WalkPosition> chokepoint_ends(const BWEM::ChokePoint* cp);
int chokepoint_width(const BWEM::ChokePoint* cp);
bool unit_has_pending_command(Unit unit,UnitCommandType command_type);
bool unit_has_target(Unit unit,Position target);
bool unit_has_target(Unit unit,Unit target);
bool not_incomplete(Unit unit);
bool not_cloaked(Unit unit);
bool is_disabled(Unit unit);
bool is_inside_bunker(Unit unit);
bool can_attack(Unit attacking_unit);
bool can_attack(Unit attacking_unit,bool defender_flying);
bool can_attack(UnitType attacking_unit_type,bool defender_flying);
bool can_attack(Unit attacking_unit,Unit defending_unit);
bool can_attack_in_range(Unit attacking_unit,Unit defending_unit,int margin=0);
bool can_attack_in_range_at_positions(Unit attacking_unit,Position attacking_position,Unit defending_unit,Position defending_position);
bool can_attack_in_range_at_positions(UnitType attacking_unit_type,Position attacking_position,Player attacking_player,UnitType defending_unit_type,Position defending_position,int margin=0);
bool can_attack_in_range_with_prediction(Unit attacking_unit,Unit defending_unit);
bool can_attack_in_range_at_position_with_prediction(Unit attacking_unit,Position attacking_position,Unit defending_unit);
bool is_melee(UnitType unit_type);
bool is_melee_or_worker(UnitType unit_type);
bool is_siege_tank(UnitType unit_type);
bool is_stimmable(UnitType unit_type);
bool is_suicidal(UnitType unit_type);
bool is_spellcaster(UnitType unit_type);
bool is_zerg_healing(UnitType unit_type);
bool is_low_priority_target(Unit unit);
bool is_undamaged(Unit unit);
bool is_less_than_half_damaged(Unit unit);
bool is_hp_undamaged(Unit unit);
bool is_on_cooldown(Unit unit,bool flying);
bool is_running_away(Unit combat_unit,Unit enemy_unit);
bool is_facing(Unit combat_unit,Unit enemy_unit);
int turn_frames_needed(Unit unit,Unit target_unit);
int ground_height(Position position);
int distance_to_terrain(WalkPosition walk_position);
int distance_to_terrain(Position position);
int ground_distance(Position a,Position b);
int ground_distance_to_bwcircle(FastPosition center,int radius,FastPosition other_position);
double distance_to_bwcircle(FastPosition delta,int radius);
double distance_to_square(FastPosition delta,int half_side);
int chebyshev_distance(FastPosition a,FastPosition b);
int chebyshev_norm(FastPosition delta);
int offense_max_range(UnitType attacking_unit_type,Player attacking_player,bool defender_flying);
int offense_max_range(Unit attacking_unit,bool defender_flying);
int weapon_min_range(UnitType attacking_unit_type);
int weapon_min_range(Unit attacking_unit);
int weapon_max_range(WeaponType weapon_type,Player player);
int weapon_max_range(UnitType attacking_unit_type,Player attacking_player,bool defender_flying);
int weapon_max_range(Unit attacking_unit,bool defender_flying);
double top_speed(UnitType unit_type,Player player);
int sight_range(UnitType unit_type,Player player);
DPF calculate_damage_per_frame(Unit source,Unit destination);
DPF calculate_damage_per_frame(UnitType source_type,Player source_player,UnitType destination_type,Player destination_player,int source_bunker_marines_loaded=0);
double calculate_frames_to_live(Unit unit);
double calculate_frames_to_live(Unit unit,const std::vector<Unit> enemy_units);
double calculate_repair_hitpoints_per_frame(UnitType type);
FastPosition edge_position(UnitType unit_type,FastPosition unit_position,FastPosition other_position);
FastPosition edge_to_point_delta(UnitType unit_type,FastPosition unit_position,FastPosition other_position);
int calculate_distance(UnitType unit_type,FastPosition unit_position,FastPosition other_position);
FastPosition edge_to_edge_delta(UnitType unit_type1,FastPosition unit_position1,UnitType unit_type2,FastPosition unit_position2);
int calculate_distance(UnitType unit_type1,FastPosition unit_position1,UnitType unit_type2,FastPosition unit_position2);
Position predict_position(Unit unit);
Position predict_position(Unit unit,int frames);
int building_extra_frames(UnitType type);
int estimate_building_start_frame_based_on_hit_points(Unit unit);
bool check_terrain_collision(UnitType type,FastPosition position);
std::vector<const InformationUnit*> colliding_units_sorted(Unit unit,FastPosition position);
bool check_unit_collision(Unit unit,FastPosition position,const InformationUnit* other_unit,int unit_margin=0);
bool check_collision(Unit unit,UnitType type,FastPosition position,int unit_margin=0);
bool check_collision(Unit unit,FastPosition position,int unit_margin=0);
int manhattan_distance(TilePosition a,TilePosition b);
void bwem_handle_destroy_safe(Unit unit);
int bwem_max_altitude();
bool done_or_in_progress(UpgradeType upgrade_type,int level=1);
bool done_or_in_progress(TechType tech_type);
void random_move(Unit unit);
void unit_move(Unit unit,Position position);
void unit_attack(Unit unit,Unit target);
void unit_right_click(Unit unit,Unit target);

struct Rect
{
	int left;
	int top;
	int right;
	int bottom;
	
	Rect(int left,int top,int right,int bottom) : left(left), top(top), right(right), bottom(bottom) {}
	Rect(Position top_left,Position bottom_right) : left(top_left.x), top(top_left.y), right(bottom_right.x), bottom(bottom_right.y) {}
	Rect(FastWalkPosition walk_position);
	Rect(UnitType type,FastPosition position);
	Rect(UnitType type,FastTilePosition tile_position,bool tile_size=false);
	
	FastPosition top_left() const { return FastPosition(left, top); }
	FastPosition bottom_right() const { return FastPosition(right, bottom); }
	
	bool is_inside(FastPosition position) const;
	bool overlaps(const Rect& o) const;
	int distance(const Rect& o) const;
	
	void draw(Color color) const;
	
	void inset(int d);
	
private:
	static bool in_range(int value,int low,int high);
	static bool ranges_overlap(int low1,int high1,int low2,int high2);
};

template <typename T>
class Line
{
public:
	Line(T a,T b) : a_(a), b_(b) {
		int w = std::abs(a.x - b.x);
		int h = std::abs(a.y - b.y);
		if (w >= h) {
			horizontal = true;
			if (a_.x > b_.x) std::swap(a_, b_);
		} else {
			horizontal = false;
			if (a_.y > b_.y) std::swap(a_, b_);
		}
	}
	T a() const { return a_; }
	T b() const { return b_; }
	class Iterator;
	Iterator begin() { return Iterator(*this, horizontal ? a_.x : a_.y); }
	Iterator end() { return Iterator(*this, (horizontal ? b_.x : b_.y) + 1); }
public:
	class Iterator
	{
	public:
		Iterator(const Line& line,int pos) : line_(line), pos_(pos) {}
		Iterator& operator++() { pos_++; return *this; }
		T operator*() {
			if (line_.horizontal) {
				double t = double(pos_ - line_.a_.x) / double(line_.b_.x - line_.a_.x);
				return T(pos_, int(line_.a_.y + t * (line_.b_.y - line_.a_.y) + 0.5));
			} else {
				double t = double(pos_ - line_.a_.y) / double(line_.b_.y - line_.a_.y);
				return T(int(line_.a_.x + t * (line_.b_.x - line_.a_.x) + 0.5), pos_);
			}
		}
		bool operator==(const Iterator& o) const { return pos_ == o.pos_; }
		bool operator!=(const Iterator& o) const { return pos_ != o.pos_; }
	private:
		const Line& line_;
		int pos_;
	};
private:
	T a_;
	T b_;
	bool horizontal;
};

class Gap
{
public:
	enum class Type
	{
		Invalid,
		None,
		Horizontal,
		Vertical,
		Diagonal
	};
	
	Gap() : type_(Type::Invalid) {}
	Gap(const BWEM::ChokePoint* chokepoint);
	Gap(const Rect& rect1,const Rect& rect2);
	
	bool is_valid() const { return type_ != Type::Invalid; }
	Type type() const { return type_; }
	Position a() const { return a_; }
	Position b() const { return b_; }
	
	int size() const;
	bool fits_through(UnitType unit_type) const;
	std::vector<Position> block_positions(UnitType unit_type,UnitType tight_for_unit_type) const;
	std::vector<Position> block_positions(const std::vector<UnitType>& unit_types,UnitType tight_for_unit_type,bool use_minimum_number_of_units=false,bool require_block=true) const;
	std::pair<Position,Position> line() const;
	
	void draw_box(Color color) const;
	void draw_line(Color color) const;
private:
	Type type_;
	Position a_;
	Position b_;
	
	class Placer
	{
	public:
		Placer(int a,int b,int middle,const std::vector<UnitType>& unit_types,int tight_dimension);
		virtual ~Placer() {}
		bool is_blocked();
		void place(std::vector<Position>& result);
		int n() { return n_; }
		
	protected:
		void init(bool use_minimum_number_of_units);
		virtual int unit_type_dimension(UnitType unit_type) = 0;
		virtual void add_result(std::vector<Position>& result,int current,UnitType unit_type) = 0;
		
		int a_;
		int b_;
		int middle_;
		const std::vector<UnitType>& unit_types_;
		int tight_dimension_;
		int n_ = 0;
		int s_ = 0;
		
	private:
		int determine_initial_gap_left();
	};
	
	class HorizontalPlacer final : public Placer
	{
	public:
		HorizontalPlacer(Position a,Position b,const std::vector<UnitType>& unit_types,UnitType tight_for_unit_type,bool use_minimum_number_of_units);
	protected:
		virtual int unit_type_dimension(UnitType unit_type) override;
		virtual void add_result(std::vector<Position>& result,int current,UnitType unit_type) override;
	};
	
	class VerticalPlacer final : public Placer
	{
	public:
		VerticalPlacer(Position a,Position b,const std::vector<UnitType>& unit_types,UnitType tight_for_unit_type,bool use_minimum_number_of_units);
	protected:
		virtual int unit_type_dimension(UnitType unit_type) override;
		virtual void add_result(std::vector<Position>& result,int current,UnitType unit_type) override;
	};
};

class DamageModel
{
public:
	DamageModel(UnitType type,Player player,int count=1);
	DamageModel(Unit target);
	void apply_damage(Unit source_unit,int damage_divisor=1);
	void apply_damage(UnitType source_type,Player source_player,int source_bunker_marines_loaded=0,int damage_divisor=1);
	void apply_damage_for_frames(UnitType source_type,Player source_player,int frames,int source_bunker_marines_loaded=0);
	
	double shields() const { return shields_; }
	double hp() const { return hp_; }
	double shields_lost() const { return initial_shields_ - shields_; }
	double hp_lost() const { return initial_hp_ - hp_; }
	bool is_dead() const { return hp_ <= 0.0; }
	
private:
	double initial_shields_;
	double initial_hp_;
	double shields_;
	double hp_;
	int shield_armor_;
	int armor_;
	UnitSizeType size_;
	bool flying_;
};

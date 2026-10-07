class UnitPotential
{
public:
	UnitPotential(Unit unit,FastPosition initial_position, std::function<void(UnitPotential&)>& potential_function) : unit_(unit), initial_position_(initial_position), potential_function_(potential_function) {}
	void add_potential(UnitType type,FastPosition position,double potential,int max_distance);
	void add_potential(Unit unit,double potential,int max_distance);
	void add_potential(Unit unit,double potential);
	void add_potential(FastPosition position,double potential,int max_distance);
	void add_potential(FastPosition position,double potential);
	void block();
	void repel_units(const std::vector<Unit>& units,int extra_margin=0,double potential=1.0);
	void repel_units_undetected(const std::vector<Unit>& units,int extra_margin=0);
	void repel_friendly(const std::vector<Unit>& units,Unit skip_unit,int distance);
	void repel_storms();
	void repel_emps();
	void repel_buildings();
	void repel_terrain();
	void kite_units(const std::vector<Unit>& units,Unit skip_unit = nullptr);
	
	Unit unit() const { return unit_; }
	UnitType type() const { return unit_->getType(); }
	FastPosition position() const { return position_; }
	FastPosition initial_position() const { return initial_position_; }
	double value() const { return value_; }
	bool empty() const { return value_ == 0.0; }
private:
	Unit unit_;
	FastPosition position_;
	FastPosition initial_position_;
	double value_;
	std::function<void(UnitPotential&)>& potential_function_;
	
	double calculate_potential(FastPosition position);
	friend bool unit_potential(Unit unit,std::function<void(UnitPotential&)> function);
};

bool unit_potential(Unit unit,std::function<void(UnitPotential&)> function);

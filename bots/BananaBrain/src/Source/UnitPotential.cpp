#include "BananaBrain.h"

double UnitPotential::calculate_potential(FastPosition position)
{
	position_ = position;
	value_ = 0.0;
	potential_function_(*this);
	return value_;
}

bool unit_potential(Unit unit,std::function<void(UnitPotential&)> potential_function)
{
	FastPosition initial_position = predict_position(unit);
	if (!check_collision(unit, initial_position)) initial_position = unit->getPosition();
	UnitPotential unit_potential(unit, initial_position, potential_function);
	FastPosition current_position = initial_position;
	double current_potential = unit_potential.calculate_potential(current_position);
	
	int step_size = unit->isFlying() ? 8 : 4;
	
	for (int step = 0; step < 16; step++) {
		FastPosition best_position = current_position;
		double best_potential = current_potential;
		bool better_position_found = false;
		
		for (FastPosition delta_position : { FastPosition(-step_size, 0), FastPosition(step_size, 0), FastPosition(0, -step_size), FastPosition(0, step_size),
										 FastPosition(-step_size, -step_size), FastPosition(step_size, -step_size), FastPosition(step_size, step_size), FastPosition(-step_size, step_size) }) {
			FastPosition new_position = current_position + delta_position;
			if (new_position.isValid() && check_collision(unit, new_position)) {
				double new_potential = unit_potential.calculate_potential(new_position);
				if (new_potential > best_potential) {
					best_position = new_position;
					best_potential = new_potential;
					better_position_found = true;
				}
			}
		}
		
		if (!better_position_found) break;
		current_position = best_position;
		current_potential = best_potential;
	}
	
	current_position.makeValid();
	
	if (unit->isFlying() && unit->getType().haltDistance() > 1) {
		int distance = initial_position.getApproxDistance(current_position);
		if (distance > unit->getPlayer()->topSpeed(unit->getType()) * Broodwar->getRemainingLatencyFrames() &&
			distance * 256 < unit->getType().haltDistance()) {
			FastPosition delta = current_position - initial_position;
			double d_halt = 1.02 * unit->getType().haltDistance() / 256.0;
			double dx = d_halt * delta.x / distance;
			double dy = d_halt * delta.y / distance;
			FastPosition new_position = initial_position + FastPosition(int(dx + 0.5), int(dy + 0.5));
			if (new_position.isValid()) current_position = new_position;
		}
	}
	
	if (current_position != initial_position) {
		unit_move(unit, current_position);
		return true;
	} else {
		return false;
	}
}

void UnitPotential::add_potential(UnitType type,FastPosition position,double potential,int max_distance)
{
	FastPosition delta = edge_to_edge_delta(unit_->getType(), position_, type, position);
	double distance = distance_to_bwcircle(delta, max_distance);
	if (distance < 0.0) {
		value_ += (distance * potential);
	}
}

void UnitPotential::add_potential(Unit unit,double potential,int max_distance)
{
	add_potential(unit->getType(), predict_position(unit), potential, max_distance);
}

void UnitPotential::add_potential(Unit unit,double potential)
{
	FastPosition delta = edge_to_edge_delta(unit_->getType(), position_, unit->getType(), predict_position(unit));
	double distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
	value_ += (distance * potential);
}

void UnitPotential::add_potential(FastPosition position,double potential,int max_distance)
{
	FastPosition delta = position_ - position;
	double distance = distance_to_bwcircle(delta, max_distance);
	if (distance < 0.0) {
		value_ += (distance * potential);
	}
}

void UnitPotential::add_potential(FastPosition position,double potential)
{
	FastPosition delta = position_ - position;
	double distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
	value_ += (distance * potential);
}

void UnitPotential::block()
{
	value_ = -INFINITY;
}

void UnitPotential::repel_units(const std::vector<Unit>& units,int extra_margin,double potential)
{
	for (Unit other_unit : units) {
		if (!other_unit->isCompleted() || is_disabled(other_unit) || !other_unit->isPowered()) continue;
		
		int max_distance = weapon_max_range(other_unit, unit_->isFlying());
		if (max_distance >= 0) {
			int margin = other_unit->getType().canMove() ? 24 : 8;
			add_potential(other_unit, potential, max_distance + margin + extra_margin);
		}
	}
}

void UnitPotential::repel_units_undetected(const std::vector<Unit>& units,int extra_margin)
{
	double largest_unit_distance = 0.0;
	double largest_detector_distance = 0.0;
	
	for (Unit other_unit : units) {
		if (!other_unit->isCompleted() || is_disabled(other_unit) || !other_unit->isPowered()) continue;
		
		int max_distance = weapon_max_range(other_unit, unit_->isFlying());
		if (max_distance >= 0) {
			FastPosition delta = edge_to_edge_delta(unit_->getType(), position_, other_unit->getType(), predict_position(other_unit));
			int margin = other_unit->getType().canMove() ? 24 : 8;
			double distance = distance_to_bwcircle(delta, max_distance + margin + extra_margin);
			largest_unit_distance = std::max(largest_unit_distance, -distance);
		}
	}
	
	for (auto& enemy_unit : information_manager.enemy_units()) {
		int range = enemy_unit->detection_range();
		if (range >= 0 &&
			(!enemy_unit->unit->exists() || enemy_unit->unit->isPowered())) {
			FastPosition delta = edge_to_edge_delta(unit_->getType(), position_, enemy_unit->type, enemy_unit->position);
			int margin = enemy_unit->type.canMove() ? 24 : 8;
			double distance = distance_to_bwcircle(delta, range + margin + extra_margin);
			largest_detector_distance = std::max(largest_detector_distance, -distance);
		}
	}
	
	value_ -= std::min(largest_unit_distance, largest_detector_distance);
}

void UnitPotential::repel_friendly(const std::vector<Unit>& units,Unit skip_unit,int distance)
{
	for (Unit other_unit : units) {
		if (other_unit == skip_unit) continue;
		add_potential(other_unit, 1.0, distance + 4);
	}
}

void UnitPotential::repel_storms()
{
	double largest_storm_evasion_distance = 0.0;
	std::vector<Position> positions = micro_manager.list_existing_storm_positions();
	
	for (auto& position : positions) {
		FastPosition delta = edge_to_point_delta(unit_->getType(), position_, position);
		double distance = distance_to_bwcircle(delta, WeaponTypes::Psionic_Storm.outerSplashRadius() + 32);
		largest_storm_evasion_distance = std::max(largest_storm_evasion_distance, -distance);
	}
	
	value_ += (-largest_storm_evasion_distance);
}

void UnitPotential::repel_emps()
{
	double largest_emp_evasion_distance = 0.0;
	
	for (auto& bullet : Broodwar->getBullets()) {
		if (bullet->getType() == BulletTypes::EMP_Missile && bullet->getTargetPosition().isValid()) {
			FastPosition delta = edge_to_point_delta(unit_->getType(), position_, bullet->getTargetPosition());
			double distance = distance_to_square(delta, WeaponTypes::EMP_Shockwave.outerSplashRadius() + 32);
			largest_emp_evasion_distance = std::max(largest_emp_evasion_distance, -distance);
		}
	}
	
	value_ += (-largest_emp_evasion_distance);
}

void UnitPotential::repel_buildings()
{
	if (unit_->isFlying()) return;
	
	for (auto& entry : information_manager.all_units()) {
		const InformationUnit& information_unit = entry.second;
		if (information_unit.type.isBuilding() && !information_unit.flying) {
			add_potential(information_unit.type, information_unit.position, 1.0, 32);
		}
	}
}

void UnitPotential::repel_terrain()
{
	if (unit_->isFlying()) return;
	int distance = distance_to_terrain(position_) - 32;
	if (distance < 0) {
		value_ += (distance * 1.0);
	}
}

void UnitPotential::kite_units(const std::vector<Unit>& units,Unit skip_unit)
{
	for (Unit other_unit : units) {
		if (other_unit == skip_unit || !other_unit->isCompleted() || is_disabled(other_unit) || !other_unit->isPowered()) continue;
		
		int max_distance = weapon_max_range(other_unit, unit_->isFlying());
		if (max_distance >= 0) {
			int attack_distance = weapon_max_range(unit_, other_unit->isFlying());
			
			if (attack_distance < 0) {
				add_potential(other_unit, 1.0, max_distance + 24);
			} else {
				int min_distance = weapon_min_range(other_unit);
				if (min_distance > 0) {
					int distance = unit_->getDistance(other_unit);
					if (distance - min_distance < max_distance - distance) {
						add_potential(other_unit, -1.0);
					} else {
						add_potential(other_unit, 1.0, attack_distance - 4);
					}
				} else {
					add_potential(other_unit, 1.0, attack_distance - 4);
				}
			}
		}
	}
}

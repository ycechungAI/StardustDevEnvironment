#pragma once

class PathFinder : public Singleton<PathFinder>
{
public:
	void init();
	void close_small_chokepoints_if_needed();
	Position first_common_path_position(Position position,std::vector<Position> destinations);
	bool execute_path(Unit unit,Position position,std::function<void(void)> command);
	
private:
	bool small_chokepoints_closed_ = false;
	std::map<const BWEM::ChokePoint*,FastTilePosition> ramp_high_ground_;
	
	void init_ramps();
	
	int distance_to_chokepoint(Position position,const BWEM::ChokePoint* choke);
};

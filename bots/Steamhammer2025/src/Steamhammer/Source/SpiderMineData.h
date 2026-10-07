#pragma once

#include "MicroManager.h"
#include <BWAPI.h>
#include <vector>

namespace UAlbertaBot
{

class SpiderMineData
{
public:
	// Abstract names for locations where mines can be laid.
	// We can assign credit to locations which are more successful.
	// TODO implementing these a bit at a time
	// TODO credit assignment is not implemented either
	enum class MineLocation
	{
		  None
		, BlockBase			// where the resource depot should go
		, WatchBase			// in sight of a base location, not blocking it
		, InEnemyBase
		//, DefendBase		// to defend the approaches of a base
		//, Antidrop		// to defend against drops and recalls
		, OnPath			// on path between friendly and enemy start bases
		//, AirPath			// on expected air paths, for vision
		//, AtChoke			// above ramps or in other chokes
		//, BlockAddon		// block an enemy addon from being built
		//, HitProduction	// at an enemy barracks, gateway, larva
		, Combat			// get in the face of sieged tanks or grouped dragoons
	};

	class Job
	{
	public:
		const MineLocation location;
		const BWAPI::Position position;
		const Base * base;			// can be null
		const double priority;
		BWAPI::Unit vulture;		// when assigned

		Job(MineLocation loc, const BWAPI::Position & pos, double p);
		Job(MineLocation loc, const BWAPI::Position & pos, const Base * b, double p);
		bool ok() const;
	};

private:
	bool _initialized;
	std::vector<Job *> _jobs;						// for laying static mines
	std::map<BWAPI::Unit, Job *> _assignments;		// vulture -> job
	//std::map<BWAPI::Unit, MineLocation> _mines;	// abstract locations of each mine

	bool validPosition(const BWAPI::Position & pos);
	void postJob(MineLocation loc, const BWAPI::Position & pos, const Base * b, double p);
	void baseJobs();
	void initialize();

public:
	SpiderMineData();

	Job * getJob(BWAPI::Unit vulture);
	void assign(BWAPI::Unit vulture, Job * job);
	Job * assignment(BWAPI::Unit vulture) const;
	std::map<BWAPI::Unit, Job *> & getAssignments() { return _assignments; }

	void jobCompleted(BWAPI::Unit vulture, BWAPI::Unit mine);
	void jobFailed(BWAPI::Unit vulture);

	// A free-ranging vulture planted a mine. It is requested to report in.
	void report(MineLocation loc, const BWAPI::Position & pos);

	void draw();

	void sanityCheckAssignments() const;
};
}
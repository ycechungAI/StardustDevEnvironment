#pragma once

#include "Common.h"
#include "MapGrid.h"
#include "SquadOrder.h"
#include "Cluster.h"
#include "MapTools.h"
#include "InformationManager.h"
#include "Micro.h"

namespace UAlbertaBot
{
struct AirThreat
{
	BWAPI::Unit	unit;
	double			weight;
};

struct GroundThreat
{
	BWAPI::Unit	unit;
	double			weight;
};

class MicroManager
{
	BWAPI::Unitset  _units;

protected:
	
	SquadOrder			order;

	virtual void        executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster) = 0;
	void                trainSubUnits(BWAPI::Unit unit) const;
	bool                checkPositionWalkable(BWAPI::Position pos);
	bool                unitNearEnemy(BWAPI::Unit unit) const;
	bool                unitNearChokepoint(BWAPI::Unit unit) const;

	bool				mobilizeUnit(BWAPI::Unit unit) const;      // unsiege or unburrow
	bool				immobilizeUnit(BWAPI::Unit unit) const;    // siege or burrow
	bool				unstickStuckUnit(BWAPI::Unit unit) const;
    
	//From Arrak
	BWAPI::Position		kiteVector; //cumulative kite vector of all threats in the area
	BWAPI::Position		tangentVector; //cumulative rotational vector of all relevant threats towards destination
	int					accumulatedTangents;
	void				initializeVectors();
	void				updateVectors(BWAPI::Unit, BWAPI::Unit, int = 4, BWAPI::Position = BWAPI::Position(-1, -1));
	void				normalizeVectors();
	BWAPI::Position		computeAttractionVector(BWAPI::Unit);

	void                drawOrderText();

public:
						MicroManager();
    virtual				~MicroManager(){}

	const BWAPI::Unitset & getUnits() const;
	BWAPI::Position     calcCenter() const;

	void				setUnits(const BWAPI::Unitset & u);
	void				execute(const SquadOrder & order, const UnitCluster & cluster);
	void				destroyNeutralTargets(const BWAPI::Unitset & targets);
	BWAPI::Unit			closestUnit(BWAPI::Unit target, const BWAPI::Unitset & targets);
	BWAPI::Unit			closestThreat(BWAPI::Unit target, const BWAPI::Unitset & targets);
	void				regroup(const BWAPI::Position & regroupPosition, const UnitCluster & cluster) const;
	int					getTargetPrioritySD(BWAPI::Unit target, BWAPI::UnitType type);
	BWAPI::Unit			getTargetSD(BWAPI::Unit unit, const BWAPI::Unitset & targets);
	bool				goodUnderDarkSwarm(BWAPI::UnitType type);

};
}
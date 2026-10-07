#pragma once;

#include <Common.h>
#include "MicroManager.h"
#include "UnitUtil.h"

namespace UAlbertaBot
{
class MicroManager;

class SurveyerManager : public MicroManager
{

	BWAPI::Unit			_firstSurveyor;
	BWAPI::Position		getNearestStartLocation();
	std::map<BWAPI::Unit, bool>	cloakedUnitMap;

public:

	SurveyerManager();
	~SurveyerManager() {}

	void				executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);
	void				update(SquadOrder order);
	BWAPI::Unit			closestCloakedUnit(const BWAPI::Unitset & cloakedUnits, BWAPI::Unit detectorUnit);

};
}
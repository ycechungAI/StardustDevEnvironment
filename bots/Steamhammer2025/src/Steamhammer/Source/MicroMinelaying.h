#pragma once

#include "MicroManager.h"
#include "SpiderMineData.h"

namespace UAlbertaBot
{

class MicroMinelaying : public MicroManager
{
	void handleDeadVultures();
	void assignVulture(BWAPI::Unit vulture);

	void microVulture(BWAPI::Unit vulture, SpiderMineData::Job * job);

public:
	MicroMinelaying();
    void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster) {};

    void update();
};
}
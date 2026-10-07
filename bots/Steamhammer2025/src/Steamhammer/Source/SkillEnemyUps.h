#pragma once

#include "Skill.h"
#include <BWAPI.h>

namespace UAlbertaBot
{

class SkillEnemyUps : public Skill
{
private:
    std::map<BWAPI::TechType, int> techs;
	std::map<std::pair<BWAPI::UpgradeType, int>, int> ups;

	void recordTech(BWAPI::TechType t);
	void updateUpgrades(const std::vector<BWAPI::UpgradeType> & ups);

	void updateTerran();
	void updateProtoss();
	void updateZerg();

public:

	SkillEnemyUps();

    std::string putData() const;
    void getData(GameRecord & r, const std::string & line);

    bool enabled() const   { return true;  };
    bool feasible() const  { return false; };
    bool good() const      { return false; };
    void execute()         { };
    void update();
};

}
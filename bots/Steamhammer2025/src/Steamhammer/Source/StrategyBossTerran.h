#pragma once

#include "StrategyBoss.h"

namespace UAlbertaBot
{

class StrategyBossTerran : public StrategyBoss
{
private:
	// The primary unit mix.
	enum class Mix
	{
		  None
		, Marines				// before gas
		, Infantry
		, TankInfantry
		, TankVulture
		, TankVultureGoliath
		, Goliath
		, BattlecruiserInfantry
		, BattlecruiserVulture
	};

	// Minor components of the army.
	enum class Aux
	{
		  FIRST = 0
		, Vessels = 0
		, Wraiths
		, Valkyries
		// , Dropships
		, Goliaths				// a small number to counter enemy air harass
		// , Ghosts
		, LAST
	};

	// Strategy decisions made early, with little information.
	// But they're expected to persist, because they affect the upgrade pattern.
	bool _preferInfantry;		// rather than mech
	bool _wraithHarass;			// make wraiths to harass, circumstances permitting

protected:
	std::string getMixString(Mix mix) const;
	std::string getMixString() const;
	std::string getAuxString() const;

private:
	// Our unit mix.
	Mix _mix;
	std::set<Aux> _auxes;				// extras, minor additions to the unit mix

	bool isBarracksMix() const;
	bool isFactoryMix() const;
	bool isAirMix() const;

	void manageBases();

	void setBackstopRules();
	void setForecastRules();
	void setMixUpgradeRules();			// air or ground, depending
	void setAuxUpgradeRules();
	void setSpecialMatchupRules();

	void setFirebatRules();
	void setBattlecruiserRules();
	void setMixRules();
	void setVesselRules();
	void setAuxesRules();
	void setRegularRules();

	void setBattlecruiserMix();
	void setSCVrules();
		
	void updateAuxTvT();
	void updateMixTvT();
	void setRulesXvT();

	void updateAuxTvP();
	void updateMixTvP();
	void setRulesXvP();

	void updateAuxTvZ();
	void updateMixTvZ();
	void setRulesXvZ();

	void strategyDecisions();

	bool isLiftable(BWAPI::Unit unit);
	void landCCs();
	void manageUrgentIssues();

	bool _emergencyNoBase;
	bool _emergencyEnemyAttack;
	bool _emergencyNeedDetection;
	bool _emergencyNeedAirDefense;
	bool _emergencyNaturalContain;
	bool _emergencyEnemyDead;

	void setBasicsRules();
	void recognizeEmergency();
	void setEmergencyRules();

public:

	StrategyBossTerran();
};

}
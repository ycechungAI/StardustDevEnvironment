#pragma once

#include <BWAPI.h>
#include <set>

namespace UAlbertaBot
{
// StrategyBoss is pure: It exists only as a superclass.
class StrategyBoss
{
private:
	bool _initialized;
	bool _lastFrameEmergency;				// notice when _emergency changes

	void makePredictions();

	bool isTechBuilding(BWAPI::UnitType type);
	void updateTechBuildings();
	void updateStatus();

protected:
	// Predicted or observed needs.
	bool _buildupMode;			// building up mass before we move out
	bool _emergency;
	bool _emergencyDefense;
	bool _allHands;
	int _airDefenseNeededFrame;
	int _detectionNeededFrame;

	int _maxWorkers;		// the number needed to saturate our bases
	int _nBases;			// how many bases we own
	int _mineralWorkers;	// how many workers are assigned to minerals
	int _nGas;				// how many refineries we have to mine from
	int _nFreeGas;			// how many untaken geysers at our bases
	std::set<BWAPI::UnitType> _techBuildings;
	bool _techBuildingsChangedThisFrame;
	int _mySupply;			// of combat units

	// Opponent unit mix.
	int enemySupply;		// of known enemy combat units
	int detectors;			// count
	int droppers;			// count
	double infantry;		// T: barracks units; P: zealot+dragoon; Z: zergling+hydra
	double enemyAir;		// air units as a proportion of combat supply

	// Terran. Proportion of supply of known combat units.
	double marine;
	int ghost;				// count
	double tank;
	double vulture;
	double goliath;
	double mech;			// tank + vulture + goliath
	double wraith;
	double valkyrie;
	double battlecruiser;
	// enemyAir == wraith + valkyrie + battlecruiser

	// Protoss.
	double zealot;
	double dragoon;
	double reaver;
	double templar;
	double dt;
	double archon;
	int darkArchon;			// count, since it's an aux unit
	double corsair;
	double scout;
	double carrier;
	int arbiter;			// count

	// Zerg.
	double zergling;
	double hydralisk;
	double lurker;
	double ultralisk;
	int queen;
	double mutalisk;
	int scourge;			// count, since they don't live long
	double guardian;
	double devourer;
	int defiler;			// count, since it's an aux unit

	double airToGroundT() const;
	double airToGroundZ() const;
	double antiAirZ() const;

	bool enemyMayHaveArbiters() const;
	bool expectLurkers() const;
	bool expectMutalisks() const;

	bool baseRunningLow() const;		// at least one base is low on minerals

	// Utility.
	bool gettingUnit(BWAPI::UnitType building);
	bool hasUnit(BWAPI::UnitType building);

	void analyzeMyMix();

	// Figure out what the opponent has made.
	void analyzeOpponentMixT();
	void analyzeOpponentMixP();
	void analyzeOpponentMixZ();

	virtual std::string getMixString() const = 0;
	virtual std::string getAuxString() const = 0;
	virtual void drawStrategyBossInfo() const;

	// Make decisions about taking bases and making refineries.
	virtual void manageBases() = 0;

	virtual void setRulesXvT() = 0;
	virtual void setRulesXvP() = 0;
	virtual void setRulesXvZ() = 0;
	void setRules();

	virtual void strategyDecisions() {};	// to feed into later mix decisions

	// If necessary, handle troubles like lifting buildings before they are destroyed.
	// Or less urgent issues like canceling unneeded production.
	virtual void manageUrgentIssues() {};

	// Low-level emergencies: Replace missing basic buildings.
	virtual void setBasicsRules() = 0;

	// Recognize whether the situation is an emergency that calls for
	// strategy boss intervention.
	// It makes sense to classify the emergency here so that
	// setEmergencyRules() doesn't have to redo it.
	virtual void recognizeEmergency() = 0;
	bool isEmergency() const { return _emergency; };
	virtual void setEmergencyRules() = 0;

	const int UpdateInterval = 211;		// a prime number of frames
	virtual bool shouldUpdate() const;

public:

	StrategyBoss();

	bool getAllHands() const { return _allHands; };
	void setBuildupMode(bool buildup) { _buildupMode = buildup; };
	bool inBuildupMode() const { return _buildupMode; };

	// NOTE Could generalize this to take a MacroAct.
	//      The 4 cases are mobile unit, building, upgrade, tech.
	int trainTimeRemaining(BWAPI::UnitType unitType) const;
	int upgradeTimeRemaining(BWAPI::UpgradeType upgrade, int level = 1) const;

	void update();
};

}
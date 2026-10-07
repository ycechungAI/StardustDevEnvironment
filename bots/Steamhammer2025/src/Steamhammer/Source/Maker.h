#pragma once

#include "MacroAct.h"

namespace UAlbertaBot
{
// Convenience macros to add a rule.
#define ADD(r) the.maker.add(new r)
#define ADD_IN_FRONT(r) the.maker.addInFront(new r)

struct MakeState
{
	int minerals;
	int gas;
	int gasMargin;
	int gasHysteresis;
	int lowGas;
	int highMinerals;
	int supply;

	// See comment at Maker::maybeAlterGasCollection().
	MakeState()
		: gasMargin(0)			// auto-adjusted when the rules are preprocessed
		, gasHysteresis(50)		// not adjusted by the current code
	{}

	void set();
};

// Priorities for reacting more quickly to emergencies.
// Most rules should use the default priority Normal.
// Emergency rules can be included alongside regular rules, but should be
// first so that their priorities are attended to first.
// TODO priorities are recorded in the rule but are not implemented
//      possibly the Urgent priority may not be needed
enum class Priority
{
	  Normal		// build as usual
	, Urgent		// pause other production to build this first
	, Emergency		// cancel already-ordered production if necessary to build this asap
};

// Rules control making of units, upgrades, and tech.
// Thw MacroAct cannot be a command. The strategy boss should do those itself.
// count == 0 for units means keep making them indefinitely.
// count > 0 for units and buildings means keep making them up to that limit, replacing losses.
// count > 0 for upgrades means upgrade to that level. For tech, only count == 1 makes sense.
// NOTE Automatic conversion will turn a unit type, upgrade type, tech type into a MacroAct for you.
//      Rule(BWAPI::UnitTypes::Terran_Marine) asks for infinite marines.
//      Rule(BWAPI::UpgradeTypes::Terran_Vehicle_Weapons, 2) upgrades mech to +2 weapons.
class Rule
{
public:
	MacroAct act;
	int count;
	Priority priority;

	Rule(MacroAct a, int n = 0, Priority p = Priority::Normal);

	// If true, the rule's conditions are met. It can be executed if there is a producer.
	virtual bool ok(const MakeState & state) const;
	// Do what the rule says--produce something. Some rule types may not need a producer.
	virtual void execute(MakeState & state, BWAPI::Unit producer = nullptr) const;

	int mineralPrice() const { return act.mineralPrice(); };
	int gasPrice() const { return act.gasPrice(); };

	virtual std::string getString() const;	// debug string
};

// What to make when minerals are piling up because we are short on gas.
// For example, zealots or gateways to make zealots.
// You can give this rule gas units, but results are likely to be awful.
class RuleLowGas : public Rule
{
public:
	RuleLowGas(MacroAct a, int n = 0, Priority p = Priority::Normal);

	bool ok(const MakeState & state) const override;
	std::string getString() const override;
};

// Maintain at least this ratio with another unit.
// This only makes sense for mobile units!
// E.g. maintain 1 medic for each 5 marines:
// RuleRatio(BWAPI::UnitTypes::Terran_Medic, BWAPI::UnitTypes::Terran_Marine, 1.0/5)
class RuleRatio : public Rule
{
public:
	BWAPI::UnitType ratioTo;
	double ratio;
	
	RuleRatio(MacroAct a, BWAPI::UnitType withRespectTo, double r, Priority p = Priority::Normal);
	RuleRatio(MacroAct a, BWAPI::UnitType withRespectTo, double r, int n, Priority p = Priority::Normal);

	bool ok(const MakeState & state) const override;
	std::string getString() const override;
};

class RuleRepower : public Rule
{
	RuleRepower(Priority p = Priority::Normal);

	bool ok(const MakeState & state) const override;
	void execute(MakeState & state, BWAPI::Unit producer = nullptr) const override;
	std::string getString() const override;
};

class Maker
{
private:
	std::vector<Rule *> _rules;
	Priority _priority;				// of the current rule
	bool _preprocessed;				// have the rules been preprocessed?

	// The mineral and gas values are only valid while production is in progress.
	MakeState _state;

	// Keep track of what producers are available.
	// In comments, this is called "the cache". It caches the producers.
	// producer type -> set of producer units that remain free to produce
	std::map<BWAPI::UnitType, std::vector<BWAPI::Unit>> _producers;

	// If production for the frame ends early, set _done = true to break out.
	bool _done;

	// We may be waiting for a given rule to be ready to execute.
	Rule * _waiting;

	bool hasProducer(const MacroAct & act);
	bool hasProducerWithoutAddon(const MacroAct & act);
	std::vector<BWAPI::Unit> & getProducers(const MacroAct & act);
	bool needsNoAddon(const MacroAct & act) const;
	BWAPI::Unit chooseProducer(const MacroAct & act, std::vector<BWAPI::Unit> & actProducers);
	bool shouldSendToQueue(const MacroAct & act) const;
	void sendToQueue(const MacroAct & act);

	void produce(Rule * rule);
	bool enoughResources(const Rule * rule) const;		// minerals, gas, supply
	void bananaSuperBeyond();
	bool tryRule(Rule * rule);

	int mostSupplyForOneItem() const;
	bool needSupply() const;
	void buildSupply();

	bool hasPrerequisite(BWAPI::UnitType type) const;
	BWAPI::UnitType checkPrerequisite(BWAPI::UnitType type) const;
	BWAPI::UnitType getPrerequisite(const Rule * rule) const;
	BWAPI::UnitType getPrerequisite(const MacroAct & act, int level = 1) const;
	void setGasMargins();
	void preprocessRulesIfNeeded();

	void cancelBuildingsAndUpgrades(const Rule * rule);
	void cancelWorkers(int mineralTarget);
	void maybeCancelStuff(const Rule * rule);
	void maybeAlterGasCollection() const;

public:

	Maker();

	// Clear, then add rules in priority order, most important first.
	void clearRules();
	void add(Rule * rule);
	void addInFront(Rule * rule);			// front rules have the highest priority

	const std::vector<Rule *> & getRules() const { return _rules; };
	void drawRules(int x, int y) ;			// Config::Debug::DrawStrategyBossInfo

	// Information about what should be produced, for outside callers.
	bool isMaking(BWAPI::UnitType type) const;

	void update();
};

}
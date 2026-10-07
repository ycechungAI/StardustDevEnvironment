#include "Maker.h"

#include "ProductionManager.h"
#include "The.h"
#include "UABAssert.h"
#include "UnitUtil.h"

// Production plan execution for terran and protoss.
// The opening is executed through the queue of ProductionManager.
// Terran and protoss then switch to Maker and no longer use the queue
// for most production. Buildings go through the queue, however,
// because the queue has the machinery to handle all the cases.
// When anything is in the queue, Maker pauses for the queue to
// clear before resuming its own production.
// (Zerg plans production in StrategyBossZerg and executes it through the queue.)

// * Maker produces stuff as declared by a set of rules.
// * See StrategyBossTerran and StrategyBossProtoss, which set rules.
// * Rules control production of units, buildings, upgrades and tech.
//   Exception: An archon is created by micro from two high templar.
// * The strategy boss sets the rules and can change them at any time.
// * Maker automatically builds tech buildings needed to execute
//   the rules. But if you want more than one production building
//   of a given type, the strategy boss must specify so in a rule.
//   Rule(Protoss_Reaver) is all you need; Maker will build one robo fac
//   and one robo bay and then start producing reavers. If you want
//   double robo, though, you need Rule(Protoss_Robotics_Facility, 2).
//   The strategy boss must decide how many production buildings of
//   each type are needed for balanced production, depending on the
//   strategy and the rate of income.
// * The strategy boss must decide when to expand and when to build
//   gas refineries. It carries out the decision by simply adding
//   rules, at the right time, that Maker follows.
// * Maker automatically builds supply and turns gas mining on or off
//   depending on need. It's not perfect, but pretty good.
// * Rule(Protoss_Zealot) makes zealots without stopping.
//   Rule(Protoss_Zealot, 4) tries to keep 4 zealots on the map.
//   RuleRatio(Protoss_Reaver, Protoss_Shuttle, 2.0) tries to maintain
//   a ratio of 2 reavers per shuttle. Any ratio >= 0 is allowed
//   (zero is not useful, but it's convenient to allow it for
//   rules whose values are filled in by code).
//   RuleRatio(Protoss_Reaver, Protoss_Shuttle, 1.0, 3) tries for
//   one reaver per shuttle but limits the reavers to 3, no matter
//   how many shuttles there may be.
//   Rule(Protoss_Ground_Weapons, 3) will upgrade one step at a time
//   until ground weapons are maxed. It will even build citadel and
//   archives automatically, though not at an efficient time.
// * RuleLowGas can declare how to spend minerals when you have a
//   severe gas shortage. Instead of banking minerals that you can't
//   make dragoons with, you can add gateways and make zealots.
//   With low gas rules in place, it's automatic.
//   Alternately, the strategy boss can intervene and set the rules
//   to something appropriate (and it can seek more gas).
// * The MacroAct can specify where the unit is to be produced (e.g.,
//   where a building is to be placed). This info is not included in
//   the Config::Debug::DrawStrategyBossInfo display, so it may look
//   confusing.
//   Rule(MacroAct(Terran_Bunker, MacroLocation::EnemyNatural)).
//   Rule(MacroAct(Protoss_Zealot, MacroLocation::Proxy)).
// * Redundant rules are not a problem. The space and time overhead
//   is tiny. Allowing them simplifies the strategy bosses.
// * Maker will try to replace destroyed buildings needed to execute
//   its rules, short of replacing a resource depot. The strategy boss
//   has to handle that emergency.
//   But if the emergency calls for a sophisticated response, other
//   code had better make the decisions. Maker is good for continuous
//   production, not for special cases.
// * Rules are tested in order. The first rule is tried first, etc.
//   The first rule that can be produced, even if there are not enough
//   minerals, or gas, or supply to do it immediately, is produced.
//   The tech must exist, and there must be an available producer.
//   Maker waits for the resources to be enough. So unit mix depends on
//   the order of rules and the number of production buildings.
// * If you put rules to build units before rules to add production
//   buildings, then the buildings will only be added when there are
//   leftover resources after producing units. It's not perfect macro,
//   but it's simple and fairly efficient.
// * Every rule has a priority. Usually it is Priority::Nornal.
//   In case of emergency, the strategy boss may react by posting
//   rules of higher priority.
//   With Priority::Urgent, if minerals are short then Maker will
//   cancel workers in production to get enough to execute the rule
//   (or at least let it execute sooner). For example,
//   Rule(Protoss_Zealot, 0, Priority::Urgent).
//   The debug display shows "!" in front of the rule.
//   With Priority::Emergency, Maker will cancel research, upgrades,
//   and buildings under construction to get enough resources.
//   The debug display shows "!!" in front of the rule.

//   The strategy boss can also choose on its own to cancel stuff
//   it doen't want any more. When mutalisks appear, it could cancel
//   zealots to get dragoons and corsairs out faster.

// Maker goes to a lot of trouble to make it easy for the strategy boss
// to post good rules, but good rules still take care to write.
// The order of rules is critical and can take experiments to adjust.
// The strategy boss has to handle special cases to get efficient production.
// For example, if you have one robo and want both observers and reavers,
// then simple rules will produce only one or the other, and a ratio rule
// with a fixed ratio will be rigid. But code can adjust the rules over
// time to get a good result. Or it can, say, prioritize observers ahead
// of reavers, but only up to a fixed limit:
//   Rule (BWAPI::UnitTypes::Protoss_Observer, 3)
//   Rule (BWAPI::UnitTypes::Protoss_Reaver)

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// State.

// Don't change the gas limit. It's under outside control.
void MakeState::set()
{
	minerals = std::max(0, the.self()->minerals() - BuildingManager::Instance().getReservedMinerals());
	gas = std::max(0, the.self()->gas() - BuildingManager::Instance().getReservedGas());
	supply = the.self()->supplyTotal() - the.self()->supplyUsed();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Rules.

Rule::Rule(MacroAct a, int n, Priority p)
	: act(a)
	, count(n <= 0 && (a.isBuilding() || a.isUpgrade()) ? 1 : n)
	, priority(p)
{
	UAB_ASSERT(!a.isCommand(), "rules cannot execute commands");
	if (a.isUpgrade())
	{
		// Force it into range.
		count = std::max(std::min(n, a.getUpgradeType().maxRepeats()), 1);
	}
	else if (a.isTech())
	{
		count = 1;		// the only value that makes sense
	}
}

RuleLowGas::RuleLowGas(MacroAct a, int n, Priority p)
	: Rule(a, n, p)
{
}

RuleRatio::RuleRatio(MacroAct a, BWAPI::UnitType withRespectTo, double r, Priority p)
	: Rule(a, 0, p)
	, ratioTo(withRespectTo)
	, ratio(r)
{
	UAB_ASSERT(act.isUnit(), "not a unit type");
	UAB_ASSERT(!act.isBuilding() && !ratioTo.isBuilding(), "buildings not supported");
	UAB_ASSERT(ratio >= 0.0, "bad ratio");
}

RuleRatio::RuleRatio(MacroAct a, BWAPI::UnitType withRespectTo, double r, int n, Priority p)
	: Rule(a, n, p)
	, ratioTo(withRespectTo)
	, ratio(r)
{
	UAB_ASSERT(act.isUnit(), "not a unit type");
	UAB_ASSERT(!act.isBuilding() && !ratioTo.isBuilding(), "buildings not supported");
	UAB_ASSERT(ratio >= 0.0, "bad ratio");
}

// TODO this is not fully implemented and won't do anything
// Add pylons for unpowered protoss buildings.
RuleRepower::RuleRepower(Priority p)
	: Rule(MacroAct(BWAPI::UnitTypes::Protoss_Pylon), 1, p)
{
	UAB_ASSERT(the.selfRace() == BWAPI::Races::Protoss, "Repower is for protoss only");
}

// -- -- -- --

bool Rule::ok(const MakeState & state) const
{
	if (count > 0)
	{
		if (act.isUnit())
		{
			if (act.getUnitType() == BWAPI::UnitTypes::None)
			{
				return false;
			}

			// Do we already have as many as the rule calls for?
			int n = the.my.all.count(act.getUnitType());
			if (act.isBuilding())
			{
				n += BuildingManager::Instance().getNumUnstarted(act.getUnitType());
			}
			return n < count;
		}

		// The constructor ensures that an upgrade or tech has a sensible count.
		if (act.isUpgrade())
		{
			return
				the.self()->getUpgradeLevel(act.getUpgradeType()) < count &&
				!the.self()->isUpgrading(act.getUpgradeType());
		}
		if (act.isTech())
		{
			return
				!the.self()->hasResearched(act.getTechType()) &&
				!the.self()->isResearching(act.getTechType());
		}
	}
	return true;		// count == 0
}

bool RuleLowGas::ok(const MakeState & state) const
{
	return
		state.gas < state.lowGas && state.minerals > state.highMinerals &&
		Rule::ok(state);
}

bool RuleRatio::ok(const MakeState & state) const
{
	if (!Rule::ok(state))
	{
		return false;
	}

	int all = the.my.all.count(ratioTo);
	return 
		all > 0 &&				// no division by zero	
		ratio > double(the.my.all.count(act.getUnitType())) / all;
}

bool RuleRepower::ok(const MakeState & state) const
{
	if (the.selfRace() != BWAPI::Races::Protoss)
	{
		return false;
	}

	// Find one unpowered building.
	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (!u->isPowered())
		{
			return true;
		}
	}
	return false;
}

// -- -- -- --

void Rule::execute(MakeState & state, BWAPI::Unit producer) const
{
	UAB_ASSERT(producer, "null producer");
	act.produce(producer);
	state.minerals -= act.mineralPrice();
	state.gas -= act.gasPrice();
	state.supply -= act.supplyRequired();
}

// TODO unfinished
void RuleRepower::execute(MakeState & state, BWAPI::Unit producer) const
{
	// Find one unpowered building.
	BWAPI::Unit unpowered = nullptr;
	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (!u->isPowered())
		{
			unpowered = u;
			break;
		}
	}

	if (unpowered == nullptr)
	{
		return;
	}
}

// -- -- -- --

std::string Rule::getString() const
{
	std::stringstream ss;
	ss << NiceMacroActName(act.getName());
	if (count > 0)
	{
		ss << " " << count;
	}
	return ss.str();
}

std::string RuleLowGas::getString() const
{
	return "low gas " + Rule::getString();
}

std::string RuleRatio::getString() const
{
	std::stringstream ss;
	ss << NiceMacroActName(act.getName()) << " " << count;
	if (ratioTo != BWAPI::UnitTypes::None)
	{
		ss << " " << ratio << "/" << NiceMacroActName(ratioTo.getName());
	}
	return ss.str();
}

std::string RuleRepower::getString() const
{
	return "Repower";
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

bool Maker::hasProducer(const MacroAct & act)
{
	if (act.isBuilding() && !act.isAddon())
	{
		return the.my.completed.count(the.self()->getRace().getWorker()) > 0;
	}
	return !getProducers(act).empty();
}

bool Maker::hasProducerWithoutAddon(const MacroAct & act)
{
	if (act.isBuilding() && !act.isAddon())
	{
		return the.my.completed.count(the.self()->getRace().getWorker()) > 0;
	}

	for (BWAPI::Unit producer : getProducers(act))
	{
		if (!producer->getAddon())
		{
			return true;
		}
	}
	return false;
}

// Find the set of available producers for this act.
// Either compute them or fetch them from the cache.
// NOTE This is not called for buildings, which are produced by workers.
std::vector<BWAPI::Unit> & Maker::getProducers(const MacroAct & act)
{
	BWAPI::UnitType producerType = act.whatBuilds();

	auto it = _producers.find(producerType);
	if (it != _producers.end())
	{
		// We found it in the cache.
		return it->second;
	}

	// It's not in the cache. Cache it and return a reference to the cache entry.
	// The caller may modify the value, so it must be a reference to the cache entry!
	std::vector<BWAPI::Unit> actProducers = act.getCandidateProducers();
	auto result = _producers.insert(std::pair(producerType, actProducers));
	UAB_ASSERT(result.second, "failed cache insertion");

	return result.first->second;
}

// Is this a terran unit that can be produced without an addon on the producer,
// though the producer may have an addon?
bool Maker::needsNoAddon(const MacroAct & act) const
{
	return
		the.selfRace() == BWAPI::Races::Terran &&
		act.isUnit() &&
		(act.getUnitType() == BWAPI::UnitTypes::Terran_Vulture ||
			act.getUnitType() == BWAPI::UnitTypes::Terran_Goliath ||
			act.getUnitType() == BWAPI::UnitTypes::Terran_Wraith);
}

// Choose a producer, paying little attention to which.
// The only exception is to prefer no addon on the producer when possible,
// so as not to unnecessarily monopolize producers with addon.
// Otherwise we just pop one off the end of the vector.
// The caller promises that there is at least one producer.
BWAPI::Unit Maker::chooseProducer(const MacroAct & act, std::vector<BWAPI::Unit> & actProducers)
{
	UAB_ASSERT(!actProducers.empty(), "zero producers");

	BWAPI::Unit producer = nullptr;
	if (needsNoAddon(act))
	{
		for (auto it = actProducers.begin(); it != actProducers.end(); )
		{
			if ((*it)->getAddon())
			{
				++it;
			}
			else
			{
				producer = *it;
				actProducers.erase(it);
				return producer;
			}
		}
	}
	
	producer = actProducers.back();
	actProducers.pop_back();

	// The cache entry may become empty. That's OK.

	//BWAPI::Broodwar->printf("  producer %s %d, %d left", producer->getType().getName().c_str(), producer->getID(), actProducers.size());

	return producer;
}

// Many items can be produced here by just calling .produce().
// Others need special machinery for finding a worker or whatever, and have to be
// sent to the queue for production.
bool Maker::shouldSendToQueue(const MacroAct & act) const
{
	return
		act.isBuilding() ||
		act.isUnit() && UnitUtil::IsMorphedUnitType(act.getUnitType());
}

void Maker::sendToQueue(const MacroAct & act)
{
	UAB_ASSERT(the.production.getQueue().isEmpty(), "queue is not empty");

	// The queue can only produce one item per frame.
	// If we send something there, stop producing; we're done for the frame.
	// There's no need to update the state.
	the.production.getQueue().queueAsLowestPriority(act);
	_done = true;
}

void Maker::produce(Rule * rule)
{
	if (shouldSendToQueue(rule->act))
	{
		//BWAPI::Broodwar->printf("PASSING TO QUEUE %s", rule->getString().c_str());
		sendToQueue(rule->act);
	}
	else
	{
		//BWAPI::Broodwar->printf("DIRECTLY PRODUCING %s", rule->getString().c_str());
		UAB_ASSERT(!getProducers(rule->act).empty(), "zero producers");
		UAB_ASSERT(!rule->act.isBuilding(), "building bypassed queue");
		rule->execute(_state, chooseProducer(rule->act, getProducers(rule->act)));
	}
}

// Do we have enough minerals, gas, and supply to produce it?
bool Maker::enoughResources(const Rule * rule) const
{
	return
		_state.minerals >= rule->mineralPrice() &&
		_state.gas >= rule->gasPrice() &&
		_state.supply >= rule->act.supplyRequired();
}

// We are waiting for a rule that does not have enough resources.
// If we have extra minerals, we might be able to produce a later rule
// that requires minerals only.
// cf. Slate and Atkins.
void Maker::bananaSuperBeyond()
{
	if (_state.minerals < _waiting->mineralPrice() + 50)
	{
		return;
	}

	bool skip = true;
	for (Rule * rule : _rules)
	{
		// Skip all earlier rules. They've already been tried.
		if (skip)
		{
			if (rule == _waiting)
			{
				skip = false;
			}
			continue;
		}

		// NOTE All addons cost gas. We don't need to check whether it's an addon.
		// NOTE Do not produce a vulture from a factory with a machine shop.
		//      hasProducerWithoutAddon() implements this: Only vultures are affected.
		//      Otherwise if we want tanks we might never get any.
		while (!_done &&
			rule->gasPrice() == 0 &&
			rule->mineralPrice() + _waiting->mineralPrice() <= _state.minerals &&
			rule->ok(_state) &&
			hasProducerWithoutAddon(rule->act))
		{
			//BWAPI::Broodwar->printf("beyond %s", rule->getString().c_str());
			produce(rule);
		}
		if (_done || _state.minerals < _waiting->mineralPrice() + 50)
		{
			return;
		}
	}
}

// Figure out if we can execute the rule, and if so, do it.
// 1. If the rule conditions are not met, go to the next rule (return false).
// 2. If the rule has no producer, go to the next rule (return false).
// 3. If the conditions are met but resources or supply are not enough,
//    wait until there's enough (set _done to stop for this frame).
// 4. Otherwise execute the rule.
//    Return true if the rule may be able to execute again (its count is not 1).
bool Maker::tryRule(Rule * rule)
{
	// If maxed in supply, do not add a production building.
	if (the.self()->supplyUsed() >= 390 && 
		rule->act.isUnit() &&
		UnitUtil::IsProductionBuildingType(rule->act.getUnitType()))
	{
		return false;
	}

	if (!rule->ok(_state))
	{
		//BWAPI::Broodwar->printf("not ok %s", rule->getString().c_str());
		return false;
	}

	if (!hasProducer(rule->act))
	{
		//BWAPI::Broodwar->printf("no producer %s", rule->getString().c_str());
		return false;
	}

	if (!enoughResources(rule))
	{
		//BWAPI::Broodwar->printf("not enough resources %s", rule->getString().c_str());
		maybeCancelStuff(rule);
		if (!enoughResources(rule))
		{
			//BWAPI::Broodwar->printf("waiting for %s", rule->getString().c_str());
			// Still not enough.
			if (rule->gasPrice() > _state.gas)
			{
				// If we need more gas, ensure we're collecting it!
				// The regular gas decision can leave it off, freezing production.
				WorkerManager::Instance().setCollectGas(true);
			}
			_waiting = rule;
			bananaSuperBeyond();		// we might be able to execute a later mineral-only rule
			_done = true;
			return false;
		}
	}

	// Don't try to produce more than one addon of a given type at once.
	// It's because addons turn into production goals, which are not
	// executed at once. One machine shop can turn into a pile of them
	// that we don't want.
	if (rule->act.isAddon() && ProductionManager::Instance().alreadyHasGoal(rule->act.getUnitType()))
	{
		return false;
	}

	_waiting = nullptr;		// we're about to produce, we're not waiting any more
	produce(rule);

	return rule->count != 1;
}

// Find the unit we might make that uses the most supply. Assume it's at least 4.
// We want enough supply that we can make it.
int Maker::mostSupplyForOneItem() const
{
	int mostSupply = 4;
	for (Rule * rule : _rules)
	{
		if (rule->act.isUnit())
		{
			mostSupply = std::max(mostSupply, rule->act.getUnitType().supplyRequired());
		}
	}
	return mostSupply;
}

// Use a simple heuristic method to decide how far ahead of time to build supply.
// NOTE BWAPI supply values are doubled compared to the GUI display.
bool Maker::needSupply() const
{
	const int totalSupply = the.self()->supplyTotal();
	if (totalSupply >= 400)
	{
		return false;
	}

	// Pending supply.
	BWAPI::UnitType supply = the.self()->getRace().getSupplyProvider();
	int pendingSupply = supply.supplyProvided() *
		(the.my.all.count(supply) - the.my.completed.count(supply) + BuildingManager::Instance().getNumUnstarted(supply));

	// Only count resource depots that will finish at least as early as regular supply.
	// No need to look in the building queue for unstarted ones--they'll take longer.
	BWAPI::UnitType depot = the.self()->getRace().getResourceDepot();
	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (u->getType() == depot && !u->isCompleted() && u->getRemainingBuildTime() <= supply.buildTime())
		{
			pendingSupply += depot.supplyProvided();
		}
	}

	// We need supply if (provided supply + pending supply) is less than (used supply + margin).
	const int supplyUsed = the.self()->supplyUsed();
	const int supplyExcess = totalSupply + pendingSupply - supplyUsed;
	int margin = mostSupplyForOneItem();

	// Add to the margin depending on a crude measure of the rate of production.
	margin += WorkerManager::Instance().getNumMineralWorkers() / 2;

	// If we're beyond the early game, then we usually didn't adjust enough.
	// This is a very rough heuristic widening of the margin.
	if (supplyUsed >= 200)
	{
		margin += 32;
	}
	else if (supplyUsed >= 70)
	{
		margin += 16;
	}

	// Special case if we're gas-limited: Spend minerals now to get ahead in supply.
	if (_waiting &&
		_waiting->gasPrice() > _state.gas &&
		_waiting->mineralPrice() <= _state.minerals + 100)
	{
		margin += 16;
	}

	return supplyExcess < margin;
}

void Maker::buildSupply()
{
	sendToQueue(the.selfRace().getSupplyProvider());
}

// Set the state's gasMargin, lowGas, and highMinerals values.
void Maker::setGasMargins()
{
	int lowGas = 50;
	int highMinerals = 100;
	int maxGas = 0;
	for (const Rule * rule : _rules)
	{
		int price = rule->gasPrice();
		maxGas = std::max(maxGas, price);
		if (rule->count == 0)
		{
			lowGas = std::max(lowGas, price);
			highMinerals = std::max(highMinerals, rule->mineralPrice());
		}
	}
	_state.gasMargin = maxGas;
	_state.lowGas = lowGas + 25;
	_state.highMinerals = highMinerals + 50;
}

// This checks whether the prerequisite is STARTED.
// If it is, we don't want to automatically make another one.
// It may not be finished yet.
bool Maker::hasPrerequisite(BWAPI::UnitType type) const
{
	if (type == BWAPI::UnitTypes::None)
	{
		return true;
	}
	bool ok = the.my.all.count(type) > 0 ||
		type.isBuilding() && BuildingManager::Instance().getNumUnstarted(type) > 0;
	return
		the.my.all.count(type) > 0 ||
		type.isBuilding() && BuildingManager::Instance().getNumUnstarted(type) > 0;
}

BWAPI::UnitType Maker::checkPrerequisite(BWAPI::UnitType type) const
{
	BWAPI::UnitType nextPrereq = getPrerequisite(type);
	if (nextPrereq == BWAPI::UnitTypes::None)
	{
		return type;
	}
	return nextPrereq;
}

// Get one prerequisite for a given rule, if needed.
BWAPI::UnitType Maker::getPrerequisite(const Rule * rule) const
{
	if (rule->act.isUpgrade())
	{
		// Rule creation should force the rule's upgrade count to be valid.
		int level = the.self()->getUpgradeLevel(rule->act.getUpgradeType());
		level = std::min(level+1, rule->count);
		UAB_ASSERT(level >= 1 && level <= 3, "bad upgrade level");
		return getPrerequisite(rule->act, level);
	}

	return getPrerequisite(rule->act);
}

// If the act needs prerequisites that we don't have, return one of them
// (the one most likely to be slow to build).
// The level is the upgrade level, for upgrades only.
// Return BWAPI::UnitTypes::None is no prerequisite is needed.
// Return BWAPI::UnitTypes::Unknown (arbitrary choice) if a prereq
// is unfinished; we have to wait for it, so post an empty rule.
BWAPI::UnitType Maker::getPrerequisite(const MacroAct & act, int level) const
{
	if (act.gasPrice() > the.self()->gas() &&
		the.my.all.count(the.self()->getRace().getRefinery()) == 0)
	{
		// Rare but can be disastrous when it happens.
		return the.self()->getRace().getRefinery();
	}
	if (act.isUnit())
	{
		// To make a probe, you need a nexus. To make a nexus, you need a probe.
		// Let's avoid infinite recursion. The strategy boss has to handle this.
		if (act.getUnitType() == the.self()->getRace().getWorker() ||
			act.getUnitType() == the.self()->getRace().getResourceDepot())
		{
			return BWAPI::UnitTypes::None;
		}

		const std::map<BWAPI::UnitType, int> & requirements = act.getUnitType().requiredUnits();
		for (std::pair<BWAPI::UnitType, int> requirement : requirements)
		{
			BWAPI::UnitType requiredType = requirement.first;
			if (hasPrerequisite(requiredType))
			{
				if (the.my.completed.count(requiredType) == 0)
				{
					return BWAPI::UnitTypes::Unknown;	// it's started but not finished
				}
			}
			else
			{
				return checkPrerequisite(requiredType);
			}
		}
	}
	else if (act.isUpgrade() || act.isTech())
	{
		BWAPI::UnitType requiredType = act.isUpgrade() ?
			act.getUpgradeType().whatsRequired(level) :
			act.getTechType().requiredUnit();
		if (hasPrerequisite(requiredType))
		{
			if (requiredType != BWAPI::UnitTypes::None && the.my.completed.count(requiredType) == 0)
			{
				return BWAPI::UnitTypes::Unknown;	// it's started but not finished
			}
		}
		else
		{
			return checkPrerequisite(requiredType);
		}
	}

	// NOTE To support lurkers, check Lurker Aspect here.

	// The producer of the act is the least likely to be slow to build.
	BWAPI::UnitType requiredType = act.whatBuilds();
	if (hasPrerequisite(requiredType))
	{
		if (requiredType != BWAPI::UnitTypes::None && the.my.completed.count(requiredType) == 0)
		{
			return BWAPI::UnitTypes::Unknown;	// it's started but not finished
		}
	}
	else
	{
		return checkPrerequisite(requiredType);
	}

	return BWAPI::UnitTypes::None;
}

// 
void Maker::preprocessRulesIfNeeded()
{
	for (auto it = _rules.begin(); it != _rules.end(); ++it)
	{
		BWAPI::UnitType prerequisite = getPrerequisite(*it);
		if (prerequisite != BWAPI::UnitTypes::None)
		{
			delete (*it);
			if (prerequisite == BWAPI::UnitTypes::Unknown)
			{
				*it = new Rule(BWAPI::UnitTypes::None);		// empty rule = wait for the prereq to finish
			}
			else
			{
				*it = new Rule(prerequisite, 1);
			}
		}
	}

	setGasMargins();			// only need to do this when the rules have changed
	_waiting = nullptr;
	_preprocessed = true;
}

// Cancel buildings and upgrades until there are enough resources to execute the rule.
// This just cancels what it finds first. There's no attempt to prioritize, except
// that production buildings are spared when possible.
void Maker::cancelBuildingsAndUpgrades(const Rule * rule)
{
	BWAPI::Unit productionBuilding = nullptr;

	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (!u->getType().isBuilding())
		{
			continue;
		}
		if (u->getLastCommandFrame() >= the.now() - BWAPI::Broodwar->getRemainingLatencyFrames())
		{
			// We already issued a command very recently.
			continue;
		}

		if (u->isUpgrading())
		{
			//BWAPI::Broodwar->printf("cancel %s", u->getUpgrade().getName().c_str());
			_state.minerals += u->getUpgrade().mineralPrice();
			_state.gas += u->getUpgrade().gasPrice();
			u->cancelUpgrade();
		}
		else if (u->isResearching())
		{
			//BWAPI::Broodwar->printf("cancel %s", u->getTech().getName().c_str());
			_state.minerals += u->getTech().mineralPrice();
			_state.gas += u->getTech().gasPrice();
			u->cancelResearch();
		}
		else if (u->isBeingConstructed() && rule->act.getUnitType() != u->getType())
		{
			// Don't cancel a building of the type we want to build in order to build it!
			if (UnitUtil::IsTechBuildingType(u->getType()))
			{
				//BWAPI::Broodwar->printf("cancel %s", u->getType().getName().c_str());
				_state.minerals += u->getType().mineralPrice();
				_state.gas += u->getType().gasPrice();
				u->cancelConstruction();
			}
			else
			{
				productionBuilding = u;
			}
		}
		if (_state.minerals >= rule->mineralPrice() && _state.gas >= rule->gasPrice())
		{
			return;
		}
	}
	// We didn't find enough. Cancel the one production building we remembered too.
	if (productionBuilding)
	{
		//BWAPI::Broodwar->printf("cancel %s", productionBuilding->getType().getName().c_str());
		_state.minerals += productionBuilding->getType().mineralPrice();
		_state.gas += productionBuilding->getType().gasPrice();
		productionBuilding->cancelConstruction();
	}
}

// Cancel workers in production to reach the mineral target.
void Maker::cancelWorkers(int mineralTarget)
{
	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (u->getType() == the.selfRace().getResourceDepot() && u->isTraining())
		{
			u->cancelTrain();
			_state.minerals += 50;
			if (_state.minerals >= mineralTarget)
			{
				return;
			}
		}
	}
}

// Cancel unneeded or unwanted stuff, depending on the rule's priority.
// This is called only when we are short of resources to produce the rule.
// 1. Normal priority - cancel nothing.
// 2. Urgent priority - cancel workers if we're short of minerals.
// 3. Emergency priority - cancel enough stuff to execute the rule.
// NOTE This version goes in stages. Better would be to optimize the
//      response as a whole to cancel the minimum.
void Maker::maybeCancelStuff(const Rule * rule)
{
	if (rule->priority == Priority::Normal)
	{
		return;
	}

	// Priority is Urgent or Emergency.
	// If we need minerals and we are not desperately short on workers, cancel workers.
	if (_state.minerals < rule->mineralPrice() &&
		the.my.completed.count(the.selfRace().getWorker()) >= 12)
	{
		cancelWorkers(rule->mineralPrice());
		if (_state.minerals >= rule->mineralPrice() && _state.gas >= rule->gasPrice())
		{
			return;
		}
	}

	if (rule->priority != Priority::Emergency)
	{
		return;
	}

	// Priority is Emergency.
	cancelBuildingsAndUpgrades(rule);
}

// Turn gas on/off.
// Stop mining gas if it goes over gasMargin + gasHysteresis.
// Start again if it goes under gasMargin.
// The gas margin is set high enough to produce any one item in the rules.
// NOTE This is different than how Steamhammer zerg does it.
// This decision should be made AFTER the rules have run and maybe used gas!
void Maker::maybeAlterGasCollection() const
{
	bool go;
	int reserve = _waiting ? _waiting->gasPrice() : 0;
	if (the.my.completed.count(the.selfRace().getWorker()) < 4)
	{
		go = false;		// not enough workers to mine gas and minerals at the same time
	}
	else if (the.self()->supplyUsed() >= 390)
	{
		go = true;		// supply is maxed, build a bank of minerals and gas (not only minerals)
	}
	else if (the.self()->minerals() >= 800)
	{
		go = true;		// we have ample minerals, always collect gas
	}
	else if (WorkerManager::Instance().isCollectingGas())
	{
		go = the.self()->gas() - reserve < _state.gasMargin + _state.gasHysteresis;
	}
	else
	{
		go = the.self()->gas() - reserve < _state.gasMargin;
	}
	WorkerManager::Instance().setCollectGas(go);
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

Maker::Maker()
	: _priority(Priority::Normal)
	, _preprocessed(false)
	, _done(false)
	, _waiting(nullptr)
{
	_rules.reserve(20);				// should usually be enough
}

void Maker::clearRules()
{
	for (Rule * rule : _rules)
	{
		delete rule;
	}

	_rules.clear();
	_preprocessed = false;
}

void Maker::add(Rule * rule)
{
	_rules.push_back(rule);
}

void Maker::addInFront(Rule * rule)
{
	_rules.insert(_rules.begin(), rule);
}

// -- -- -- --
// Debug drawing.

void Maker::drawRules(int x, int y)
{
	for (Rule * rule : _rules)
	{
		if (rule == _waiting)
		{
			BWAPI::Broodwar->drawCircleScreen(BWAPI::Position(x-7, y+6), 3, BWAPI::Colors::Green, true);
		}

		char color = green;
		     if (!rule->ok(_state))       { color = red; }
		else if (!hasProducer(rule->act)) { color = orange; }
		else if (!enoughResources(rule))  { color = yellow; }
		
		std::string priority = "";
		if (rule->priority == Priority::Urgent)
		{
			priority = "!";
		}
		else if (rule->priority == Priority::Emergency)
		{
			priority = "!!";
		}

		BWAPI::Broodwar->drawTextScreen(x, y, "%c%s%s", color, priority.c_str(), rule->getString().c_str());
		y += 10;
	}
}

// -- -- -- --
// Information.

// Other code sometimes needs to know what is being produced.
// For example, it's no use making a bunker if marines are not on the menu.
bool Maker::isMaking(BWAPI::UnitType type) const
{
	for (const Rule * rule : _rules)
	{
		if (rule->act.isUnit() && rule->act.getUnitType() == type)
		{
			return true;
		}
	}
	return false;
}

// -- -- -- --

void Maker::update()
{
	if (!ProductionManager::Instance().isOutOfBook())
	{
		return;						// nothing to do while in book
	}

	// We have to do this before building supply, because needSupply()
	// refers to _waiting which points into the old rules.
	preprocessRulesIfNeeded();

	if (needSupply())
	{
		buildSupply();
		return;						// supply is queued, so no more production this frame
	}

	_producers.clear();				// rebuild the cache, set of producers may be different
	_done = false;

	// Current minerals and gas.
	_state.set();
	if (_state.minerals < 50)
	{
		// Not enough to produce anything (ignoring ghost and scourge).
		return;
	}

	// Produce as possible.
	// Execute the first rule that passes its conditions and has a producer.
	// If we are short of resources or supply, we're done for now; wait for enough.
	// We're also done if we passed something to the queue to be produced there.
	for (Rule * rule : _rules)
	{
		while (!_done && tryRule(rule))		// execute this one rule until we can't
		{}
		if (_done)
		{
			break;
		}
	}

	maybeAlterGasCollection();				// don't collect more gas than we need
}

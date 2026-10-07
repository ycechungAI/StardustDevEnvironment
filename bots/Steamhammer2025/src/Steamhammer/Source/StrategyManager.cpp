#include "StrategyManager.h"

#include "Bases.h"
#include "BuildingPilot.h"
#include "CombatCommander.h"
#include "Maker.h"
#include "MapTools.h"
#include "Moveout.h"
#include "OpponentModel.h"
#include "OpsStrategy.h"
#include "ProductionManager.h"
#include "StrategyBossTerran.h"
#include "StrategyBossProtoss.h"
#include "StrategyBossZerg.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

StrategyManager::StrategyManager() 
    : _selfRace(the.self()->getRace())
    , _enemyRace(the.enemy()->getRace())
    , _emptyBuildOrder(the.self()->getRace())
    , _openingGroup("")
    , _openingStaticDefenseDropped(false)
	, _strategyBoss(getStrategyBoss())
{
}

StrategyBoss * StrategyManager::getStrategyBoss()
{
	if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Terran)
	{
		return new StrategyBossTerran();
	}
	if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Protoss)
	{
		return new StrategyBossProtoss();
	}
	return nullptr;
}

StrategyManager & StrategyManager::Instance() 
{
    static StrategyManager instance;
    return instance;
}

const BuildOrder & StrategyManager::getOpeningBookBuildOrder() const
{
    auto strategyIt = _strategies.find(Config::Strategy::StrategyName);

    // look for the build order in the build order map
    if (strategyIt != std::end(_strategies))
    {
		the.moveout.setRush((*strategyIt).second.getRush());
        return (*strategyIt).second._buildOrder;
    }
    else
    {
        UAB_ASSERT_WARNING(false, "Strategy not found: %s, returning empty initial build order", Config::Strategy::StrategyName.c_str());
		the.moveout.setRush(false);
		return _emptyBuildOrder;
    }
}

void StrategyManager::addStrategy(const std::string & name, Strategy strategy)
{
    _strategies[name] = strategy;
}

// Set _openingGroup depending on the current strategy, which in principle
// might be from the config file or from opening learning.
// This is part of initialization; it happens early on.
void StrategyManager::setOpeningGroup()
{
    auto buildOrderItr = _strategies.find(Config::Strategy::StrategyName);

    if (buildOrderItr != std::end(_strategies))
    {
        _openingGroup = (*buildOrderItr).second._openingGroup;
    }
}

const std::string & StrategyManager::getOpeningGroup() const
{
    return _openingGroup;
}

// NOTE Assumes that only terran uses buildup mode to gather mass before moving out.
void StrategyManager::setBuildupMode(bool buildup)
{
	if (the.selfRace() == BWAPI::Races::Terran)
	{
		_strategyBoss->setBuildupMode(buildup);
	}
}

void StrategyManager::update()
{
	if (_selfRace == BWAPI::Races::Protoss || _selfRace == BWAPI::Races::Terran)
	{
		_strategyBoss->update();
	}
}

// Called every frame before items are produced from the queue.
// This only operates during the opening.
// For the rest of the game, the strategy boss takes care of things.
void StrategyManager::handleUrgentProductionIssues(BuildOrderQueue & queue)
{
	if (_selfRace == BWAPI::Races::Zerg)
	{
		StrategyBossZerg::Instance().handleUrgentProductionIssues(queue);
		return;
	}

	if (ProductionManager::Instance().isOutOfBook())
	{
		return;
	}

    // This is the enemy plan that we have seen in action.
    OpeningPlan enemyPlan = OpponentModel::Instance().getEnemyPlan();

    // For all races, if we've just discovered that the enemy is going with a heavy macro opening,
    // drop any static defense that our opening build order told us to make.
    if (!_openingStaticDefenseDropped)
    {
        // We're in the opening book and haven't dropped static defenses yet. Should we?
        if (enemyPlan == OpeningPlan::Turtle ||
            enemyPlan == OpeningPlan::SafeExpand)
            // enemyPlan == OpeningPlan::NakedExpand && _enemyRace != BWAPI::Races::Zerg) // could do this too
        {
            // 1. Remove upcoming defense buildings from the queue.
            queue.dropStaticDefenses();
            // 2. Cancel unfinished defense buildings.
            for (BWAPI::Unit unit : the.self()->getUnits())
            {
                if (UnitUtil::IsComingStaticDefense(unit->getType()) && unit->canCancelConstruction())
                {
                    the.micro.Cancel(unit);
                }
            }
            // 3. Never do it again.
            _openingStaticDefenseDropped = true;
            if (Config::Debug::DrawQueueFixInfo)
            {
                BWAPI::Broodwar->printf("queue: any static defense dropped as unnecessary");
            }
        }
    }

	const int numDepots = the.my.all.count(_selfRace.getResourceDepot());
	const int nWorkers =
		the.my.all.count(_selfRace == BWAPI::Races::Terran
			? BWAPI::UnitTypes::Terran_SCV
			: BWAPI::UnitTypes::Protoss_Probe);
	
	// If we're in an extreme emergency, break out of the opening.
	if (numDepots == 0 || nWorkers < 3)
	{
		ProductionManager::Instance().goOutOfBookAndClearQueue();
	}

	// Detect if there's a supply block once per second.
	if ((BWAPI::Broodwar->getFrameCount() % 24 == 1) && detectSupplyBlock(queue) && the.self()->minerals() >= 100 && nWorkers > 0)
	{
		if (Config::Debug::DrawQueueFixInfo)
		{
			BWAPI::Broodwar->printf("queue: building supply");
		}

		queue.queueAsHighestPriority(MacroAct(the.self()->getRace().getSupplyProvider()));
	}

	const MacroAct * nextInQueuePtr = queue.isEmpty() ? nullptr : &(queue.getHighestPriorityItem().macroAct);

	// If we need gas, make sure it is turned on.
	int gas = the.self()->gas();
	if (nextInQueuePtr)
	{
		if (nextInQueuePtr->gasPrice() > gas)
		{
			WorkerManager::Instance().setCollectGas(true);
		}
	}

	// If we have collected too much gas (and we're running from the queue), turn it off.
	if (ProductionManager::Instance().isOutOfBook() &&
		!ProductionManager::Instance().getQueue().isEmpty() &&
		gas > 400 &&
		gas > 4 * the.self()->minerals())
	{
		int queueMinerals, queueGas;
		queue.totalCosts(queueMinerals, queueGas);
		if (gas >= queueGas)
		{
			WorkerManager::Instance().setCollectGas(false);
		}
	}

	// This is the enemy plan that we have seen, or if none yet, the expected enemy plan.
	// Some checks can use the expected plan, some are better with the observed plan.
	OpeningPlan likelyEnemyPlan = OpponentModel::Instance().getBestGuessEnemyPlan();

	// If the opponent is rushing, make some defense.
	if (likelyEnemyPlan == OpeningPlan::Proxy ||
		likelyEnemyPlan == OpeningPlan::WorkerRush ||
		likelyEnemyPlan == OpeningPlan::FastRush)
		// enemyPlan == OpeningPlan::HeavyRush)           // we can react later to this
	{
		// If we are terran and have marines, make a bunker.
		if (_selfRace == BWAPI::Races::Terran)
		{
			if (!queue.anyInQueue(BWAPI::UnitTypes::Terran_Bunker) &&
				the.my.all.count(BWAPI::UnitTypes::Terran_Marine) > 0 &&          // usefulness requirement
				the.my.completed.count(BWAPI::UnitTypes::Terran_Barracks) > 0 &&  // tech requirement for a bunker
				the.my.all.count(BWAPI::UnitTypes::Terran_Bunker) == 0 &&
				!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Bunker) &&
				the.self()->minerals() >= 49 && nWorkers > 0)
			{
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Bunker, MacroLocation::Front));
			}
		}

		// If we are protoss, make a shield battery.
		// NOTE This works, but is turned off because protoss can't use the battery yet.
		/*
		else if (_selfRace == BWAPI::Races::Protoss)
		{
		if (the.my.completed.count(BWAPI::UnitTypes::Protoss_Pylon) > 0 &&    // tech requirement
		the.my.completed.count(BWAPI::UnitTypes::Protoss_Gateway) > 0 &&  // tech requirement
		the.my.all.count(BWAPI::UnitTypes::Protoss_Shield_Battery) == 0 &&
		!queue.anyInQueue(BWAPI::UnitTypes::Protoss_Shield_Battery) &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Shield_Battery) &&
		anyWorkers)
		{
		queue.queueAsHighestPriority(BWAPI::UnitTypes::Protoss_Shield_Battery);
		}
		}
		*/
	}
}

// Return true if we're supply blocked and should build supply.
// NOTE This understands zerg supply but is not used when we are zerg.
bool StrategyManager::detectSupplyBlock(BuildOrderQueue & queue) const
{
    // If the _queue is empty or supply is maxed, there is no block.
    if (queue.isEmpty() || the.self()->supplyTotal() >= 400)
    {
        return false;
    }

    // If supply is being built now, there's no block. Return right away.
    // Terran and protoss calculation:
    if (BuildingManager::Instance().isBeingBuilt(the.self()->getRace().getSupplyProvider()))
    {
        return false;
    }

    // Terran and protoss calculation:
    int supplyAvailable = the.self()->supplyTotal() - the.self()->supplyUsed();

    // Zerg calculation:
    // Zerg can create an overlord that doesn't count toward supply until the next check.
    // To work around it, add up the supply by hand, including hatcheries.
    if (the.self()->getRace() == BWAPI::Races::Zerg) {
        supplyAvailable = -the.self()->supplyUsed();
        for (auto unit : the.self()->getUnits())
        {
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
            {
                supplyAvailable += 16;
            }
            else if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg &&
                unit->getBuildType() == BWAPI::UnitTypes::Zerg_Overlord)
            {
                return false;    // supply is building, return immediately
                // supplyAvailable += 16;
            }
            else if ((unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery && unit->isCompleted()) ||
                unit->getType() == BWAPI::UnitTypes::Zerg_Lair ||
                unit->getType() == BWAPI::UnitTypes::Zerg_Hive)
            {
                supplyAvailable += 2;
            }
        }
    }

    int supplyCost = queue.getHighestPriorityItem().macroAct.supplyRequired();
    // Available supply can be negative, which breaks the test below. Fix it.
    supplyAvailable = std::max(0, supplyAvailable);

    // if we don't have enough supply, we're supply blocked
    if (supplyAvailable < supplyCost)
    {
        // If we're zerg, check to see if a building is planned to be built.
        // Only count it as releasing supply very early in the game.
        if (_selfRace == BWAPI::Races::Zerg
            && BuildingManager::Instance().buildingsQueued().size() > 0
            && the.self()->supplyTotal() <= 18)
        {
            return false;
        }
        return true;
    }

    return false;
}

// Called to refill the production queue when it is empty.
// Zerg does that.
// Alternately, call on Maker for production.
void StrategyManager::freshProductionPlan()
{
    if (_selfRace == BWAPI::Races::Zerg)
    {
        ProductionManager::Instance().setBuildOrder(StrategyBossZerg::Instance().freshProductionPlan());
    }
	else
	{
		the.maker.update();
	}
}

// Do we expect or plan to drop at some point during the game?
bool StrategyManager::dropIsPlanned() const
{
    // Otherwise plan drop if the opening says so, or if the map has islands to take.
    return
        getOpeningGroup() == "drop" ||
        Config::Macro::ExpandToIslands && the.bases.hasIslandBases();
}

#pragma once

#include "BuildOrder.h"
#include "BuildOrderQueue.h"
#include "Common.h"
#include "InformationManager.h"
#include "WorkerManager.h"

namespace UAlbertaBot
{
typedef std::pair<MacroAct, size_t> MetaPair;
typedef std::vector<MetaPair> MetaPairVector;

struct Strategy
{
    std::string _name;
    BWAPI::Race _race;
    std::string _openingGroup;
    BuildOrder  _buildOrder;

    Strategy()
        : _name("None")
        , _race(BWAPI::Races::None)
        , _openingGroup("")
    {
    }

    Strategy(const std::string & name, const BWAPI::Race & race, const std::string & openingGroup, const BuildOrder & buildOrder)
        : _name(name)
        , _race(race)
        , _openingGroup(openingGroup)
        , _buildOrder(buildOrder)
    {
    }
	
	bool getRush() const { return _buildOrder.getRush(); };
};

class StrategyBoss;		// forward declaration

class StrategyManager 
{
    StrategyManager();

	StrategyBoss *					getStrategyBoss();
	
	BWAPI::Race					    _selfRace;
    BWAPI::Race					    _enemyRace;
    std::map<std::string, Strategy> _strategies;
    const BuildOrder                _emptyBuildOrder;
    std::string						_openingGroup;
    bool							_openingStaticDefenseDropped;	// make sure we do this at most once ever
	StrategyBoss *					_strategyBoss;

    bool							detectSupplyBlock(BuildOrderQueue & queue) const;

public:
    
    static	StrategyManager &	    Instance();

            void                    addStrategy(const std::string & name, Strategy strategy);
            void					setOpeningGroup();
    const	std::string &			getOpeningGroup() const;
			void					setBuildupMode(bool buildup);
    const	BuildOrder &            getOpeningBookBuildOrder() const;

			void					update();
            void					handleUrgentProductionIssues(BuildOrderQueue & queue);
            void					freshProductionPlan();

            bool					dropIsPlanned() const;
};

}
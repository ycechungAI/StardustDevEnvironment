#pragma once

#include <Common.h>
#include "BuildOrderQueue.h"
#include "BuildingManager.h"
#include "StrategyManager.h"
#include "BuildOrder.h"

namespace UAlbertaBot
{
typedef unsigned char Action;

enum class ExtractorTrick { None, Start, ExtractorOrdered, DroneOrdered };

class ProductionManager
{
    ProductionManager();
    
    BuildOrderQueue     _queue;
	int					_lastProductionFrame;            // for detecting jams
	BWAPI::TilePosition _predictedTilePosition;
	BWAPI::Unit         _assignedWorkerForThisBuilding;
    bool                _haveLocationForThisBuilding;
	int					_delayBuildingPredictionUntilFrame;
	bool				_outOfBook;                      // production queue is beyond the opening book
	int					_targetGasAmount;                // for "go gas until <n>"; set to 0 if no target
	ExtractorTrick		_extractorTrickState;
	Building *			_extractorTrickBuilding;         // set depending on the extractor trick state
	BWAPI::UnitType		_extractorTrickUnitType;
	bool				_goAggressive;
    
    BWAPI::Unit         getClosestUnitToPosition(const BWAPI::Unitset & units, BWAPI::Position closestTo);
	BWAPI::Unit         getFarthestUnitFromPosition(const BWAPI::Unitset & units, BWAPI::Position farthest);
	BWAPI::Unit         getClosestLarvaToPosition(BWAPI::Position closestTo);
	BWAPI::Unit         getBestLarva(const BWAPI::Unitset & units, BWAPI::Position closestTo);
	BWAPI::Unit         selectUnitOfType(BWAPI::UnitType type, BWAPI::Position closestTo = BWAPI::Position(0, 0));
	
	void				executeCommand(MacroAct act);
    bool                meetsReservedResources(MacroAct type);
	void                create(BWAPI::Unit producer, const BuildOrderItem & item);
    void                manageBuildOrderQueue();
	void				maybeReorderQueue();
	bool				independentUnitType(BWAPI::UnitType type) const;
    bool                canMakeNow(BWAPI::Unit producer,MacroAct t);
    void                predictWorkerMovement(const Building & b);

	int                 getFreeMinerals();
    int                 getFreeGas();
	
	bool                canPlanBuildOrderNow() const;

	void				doExtractorTrick();

	BWAPI::Unit			getProducer(MacroAct t, BWAPI::Position closestTo = BWAPI::Positions::None);

public:

    static ProductionManager &	Instance();

    void        drawQueueInformation(std::map<BWAPI::UnitType,int> & numUnits,int x,int y,int index);
	void        setBuildOrder(const BuildOrder & buildOrder);
	void        update();
    void        onUnitMorph(BWAPI::Unit unit);
    void        onUnitDestroy(BWAPI::Unit unit);
    //void        performBuildOrderSearch();
    void        drawProductionInformation(int x,int y);
	void		startExtractorTrick();
	void        queueGasSteal();

	bool		nextIsBuilding() const;

	void		goOutOfBook();
	bool		isOutOfBook() const { return _outOfBook; };

	bool		getAggression() { return _goAggressive; };
	void		setAggression(bool aggressive)
	{
		if (aggressive && !_goAggressive)
		{
			int count = 0;
			for (const auto unit : BWAPI::Broodwar->self()->getUnits())
			{
				if (UnitUtil::IsCombatUnit(unit) && unit->isCompleted())
				{
					++count;
				}
			}

			Log().Get() << "Go aggressive with " << count << " combat units and " << UnitUtil::GetCompletedUnitCount(BWAPI::Broodwar->self()->getRace().getWorker()) << " workers";
		}

		_goAggressive = aggressive;
	}

};


class CompareWhenStarted
{

public:

    CompareWhenStarted() {}

    // the sorting operator
    bool operator() (BWAPI::Unit u1,BWAPI::Unit u2)
    {
        int startedU1 = BWAPI::Broodwar->getFrameCount() - (u1->getType().buildTime() - u1->getRemainingBuildTime());
        int startedU2 = BWAPI::Broodwar->getFrameCount() - (u2->getType().buildTime() - u2->getRemainingBuildTime());
        return startedU1 > startedU2;
    }
};

}
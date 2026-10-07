#pragma once

#include "Common.h"
#include "MacroAct.h"
#include "BuildingData.h"

namespace UAlbertaBot
{
struct BuildOrderItem
{
    MacroAct			macroAct;	// the thing we want to 'build'
	bool				isGasSteal;
	size_t				defenseLocation;

	BuildOrderItem(MacroAct m, bool gasSteal = false, size_t defenseLocation = DefenseLocation::Normal)
        : macroAct(m)
        , isGasSteal(gasSteal)
		, defenseLocation(defenseLocation)
    {
    }
};

class BuildOrderQueue
{
    std::deque< BuildOrderItem > queue;							// highest priority item is in the back
	bool modified;												// so ProductionManager can detect changes made behind its back

public:

    BuildOrderQueue();

	bool isModified() const { return modified; };
	void resetModified() { modified = false; };

    void clearAll();											// clears the entire build order queue
	// queues something at the highest priority
	void queueAsHighestPriority(MacroAct m, bool gasSteal = false, size_t defenseLocation = DefenseLocation::Normal);
	// queues something at the lowest priority
	void queueAsLowestPriority(MacroAct m, bool gasSteal = false, size_t defenseLocation = DefenseLocation::Normal);
	void queueAfterSunken(MacroAct m, bool gasSteal = false, size_t defenseLocation = DefenseLocation::Normal);
    void removeHighestPriorityItem();							// removes the highest priority item
	void doneWithHighestPriorityItem();							// remove highest priority item without setting `modified`
	void pullToTop(size_t i);									// move item at index i to the highest priority position
	void removeAll(MacroAct m);									// removes all matching meta types from queue

    size_t size() const;										// returns the size of the queue
	bool isEmpty() const;

    const BuildOrderItem & getHighestPriorityItem() const;		// returns the highest priority item
	BWAPI::UnitType getNextUnit() const;						// skip commands and return item if it's a unit
	int getNextGasCost(int n) const;							// look n ahead, return next nonzero gas cost

	bool anyInQueue(BWAPI::UnitType type) const;
	bool anyInQueue(BWAPI::UpgradeType type) const;
	bool anyInQueue(BWAPI::TechType type) const;
	bool anyInNextN(BWAPI::UnitType type, int n) const;
	bool anyInNextN(BWAPI::UpgradeType type, int n) const;
	bool anyInNextN(BWAPI::TechType type, int n) const;
	size_t numInQueue(BWAPI::UnitType type) const;
	size_t numInNextN(BWAPI::UnitType type, int n) const;
	bool combatUnitInQueue();
	bool buildingInQueue();
	bool upgradeInQueue();
	void totalCosts(int & minerals, int & gas);

	void drawQueueInformation(int x, int y, bool outOfBook);

	// overload the bracket operator for ease of use
	// queue[queue.size()-1] is the next item
	// queue[0] is the last item
    BuildOrderItem operator [] (int i);
};
}
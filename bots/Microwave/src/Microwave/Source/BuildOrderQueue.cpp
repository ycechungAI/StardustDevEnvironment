#include "BuildOrderQueue.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

BuildOrderQueue::BuildOrderQueue() 
	: modified(false)
{
}

void BuildOrderQueue::clearAll() 
{
	// clear the queue
	if (!isEmpty())
	{
		queue.clear();
		modified = true;
		//Log().Debug() << "Cleared build queue (modifying queue)";
	}
}

void BuildOrderQueue::queueAsHighestPriority(MacroAct m, bool gasSteal, size_t defenseLocation)
{
	// queue the item
	queue.push_back(BuildOrderItem(m, gasSteal, defenseLocation));
	modified = true;
	//Log().Debug() << "Queued " << m.getName() << " at top of queue (modifying queue)";
}

void BuildOrderQueue::queueAsLowestPriority(MacroAct m, bool gasSteal, size_t defenseLocation)
{
	// queue the item
	// Does not "modify" the queue for purposes of the production manager.
	queue.push_front(BuildOrderItem(m, gasSteal, defenseLocation));
	//modified = true;
	//Log().Debug() << "Queued " << m.getName() << " at bottom of queue";
}

void BuildOrderQueue::queueAfterSunken(MacroAct m, bool gasSteal, size_t defenseLocation)
{
	queueAsHighestPriority(m, gasSteal, defenseLocation);

	if (size() > 1 && queue[1].macroAct.isUnit() &&
		(queue[1].macroAct.getUnitType() == BWAPI::UnitTypes::Zerg_Spore_Colony || 
			queue[1].macroAct.getUnitType() == BWAPI::UnitTypes::Zerg_Sunken_Colony))
	{
		// BWAPI::Broodwar->printf("queued %s after spore (modifying queue)", m.getName().c_str());
		pullToTop(1);
	}
}

void BuildOrderQueue::removeHighestPriorityItem() 
{
	// remove the back element of the vector
	if (!queue.empty())
	{
		queue.pop_back();
		modified = true;
		//Log().Debug() << "Removed highest priority item (modifying queue)";
	}
}

void BuildOrderQueue::doneWithHighestPriorityItem()
{
	// Does not "modify" the queue for purposes of the production manager.
	queue.pop_back();
}

void BuildOrderQueue::pullToTop(size_t i)
{
	UAB_ASSERT(i >= 0 && i < queue.size() - 1, "bad index");

	BuildOrderItem item = queue[i];								// copy it
	queue.erase(queue.begin() + i);
	queueAsHighestPriority(item.macroAct, item.isGasSteal);		// this sets modified = true
}

const BuildOrderItem & BuildOrderQueue::getHighestPriorityItem() const
{
	UAB_ASSERT(!queue.empty(), "taking from empty queue");

	// the queue will be sorted with the highest priority at the back
	return queue.back();
}

// Return the next unit type in the queue, or None, skipping over commands.
BWAPI::UnitType BuildOrderQueue::getNextUnit() const
{
	for (int i = queue.size() - 1; i >= 0; --i)
	{
		const MacroAct & act = queue[i].macroAct;
		if (act.isUnit())
		{
			return act.getUnitType();
		}
		if (!act.isCommand())
		{
			return BWAPI::UnitTypes::None;
		}
	}

	return BWAPI::UnitTypes::None;
}

// Return the gas cost of the next item in the queue that has a nonzero gas cost.
// Look at most n items ahead in the queue.
int BuildOrderQueue::getNextGasCost(int n) const
{
	for (int i = queue.size() - 1; i >= std::max(0, int(queue.size()) - n); --i)
	{
		int price = queue[i].macroAct.gasPrice();
		if (price > 0)
		{
			return price;
		}
	}

	return 0;
}

size_t BuildOrderQueue::size() const
{
	return queue.size();
}

bool BuildOrderQueue::isEmpty() const
{
	return queue.empty();
}

bool BuildOrderQueue::anyInQueue(BWAPI::UpgradeType type) const
{
	for (auto & item : queue)
	{
		if (item.macroAct.isUpgrade() && item.macroAct.getUpgradeType() == type)
		{
			return true;
		}
	}
	return false;
}

bool BuildOrderQueue::anyInQueue(BWAPI::UnitType type) const
{
	for (auto & item : queue)
	{
		if (item.macroAct.isUnit() && item.macroAct.getUnitType() == type)
		{
			return true;
		}
	}
	return false;
}

bool BuildOrderQueue::anyInQueue(BWAPI::TechType type) const
{
	for (auto & item : queue)
	{
		if (item.macroAct.isTech() && item.macroAct.getTechType() == type)
		{
			return true;
		}
	}
	return false;
}

// Are there any of these in the next N items in the queue?
bool BuildOrderQueue::anyInNextN(BWAPI::UnitType type, int n) const
{
	for (int i = queue.size() - 1; i >= std::max(0, int(queue.size()) - 1 - n); --i)
	{
		const MacroAct & act = queue[i].macroAct;
		if (act.isUnit() && act.getUnitType() == type)
		{
			return true;
		}
	}

	return false;
}

// Are there any of these in the next N items in the queue?
bool BuildOrderQueue::anyInNextN(BWAPI::UpgradeType type, int n) const
{
	for (int i = queue.size() - 1; i >= std::max(0, int(queue.size()) - 1 - n); --i)
	{
		const MacroAct & act = queue[i].macroAct;
		if (act.isUpgrade() && act.getUpgradeType() == type)
		{
			return true;
		}
	}

	return false;
}

// Are there any of these in the next N items in the queue?
bool BuildOrderQueue::anyInNextN(BWAPI::TechType type, int n) const
{
	for (int i = queue.size() - 1; i >= std::max(0, int(queue.size()) - 1 - n); --i)
	{
		const MacroAct & act = queue[i].macroAct;
		if (act.isTech() && act.getTechType() == type)
		{
			return true;
		}
	}

	return false;
}

size_t BuildOrderQueue::numInQueue(BWAPI::UnitType type) const
{
	size_t count = 0;
	for (auto & item : queue)
	{
		if (item.macroAct.isUnit() && item.macroAct.getUnitType() == type)
		{
			++count;
		}
	}
	return count;
}

size_t BuildOrderQueue::numInNextN(BWAPI::UnitType type, int n) const
{
	size_t count = 0;

	for (int i = queue.size() - 1; i >= std::max(0, int(queue.size()) - 1 - n); --i)
	{
		const MacroAct & act = queue[i].macroAct;
		if (act.isUnit() && act.getUnitType() == type)
		{
			++count;
		}
	}

	return count;
}

bool BuildOrderQueue::combatUnitInQueue()
{
	for (auto & item : queue)
	{
		if (item.macroAct.isUnit() && UnitUtil::IsCombatUnit(item.macroAct.getUnitType()))
		{
			return true;
		}
	}
	return false;
}

bool BuildOrderQueue::buildingInQueue()
{
	for (auto & item : queue)
	{
		if (item.macroAct.isBuilding())
		{
			return true;
		}
	}
	return false;
}

bool BuildOrderQueue::upgradeInQueue()
{
	for (auto & item : queue)
	{
		if (item.macroAct.isUpgrade() || item.macroAct.isTech())
		{
			return true;
		}
	}
	return false;
}

void BuildOrderQueue::totalCosts(int & minerals, int & gas)
{
	minerals = 0;
	gas = 0;
	for (auto & item : queue)
	{
		minerals += item.macroAct.mineralPrice();
		gas += item.macroAct.gasPrice();
	}
}

void BuildOrderQueue::drawQueueInformation(int x, int y, bool outOfBook) 
{
    if (!Config::Debug::DrawProductionInfo)
    {
        return;
    }
	
	std::string prefix = "\x04";

	size_t reps = std::min(size_t(12), queue.size());
	int remaining = queue.size() - reps;
	
	// for each item in the queue
	for (size_t i(0); i<reps; i++) {

		prefix = "\x04";

		const BuildOrderItem & item = queue[queue.size() - 1 - i];
        const MacroAct & act = item.macroAct;

        if (act.isUnit())
        {
            if (act.getUnitType().isWorker())
            {
                prefix = "\x1F";
            }
            else if (act.getUnitType().supplyProvided() > 0)
            {
                prefix = "\x03";
            }
            else if (act.getUnitType().isRefinery())
            {
                prefix = "\x1E";
            }
            else if (act.isBuilding())
            {
                prefix = "\x11";
            }
            else if (act.getUnitType().groundWeapon() != BWAPI::WeaponTypes::None || act.getUnitType().airWeapon() != BWAPI::WeaponTypes::None)
            {
                prefix = "\x06";
            }
        }
		else if (act.isCommand())
		{
			prefix = "\x04";
		}

		BWAPI::Broodwar->drawTextScreen(x, y, " %s%s", prefix.c_str(), TrimRaceName(act.getName()).c_str());
		y += 10;
	}

	std::stringstream endMark;
	if (remaining > 0)
	{
		endMark << '+' << remaining << " more ";
	}
	if (!outOfBook)
	{
		endMark << "[book]";
	}
	if (!endMark.str().empty())
	{
		BWAPI::Broodwar->drawTextScreen(x, y, " %s%s", "\x04", endMark.str().c_str());
	}
}

BuildOrderItem BuildOrderQueue::operator [] (int i)
{
	return queue[i];
}

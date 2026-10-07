#include "MicroScourge.h"

#include <algorithm>

#include "InformationManager.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// -----------------------------------------------------------------------------------------

// How well can this cluster shoot down incoming scourge?
int MicroScourge::clusterDefense(const UnitCluster & c)
{
	// TODO
	return 0;
}

void MicroScourge::findTargetClusters()
{
	std::vector<TargetCluster> targetClusters;

	for (const UnitCluster & c : the.ops.getEnemyClusters())
	{
		if (c.air)
		{
			targetClusters.push_back(TargetCluster(c, clusterDefense(c)));
		}
	}
}

// --

// Scourge teams chasing a dead target go into the unassigned set.
// Does not check whether scourge on the team are dead, so dead scourge may be unassigned.
// Return whether anything was dropped.
bool MicroScourge::anyDeadTargets() const
{
	// For enemy units as the key of _scourgeTeams.
	std::function alive = [](const std::pair<BWAPI::Unit, BWAPI::Unitset> & targetPair) -> bool
		{
			return the.info.getUnitData(the.enemy()).getUnitInfo(targetPair.first) != nullptr;
		};

	return std::any_of(_scourgeTeams.begin(), _scourgeTeams.end(), alive);
}

// Have any unassigned or assigned scourge died?
bool MicroScourge::anyDeadScourge() const
{
	// For our own units as BWAPI::Unit.
	std::function alive = [](BWAPI::Unit unit) -> bool
		{
			return unit->exists();
		};

	return
		std::any_of(_unassignedScourge.begin(), _unassignedScourge.end(), alive) ||
		std::any_of(_assignedScourge.begin(), _assignedScourge.end(), alive);
}

// Have any fresh scourge spawned?
bool MicroScourge::anyNewScourge() const
{
	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (u->getType() == BWAPI::UnitTypes::Zerg_Scourge &&
			!_assignedScourge.contains(u) &&
			!_unassignedScourge.contains(u))
		{
			return true;
		}
	}

	return false;
}

// Clear scourge data and repopulate it with all unassigned scourge.
void MicroScourge::resetScourge()
{
	_scourgeTeams.clear();
	_unassignedScourge.clear();
	_assignedScourge.clear();

	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (u->getType() == BWAPI::UnitTypes::Zerg_Scourge && UnitUtil::IsValidUnit(u))
		{
			_unassignedScourge.insert(u);
		}
	}
}

int MicroScourge::getTargetPriority(const UnitInfo & ui) const
{
	return getAttackPriority(ui.type);
}

void MicroScourge::prioritizeTargets(std::priority_queue<std::pair<BWAPI::Unit, int>> & priorities)
{
	for (const auto & kv : the.info.getUnitData(the.enemy()).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isFlyer())		// ignore floating buildings, include all other air units
		{
			priorities.push(std::make_pair(ui.unit, getTargetPriority(ui)));
		}
	}
}

// TODO
void MicroScourge::assignTeamsToTargets(std::priority_queue<std::pair<BWAPI::Unit, int>> & priorities)
{
}

void MicroScourge::assignScourge()
{
	// TODO provide greater_than
	std::priority_queue<std::pair<BWAPI::Unit, int>> targetPriorities;

	prioritizeTargets(targetPriorities);
	assignTeamsToTargets(targetPriorities);
}

// TODO
void MicroScourge::microScourge()
{
}

// -----------------------------------------------------------------------------------------

MicroScourge::MicroScourge()
	: _lastAssignmentFrame(0)
{
}

void MicroScourge::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
    BWAPI::Unitset units = Intersection(getUnits(), cluster.units);
    if (units.empty())
    {
        return;
    }

    assignTargets(units, targets);
}

void MicroScourge::update()
{
	// 0. Short-circuit if we have nothing to do.
	if (the.my.completed.count(BWAPI::UnitTypes::Zerg_Scourge) == 0)
	{
		return;
	}

	// 1. We redo assignments if anything has changed.
	// NOTE This causes recalculation when attacking scourge blow up at different times, but it's OK.
	bool redo = anyDeadTargets() || anyDeadScourge() || anyNewScourge();

	// 2. Make assignments when needed or periodically.
	if (redo || the.now() > _lastAssignmentFrame + 16)
	{
		resetScourge();
		assignScourge();
		_lastAssignmentFrame = the.now();
	}

	// 3. Carry out micro.
	microScourge();
}

void MicroScourge::assignTargets(const BWAPI::Unitset & scourge, const BWAPI::Unitset & targets)
{
    // The set of potential targets.
    BWAPI::Unitset scourgeTargets;
    for (BWAPI::Unit target : targets)
    {
        if (target->getType().isFlyer() &&								// excludes floating buildings
            target->getType() != BWAPI::UnitTypes::Protoss_Interceptor &&
            target->getType() != BWAPI::UnitTypes::Zerg_Overlord &&
            !the.airHitsFixed.inRange(target->getTilePosition()))		// skip defended targets
        {
            scourgeTargets.insert(target);
        }
    }

    for (BWAPI::Unit scourgeUnit : scourge)
    {
        BWAPI::Unit target = getTarget(scourgeUnit, scourgeTargets);
        if (target)
        {
            // A target was found. Attack it.
            if (Config::Debug::DrawUnitTargets)
            {
                BWAPI::Broodwar->drawLineMap(scourgeUnit->getPosition(), scourgeUnit->getTargetPosition(), BWAPI::Colors::Blue);
            }

            the.micro.CatchAndAttackUnit(scourgeUnit, target);
        }
        else
        {
            // No target found. If we're not near the order position, go there.
            // Use Move (not AttackMove) so that we don't attack overlords and such along the way.
            if (scourgeUnit->getDistance(order->getPosition()) > 3 * 32)
            {
                the.micro.MoveNear(scourgeUnit, order->getPosition());
                if (Config::Debug::DrawUnitTargets)
                {
                    BWAPI::Broodwar->drawLineMap(scourgeUnit->getPosition(), order->getPosition(), BWAPI::Colors::Orange);
                }
            }
        }
    }
}

BWAPI::Unit MicroScourge::getTarget(BWAPI::Unit scourge, const BWAPI::Unitset & targets)
{
    int bestScore = INT_MIN;
    BWAPI::Unit bestTarget = nullptr;

    for (BWAPI::Unit target : targets)
    {
        const int priority = getAttackPriority(target->getType());	// 0..12
        const int range = scourge->getDistance(target);				// 0..map diameter in pixels

        // Let's say that 1 priority step is worth 3 tiles.
        // We care about unit-target range and target-order position distance.
        int score = 3 * 32 * priority - range;

        if (score > bestScore)
        {
            bestScore = score;
            bestTarget = target;
        }
    }

    return bestTarget;
}

int MicroScourge::getAttackPriority(BWAPI::UnitType targetType)
{
    if (targetType == BWAPI::UnitTypes::Zerg_Cocoon ||
        targetType == BWAPI::UnitTypes::Zerg_Guardian)
    {
        // Helpless and valuable.
        return 10;
    }
    if (targetType == BWAPI::UnitTypes::Terran_Science_Vessel ||
        targetType == BWAPI::UnitTypes::Terran_Valkyrie ||
        targetType == BWAPI::UnitTypes::Protoss_Carrier ||
        targetType == BWAPI::UnitTypes::Protoss_Arbiter ||
        targetType == BWAPI::UnitTypes::Zerg_Devourer)
    {
        // Capital ships that are mostly vulnerable.
        return 9;
    }
    if (targetType == BWAPI::UnitTypes::Terran_Dropship ||
        targetType == BWAPI::UnitTypes::Protoss_Shuttle ||
        targetType == BWAPI::UnitTypes::Zerg_Queen)
    {
        // Transports other than overlords, plus queens. They are important and defenseless.
        return 8;
    }
    if (targetType == BWAPI::UnitTypes::Terran_Battlecruiser ||
        targetType == BWAPI::UnitTypes::Protoss_Scout)
    {
        // Capital ships that can shoot back efficiently.
        return 7;
    }
    if (targetType == BWAPI::UnitTypes::Terran_Wraith ||
        targetType == BWAPI::UnitTypes::Protoss_Corsair ||
        targetType == BWAPI::UnitTypes::Zerg_Mutalisk)
    {
        // Lesser flyers that can shoot back.
        return 5;
    }
    if (targetType == BWAPI::UnitTypes::Protoss_Observer)
    {
        // Higher priority if we have burrow or lurker tech.
        return
            (BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Burrowing) ||
            BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect))
                ? 7
                : 5;
    }

    // Overlords, scourge, interceptors, floating buildings.
    return 0;
}

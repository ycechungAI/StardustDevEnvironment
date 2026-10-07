#include "Base.h"

#include <functional>
#include "Bases.h"
#include "BuildingManager.h"
#include "InformationManager.h"
#include "The.h"
#include "UnitUtil.h"
#include "WorkerManager.h"

using namespace UAlbertaBot;

// Is the base one of the map's starting bases?
// The only purpose of this method is to initialize the startingBase flag.
// NOTE This depends on tilePosition, so the startingBase flag must be declared after tilePosition.
bool Base::findIsStartingBase() const
{
    for (BWAPI::TilePosition tile : BWAPI::Broodwar->getStartLocations())
    {
        if (tile == tilePosition)
        {
            return true;
        }
    }
    return false;
}

// Figure out the "front" of the base where defenses against approach should go.
// This is most important for natural bases, which usually have one line of approach.
// (Defenses against raids may be elsewhere.)
// Called once at startup.
BWAPI::TilePosition Base::findFront() const
{
    // Seek a tile at the right distance from the base and as close as possible
    // to an outside reference point that represents the enemy origin.
    // That is, place the front toward the enemy.
    BWAPI::TilePosition startTile = getCenterTile();
    Base * outsideBase = nullptr;
    for (Base * base : the.bases.getStarting())
    {
        if (base != this && base->getNatural() != this &&
            base->getDistances().at(startTile) > 0)   // connected by ground
        {
            outsideBase = base;
            break;
        }
    }
    if (!outsideBase)
    {
        // Fallback method: The front is away from the minerals.
        return BWAPI::TilePosition((getCenter() - getMineralOffset()).makeValid());
    }
    const GridDistances & outsideDistances = outsideBase->getDistances();

    BWAPI::TilePosition tile = startTile;
    int inDist = 0;                                 // box distance from start tile
    int outDist = outsideDistances.at(startTile);   // tile distance from outside reference
    while (1)
    {
loop:
        for (int dx = -1; dx <= 1; ++dx)
        {
            for (int dy = -1; dy <= 1; ++dy)
            {
                BWAPI::TilePosition xy(tile.x + dx, tile.y + dy);
                if (xy.isValid() && the.map.isBuildable(xy))
                {
                    int inD = TileBoxDistance(startTile, xy);
                    int outD = outsideDistances.at(xy);
                    if (outD < outDist && inD >= inDist && inD <= 5)
                    {
                        tile = xy;
                        inDist = inD;
                        outDist = outD;
                        goto loop;
                    }
                }
            }
        }
        // We went through all the neighbors without improvement. Stop.
        break;
    }
    return tile;
}

// The mean offset of the base's mineral patches from the center of the resource depot.
// This is used to tell what direction the minerals are in.
BWAPI::Position Base::findMineralOffset() const
{
    BWAPI::Position center = getCenter();
    BWAPI::Position offset = BWAPI::Positions::Origin;
    for (BWAPI::Unit mineral : getMinerals())
    {
        offset += mineral->getInitialPosition() - center;
    }
    return BWAPI::Position(offset / getMinerals().size());
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Create a base given its position and a set of resources that may belong to it.
// The caller is responsible for eliminating resources which are too small to be worth it.
// NOTE groundSafePaths and airSafePaths are not initialized yet. They are created on demand.
Base::Base(BWAPI::TilePosition tile, const BWAPI::Unitset & availableResources)
    : id(-1)								// invalid value, will be reset after bases are sorted
    , tilePosition(tile)
    , distances(tile)
	, groundSafePaths(GridSafePathGround(tile, static_cast<const Grid &>(distances)))
	, airSafePaths(GridSafePathAir(tile))
	, startingBase(findIsStartingBase())
    , naturalBase(nullptr)
    , mainBase(nullptr)
    , front(BWAPI::Positions::Unknown)		// filled in by initializeFront() when all info is known
	, notTheEnemyStart(false)				// excluded by scouting code as not the enemy starting base
	, enemyStartEvidence(0)					// signs that it may be the enemy starting base
	, reserved(false)
	, failedPlacements(0)
	, buildingsCost(0)
	, underAttack(false)
    , overlordDanger(false)
    , workerDanger(false)
    , doomed(false)
    , resourceDepot(nullptr)
    , owner(BWAPI::Broodwar->neutral())
{
	// Calculate distances up to BaseResourceRange. Tiles beyond are initialized to -1.
    GridDistances resourceDistances(tile, BaseResourceRange, false);

    for (BWAPI::Unit resource : availableResources)
    {
        if (resource->getInitialTilePosition().isValid() && resourceDistances.getStaticUnitDistance(resource) >= 0)
        {
            if (resource->getInitialType().isMineralField())
            {
                minerals.insert(resource);
            }
            else if (resource->getInitialType() == BWAPI::UnitTypes::Resource_Vespene_Geyser)
            {
                initialGeysers.insert(resource);
            }
        }
    }
    geysers = initialGeysers;
    mineralOffset = findMineralOffset();

    // Fill in the set of blockers, destructible neutral units that are very close to the base
    // and may interfere with its operation. (Or may protect the base. Who knows?)
    // This does not include the minerals to mine!
    for (BWAPI::Unit unit : BWAPI::Broodwar->getStaticNeutralUnits())
    {
        // NOTE Khaydarin crystals are not destructible, and I don't know any way
        //      to find that out other than to check the name explicitly. Is there a way?
        if (!unit->getInitialType().canMove() &&
            !unit->isInvincible() &&
            unit->isTargetable() &&
            !unit->isFlying() &&
            unit->getInitialType().getName().find("Khaydarin") == std::string::npos)
        {
            int dist = resourceDistances.getStaticUnitDistance(unit);
            if (dist >= 0)		// and necessarily <= BaseResourceRange
            {
                blockers.insert(unit);
            }
        }
    }
}

// This is to be called exactly once at startup time.
// With the initial -1 value set in the constructor, the checks here prevent multiple calls.
void Base::setID(int baseID)
{
    UAB_ASSERT(id == -1, "BUG! base ID reset");
    UAB_ASSERT(baseID >= 1, "BUG! bad base ID");
    id = baseID;
}

// The closest non-starting base to this base is its natural (with other conditions).
// This is only called (by Bases) if this is a starting base. Other bases have no natural.
// Also tell the natural that this is its main base.
// NOTE If the map has two naturals for each main, this will pick the same one each time
//      as "the" natural. We'd need a fancier analysis.
// NOTE If the map has two mains sharing the same natural, this will give bad results.
//      No competitive map should be laid out that way.
void Base::initializeNatural(const std::vector<Base *> & bases)
{
    int minDist = MAX_DISTANCE;
    Base * bestNatural = nullptr;
    for (Base * base : bases)
    {
        if (!base->isAStartingBase() && !base->getMain() && base->getInitialMinerals() > 0)
        {
            int dist = base->getTileDistance(tilePosition);     // -1 if not connected by ground
            if (base->getInitialGas() == 0)
            {
                // If the base is mineral-only, accept it only if it is much closer.
                dist *= 2;      // -1 goes to -2; that's OK
            }
            if (dist > 0 && dist < minDist)
            {
                minDist = dist;
                bestNatural = base;
            }
        }
    }
    if (bestNatural)
    {
        naturalBase = bestNatural;
        naturalBase->mainBase = this;
    }
}

// The "front line" of the base, where static defense and mobile defenders will go
// if the base is the frontmost base.
void Base::initializeFront()
{
    front = findFront();
}

BWAPI::Player Base::getOwner() const
{
	if (this == the.bases.enemyStart() && !isExplored())
	{
		// Inferred but unscouted enemy starting base.
		return the.enemy();
	}

	return owner;
};

// The base is on an island, unconnected by ground to any starting base.
bool Base::isIsland() const
{
    for (const BWAPI::TilePosition & tile : BWAPI::Broodwar->getStartLocations())
    {
        if (tile != getTilePosition() && getTileDistance(tile) > 0)
        {
            return false;
        }
    }

    return true;
}

// The base is completed, that is, its resource depot is completed.
// For an enemy base, we rely on best information.
bool Base::isCompleted() const
{
    if (resourceDepot)
    {
        if (resourceDepot->isVisible())
        {
            return UnitUtil::IsCompletedResourceDepot(resourceDepot);
        }

        // It's not visible. That implies that the base is enemy.
        // This could give a wrong answer for a terran CC that burned down or lifted.
        UAB_ASSERT(owner == the.enemy(), "unexpected base owner");
        const UnitInfo * ui = the.info.getUnitInfo(resourceDepot);
        if (ui)
        {
            return ui->isCompleted();
        }
    }

    // No resource depot means the base is unowned or inferred to be enemy.
    // We may know it's completed if we know it's the enemy start, but there are tricky cases.
    return false;
}

bool Base::isMyBase() const
{
	return getOwner() == the.self();
}

bool Base::isMyCompletedBase() const
{
    return getOwner() == the.self() && isCompleted();
}

// An "inner base" is one that is potentially protected from ground assault by defenses at
// another base along the way.
// Any base that is not an inner base is an outer base.
// Criteria:
// - The same player must own both bases.
// - Both bases must be completed.
// - For now, the inner base is a main base that has a natural.
bool Base::isInnerBase() const
{
    return
        getNatural() &&
        getOwner() != the.neutral() &&
        getOwner() == getNatural()->getOwner() &&
        isCompleted() &&
        getNatural()->isCompleted();
}

// We own this base, and we can build here.
// The meaning is "we may want to build here now" (something other than a proxy).
bool Base::isBuildable() const
{
	if (getOwner() != the.self())
	{
		return false;
	}

	if (the.selfRace() == BWAPI::Races::Terran)
	{
		return true;
	}

	if (the.selfRace() == BWAPI::Races::Protoss)
	{
		BWAPI::Unit pylon = getPylon();
		return pylon && pylon->isCompleted();
	}

	// Zerg.
	return isCompleted();
}

// The base (whatever the owner) is infested with a bunker, cannon, or sunken.
// Sometimes used by protoss to decide whether to build a reaver.
bool Base::hasEnemyDefense() const
{
	for (const auto & kv : the.info.getUnitData(the.enemy()).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Terran_Bunker ||
			ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
			ui.type == BWAPI::UnitTypes::Zerg_Sunken_Colony)
		{
			if (isInBase(ui.lastPosition))
			{
				return true;
			}
		}
	}

	return false;
}

// Recalculate the base's set of geysers, including refineries (completed or not).
// This only works for visible geysers, so it should be called only for bases we own.
// Called to work around BWAPI behavior (maybe not strictly a bug).
void Base::updateGeysers()
{
    geysers.clear();

    for (BWAPI::Unit unit : BWAPI::Broodwar->getAllUnits())
    {
        if ((unit->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser || unit->getType().isRefinery()) &&
            unit->getPosition().isValid() &&
            unit->getDistance(getCenter()) < 320)
        {
            geysers.insert(unit);
        }
    }
}

// Return a tile near the center of the resource depot location. No tile is at the exact center.
BWAPI::TilePosition Base::getCenterTile() const
{
    return tilePosition + BWAPI::TilePosition(1, 1);
}

// Return the center of the resource depot location.
BWAPI::Position Base::getCenter() const
{
    return BWAPI::Position(tilePosition) + BWAPI::Position(64, 48);
}

// Return a tile near the middle of the mineral line.
// This assumes the mineral line is either north, south, east, or west, not in between or split.
BWAPI::TilePosition Base::getMineralLineTile() const
{
    if (abs(mineralOffset.x) > abs(mineralOffset.y))
    {
        return BWAPI::TilePosition(tilePosition.x + (mineralOffset.x > 0 ? 4 : -1), tilePosition.y);
    }

    return BWAPI::TilePosition(tilePosition.x + 1, tilePosition.y + (mineralOffset.y > 0 ? 3 : -1));
}

// Return a tile opposite the resource depot from the tile returned by getMineralLineTile().
// It's used for placing static defense sometimes.
BWAPI::TilePosition Base::getAntiMineralLineTile() const
{
	BWAPI::Position xy = BWAPI::Position(getMineralLineTile());

	v2 result = v2(xy) + (v2(getCenter()) - v2(xy)).normalize() * (7.5 * 32.0);
	return BWAPI::TilePosition(BWAPI::Position(result));
}

int Base::getAirTileDistance(const BWAPI::TilePosition & pos) const
{
	return 32 * getAirDistance(pos);
}

int Base::getAirTileDistance(const BWAPI::Position & pos) const
{
	return 32 * getAirDistance(pos);
}

int Base::getAirDistance(const BWAPI::TilePosition & pos) const
{
	return TileCenter(getTilePosition()).getApproxDistance(TileCenter(pos));
}

int Base::getAirDistance(const BWAPI::Position & pos) const
{
	return TileCenter(getTilePosition()).getApproxDistance(pos);
}

// The depot may be null. (That's why player is a separate argument, not depot->getPlayer().)
// A null depot for an enemy-owned base means that the base is inferred and hasn't been seen.
void Base::setOwner(BWAPI::Unit depot, BWAPI::Player player)
{
    resourceDepot = depot;
    owner = player;
    reserved = false;

    if (player != the.self() || depot == nullptr)
    {
        overlordDanger = false;
        workerDanger = false;
        doomed = false;
    }
}

// The resource depot of this base has not been seen, but we think it's enemy owned.
void Base::setInferredEnemyBase()
{
    if (owner == BWAPI::Broodwar->neutral())
    {
        setOwner(nullptr, the.enemy());
    }
}

// Return the set of mineral patches that have minerals.
// For bases we can see, the result is up to date.
const BWAPI::Unitset Base::getRemainingMinerals() const
{
    BWAPI::Unitset mins;

    for (BWAPI::Unit min : minerals)
    {
		if (the.info.getResourceAmount(min) > 0)
		{
            mins.insert(min);
        }
    }

    return mins;
}

// Return the set of mineral patches that have minerals AND
// are not in range of an enemy immobile unit (static defense, sieged tank, lurker).
// For bases we can see, the result is up to date.
const BWAPI::Unitset Base::getRemainingSafeMinerals() const
{
	BWAPI::Unitset mins;

	for (BWAPI::Unit min : minerals)
	{
		if (the.info.getResourceAmount(min) > 0 && the.groundHitsFixed.safeToVisit(min))
		{
			mins.insert(min);
		}
	}

	return mins;
}

// The total remaining minerals at the base, as of last report.
// For bases we can see, the result is up to date.
int Base::getLastKnownMinerals() const
{
    int total = 0;

    for (BWAPI::Unit min : minerals)
    {
        total += InformationManager::Instance().getResourceAmount(min);
    }

    return total;
}

// The total remaining gas at the base, as of last report.
// For bases we can see, the result is up to date.
int Base::getLastKnownGas() const
{
    int total = 0;

    for (BWAPI::Unit gas : initialGeysers)
    {
        total += InformationManager::Instance().getResourceAmount(gas);
    }

    return total;
}

// The total initial minerals at the base.
int Base::getInitialMinerals() const
{
    int total = 0;
    for (const BWAPI::Unit min : minerals)
    {
        total += min->getInitialResources();
    }
    return total;
}

// The total initial gas at the base.
int Base::getInitialGas() const
{
    int total = 0;
    for (const BWAPI::Unit gas : initialGeysers)
    {
        total += gas->getInitialResources();
    }
    return total;
}

// How many mineral workers to saturate the mineral line?
int Base::getMaxMineralWorkers() const
{
    return int(Config::Macro::WorkersPerPatch * minerals.size());
}

// How many workers to saturate the base?
// NOTE This doesn't account for mineral patches mining out, decreasing the maximum.
int Base::getMaxWorkers() const
{
    return getMaxMineralWorkers() + Config::Macro::WorkersPerRefinery * geysers.size();
}

// How many workers are assigned to minerals?
int Base::getNumMineralWorkers() const
{
    return WorkerManager::Instance().getNumWorkers(resourceDepot);;\
}

// How many workers are assigned to minerals or gas?
// NOTE Some may be in transit and still far away.
int Base::getNumWorkers() const
{
    // The number of assigned mineral workers.
    int nWorkers = WorkerManager::Instance().getNumWorkers(resourceDepot);

    // Add the assigned gas workers.
    for (BWAPI::Unit geyser : geysers)
    {
        nWorkers += WorkerManager::Instance().getNumWorkers(geyser);
    }

    return nWorkers;
}

// For one of our bases, return the number of our units of the given type there.
// Specifically, the number within a fixed (shortish) distance of the base center.
// Include buildings that are in the building queue, not yet started, anywhere in the base.
// It's used for counting static defense buildings, and can be used for other purposes.
int Base::getNumUnits(BWAPI::UnitType type) const
{
    BWAPI::Unitset units = BWAPI::Broodwar->getUnitsInRadius(
        getCenter(),
        8 * 32,
        BWAPI::Filter::GetType == type && BWAPI::Filter::IsOwned
    );

	if (type.isBuilding())
	{
		return int(units.size() + BuildingManager::Instance().getNumUnstarted(type, this));
	}
	return int(units.size());

}

// Count enemy buildings in the base, leaving out static defense.
int Base::getNumEnemyNondefenseBuildings() const
{
	int count = 0;
	for (const auto & kv : the.info.getUnitData(the.enemy()).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isBuilding() &&
			isInBase(BWAPI::TilePosition(ui.lastPosition)) &&
			!(UnitUtil::IsStaticDefense(ui.type) || ui.type == BWAPI::UnitTypes::Zerg_Creep_Colony))
		{
			++count;
		}
	}
	return count;
}

// Return whether a given tile is "in the base". It's an approximation.
bool Base::isInBase(const BWAPI::TilePosition & tile) const
{
	// In the same zone and close enough.
	return
		tile.isValid() &&
		the.zone.at(getTilePosition()) == the.zone.at(tile) &&
		getPosition().getDistance(BWAPI::Position(tile)) < the.bases.BaseRadius;
}

// Return a pylon near the nexus, if any. Let's call 256 pixels "near".
// This only makes a difference for protoss, of course.
// NOTE The pylon may not be complete!
BWAPI::Unit Base::getPylon() const
{
    return BWAPI::Broodwar->getClosestUnit(
        getCenter(),
        BWAPI::Filter::IsOwned && BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Pylon,
        8 * 32);
}

// We're scouting in the early game. Have we seen whether the enemy base is here?
// To turn the scout around as early as possible if the base is empty, we check
// each corner of the resource depot spot.
// Any code that verifies that the enemy is not here can set notTheEnemyStart.
bool Base::isExplored() const
{
    return
        BWAPI::Broodwar->isExplored(tilePosition) ||
        BWAPI::Broodwar->isExplored(tilePosition + BWAPI::TilePosition(3, 2)) ||
        BWAPI::Broodwar->isExplored(tilePosition + BWAPI::TilePosition(0, 2)) ||
        BWAPI::Broodwar->isExplored(tilePosition + BWAPI::TilePosition(3, 0)) ||
		notTheEnemyStart;
}

// Should we be able to see the resource depot at this base?
// Yes if we can see any corner of its position.
// This is for checking whether an expected enemy base is missing.
bool Base::isVisible() const
{
    return
        BWAPI::Broodwar->isVisible(tilePosition) ||
        BWAPI::Broodwar->isVisible(tilePosition + BWAPI::TilePosition(3, 2)) ||
        BWAPI::Broodwar->isVisible(tilePosition + BWAPI::TilePosition(0, 2)) ||
        BWAPI::Broodwar->isVisible(tilePosition + BWAPI::TilePosition(3, 0));
}

// True if dangerous enemy ground forces are close enough to menace the base.
// They don't have to be attacking, just near enough to be a little scary.
// NOTE The check is crude. It only looks at the nearest enemy cluster.
bool Base::bewareEnemyGroundAttackers() const
{
	if (isDoomed())
	{
		return true;
	}

	// DPF = damage per frame. Two zealots are about 0.7 DPF.
	std::function p = [](const UnitCluster & c) -> bool { return !c.air && c.groundDPF > 0.5; };

	const UnitCluster * cluster = the.ops.getNearestEnemyClusterSuchThat(
		getPosition(),
		p
	);

	// Time to build creep colony and morph to sunken is 600 frames.
	if (cluster)
	{
		// The cluster center may be on unwalkable ground. Handle it.
		int dist = std::max(getPosition().getApproxDistance(cluster->center), 32 * distances.at(cluster->center));
		return cluster->timeToTravel(dist) < 600 + 5 * 24;
	}
	return false;
}

void Base::clearBlocker(BWAPI::Unit blocker)
{
    blockers.erase(blocker);
}

// Count the cost of all our completed buildings in the base, minerals + gas.
// It includes floating buildings.
// The info is used for defense-related decisions.
void Base::updateBuildingsCost()
{
	int cost = 0;
	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (u->getType().isBuilding() && u->isCompleted() && isInBase(u->getTilePosition()))
		{
			cost += u->getType().mineralPrice() + u->getType().gasPrice();
		}
	}
	buildingsCost = cost;
}

// Called for Config::DrawMapInfo.
void Base::drawBaseInfo() const
{
    BWAPI::Position center = getCenter();
    if (getNatural())
    {
        BWAPI::Position natural = getNatural()->getCenter();
        BWAPI::Broodwar->drawLineMap(center, natural, BWAPI::Colors::Grey);
        BWAPI::Broodwar->drawCircleMap(natural, 64, BWAPI::Colors::Grey);
    }
    BWAPI::Color color = this == the.bases.myFront() ? BWAPI::Colors::Red : BWAPI::Colors::Blue;
    BWAPI::Broodwar->drawCircleMap(getFront(), 32, color);
    BWAPI::Broodwar->drawLineMap(center, getFront(), color);

    BWAPI::Position offset(-16, -6);
    for (BWAPI::Unit min : minerals)
    {
        BWAPI::Broodwar->drawTextMap(min->getInitialPosition() + offset, "%c%d", cyan, id);
        // BWAPI::Broodwar->drawTextMap(min->getInitialPosition() + BWAPI::Position(-18, 4), "%c%d", yellow, d.getStaticUnitDistance(min));
    }
    for (BWAPI::Unit gas : geysers)
    {
        BWAPI::Broodwar->drawTextMap(gas->getInitialPosition() + offset, "%cgas %d", cyan, id);
        // BWAPI::Broodwar->drawTextMap(gas->getInitialPosition() + BWAPI::Position(-18, 4), "%cgas %d", yellow, d.getStaticUnitDistance(gas));
    }
    for (BWAPI::Unit blocker : blockers)
    {
        BWAPI::Position pos = blocker->getInitialPosition();
        BWAPI::UnitType type = blocker->getInitialType();
        BWAPI::Broodwar->drawBoxMap(
            pos - BWAPI::Position(type.dimensionLeft(), type.dimensionUp()),
            pos + BWAPI::Position(type.dimensionRight(), type.dimensionDown()),
            BWAPI::Colors::Red);
    }

    BWAPI::Broodwar->drawBoxMap(
        BWAPI::Position(tilePosition),
        BWAPI::Position(tilePosition + BWAPI::TilePosition(4, 3)),
        BWAPI::Colors::Cyan, false);

	int dx = 35;
    int dy = 40;

	BWAPI::Broodwar->drawTextMap(BWAPI::Position(tilePosition) + BWAPI::Position(dx, dy),
        "%c%d @ (%d,%d)",
        cyan, id, tilePosition.x, tilePosition.y);

    if (getOwner() != BWAPI::Broodwar->neutral())
    {
        dy += 12;
        color = green;
        std::string ownerString = "mine";
		if (owner == BWAPI::Broodwar->self())
		{
			if (this == the.bases.myMain())
			{
				ownerString = "current main";
			}
		}
		else
        {
            color = orange;
            ownerString = "yours";
        }
        BWAPI::Broodwar->drawTextMap(BWAPI::Position(tilePosition) + BWAPI::Position(dx, dy),
            "%c%s", color, ownerString.c_str());
    }

	if (buildingsCost > 0)
	{
		dy += 12;
		BWAPI::Broodwar->drawTextMap(BWAPI::Position(tilePosition) + BWAPI::Position(dx, dy),
			"%cbuilding cost %c%d", gray, green, buildingsCost);
	}

	if (getOwner() == the.self())
	{
		dy += 12;
		bool beware = bewareEnemyGroundAttackers();
		if (isDoomed() || beware || inOverlordDanger() || inWorkerDanger())
		{
			std::string doom = isDoomed() ? "DOOMED" : (beware ? "beware" : "");
			std::string ovyDanger = inOverlordDanger() ? "air danger!" : "";
			std::string workDanger = inWorkerDanger() ? "worker danger!" : "";

			BWAPI::Broodwar->drawTextMap(BWAPI::Position(tilePosition) + BWAPI::Position(dx, dy),
				"%c%s %c%s %s", orange, doom.c_str(), yellow, ovyDanger.c_str(), workDanger.c_str());
		}
		else
		{
			BWAPI::Broodwar->drawTextMap(BWAPI::Position(tilePosition) + BWAPI::Position(dx, dy),
				"%csafe for now", green);
		}
	}

    if (blockers.size() > 0)
    {
        dy += 12;
        BWAPI::Broodwar->drawTextMap(BWAPI::Position(tilePosition) + BWAPI::Position(dx, dy),
            "%cblockers: %c%d",
            red, cyan, blockers.size());
    }
}

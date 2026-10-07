#include "Bases.h"

#include "BuildingManager.h"
#include "MapTools.h"
#include "InformationManager.h"
#include "The.h"

using namespace UAlbertaBot;

// These numbers are in tiles.
const int BaseResourceRange = 22;   // max distance of one resource from another
const int BasePositionRange = 15;   // max distance of the base location from the start point
const int DepotTileWidth = 4;
const int DepotTileHeight = 3;

// Each base much meet at least one of these minimum limits to be worth keeping.
const int MinTotalMinerals = 500;
const int MinTotalGas = 500;

// Most initialization happens in initialize() below.
Bases::Bases()
    // These are set to their final values in initialize().
    : startingBase(nullptr)
    , mainBase(nullptr)
    , naturalBase(nullptr)
	, frontBase(nullptr)
	, enemyStartingBase(nullptr)
	, enemyStartingBaseBestGuess(nullptr)
    , islandStart(false)
    , islandBases(false)
	, doomedBaseCancelTime(0)
{
}

// For sorting bases in lexicographic order by y, x coordinates.
// No two bases are in the same location, so there are no ties.
bool compareBases(const Base * a, const Base * b)
{
    return
        a->getTilePosition().y < b->getTilePosition().y ||
        a->getTilePosition().y == b->getTilePosition().y && a->getTilePosition().x < b->getTilePosition().x;
}

// Give each base its ID number.
// NOTE We write base IDs into opponent model files, so base IDs for a map need to be
//      stable across runs. That's why we sort them first.
void Bases::setBaseIDs()
{
    std::sort(bases.begin(), bases.end(), compareBases);

    // Set the IDs.
    int id = 1;
    for (Base * base : bases)
    {
        base->setID(id);
        ++id;
    }
}

// Check whether we are starting on an island (or semi-island).
// Returns true if no other start position is reachable from ours.
bool Bases::checkIslandStart() const
{
	for (Base * base : startingBases)
	{
		if (base != startingBase && connectedToStart(base->getTilePosition()))
		{
			return false;
		}
	}

	return true;
}

// Check whether the map has islands at all.
bool Bases::checkIslandBases() const
{
    for (Base * base : bases)
    {
        if (!connectedToStart(base->getTilePosition()))
        {
            return true;
        }
    }
    return false;
}

// Each base finds and remembers a set of blockers, neutral units that should be destroyed
// to make best use of the base. Here we create the reverse map blockers -> bases.
void Bases::rememberBaseBlockers()
{
    for (Base * base : bases)
    {
        for (BWAPI::Unit blocker : base->getBlockers())
        {
            baseBlockers[blocker] = base;
        }
    }
}

// Given a set of resources, remove any that are used in the base.
void Bases::removeUsedResources(BWAPI::Unitset & resources, const Base * base) const
{
    UAB_ASSERT(base, "bad base");
    for (BWAPI::Unit mins : base->getMinerals())
    {
        resources.erase(mins);
    }
    for (BWAPI::Unit gas : base->getGeysers())
    {
        resources.erase(gas);
    }
}

// Count the initial minerals and gas available from a resource.
void Bases::countResources(BWAPI::Unit resource, int & minerals, int & gas) const
{
    if (resource->getType().isMineralField())
    {
        minerals += resource->getInitialResources();
    }
    else if (resource->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser)
    {
        gas += resource->getInitialResources();
    }
}

// Given a set of resources (mineral patches and geysers), find a base position nearby if possible.
// This identifies the spot for the resource depot.
// Return BWAPI::TilePositions::Invalid if no acceptable place is found.
// NOTE The found location affects the eventual ID the base is given, and base IDs must be stable
//      across runs because they are written into opponent model files. So a change that affects
//      where bases are placed may invalidate learned data.
BWAPI::TilePosition Bases::findBasePosition(BWAPI::Unitset resources)
{
    UAB_ASSERT(resources.size() > 0, "no resources");

    // Find the bounding box of the resources.
    int left = MAX_DISTANCE;
    int right = 0;
    int top = MAX_DISTANCE;
    int bottom = 0;
    for (BWAPI::Unit resource : resources)
    {
        left = std::min(left, resource->getLeft());
        right = std::max(right, resource->getRight());
        top = std::min(top, resource->getTop());
        bottom = std::max(bottom, resource->getBottom());
    }

    // The geometric center of the bounding box will be our starting point for placing the resource depot.
    BWAPI::TilePosition centerOfResources(BWAPI::Position(left + (right - left) / 2, top + (bottom - top) / 2));

    potentialBases.push_back(PotentialBase(left, right, top, bottom, centerOfResources));

    GridDistances distances(centerOfResources, BasePositionRange, false);

    int bestScore = INT_MAX;               // smallest is best
    BWAPI::TilePosition bestTile = BWAPI::TilePositions::Invalid;

    for (BWAPI::TilePosition tile : distances.getSortedTiles())
    {
        // NOTE Every resource depot is the same size, 4x3 tiles.
        if (the.map.isBuildable(tile, BWAPI::UnitTypes::Protoss_Nexus))    // TODO deprecated call
        {
            int score = baseLocationScore(tile, resources);
            if (score < bestScore)
            {
                bestScore = score;
                bestTile = tile;
            }
        }
    }

    return bestTile;
}

int Bases::baseLocationScore(const BWAPI::TilePosition & tile, BWAPI::Unitset resources) const
{
    int score = 0;

    for (const BWAPI::Unit resource : resources)
    {
        score += tilesBetweenBoxes
            ( tile
            , tile + BWAPI::TilePosition(DepotTileWidth, DepotTileHeight)
            , resource->getInitialTilePosition()
            , resource->getInitialTilePosition() + BWAPI::TilePosition(2, 2)
            );
    }

    return score;
}

// One-dimensional edge-to-edge distance between two tile rectangles.
// Used to figure out distance from a resource depot location to a resource.
int Bases::tilesBetweenBoxes
    ( const BWAPI::TilePosition & topLeftA
    , const BWAPI::TilePosition & bottomRightA
    , const BWAPI::TilePosition & topLeftB
    , const BWAPI::TilePosition & bottomRightB
    ) const
{
    int leftDist = 0;
    int rightDist = 0;

    if (topLeftB.x >= bottomRightA.x)
    {
        leftDist = topLeftB.x - bottomRightA.x;
    }
    else if (topLeftA.x >= bottomRightB.x)
    {
        leftDist = topLeftA.x - bottomRightB.x;
    }

    if (topLeftA.y >= bottomRightB.y)
    {
        rightDist = topLeftA.y - bottomRightB.y;
    }
    else if (topLeftB.y >= bottomRightA.y)
    {
        rightDist = topLeftB.y - bottomRightA.y;
    }

    UAB_ASSERT(leftDist >= 0 && rightDist >= 0, "bad tile distance");
    return std::max(leftDist, rightDist);
}

// The two possible base positions are close enough together
// that we can say they are "the same place" as a base.
bool Bases::closeEnough(BWAPI::TilePosition a, BWAPI::TilePosition b) const
{
    return abs(a.x - b.x) <= 8 && abs(a.y - b.y) <= 8;
}

// If the opponent is zerg and it's early in the game, when we see an overlord
// we may be able to exclude enemy start locations that it could not have reached yet.
// It's common to exclude all but the real enemy starting base, if we're early enough
// in the game.
bool Bases::inferEnemyStartFromOverlord()
{
    const int now = the.now();

    if (the.enemy()->getRace() != BWAPI::Races::Zerg || now > 5 * 60 * 24)
    {
        return false;
    }

	for (BWAPI::Unit unit : the.enemy()->getUnits())
	{
		if (unit->getType() != BWAPI::UnitTypes::Zerg_Overlord)
		{
			continue;
		}

		// Exclude bases that the overlord could not have started from.
		for (Base * base : startingBases)
		{
			if (base->isExplored())
			{
				// We've already excluded this base, one way or another.
				continue;
			}

			// Assume the overlord came from this base.
			// Where did the overlord start from? It starts offset from the hatchery in a specific way.
			BWAPI::Position overlordStartPos;
			overlordStartPos.x = base->getPosition().x + ((base->getPosition().x < 32 * BWAPI::Broodwar->mapWidth() / 2) ? +99 : -99);
			overlordStartPos.y = base->getPosition().y + ((base->getPosition().y < 32 * BWAPI::Broodwar->mapHeight() / 2) ? +65 : -65);

			// How far could it have traveled from there?
			double maxDistance = double(now) * (BWAPI::UnitTypes::Zerg_Overlord).topSpeed();
			if (maxDistance < double(unit->getDistance(overlordStartPos)))
			{
				// It's too far away from this base. It could not have traveled so far.
				base->setNotTheEnemyStart();

				if (Config::Debug::DrawScoutInfo)
				{
					BWAPI::Broodwar->printf("enemy start: exclude base %d by overlord sighting", base->getID());
				}

			}
		}
	}
    return false;
}

// Noticing creep can let us find the enemy starting base earlier.
// It can save whole seconds on an overlord spotting the enemy base.
// NOTE This could return a false positive on a map with neutral creep very close to the base location,
//      which can't be told apart from creep generated by the base.
//      No competitive map has that.
bool Bases::inferEnemyStartFromCreep()
{
	if (the.enemyRace() != BWAPI::Races::Zerg && the.enemyRace() != BWAPI::Races::Unknown)
	{
		return false;
	}

	for (BWAPI::TilePosition creepTile : the.creep.getNewCreep())
	{
		Base * base = the.bases.findBaseByPosition(creepTile);
		if (base &&
			!base->isExplored() &&
			base->isAStartingBase() &&
			base->getCenter().getApproxDistance(TileCenter(creepTile)) < 10 * 32)	// close enough for creep to reach (horizontally)
		{
			enemyStartingBase = base;
			enemyStartingBase->setInferredEnemyBase();
			// NOTE If the enemy is Unknown, we don't change that. We'll find out soon enough.

			if (Config::Debug::DrawScoutInfo)
			{
				BWAPI::Broodwar->printf("enemy start: creep sighted");
			}
			return true;
		}
	}
	return false;
}

// We haven't found the enemy starting base. If the enemy is zerg,
// look for bases that can be excluded because we've seen where the creep would be.
// This is a much bigger speedup than noticing where the creep actually is.
// If there's only one remaining unexplored enemy base after this, the caller
// will figure out that the enemy must be there.
void Bases::inferEnemyAbsenceFromLackOfCreep()
{
	if (the.enemyRace() != BWAPI::Races::Zerg)
	{
		return;
	}

	for (Base * base : startingBases)
	{
		if (!base->isExplored() && the.creep.missingCreep(base))
		{
			base->setNotTheEnemyStart();

			if (Config::Debug::DrawScoutInfo)
			{
				BWAPI::Broodwar->printf("enemy start: no creep at base %d", base->getID());
			}
		}
	}
}

// If we don't know the enemy's starting base, check to see whether we have just found it.
// This relies on a heuristic: If we see any enemy building in a starting base region,
// we assume that that must be the true enemy starting base.
// False positives are extremely rare, and will be corrected later anyway.
void Bases::updateEnemyStart()
{
    if (enemyStartingBase)
    {
        // The enemy is already found.
		enemyStartingBaseBestGuess = nullptr;
        return;
    }

    // Call only when enemyStartingBase is unknown (null).
    if (inferEnemyStartFromOverlord())
    {
        // We were able to deduce the enemy's location by seeing an overlord.
        return;
    }

	// If the enemy is or might be zerg, creep could tell us where the base is.
	if (inferEnemyStartFromCreep())
	{
		return;
	}

	// If the enemy is zerg, we can exclude a base where we don't see creep, saving time.
	inferEnemyAbsenceFromLackOfCreep();

    size_t nExplored = 0;
    Base * unexploredBase = nullptr;

    for (Base * start : startingBases)
    {
        // An enemy proxy at our base is not a sign of the enemy base.
        if (start != startingBase)
        {
            int startZone = the.zone.at(start->getTilePosition());
            if (startZone > 0)
            {
                // This runs every frame, so it only needs to consider visible enemies.
                for (BWAPI::Unit enemy : the.enemy()->getUnits())
                {
                    if (enemy->getType().isBuilding() &&
						!enemy->isLifted() &&
						start->isInBase(enemy->getTilePosition()))
                    {
                        if (Config::Debug::DrawScoutInfo)
                        {
                            BWAPI::Broodwar->printf("enemy start: Enemy base seen");
                        }

                        enemyStartingBase = start;
                        enemyStartingBase->setInferredEnemyBase();
                        return;
                    }
                }
            }
        }
        if (start->isExplored())
        {
            ++nExplored;
        }
        else
        {
            unexploredBase = start;
        }
    }

    // Only one base is unexplored, so the enemy must be there.
    if (nExplored + 1 == startingBases.size())
    {
        if (Config::Debug::DrawScoutInfo)
        {
            BWAPI::Broodwar->printf("Enemy base found by elimination");
        }

        enemyStartingBase = unexploredBase;
        enemyStartingBase->setInferredEnemyBase();
		return;
    }

	// The enemy starting base is not known.
	// Collect data to see if we can guess where it likely is: Near enemy stuff that we've seen.
	// This means that, e.g. we don't destroy the enemy natural and then go haring out across
	// the map to find the enemy start. We can already guess.
	for (BWAPI::Unit enemy : the.enemy()->getUnits())
	{
		for (Base * start : startingBases)
		{
			if (!start->isExplored())
			{
				if (start->isInBase(enemy))
				{
					start->addEnemyStartEvidence(4);
				}
				else if (start->getNatural() && start->getNatural()->isInBase(enemy))
				{
					start->getNatural()->addEnemyStartEvidence(enemy->getType().isBuilding() ? 2 : 1);
				}
			}
		}
	}

	int bestEvidence = 0;
	for (Base * start : startingBases)
	{
		if (!start->isExplored())
		{
			if (start->getEnemyStartEvidence() > bestEvidence)
			{
				bestEvidence = start->getEnemyStartEvidence();
				enemyStartingBaseBestGuess = start;
			}
		}
	}
}

void Bases::updateBaseOwners()
{
    for (Base * base : bases)
    {
        if (base->isVisible())
        {
            // Find the units that overlap the rectangle.
            BWAPI::Unitset units = BWAPI::Broodwar->getUnitsInRectangle(
                BWAPI::Position(base->getTilePosition()),
                BWAPI::Position(base->getTilePosition() + BWAPI::TilePosition(3, 2))
                );
            // Is one of the units a resource depot?
            BWAPI::Unit depot = nullptr;
            for (BWAPI::Unit unit : units)
            {
                if (unit->getType().isResourceDepot() && !unit->isLifted())
                {
                    depot = unit;
                    break;
                }
            }
            if (depot)
            {
                // The base is occupied.
                // NOTE The player might be the neutral player. That's OK.
                base->setOwner(depot, depot->getPlayer());
            }
            else
            {
                // The base is empty.
                base->setOwner(nullptr, the.neutral());
            }
        }
        else
        {
            // The base is out of sight. It's definitely not our base.
            if (base->getOwner() == the.self())
            {
                base->setOwner(nullptr, the.neutral());
            }
        }
    }
}

// Look for a new main base.
// The starting base is the base we began at. Once set, it never changes.
// The main base is initially the starting base.
// We change it if the main base is lost or doomed (expected to be lost).
// By decree, we always have a main base, even if it is unoccupied. It simplifies the code.
void Bases::changeMainBase()
{
	// Choose a base we own which is as far away from the old main as possible.
	// Maybe that will be safer.
	int newMainDist = 0;
	Base * newMain = nullptr;

	// Step 1: Avoid doomed bases.
	for (Base * base : bases)
	{
		if (base->owner == the.self() && !base->isDoomed())
		{
			int dist = base->getPosition().getApproxDistance(mainBase->getPosition());
			if (dist > newMainDist)
			{
				newMainDist = dist;
				newMain = base;
			}
		}
	}

	if (!newMain)
	{
		// Step 2: It looks like all our bases are doomed. Pick one anyway.
		for (Base * base : bases)
		{
			if (base->owner == the.self())
			{
				int dist = base->getPosition().getApproxDistance(mainBase->getPosition());
				if (dist > newMainDist)
				{
					newMainDist = dist;
					newMain = base;
				}
			}
		}
	}

	// Step 3. Set the new main.
	if (newMain)
	{
		mainBase = newMain;
		// If we changed mains to a new starting base and our original starting base is gone,
		// declare the new natural base.
		if (newMain->getNatural() && startingBase->getOwner() != the.self())
		{
			naturalBase = newMain->getNatural();
		}
		setFrontBase();
	}

	// If we didn't find a new main base, we're in deep trouble. We may as well keep the old one.
}

// Is the main or the natural the "front" base to defend the approaches?
// frontBase is not necessarily the base that myFront() returns:
// It's only the current front base if we own it.
// This must be called every time the main base changes.
void Bases::setFrontBase()
{
	frontBase = mainBase;

	// This makes no sense on an island map.
	if (isIslandStart())
	{
		return;
	}

	// Is the natural in front of the main? Pick any different start and check distances.
	for (Base * b : startingBases)
	{
		if (b != mainBase)
		{
			int dM = b->getTileDistance(mainBase->getCenterTile());
			int dN = b->getTileDistance(naturalBase->getCenterTile());
			UAB_ASSERT(dM > 0 && dN > 0, "bad distances");
			if (dN < dM)
			{
				frontBase = naturalBase;
			}
			break;
		}
	}
}

void Bases::updateSmallMinerals()
{
    if (the.now() % 27 == 0)
    {
        for (auto it = smallMinerals.begin(); it != smallMinerals.end(); )
        {
            if (the.info.isMineralDestroyed(*it))
            {
                it = smallMinerals.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Find the bases on the map at the beginning of the game.
void Bases::initialize()
{
    // Find the resources to mine: Mineral patches and geysers.
    BWAPI::Unitset resources;

    for (BWAPI::Unit unit : BWAPI::Broodwar->getStaticMinerals())
    {
        // Skip mineral patches with negligible resources. Bases don't belong there.
        if (unit->getInitialResources() > 64)
        {
            resources.insert(unit);
        }
        else
        {
            smallMinerals.insert(unit);
        }
    }
    for (BWAPI::Unit unit : BWAPI::Broodwar->getStaticGeysers())
    {
        if (unit->getInitialResources() > 0)
        {
            resources.insert(unit);
        }
    }

    // Add the starting bases.
    // Remove their resources from the list.
    for (BWAPI::TilePosition pos : BWAPI::Broodwar->getStartLocations())
    {
		if (!pos.isValid())		// has never happened, but guard against bad maps
		{
			continue;
		}

        Base * base = new Base(pos, resources);
        if (base->getMinerals().empty())
        {
            // The base has no minerals. It's probably an observer slot, not a real base.
            delete base;
            continue;
        }
        bases.push_back(base);
        startingBases.push_back(base);
        removeUsedResources(resources, base);

        if (pos == the.self()->getStartLocation())
        {
            startingBase = base;
            mainBase = base;
			// NOTE setFrontBase() is called below. It needs data that isn't there yet.
        }
    }

    // Add the remaining bases.
    size_t priorResourceSize = resources.size();
    while (resources.size() > 0)
    {
        // 1. Choose a resource, any resource. We'll use ground distances from it.
        BWAPI::Unit startResource = *(resources.begin());
        GridDistances fromStart(startResource->getInitialTilePosition(), BaseResourceRange, false);

        // 2. Form a group of nearby resources that are candidates for placing a base near.
        //    Keep track of how much stuff gets included in the group.
        BWAPI::Unitset resourceGroup;
        int totalMins = 0;
        int totalGas = 0;
        for (BWAPI::Unit otherResource : resources)
        {
            const int dist = fromStart.getStaticUnitDistance(otherResource);
            // All distances are <= baseResourceRange, because we limited the distance map to that distance.
            // dist == -1 means inaccessible by ground.
            if (dist >= 0)
            {
                resourceGroup.insert(otherResource);
                countResources(otherResource, totalMins, totalGas);
            }
        }

        UAB_ASSERT(resourceGroup.size() > 0, "no resources in group");

        // 3. If the base is worth making and can be made, make it.
        //    If not, remove all the resources: They are all useless.
        BWAPI::TilePosition basePosition = BWAPI::TilePositions::Invalid;
        if (totalMins >= MinTotalMinerals || totalGas >= MinTotalGas)
        {
            basePosition = findBasePosition(resourceGroup);
        }

        Base * base = nullptr;

        if (basePosition.isValid())
        {
            // Make the base. The data structure lifetime is normally the program lifetime.
            base = new Base(basePosition, resources);

            // Check whether the base actually grabbed enough resources to be useful.
            if (base->getInitialMinerals() >= MinTotalMinerals || base->getInitialGas() >= MinTotalGas)
            {
                removeUsedResources(resources, base);
                bases.push_back(base);
            }
            else
            {
                // No good. Drop the base after all.
                // This can happen when resources exist but are (or seem) inaccessible.
                delete base;
                base = nullptr;
            }
        }

        if (!base)
        {
            // It's not possible to make the base, or not worth it.
            // All resources in the group should be considered "used up" or "assigned to nothing".
            nonbases.push_back(resourceGroup);
            for (BWAPI::Unit resource : resourceGroup)
            {
                resources.erase(resource);
            }
        }

        // Error check. This should never happen.
        if (resources.size() >= priorResourceSize)
        {
            BWAPI::Broodwar->printf("failed to remove any resources");
            break;
        }
        priorResourceSize = resources.size();
    }

    // Fill in other map properties we want to remember.
    setBaseIDs();
    islandStart = checkIslandStart();       // we start on an island
    islandBases = checkIslandBases();       // the map includes islands
    rememberBaseBlockers();

    // Pick a natural for each starting base.
	// It can be null on nonstandard maps.
    // Other bases have null for the natural pointer.
    for (Base * start : startingBases)
    {
        start->initializeNatural(bases);
    }
    naturalBase = startingBase->getNatural();

    // The "front line" position for each base has dependencies set above.
    for (Base * base : bases)
    {
        base->initializeFront();
    }

	setFrontBase();
}

void Bases::update()
{
    updateEnemyStart();
    updateBaseOwners();
	if (myMain()->getOwner() != the.self())
	{
		changeMainBase();
	}
    updateSmallMinerals();
}

// When a building is placed, we are told the desired and actual location of the building.
// Buildings are usually placed in the "main" base, so this can give us a hint when the main base
// is full and we need to choose a new main base.
void Bases::checkBuildingPosition(const BWAPI::TilePosition & desired, const BWAPI::TilePosition & actual)
{
    UAB_ASSERT(desired.isValid(), "bad location");

    if (the.zone.at(desired) == the.zone.at(mainBase->getTilePosition()))
    {
        // We tried to place in the main base, so let's keep checking.
        if (!actual.isValid() || the.zone.at(actual) != the.zone.at(desired))
        {
            // We failed to place it, or placed it outside the base's zone.

            // 1. Count the failures associated with this base.
            mainBase->placementFailed();
            // BWAPI::Broodwar->printf("placement failed in main base %d (total %d)", mainBase->getID(), mainBase->getFailedPlacements());

            // 2. Look for a new main base. Pick the one with the fewest failures.
            int bestFails = mainBase->getFailedPlacements();
            for (Base * base : bases)
            {
                // Protoss: The base must have a completed pylon.
                // Zerg: The base must be completed, so that it has some creep.
                if (base->getOwner() == the.self() &&
                    (the.self()->getRace() != BWAPI::Races::Protoss || (base->getPylon() && base->getPylon()->isCompleted())) &&
                    (the.self()->getRace() != BWAPI::Races::Zerg || base->isCompleted()))
                {
                    int fails = base->getFailedPlacements();
                    if (fails < bestFails)
                    {
                        bestFails = fails;
                        mainBase = base;
                    }
                }
            }
			setFrontBase();
            // BWAPI::Broodwar->printf("  new main %d (total %d)", mainBase->getID(), mainBase->getFailedPlacements());
        }
    }
}

void Bases::drawBaseInfo() const
{
    //the.partitions.drawWalkable();
    //the.partitions.drawPartition(
    //	the.partitions.id(the.bases.myMain()->getPosition()),
    //	BWAPI::Colors::Teal);

    //the.inset.draw();
    //the.vWalkRoom.draw();
    //the.tileRoom.draw();
    //the.zone.draw();

    if (!Config::Debug::DrawMapInfo)
    {
        return;
    }

    for (const Base * base : bases)
    {
        base->drawBaseInfo();
		if (base == enemyStartingBaseBestGuess)
		{
			int dx = 35;
			int dy = 38;
			BWAPI::Broodwar->drawTextMap(base->getPosition() + BWAPI::Position(dx, dy),
				"%cGUESSED ENEMY START", yellow);
		}
    }

    for (BWAPI::Unit small : smallMinerals)
    {
        BWAPI::Broodwar->drawCircleMap(small->getInitialPosition() + BWAPI::Position(0, 0), 32, BWAPI::Colors::Green);
    }

    int i = 0;
    for (const auto & rejected : nonbases)
    {
        ++i;
        for (auto resource : rejected)
        {
            BWAPI::Broodwar->drawTextMap(resource->getInitialPosition() + BWAPI::Position(-16, -6), "%c%d", red, i);
            BWAPI::Broodwar->drawCircleMap(resource->getInitialPosition() + BWAPI::Position(0, 0), 32, BWAPI::Colors::Red);
        }
    }

    for (const auto & potential : potentialBases)
    {
        // THe bounding box of the resources.
        BWAPI::Broodwar->drawBoxMap(BWAPI::Position(potential.left, potential.top), BWAPI::Position(potential.right, potential.bottom), BWAPI::Colors::Yellow);

        // The starting tile for base placement.
        //BWAPI::Broodwar->drawBoxMap(BWAPI::Position(potential.startTile), BWAPI::Position(potential.startTile)+BWAPI::Position(31,31), BWAPI::Colors::Yellow);
    }
}

// Our "frontmost" base, the base where we may want to defend the approaches.
// NOTE It's null if we're protoss and no pylon is near the main or natural nexus.
//      That includes the start of the game before the first pylon is built.
// May be null if we do not own any bases!
Base * Bases::myFront() const
{
	if (enemyStart() && enemyStart()->getNatural() &&
		enemyStart()->getNatural()->isBuildable())		// isBuildable() includes "we own it"
	{
		// We took the enemy natural. That's definitely our front base.
		return enemyStart()->getNatural();
	}

	if (naturalBase && naturalBase == frontBase && naturalBase->isBuildable())
	{
		return naturalBase;
	}
	if (mainBase->isBuildable())
	{
		return mainBase;
	}

	// We have no suitable bases. If there were one, mainBase would be it. Ouch.
    return nullptr;
}

// Return a position to place static defense or deploy a defensive force.
// If we can't figure out such a place, return no position.
// Not too smart, so far.
BWAPI::Position Bases::front() const
{
    Base * frontBase = myFront();
    if (frontBase)
    {
        return frontBase->getFront();
    }
    return BWAPI::Positions::None;
}

// Return a position to place static defense or deploy a defensive force.
BWAPI::TilePosition Bases::frontTile() const
{
    Base * frontBase = myFront();
    if (frontBase)
    {
        return frontBase->getFrontTile();
    }
    return BWAPI::TilePositions::None;
}

// The given position is reachable by ground from our starting base.
// The caller must ensure that the position passed in is valid.
bool Bases::connectedToStart(const BWAPI::Position & pos) const
{
    return the.partitions.id(pos) == the.partitions.id(startingBase->getTilePosition());
}

// The given tile is reachable by ground from our starting base.
// The caller must ensure that the position passed in is valid.
bool Bases::connectedToStart(const BWAPI::TilePosition & tile) const
{
    return the.partitions.id(tile) == the.partitions.id(startingBase->getTilePosition());
}

// Return the base at or close to the given position, or null if none.
Base * Bases::getBaseAtTilePosition(BWAPI::TilePosition pos)
{
    for (Base * base : bases)
    {
        if (closeEnough(pos, base->getTilePosition()))
        {
            return base;
        }
    }

    return nullptr;
}

// The number of bases believed owned by the given player,
// self, enemy, or neutral.
int Bases::baseCount(BWAPI::Player player) const
{
    int count = 0;

    for (const Base * base : bases)
    {
        if (base->getOwner() == player)
        {
            ++count;
        }
    }

    return count;
}

// The number of completed bases believed owned by the given player,
// self, enemy, or neutral.
int Bases::completedBaseCount(BWAPI::Player player) const
{
    int count = 0;

    for (const Base * base : bases)
    {
        if (base->getOwner() == player && base->isCompleted())
        {
            ++count;
        }
    }

    return count;
}

// The number of reachable expansions that are believed not yet taken.
int Bases::freeLandBaseCount() const
{
    int count = 0;

    for (Base * base : bases)
    {
        if (base->getOwner() == the.neutral() && connectedToStart(base->getTilePosition()))
        {
            ++count;
        }
    }

    return count;
}

// Number of non-zero-amount mineral patches at all of my bases.
// Decreases as patches mine out, increases as new bases are taken.
int Bases::remainingMineralPatchCount() const
{
    int count = 0;

    for (Base * base : bases)
    {
        if (base->isMyBase())
        {
            count += base->getRemainingMinerals().size();
        }
    }

    return count;
}

// Current number of geysers at all bases of the given player, whether taken or not.
int Bases::geyserCount(BWAPI::Player player) const
{
    int count = 0;

    for (Base * base : bases)
    {
        if (base->getOwner() == player)
        {
            count += base->getGeysers().size();
        }
    }

    return count;
}

// Current number of my completed refineries at my completed bases,
// and number of bare geysers available to be taken.
// Not counted: Enemy stolen refineries; refineries under construction.
// NOTE That means it will return no geysers, free or taken, if the one geyser is building a refinery.
void Bases::gasCounts(int & nRefineries, int & nFreeGeysers) const
{
    int refineries = 0;
    int geysers = 0;

    for (Base * base : bases)
    {
        if (base->getOwner() == the.self() && base->isCompleted())
        {
            // Recalculate the base's geysers every time.
            // This is a slow but accurate way to work around the BWAPI geyser bug.
            base->updateGeysers();

            for (BWAPI::Unit geyser : base->getGeysers())
            {
                if (geyser->getPlayer() == the.self() &&
                    geyser->getType().isRefinery() &&
                    geyser->isCompleted())
                {
                    ++refineries;
                }
                else if (geyser->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser)
                {
                    ++geysers;
                }
            }
        }
    }

    nRefineries = refineries;
    nFreeGeysers = geysers;
}

// Find the closest base which contains the given position, if any.
// A point is "in a base" if it is in the same zone and is close enough.
// On some maps, there may be more than one, so we have to find the closest.
Base * Bases::findBaseByPosition(const BWAPI::TilePosition & tile) const
{
	Base * bestBase = nullptr;
	int bestDist = MAX_DISTANCE;

	for (Base * b : getAll())
	{
		if (the.zone.at(b->getTilePosition()) == the.zone.at(tile))
		{
			int dist = tile.getApproxDistance(b->getTilePosition());
			if (dist <= BaseRadius && dist < bestDist)
			{
				bestBase = b;
				bestDist = dist;
			}
		}
	}
	return bestBase;
}

Base * Bases::findBaseByPosition(const BWAPI::Position & pos) const
{
	return findBaseByPosition(BWAPI::TilePosition(pos));
}

// A neutral building has been destroyed.
// If it was a base blocker, forget it so that we don't try (again) to destroy it.
// This should be easy to extend to the case of clearing a blocking building or mineral patch.
void Bases::clearNeutral(BWAPI::Unit unit)
{
    if (unit &&
        unit->getPlayer() == the.neutral() &&
        unit->getType().isBuilding())
    {
        auto it = baseBlockers.find(unit);
        if (it != baseBlockers.end())
        {
            it->second->clearBlocker(unit);
            (void)baseBlockers.erase(it);
        }
    }
}

Bases & Bases::Instance()
{
    static Bases instance;
    return instance;
}

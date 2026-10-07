#include "OpsBoss.h"

#include "The.h"

#include "Bases.h"
#include "InformationManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// Operations boss.
// Responsible for high-level tactical analysis and decisions. (Or will be, when it's finished.)
// This will eventually replace CombatCommander, once all the new parts are available.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

UnitCluster::UnitCluster()
{
    clear();
}

void UnitCluster::clear()
{
    center = BWAPI::Positions::Origin;
    radius = 0;
    status = ClusterStatus::None;

    air = false;
    speed = 0.0;

    count = 0;
    hp = 0;
    groundDPF = 0.0;
    airDPF = 0.0;

    extraText = "";
}

// Add a unit to the cluster.
// While adding units, we don't worry about the center and radius.
void UnitCluster::add(const UnitInfo & ui)
{
    if (count == 0)
    {
        air = ui.type.isFlyer();
        speed = BWAPI::Broodwar->enemy()->topSpeed(ui.type);
    }
    else
    {
        double topSpeed = BWAPI::Broodwar->enemy()->topSpeed(ui.type);
		if (ui.burrowed)
		{
			speed = 0.0;
		}
        else if (topSpeed > 0.0)
        {
            // NOTE A static defense building added to the cluster will not affect the cluster's speed.
            speed = std::min(speed, topSpeed);
        }
    }
    ++count;
    hp += ui.estimateHealth();
    groundDPF += UnitUtil::GroundDPF(BWAPI::Broodwar->enemy(), ui.type);
    airDPF += UnitUtil::AirDPF(BWAPI::Broodwar->enemy(), ui.type);
    units.insert(ui.unit);
}

// The time in frames to travel the given distance in pixels.
int UnitCluster::timeToTravel(int pixels) const
{
	if (speed <= 0.0 || pixels < 0)			// for safety
	{
		return MAX_FRAME;
	}
	return int(pixels / speed);
}

// This is used only for clusters of our own units (at least initially).
bool UnitCluster::includesType(BWAPI::UnitType type) const
{
	for (BWAPI::Unit u : units)
	{
		if (u->getType() == type)
		{
			return true;
		}
	}
	return false;
}

// Count units of a given type.
size_t UnitCluster::countUnits(BWAPI::UnitType type) const
{
	size_t n = 0;
	for (BWAPI::Unit u : units)
	{
		if (u->getType() == type)
		{
			++n;
		}
	}
	return n;
}

void UnitCluster::draw(BWAPI::Color color, const std::string & label) const
{
    BWAPI::Broodwar->drawCircleMap(center, radius, color);

    BWAPI::Position xy(center.x - 12, center.y - radius + 8);
    if (xy.y < 8)
    {
        xy.y = center.y + radius - 4 * 10 - 8;
    }

    BWAPI::Broodwar->drawTextMap(xy, "%c%s %c%d", orange, air ? "air" : "ground", cyan, count);
    xy.y += 10;
    BWAPI::Broodwar->drawTextMap(xy, "%chp %c%d", orange, cyan, hp);
    xy.y += 10;
    BWAPI::Broodwar->drawTextMap(xy, "%cdpf %c%g/%g", orange, cyan, groundDPF, airDPF);
    xy.y += 10;
    //BWAPI::Broodwar->drawTextMap(xy, "%cspeed %c%g", orange, cyan, speed);
    // xy.y += 10;
    if (label != "")
    {
        BWAPI::Broodwar->drawTextMap(xy, "%s", label.c_str());
        xy.y += 10;
    }
    if (extraText != "")
    {
        BWAPI::Broodwar->drawTextMap(xy, "%s", extraText.c_str());
        xy.y += 10;
    }
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Given the set of unit positions in a cluster, find the center and radius.
void OpsBoss::locateCluster(const std::vector<BWAPI::Position> & points, UnitCluster & cluster)
{
    BWAPI::Position total = BWAPI::Positions::Origin;
    UAB_ASSERT(points.size() > 0, "empty cluster");
    for (const BWAPI::Position & point : points)
    {
        total += point;
    }
    cluster.center = total / points.size();

    int radius = 0;
    for (const BWAPI::Position & point : points)
    {
        radius = std::max(radius, point.getApproxDistance(cluster.center));
    }
    cluster.radius = std::max(32, radius);
}

// Form a cluster around the given seed, updating the value of the cluster argument.
// Remove enemies added to the cluster from the enemies set.
// NOTE Units in a cluster are not necessarily in the same zone. It matters for ground units.
//      The cluster may be traversing a ramp (OK). Or it may be split by a cliff (bad).
//      It could be checked by comparing ground distance to air distance for ground units.
void OpsBoss::formCluster(const UnitInfo & seed, const UIMap & theUI, BWAPI::Unitset & units, UnitCluster & cluster)
{
    cluster.add(seed);
    cluster.center = seed.lastPosition;

    // The locations of each unit in the cluster so far.
    std::vector<BWAPI::Position> points;
    points.push_back(seed.lastPosition);

    bool any;
    int nextRadius = clusterStart;
    do
    {
        any = false;
        for (auto it = units.begin(); it != units.end();)
        {
            const UnitInfo & ui = theUI.at(*it);
            if (ui.type.isFlyer() == cluster.air &&
                cluster.center.getApproxDistance(ui.lastPosition) <= nextRadius)
            {
                any = true;
                points.push_back(ui.lastPosition);
                cluster.add(ui);
                it = units.erase(it);
            }
            else
            {
                ++it;
            }
        }
        locateCluster(points, cluster);
        nextRadius = cluster.radius + clusterRange;
    } while (any);
}

// Group a given set of units into clusters.
// NOTE It modifies the set of units! You may have to copy it before you pass it in.
void OpsBoss::clusterUnits(BWAPI::Player player, BWAPI::Unitset & units, std::vector<UnitCluster> & clusters)
{
    clusters.clear();

    if (units.empty())
    {
        return;
    }

    const UIMap & theUI = InformationManager::Instance().getUnitData(player).getUnits();

    while (!units.empty())
    {
        const auto & seed = theUI.at(*units.begin());
        units.erase(units.begin());

        clusters.push_back(UnitCluster());
        formCluster(seed, theUI, units, clusters.back());
    }
}

// Cluster units that can perform ground and/or air defense,
// so that our fleeing units can find safe places to flee to.
// Include static defense.
void OpsBoss::updateDefenders()
{
    BWAPI::Unitset groundDefenders;
    BWAPI::Unitset airDefenders;

    for (BWAPI::Unit u : the.self()->getUnits())
    {
        if (u->isCompleted() && u->getPosition().isValid())
        {
            if (UnitUtil::CanAttackGround(u) && !u->getType().isWorker() && u->getType() != BWAPI::UnitTypes::Zerg_Broodling)
            {
                groundDefenders.insert(u);
            }
            if (UnitUtil::TypeCanAttackAir(u->getType()))
            {
                airDefenders.insert(u);
            }
        }
    }

    clusterUnits(the.self(), groundDefenders, groundDefenseClusters);
    clusterUnits(the.self(), airDefenders, airDefenseClusters);
}

// 1. The enemy has no known static defense buildings.
// 2. The enemy has no known anti-air units.
// 3. We have enough army to hold, given that the enemy has no production.
// NOTE Spellcasters are not counted.
// NOTE This could be tightened up by counting our expected production.
bool OpsBoss::enemyCannotWin() const
{
	int yourSupply = 0;		// combat units that can hit ground
	int mySupply = 0;

	for (const std::pair<BWAPI::UnitType, int> & unitCount : the.your.seen.getCounts())
	{
		if (UnitUtil::TypeCanAttackAir(unitCount.first))		// includes bunkers
		{
			return false;
		}
		if (UnitUtil::TypeCanAttackGround(unitCount.first))
		{
			if (unitCount.first.isBuilding())
			{
				return false;
			}
			yourSupply += unitCount.second * unitCount.first.supplyRequired();
		}
	}

	for (const std::pair<BWAPI::UnitType, int> & unitCount : the.my.completed.getCounts())
	{
		if (UnitUtil::TypeCanAttackGround(unitCount.first))
		{
			mySupply += unitCount.second * unitCount.first.supplyRequired();
		}
	}

	return 0.75 * mySupply >= yourSupply;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

OpsBoss::OpsBoss()
    : defenderUpdateFrame(0)
{
}

void OpsBoss::initialize()
{
}

// Should we attack because we are maxed?
// This maintains a little state to provide hysteresis.
bool OpsBoss::attackAtMax()
{
	// If we're nearly maxed and have good income or cash.
	if (the.self()->supplyUsed() >= 390 &&
		(the.self()->minerals() > 1000 || WorkerManager::Instance().getNumMineralWorkers() > 12))
	{
		_attackAtMax = true;
	}

	if (_attackAtMax && the.self()->supplyUsed() < 320)
	{
		_attackAtMax = false;
	}

	return _attackAtMax;
}

// The enemy is toast, and all we have left to do is to eradicate the surviving enemy buildings.
// There might be stray units or whole bases that we haven't seen, but if so we can change
// gears then.
// When this is true, we prefer to make air units because they can find terran floating buildings
// and buildings on islands. Also they scout faster.
bool OpsBoss::enemySeemsDead()
{
	static bool dead = false;
	static int lastUpdate = 30 * 24;

	if (the.now() < lastUpdate + 4)
	{
		return dead;
	}

	dead =
		the.bases.enemyStart() &&					// enemy start is known
		the.bases.baseCount(the.enemy()) == 0 &&	// no enemy bases known
		enemyCannotWin();

	lastUpdate = the.now();
	return dead;
}

// Wraiths are forbidden to attack buildings other than static defense
// unless this test passes: The enemy has no known mobile units.
bool OpsBoss::wraithsAttackEnemyBuildings()
{
	static bool attack = false;
	static int lastUpdate = 4 * 60 * 24;

	if (the.now() < lastUpdate + 17)
	{
		return attack;
	}

	attack = true;
	for (const std::pair<BWAPI::UnitType, int> & unitCount : the.your.seen.getCounts())
	{
		if (!unitCount.first.isBuilding())
		{
			attack = false;
			break;
		}
	}

	lastUpdate = the.now();
	return attack;
}

// Group all known units of a player into clusters.
void OpsBoss::cluster(BWAPI::Player player, std::vector<UnitCluster> & clusters)
{
    const UIMap & theUI = InformationManager::Instance().getUnitData(player).getUnits();

    // Step 1: Gather units that should be put into clusters.

    int now = the.now();
    BWAPI::Unitset units;

    for (const auto & kv : theUI)
    {
        const UnitInfo & ui = kv.second;

        if (UnitUtil::IsCombatSimUnit(ui.type) &&	// not a worker, larva, ...
            !ui.type.isBuilding() &&				// not a static defense building
            (!ui.goneFromLastPosition ||            // not known to have moved from its last position, or
            now - ui.updateFrame < 5 * 24))			// known to have moved but not long ago
        {
            units.insert(kv.first);
        }
    }

    // Step 2: Fill in the clusters.

    clusterUnits(player, units, clusters);
}

// Group a given set of units, owned by the same player, into clusters.
void OpsBoss::cluster(BWAPI::Player player, const BWAPI::Unitset & units, std::vector<UnitCluster> & clusters)
{
    BWAPI::Unitset unitsCopy = units;

    clusterUnits(player, unitsCopy, clusters);		// NOTE modifies unitsCopy
}

void OpsBoss::update()
{
	// Clusters.
	int phase = BWAPI::Broodwar->getFrameCount() % 5;
    if (phase == 0)
    {
        cluster(the.enemy(), enemyClusters);
    }
    drawClusters();
}

// Return the clusters of ground defenders, from cache when available.
const std::vector<UnitCluster> & OpsBoss::getGroundDefenseClusters()
{
    if (defenderUpdateFrame != the.now())
    {
        updateDefenders();
        defenderUpdateFrame = the.now();
    }

    return groundDefenseClusters;
}

// Return the clusters of air defenders, from cache when available.
const std::vector<UnitCluster> & OpsBoss::getAirDefenseClusters()
{
    if (defenderUpdateFrame != the.now())
    {
        updateDefenders();
        defenderUpdateFrame = the.now();
    }

    return airDefenseClusters;
}

// The cluster must have a non-zero DPF.
// Return null if none.
const UnitCluster * OpsBoss::getNearestEnemyClusterVs(const BWAPI::Position & pos, bool vsGround, bool vsAir)
{
    int distance = MAX_DISTANCE;
    const UnitCluster * cluster = nullptr;

    for (const UnitCluster & c : enemyClusters)
    {
        int d = pos.getApproxDistance(c.center);
        if (d < distance && (vsGround && c.groundDPF > 0.0 || vsAir && c.airDPF > 0.0))
        {
            distance = d;
            cluster = &c;
        }
    }

    return cluster;
}

// Return null if none.
// The cluster must have a non-negligible DPF. It's different than for enemy clusters.
const UnitCluster * OpsBoss::getNearestFriendlyClusterVs(const BWAPI::Position & pos, bool vsGround, bool vsAir)
{
	int distance = MAX_DISTANCE;
	const UnitCluster * cluster = nullptr;

	if (vsGround)
	{
		for (const UnitCluster & c : groundDefenseClusters)
		{
			int d = pos.getApproxDistance(c.center);
			if (d < distance && c.groundDPF > 0.1)
			{
				distance = d;
				cluster = &c;
			}
		}
	}

	if (vsAir)
	{
		for (const UnitCluster & c : airDefenseClusters)
		{
			int d = pos.getApproxDistance(c.center);
			if (d < distance && c.airDPF > 0.1)
			{
				distance = d;
				cluster = &c;
			}
		}
	}

	return cluster;
}

// Usage:
// std::function p = [...](const UnitCluster & c) -> bool { return ...; };
// const UnitCluster * enemyCluster = getNearestEnemyClusterSuchThat(pos, p);
const UnitCluster * OpsBoss::getNearestEnemyClusterSuchThat(
	const BWAPI::Position & pos,
	std::function<bool(const UnitCluster & cluster)> & p)
{
	int distance = MAX_DISTANCE;
	const UnitCluster * cluster = nullptr;

	for (const UnitCluster & c : enemyClusters)
	{
		int d = pos.getApproxDistance(c.center);
		if (d < distance && p(c))
		{
			distance = d;
			cluster = &c;
		}
	}

	return cluster;
}

// Draw enemy clusters.
// Squads are responsible for drawing squad clusters.
void OpsBoss::drawClusters() const
{
    if (Config::Debug::DrawClusters)
    {
        for (const UnitCluster & cluster : enemyClusters)
        {
            cluster.draw(BWAPI::Colors::Red);
        }
    }
}

#include "SparkTerran.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace BWAPI;
using namespace Filter;

namespace
{
    const int SCVsPerBase = 22;        // ~2.5 per mineral patch plus 3 on gas
    const int MaxSCVs = 66;
    const int FirstAttackArmy = 10;    // first push goes at this army supply
    const int LaterAttackArmy = 18;
    const int RetreatBelow = 6;
    const double AttackRatio = 1.15;   // attack when our nearby power beats theirs by this much
    const double RetreatRatio = 1.4;   // retreat when theirs beats ours by this much
    const int RegroupDistance = 500;
    const int DefenseRadius = 900;     // enemies this close to a base pull the army home
    const int ScoutSupply = 8;         // SCV scout leaves at this supply

    bool isArmyUnit(UnitType type)
    {
        return type == UnitTypes::Terran_Marine || type == UnitTypes::Terran_Firebat
               || type == UnitTypes::Terran_Medic || type == UnitTypes::Terran_Ghost
               || type == UnitTypes::Terran_Vulture || type == UnitTypes::Terran_Goliath
               || type == UnitTypes::Terran_Siege_Tank_Tank_Mode || type == UnitTypes::Terran_Siege_Tank_Siege_Mode
               || type == UnitTypes::Terran_Wraith || type == UnitTypes::Terran_Valkyrie
               || type == UnitTypes::Terran_Science_Vessel || type == UnitTypes::Terran_Battlecruiser;
    }

    bool isCombat(Unit unit)
    {
        auto type = unit->getType();
        return isArmyUnit(type) && !unit->isLoaded();
    }

    // Enemy units worth shooting first: static defence and tanks, then anything that shoots back, then workers
    int targetPriority(Unit target)
    {
        auto type = target->getType();
        if (type == UnitTypes::Terran_Bunker || type == UnitTypes::Terran_Siege_Tank_Siege_Mode
            || type == UnitTypes::Zerg_Sunken_Colony || type == UnitTypes::Zerg_Lurker
            || type == UnitTypes::Protoss_Photon_Cannon || type == UnitTypes::Protoss_Reaver) return 5;
        if (type == UnitTypes::Terran_Missile_Turret || type == UnitTypes::Zerg_Spore_Colony) return 4;
        if (type.isDetector()) return 4;
        if (type == UnitTypes::Protoss_Shuttle || type == UnitTypes::Terran_Dropship) return 4;
        if (type.canAttack() && !type.isWorker()) return 3;
        if (type.isWorker()) return 2;
        if (type.isResourceDepot()) return 1;
        return 0;
    }

    // An enemy that is fighting rather than walking past
    bool hostile(Unit enemy)
    {
        auto type = enemy->getType();
        if (!type.isWorker()) return type.canAttack() || type.isBuilding();
        if (enemy->isAttacking() || enemy->isConstructing()) return true;
        auto target = enemy->getOrderTarget();
        return enemy->getOrder() == Orders::AttackUnit && target && target->getPlayer() == Broodwar->self();
    }

    Position towards(Position from, Position to, int distance)
    {
        int dx = to.x - from.x, dy = to.y - from.y;
        double length = std::sqrt(double(dx * dx + dy * dy));
        if (length < 1) return from;
        return Position(from.x + int(dx * distance / length), from.y + int(dy * distance / length)).makeValid();
    }

    // A tile ground units can cross: its middle four walk cells are walkable
    bool walkableTile(int x, int y)
    {
        for (int wx = x * 4 + 1; wx <= x * 4 + 2; wx++)
        {
            for (int wy = y * 4 + 1; wy <= y * 4 + 2; wy++)
            {
                if (!Broodwar->isWalkable(wx, wy)) return false;
            }
        }
        return true;
    }

    // Ground distance in tiles from the nearest source to every tile (-1 where unreachable), out to a limit
    std::vector<int> groundDistances(const std::vector<TilePosition> &sources, int limit = INT_MAX)
    {
        int width = Broodwar->mapWidth(), height = Broodwar->mapHeight();
        std::vector<int> distance(width * height, -1);
        std::vector<TilePosition> queue;
        for (auto source : sources)
        {
            if (!source.isValid() || distance[source.y * width + source.x] >= 0) continue;
            distance[source.y * width + source.x] = 0;
            queue.push_back(source);
        }
        const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
        for (size_t i = 0; i < queue.size(); i++)
        {
            auto tile = queue[i];
            int next = distance[tile.y * width + tile.x] + 1;
            if (next > limit) continue;
            for (int d = 0; d < 4; d++)
            {
                int x = tile.x + dx[d], y = tile.y + dy[d];
                if (x < 0 || y < 0 || x >= width || y >= height || distance[y * width + x] >= 0) continue;
                if (!walkableTile(x, y)) continue;
                distance[y * width + x] = next;
                queue.emplace_back(x, y);
            }
        }
        return distance;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Setup

void SparkTerran::onStart()
{
    Broodwar->enableFlag(Flag::UserInput);
    enemyRace = Broodwar->enemy() ? Broodwar->enemy()->getRace() : Races::Unknown;

    home = Position(Broodwar->self()->getStartLocation()) + Position(64, 48);
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType().isResourceDepot())
        {
            mainCC = unit;
            home = unit->getPosition();
        }
    }

    Position sum(0, 0);
    int minerals = 0;
    for (auto mineral : Broodwar->getStaticMinerals())
    {
        if (mineral->getDistance(home) < 320)
        {
            sum += mineral->getInitialPosition();
            minerals++;
        }
    }
    mineralCenter = minerals ? Position(sum.x / minerals, sum.y / minerals) : home;

    Position center(Broodwar->mapWidth() * 16, Broodwar->mapHeight() * 16);
    rally = home + (center - home) / 4;

    for (auto start : Broodwar->getStartLocations())
    {
        if (start != Broodwar->self()->getStartLocation()) scoutTargets.push_back(start);
    }
    std::sort(scoutTargets.begin(), scoutTargets.end(), [this](TilePosition a, TilePosition b)
    {
        return Position(a).getApproxDistance(home) < Position(b).getApproxDistance(home);
    });
    if (scoutTargets.size() == 1) enemyBase = Position(scoutTargets[0]) + Position(64, 48);

    findBases();
    rampTop = findRampTop();
    rally = rampTop;
    analyseMap();
}

// How far the enemy is by ground: a short rush distance means no greedy expand
void SparkTerran::analyseMap()
{
    int width = Broodwar->mapWidth();
    TilePosition homeTile(home);
    std::vector<TilePosition> enemyStarts;
    for (auto start : Broodwar->getStartLocations())
    {
        if (start != Broodwar->self()->getStartLocation()) enemyStarts.push_back(start + TilePosition(2, 1));
    }
    auto fromHome = groundDistances({homeTile});
    rushDistance = INT_MAX;
    for (auto start : enemyStarts)
    {
        int distance = fromHome[start.y * width + start.x];
        if (distance >= 0) rushDistance = std::min(rushDistance, distance);
    }
}

// The highest-ground tile of our main closest to the natural: the top of the ramp, where defending is easiest
Position SparkTerran::findRampTop() const
{
    Position toward = natural ? natural->center : Position(Broodwar->mapWidth() * 16, Broodwar->mapHeight() * 16);
    TilePosition homeTile(home);
    int homeHeight = Broodwar->getGroundHeight(homeTile);
    Position best = rally;
    int bestDistance = INT_MAX;
    for (int dx = -24; dx <= 24; dx++)
    {
        for (int dy = -24; dy <= 24; dy++)
        {
            TilePosition tile(homeTile.x + dx, homeTile.y + dy);
            if (!tile.isValid() || Broodwar->getGroundHeight(tile) != homeHeight) continue;
            WalkPosition walk(tile);
            if (!Broodwar->isWalkable(walk.x + 1, walk.y + 1) || !Broodwar->isBuildable(tile)) continue;
            Position center = Position(tile) + Position(16, 16);
            if (!Broodwar->hasPath(home, center)) continue;
            int distance = center.getApproxDistance(toward);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = center;
            }
        }
    }
    // A little back from the edge, so the army stands on the high ground rather than at the lip
    return towards(best, home, 64);
}

// Groups the map's mineral fields into bases and works out where each one's command center goes
void SparkTerran::findBases()
{
    std::vector<Unit> minerals;
    for (auto mineral : Broodwar->getStaticMinerals())
    {
        if (mineral->getInitialResources() >= 200) minerals.push_back(mineral);  // skip mineral walls
    }

    std::vector<bool> used(minerals.size(), false);
    for (size_t seed = 0; seed < minerals.size(); seed++)
    {
        if (used[seed]) continue;
        std::vector<size_t> cluster = {seed};
        used[seed] = true;
        for (size_t i = 0; i < cluster.size(); i++)
        {
            for (size_t j = 0; j < minerals.size(); j++)
            {
                if (!used[j] && minerals[cluster[i]]->getInitialPosition().getApproxDistance(
                        minerals[j]->getInitialPosition()) < 260)
                {
                    used[j] = true;
                    cluster.push_back(j);
                }
            }
        }
        if (cluster.size() < 5) continue;

        Position sum(0, 0);
        for (auto index : cluster) sum += minerals[index]->getInitialPosition();
        Base base;
        base.center = Position(sum.x / (int) cluster.size(), sum.y / (int) cluster.size());

        int bestGeyser = 400;
        for (auto geyser : Broodwar->getStaticGeysers())
        {
            int distance = geyser->getInitialPosition().getApproxDistance(base.center);
            if (distance < bestGeyser)
            {
                bestGeyser = distance;
                base.geyser = geyser;
            }
        }

        // The command center spot closest to the minerals (and geyser) that the game allows
        TilePosition centerTile(base.center);
        Position resources = base.geyser ? (base.center * 3 + base.geyser->getInitialPosition()) / 4 : base.center;
        int bestDistance = INT_MAX;
        base.depot = TilePositions::Invalid;
        for (int dx = -12; dx <= 12; dx++)
        {
            for (int dy = -12; dy <= 12; dy++)
            {
                TilePosition tile(centerTile.x + dx, centerTile.y + dy);
                if (!tile.isValid() || !Broodwar->canBuildHere(tile, UnitTypes::Terran_Command_Center)) continue;
                int distance = (Position(tile) + Position(64, 48)).getApproxDistance(resources);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    base.depot = tile;
                }
            }
        }
        if (base.depot.isValid()) bases.push_back(base);
    }

    std::sort(bases.begin(), bases.end(), [this](const Base &a, const Base &b)
    {
        return a.center.getApproxDistance(home) < b.center.getApproxDistance(home);
    });

    // The natural: the nearest other base we can walk to
    for (auto &base : bases)
    {
        if (base.center.getApproxDistance(home) < 400) continue;
        if (!Broodwar->hasPath(home, base.center)) continue;
        natural = &base;
        break;
    }
}

void SparkTerran::onUnitDestroy(Unit unit)
{
    enemyBuildings.erase(unit->getID());
    enemyArmy.erase(unit->getID());
    builders.erase(unit);
    pendingAddons.erase(unit);
    if (unit == scout) scout = nullptr;
    if (unit == mainCC) mainCC = nullptr;
}

void SparkTerran::onFrame()
{
    if (Broodwar->isPaused() || !Broodwar->self()) return;
    int frame = Broodwar->getFrameCount();

    if (getenv("ST_DEBUG") && frame % 500 == 0)
    {
        auto self = Broodwar->self();
        printf("ST %d: supply %d/%d min %d gas %d cc %d scv %d rax %d mar %d med %d fac %d tank %d attacking %d wave %d\n",
               frame, self->supplyUsed() / 2, self->supplyTotal() / 2, self->minerals(), self->gas(),
               self->allUnitCount(UnitTypes::Terran_Command_Center), self->allUnitCount(UnitTypes::Terran_SCV),
               self->allUnitCount(UnitTypes::Terran_Barracks), self->allUnitCount(UnitTypes::Terran_Marine),
               self->allUnitCount(UnitTypes::Terran_Medic), self->allUnitCount(UnitTypes::Terran_Factory),
               self->allUnitCount(UnitTypes::Terran_Siege_Tank_Tank_Mode)
                   + self->allUnitCount(UnitTypes::Terran_Siege_Tank_Siege_Mode),
               attacking, wave);
        printf("     race %s rush %d proxy %d fe %d air %d cloak %d tanks %d mech %d step %zu/%zu\n",
               enemyRace.c_str(), rushSeen, proxySeen, feSeen, airSeen, cloakSeen, tanksSeen, mechSeen,
               buildOrderStep, buildOrder.size());
        fflush(stdout);
    }

    trackEnemy();
    if (frame % 4 == 0) controlArmy();
    if (frame % 8 == 0)
    {
        if (!mainCC || !mainCC->exists())
        {
            mainCC = nullptr;
            auto all = commandCenters(true);
            if (!all.empty()) mainCC = all.front();
        }
        buildStructures();
        trainUnits();
        manageWorkers();
        researchUpgrades();
        scoutEnemy();
        useComsat();
    }
    if (frame % 240 == 0) tryExpand();
}

// ---------------------------------------------------------------------------------------------------------------------
// Build orders

void SparkTerran::chooseBuildOrder()
{
    using namespace UnitTypes;
    if (enemyRace == Races::Zerg) plan = Plan::RaxPressure;
    else if (enemyRace == Races::Terran) plan = Plan::Defensive;
    else plan = Plan::FEWall;  // Protoss or unknown: the wall-in expand is the safe default

    if (plan == Plan::RaxPressure)
    {
        // 2-rax pressure: two barracks of marines, bunker if rushed, academy then factory behind the pressure
        buildOrder = {{8, Terran_Supply_Depot}, {9, Terran_Barracks}, {11, Terran_Barracks}, {13, Terran_Supply_Depot},
                      {15, Terran_Refinery}, {19, Terran_Academy}, {21, Terran_Supply_Depot},
                      {24, Terran_Factory}, {26, Terran_Engineering_Bay}, {30, Terran_Supply_Depot}};
    }
    else if (plan == Plan::Defensive)
    {
        // TvT: barracks, bunker at the ramp, factory into tanks; the expansion waits until the tanks hold
        buildOrder = {{8, Terran_Supply_Depot}, {10, Terran_Barracks}, {12, Terran_Refinery}, {14, Terran_Supply_Depot},
                      {15, Terran_Bunker, false, true}, {18, Terran_Factory}, {22, Terran_Supply_Depot},
                      {24, Terran_Engineering_Bay}, {28, Terran_Supply_Depot}};
    }
    else
    {
        // 1-rax wall-in fast expand: depot and barracks plug the ramp, command center at the natural behind them,
        // then the academy, factory and tanks. Unknown race scouts first and may still switch to 2-rax vs Zerg
        buildOrder = {{8, Terran_Supply_Depot, false, true}, {10, Terran_Barracks, false, true},
                      {12, Terran_Refinery}, {14, Terran_Supply_Depot}, {16, Terran_Command_Center, true},
                      {18, Terran_Barracks}, {20, Terran_Academy}, {22, Terran_Supply_Depot}, {24, Terran_Factory},
                      {28, Terran_Engineering_Bay}};
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Building

std::vector<Unit> SparkTerran::commandCenters(bool completedOnly) const
{
    std::vector<Unit> result;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType().isResourceDepot() && (unit->isCompleted() || !completedOnly)) result.push_back(unit);
    }
    return result;
}

// The completed command center with the fewest mineral workers per mineral patch
Unit SparkTerran::ccNeedingWorkers() const
{
    Unit best = nullptr;
    double bestRatio = 1e9;
    for (auto cc : commandCenters(true))
    {
        int patches = (int) cc->getUnitsInRadius(320, IsMineralField).size();
        if (patches == 0) continue;
        int workers = (int) cc->getUnitsInRadius(320, IsOwned && IsWorker && IsGatheringMinerals).size();
        double ratio = double(workers) / patches;
        if (ratio < bestRatio)
        {
            bestRatio = ratio;
            best = cc;
        }
    }
    return best;
}

int SparkTerran::pendingCount(UnitType type) const
{
    int n = 0;
    for (auto &[builder, pending] : builders)
    {
        if (pending.type == type) n++;
    }
    return n;
}

int SparkTerran::count(UnitType type, bool includePending) const
{
    int n = Broodwar->self()->allUnitCount(type);
    return includePending ? n + pendingCount(type) : n;
}

int SparkTerran::reservedMinerals() const
{
    int total = 0;
    for (auto &[builder, pending] : builders) total += pending.type.mineralPrice();
    return total;
}

int SparkTerran::reservedGas() const
{
    int total = 0;
    for (auto &[builder, pending] : builders) total += pending.type.gasPrice();
    return total;
}

Unit SparkTerran::chooseBuilder(Position near)
{
    Unit best = nullptr;
    int bestDistance = INT_MAX;
    for (auto scv : Broodwar->self()->getUnits())
    {
        if (!scv->getType().isWorker() || !scv->isCompleted() || scv == scout || builders.count(scv)) continue;
        if (scv->isCarryingMinerals() || scv->isGatheringGas() || scv->isConstructing()) continue;
        if (scv->getOrder() == Orders::AttackUnit || scv->getOrder() == Orders::Repair) continue;
        int distance = scv->getDistance(near);
        if (distance < bestDistance)
        {
            best = scv;
            bestDistance = distance;
        }
    }
    return best;
}

// Supply depots spread around the main, away from the mineral line, a few tiles apart
TilePosition SparkTerran::depotSpot()
{
    Position away = home + (home - mineralCenter) * 2 / 3;
    int depots = count(UnitTypes::Terran_Supply_Depot);
    Position offset(((depots % 4) - 1) * 96, ((depots / 4) % 3 - 1) * 96);
    return TilePosition(away + offset);
}

// Wall-in pieces and bunkers: near the ramp top, tight against the high ground
TilePosition SparkTerran::rampSpot(UnitType type)
{
    Position center = rampTop.isValid() ? rampTop : home;
    int n = count(type);
    Position offset(((n % 3) - 1) * 64, ((n / 3) % 2) * 64);
    return TilePosition(center + offset);
}

// Whether a building fits at `tile` if the units walking about there moved away: buildable, off creep, and no
// building, resource or other immobile unit in the way
bool SparkTerran::placeable(TilePosition tile, UnitType type, bool checkExplored) const
{
    int w = type.tileWidth(), h = type.tileHeight();
    if (tile.x < 0 || tile.y < 0 || tile.x + w > Broodwar->mapWidth() || tile.y + h > Broodwar->mapHeight()) return false;
    for (int x = tile.x; x < tile.x + w; x++)
    {
        for (int y = tile.y; y < tile.y + h; y++)
        {
            if (!Broodwar->isBuildable(x, y, true) || Broodwar->hasCreep(x, y)) return false;
            if (checkExplored && !Broodwar->isExplored(x, y)) return false;
        }
    }
    auto blocking = Broodwar->getUnitsInRectangle(Position(tile), Position(tile + TilePosition(w, h)) - Position(1, 1),
                                                  IsBuilding || !CanMove);
    return blocking.empty();
}

TilePosition SparkTerran::findBuildSpot(UnitType type, TilePosition near, bool checkExplored) const
{
    int w = type.tileWidth(), h = type.tileHeight();
    for (int radius = 0; radius <= 20; radius++)
    {
        for (int dx = -radius; dx <= radius; dx++)
        {
            for (int dy = -radius; dy <= radius; dy++)
            {
                if (std::abs(dx) != radius && std::abs(dy) != radius) continue;  // the ring at this radius only
                TilePosition tile(near.x + dx, near.y + dy);
                if (!tile.isValid()) continue;
                if (!placeable(tile, type, checkExplored) && !Broodwar->canBuildHere(tile, type, nullptr, checkExplored))
                    continue;
                Position center = Position(tile) + Position(w * 16, h * 16);
                // Not between the command center and its minerals (where only turrets and bunkers belong)
                if (type != UnitTypes::Terran_Missile_Turret && type != UnitTypes::Terran_Bunker
                    && center.getApproxDistance(mineralCenter) < home.getApproxDistance(mineralCenter)
                    && center.getApproxDistance(home) < 400)
                {
                    continue;
                }
                // Never on (or touching) a base's command center spot
                bool onDepot = false;
                if (!type.isResourceDepot())
                {
                    for (auto &base : bases)
                    {
                        if (tile.x + w >= base.depot.x - 1 && tile.x <= base.depot.x + 4
                            && tile.y + h >= base.depot.y - 1 && tile.y <= base.depot.y + 3)
                        {
                            onDepot = true;
                        }
                    }
                }
                if (onDepot) continue;
                // Nor where an SCV is already on its way to build something else
                bool taken = false;
                for (auto &[builder, pending] : builders)
                {
                    if (tile.x + w >= pending.tile.x && tile.x <= pending.tile.x + pending.type.tileWidth()
                        && tile.y + h >= pending.tile.y && tile.y <= pending.tile.y + pending.type.tileHeight())
                    {
                        taken = true;
                    }
                }
                if (taken) continue;
                // Leave a free tile around the building so the base doesn't wall itself in
                bool clear = true;
                for (int x = tile.x - 1; x <= tile.x + w && clear; x++)
                {
                    for (int y = tile.y - 1; y <= tile.y + h && clear; y++)
                    {
                        if (x < 0 || y < 0 || x >= Broodwar->mapWidth() || y >= Broodwar->mapHeight()) continue;
                        bool border = x == tile.x - 1 || x == tile.x + w || y == tile.y - 1 || y == tile.y + h;
                        if (!border) continue;
                        for (auto unit : Broodwar->getUnitsOnTile(x, y))
                        {
                            auto blocker = unit->getType();
                            if (blocker.isMineralField() || blocker == UnitTypes::Resource_Vespene_Geyser
                                || blocker.isBuilding())
                            {
                                clear = false;
                            }
                        }
                    }
                }
                if (clear) return tile;
            }
        }
    }
    return TilePositions::Invalid;
}

bool SparkTerran::build(UnitType type, TilePosition near)
{
    if (Broodwar->self()->minerals() - reservedMinerals() < type.mineralPrice()) return false;
    if (Broodwar->self()->gas() - reservedGas() < type.gasPrice()) return false;

    TilePosition tile = TilePositions::Invalid;
    if (type.isRefinery())
    {
        // The nearest free geyser to a completed command center
        int best = INT_MAX;
        for (auto cc : commandCenters(true))
        {
            for (auto geyser : Broodwar->getGeysers())
            {
                if (geyser->getType() != UnitTypes::Resource_Vespene_Geyser) continue;
                int distance = geyser->getDistance(cc);
                if (distance < 300 && distance < best)
                {
                    best = distance;
                    tile = geyser->getTilePosition();
                }
            }
        }
    }
    else if (type.isResourceDepot())
    {
        tile = near;  // the base's precomputed spot
    }
    else
    {
        tile = findBuildSpot(type, near, Broodwar->isExplored(near));  // the natural may not be seen yet
    }
    if (!tile.isValid()) return false;

    auto builder = chooseBuilder(Position(tile));
    if (!builder) return false;

    if (getenv("ST_DEBUG"))
        printf("     BUILD %s near %d,%d -> %d,%d frame %d\n", type.c_str(), near.x, near.y, tile.x, tile.y,
               Broodwar->getFrameCount());
    builders[builder] = PendingBuild{type, tile, Broodwar->getFrameCount()};
    if (!builder->build(type, tile)) builder->move(Position(tile) + Position(type.tileWidth() * 16, type.tileHeight() * 16));
    return true;
}

// An addon on a finished building: machine shop, control tower, comsat station, physics lab
bool SparkTerran::buildAddon(Unit building, UnitType addon)
{
    if (!building || !building->exists() || !building->isCompleted()) return false;
    if (building->getAddon() || pendingAddons.count(building)) return false;
    if (Broodwar->self()->minerals() < addon.mineralPrice() || Broodwar->self()->gas() < addon.gasPrice()) return false;
    if (!building->buildAddon(addon)) return false;
    pendingAddons[building] = addon;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Macro: structures, training, upgrades, expansions

void SparkTerran::buildStructures()
{
    if (commandCenters(true).empty()) return;
    auto self = Broodwar->self();
    if (buildOrder.empty()) chooseBuildOrder();

    // Unknown race turned out to be Zerg early: switch to 2-rax pressure while it still matters
    if (!zergSwitchDone && enemyRace == Races::Zerg && plan == Plan::FEWall && Broodwar->getFrameCount() < 4000
        && count(UnitTypes::Terran_Barracks) < 2 && pendingCount(UnitTypes::Terran_Barracks) == 0)
    {
        zergSwitchDone = true;
        plan = Plan::RaxPressure;
        build(UnitTypes::Terran_Barracks, TilePosition(home));
    }

    int supplyUsed = self->supplyUsed() / 2;
    int supplyTotal = self->supplyTotal() / 2;

    // The build order's steps, each from its supply count
    while (buildOrderStep < buildOrder.size() && supplyUsed >= buildOrder[buildOrderStep].supply)
    {
        const auto &step = buildOrder[buildOrderStep];
        // Skip the natural command center while a rush is being held: the army comes first
        if (step.type.isResourceDepot() && rushSeen && count(UnitTypes::Terran_Marine, false) < 6)
        {
            break;
        }
        TilePosition near = TilePosition(home);
        if (step.atNatural && natural) near = natural->depot;
        else if (step.atRamp) near = rampSpot(step.type);
        else if (step.type == UnitTypes::Terran_Supply_Depot) near = depotSpot();
        if (build(step.type, near))
        {
            buildOrderStep++;
        }
        else
        {
            break;  // no money or no builder: try again next time
        }
    }

    // Supply depots ahead of blocks: one in the works when free supply is low
    int supplyBlock = supplyTotal - supplyUsed;
    int depotsWanted = supplyBlock <= 4 + (int) buildOrderStep / 4 ? 1 : 0;
    if (depotsWanted > pendingCount(UnitTypes::Terran_Supply_Depot) && supplyTotal < 200
        && self->minerals() - reservedMinerals() >= 100)
    {
        build(UnitTypes::Terran_Supply_Depot, depotSpot());
    }

    // Refineries: one per finished command center once the build order's first is done
    if (refineryWanted() && pendingCount(UnitTypes::Terran_Refinery) == 0)
    {
        build(UnitTypes::Terran_Refinery, TilePosition(home));
    }

    // Bunkers: at the ramp when rushed, one per base against air harassment later
    if ((rushSeen || plan == Plan::Defensive) && count(UnitTypes::Terran_Bunker) < (int) commandCenters(true).size()
        && pendingCount(UnitTypes::Terran_Bunker) == 0)
    {
        build(UnitTypes::Terran_Bunker, rampSpot(UnitTypes::Terran_Bunker));
    }

    // Missile turrets: in the mineral lines when air or cloaked ground is about, at the ramp against drops
    bool turretsWanted = (airSeen || mutasSeen || wraithsSeen || dtSeen || lurkersSeen || dropsSeen)
                         && Broodwar->getFrameCount() > 5000;
    int turretsHave = count(UnitTypes::Terran_Missile_Turret);
    int turretsWant = turretsWanted ? (int) commandCenters(true).size() * 2 : 0;
    if (turretsHave < turretsWant && pendingCount(UnitTypes::Terran_Missile_Turret) < 2
        && self->minerals() - reservedMinerals() >= 75)
    {
        TilePosition near = TilePosition(mineralCenter);
        if (turretsHave % 2 == 1 && natural) near = TilePosition(natural->center);
        build(UnitTypes::Terran_Missile_Turret, near);
    }

    // Tech buildings past the opening: academy, factory, engineering bay, starport, science facility, armory
    using namespace UnitTypes;
    auto have = [&](UnitType t) { return count(t) > 0; };
    int frame = Broodwar->getFrameCount();
    if (!have(Terran_Academy) && pendingCount(Terran_Academy) == 0 && frame > 3000
        && self->minerals() - reservedMinerals() >= 150)
        build(Terran_Academy, TilePosition(home));
    if (!have(Terran_Factory) && pendingCount(Terran_Factory) == 0 && frame > 4000
        && self->minerals() - reservedMinerals() >= 200 && self->gas() - reservedGas() >= 100)
        build(Terran_Factory, TilePosition(home));
    if (!have(Terran_Engineering_Bay) && pendingCount(Terran_Engineering_Bay) == 0 && frame > 5000
        && self->minerals() - reservedMinerals() >= 125)
        build(Terran_Engineering_Bay, TilePosition(home));
    if (!have(Terran_Starport) && pendingCount(Terran_Starport) == 0 && frame > 7000
        && self->minerals() - reservedMinerals() >= 150 && self->gas() - reservedGas() >= 100)
        build(Terran_Starport, TilePosition(home));
    if (!have(Terran_Science_Facility) && pendingCount(Terran_Science_Facility) == 0 && frame > 9000
        && self->minerals() - reservedMinerals() >= 100 && self->gas() - reservedGas() >= 150)
        build(Terran_Science_Facility, TilePosition(home));
    if (!have(Terran_Armory) && pendingCount(Terran_Armory) == 0 && frame > 11000
        && self->minerals() - reservedMinerals() >= 100 && self->gas() - reservedGas() >= 50)
        build(Terran_Armory, TilePosition(home));

    // Addons: comsat on each command center (one at a time), machine shop on the first factory,
    // control tower on the starport, physics lab for battlecruisers vs carriers
    if (have(Terran_Academy) && count(Terran_Comsat_Station, false) < (int) commandCenters(true).size())
    {
        for (auto cc : commandCenters(true))
        {
            if (!cc->getAddon() && !pendingAddons.count(cc))
            {
                buildAddon(cc, Terran_Comsat_Station);
                break;
            }
        }
    }
    for (auto unit : self->getUnits())
    {
        if (!unit->isCompleted() || pendingAddons.count(unit) || unit->getAddon()) continue;
        auto type = unit->getType();
        if (type == Terran_Factory && !have(Terran_Machine_Shop) && frame > 5000)
            buildAddon(unit, Terran_Machine_Shop);
        else if (type == Terran_Starport && frame > 8000)
            buildAddon(unit, Terran_Control_Tower);
        else if (type == Terran_Science_Facility && carriersSeen && frame > 14000)
            buildAddon(unit, Terran_Physics_Lab);
    }

    // A second and third factory when the mineral bank grows and tanks are the answer
    if (frame > 9000 && have(Terran_Factory) && self->minerals() - reservedMinerals() > 600
        && count(Terran_Factory) < 3 && pendingCount(Terran_Factory) == 0)
    {
        build(Terran_Factory, TilePosition(home));
    }
    // Extra barracks for marine production when floating minerals
    if (frame > 7000 && have(Terran_Barracks) && self->minerals() - reservedMinerals() > 500
        && count(Terran_Barracks) < 5 && pendingCount(Terran_Barracks) == 0)
    {
        build(Terran_Barracks, TilePosition(home));
    }
}

// A second refinery's gas once the first is saturated and tech wants gas
bool SparkTerran::refineryWanted() const
{
    auto self = Broodwar->self();
    int refineries = self->completedUnitCount(UnitTypes::Terran_Refinery);
    if (refineries == 0) return Broodwar->getFrameCount() > 2500;
    if (refineries >= (int) commandCenters(true).size()) return false;
    return self->gas() < 100 && Broodwar->getFrameCount() > 6000;
}

void SparkTerran::researchUpgrades()
{
    auto self = Broodwar->self();
    using namespace UnitTypes;
    using namespace TechTypes;
    using namespace UpgradeTypes;

    // Find finished buildings of a type
    auto finished = [&](UnitType t) -> Unit
    {
        for (auto u : self->getUnits())
            if (u->getType() == t && u->isCompleted()) return u;
        return nullptr;
    };

    if (auto a = finished(Terran_Academy))
    {
        if (!self->hasResearched(Stim_Packs) && !self->isResearching(Stim_Packs) && self->minerals() >= 100
            && self->gas() >= 100)
            a->research(Stim_Packs);
        else if (self->getUpgradeLevel(U_238_Shells) == 0 && !self->isUpgrading(U_238_Shells) && self->minerals() >= 150
                 && self->gas() >= 150)
            a->upgrade(U_238_Shells);
        else if (self->getUpgradeLevel(Caduceus_Reactor) == 0 && !self->isUpgrading(Caduceus_Reactor)
                 && self->minerals() >= 150 && self->gas() >= 150)
            a->upgrade(Caduceus_Reactor);
    }
    if (auto ms = finished(Terran_Machine_Shop))
    {
        if (!self->hasResearched(Tank_Siege_Mode) && !self->isResearching(Tank_Siege_Mode) && self->minerals() >= 150
            && self->gas() >= 150)
            ms->research(Tank_Siege_Mode);
        else if ((carriersSeen || wraithsSeen || mutasSeen) && self->getUpgradeLevel(Charon_Boosters) == 0
                 && !self->isUpgrading(Charon_Boosters) && self->minerals() >= 150 && self->gas() >= 150)
            ms->upgrade(Charon_Boosters);
        else if (self->getUpgradeLevel(Ion_Thrusters) == 0 && !self->isUpgrading(Ion_Thrusters) && self->minerals() >= 100
                 && self->gas() >= 100)
            ms->upgrade(Ion_Thrusters);
    }
    if (auto sf = finished(Terran_Science_Facility))
    {
        if (!self->hasResearched(EMP_Shockwave) && !self->isResearching(EMP_Shockwave) && self->minerals() >= 200
            && self->gas() >= 200)
            sf->research(EMP_Shockwave);
        else if (enemyRace == Races::Zerg && !self->hasResearched(Irradiate) && !self->isResearching(Irradiate)
                 && self->minerals() >= 200 && self->gas() >= 200)
            sf->research(Irradiate);
        else if (self->getUpgradeLevel(Titan_Reactor) == 0 && !self->isUpgrading(Titan_Reactor) && self->minerals() >= 150
                 && self->gas() >= 150)
            sf->upgrade(Titan_Reactor);
    }
    if (auto eb = finished(Terran_Engineering_Bay))
    {
        // Infantry weapons first, then armor; vehicle weapons when tanks are the army (TvT)
        int infW = self->getUpgradeLevel(Terran_Infantry_Weapons);
        int infA = self->getUpgradeLevel(Terran_Infantry_Armor);
        int vehW = self->getUpgradeLevel(Terran_Vehicle_Weapons);
        bool wantVeh = plan == Plan::Defensive || tanksSeen;
        if (!self->isUpgrading(Terran_Infantry_Weapons) && !self->isUpgrading(Terran_Infantry_Armor)
            && !self->isUpgrading(Terran_Vehicle_Weapons))
        {
            if (wantVeh && vehW <= infW && self->minerals() >= 100 + vehW * 75 && self->gas() >= 100 + vehW * 75)
                eb->upgrade(Terran_Vehicle_Weapons);
            else if (infW <= infA && self->minerals() >= 100 + infW * 75 && self->gas() >= 100 + infW * 75)
                eb->upgrade(Terran_Infantry_Weapons);
            else if (self->minerals() >= 100 + infA * 75 && self->gas() >= 100 + infA * 75)
                eb->upgrade(Terran_Infantry_Armor);
        }
    }
}

void SparkTerran::trainUnits()
{
    auto self = Broodwar->self();
    using namespace UnitTypes;
    int frame = Broodwar->getFrameCount();

    int scvs = self->allUnitCount(Terran_SCV);
    int ccs = (int) commandCenters(true).size();
    int scvTarget = std::min(MaxSCVs, SCVsPerBase * std::max(1, ccs));

    int marines = self->allUnitCount(Terran_Marine);
    int medics = self->allUnitCount(Terran_Medic);
    int firebats = self->allUnitCount(Terran_Firebat);
    int tanks = self->allUnitCount(Terran_Siege_Tank_Tank_Mode) + self->allUnitCount(Terran_Siege_Tank_Siege_Mode);
    int goliaths = self->allUnitCount(Terran_Goliath);
    int vessels = self->allUnitCount(Terran_Science_Vessel);

    for (auto unit : self->getUnits())
    {
        if (!unit->isCompleted()) continue;
        auto type = unit->getType();
        if (unit->getTrainingQueue().size() >= 2) continue;

        if (type == Terran_Command_Center)
        {
            if (scvs < scvTarget && self->minerals() >= 50) unit->train(Terran_SCV);
        }
        else if (type == Terran_Barracks)
        {
            if (self->minerals() < 50) continue;
            bool wantMedic = self->completedUnitCount(Terran_Academy) > 0 && medics * 4 < marines && medics < 8;
            bool wantFirebat = enemyRace == Races::Zerg && firebats < 4 && marines > 8;
            if (wantFirebat && self->gas() >= 25) unit->train(Terran_Firebat);
            else if (wantMedic && self->minerals() >= 50 && self->gas() >= 25) unit->train(Terran_Medic);
            else unit->train(Terran_Marine);
        }
        else if (type == Terran_Factory)
        {
            if (self->minerals() < 150 || self->gas() < 100) continue;
            // Goliaths when the enemy has air; otherwise tanks
            bool wantGoliath = (mutasSeen || wraithsSeen || carriersSeen) && goliaths < tanks + 4;
            auto addon = unit->getAddon();
            if (wantGoliath && addon && addon->isCompleted() && addon->getType() == Terran_Machine_Shop)
                unit->train(Terran_Goliath);
            else unit->train(Terran_Siege_Tank_Tank_Mode);
        }
        else if (type == Terran_Starport)
        {
            bool wantVessel = self->completedUnitCount(Terran_Science_Facility) > 0 && vessels < 3
                              && unit->getAddon() && unit->getAddon()->getType() == Terran_Control_Tower;
            bool wantBC = carriersSeen && frame > 16000 && self->minerals() >= 400 && self->gas() >= 300;
            if (wantVessel && self->minerals() >= 100 && self->gas() >= 225) unit->train(Terran_Science_Vessel);
            else if (wantBC) unit->train(Terran_Battlecruiser);
            else if (wraithsSeen && self->minerals() >= 150 && self->gas() >= 100) unit->train(Terran_Wraith);
        }
    }

    // Rally new army units to the rally point
    for (auto unit : self->getUnits())
    {
        auto type = unit->getType();
        if ((type == Terran_Barracks || type == Terran_Factory || type == Terran_Starport) && unit->isCompleted())
        {
            if (unit->getRallyPosition() != rally) unit->setRallyPoint(rally);
        }
    }
}

// Take the natural when the money is there and the army can hold it; a third when two bases are going
void SparkTerran::tryExpand()
{
    auto self = Broodwar->self();
    int ccs = count(UnitTypes::Terran_Command_Center);
    if (ccs == 0) return;
    int frame = Broodwar->getFrameCount();

    bool money = self->minerals() - reservedMinerals() >= 400;
    bool safe = !rushSeen || self->allUnitCount(UnitTypes::Terran_Marine) >= 6;
    bool armyOk = self->allUnitCount(UnitTypes::Terran_Marine) + self->allUnitCount(UnitTypes::Terran_Firebat)
                  >= (frame < 8000 ? 8 : 12);

    if (ccs < 2 && money && safe && frame > 3500)
    {
        if (natural) build(UnitTypes::Terran_Command_Center, natural->depot);
        return;
    }
    if (ccs < 3 && money && self->minerals() - reservedMinerals() >= 500 && armyOk && frame > 12000)
    {
        for (auto &base : bases)
        {
            if (base.center.getApproxDistance(home) < 400) continue;
            if (natural && base.center == natural->center) continue;
            if (!Broodwar->hasPath(home, base.center)) continue;
            if (!Broodwar->getUnitsInRadius(Position(base.depot) + Position(64, 48), 160, IsResourceDepot).empty())
                continue;
            bool enemyThere = false;
            for (auto &[id, building] : enemyBuildings)
            {
                if (building.second.getApproxDistance(base.center) < 400) enemyThere = true;
            }
            if (enemyThere) continue;
            build(UnitTypes::Terran_Command_Center, base.depot);
            return;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Workers

void SparkTerran::manageWorkers()
{
    if (commandCenters(true).empty()) return;
    int frame = Broodwar->getFrameCount();

    // Builders whose building has started (or that gave up) go back to mining. Terran SCVs build until the
    // building is done, so the builder stays assigned until it is completed
    for (auto it = builders.begin(); it != builders.end();)
    {
        auto builder = it->first;
        auto &pending = it->second;
        bool done = false;
        bool started = false;
        for (auto unit : Broodwar->getUnitsOnTile(pending.tile, IsOwned))
        {
            if (unit->getType() == pending.type)
            {
                started = true;
                if (unit->isCompleted()) done = true;
            }
        }
        if (done || !builder->exists() || frame - pending.frame > 1500)
        {
            // A dead or stuck builder on an unfinished building: hand it to a fresh SCV so it resumes
            bool resume = started && !done;
            if (builder->exists() && !done) builder->stop();
            it = builders.erase(it);
            if (resume)
            {
                auto replacement = chooseBuilder(Position(pending.tile));
                if (replacement)
                {
                    builders[replacement] = PendingBuild{pending.type, pending.tile, frame};
                    replacement->build(pending.type, pending.tile);
                }
            }
            continue;
        }
        if (!started && builder->exists() && !builder->isConstructing()
            && builder->getOrder() != Orders::PlaceBuilding)
        {
            if (Broodwar->canBuildHere(pending.tile, pending.type, builder, true))
            {
                if (!builder->build(pending.type, pending.tile))
                    builder->move(Position(pending.tile) + Position(32, 32));
            }
            else if (!Broodwar->isExplored(pending.tile))
            {
                builder->move(Position(pending.tile) + Position(64, 48));
            }
            else
            {
                // Something is in the way: the nearest free spot close by, or give it up
                auto spot = findBuildSpot(pending.type, pending.tile);
                if (pending.type.isResourceDepot())
                {
                    if (builder->getDistance(Position(pending.tile)) > 96)
                        builder->move(Position(pending.tile) + Position(64, 48));
                    ++it;
                    continue;
                }
                if (!spot.isValid() || spot.getApproxDistance(pending.tile) > 6)
                {
                    it = builders.erase(it);
                    continue;
                }
                pending.tile = spot;
                if (!builder->build(pending.type, spot)) builder->move(Position(spot) + Position(32, 32));
            }
        }
        ++it;
    }

    // Addons that finished are forgotten; ones stuck for over 2 minutes are retried
    for (auto it = pendingAddons.begin(); it != pendingAddons.end();)
    {
        auto building = it->first;
        if (!building->exists() || building->getAddon() != nullptr)
        {
            it = pendingAddons.erase(it);
            continue;
        }
        ++it;
    }

    // Three SCVs on each finished refinery; pull extras back to minerals when gas banks up
    for (auto gas : Broodwar->self()->getUnits())
    {
        if (gas->getType() != UnitTypes::Terran_Refinery || !gas->isCompleted()) continue;
        auto self = Broodwar->self();
        int wantOnGas = self->gas() > 300 ? 2 : 3;
        int onGas = (int) gas->getUnitsInRadius(320, IsOwned && IsWorker && IsGatheringGas).size();
        for (auto scv : gas->getUnitsInRadius(320, IsOwned && IsWorker && IsGatheringGas && !IsCarryingGas))
        {
            if (onGas <= wantOnGas) break;
            auto cc = ccNeedingWorkers();
            auto mineral = cc ? cc->getClosestUnit(IsMineralField, 400) : nullptr;
            if (!mineral) break;
            scv->gather(mineral);
            onGas--;
        }
        for (auto scv : gas->getUnitsInRadius(400, IsOwned && IsWorker && IsGatheringMinerals && !IsCarryingMinerals))
        {
            if (onGas >= wantOnGas) break;
            if (scv == scout || builders.count(scv)) continue;
            scv->gather(gas);
            onGas++;
        }
    }

    // Idle SCVs mine at the base that needs them most, each on the patch there with the fewest SCVs
    std::map<Unit, int> onPatch;
    for (auto scv : Broodwar->self()->getUnits())
    {
        auto target = scv->getOrderTarget();
        if (scv->getType().isWorker() && scv->isGatheringMinerals() && target && target->getType().isMineralField())
        {
            onPatch[target]++;
        }
    }
    for (auto scv : Broodwar->self()->getUnits())
    {
        if (!scv->getType().isWorker() || !scv->isCompleted() || scv == scout || builders.count(scv)) continue;
        if (!scv->isIdle()) continue;
        if (scv->isCarryingMinerals() || scv->isCarryingGas())
        {
            scv->returnCargo();
            continue;
        }
        auto cc = ccNeedingWorkers();
        if (!cc) continue;
        Unit mineral = nullptr;
        int bestScore = INT_MAX;
        for (auto patch : cc->getUnitsInRadius(320, IsMineralField))
        {
            int score = onPatch[patch] * 1000 + cc->getDistance(patch);
            if (score < bestScore)
            {
                mineral = patch;
                bestScore = score;
            }
        }
        if (!mineral) mineral = cc->getClosestUnit(IsMineralField, 400);
        if (mineral)
        {
            scv->gather(mineral);
            onPatch[mineral]++;
        }
    }

    // Every 10 seconds, move mineral SCVs from an oversaturated base to one that needs them
    if (frame % 240 == 0)
    {
        auto target = ccNeedingWorkers();
        if (target)
        {
            int targetPatches = (int) target->getUnitsInRadius(320, IsMineralField).size();
            int targetWorkers = (int) target->getUnitsInRadius(320, IsOwned && IsWorker && IsGatheringMinerals).size();
            int wanted = std::max(0, targetPatches * 2 - targetWorkers);
            for (auto cc : commandCenters(true))
            {
                if (cc == target || wanted <= 0) continue;
                int patches = (int) cc->getUnitsInRadius(320, IsMineralField).size();
                auto workers = cc->getUnitsInRadius(320, IsOwned && IsWorker && IsGatheringMinerals);
                int excess = (int) workers.size() - patches * 2;
                auto mineral = target->getClosestUnit(IsMineralField, 400);
                for (auto scv : workers)
                {
                    if (excess <= 0 || wanted <= 0 || !mineral) break;
                    if (scv == scout || builders.count(scv) || scv->isCarryingMinerals()) continue;
                    scv->gather(mineral);
                    excess--;
                    wanted--;
                }
            }
        }
    }

    // Repair: an SCV keeps the bunker (and damaged tanks nearby) alive while fighting
    if (frame % 48 == 0)
    {
        for (auto bunker : Broodwar->self()->getUnits())
        {
            if (bunker->getType() != UnitTypes::Terran_Bunker || !bunker->isCompleted()) continue;
            if (bunker->getHitPoints() >= bunker->getType().maxHitPoints()) continue;
            if (!threatsNear(bunker->getPosition(), 400).empty()) continue;  // too hot for the SCV
            auto scv = chooseBuilder(bunker->getPosition());
            if (scv) scv->repair(bunker);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Scouting and information

void SparkTerran::scoutEnemy()
{
    int frame = Broodwar->getFrameCount();
    auto self = Broodwar->self();

    // The SCV scout leaves at 8 supply, checks the starts nearest first, then watches the enemy's natural
    if (!scout && !scoutDone && self->supplyUsed() / 2 >= ScoutSupply)
    {
        scout = chooseBuilder(home);
    }
    if (scout && (!scout->exists() || frame > 20000))
    {
        scout = nullptr;
        scoutDone = true;
    }
    if (!scout || !scout->exists()) return;

    // Found the enemy base: learn the race, then circle the natural for a while
    if (enemyBase == Positions::Unknown)
    {
        for (auto target : scoutTargets)
        {
            if (scout->getDistance(Position(target)) > 400) continue;
            for (auto unit : Broodwar->getUnitsInRadius(Position(target), 600, IsEnemy))
            {
                if (unit->getType().isResourceDepot() || unit->getType().isWorker())
                {
                    enemyBase = unit->getPosition();
                    enemyRace = Broodwar->enemy()->getRace();
                    if (getenv("ST_DEBUG")) printf("     SCOUT found %s at %d\n", enemyRace.c_str(), frame);
                    break;
                }
            }
            if (enemyBase != Positions::Unknown) break;
        }
    }
    if (enemyBase != Positions::Unknown)
    {
        // Note the enemy's tech buildings while inside
        for (auto unit : Broodwar->getUnitsInRadius(scout->getPosition(), 500, IsEnemy && IsBuilding))
        {
            auto type = unit->getType();
            enemyBuildings[unit->getID()] = {type, unit->getPosition()};
        }
        // Circle at the edge of the base; run from anything that shoots
        auto near = scout->getUnitsInRadius(300, IsEnemy && IsVisible);
        bool danger = false;
        for (auto e : near)
            if (e->getType().canAttack() && !e->getType().isWorker()) danger = true;
        if (danger)
        {
            scout->move(towards(scout->getPosition(), enemyBase, -300));
        }
        else if (scout->getDistance(enemyBase) > 300)
        {
            scout->move(towards(enemyBase, scout->getPosition(), 300));
        }
        else if (scout->isIdle())
        {
            Position circle = enemyBase + Position(int(300 * std::cos(frame / 40.0)), int(300 * std::sin(frame / 40.0)));
            scout->move(circle.makeValid());
        }
        if (frame > 12000)
        {
            auto mineral = mainCC ? mainCC->getClosestUnit(IsMineralField, 400) : nullptr;
            if (mineral) scout->gather(mineral);
            else scout->stop();
            scout = nullptr;
            scoutDone = true;
        }
        return;
    }

    // Still searching: the next unscouted start
    for (auto target : scoutTargets)
    {
        if (scout->getDistance(Position(target)) < 200) continue;
        if (scout->getOrder() != Orders::Move || scout->getOrderTarget() == nullptr)
            scout->move(Position(target) + Position(64, 48));
        break;
    }
}

void SparkTerran::trackEnemy()
{
    int frame = Broodwar->getFrameCount();
    using namespace UnitTypes;

    for (auto unit : Broodwar->enemy()->getUnits())
    {
        if (!unit->isVisible()) continue;
        auto type = unit->getType();
        auto pos = unit->getPosition();
        if (type.isBuilding())
        {
            enemyBuildings[unit->getID()] = {type, pos};
            // A proxy: enemy buildings uncomfortably close to our main
            if (pos.getApproxDistance(home) < 1200 && frame < 8000
                && (type == Zerg_Spawning_Pool || type == Terran_Barracks || type == Protoss_Gateway
                    || type == Protoss_Pylon || type == Protoss_Photon_Cannon || type == Zerg_Creep_Colony))
            {
                proxySeen = true;
                rushSeen = true;
            }
            if (type.isResourceDepot() && pos.getApproxDistance(home) > 1500) feSeen = true;
        }
        else if (isArmyUnit(type) || type.isWorker())
        {
            enemyArmy[unit->getID()] = {type, unit->getHitPoints() + unit->getShields(), frame, pos};
        }

        // Conclusions from what is seen
        if (type == Zerg_Spawning_Pool && frame < 3500) rushSeen = true;
        if (type == Zerg_Zergling && frame < 4500 && pos.getApproxDistance(home) < 1500) rushSeen = true;
        if (type == Terran_Barracks && frame < 3500 && pos.getApproxDistance(home) < 1500) rushSeen = true;
        if (type == Protoss_Gateway && frame < 3500 && pos.getApproxDistance(home) < 1500) rushSeen = true;
        if (type == Protoss_Zealot && frame < 5000 && pos.getApproxDistance(home) < 1500) rushSeen = true;
        if (type == Zerg_Spire || type == Zerg_Mutalisk) { airSeen = true; mutasSeen = true; }
        if (type == Terran_Starport || type == Terran_Wraith) { airSeen = true; wraithsSeen = true; }
        if (type == Protoss_Stargate || type == Protoss_Corsair || type == Protoss_Scout) airSeen = true;
        if (type == Zerg_Lurker || type == Zerg_Lurker_Egg) { cloakSeen = true; lurkersSeen = true; }
        if (type == Protoss_Dark_Templar) { cloakSeen = true; dtSeen = true; }
        if (type == Terran_Wraith && unit->isCloaked()) cloakSeen = true;
        if (type == Terran_Siege_Tank_Tank_Mode || type == Terran_Siege_Tank_Siege_Mode) tanksSeen = true;
        if (type == Protoss_Reaver) reaversSeen = true;
        if (type == Protoss_Carrier) { carriersSeen = true; airSeen = true; }
        if (type == Terran_Vulture || type == Terran_Goliath) mechSeen = true;
        if (type == Protoss_Shuttle || type == Terran_Dropship) dropsSeen = true;
        if (type == Protoss_Templar_Archives) cloakSeen = true;  // storm or archons coming
    }

    // Forget enemy buildings whose tile we can see and that are gone
    for (auto it = enemyBuildings.begin(); it != enemyBuildings.end();)
    {
        auto tile = TilePosition(it->second.second);
        if (tile.isValid() && Broodwar->isVisible(tile) && Broodwar->getUnitsOnTile(tile).empty())
            it = enemyBuildings.erase(it);
        else
            ++it;
    }
    // Forget army units not seen for 3 minutes
    for (auto it = enemyArmy.begin(); it != enemyArmy.end();)
    {
        if (frame - it->second.frame > 4320) it = enemyArmy.erase(it);
        else ++it;
    }

    // Worker rush: several enemy workers in our main early
    if (frame < 5000 && !rushSeen)
    {
        int workers = 0;
        for (auto unit : Broodwar->enemy()->getUnits())
        {
            if (unit->getType().isWorker() && unit->getDistance(home) < 600) workers++;
        }
        if (workers >= 3) rushSeen = true;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Army

// Fighting power of one unit: durability times damage per frame, from its own stats
double SparkTerran::unitPower(Unit unit)
{
    auto type = unit->getType();
    double durability = unit->getHitPoints() + unit->getShields();
    if (durability <= 0) durability = type.maxHitPoints();
    double dps = 0;
    for (auto weapon : {type.groundWeapon(), type.airWeapon()})
    {
        if (weapon == WeaponTypes::None) continue;
        double cooldown = std::max(1, weapon.damageCooldown());
        double damage = weapon.damageAmount() * weapon.damageFactor();
        dps = std::max(dps, damage / cooldown);
    }
    if (type == UnitTypes::Terran_Medic) dps = 2.0;  // medics keep marines alive: worth about this much
    if (type == UnitTypes::Terran_Science_Vessel) dps = 3.0;
    if (type.isWorker()) dps = 0.2;
    return durability * (0.5 + dps);
}

double SparkTerran::groupPower(const Unitset &units)
{
    double power = 0;
    for (auto unit : units)
        if (unit->exists() && isCombat(unit)) power += unitPower(unit);
    return power;
}

// The same scale for an enemy seen earlier, from its type and last known health
double SparkTerran::seenPower(UnitType type, int health)
{
    double durability = health > 0 ? health : type.maxHitPoints();
    double dps = 0;
    for (auto weapon : {type.groundWeapon(), type.airWeapon()})
    {
        if (weapon == WeaponTypes::None) continue;
        double cooldown = std::max(1, weapon.damageCooldown());
        dps = std::max(dps, double(weapon.damageAmount() * weapon.damageFactor()) / cooldown);
    }
    if (type.isWorker()) dps = 0.2;
    return durability * (0.5 + dps);
}

Unitset SparkTerran::threatsNear(Position where, int radius) const
{
    Unitset threats;
    for (auto unit : Broodwar->enemy()->getUnits())
    {
        if (!unit->isVisible() || unit->getType().isBuilding()) continue;
        if (!hostile(unit)) continue;
        if (unit->getDistance(where) < radius) threats.insert(unit);
    }
    return threats;
}

// The attack judgment: gather until the army is big enough, attack when it outpowers what is known near the
// target, retreat when it doesn't, and never leave the bases undefended
void SparkTerran::judgeAttack(const Unitset &army)
{
    int frame = Broodwar->getFrameCount();
    int armySupply = 0;
    for (auto unit : army)
        if (isCombat(unit)) armySupply += unit->getType().supplyRequired() / 2;

    // The target: the enemy base, else the last known enemy building, else the next unscouted start
    if (enemyBase != Positions::Unknown) attackTarget = enemyBase;
    else if (!enemyBuildings.empty()) attackTarget = enemyBuildings.begin()->second.second;
    else
    {
        for (auto target : scoutTargets)
        {
            if (!Broodwar->isExplored(target))
            {
                attackTarget = Position(target) + Position(64, 48);
                break;
            }
        }
    }

    // What the enemy has near the target: seen units plus a guess from their buildings
    double enemyPower = 0;
    for (auto &[id, seen] : enemyArmy)
    {
        if (frame - seen.frame > 2000) continue;
        if (seen.pos.getApproxDistance(attackTarget) < 1200) enemyPower += seenPower(seen.type, seen.health);
    }
    double ourPower = groupPower(army);

    if (!attacking)
    {
        int want = wave == 0 ? FirstAttackArmy : LaterAttackArmy;
        if (armySupply >= want && ourPower > enemyPower * AttackRatio && frame - lastRetreatFrame > 1200)
        {
            attacking = true;
            wave++;
            if (getenv("ST_DEBUG")) printf("     ATTACK wave %d at %d\n", wave, frame);
        }
    }
    else
    {
        if (armySupply < RetreatBelow || ourPower * RetreatRatio < enemyPower || frame - lastRetreatFrame < 0)
        {
            attacking = false;
            lastRetreatFrame = frame;
            if (getenv("ST_DEBUG")) printf("     RETREAT at %d\n", frame);
        }
    }
}

// One unit's fighting: pick the best target in reach, stim and siege handled by the unit controllers; with a
// leash, only enemies near the anchor (or already in weapon range) are taken on, and a unit beyond it with
// nothing in range walks back
void SparkTerran::fight(Unit unit, Position goal, Position anchor, int leash)
{
    auto type = unit->getType();
    int range = std::max(type.groundWeapon().maxRange(), type.airWeapon().maxRange());

    Unit best = nullptr;
    int bestScore = INT_MIN;
    int sight = type.sightRange() + 160;
    for (auto enemy : Broodwar->enemy()->getUnits())
    {
        if (!enemy->isVisible() || !enemy->exists()) continue;
        if (enemy->isCloaked() && !enemy->isDetected()) continue;
        if (enemy->getType().isBuilding() && !hostile(enemy) && unit->getDistance(goal) > 800) continue;
        int distance = unit->getDistance(enemy);
        if (distance > sight) continue;
        if (leash > 0 && anchor.isValid() && anchor.getApproxDistance(enemy->getPosition()) > leash
            && distance > range + 64)
            continue;
        int score = targetPriority(enemy) * 10000 - (enemy->getHitPoints() + enemy->getShields()) - distance;
        // Don't overkill: skip targets others have already covered, unless nothing else is in reach
        auto aimed = aimedDamage.find(enemy);
        if (aimed != aimedDamage.end() && aimed->second > enemy->getHitPoints() + enemy->getShields()
            && bestScore > INT_MIN)
            score -= 50000;
        if (score > bestScore)
        {
            bestScore = score;
            best = enemy;
        }
    }

    if (best)
    {
        if (unit->getOrderTarget() != best || unit->getOrder() != Orders::AttackUnit) unit->attack(best);
        // Don't pile every shot onto one target: track roughly one volley's damage per aimer
        int shot = 1;
        for (auto weapon : {type.groundWeapon(), type.airWeapon()})
        {
            if (weapon == WeaponTypes::None) continue;
            shot = std::max(shot, weapon.damageAmount() * weapon.damageFactor());
        }
        aimedDamage[best] += shot;
    }
    else if (leash > 0 && anchor.isValid() && unit->getDistance(anchor) > leash)
    {
        if (unit->getOrder() != Orders::Move) unit->move(anchor);
    }
    else
    {
        if (unit->getOrderTarget() != nullptr || unit->getOrder() != Orders::AttackUnit) unit->attack(goal);
    }
}

// Marines: stim before contact when researched, focus fire, keep moving toward the goal
void SparkTerran::controlMarines(const Unitset &army)
{
    bool stimmed = Broodwar->self()->hasResearched(TechTypes::Stim_Packs);
    for (auto unit : army)
    {
        auto type = unit->getType();
        if (type != UnitTypes::Terran_Marine && type != UnitTypes::Terran_Firebat) continue;
        if (stimmed && !unit->isStimmed() && unit->getHitPoints() > 20)
        {
            auto near = unit->getUnitsInRadius(320, IsEnemy && IsVisible);
            if (!near.empty()) unit->useTech(TechTypes::Stim_Packs);
        }
        Position goal = attacking ? attackTarget : rally;
        Position anchor = attacking ? Positions::None : rally;
        fight(unit, goal, anchor, attacking ? 0 : 500);
    }
}

// Tanks: siege when the enemy ground is in range, unsiege to move with the push, leapfrog on the attack
void SparkTerran::controlTanks(const Unitset &army)
{
    bool siegeMode = Broodwar->self()->hasResearched(TechTypes::Tank_Siege_Mode);
    for (auto unit : army)
    {
        auto type = unit->getType();
        if (type != UnitTypes::Terran_Siege_Tank_Tank_Mode && type != UnitTypes::Terran_Siege_Tank_Siege_Mode) continue;
        auto enemies = unit->getUnitsInRadius(400, IsEnemy && IsVisible && !IsFlying);
        bool groundNear = false;
        for (auto e : enemies)
            if (!e->getType().isBuilding() || hostile(e)) groundNear = true;

        if (siegeMode && !unit->isSieged() && groundNear)
        {
            unit->siege();
            continue;
        }
        if (unit->isSieged())
        {
            if (attacking && enemies.empty() && unit->getDistance(attackTarget) > 700)
            {
                unit->unsiege();  // leapfrog: move up with the push
                continue;
            }
            // Sieged tanks shoot what comes in range
            Unit best = nullptr;
            int bestScore = INT_MIN;
            for (auto enemy : Broodwar->enemy()->getUnits())
            {
                if (!enemy->isVisible() || enemy->getType().isFlyer()) continue;
                if (enemy->isCloaked() && !enemy->isDetected()) continue;
                int distance = unit->getDistance(enemy);
                if (distance > 384 || distance < 64) continue;
                int score = targetPriority(enemy) * 10000 - (enemy->getHitPoints() + enemy->getShields()) - distance;
                if (score > bestScore)
                {
                    bestScore = score;
                    best = enemy;
                }
            }
            if (best && unit->getOrderTarget() != best) unit->attack(best);
            continue;
        }
        Position goal = attacking ? attackTarget : rally;
        Position anchor = attacking ? Positions::None : rally;
        fight(unit, goal, anchor, attacking ? 0 : 500);
    }
}

// Medics: stay behind the marines, never lead; healing is automatic when they are close and idle
void SparkTerran::controlMedics(const Unitset &army)
{
    Position center(0, 0);
    int marines = 0;
    for (auto unit : army)
    {
        if (unit->getType() == UnitTypes::Terran_Marine && unit->exists())
        {
            center += unit->getPosition();
            marines++;
        }
    }
    for (auto unit : army)
    {
        if (unit->getType() != UnitTypes::Terran_Medic) continue;
        // A hurt marine nearby: go heal it
        Unit patient = nullptr;
        int worst = INT_MAX;
        for (auto other : army)
        {
            if (other->getType() != UnitTypes::Terran_Marine || !other->exists()) continue;
            int missing = other->getType().maxHitPoints() - other->getHitPoints();
            if (missing > 10 && unit->getDistance(other) < 320 && missing < worst)
            {
                worst = missing;
                patient = other;
            }
        }
        if (patient)
        {
            if (unit->getOrderTarget() != patient) unit->move(patient->getPosition());
            continue;
        }
        // Otherwise stay a little behind the marines
        Position goal;
        if (marines > 0)
        {
            Position avg(center.x / marines, center.y / marines);
            Position back = attacking ? attackTarget : rally;
            goal = towards(avg, back, -128);
        }
        else
        {
            goal = attacking ? attackTarget : rally;
        }
        if (unit->getDistance(goal) > 96 && unit->getOrder() != Orders::Move) unit->move(goal);
    }
}

// Science vessels: stay far back, EMP Protoss shields, irradiate Zerg masses, matrix hurt tanks
void SparkTerran::controlVessels(const Unitset &army)
{
    auto self = Broodwar->self();
    bool emp = self->hasResearched(TechTypes::EMP_Shockwave);
    bool irradiate = self->hasResearched(TechTypes::Irradiate);
    for (auto unit : army)
    {
        if (unit->getType() != UnitTypes::Terran_Science_Vessel) continue;
        bool cast = false;
        if (unit->getEnergy() >= 100)
        {
            if (emp && enemyRace == Races::Protoss)
            {
                // The densest clump of shielded enemies
                Unit best = nullptr;
                int bestCount = 3;
                for (auto enemy : Broodwar->enemy()->getUnits())
                {
                    if (!enemy->isVisible() || enemy->getType().isBuilding()) continue;
                    if (enemy->getShields() < 50) continue;
                    int near = (int) enemy->getUnitsInRadius(128, IsEnemy && IsVisible).size();
                    if (near > bestCount && unit->getDistance(enemy) < 320)
                    {
                        bestCount = near;
                        best = enemy;
                    }
                }
                if (best)
                {
                    unit->useTech(TechTypes::EMP_Shockwave, best->getPosition());
                    cast = true;
                }
            }
            if (!cast && irradiate && enemyRace == Races::Zerg)
            {
                Unit best = nullptr;
                int bestHP = 200;
                for (auto enemy : Broodwar->enemy()->getUnits())
                {
                    if (!enemy->isVisible() || !enemy->getType().isOrganic() || enemy->getType().isBuilding()) continue;
                    if (unit->getDistance(enemy) > 320) continue;
                    int hp = enemy->getHitPoints();
                    if (hp > bestHP)
                    {
                        bestHP = hp;
                        best = enemy;
                    }
                }
                if (best)
                {
                    unit->useTech(TechTypes::Irradiate, best);
                    cast = true;
                }
            }
        }
        if (!cast && unit->getEnergy() >= 100)
        {
            // Defense matrix on the most hurt sieged tank nearby
            Unit hurt = nullptr;
            int worst = INT_MAX;
            for (auto other : army)
            {
                if (other->getType() != UnitTypes::Terran_Siege_Tank_Siege_Mode || !other->exists()) continue;
                int missing = other->getType().maxHitPoints() - other->getHitPoints();
                if (missing > 60 && missing < worst && unit->getDistance(other) < 320)
                {
                    worst = missing;
                    hurt = other;
                }
            }
            if (hurt)
            {
                unit->useTech(TechTypes::Defensive_Matrix, hurt);
                cast = true;
            }
        }
        if (cast) continue;
        // Keep well behind the army
        Position anchor = attacking ? attackTarget : rally;
        Position back = towards(unit->getPosition(), anchor, -256);
        if (unit->getDistance(back) > 96) unit->move(back.makeValid());
    }
}

// Goliaths: shoot air first, stay with the tanks otherwise
void SparkTerran::controlGoliaths(const Unitset &army)
{
    for (auto unit : army)
    {
        if (unit->getType() != UnitTypes::Terran_Goliath) continue;
        Unit air = nullptr;
        int bestScore = INT_MIN;
        for (auto enemy : unit->getUnitsInRadius(unit->getType().sightRange() + 96, IsEnemy && IsVisible && IsFlying))
        {
            if (enemy->isCloaked() && !enemy->isDetected()) continue;
            int score = targetPriority(enemy) * 10000 - (enemy->getHitPoints() + enemy->getShields())
                        - unit->getDistance(enemy);
            if (score > bestScore)
            {
                bestScore = score;
                air = enemy;
            }
        }
        if (air)
        {
            if (unit->getOrderTarget() != air) unit->attack(air);
            continue;
        }
        Position goal = attacking ? attackTarget : rally;
        Position anchor = attacking ? Positions::None : rally;
        fight(unit, goal, anchor, attacking ? 0 : 500);
    }
}

// Wraiths and battlecruisers: simple attack-move with the army
void SparkTerran::controlAir(const Unitset &army)
{
    for (auto unit : army)
    {
        auto type = unit->getType();
        if (type != UnitTypes::Terran_Wraith && type != UnitTypes::Terran_Battlecruiser
            && type != UnitTypes::Terran_Valkyrie)
            continue;
        Unit best = nullptr;
        int bestScore = INT_MIN;
        for (auto enemy : unit->getUnitsInRadius(type.sightRange() + 96, IsEnemy && IsVisible))
        {
            if (enemy->isCloaked() && !enemy->isDetected()) continue;
            int score = targetPriority(enemy) * 10000 - (enemy->getHitPoints() + enemy->getShields())
                        - unit->getDistance(enemy);
            if (score > bestScore)
            {
                bestScore = score;
                best = enemy;
            }
        }
        if (best)
        {
            if (unit->getOrderTarget() != best) unit->attack(best);
        }
        else if (unit->getOrder() != Orders::AttackUnit)
        {
            unit->attack(attacking ? attackTarget : rally);
        }
    }
}

// Marines caught in a clump when lurker spines are about: spread out so one volley doesn't hit them all
void SparkTerran::splitVsLurkers(const Unitset &marines)
{
    if (!lurkersSeen || Broodwar->getFrameCount() % 24 != 0) return;
    for (auto unit : marines)
    {
        if (unit->getType() != UnitTypes::Terran_Marine || !unit->exists()) continue;
        int neighbors = (int) unit->getUnitsInRadius(64, IsOwned && IsVisible).size();
        if (neighbors < 4) continue;
        Position away(0, 0);
        for (auto other : unit->getUnitsInRadius(64, IsOwned && IsVisible))
        {
            if (other == unit) continue;
            away += unit->getPosition() - other->getPosition();
        }
        if (away == Position(0, 0)) continue;
        Position spot = (unit->getPosition() + away).makeValid();
        if (unit->getOrder() != Orders::Move) unit->move(spot);
    }
}

// Comsat stations: scan cloaked enemies in fights, and the enemy base now and then
void SparkTerran::useComsat()
{
    using namespace UnitTypes;
    for (auto cc : Broodwar->self()->getUnits())
    {
        if (cc->getType() != Terran_Command_Center || !cc->isCompleted()) continue;
        auto addon = cc->getAddon();
        if (!addon || addon->getType() != Terran_Comsat_Station || !addon->isCompleted()) continue;
        if (addon->getEnergy() < 50) continue;

        // Cloaked enemies fighting our army: scan them
        if (cloakSeen)
        {
            for (auto unit : Broodwar->self()->getUnits())
            {
                if (!isCombat(unit) || !unit->exists()) continue;
                for (auto enemy : unit->getUnitsInRadius(320, IsEnemy && IsVisible))
                {
                    if (enemy->isCloaked() && !enemy->isDetected())
                    {
                        addon->useTech(TechTypes::Scanner_Sweep, enemy->getPosition());
                        return;
                    }
                }
            }
        }
        // A periodic look at the enemy base
        if (enemyBase != Positions::Unknown && Broodwar->getFrameCount() % 2400 < 8
            && !Broodwar->isVisible(TilePosition(enemyBase)))
        {
            addon->useTech(TechTypes::Scanner_Sweep, enemyBase);
            return;
        }
    }
}

// Enemies near our bases pull defenders home with a leash; bunkers get loaded. Called last so defence
// orders win. Threats at home while attacking call the push back
void SparkTerran::defendBases(const Unitset &army)
{
    using namespace UnitTypes;
    for (auto cc : commandCenters(true))
    {
        auto threats = threatsNear(cc->getPosition(), DefenseRadius);
        if (threats.empty()) continue;
        if (attacking)
        {
            attacking = false;
            lastRetreatFrame = Broodwar->getFrameCount();
            if (getenv("ST_DEBUG")) printf("     RECALL to defend at %d\n", Broodwar->getFrameCount());
        }
        // Bunkers near this base get marines
        for (auto bunker : Broodwar->self()->getUnits())
        {
            if (bunker->getType() != Terran_Bunker || !bunker->isCompleted()) continue;
            if (bunker->getDistance(cc) > 700) continue;
            int loaded = (int) bunker->getLoadedUnits().size();
            for (auto unit : army)
            {
                if (loaded >= 4) break;
                if (unit->getType() == Terran_Marine && unit->exists() && unit->getDistance(bunker) < 500)
                {
                    bunker->load(unit);
                    loaded++;
                }
            }
        }
        // The army fights near the base with a leash, so it doesn't chase across the map
        for (auto unit : army)
        {
            auto type = unit->getType();
            if (type == Terran_Medic || type == Terran_Science_Vessel) continue;
            if (type == Terran_Siege_Tank_Siege_Mode) continue;  // handled by controlTanks
            fight(unit, cc->getPosition(), cc->getPosition(), DefenseRadius);
        }
    }
}

// Worker rush or SCVs in our base: pull SCVs onto them
void SparkTerran::defendWorkerRush()
{
    int frame = Broodwar->getFrameCount();
    if (frame > 6000) return;
    Unitset intruders;
    for (auto enemy : Broodwar->enemy()->getUnits())
    {
        if (!enemy->isVisible() || !enemy->exists()) continue;
        if (enemy->getType().isWorker() && enemy->getDistance(home) < 700) intruders.insert(enemy);
        if (proxySeen && enemy->getType().isBuilding() && enemy->getDistance(home) < 900) intruders.insert(enemy);
    }
    if (intruders.empty()) return;
    int pulled = 0;
    for (auto scv : Broodwar->self()->getUnits())
    {
        if (pulled >= 8) break;
        if (!scv->getType().isWorker() || !scv->isCompleted() || scv == scout || builders.count(scv)) continue;
        if (scv->getOrder() == Orders::AttackUnit) continue;
        Unit closest = nullptr;
        int best = INT_MAX;
        for (auto intruder : intruders)
        {
            int d = scv->getDistance(intruder);
            if (d < best)
            {
                best = d;
                closest = intruder;
            }
        }
        if (closest && best < 800)
        {
            scv->attack(closest);
            pulled++;
        }
    }
}

void SparkTerran::controlArmy()
{
    Unitset army;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (!unit->isCompleted() || !unit->exists()) continue;
        if (isCombat(unit)) army.insert(unit);
    }
    if (army.empty()) return;

    aimedDamage.clear();
    defendWorkerRush();
    judgeAttack(army);

    Unitset marines;
    for (auto unit : army)
        if (unit->getType() == UnitTypes::Terran_Marine) marines.insert(unit);
    splitVsLurkers(marines);

    controlMarines(army);
    controlTanks(army);
    controlMedics(army);
    controlVessels(army);
    controlGoliaths(army);
    controlAir(army);
    defendBases(army);  // last, so defence orders win when home is threatened
}

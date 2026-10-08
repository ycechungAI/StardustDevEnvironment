#include "ClaudeOpus55.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace BWAPI;
using namespace Filter;

namespace
{
    const int ProbesPerBase = 24;      // ~2.5 per mineral patch plus 3 on gas
    const int MaxProbes = 60;
    const int FirstAttackArmy = 12;
    const int LaterAttackArmy = 16;
    const int RetreatBelow = 6;
    const double RetreatRatio = 1.25;  // retreat when the nearby enemy is this much stronger
    const int RegroupDistance = 450;

    bool isArmy(UnitType type)
    {
        return type == UnitTypes::Protoss_Zealot || type == UnitTypes::Protoss_Dragoon || type == UnitTypes::Protoss_Reaver;
    }

    bool isMelee(UnitType type)
    {
        auto weapon = type.groundWeapon();
        return weapon != WeaponTypes::None && weapon.maxRange() <= 32 && !type.isWorker();
    }

    // Enemy units worth shooting first: those that can hurt us, then workers, then the rest
    int targetPriority(Unit target)
    {
        auto type = target->getType();
        if (type == UnitTypes::Terran_Bunker || type == UnitTypes::Terran_Siege_Tank_Siege_Mode
            || type == UnitTypes::Terran_Siege_Tank_Tank_Mode || type == UnitTypes::Protoss_Reaver
            || type == UnitTypes::Zerg_Lurker) return 4;
        // A shuttle carries the reaver (or zealots) in and out of range: shooting it down takes them with it
        if (type == UnitTypes::Protoss_Shuttle || type == UnitTypes::Terran_Dropship) return 4;
        if (type.canAttack() && !type.isWorker()) return 3;
        if (type.isWorker()) return 2;
        if (type == UnitTypes::Protoss_Pylon || type.isResourceDepot()) return 1;
        return 0;
    }

    // An enemy that is fighting rather than walking past: anything with a weapon except a worker, or a worker that is
    // attacking or building (a bunker or cannon in our base)
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

void ClaudeOpus55::onStart()
{
    Broodwar->enableFlag(Flag::UserInput);
    enemyRace = Broodwar->enemy() ? Broodwar->enemy()->getRace() : Races::Unknown;

    home = Position(Broodwar->self()->getStartLocation()) + Position(64, 48);
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType().isResourceDepot())
        {
            mainNexus = unit;
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
    if (natural)
    {
        // The natural's front: from its minerals, past the nexus and on a little, where attacks come from
        Position depot = Position(natural->depot) + Position(64, 48);
        naturalFront = towards(depot, depot + (depot - natural->center), 192);
    }
    analyseMap();
}

// How far the enemy is by ground, and how many ways lead into our natural from the enemy's side and how wide they
// are: a forge expand needs a natural with one narrow entrance, on a map big enough that a rush can't arrive first
void ClaudeOpus55::analyseMap()
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
    if (!natural) return;

    const int Ring = 14;  // tiles out from the natural nexus: past its buildings, at its chokes
    TilePosition naturalTile = natural->depot + TilePosition(2, 1);
    auto fromNatural = groundDistances({naturalTile}, Ring);
    auto fromEnemy = groundDistances(enemyStarts);
    int naturalToEnemy = fromEnemy[naturalTile.y * width + naturalTile.x];
    int naturalToHome = fromHome[naturalTile.y * width + naturalTile.x];
    if (naturalToEnemy < 0 || naturalToHome < 0) return;

    // The ring of tiles Ring steps from the natural that lead on toward the enemy and not back up to our main
    std::set<int> ring;
    for (int i = 0; i < (int) fromNatural.size(); i++)
    {
        if (fromNatural[i] == Ring && fromEnemy[i] >= 0 && fromEnemy[i] < naturalToEnemy
            && fromHome[i] > naturalToHome - Ring / 2) ring.insert(i);
    }
    // Each connected stretch of it is one entrance
    naturalEntrances = 0;
    widestEntrance = 0;
    while (!ring.empty())
    {
        std::vector<int> stretch = {*ring.begin()};
        ring.erase(ring.begin());
        for (size_t i = 0; i < stretch.size(); i++)
        {
            int x = stretch[i] % width, y = stretch[i] / width;
            for (int dx = -1; dx <= 1; dx++)
            {
                for (int dy = -1; dy <= 1; dy++)
                {
                    auto it = ring.find((y + dy) * width + x + dx);
                    if (it == ring.end()) continue;
                    stretch.push_back(*it);
                    ring.erase(it);
                }
            }
        }
        if (stretch.size() < 2) continue;  // a crack in the cliffs, not a way in
        naturalEntrances++;
        widestEntrance = std::max(widestEntrance, (int) stretch.size());
    }
}

// The highest-ground tile of our main closest to the natural: the top of the ramp, where defending is easiest
Position ClaudeOpus55::findRampTop() const
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

// Groups the map's mineral fields into bases and works out where each one's nexus goes
void ClaudeOpus55::findBases()
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

        // The nexus spot closest to the minerals (and geyser) that the game allows
        TilePosition centerTile(base.center);
        Position resources = base.geyser ? (base.center * 3 + base.geyser->getInitialPosition()) / 4 : base.center;
        int bestDistance = INT_MAX;
        base.depot = TilePositions::Invalid;
        for (int dx = -12; dx <= 12; dx++)
        {
            for (int dy = -12; dy <= 12; dy++)
            {
                TilePosition tile(centerTile.x + dx, centerTile.y + dy);
                if (!tile.isValid() || !Broodwar->canBuildHere(tile, UnitTypes::Protoss_Nexus)) continue;
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

void ClaudeOpus55::onUnitDestroy(Unit unit)
{
    enemyBuildings.erase(unit->getID());
    enemyArmy.erase(unit->getID());
    builders.erase(unit);
    // CO55_DEBUG: each unit we lose, where, and what enemies stood near it
    if (getenv("CO55_DEBUG") && unit->getPlayer() == Broodwar->self())
    {
        std::map<std::string, int> near;
        for (auto enemy : Broodwar->getUnitsInRadius(unit->getPosition(), 256, IsEnemy && IsVisible))
            near[enemy->getType().getName()]++;
        printf("LOST %d %s %d from home at %d,%d (rally %d,%d):", Broodwar->getFrameCount(),
               unit->getType().getName().c_str(), unit->getPosition().getApproxDistance(home), unit->getTilePosition().x,
               unit->getTilePosition().y, TilePosition(rally).x, TilePosition(rally).y);
        for (auto &[name, n] : near) printf(" %s=%d", name.c_str(), n);
        printf("\n");
    }
    if (unit == scout) scout = nullptr;
    if (unit == mainNexus) mainNexus = nullptr;
}

void ClaudeOpus55::onFrame()
{
    if (Broodwar->isPaused() || !Broodwar->self()) return;
    int frame = Broodwar->getFrameCount();

    if (getenv("CO55_DEBUG") && frame % 500 == 0)
    {
        auto self = Broodwar->self();
        printf("CO55 %d: supply %d/%d min %d gas %d nexus %d probes %d gates %d zealots %d goons %d attacking %d wave %d\n",
               frame, self->supplyUsed() / 2, self->supplyTotal() / 2, self->minerals(), self->gas(),
               self->allUnitCount(UnitTypes::Protoss_Nexus), self->allUnitCount(UnitTypes::Protoss_Probe),
               self->allUnitCount(UnitTypes::Protoss_Gateway), self->allUnitCount(UnitTypes::Protoss_Zealot),
               self->allUnitCount(UnitTypes::Protoss_Dragoon), attacking, wave);
        printf("     natural %s expansionDue %d threats %zu rush %d cloak %d air %d ranged %d step %zu/%zu\n",
               natural ? "yes" : "no", expansionDue, threatsNearHome().size(), rushSeen, cloakSeen, airSeen,
               rangedNeeded, buildOrderStep, buildOrder.size());
        if (getenv("CO55_DEBUG")[0] == '2')
        {
            printf("     map %s rushDistance %d naturalEntrances %d widest %d\n", Broodwar->mapFileName().c_str(),
                   rushDistance, naturalEntrances, widestEntrance);
            printf("     home %d,%d naturalDepot %d,%d front %d,%d\n", TilePosition(home).x, TilePosition(home).y,
                   natural ? natural->depot.x : -1, natural ? natural->depot.y : -1, TilePosition(naturalFront).x,
                   TilePosition(naturalFront).y);
            for (auto &[b, p] : builders)
                printf("     pending %s at %d,%d since %d order %s builder at %d,%d\n", p.type.c_str(), p.tile.x,
                       p.tile.y, p.frame, b->getOrder().c_str(), b->getTilePosition().x, b->getTilePosition().y);
            for (auto u : Broodwar->self()->getUnits())
                if (u->getType().isBuilding()) printf("     have %s at %d,%d\n", u->getType().c_str(), u->getTilePosition().x, u->getTilePosition().y);
        }
        std::map<std::string, int> orders;
        for (auto u : Broodwar->self()->getUnits())
            if (u->getType().isWorker()) orders[u->getOrder().getName()]++;
        printf("     probe orders:");
        for (auto &[name, n] : orders) printf(" %s=%d", name.c_str(), n);
        printf("\n");
        std::map<std::string, int> seen;
        for (auto &[id, s] : enemyArmy) seen[s.type.getName()]++;
        printf("     enemy army:");
        for (auto &[name, n] : seen) printf(" %s=%d", name.c_str(), n);
        printf("\n");
        fflush(stdout);
    }

    trackEnemy();
    if (frame % 4 == 0) controlArmy();
    if (frame % 8 == 0)
    {
        if (!mainNexus || !mainNexus->exists())
        {
            mainNexus = nullptr;
            auto all = nexuses(true);
            if (!all.empty()) mainNexus = all.front();
        }
        buildStructures();
        trainUnits();
        manageWorkers();
        scoutEnemy();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Economy

std::vector<Unit> ClaudeOpus55::nexuses(bool completedOnly) const
{
    std::vector<Unit> result;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType().isResourceDepot() && (unit->isCompleted() || !completedOnly)) result.push_back(unit);
    }
    return result;
}

// The completed nexus with the fewest mineral workers per mineral patch
Unit ClaudeOpus55::nexusNeedingWorkers() const
{
    Unit best = nullptr;
    double bestRatio = 1e9;
    for (auto nexus : nexuses(true))
    {
        int patches = (int) nexus->getUnitsInRadius(320, IsMineralField).size();
        if (patches == 0) continue;
        int workers = (int) nexus->getUnitsInRadius(320, IsOwned && IsWorker && IsGatheringMinerals).size();
        double ratio = double(workers) / patches;
        if (ratio < bestRatio)
        {
            bestRatio = ratio;
            best = nexus;
        }
    }
    return best;
}

int ClaudeOpus55::pendingCount(UnitType type) const
{
    int n = 0;
    for (auto &[builder, pending] : builders)
    {
        if (pending.type == type) n++;
    }
    return n;
}

int ClaudeOpus55::count(UnitType type, bool includePending) const
{
    int n = Broodwar->self()->allUnitCount(type);
    return includePending ? n + pendingCount(type) : n;
}

int ClaudeOpus55::reservedMinerals() const
{
    int total = 0;
    for (auto &[builder, pending] : builders) total += pending.type.mineralPrice();
    return total;
}

int ClaudeOpus55::reservedGas() const
{
    int total = 0;
    for (auto &[builder, pending] : builders) total += pending.type.gasPrice();
    return total;
}

void ClaudeOpus55::manageWorkers()
{
    if (nexuses(true).empty()) return;

    // Builders whose building has started (or that gave up) go back to mining
    for (auto it = builders.begin(); it != builders.end();)
    {
        auto builder = it->first;
        auto &pending = it->second;
        bool started = false;
        for (auto unit : Broodwar->getUnitsOnTile(pending.tile, IsOwned && IsBuilding))
        {
            if (unit->getType() == pending.type) started = true;
        }
        if (started || !builder->exists() || Broodwar->getFrameCount() - pending.frame > 900)
        {
            if (builder->exists()) builder->stop();
            if (!started) retryOpeningStep(pending.type);
            it = builders.erase(it);
            continue;
        }
        if (builder->getOrder() != Orders::PlaceBuilding && !builder->isConstructing())
        {
            if (Broodwar->canBuildHere(pending.tile, pending.type, builder, true))
            {
                if (!builder->build(pending.type, pending.tile)) builder->move(Position(pending.tile) + Position(32, 32));
            }
            else if (!Broodwar->isExplored(pending.tile))
            {
                builder->move(Position(pending.tile) + Position(32, 32));
            }
            else if (Broodwar->isVisible(pending.tile))
            {
                // Something is in the way: the nearest free spot close by, or give it up
                auto spot = findBuildSpot(pending.type, pending.tile);
                // A nexus has only its one spot: wait for the army to clear whatever stands there
                if (pending.type.isResourceDepot())
                {
                    if (builder->getDistance(Position(pending.tile)) > 96) builder->move(Position(pending.tile) + Position(64, 48));
                    ++it;
                    continue;
                }
                if ((!spot.isValid() || spot.getApproxDistance(pending.tile) > 6)
                    && placeableIgnoringUnits(pending.tile, pending.type, true))
                {
                    // Only units in the way: wait next to the spot for them to move (until the build times out)
                    if (builder->getDistance(Position(pending.tile)) > 64) builder->move(Position(pending.tile) + Position(32, 32));
                    ++it;
                    continue;
                }
                if (!spot.isValid() || spot.getApproxDistance(pending.tile) > 6)
                {
                    retryOpeningStep(pending.type);
                    it = builders.erase(it);
                    continue;
                }
                pending.tile = spot;
                if (!builder->build(pending.type, spot)) builder->move(Position(spot) + Position(32, 32));
            }
            else
            {
                builder->move(Position(pending.tile) + Position(64, 48));
            }
        }
        ++it;
    }

    // Three on each finished assimilator
    for (auto gas : Broodwar->self()->getUnits())
    {
        if (gas->getType() != UnitTypes::Protoss_Assimilator || !gas->isCompleted()) continue;
        auto self = Broodwar->self();
        int wantOnGas = self->gas() > 300 ? 1 : self->gas() > 100 ? 2 : 3;  // don't bank much more than 100 gas
        auto gasWorkers = gas->getUnitsInRadius(320, IsOwned && IsWorker && IsGatheringGas && !IsCarryingGas);
        int onGas = (int) gas->getUnitsInRadius(320, IsOwned && IsWorker && IsGatheringGas).size();
        for (auto probe : gasWorkers)
        {
            if (onGas <= wantOnGas) break;
            auto nexus = nexusNeedingWorkers();
            auto mineral = nexus ? nexus->getClosestUnit(IsMineralField, 400) : nullptr;
            if (!mineral) break;
            probe->gather(mineral);
            onGas--;
        }
        for (auto probe : gas->getUnitsInRadius(400, IsOwned && IsWorker && IsGatheringMinerals && !IsCarryingMinerals))
        {
            if (onGas >= wantOnGas) break;
            if (probe == scout || builders.count(probe)) continue;
            probe->gather(gas);
            onGas++;
        }
    }

    // Idle probes mine at the base that needs them most, each on the patch there with the fewest probes (the closest
    // first). Sending each to the one closest patch made them queue on it and wander off to others: 48 minerals fewer
    // than UAlbertaBot by 1:00 with the same 8 probes, and the pylon and gateways 10 seconds later
    std::map<Unit, int> onPatch;
    for (auto probe : Broodwar->self()->getUnits())
    {
        auto target = probe->getOrderTarget();
        if (probe->getType().isWorker() && probe->isGatheringMinerals() && target && target->getType().isMineralField())
        {
            onPatch[target]++;
        }
    }
    for (auto probe : Broodwar->self()->getUnits())
    {
        if (!probe->getType().isWorker() || !probe->isCompleted() || probe == scout || builders.count(probe)) continue;
        if (!probe->isIdle()) continue;
        if (probe->isCarryingMinerals() || probe->isCarryingGas())
        {
            probe->returnCargo();
            continue;
        }
        auto nexus = nexusNeedingWorkers();
        if (!nexus) continue;
        Unit mineral = nullptr;
        int bestScore = INT_MAX;
        for (auto patch : nexus->getUnitsInRadius(320, IsMineralField))
        {
            int score = onPatch[patch] * 1000 + nexus->getDistance(patch);
            if (score < bestScore)
            {
                mineral = patch;
                bestScore = score;
            }
        }
        if (!mineral) mineral = nexus->getClosestUnit(IsMineralField, 400);
        if (mineral)
        {
            probe->gather(mineral);
            onPatch[mineral]++;
        }
    }

    // Every 10 seconds, move mineral probes from an oversaturated base to one that needs them
    if (Broodwar->getFrameCount() % 240 == 0)
    {
        auto target = nexusNeedingWorkers();
        if (!target) return;
        int targetPatches = (int) target->getUnitsInRadius(320, IsMineralField).size();
        int targetWorkers = (int) target->getUnitsInRadius(320, IsOwned && IsWorker && IsGatheringMinerals).size();
        int wanted = std::max(0, targetPatches * 2 - targetWorkers);
        for (auto nexus : nexuses(true))
        {
            if (nexus == target || wanted <= 0) continue;
            int patches = (int) nexus->getUnitsInRadius(320, IsMineralField).size();
            auto workers = nexus->getUnitsInRadius(320, IsOwned && IsWorker && IsGatheringMinerals);
            int excess = (int) workers.size() - patches * 2;
            auto mineral = target->getClosestUnit(IsMineralField, 400);
            for (auto probe : workers)
            {
                if (excess <= 0 || wanted <= 0 || !mineral) break;
                if (probe == scout || builders.count(probe) || probe->isCarryingMinerals()) continue;
                probe->gather(mineral);
                excess--;
                wanted--;
            }
        }
    }
}

Unit ClaudeOpus55::chooseBuilder(Position near)
{
    Unit best = nullptr;
    int bestDistance = INT_MAX;
    for (auto probe : Broodwar->self()->getUnits())
    {
        if (!probe->getType().isWorker() || !probe->isCompleted() || probe == scout || builders.count(probe)) continue;
        if (probe->isCarryingMinerals() || probe->isGatheringGas() || probe->isConstructing()) continue;
        if (probe->getOrder() == Orders::AttackUnit) continue;
        int distance = probe->getDistance(near);
        if (distance < bestDistance)
        {
            best = probe;
            bestDistance = distance;
        }
    }
    return best;
}

TilePosition ClaudeOpus55::pylonSpot()
{
    Position away = home + (home - mineralCenter) * 2 / 3;
    int pylons = count(UnitTypes::Protoss_Pylon);
    Position offset(((pylons % 3) - 1) * 96, ((pylons / 3) % 3 - 1) * 96);
    return TilePosition(away + offset);
}

// A building of the opening that was given up (its builder killed, or its spot blocked) is tried again rather than lost
void ClaudeOpus55::retryOpeningStep(UnitType type)
{
    if (buildOrderStep == 0 || buildOrderStep > buildOrder.size()) return;
    if (buildOrder[buildOrderStep - 1].type == type) buildOrderStep--;
}

bool ClaudeOpus55::build(UnitType type, TilePosition near)
{
    if (Broodwar->self()->minerals() - reservedMinerals() < type.mineralPrice()) return false;
    if (Broodwar->self()->gas() - reservedGas() < type.gasPrice()) return false;

    TilePosition tile = TilePositions::Invalid;
    if (type.isRefinery())
    {
        // The nearest free geyser to a completed nexus
        int best = INT_MAX;
        for (auto nexus : nexuses(true))
        {
            for (auto geyser : Broodwar->getGeysers())
            {
                int distance = geyser->getDistance(nexus);
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
        // The cannon opening's pylon and cannons go among the mining probes, whom the first pass treats as in the way:
        // it put them 6 to 8 tiles off, out of range of the mineral line
        bool amongProbes = cannonOpening && (type == UnitTypes::Protoss_Photon_Cannon
                                             || type == UnitTypes::Protoss_Pylon && count(UnitTypes::Protoss_Pylon) == 0);
        if (amongProbes) tile = findBuildSpot(type, near, Broodwar->isExplored(near), true);
        if (!tile.isValid()) tile = findBuildSpot(type, near, Broodwar->isExplored(near));  // the natural may not be seen yet
        // Every spot taken by units standing about: one that is free apart from them, where the probe waits
        if (!tile.isValid()) tile = findBuildSpot(type, near, Broodwar->isExplored(near), true);
    }
    if (!tile.isValid()) return false;

    auto builder = chooseBuilder(Position(tile));
    if (!builder) return false;

    if (getenv("CO55_DEBUG")) printf("     BUILD %s near %d,%d -> %d,%d frame %d\n", type.c_str(), near.x, near.y, tile.x, tile.y, Broodwar->getFrameCount());
    builders[builder] = PendingBuild{type, tile, Broodwar->getFrameCount()};
    if (!builder->build(type, tile)) builder->move(Position(tile) + Position(type.tileWidth() * 16, type.tileHeight() * 16));
    return true;
}

// The nearest explored, buildable spot to `near` that leaves a free tile around the building (so the base doesn't wall
// itself in) and stays out of the mineral line
// Whether a building fits at `tile` if the units walking about there moved away: buildable, powered, off creep, and no
// building, resource or other immobile unit in the way. Enemy workers standing on every free spot near our pylons
// otherwise stop all building (an SCV army camped in our main kept us without a gateway for a whole game)
bool ClaudeOpus55::placeableIgnoringUnits(TilePosition tile, UnitType type, bool checkExplored) const
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
    if (type.requiresPsi() && !Broodwar->hasPower(tile, type)) return false;
    auto blocking = Broodwar->getUnitsInRectangle(Position(tile), Position(tile + TilePosition(w, h)) - Position(1, 1),
                                                  IsBuilding || !CanMove);
    return blocking.empty();
}

TilePosition ClaudeOpus55::findBuildSpot(UnitType type, TilePosition near, bool checkExplored, bool ignoreUnits) const
{
    int w = type.tileWidth(), h = type.tileHeight();
    int homeToMinerals = home.getApproxDistance(mineralCenter);
    for (int radius = 0; radius <= 20; radius++)
    {
        for (int dx = -radius; dx <= radius; dx++)
        {
            for (int dy = -radius; dy <= radius; dy++)
            {
                if (std::abs(dx) != radius && std::abs(dy) != radius) continue;  // the ring at this radius only
                TilePosition tile(near.x + dx, near.y + dy);
                if (!tile.isValid()) continue;
                if (ignoreUnits ? !placeableIgnoringUnits(tile, type, checkExplored)
                                : !Broodwar->canBuildHere(tile, type, nullptr, checkExplored)) continue;
                Position center = Position(tile) + Position(w * 16, h * 16);
                // ... and the cannon opening's first pylon, to power them: put a few tiles off, the cannons were out of
                // range of the mineral line where zerglings killed 10 probes
                bool cannonPylon = cannonOpening && type == UnitTypes::Protoss_Pylon && count(UnitTypes::Protoss_Pylon) == 0;
                if (type != UnitTypes::Protoss_Photon_Cannon && !cannonPylon
                    && center.getApproxDistance(mineralCenter) < homeToMinerals && center.getApproxDistance(home) < 400)
                {
                    continue;  // between the nexus and its minerals (where only cannons belong)
                }
                // Never on (or touching) a base's nexus spot: the natural's front buildings went down before its nexus
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
                // Nor where a probe is already on its way to build something else
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
                // The cannon opening's pylon and cannons fit in the gap between the nexus and its minerals only if
                // they may touch the nexus and each other; the free tile is kept from the minerals, for the probes
                bool amongProbes = cannonOpening && (type == UnitTypes::Protoss_Photon_Cannon
                                                     || type == UnitTypes::Protoss_Pylon && count(UnitTypes::Protoss_Pylon) == 0);
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
                                || blocker.isBuilding() && !amongProbes)
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

void ClaudeOpus55::chooseBuildOrder()
{
    using namespace UnitTypes;
    // Forge fast expand, as the strong Protoss bots were seen to beat Zerg: pylon and forge at the natural's front,
    // the natural nexus, cannons there, then gateways and the core in the main. Only where the natural has one narrow
    // way in, which cannons can hold, and the map isn't so small that zerglings arrive before them
    bool forgeExpandMap = natural && naturalFront.isValid() && naturalEntrances == 1 && widestEntrance <= 12
                          && rushDistance >= 150 && rushDistance != INT_MAX;
    // Off: in practice games the forge, cannon and gateway came too slowly to hold a 9-pool (no zealot before frame
    // 6500, 0 wins in 8 games), while two gateways first won some
    const bool useForgeExpand = false;
    if (enemyRace == Races::Zerg && forgeExpandMap && useForgeExpand)
    {
        forgeExpand = true;
        // The first cannon and a gateway go down before the nexus, so a 9-pool finds defences already there
        buildOrder = {{8, Protoss_Pylon, true}, {10, Protoss_Forge, true}, {11, Protoss_Photon_Cannon, true},
                      {12, Protoss_Gateway, true}, {13, Protoss_Nexus}, {14, Protoss_Pylon},
                      {15, Protoss_Photon_Cannon, true}, {16, Protoss_Gateway}, {17, Protoss_Assimilator},
                      {18, Protoss_Cybernetics_Core}, {19, Protoss_Pylon}, {21, Protoss_Gateway}, {24, Protoss_Assimilator}};
    }
    else if (enemyRace == Races::Zerg)
    {
        // Forge and cannons in the main first, as Locutus was seen to hold ZZZKBot's four-pool without losing a unit
        // (forge at 1:14, three cannons at 1:44). A gateway first lost 5 of 12 games to four- and five-pools, each time
        // two zealots and the probes against the first zerglings. The first pylon goes by the minerals to power them
        // (cannonsWait); the second gateway still waits for two zealots (secondGatewayWaits), then the core
        cannonOpening = true;
        // A second pylon, away from the minerals, before the gateway: the first powers too little room for one
        buildOrder = {{8, Protoss_Pylon}, {9, Protoss_Forge}, {10, Protoss_Pylon}, {11, Protoss_Gateway},
                      {14, Protoss_Pylon}, {15, Protoss_Gateway}, {16, Protoss_Assimilator},
                      {17, Protoss_Cybernetics_Core}, {21, Protoss_Pylon}, {24, Protoss_Gateway}};
    }
    else if (enemyRace == Races::Terran)
    {
        // Two gateways of zealots, then the core. No early natural: marine, bunker and SCV rushes punish it, so the
        // natural waits for an army like against the other races. With one gateway and the core first, 13 marines met
        // 4 army supply at 4:28, beat it twice, and the game was won too late to finish
        buildOrder = {{8, Protoss_Pylon}, {9, Protoss_Gateway}, {11, Protoss_Gateway}, {14, Protoss_Pylon},
                      {15, Protoss_Assimilator}, {16, Protoss_Cybernetics_Core}, {20, Protoss_Pylon},
                      {24, Protoss_Gateway}};
    }
    else
    {
        // Protoss (or unknown): an early gateway and zealot, a second gateway, then the core and dragoons
        buildOrder = {{8, Protoss_Pylon}, {9, Protoss_Gateway}, {12, Protoss_Gateway}, {14, Protoss_Pylon},
                      {16, Protoss_Assimilator}, {17, Protoss_Cybernetics_Core}, {21, Protoss_Pylon},
                      {24, Protoss_Gateway}};
    }
}

// Against Zerg the second gateway waits until the first has made two zealots, or one has held off zerglings: two
// gateways at once left no money for the first zealot when a four-pool arrived
bool ClaudeOpus55::secondGatewayWaits(const Step &step) const
{
    auto self = Broodwar->self();
    return enemyRace == Races::Zerg && step.type == UnitTypes::Protoss_Gateway && count(UnitTypes::Protoss_Gateway) == 1
           && self->allUnitCount(UnitTypes::Protoss_Zealot) < 2 && self->killedUnitCount(UnitTypes::Zerg_Zergling) < 2;
}

// The nearest base we can walk to without a nexus yet
const ClaudeOpus55::Base *ClaudeOpus55::nextBase() const
{
    for (auto &base : bases)
    {
        if (base.center.getApproxDistance(home) < 400) continue;
        if (!Broodwar->hasPath(home, base.center)) continue;
        if (!Broodwar->getUnitsInRadius(Position(base.depot) + Position(64, 48), 160, IsResourceDepot).empty()) continue;
        bool enemyThere = false;
        for (auto &[id, building] : enemyBuildings)
        {
            if (building.second.getApproxDistance(base.center) < 400) enemyThere = true;
        }
        if (!enemyThere) return &base;
    }
    return nullptr;
}

void ClaudeOpus55::buildStructures()
{
    if (nexuses(true).empty()) return;
    auto self = Broodwar->self();
    if (buildOrder.empty()) chooseBuildOrder();

    int supplyUsed = self->supplyUsed() / 2;
    int supplyTotal = self->supplyTotal() / 2;
    int gateways = count(UnitTypes::Protoss_Gateway);
    int finishedGateways = self->completedUnitCount(UnitTypes::Protoss_Gateway);
    int nexusCount = count(UnitTypes::Protoss_Nexus);
    bool coreDone = self->completedUnitCount(UnitTypes::Protoss_Cybernetics_Core) > 0;
    int army = self->allUnitCount(UnitTypes::Protoss_Zealot) + self->allUnitCount(UnitTypes::Protoss_Dragoon);
    bool underAttack = !threatsNearHome().empty();
    int freeMinerals = self->minerals() - reservedMinerals();

    Position cannonSpot = (home + mineralCenter) / 2;
    TilePosition nearPylon = pylonSpot();
    for (auto pylon : self->getUnits())
    {
        if (pylon->getType() == UnitTypes::Protoss_Pylon && pylon->isCompleted() && pylon->getDistance(home) < 600)
        {
            nearPylon = pylon->getTilePosition();
            // The cannon opening's pylon by the minerals only while there is no other
            if (!cannonOpening || pylon->getDistance(cannonSpot) > 160) break;
        }
    }
    TilePosition frontTile = naturalFront.isValid() ? TilePosition(naturalFront) : TilePositions::Invalid;
    auto placeFor = [&](UnitType type) -> TilePosition
    {
        if (type == UnitTypes::Protoss_Pylon && cannonOpening && count(UnitTypes::Protoss_Pylon) == 0)
        {
            return TilePosition(cannonSpot);
        }
        if (type == UnitTypes::Protoss_Pylon) return pylonSpot();
        if (type.isResourceDepot())
        {
            auto base = natural ? natural : nextBase();
            return base ? base->depot : TilePositions::Invalid;
        }
        if (type.isRefinery()) return TilePosition(home);
        return nearPylon;
    };

    // The cannon opening against Zerg: after the forge, nothing else in the opening until two cannons by the minerals
    // are on their way, three once an early pool or zerglings are seen
    // Once they have all been started it goes on: waiting to replace a lost cannon kept the gateway from ever starting,
    // and ZZZKBot's zerglings killed the probes and the cannons one by one
    if (cannonOpening && self->allUnitCount(UnitTypes::Protoss_Photon_Cannon) >= openingCannons()) openingCannonsStarted = true;
    bool cannonsWait = false;
    if (cannonOpening && !openingCannonsStarted && Broodwar->getFrameCount() < 5000 && buildOrderStep < buildOrder.size()
        && buildOrder[buildOrderStep].type != UnitTypes::Protoss_Forge && count(UnitTypes::Protoss_Forge) > 0)
    {
        cannonsWait = self->completedUnitCount(UnitTypes::Protoss_Forge) == 0 || !cannonsNear(cannonSpot, openingCannons());
    }

    // An early rush seen (an early pool, zerglings, mass gateways or a worker rush): a forge and cannons by the minerals,
    // alongside as many zealots as the gateways can make
    if (!forgeExpand && rushSeen && Broodwar->getFrameCount() < 9000
        && self->completedUnitCount(UnitTypes::Protoss_Pylon) > 0)
    {
        // Two barracks of marines: a second gateway at once, and zealots rather than a forge and cannons (eleven marines
        // were in our main by 5:21 while one gateway made zealots and the money went into the forge)
        if (barracksRush)
        {
            if (count(UnitTypes::Protoss_Gateway) == 1) build(UnitTypes::Protoss_Gateway, nearPylon);
        }
        // A gateway rush likewise: the forge and two cannons cost four zealots, and the first cannon finished at 5:08,
        // after 16 supply of zealots had beaten our 10 at 4:33
        // ... and a third gateway to match theirs: two made 12 army supply by 5:00 against their 22 to 28
        else if (enemyRace == Races::Protoss)
        {
            if (count(UnitTypes::Protoss_Gateway) == 2 && army >= 2) build(UnitTypes::Protoss_Gateway, nearPylon);
        }
        // Zealots first: a forge and cannon take too long to stop a rush already on its way
        else if (count(UnitTypes::Protoss_Forge) < 1 && count(UnitTypes::Protoss_Gateway) >= 1 && army >= 3)
        {
            build(UnitTypes::Protoss_Forge, nearPylon);
        }
        // ... but not ahead of the cannon opening's gateway
        else if (self->completedUnitCount(UnitTypes::Protoss_Forge) > 0 && !(cannonOpening && count(UnitTypes::Protoss_Gateway) == 0))
        {
            cannonsNear((home + mineralCenter) / 2, 2);
        }
    }

    // Detection, ahead of the opening: cannons by the minerals as soon as cloaked units (or the tech for them) are seen;
    // a robotics facility for observers against Templar Archives or a Citadel, and against Protoss right after the core
    // (later against a gateway rush, when zealots come first). A dark templar at 6:02 beat a robotics facility started
    // at 4:54 behind the opening, and killed 27 probes
    // Against Protoss one cannon there anyway once the core is started and no rush needs every mineral: a dark templar
    // at 5:46 came before the robotics facility (6:40) and killed 30 probes, and a forge and cannon are quicker
    bool cannonInsurance = enemyRace == Races::Protoss && count(UnitTypes::Protoss_Cybernetics_Core) > 0 && !rushMode();
    if (cloakSeen || cannonInsurance)
    {
        if (count(UnitTypes::Protoss_Forge) < 1)
        {
            build(UnitTypes::Protoss_Forge, nearPylon);
        }
        else if (self->completedUnitCount(UnitTypes::Protoss_Forge) > 0)
        {
            cannonsNear((home + mineralCenter) / 2, cloakSeen ? 2 : 1);
        }
    }
    bool wantRobo = templarArchivesSeen
                    || enemyRace == Races::Protoss
                       && (!rushSeen || gateways >= 3 || Broodwar->getFrameCount() >= 7000);
    if (coreDone && (wantRobo || reaversWanted) && count(UnitTypes::Protoss_Robotics_Facility) < 1)
    {
        build(UnitTypes::Protoss_Robotics_Facility, nearPylon);
    }
    else if (self->completedUnitCount(UnitTypes::Protoss_Robotics_Facility) > 0
             && count(UnitTypes::Protoss_Observatory) < 1)
    {
        build(UnitTypes::Protoss_Observatory, nearPylon);
    }
    else if (reaversWanted && self->completedUnitCount(UnitTypes::Protoss_Robotics_Facility) > 0
             && count(UnitTypes::Protoss_Robotics_Support_Bay) < 1)
    {
        build(UnitTypes::Protoss_Robotics_Support_Bay, nearPylon);
    }

    // A shield battery where the army waits, once two gateways and the core are up, and one more with the natural
    // (the user's advice: shields recharged at a battery, or left to regenerate, keep units alive far longer)
    if (coreDone && self->completedUnitCount(UnitTypes::Protoss_Gateway) >= 2 && !rushMode()
        && count(UnitTypes::Protoss_Shield_Battery) < (self->completedUnitCount(UnitTypes::Protoss_Nexus) >= 2 ? 2 : 1))
    {
        Unit rallyPylon = nullptr;
        for (auto pylon : self->getUnits())
        {
            if (pylon->getType() != UnitTypes::Protoss_Pylon || !pylon->isCompleted() || pylon->getDistance(rally) > 480)
                continue;
            if (!rallyPylon || pylon->getDistance(rally) < rallyPylon->getDistance(rally)) rallyPylon = pylon;
        }
        if (rallyPylon) build(UnitTypes::Protoss_Shield_Battery, rallyPylon->getTilePosition());
    }

    // The opening, step by step; a step waits for its requirements (and money), never skipped
    expansionDue = false;
    if (buildOrderStep < buildOrder.size())
    {
        auto &step = buildOrder[buildOrderStep];
        // Supply runs short when the opening's pylons can't keep up with the gateways: keep room for a round of units
        bool supplyBlocked = supplyTotal - supplyUsed <= 1 + 2 * finishedGateways && supplyTotal < 200
                             && pendingCount(UnitTypes::Protoss_Pylon) + self->incompleteUnitCount(UnitTypes::Protoss_Pylon) == 0;
        // Forge expand against an early pool: both cannons before the nexus
        bool cannonsFirst = forgeExpand && rushSeen && step.type == UnitTypes::Protoss_Nexus
                            && self->completedUnitCount(UnitTypes::Protoss_Forge) > 0
                            && count(UnitTypes::Protoss_Photon_Cannon) < 2;
        // A building for the natural's front needs a pylon there: if the first one was lost or never placed, another
        bool frontPowered = true;
        if (step.atNatural && step.type.requiresPsi() && frontTile.isValid())
        {
            frontPowered = false;
            for (auto pylon : self->getUnits())
            {
                if (pylon->getType() == UnitTypes::Protoss_Pylon && pylon->getTilePosition().getApproxDistance(frontTile) <= 7)
                {
                    frontPowered = true;
                }
            }
            for (auto &[builder, pending] : builders)
            {
                if (pending.type == UnitTypes::Protoss_Pylon && pending.tile.getApproxDistance(frontTile) <= 7)
                {
                    frontPowered = true;
                }
            }
        }
        if (!frontPowered)
        {
            build(UnitTypes::Protoss_Pylon, frontTile);
        }
        else if (supplyBlocked && step.type != UnitTypes::Protoss_Pylon)
        {
            build(UnitTypes::Protoss_Pylon, pylonSpot());
        }
        else if (cannonsFirst)
        {
            build(UnitTypes::Protoss_Photon_Cannon, frontTile);
        }
        else if (cannonsWait)
        {
            // The cannons by the minerals come first
        }
        else if (rushMode() && (step.type.isRefinery() || step.type.isResourceDepot()))
        {
            // During a rush, gas and the natural wait until a few zealots are out
        }
        else if (secondGatewayWaits(step))
        {
            // Zealots from the first gateway come first
        }
        else if (supplyUsed >= step.supply && (step.type == UnitTypes::Protoss_Pylon || Broodwar->canMake(step.type))
                 && build(step.type, step.atNatural && frontTile.isValid() ? frontTile : placeFor(step.type)))
        {
            buildOrderStep++;
        }
        if (step.type.isResourceDepot()) expansionDue = true;
        return;
    }

    // After the opening: supply first, keeping ahead of production
    int buffer = 3 + 3 * finishedGateways + 2 * (nexusCount - 1);
    int pendingPylons = pendingCount(UnitTypes::Protoss_Pylon) + self->incompleteUnitCount(UnitTypes::Protoss_Pylon);
    if (supplyTotal < 200 && supplyTotal - supplyUsed <= buffer && pendingPylons < 1 + finishedGateways / 3)
    {
        build(UnitTypes::Protoss_Pylon, pylonSpot());
        return;
    }

    // Mutalisks (or their spire) seen: cannons in every mineral line, and a stargate for corsairs. Dark templar or
    // lurkers too: with cannons only in the main, 7 dark templar killed 63 probes at the natural
    if (airSeen || hiddenArmySeen)
    {
        buildMineralLineCannons();
        if (coreDone && enemyRace == Races::Zerg && count(UnitTypes::Protoss_Stargate) < 1)
        {
            build(UnitTypes::Protoss_Stargate, nearPylon);
        }
    }

    // Spending: with 400+ minerals spare, take another base when it is safe and our army is holding its own (or we
    // have only one base); otherwise more gateways to strengthen the army
    auto base = nextBase();
    bool holding = army >= 6 && Broodwar->getFrameCount() - lastRetreatFrame > 480;
    // Never expand into an enemy army clearly bigger than ours: against Protoss, the natural waits for parity
    double myDurability = 0, myDps = 0, theirDurability = 0, theirDps = 0;
    for (auto unit : self->getUnits())
    {
        if (isArmy(unit->getType()) && unit->isCompleted())
            addToGroup(unit->getType(), unit->getHitPoints() + unit->getShields(), myDurability, myDps);
    }
    for (auto &[id, seen] : enemyArmy) addToGroup(seen.type, seen.health, theirDurability, theirDps);
    bool notOutmatched = groupStrength(myDurability, myDps) >= 0.8 * groupStrength(theirDurability, theirDps);
    int armyForNatural = enemyRace == Races::Protoss ? 8 : 6;
    bool wantBase = base && !underAttack && coreDone && pendingCount(UnitTypes::Protoss_Nexus) == 0
                    && !(rushSeen && Broodwar->getFrameCount() < 9000)
                    && notOutmatched
                    && (nexusCount < 2 ? army >= armyForNatural : holding && self->allUnitCount(UnitTypes::Protoss_Probe) >= 18 * nexusCount);
    expansionDue = wantBase;
    int gatewayCap = std::min(12, 4 + 3 * (self->completedUnitCount(UnitTypes::Protoss_Nexus) - 1));
    if (wantBase && freeMinerals >= 400)
    {
        build(UnitTypes::Protoss_Nexus, base->depot);
    }
    else if (coreDone && gateways < gatewayCap && freeMinerals >= (wantBase ? 550 : 250))
    {
        build(UnitTypes::Protoss_Gateway, nearPylon);
    }
    else if (count(UnitTypes::Protoss_Assimilator) < self->completedUnitCount(UnitTypes::Protoss_Nexus)
             && gateways >= 4)
    {
        build(UnitTypes::Protoss_Assimilator, TilePosition(home));
    }
    else if (self->completedUnitCount(UnitTypes::Protoss_Nexus) >= 2 && gateways >= 5
             && count(UnitTypes::Protoss_Forge) < 1)
    {
        build(UnitTypes::Protoss_Forge, nearPylon);
    }
    // Zealot speed with the spare gas: slow zealots chased marines that kited them, the gas banked up to 1,220, and
    // games against marines ran out of time while we were ahead
    else if (self->completedUnitCount(UnitTypes::Protoss_Nexus) >= 2 && gateways >= 5 && coreDone
             && count(UnitTypes::Protoss_Citadel_of_Adun) < 1 && self->gas() >= 150)
    {
        build(UnitTypes::Protoss_Citadel_of_Adun, nearPylon);
    }

    // Upgrades: dragoon range first, then ground weapons and armor from the forge
    for (auto building : self->getUnits())
    {
        if (!building->isCompleted() || !building->isIdle()) continue;
        if (building->getType() == UnitTypes::Protoss_Cybernetics_Core
            && self->getUpgradeLevel(UpgradeTypes::Singularity_Charge) == 0
            && !self->isUpgrading(UpgradeTypes::Singularity_Charge) && self->gas() >= 150 && self->minerals() >= 150)
        {
            building->upgrade(UpgradeTypes::Singularity_Charge);
        }
        if (building->getType() == UnitTypes::Protoss_Citadel_of_Adun
            && self->getUpgradeLevel(UpgradeTypes::Leg_Enhancements) == 0
            && !self->isUpgrading(UpgradeTypes::Leg_Enhancements) && self->gas() >= 150 && freeMinerals >= 150)
        {
            building->upgrade(UpgradeTypes::Leg_Enhancements);
        }
        if (building->getType() == UnitTypes::Protoss_Forge && freeMinerals >= 200)
        {
            if (!self->isUpgrading(UpgradeTypes::Protoss_Ground_Weapons)
                && self->getUpgradeLevel(UpgradeTypes::Protoss_Ground_Weapons) == 0)
            {
                building->upgrade(UpgradeTypes::Protoss_Ground_Weapons);
            }
            else if (!self->isUpgrading(UpgradeTypes::Protoss_Ground_Armor)
                     && self->getUpgradeLevel(UpgradeTypes::Protoss_Ground_Armor) == 0)
            {
                building->upgrade(UpgradeTypes::Protoss_Ground_Armor);
            }
        }
    }
}

// Two cannons between each completed nexus and its minerals (with a pylon there first), against air harassment
void ClaudeOpus55::buildMineralLineCannons()
{
    auto self = Broodwar->self();
    if (self->completedUnitCount(UnitTypes::Protoss_Forge) == 0) return;
    for (auto nexus : nexuses(true))
    {
        auto minerals = nexus->getUnitsInRadius(320, IsMineralField);
        if (minerals.empty()) continue;
        if (!cannonsNear((nexus->getPosition() + minerals.getPosition()) / 2, 2)) return;  // one base at a time
    }
}

// Cannons by a spot, with a pylon there first; true once `wanted` cannons are there or on their way. Pylons and cannons
// still building or about to be count: a pylon only gives power once finished, and buildings stay out of the mineral
// line, so the pylon lands a few tiles off. Counting neither made 16 pylons in a row while the first one went up
bool ClaudeOpus55::cannonsNear(Position spot, int wanted)
{
    int cannons = 0;
    Unit pylon = nullptr;
    bool pylonComing = false;
    for (auto unit : Broodwar->getUnitsInRadius(spot, 448, IsOwned))
    {
        if (unit->getType() == UnitTypes::Protoss_Photon_Cannon) cannons++;
        if (unit->getType() != UnitTypes::Protoss_Pylon || unit->getDistance(spot) >= 320) continue;
        if (!unit->isCompleted()) pylonComing = true;
        else if (!pylon || unit->getDistance(spot) < pylon->getDistance(spot)) pylon = unit;
    }
    for (auto &[builder, pending] : builders)
    {
        int distance = Position(pending.tile).getApproxDistance(spot);
        if (pending.type == UnitTypes::Protoss_Photon_Cannon && distance < 448) cannons++;
        if (pending.type == UnitTypes::Protoss_Pylon && distance < 320) pylonComing = true;
    }
    if (cannons >= wanted) return true;
    if (pylon) build(UnitTypes::Protoss_Photon_Cannon, TilePosition(spot));
    else if (!pylonComing) build(UnitTypes::Protoss_Pylon, TilePosition(spot));
    return false;
}

void ClaudeOpus55::trainUnits()
{
    auto self = Broodwar->self();
    int probes = self->allUnitCount(UnitTypes::Protoss_Probe);
    int freeMinerals = self->minerals() - reservedMinerals();
    int freeGas = self->gas() - reservedGas();
    // Against Protoss the robotics facility and observatory come before more dragoons (the gateways make zealots
    // meanwhile): with dragoons first the robotics facility waited until 5:57, and a dark templar at 7:12 killed 30
    // probes before the observer was out
    if (enemyRace == Races::Protoss && !rushMode() && self->completedUnitCount(UnitTypes::Protoss_Cybernetics_Core) > 0
        && (!rushSeen || count(UnitTypes::Protoss_Gateway) >= 3 || Broodwar->getFrameCount() >= 7000))
    {
        if (count(UnitTypes::Protoss_Robotics_Facility) < 1) freeGas -= 200;
        else if (count(UnitTypes::Protoss_Observatory) < 1) freeGas -= 100;
    }
    // On one base more than 22 probes add little mining; the money is better spent on the army. A few spare probes
    // go ahead of the next nexus, so it starts mining at once
    int nexusCount = (int) nexuses(false).size();
    int probeTarget = std::min(MaxProbes, (nexusCount == 1 ? 22 : ProbesPerBase * nexusCount) + (expansionDue ? 6 : 0));

    // Against Zerg the army has to keep growing from the start, as zerglings come in numbers we rarely see whole:
    // while it is behind, probes and the opening wait for gateway units
    int ourArmy = self->allUnitCount(UnitTypes::Protoss_Zealot) + self->allUnitCount(UnitTypes::Protoss_Dragoon);
    int frame = Broodwar->getFrameCount();
    bool armyBehind = enemyRace == Races::Zerg && frame > 4500 && ourArmy < (frame - 4000) / 600;

    // During a rush the gateways come first: past 12 probes, a probe only with a zealot's cost to spare
    int rushReserve = rushMode() && probes >= 12 ? 100 : 0;

    int pylonDue = 0;
    if (buildOrderStep < buildOrder.size() && buildOrder[buildOrderStep].type == UnitTypes::Protoss_Pylon
        && self->supplyUsed() / 2 >= buildOrder[buildOrderStep].supply)
    {
        pylonDue = 100;
    }
    // The first gateway goes down the moment the pylon powers it, ahead of more probes
    // ... and so does the cannon opening's forge (at 1:23 rather than Locutus's 1:14, its cannons were still building
    // when the zerglings came)
    if (buildOrderStep < buildOrder.size() && count(UnitTypes::Protoss_Pylon) > 0
        && (buildOrder[buildOrderStep].type == UnitTypes::Protoss_Gateway && count(UnitTypes::Protoss_Gateway) == 0
               && (!cannonOpening || openingCannonsStarted)
            || buildOrder[buildOrderStep].type == UnitTypes::Protoss_Forge && count(UnitTypes::Protoss_Forge) == 0))
    {
        pylonDue = 150;
    }

    // Against Zerg the gateway is never left idle for want of money while the first zealots are made (not against
    // Protoss: holding money back for zealots there lost all 6 games to UAlbertaBot's zealot rush, against 2 of 4 before)
    int zealotReserve = 0;
    if (enemyRace == Races::Zerg && frame < 9000 && ourArmy < 6)
    {
        for (auto gateway : self->getUnits())
        {
            if (gateway->getType() != UnitTypes::Protoss_Gateway) continue;
            if (gateway->isCompleted() ? gateway->isIdle() || gateway->getRemainingTrainTime() < 120
                                       : gateway->getRemainingBuildTime() < 120)
            {
                zealotReserve = 100;
            }
        }
    }

    // The cannon opening banks for all its cannons at once from 10 probes, as Locutus does: started one at a time
    // between probes, only one of three was finished when ZZZKBot's zerglings arrived at 2:25
    int cannonReserve = 0;
    int cannonsWanted = openingCannons();
    if (cannonOpening && !openingCannonsStarted && frame < 5000 && probes >= 10 && count(UnitTypes::Protoss_Forge) > 0
        && count(UnitTypes::Protoss_Photon_Cannon) < cannonsWanted)
    {
        cannonReserve = 150 * (cannonsWanted - count(UnitTypes::Protoss_Photon_Cannon));
    }

    for (auto nexus : nexuses(true))
    {
        if (nexus->isIdle() && probes < probeTarget
            && freeMinerals - pylonDue - std::max({armyBehind ? 100 : 0, rushReserve, zealotReserve, cannonReserve}) >= 50
            && self->supplyUsed() < self->supplyTotal())
        {
            nexus->train(UnitTypes::Protoss_Probe);
            freeMinerals -= 50;
            probes++;
        }
    }

    // The next opening building comes before the army: once it is due, keep its cost aside. Probes come first, except
    // that a due pylon waits for no one
    // ... unless the enemy army we know of outnumbers ours: then units first, or the opening never finishes
    int theirArmy = 0;
    for (auto &[id, seen] : enemyArmy) theirArmy += seen.type == UnitTypes::Zerg_Zergling ? 1 : 2;
    bool armyFirst = theirArmy > 2 * ourArmy || armyBehind || rushMode() || !threatsNearHome().empty();

    // Before the core, zealots from the two gateways (more when a rush is coming); three against Terran, four against
    // a marine rush
    int zealotsBeforeCore = enemyRace == Races::Terran ? (barracksRush ? 4 : 3) : (rushSeen ? 6 : 4);
    if (buildOrderStep < buildOrder.size())
    {
        auto &step = buildOrder[buildOrderStep];
        // Once those zealots are out the core's money is kept aside whatever else is going on: while marines kept
        // coming the zealots took every mineral, the core waited from 1:45 to 4:30 with 400 gas banked, and the
        // dragoons came too late
        bool coreDue = step.type == UnitTypes::Protoss_Cybernetics_Core
                       && self->allUnitCount(UnitTypes::Protoss_Zealot) >= zealotsBeforeCore;
        if ((!armyFirst || coreDue) && self->supplyUsed() / 2 >= step.supply && !secondGatewayWaits(step))
        {
            freeMinerals -= step.type.mineralPrice();
            freeGas -= step.type.gasPrice();
        }
    }
    if (count(UnitTypes::Protoss_Cybernetics_Core) == 0
        && self->allUnitCount(UnitTypes::Protoss_Zealot) >= zealotsBeforeCore && threatsNearHome().empty()) return;

    // Dragoon range as soon as possible, even during the opening
    for (auto core : self->getUnits())
    {
        if (core->getType() == UnitTypes::Protoss_Cybernetics_Core && core->isCompleted() && core->isIdle()
            && self->getUpgradeLevel(UpgradeTypes::Singularity_Charge) == 0
            && !self->isUpgrading(UpgradeTypes::Singularity_Charge) && freeMinerals >= 150 && freeGas >= 150)
        {
            core->upgrade(UpgradeTypes::Singularity_Charge);
            freeMinerals -= 150;
            freeGas -= 150;
        }
    }

    for (auto robo : self->getUnits())
    {
        if (robo->getType() == UnitTypes::Protoss_Robotics_Facility && robo->isCompleted() && robo->isIdle()
            && self->completedUnitCount(UnitTypes::Protoss_Observatory) > 0
            && self->allUnitCount(UnitTypes::Protoss_Observer) < (hiddenArmySeen ? 3 : 2)
            && freeMinerals >= 25 && freeGas >= 75)
        {
            robo->train(UnitTypes::Protoss_Observer);
            freeMinerals -= 25;
            freeGas -= 75;
        }
        // Up to four reavers once the support bay is up, ahead of the gateways
        else if (robo->getType() == UnitTypes::Protoss_Robotics_Facility && robo->isCompleted() && robo->isIdle()
                 && reaversWanted && self->completedUnitCount(UnitTypes::Protoss_Robotics_Support_Bay) > 0
                 && self->allUnitCount(UnitTypes::Protoss_Reaver) < 4 && freeMinerals >= 200 && freeGas >= 100
                 && self->supplyUsed() + 8 <= self->supplyTotal())
        {
            robo->train(UnitTypes::Protoss_Reaver);
            freeMinerals -= 200;
            freeGas -= 100;
        }
    }
    // Each reaver keeps its scarabs topped up
    for (auto reaver : self->getUnits())
    {
        if (reaver->getType() == UnitTypes::Protoss_Reaver && reaver->isCompleted() && !reaver->isTraining()
            && reaver->getScarabCount() < 5 && freeMinerals >= 15)
        {
            reaver->train(UnitTypes::Protoss_Scarab);
            freeMinerals -= 15;
        }
    }

    // Leave money for the natural once it is due
    if (expansionDue && pendingCount(UnitTypes::Protoss_Nexus) == 0) freeMinerals -= 400;

    // Corsairs against mutalisks: up to 8, before the gateways spend everything
    for (auto stargate : self->getUnits())
    {
        if (stargate->getType() == UnitTypes::Protoss_Stargate && stargate->isCompleted() && stargate->isIdle()
            && self->allUnitCount(UnitTypes::Protoss_Corsair) < 8 && freeMinerals >= 150 && freeGas >= 100
            && self->supplyUsed() + 4 <= self->supplyTotal())
        {
            stargate->train(UnitTypes::Protoss_Corsair);
            freeMinerals -= 150;
            freeGas -= 100;
        }
    }

    // Against zerglings, mostly zealots with a few dragoons; against hydralisks, lurkers or mutalisks, dragoons only
    bool canMakeDragoons = self->completedUnitCount(UnitTypes::Protoss_Cybernetics_Core) > 0;
    int zealots = self->allUnitCount(UnitTypes::Protoss_Zealot), dragoons = self->allUnitCount(UnitTypes::Protoss_Dragoon);
    // Against mostly marines, zealots alongside the dragoons: a dragoon's shot does half damage to small units, and 13
    // marines twice beat our 4 dragoons before 5:30
    int marines = 0, otherTerran = 0;
    for (auto &[id, seen] : enemyArmy)
    {
        if (seen.type == UnitTypes::Terran_Marine || seen.type == UnitTypes::Terran_Medic) marines++;
        else if (seen.type.getRace() == Races::Terran && !seen.type.isWorker()) otherTerran++;
    }
    bool marineArmy = enemyRace == Races::Terran && marines > 2 * otherTerran;
    for (auto gateway : self->getUnits())
    {
        if (gateway->getType() != UnitTypes::Protoss_Gateway || !gateway->isCompleted() || !gateway->isIdle()) continue;
        if (self->supplyUsed() + 4 > self->supplyTotal()) break;
        // Against a zealot rush, zealots alongside the dragoons: they hold the ramp while the dragoons shoot
        bool wantZealot = enemyRace == Races::Zerg && !rangedNeeded && zealots < 2 * dragoons + 2
                          || enemyRace == Races::Protoss && rushSeen && zealots < dragoons + 1
                          || marineArmy && zealots < dragoons + 2;
        if (wantZealot && freeMinerals >= 100)
        {
            gateway->train(UnitTypes::Protoss_Zealot);
            freeMinerals -= 100;
            zealots++;
        }
        else if (canMakeDragoons && freeMinerals >= 125 && freeGas >= 50)
        {
            gateway->train(UnitTypes::Protoss_Dragoon);
            freeMinerals -= 125;
            freeGas -= 50;
            dragoons++;
        }
        else if ((!canMakeDragoons || freeGas < 50) && freeMinerals >= 100)
        {
            gateway->train(UnitTypes::Protoss_Zealot);
            freeMinerals -= 100;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Information

void ClaudeOpus55::trackEnemy()
{
    if (enemyRace == Races::Unknown || enemyRace == Races::Random)
    {
        for (auto unit : Broodwar->enemy()->getUnits())
        {
            auto race = unit->getType().getRace();
            if (race == Races::Zerg || race == Races::Terran || race == Races::Protoss)
            {
                enemyRace = race;
                break;
            }
        }
    }

    for (auto enemy : Broodwar->enemies())
    {
        for (auto unit : enemy->getUnits())
        {
            auto type = unit->getType();
            // Only what stays hidden from the army: a burrowed zergling or an enemy in the fog doesn't need detection
            if (type == UnitTypes::Protoss_Dark_Templar || type == UnitTypes::Protoss_Templar_Archives
                || type == UnitTypes::Protoss_Arbiter || type == UnitTypes::Protoss_Arbiter_Tribunal
                || type == UnitTypes::Zerg_Lurker || type == UnitTypes::Zerg_Lurker_Egg
                || type == UnitTypes::Terran_Vulture_Spider_Mine || type == UnitTypes::Terran_Ghost
                || (type == UnitTypes::Terran_Wraith && unit->isCloaked()))
            {
                cloakSeen = true;
            }
            if (type == UnitTypes::Protoss_Dark_Templar || type == UnitTypes::Zerg_Lurker) hiddenArmySeen = true;
            if (type == UnitTypes::Protoss_Templar_Archives || type == UnitTypes::Protoss_Citadel_of_Adun)
            {
                templarArchivesSeen = true;
            }
            if (type == UnitTypes::Zerg_Spire || type == UnitTypes::Zerg_Mutalisk || type == UnitTypes::Zerg_Greater_Spire)
            {
                airSeen = true;
            }
            if (airSeen || type == UnitTypes::Zerg_Hydralisk_Den || type == UnitTypes::Zerg_Hydralisk
                || type == UnitTypes::Zerg_Lurker)
            {
                rangedNeeded = true;
            }
            if (Broodwar->getFrameCount() < 7500 && !rushSeen)
            {
                auto counts = [&](UnitType t) { return Broodwar->enemy()->visibleUnitCount(t); };
                // Zealots remembered rather than only those in sight: a gateway rush arrives a few at a time
                int zealotsKnown = 0;
                for (auto &[id, seen] : enemyArmy)
                {
                    if (seen.type == UnitTypes::Protoss_Zealot) zealotsKnown++;
                }
                int frame = Broodwar->getFrameCount();
                if (zealotsKnown >= 3 && frame < 6000 || zealotsKnown >= 5) rushSeen = true;
                int enemyWorkersNearHome = 0;
                for (auto worker : Broodwar->enemy()->getUnits())
                {
                    if (worker->getType().isWorker() && worker->getDistance(home) < 500) enemyWorkersNearHome++;
                }
                if (frame < 5000 && counts(UnitTypes::Terran_Barracks) >= 2) barracksRush = true;
                bool coreKnown = false;
                for (auto &[id, building] : enemyBuildings)
                {
                    if (building.first == UnitTypes::Protoss_Cybernetics_Core) coreKnown = true;
                }
                if (frame < 5000 && (enemyWorkersNearHome >= 3
                    // Two gateways and no core yet: zealots coming. A core alongside means dragoons or dark templar
                    || counts(UnitTypes::Protoss_Gateway) >= 2
                       && (frame < 3600 || frame < 4300 && !coreKnown)
                    || counts(UnitTypes::Terran_Barracks) >= 2 || counts(UnitTypes::Zerg_Zergling) >= 4
                    || counts(UnitTypes::Protoss_Zealot) >= 3
                    || type == UnitTypes::Zerg_Spawning_Pool && Broodwar->getFrameCount() < 2400))
                {
                    rushSeen = true;
                }
            }
            if (!unit->getType().isBuilding()) continue;
            enemyBuildings[unit->getID()] = {unit->getType(), unit->getPosition()};
            if (unit->getType().isResourceDepot() && enemyBase == Positions::Unknown) enemyBase = unit->getPosition();
        }
    }

    // Remember the enemy's army; forget units not seen for a minute and a half (they may have died out of sight)
    int now = Broodwar->getFrameCount();
    for (auto unit : Broodwar->enemy()->getUnits())
    {
        auto type = unit->getType();
        if (type.isBuilding() || type.isWorker() || !unit->isVisible()) continue;
        if (type.groundWeapon() == WeaponTypes::None && type.airWeapon() == WeaponTypes::None) continue;
        enemyArmy[unit->getID()] = {type, unit->getHitPoints() + unit->getShields(), now};
    }
    for (auto it = enemyArmy.begin(); it != enemyArmy.end();)
    {
        if (now - it->second.frame > 2200) it = enemyArmy.erase(it);
        else ++it;
    }

    // Reavers (ChatGPT's plan for LunaOpus55) against what dragoons attack badly: two siege tanks, three bunkers or
    // other static defences, or eight large ground units
    if (!reaversWanted)
    {
        int tanks = 0, large = 0, defences = 0, infantry = 0;
        for (auto &[id, seen] : enemyArmy)
        {
            if (seen.type == UnitTypes::Terran_Siege_Tank_Tank_Mode || seen.type == UnitTypes::Terran_Siege_Tank_Siege_Mode)
                tanks++;
            if (seen.type.size() == UnitSizeTypes::Large && !seen.type.isFlyer()) large++;
            if (seen.type == UnitTypes::Terran_Marine || seen.type == UnitTypes::Terran_Firebat
                || seen.type == UnitTypes::Terran_Medic)
                infantry++;
        }
        for (auto &[id, building] : enemyBuildings)
        {
            if (building.first == UnitTypes::Terran_Bunker || building.first == UnitTypes::Protoss_Photon_Cannon
                || building.first == UnitTypes::Zerg_Sunken_Colony)
                defences++;
        }
        // ... and against massed marines: 32 of them with medics beat 10 dragoons and 9 zealots in the open at 12:45
        if (tanks >= 2 || defences >= 3) reaversWanted = true;
    }

    for (auto it = enemyBuildings.begin(); it != enemyBuildings.end();)
    {
        auto tile = TilePosition(it->second.second);
        auto unit = Broodwar->getUnit(it->first);
        if (Broodwar->isVisible(tile) && (!unit || !unit->exists() || !unit->isVisible())) it = enemyBuildings.erase(it);
        else ++it;
    }

    if (enemyBase != Positions::Unknown && Broodwar->isVisible(TilePosition(enemyBase)))
    {
        bool depotThere = false;
        for (auto &[id, building] : enemyBuildings)
        {
            if (building.first.isResourceDepot() && building.second.getApproxDistance(enemyBase) < 128) depotThere = true;
        }
        if (!depotThere && scoutTargets.size() > 1) enemyBase = Positions::Unknown;
    }
}

void ClaudeOpus55::scoutEnemy()
{
    auto self = Broodwar->self();
    int frame = Broodwar->getFrameCount();
    auto finish = [&]()
    {
        if (scout && scout->exists()) scout->stop();  // idle, so it goes back to mining
        scout = nullptr;
        scoutingDone = true;
    };
    if (scoutingDone) return;
    if (enemyBase != Positions::Unknown)
    {
        // The main is known (a two-player map, or just found): the scout looks round it once, for an early pool, a
        // proxy-free main (so the gateways are elsewhere), tech buildings and the size of the army
        if (frame > 6000 || scout && !scout->exists())
        {
            finish();
            return;
        }
        if (self->supplyUsed() / 2 < 9) return;
        if (!scout)
        {
            scout = chooseBuilder(home);
            if (!scout) return;
        }
        if (scoutArrivedFrame < 0)
        {
            if (scout->getDistance(enemyBase) > 320)
            {
                if (scout->getTargetPosition().getApproxDistance(enemyBase) > 64) scout->move(enemyBase);
                return;
            }
            scoutArrivedFrame = frame;
        }
        int elapsed = frame - scoutArrivedFrame;
        if (elapsed > 720 || scout->isUnderAttack() && scout->getShields() < 10)
        {
            finish();
            return;
        }
        if (elapsed % 12 == 0)
        {
            double angle = elapsed / 720.0 * 2 * 3.14159 * 1.5;  // a lap and a half
            scout->move(Position(enemyBase.x + int(std::cos(angle) * 256), enemyBase.y + int(std::sin(angle) * 256)).makeValid());
        }
        return;
    }
    if (self->supplyUsed() / 2 < 9) return;

    if (!scout || !scout->exists())
    {
        scout = chooseBuilder(home);
        if (!scout) return;
    }
    while (!scoutTargets.empty() && Broodwar->isExplored(scoutTargets.front()))
    {
        scoutTargets.erase(scoutTargets.begin());
    }
    if (scoutTargets.empty()) return;
    auto target = Position(scoutTargets.front()) + Position(64, 48);
    if (scout->getTargetPosition() != target) scout->move(target);
}

// ---------------------------------------------------------------------------------------------------------------------
// Army

Unitset ClaudeOpus55::threatsNearHome() const
{
    Unitset threats, workers;
    bool haveNatural = natural && Broodwar->self()->allUnitCount(UnitTypes::Protoss_Nexus) >= 2;
    for (auto enemy : Broodwar->enemies())
    {
        for (auto unit : enemy->getUnits())
        {
            if (!unit->isVisible() || unit->getType().isFlyer() && !unit->getType().canAttack()) continue;
            if (unit->getType().isBuilding() && !unit->getType().canAttack()) continue;
            if (!unit->isDetected()) continue;  // nothing the army can do about it; observers and cannons can
            bool near = unit->getDistance(home) < 900 || haveNatural && unit->getDistance(natural->center) < 600;
            if (!near) continue;
            // A worker attacking ours (or building in our base) is a threat on its own: a lone drone or SCV left
            // alone kills probe after probe. Workers only walking about count when there are several
            if (unit->getType().isWorker() && !hostile(unit)) workers.insert(unit);
            else threats.insert(unit);
        }
    }
    if (workers.size() >= 3) threats.insert(workers.begin(), workers.end());
    return threats;
}

bool ClaudeOpus55::rushMode() const
{
    if (!rushSeen || Broodwar->getFrameCount() >= 9000) return false;

    // Four units are not enough while the rush keeps coming (5 zealots killed 4 of ours at the ramp once the gas and
    // probes had started again): the rush lasts until our army, those in production too, matches the one seen
    double myDurability = 0, myDps = 0, theirDurability = 0, theirDps = 0;
    int units = 0;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType() == UnitTypes::Protoss_Photon_Cannon && unit->isCompleted())
        {
            addToGroup(unit->getType(), unit->getHitPoints() + unit->getShields(), myDurability, myDps);
        }
        if (!isArmy(unit->getType())) continue;
        units++;
        addToGroup(unit->getType(), unit->isCompleted() ? unit->getHitPoints() + unit->getShields() : 0, myDurability, myDps);
    }
    for (auto &[id, seen] : enemyArmy) addToGroup(seen.type, seen.health, theirDurability, theirDps);
    return units < 4 || groupStrength(myDurability, myDps) < groupStrength(theirDurability, theirDps);
}

void ClaudeOpus55::addToGroup(UnitType type, int health, double &durability, double &dps)
{
    if (type == UnitTypes::Terran_Bunker)
    {
        durability += 350;
        dps += 4 * 6.0 / 15;  // about four marines inside
        return;
    }
    auto weapon = type.groundWeapon();
    if (weapon == WeaponTypes::None) weapon = type.airWeapon();
    if (weapon == WeaponTypes::None || type.isWorker()) return;
    durability += health > 0 ? health : type.maxHitPoints() + type.maxShields();
    dps += weapon.damageAmount() * std::max(1, weapon.damageFactor() * type.maxGroundHits())
           / double(std::max(1, weapon.damageCooldown()));
}

// How much a unit contributes to a fight: durability times damage output
double ClaudeOpus55::strength(Unit unit)
{
    auto type = unit->getType();
    if (type == UnitTypes::Terran_Bunker) return 350.0 * 4 * 6.0 / 15;  // about four marines inside
    auto weapon = type.groundWeapon();
    if (weapon == WeaponTypes::None) weapon = type.airWeapon();
    if (weapon == WeaponTypes::None || type.isWorker()) return 0;
    double damage = weapon.damageAmount() * std::max(1, weapon.damageFactor() * type.maxGroundHits());
    double dps = damage / std::max(1, weapon.damageCooldown());
    double durability = unit->getHitPoints() + unit->getShields();
    if (durability <= 0) durability = type.maxHitPoints() + type.maxShields();
    return durability * dps;
}

void ClaudeOpus55::fight(Unit unit, Position goal)
{
    // A badly damaged unit steps back so healthier ones take the front (only with enemies close)
    if (unit->getShields() == 0 && unit->getHitPoints() * 2 < unit->getType().maxHitPoints()
        && unit->getType() == UnitTypes::Protoss_Dragoon)
    {
        auto closest = unit->getClosestUnit(IsEnemy && IsVisible && CanAttack, 224);
        if (closest)
        {
            unit->move(towards(unit->getPosition(), unit->getPosition() * 2 - closest->getPosition(), 96));
            return;
        }
    }
    // ... and so does any unit low on shields while a fresher one beside it can take its place (the user's advice:
    // shields come back, hit points don't)
    auto maxShields = unit->getType().maxShields();
    if (maxShields > 0 && !hurtNeeded && unit->getShields() * 4 < maxShields && unit->getType() != UnitTypes::Protoss_Reaver)
    {
        auto closest = unit->getClosestUnit(IsEnemy && IsVisible && CanAttack && !IsWorker, 192);
        auto fresher = unit->getClosestUnit(IsOwned && IsCompleted && [&](Unit u) {
            return isArmy(u->getType()) && u->getShields() * 2 >= u->getType().maxShields();
        }, 128);
        if (closest && fresher)
        {
            unit->move(towards(unit->getPosition(), unit->getPosition() * 2 - closest->getPosition(), 64));
            return;
        }
    }

    // Dragoons step back from melee units while their weapon reloads
    if (unit->getType() == UnitTypes::Protoss_Dragoon && unit->getGroundWeaponCooldown() > 8)
    {
        auto melee = unit->getClosestUnit(IsEnemy && IsVisible && !IsFlying && !IsWorker
                                          && [](Unit u) { return isMelee(u->getType()); }, 96);
        if (melee)
        {
            unit->move(towards(unit->getPosition(), unit->getPosition() * 2 - melee->getPosition(), 64));
            return;
        }
    }

    // Pick a target: the most important kind first, then one already in weapon range (walking to a far target
    // while something close hits us loses fights), then the one closest to dying
    Unit best = nullptr;
    int bestScore = INT_MIN;
    auto self = Broodwar->self();
    auto groundWeapon = unit->getType().groundWeapon(), airWeapon = unit->getType().airWeapon();
    bool hitsGround = groundWeapon != WeaponTypes::None, hitsAir = airWeapon != WeaponTypes::None;
    int groundRange = hitsGround ? self->weaponMaxRange(groundWeapon) : 0;
    int airRange = hitsAir ? self->weaponMaxRange(airWeapon) : 0;
    int range = std::max({groundRange, airRange, 32}) + 64;
    auto inReach = [&](Unit enemy, int slack) {
        return unit->getDistance(enemy) <= (enemy->isFlying() ? airRange : groundRange) + slack;
    };
    // Focus fire (ChatGPT's plan for LunaOpus55): dragoons prefer a target in range that others are already shooting,
    // so it dies before it can do more harm, but not one their shots in flight will already kill
    bool focus = unit->getType() == UnitTypes::Protoss_Dragoon;
    auto othersAim = [&](Unit enemy) {
        auto it = aimedDamage.find(enemy);
        int aimed = it == aimedDamage.end() ? 0 : it->second;
        if (unit->getOrderTarget() == enemy) aimed -= Broodwar->getDamageFrom(unit->getType(), enemy->getType(), self, enemy->getPlayer());
        return aimed;
    };
    auto overkilled = [&](Unit enemy) {
        return focus && othersAim(enemy) >= enemy->getHitPoints() + enemy->getShields();
    };
    for (auto enemy : unit->getUnitsInRadius(range + 96, IsEnemy && IsVisible))
    {
        if (!enemy->isDetected() || (enemy->isFlying() ? !hitsAir : !hitsGround)) continue;
        int health = enemy->getHitPoints() + enemy->getShields();
        int score = targetPriority(enemy) * 10000 + (inReach(enemy, 16) ? 5000 : 0) - health - unit->getDistance(enemy);
        if (focus && inReach(enemy, 16))
        {
            if (overkilled(enemy)) score -= 3000;
            else if (othersAim(enemy) > 0) score += 400;
        }
        if (score > bestScore)
        {
            best = enemy;
            bestScore = score;
        }
    }
    if (best)
    {
        // Keep shooting the current target while it is as important and still in weapon range: re-targeting every
        // frame cancels the attack before it fires (a dragoon's shot takes several frames to come out)
        auto current = unit->getOrderTarget();
        if (current && current->exists() && current->isVisible() && current->getPlayer() == Broodwar->enemy()
            && targetPriority(current) >= targetPriority(best) && (inReach(current, 16) || !inReach(best, 16))
            && !(overkilled(current) && current != best))
        {
            if (unit->getOrder() != Orders::AttackUnit) unit->attack(current);
            return;
        }
        if (Broodwar->getFrameCount() - unit->getLastCommandFrame() < 6 && unit->getOrder() == Orders::AttackUnit) return;
        unit->attack(best);
        return;
    }
    if (unit->getOrder() != Orders::AttackMove || unit->getTargetPosition().getApproxDistance(goal) > 64)
    {
        unit->attack(goal);
    }
}

void ClaudeOpus55::controlArmy()
{
    int frame = Broodwar->getFrameCount();
    Unitset army;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (isArmy(unit->getType()) && unit->isCompleted()) army.insert(unit);
    }
    aimedDamage.clear();
    hurtNeeded = false;
    for (auto unit : army)
    {
        auto target = unit->getOrderTarget();
        if (unit->getOrder() != Orders::AttackUnit || !target || !target->exists() || target->getPlayer() != Broodwar->enemy())
            continue;
        if (unit->getDistance(target) > 256) continue;
        aimedDamage[target] += Broodwar->getDamageFrom(unit->getType(), target->getType(), Broodwar->self(), target->getPlayer());
    }

    // CO55_DEBUG=3: what each unit is doing while enemies are close
    static bool fightDebug = getenv("CO55_DEBUG") && atoi(getenv("CO55_DEBUG")) >= 3;
    if (fightDebug && frame % 24 == 0)
    {
        for (auto unit : army)
        {
            auto enemy = unit->getClosestUnit(IsEnemy && IsVisible && !IsWorker && !IsBuilding, 320);
            if (!enemy) continue;
            printf("FIGHT %d %s hp %d order %s target %s dist %d cd %d attacking %d enemyDist %d\n", frame,
                   unit->getType().getName().c_str() + 8, unit->getHitPoints() + unit->getShields(),
                   unit->getOrder().getName().c_str(),
                   unit->getOrderTarget() ? unit->getOrderTarget()->getType().getName().c_str() : "-",
                   unit->getOrderTarget() ? unit->getDistance(unit->getOrderTarget()) : -1,
                   unit->getGroundWeaponCooldown(), attacking, unit->getDistance(enemy));
        }
    }

    controlObservers(army);
    controlCorsairs(army);

    // Once the natural is ours, the army waits in front of it rather than at the top of the main ramp
    // (or as soon as it is due, to clear the spot and cover the new nexus)
    if (naturalFront.isValid() && (Broodwar->self()->allUnitCount(UnitTypes::Protoss_Nexus) >= 2 || expansionDue))
    {
        rally = towards(naturalFront, Position(natural->depot) + Position(64, 48), 64);
    }
    // During a rush it waits in front of the main nexus instead: at the ramp, outside the home defence's reach, the
    // zealots arrived one at a time and died to the rush there; by the nexus the probes and cannons fight with them
    else if (rushMode())
    {
        rally = towards(home, rampTop, 160);
    }
    else
    {
        rally = rampTop;
    }

    // Dark templar (or lurkers) the army can see but not hit: it falls back to the cannons by the main's minerals, which
    // detect them, rather than standing in them (7 dragoons died at once to two dark templar at the natural)
    bool observerOut = Broodwar->self()->completedUnitCount(UnitTypes::Protoss_Observer) > 0;
    bool hiddenNearArmy = false;
    for (auto enemy : Broodwar->enemy()->getUnits())
    {
        auto type = enemy->getType();
        if (type != UnitTypes::Protoss_Dark_Templar && type != UnitTypes::Zerg_Lurker) continue;
        if (!enemy->isVisible() || enemy->isDetected()) continue;
        for (auto unit : army)
        {
            if (unit->getDistance(enemy) < 480) hiddenNearArmy = true;
        }
    }
    if (hiddenNearArmy)
    {
        if (attacking)
        {
            attacking = false;
            wave++;
            lastRetreatFrame = frame;
        }
        Position cannons = (home + mineralCenter) / 2;
        for (auto unit : army)
        {
            if (unit->getDistance(cannons) > 128)
            {
                if (unit->getTargetPosition().getApproxDistance(cannons) > 64) unit->move(cannons);
            }
            else
            {
                fight(unit, cannons);
            }
        }
        return;
    }

    // Defend first: everything goes for enemies near home, with probes helping against an early rush
    auto threats = threatsNearHome();
    int fighterThreats = 0;
    for (auto threat : threats)
    {
        if (!threat->getType().isWorker()) fighterThreats++;
    }

    // Only workers about (an SCV army, a worker harass): a home guard deals with them and the rest of the army is free
    // to attack, or it waits at home for as long as they keep wandering in
    Unitset guard;
    if (!threats.empty() && fighterThreats == 0 && army.size() >= 4)
    {
        size_t guards = std::min(army.size() / 2, std::max<size_t>(2, threats.size() / 3 + 1));
        std::vector<Unit> byDistance(army.begin(), army.end());
        std::sort(byDistance.begin(), byDistance.end(),
                  [&](Unit a, Unit b) { return a->getDistance(home) < b->getDistance(home); });
        for (size_t i = 0; i < guards; i++) guard.insert(byDistance[i]);
    }

    if (!threats.empty())
    {
        if (fighterThreats >= 3 && attacking)
        {
            attacking = false;
            wave++;
            lastRetreatFrame = frame;
        }
        Position target = threats.getPosition();

        // Outmatched: hold by the nexus (with the probes and cannons to help) and fight only what reaches the base,
        // rather than walking out to an army that is waiting for us
        double ourDurability = 0, ourDps = 0, threatDurability = 0, threatDps = 0;
        for (auto unit : army) addToGroup(unit->getType(), unit->getHitPoints() + unit->getShields(), ourDurability, ourDps);
        for (auto threat : threats) addToGroup(threat->getType(), threat->getHitPoints() + threat->getShields(), threatDurability, threatDps);
        // During a rush the few zealots we can see are scouts for the rest: the army met the whole rush in the open
        // when it went out to them, so it stays home until it is a match for everything seen
        bool outmatched = groupStrength(ourDurability, ourDps) < 1.2 * groupStrength(threatDurability, threatDps)
                          || rushMode();
        bool atBase = false;
        for (auto threat : threats)
        {
            if (threat->getDistance(home) < 400
                || threat->getClosestUnit(IsOwned && IsBuilding, 160) && threat->getDistance(home) < 700)
            {
                atBase = true;
            }
        }
        // Against zerglings it holds in the mineral line, where the probes fight beside it: a zealot met 5 zerglings
        // in front of the nexus alone and died, and the probes then fought them a few at a time (13 lost for 4)
        bool zerglingsIn = false;
        for (auto threat : threats)
        {
            if (threat->getType() == UnitTypes::Zerg_Zergling && threat->getDistance(home) < 400) zerglingsIn = true;
        }
        Position holdAt = towards(home, mineralCenter, zerglingsIn ? 96 : -96);

        for (auto unit : guard.empty() ? army : guard)
        {
            if (attacking && unit->getDistance(home) >= 1200 && guard.empty()) continue;
            if (!outmatched || atBase || unit->getClosestUnit(IsEnemy && IsVisible && !IsFlying, 160))
            {
                fight(unit, atBase && outmatched ? holdAt : target);
            }
            else if (unit->getDistance(holdAt) > 128 && unit->getTargetPosition().getApproxDistance(holdAt) > 64)
            {
                unit->move(holdAt);
            }
        }

        // Probes only help against enemies right at the nexus that are fighting, when the army can't cope alone.
        // Enemy workers that only walk through are left to the army: chasing them stops the mining, which is what a
        // worker harass is after
        int fightersAtNexus = 0, workersAtNexus = 0, defenders = 0, zerglings = 0;
        for (auto threat : threats)
        {
            if (threat->getDistance(home) < 300 && hostile(threat))
            {
                (threat->getType().isWorker() ? workersAtNexus : fightersAtNexus)++;
                if (threat->getType() == UnitTypes::Zerg_Zergling) zerglings++;
            }
        }
        for (auto unit : army)
        {
            if (unit->getDistance(home) < 600) defenders++;
        }
        // A finished cannon by the minerals counts as two defenders: probes pulled out after zerglings left its range
        // and died there (11 lost beside three cannons)
        defenders += 2 * int(Broodwar->getUnitsInRadius(home, 400, IsOwned && IsCompleted
                                                                   && GetType == UnitTypes::Protoss_Photon_Cannon).size());
        // Two probes beat an attacking worker (one loses to an SCV); the army nearby takes some of that on
        // A zergling outfights two probes one at a time, so against them two probes each go: six four-pool zerglings
        // met one zealot and eight probes and killed 13 of them for 4 zerglings
        int toPull = fightersAtNexus > 0
                     ? std::max(0, fightersAtNexus * 3 / 2 + zerglings / 2 + 1 - defenders * 2) : 0;
        toPull = std::min(16, toPull + std::max(0, workersAtNexus * 2 - defenders));
        for (auto probe : Broodwar->self()->getUnits())
        {
            if (probe->getType().isWorker() && probe->getOrder() == Orders::AttackUnit) toPull--;
        }
        for (auto probe : Broodwar->self()->getUnits())
        {
            if (toPull <= 0) break;
            if (!probe->getType().isWorker() || probe == scout || builders.count(probe)) continue;
            if (probe->getOrder() == Orders::AttackUnit) continue;
            auto enemy = probe->getClosestUnit(IsEnemy && !IsFlying && IsVisible, zerglings > 0 ? 320 : 200);
            if (enemy && enemy->getDistance(home) < 300 && hostile(enemy))
            {
                probe->attack(enemy);
                toPull--;
            }
        }
    }

    // Pulled probes go back to mining as soon as their target is dead or has left the nexus area
    auto mineNexus = nexusNeedingWorkers();
    for (auto probe : Broodwar->self()->getUnits())
    {
        if (!mineNexus || !probe->getType().isWorker() || probe == scout || builders.count(probe)) continue;
        if (probe->getOrder() != Orders::AttackUnit) continue;
        auto target = probe->getOrderTarget();
        if (!target || !target->exists() || target->getDistance(home) >= 300 || probe->getDistance(target) > 160
            || !hostile(target))
        {
            // Zerglings dodge between probes: one that steps away is replaced by the next one close by, or the probes
            // go back to mining one at a time while the rest are eaten (12 probes lost for 2 zerglings)
            auto next = probe->getClosestUnit(IsEnemy && IsVisible && !IsFlying && GetType == UnitTypes::Zerg_Zergling, 96);
            if (next && next->getDistance(home) < 300)
            {
                probe->attack(next);
                continue;
            }
            auto mineral = mineNexus->getClosestUnit(IsMineralField, 400);
            if (mineral) probe->gather(mineral);
        }
    }
    if (!threats.empty() && guard.empty() && !attacking) return;
    for (auto unit : guard) army.erase(unit);

    // Attack once the army is big enough and clearly stronger than the enemy army we know about (or nearly maxed)
    int needed = wave == 0 ? FirstAttackArmy : LaterAttackArmy;
    bool maxed = Broodwar->self()->supplyUsed() / 2 >= 150;
    double myDurability = 0, myDps = 0, theirDurability = 0, theirDps = 0;
    for (auto unit : army) addToGroup(unit->getType(), unit->getHitPoints() + unit->getShields(), myDurability, myDps);
    for (auto &[id, seen] : enemyArmy) addToGroup(seen.type, seen.health, theirDurability, theirDps);
    bool stronger = groupStrength(myDurability, myDps) >= 1.5 * groupStrength(theirDurability, theirDps);
    // ... and with its shields back: they regenerate (or recharge at a battery) for free
    int shields = 0, maxShields = 0;
    for (auto unit : army)
    {
        shields += unit->getShields();
        maxShields += unit->getType().maxShields();
    }
    bool shieldsUp = shields * 10 >= maxShields * 4;  // 40% is enough (the user), not a full recharge
    if (!attacking && ((int) army.size() >= needed && stronger && shieldsUp || maxed) && frame - lastRetreatFrame > 480
        && (observerOut || !hiddenArmySeen))
    {
        attacking = true;
    }
    if (attacking && (int) army.size() < RetreatBelow)
    {
        attacking = false;
        wave++;
        lastRetreatFrame = frame;
    }

    Position target = rally;
    if (attacking)
    {
        if (enemyBase != Positions::Unknown)
        {
            target = enemyBase;
        }
        else if (!enemyBuildings.empty())
        {
            target = enemyBuildings.begin()->second.second;
        }
        else if (!scoutTargets.empty())
        {
            target = Position(scoutTargets.front()) + Position(64, 48);
        }
        if (enemyBase != Positions::Unknown && Broodwar->isVisible(TilePosition(enemyBase)) && enemyBuildings.empty())
        {
            for (auto &base : bases)
            {
                if (!Broodwar->isVisible(TilePosition(base.center)))
                {
                    target = base.center;
                    break;
                }
            }
        }
        if (!enemyBuildings.empty() && Broodwar->isVisible(TilePosition(target)))
        {
            target = enemyBuildings.begin()->second.second;
        }

        // The leading group is the units near the one closest to the target; reinforcements join it
        Unit leader = nullptr;
        for (auto unit : army)
        {
            if (!leader || unit->getDistance(target) < leader->getDistance(target)) leader = unit;
        }
        Unitset group;
        for (auto unit : army)
        {
            if (leader && unit->getDistance(leader) < 500) group.insert(unit);
        }
        Position center = group.empty() ? army.getPosition() : group.getPosition();
        double groupDurability = 0, groupDps = 0, nearDurability = 0, nearDps = 0;

        // Higher ground at the target: attacking up a ramp needs a much stronger army
        bool uphill = Broodwar->getGroundHeight(TilePosition(target)) > Broodwar->getGroundHeight(TilePosition(center));

        // Fight only when the nearby fight looks winnable; otherwise fall back and rebuild
        for (auto unit : army)
        {
            if (unit->getDistance(center) < 600)
            {
                addToGroup(unit->getType(), unit->getHitPoints() + unit->getShields(), groupDurability, groupDps);
            }
        }
        for (auto enemy : Broodwar->enemy()->getUnits())
        {
            if (enemy->isVisible() && enemy->getDistance(center) < 700)
            {
                addToGroup(enemy->getType(), enemy->getHitPoints() + enemy->getShields(), nearDurability, nearDps);
            }
        }
        double ours = groupStrength(groupDurability, groupDps), theirs = groupStrength(nearDurability, nearDps);
        // Damaged units stay in only when they are what makes the fight decisive (the user's advice): 1.5 times the
        // enemy nearby with them, short of it without
        double freshDurability = 0, freshDps = 0;
        for (auto unit : army)
        {
            if (unit->getDistance(center) < 600 && unit->getShields() * 4 >= unit->getType().maxShields())
                addToGroup(unit->getType(), unit->getHitPoints() + unit->getShields(), freshDurability, freshDps);
        }
        hurtNeeded = theirs > 0 && ours >= 1.5 * theirs && groupStrength(freshDurability, freshDps) < 1.5 * theirs;
        if (theirs * (uphill ? 2.0 : 1.0) > ours * RetreatRatio && !maxed)
        {
            attacking = false;
            wave++;
            lastRetreatFrame = frame;
            target = rally;
        }
        else
        {
            // Before contact, gather short of the target so the army arrives together (a wide front, not a line)
            bool contact = false;
            for (auto unit : group)
            {
                if (unit->getClosestUnit(IsEnemy && IsVisible && !IsFlying, 400)) contact = true;
            }
            int spread = 0;
            for (auto unit : group) spread = std::max(spread, unit->getDistance(center));
            // Only units close enough to catch up count: with ten gateways the stream of new units from home never
            // made three quarters of the army, and the front stood still for five minutes killing marines one by
            // one until the frame limit. And no wave gathers for more than about 15 seconds
            size_t closeBy = 0;
            for (auto unit : army)
            {
                if (unit->getDistance(center) < 1200) closeBy++;
            }
            if (contact || gatherStart < 0 || frame - gatherStart > 1500) gatherStart = frame;
            bool gathering = !contact && (group.size() < closeBy * 3 / 4 || spread > 250) && frame - gatherStart < 360;
            if (gathering && center.getApproxDistance(target) > 600)
            {
                for (auto unit : army)
                {
                    if (unit->getDistance(center) > 96 && unit->getTargetPosition().getApproxDistance(center) > 64)
                    {
                        unit->move(center);
                    }
                }
                return;
            }

            Unit battery = nullptr;
            for (auto unit : Broodwar->self()->getUnits())
            {
                if (unit->getType() == UnitTypes::Protoss_Shield_Battery && unit->isCompleted()) battery = unit;
            }
            for (auto unit : army)
            {
                // Units out of shields and down to half their hit points go home to defend it and recharge
                if (battery && !hurtNeeded && unit->getShields() == 0 && unit->getHitPoints() * 2 < unit->getType().maxHitPoints()
                    && unit->getType() != UnitTypes::Protoss_Reaver && unit->getDistance(battery) > 640)
                {
                    if (unit->getTargetPosition().getApproxDistance(battery->getPosition()) > 96) unit->move(battery->getPosition());
                    continue;
                }
                // Stragglers join the group before it engages
                bool engaged = unit->getClosestUnit(IsEnemy && IsVisible && !IsFlying, 320) != nullptr;
                if (!engaged && unit->getDistance(center) > RegroupDistance)
                {
                    if (unit->getTargetPosition().getApproxDistance(center) > 96) unit->move(center);
                }
                else
                {
                    fight(unit, target);
                }
            }
            return;
        }
    }

    for (auto unit : army)
    {
        // Units with their shields down recharge at a battery nearby when nothing is close
        if (unit->getShields() * 2 < unit->getType().maxShields() && !unit->getClosestUnit(IsEnemy && IsVisible && CanAttack, 224))
        {
            auto battery = unit->getClosestUnit(IsOwned && IsCompleted && GetType == UnitTypes::Protoss_Shield_Battery
                                                && Energy >= 20, 640);
            if (battery)
            {
                if (unit->getOrder() != Orders::RechargeShieldsUnit) unit->rightClick(battery);
                continue;
            }
        }
        if (unit->getDistance(rally) > 192)
        {
            // Retreating units still shoot back at anything right next to them
            if (unit->getClosestUnit(IsEnemy && IsVisible && !IsFlying, 96) && !attacking)
            {
                fight(unit, rally);
            }
            else if (unit->getTargetPosition() != rally || unit->isIdle())
            {
                unit->move(rally);
            }
        }
        else
        {
            fight(unit, rally);
        }
    }
}

// Corsairs guard the mineral lines (or fly with the attacking army) and shoot down any flyer that comes near
void ClaudeOpus55::controlCorsairs(const Unitset &army)
{
    Unitset corsairs;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType() == UnitTypes::Protoss_Corsair && unit->isCompleted()) corsairs.insert(unit);
    }
    if (corsairs.empty()) return;
    Position goal = attacking && !army.empty() ? army.getPosition() : (home + mineralCenter) / 2;

    // Mutalisks anywhere near our bases or the army come first; otherwise overlords near the flock once it has 4
    Unit target = nullptr;
    int best = INT_MAX;
    for (auto enemy : Broodwar->enemy()->getUnits())
    {
        if (!enemy->isVisible() || !enemy->isFlying() || !enemy->isDetected()) continue;
        bool dangerous = enemy->getType().canAttack();
        if (!dangerous && corsairs.size() < 4) continue;
        int distance = enemy->getDistance(corsairs.getPosition());
        int fromHome = std::min(enemy->getDistance(home), enemy->getDistance(goal));
        if (dangerous ? fromHome > 1200 : distance > 500) continue;
        int score = distance - (dangerous ? 2000 : 0);
        if (score < best)
        {
            best = score;
            target = enemy;
        }
    }
    for (auto corsair : corsairs)
    {
        if (target)
        {
            if (corsair->getOrderTarget() != target) corsair->attack(target);
        }
        else if (corsair->getDistance(goal) > 96 && corsair->getTargetPosition().getApproxDistance(goal) > 64)
        {
            corsair->move(goal);
        }
    }
}

// The first observer stays with the army (or over the base while it is home); the second watches the mineral line
void ClaudeOpus55::controlObservers(const Unitset &army)
{
    std::vector<Unit> observers;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType() == UnitTypes::Protoss_Observer && unit->isCompleted()) observers.push_back(unit);
    }
    // Cloaked enemies at our bases come first for the observers not with the army: 7 dark templar killed 63 probes at
    // the natural while one observer sat over the army and the other over the main's minerals
    std::vector<Position> hidden;
    for (auto enemy : Broodwar->enemy()->getUnits())
    {
        if (!enemy->isVisible() || enemy->isDetected() || enemy->getType().isBuilding()) continue;
        for (auto nexus : nexuses(false))
        {
            if (enemy->getDistance(nexus) < 640)
            {
                hidden.push_back(enemy->getPosition());
                break;
            }
        }
    }
    std::vector<Unit> otherNexuses;
    for (auto nexus : nexuses(true))
    {
        if (nexus != mainNexus) otherNexuses.push_back(nexus);
    }
    for (size_t i = 0; i < observers.size(); i++)
    {
        Position goal = (home + mineralCenter) / 2;
        bool withArmy = i == 0 && !army.empty();
        if (withArmy)
        {
            // Over the army, a little behind its front
            goal = army.getPosition();
            if (attacking) goal = towards(goal, home, 64);
        }
        else if (!hidden.empty())
        {
            auto closest = std::min_element(hidden.begin(), hidden.end(), [&](Position a, Position b) {
                return observers[i]->getDistance(a) < observers[i]->getDistance(b);
            });
            goal = *closest;
            hidden.erase(closest);
        }
        else if (i >= 2 && !otherNexuses.empty())
        {
            // The third watches the other mineral lines in turn
            auto nexus = otherNexuses[(Broodwar->getFrameCount() / 720 + i) % otherNexuses.size()];
            auto minerals = nexus->getUnitsInRadius(320, IsMineralField);
            goal = minerals.empty() ? nexus->getPosition() : (nexus->getPosition() + minerals.getPosition()) / 2;
        }
        if (observers[i]->getDistance(goal) > 64) observers[i]->move(goal);
    }
}

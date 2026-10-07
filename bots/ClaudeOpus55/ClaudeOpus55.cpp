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
    const int ProbesPerBase = 21;      // ~2 per mineral patch plus 3 on gas
    const int MaxProbes = 60;
    const int FirstAttackArmy = 12;
    const int LaterAttackArmy = 16;
    const int RetreatBelow = 6;
    const double RetreatRatio = 1.25;  // retreat when the nearby enemy is this much stronger
    const int RegroupDistance = 450;

    bool isArmy(UnitType type)
    {
        return type == UnitTypes::Protoss_Zealot || type == UnitTypes::Protoss_Dragoon;
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
        if (type.canAttack() && !type.isWorker()) return 3;
        if (type.isWorker()) return 2;
        if (type == UnitTypes::Protoss_Pylon || type.isResourceDepot()) return 1;
        return 0;
    }

    Position towards(Position from, Position to, int distance)
    {
        int dx = to.x - from.x, dy = to.y - from.y;
        double length = std::sqrt(double(dx * dx + dy * dy));
        if (length < 1) return from;
        return Position(from.x + int(dx * distance / length), from.y + int(dy * distance / length)).makeValid();
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
    rally = findRampTop();
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
    if (unit == scout) scout = nullptr;
    if (unit == mainNexus) mainNexus = nullptr;
}

void ClaudeOpus55::onFrame()
{
    if (Broodwar->isPaused() || !Broodwar->self()) return;
    int frame = Broodwar->getFrameCount();

    if (getenv("CO55_DEBUG") && frame % 1000 == 0)
    {
        auto self = Broodwar->self();
        printf("CO55 %d: supply %d/%d min %d gas %d nexus %d probes %d gates %d zealots %d goons %d attacking %d wave %d\n",
               frame, self->supplyUsed() / 2, self->supplyTotal() / 2, self->minerals(), self->gas(),
               self->allUnitCount(UnitTypes::Protoss_Nexus), self->allUnitCount(UnitTypes::Protoss_Probe),
               self->allUnitCount(UnitTypes::Protoss_Gateway), self->allUnitCount(UnitTypes::Protoss_Zealot),
               self->allUnitCount(UnitTypes::Protoss_Dragoon), attacking, wave);
        printf("     natural %s expansionDue %d threats %zu bases %zu\n",
               natural ? "yes" : "no", expansionDue, threatsNearHome().size(), bases.size());
        for (auto &[b, p] : builders)
            printf("     pending %s at %d,%d since %d order %s builder at %d,%d\n", p.type.c_str(), p.tile.x, p.tile.y,
                   p.frame, b->getOrder().c_str(), b->getTilePosition().x, b->getTilePosition().y);
        auto spot = pylonSpot();
        printf("     pylonSpot %d,%d home %d,%d minerals %d,%d\n", spot.x, spot.y, TilePosition(home).x,
               TilePosition(home).y, TilePosition(mineralCenter).x, TilePosition(mineralCenter).y);
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
                it = builders.erase(it);
                continue;
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

    // Idle probes mine at the base that needs them most
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
        auto mineral = nexus->getClosestUnit(IsMineralField, 400);
        if (mineral) probe->gather(mineral);
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
        tile = findBuildSpot(type, near);
    }
    if (!tile.isValid()) return false;

    auto builder = chooseBuilder(Position(tile));
    if (!builder) return false;

    builders[builder] = PendingBuild{type, tile, Broodwar->getFrameCount()};
    if (!builder->build(type, tile)) builder->move(Position(tile) + Position(type.tileWidth() * 16, type.tileHeight() * 16));
    return true;
}

// The nearest explored, buildable spot to `near` that leaves a free tile around the building (so the base doesn't wall
// itself in) and stays out of the mineral line
TilePosition ClaudeOpus55::findBuildSpot(UnitType type, TilePosition near) const
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
                if (!tile.isValid() || !Broodwar->canBuildHere(tile, type, nullptr, true)) continue;
                Position center = Position(tile) + Position(w * 16, h * 16);
                if (center.getApproxDistance(mineralCenter) < homeToMinerals && center.getApproxDistance(home) < 400)
                {
                    continue;  // between the nexus and its minerals
                }
                bool clear = true;
                for (int x = tile.x - 1; x <= tile.x + w && clear; x++)
                {
                    for (int y = tile.y - 1; y <= tile.y + h && clear; y++)
                    {
                        if (x < 0 || y < 0 || x >= Broodwar->mapWidth() || y >= Broodwar->mapHeight()) continue;
                        bool border = x == tile.x - 1 || x == tile.x + w || y == tile.y - 1 || y == tile.y + h;
                        if (border && !Broodwar->getUnitsOnTile(x, y, IsBuilding || IsMineralField
                                                                || GetType == UnitTypes::Resource_Vespene_Geyser).empty())
                        {
                            clear = false;
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
    if (enemyRace == Races::Zerg)
    {
        // Two gateways early for the zergling rushes, then the core
        buildOrder = {{8, Protoss_Pylon}, {10, Protoss_Gateway}, {12, Protoss_Gateway}, {14, Protoss_Pylon},
                      {16, Protoss_Assimilator}, {17, Protoss_Cybernetics_Core}, {21, Protoss_Pylon},
                      {24, Protoss_Gateway}};
    }
    else if (enemyRace == Races::Terran)
    {
        // One gateway, core, then an early natural: Terran is slow to attack
        buildOrder = {{8, Protoss_Pylon}, {10, Protoss_Gateway}, {11, Protoss_Assimilator},
                      {13, Protoss_Cybernetics_Core}, {14, Protoss_Pylon}, {20, Protoss_Nexus}, {22, Protoss_Gateway},
                      {24, Protoss_Pylon}, {26, Protoss_Gateway}};
    }
    else
    {
        // Protoss (or unknown): two gateways with zealots early, then the core and dragoons
        buildOrder = {{8, Protoss_Pylon}, {10, Protoss_Gateway}, {12, Protoss_Gateway}, {14, Protoss_Pylon},
                      {16, Protoss_Assimilator}, {17, Protoss_Cybernetics_Core}, {21, Protoss_Pylon},
                      {24, Protoss_Gateway}};
    }
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

    TilePosition nearPylon = pylonSpot();
    for (auto pylon : self->getUnits())
    {
        if (pylon->getType() == UnitTypes::Protoss_Pylon && pylon->isCompleted() && pylon->getDistance(home) < 600)
        {
            nearPylon = pylon->getTilePosition();
            break;
        }
    }
    auto placeFor = [&](UnitType type) -> TilePosition
    {
        if (type == UnitTypes::Protoss_Pylon) return pylonSpot();
        if (type.isResourceDepot())
        {
            auto base = natural ? natural : nextBase();
            return base ? base->depot : TilePositions::Invalid;
        }
        if (type.isRefinery()) return TilePosition(home);
        return nearPylon;
    };

    // An early rush seen (an early pool, zerglings, mass gateways or a worker rush): a forge and cannons by the minerals,
    // alongside as many zealots as the gateways can make
    if (rushSeen && Broodwar->getFrameCount() < 9000 && self->completedUnitCount(UnitTypes::Protoss_Pylon) > 0)
    {
        auto spot = TilePosition((home + mineralCenter) / 2);
        if (count(UnitTypes::Protoss_Forge) < 1 && count(UnitTypes::Protoss_Gateway) >= 1)
        {
            build(UnitTypes::Protoss_Forge, nearPylon);
        }
        else if (self->completedUnitCount(UnitTypes::Protoss_Forge) > 0 && count(UnitTypes::Protoss_Photon_Cannon) < 2)
        {
            if (Broodwar->hasPower(spot, UnitTypes::Protoss_Photon_Cannon)) build(UnitTypes::Protoss_Photon_Cannon, spot);
            else if (pendingCount(UnitTypes::Protoss_Pylon) == 0) build(UnitTypes::Protoss_Pylon, spot);
        }
    }

    // The opening, step by step; a step waits for its requirements (and money), never skipped
    expansionDue = false;
    if (buildOrderStep < buildOrder.size())
    {
        auto &step = buildOrder[buildOrderStep];
        bool supplyBlocked = supplyTotal - supplyUsed <= 1 && supplyTotal < 200
                             && pendingCount(UnitTypes::Protoss_Pylon) + self->incompleteUnitCount(UnitTypes::Protoss_Pylon) == 0;
        if (supplyBlocked && step.type != UnitTypes::Protoss_Pylon)
        {
            build(UnitTypes::Protoss_Pylon, pylonSpot());
        }
        else if (supplyUsed >= step.supply && (step.type == UnitTypes::Protoss_Pylon || Broodwar->canMake(step.type))
                 && build(step.type, placeFor(step.type)))
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

    // Detection: cannons as soon as cloaked units (or the tech for them) are seen; a robotics facility for observers
    // only against Templar Archives
    if (cloakSeen)
    {
        if (count(UnitTypes::Protoss_Forge) < 1)
        {
            build(UnitTypes::Protoss_Forge, nearPylon);
        }
        else if (self->completedUnitCount(UnitTypes::Protoss_Forge) > 0 && count(UnitTypes::Protoss_Photon_Cannon) < 2)
        {
            auto spot = TilePosition((home + mineralCenter) / 2);
            bool powered = Broodwar->hasPower(spot, UnitTypes::Protoss_Photon_Cannon);
            if (!powered && pendingCount(UnitTypes::Protoss_Pylon) == 0) build(UnitTypes::Protoss_Pylon, spot);
            else if (powered) build(UnitTypes::Protoss_Photon_Cannon, spot);
        }
    }
    if (coreDone && templarArchivesSeen && count(UnitTypes::Protoss_Robotics_Facility) < 1)
    {
        build(UnitTypes::Protoss_Robotics_Facility, nearPylon);
    }
    else if (self->completedUnitCount(UnitTypes::Protoss_Robotics_Facility) > 0
             && count(UnitTypes::Protoss_Observatory) < 1)
    {
        build(UnitTypes::Protoss_Observatory, nearPylon);
    }

    // Spending: with 400+ minerals spare, take another base when it is safe and our army is holding its own (or we
    // have only one base); otherwise more gateways to strengthen the army
    auto base = nextBase();
    bool holding = army >= 6 && Broodwar->getFrameCount() - lastRetreatFrame > 480;
    bool wantBase = base && !underAttack && coreDone && pendingCount(UnitTypes::Protoss_Nexus) == 0
                    && !(rushSeen && Broodwar->getFrameCount() < 9000)
                    && (nexusCount < 2 ? army >= 6 : holding && self->allUnitCount(UnitTypes::Protoss_Probe) >= 18 * nexusCount);
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

void ClaudeOpus55::trainUnits()
{
    auto self = Broodwar->self();
    int probes = self->allUnitCount(UnitTypes::Protoss_Probe);
    int freeMinerals = self->minerals() - reservedMinerals();
    int freeGas = self->gas() - reservedGas();
    int probeTarget = std::min(MaxProbes, ProbesPerBase * (int) nexuses(false).size());

    // The next opening building comes first: once it is due (e.g. the pylon at 8/9 supply), keep its cost aside
    if (buildOrderStep < buildOrder.size())
    {
        auto &step = buildOrder[buildOrderStep];
        if (self->supplyUsed() / 2 >= step.supply)
        {
            freeMinerals -= step.type.mineralPrice();
            freeGas -= step.type.gasPrice();
        }
    }

    for (auto nexus : nexuses(true))
    {
        if (nexus->isIdle() && probes < probeTarget && freeMinerals >= 50 && self->supplyUsed() < self->supplyTotal())
        {
            nexus->train(UnitTypes::Protoss_Probe);
            freeMinerals -= 50;
            probes++;
        }
    }

    // Before the core, zealots from the two gateways (more when a rush is coming); one against Terran
    int zealotsBeforeCore = enemyRace == Races::Terran ? 1 : (rushSeen ? 6 : 4);
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
            && self->allUnitCount(UnitTypes::Protoss_Observer) < 2 && freeMinerals >= 25 && freeGas >= 75)
        {
            robo->train(UnitTypes::Protoss_Observer);
            freeMinerals -= 25;
            freeGas -= 75;
        }
    }

    // Leave money for the natural once it is due
    if (expansionDue && pendingCount(UnitTypes::Protoss_Nexus) == 0) freeMinerals -= 400;

    bool canMakeDragoons = self->completedUnitCount(UnitTypes::Protoss_Cybernetics_Core) > 0;
    for (auto gateway : self->getUnits())
    {
        if (gateway->getType() != UnitTypes::Protoss_Gateway || !gateway->isCompleted() || !gateway->isIdle()) continue;
        if (self->supplyUsed() + 4 > self->supplyTotal()) break;
        if (canMakeDragoons && freeMinerals >= 125 && freeGas >= 50)
        {
            gateway->train(UnitTypes::Protoss_Dragoon);
            freeMinerals -= 125;
            freeGas -= 50;
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
            if (unit->isCloaked() || unit->isBurrowed() || !unit->isDetected() || type == UnitTypes::Protoss_Dark_Templar
                || type == UnitTypes::Zerg_Lurker || type == UnitTypes::Protoss_Templar_Archives
                || type == UnitTypes::Terran_Vulture_Spider_Mine)
            {
                cloakSeen = true;
            }
            if (type == UnitTypes::Protoss_Templar_Archives) templarArchivesSeen = true;
            if (Broodwar->getFrameCount() < 5000 && !rushSeen)
            {
                auto counts = [&](UnitType t) { return Broodwar->enemy()->visibleUnitCount(t); };
                int enemyWorkersNearHome = 0;
                for (auto worker : Broodwar->enemy()->getUnits())
                {
                    if (worker->getType().isWorker() && worker->getDistance(home) < 500) enemyWorkersNearHome++;
                }
                if (enemyWorkersNearHome >= 3
                    || counts(UnitTypes::Protoss_Gateway) >= 2 && Broodwar->getFrameCount() < 3600
                    || counts(UnitTypes::Terran_Barracks) >= 2 || counts(UnitTypes::Zerg_Zergling) >= 4
                    || counts(UnitTypes::Protoss_Zealot) >= 3
                    || type == UnitTypes::Zerg_Spawning_Pool && Broodwar->getFrameCount() < 2400)
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
    if (enemyBase != Positions::Unknown)
    {
        if (scout && scout->exists())
        {
            scout->stop();
            scout = nullptr;
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
            (unit->getType().isWorker() ? workers : threats).insert(unit);
        }
    }
    if (workers.size() >= 3) threats.insert(workers.begin(), workers.end());
    return threats;
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

    // Dragoons step back from melee units while their weapon reloads
    if (unit->getType() == UnitTypes::Protoss_Dragoon && unit->getGroundWeaponCooldown() > 0)
    {
        auto melee = unit->getClosestUnit(IsEnemy && IsVisible && !IsFlying && !IsWorker
                                          && [](Unit u) { return isMelee(u->getType()); }, 96);
        if (melee)
        {
            unit->move(towards(unit->getPosition(), unit->getPosition() * 2 - melee->getPosition(), 64));
            return;
        }
    }

    Unit best = nullptr;
    int bestPriority = -1, bestHealth = INT_MAX;
    int range = std::max(unit->getType().groundWeapon().maxRange(), 32) + 64;
    for (auto enemy : unit->getUnitsInRadius(range + 96, IsEnemy && IsVisible && !IsFlying))
    {
        if (!enemy->isDetected()) continue;
        int priority = targetPriority(enemy);
        int health = enemy->getHitPoints() + enemy->getShields();
        if (priority > bestPriority || priority == bestPriority && health < bestHealth)
        {
            best = enemy;
            bestPriority = priority;
            bestHealth = health;
        }
    }
    if (best)
    {
        if (unit->getOrderTarget() != best) unit->attack(best);
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

    controlObservers(army);

    // Defend first: everything goes for enemies near home, with probes helping against an early rush
    auto threats = threatsNearHome();
    if (!threats.empty())
    {
        if (threats.size() >= 3 && attacking)
        {
            attacking = false;
            wave++;
            lastRetreatFrame = frame;
        }
        Position target = threats.getPosition();
        for (auto unit : army)
        {
            if (!attacking || unit->getDistance(home) < 1200) fight(unit, target);
        }

        // Probes only help against enemies right at the nexus, when the army can't cope alone
        int atNexus = 0;
        for (auto threat : threats)
        {
            if (threat->getDistance(home) < 300) atNexus++;
        }
        int toPull = std::min(16, atNexus * 3 / 2 + 1 - (int) army.size() * 2);
        for (auto probe : Broodwar->self()->getUnits())
        {
            if (toPull <= 0) break;
            if (!probe->getType().isWorker() || probe == scout || builders.count(probe)) continue;
            if (probe->getOrder() == Orders::AttackUnit) continue;
            auto enemy = probe->getClosestUnit(IsEnemy && !IsFlying && IsVisible, 200);
            if (enemy && enemy->getDistance(home) < 300)
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
        if (!target || !target->exists() || target->getDistance(home) >= 300)
        {
            auto mineral = mineNexus->getClosestUnit(IsMineralField, 400);
            if (mineral) probe->gather(mineral);
        }
    }
    if (!threats.empty() && !attacking) return;

    // Attack once the army is big enough and clearly stronger than the enemy army we know about (or nearly maxed)
    int needed = wave == 0 ? FirstAttackArmy : LaterAttackArmy;
    bool maxed = Broodwar->self()->supplyUsed() / 2 >= 150;
    double myDurability = 0, myDps = 0, theirDurability = 0, theirDps = 0;
    for (auto unit : army) addToGroup(unit->getType(), unit->getHitPoints() + unit->getShields(), myDurability, myDps);
    for (auto &[id, seen] : enemyArmy) addToGroup(seen.type, seen.health, theirDurability, theirDps);
    bool stronger = groupStrength(myDurability, myDps) >= 1.5 * groupStrength(theirDurability, theirDps);
    if (!attacking && ((int) army.size() >= needed && stronger || maxed) && frame - lastRetreatFrame > 480)
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
            if (!contact && (group.size() < army.size() * 3 / 4 || spread > 250) && center.getApproxDistance(target) > 600)
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

            for (auto unit : army)
            {
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

// The first observer stays with the army (or over the base while it is home); the second watches the mineral line
void ClaudeOpus55::controlObservers(const Unitset &army)
{
    std::vector<Unit> observers;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType() == UnitTypes::Protoss_Observer && unit->isCompleted()) observers.push_back(unit);
    }
    for (size_t i = 0; i < observers.size(); i++)
    {
        Position goal = (home + mineralCenter) / 2;
        if (i == 0 && !army.empty())
        {
            // Over the army, a little behind its front
            goal = army.getPosition();
            if (attacking) goal = towards(goal, home, 64);
        }
        if (observers[i]->getDistance(goal) > 64) observers[i]->move(goal);
    }
}

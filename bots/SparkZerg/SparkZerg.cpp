#include "SparkZerg.h"

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
    const int DronesPerMineralPatch = 2;  // ~2 per patch plus 3 on each gas
    const int MaxDrones = 66;
    const int FirstAttackSupply = 20;     // army supply before the first push
    const double AttackRatio = 1.3;       // attack when this much stronger than the seen enemy
    const double RetreatRatio = 1.25;     // retreat when the nearby enemy is this much stronger
    const int LeashDistance = 700;        // defenders don't chase past this from the base
    const int RegroupDistance = 500;
    const int MutaPackSize = 9;           // mutalisks for the harass option
    const int MaxMutas = 12;
    const int MaxScourge = 8;
    const int MaxDefilers = 3;
    const int DroneDefensePull = 6;       // drones pulled to fight an attack on a base

    bool isArmy(UnitType type)
    {
        return type == UnitTypes::Zerg_Zergling || type == UnitTypes::Zerg_Hydralisk
               || type == UnitTypes::Zerg_Lurker || type == UnitTypes::Zerg_Mutalisk
               || type == UnitTypes::Zerg_Scourge || type == UnitTypes::Zerg_Ultralisk
               || type == UnitTypes::Zerg_Defiler || type == UnitTypes::Zerg_Guardian
               || type == UnitTypes::Zerg_Devourer;
    }

    bool isMelee(UnitType type)
    {
        auto weapon = type.groundWeapon();
        return weapon != WeaponTypes::None && weapon.maxRange() <= 32 && !type.isWorker();
    }

    // Enemy units worth shooting first: the ones that punish clumps or outrange us, then anything that shoots
    int targetPriority(Unit target)
    {
        auto type = target->getType();
        if (type == UnitTypes::Zerg_Lurker || type == UnitTypes::Terran_Siege_Tank_Siege_Mode
            || type == UnitTypes::Terran_Siege_Tank_Tank_Mode || type == UnitTypes::Protoss_Reaver
            || type == UnitTypes::Terran_Bunker || type == UnitTypes::Protoss_Photon_Cannon
            || type == UnitTypes::Terran_Missile_Turret || type == UnitTypes::Zerg_Sunken_Colony
            || type == UnitTypes::Zerg_Spore_Colony) return 4;
        if (type == UnitTypes::Protoss_Shuttle || type == UnitTypes::Terran_Dropship
            || type == UnitTypes::Zerg_Overlord) return 4;  // transports and the Zerg's flying supply
        if (type.canAttack() && !type.isWorker()) return 3;
        if (type.isWorker()) return 2;
        if (type == UnitTypes::Protoss_Pylon || type.isResourceDepot()) return 1;
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
            for (int wy = x * 4 + 1; wy <= y * 4 + 2; wy++)
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

    bool isHatcheryType(UnitType type)
    {
        return type == UnitTypes::Zerg_Hatchery || type == UnitTypes::Zerg_Lair || type == UnitTypes::Zerg_Hive;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Setup

void SparkZerg::onStart()
{
    Broodwar->enableFlag(Flag::UserInput);
    enemyRace = Broodwar->enemy() ? Broodwar->enemy()->getRace() : Races::Unknown;

    home = Position(Broodwar->self()->getStartLocation()) + Position(64, 48);
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType().isResourceDepot())
        {
            mainHatchery = unit;
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
    if (natural)
    {
        // The natural's front: past the hatchery toward the map center, where attacks come from
        Position depot = Position(natural->depot) + Position(64, 48);
        Position mapCenter(Broodwar->mapWidth() * 16, Broodwar->mapHeight() * 16);
        naturalFront = towards(depot, mapCenter, 224);
        rally = naturalFront;
    }
    analyseMap();
    chooseBuildOrder();
}

// How far the enemy is by ground: a short rush distance means a safer, earlier pool
void SparkZerg::analyseMap()
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
        if (!start.isValid()) continue;
        int distance = fromHome[start.y * width + start.x];
        if (distance >= 0) rushDistance = std::min(rushDistance, distance);
    }
}

// Resource clusters: minerals grouped by proximity, nearest geyser attached, nearest cluster to home first
void SparkZerg::findBases()
{
    struct Cluster
    {
        std::vector<Unit> minerals;
    };
    std::vector<Cluster> clusters;
    for (auto mineral : Broodwar->getStaticMinerals())
    {
        bool placed = false;
        for (auto &cluster : clusters)
        {
            for (auto other : cluster.minerals)
            {
                if (mineral->getInitialPosition().getApproxDistance(other->getInitialPosition()) < 320)
                {
                    cluster.minerals.push_back(mineral);
                    placed = true;
                    break;
                }
            }
            if (placed) break;
        }
        if (!placed) clusters.push_back({{mineral}});
    }
    for (auto &cluster : clusters)
    {
        Base base;
        Position sum(0, 0);
        for (auto mineral : cluster.minerals) sum += mineral->getInitialPosition();
        base.center = Position(sum.x / (int) cluster.minerals.size(), sum.y / (int) cluster.minerals.size());
        int best = INT_MAX;
        for (auto geyser : Broodwar->getStaticGeysers())
        {
            int distance = geyser->getInitialPosition().getApproxDistance(base.center);
            if (distance < best)
            {
                best = distance;
                base.geyser = geyser;
            }
        }
        // Hatchery spot: a few tiles toward the map center from the minerals, on buildable ground
        Position center(Broodwar->mapWidth() * 16, Broodwar->mapHeight() * 16);
        Position want = towards(base.center, center, 96);
        TilePosition spot = Broodwar->getBuildLocation(UnitTypes::Zerg_Hatchery, TilePosition(want), 32);
        base.depot = spot.isValid() ? spot : TilePosition(want);
        bases.push_back(base);
    }
    std::sort(bases.begin(), bases.end(), [this](const Base &a, const Base &b)
    {
        return a.center.getApproxDistance(home) < b.center.getApproxDistance(home);
    });
    if (bases.size() > 1) natural = &bases[1];
}

void SparkZerg::onUnitDestroy(Unit unit)
{
    enemyBuildings.erase(unit->getID());
    enemyArmy.erase(unit->getID());
    aimedDamage.erase(unit);
    builders.erase(unit);
    if (unit == scoutOverlord) scoutOverlord = nullptr;
    if (unit == mainHatchery)
    {
        mainHatchery = nullptr;
        for (auto hatch : hatcheries(true))
        {
            mainHatchery = hatch;
            break;
        }
        if (mainHatchery) home = mainHatchery->getPosition();
    }
}

void SparkZerg::onFrame()
{
    if (Broodwar->isReplay()) return;
    trackEnemy();
    scoutEnemy();
    manageOverlords();
    buildStructures();
    trainUnits();
    manageTech();
    manageDrones();
    defendMineralLines();
    controlArmy();

    // Forget stale builder records: the drone is gone (consumed) or the building is up
    for (auto it = builders.begin(); it != builders.end();)
    {
        if (!it->first->exists() || Broodwar->getFrameCount() - it->second.frame > 2400) it = builders.erase(it);
        else ++it;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Counting and building

std::vector<Unit> SparkZerg::hatcheries(bool completedOnly) const
{
    std::vector<Unit> result;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (isHatcheryType(unit->getType()) && (!completedOnly || unit->isCompleted())) result.push_back(unit);
    }
    return result;
}

int SparkZerg::pendingCount(UnitType type) const
{
    int n = 0;
    for (auto unit : Broodwar->self()->getUnits())
    {
        // A drone on its way to morph counts too: the building doesn't exist yet, only the builders record
        if (unit->getType() == type && !unit->isCompleted()) n++;
    }
    for (auto &entry : builders)
    {
        if (entry.second.type == type) n++;
    }
    return n;
}

int SparkZerg::count(UnitType type, bool includePending) const
{
    int n = 0;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType() == type && (includePending || unit->isCompleted())) n++;
    }
    return n;
}

int SparkZerg::larvaeAvailable() const
{
    int n = 0;
    for (auto hatch : hatcheries(false))
    {
        if (hatch->isCompleted() && !hatch->isMorphing()) n += (int) hatch->getLarva().size();
    }
    return n;
}

int SparkZerg::reservedMinerals() const
{
    int n = 0;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getPlayer() == Broodwar->self() && !unit->isCompleted() && unit->getType().isBuilding())
        {
            n += unit->getType().mineralPrice();
        }
    }
    return n;
}

int SparkZerg::reservedGas() const
{
    int n = 0;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getPlayer() == Broodwar->self() && !unit->isCompleted() && unit->getType().isBuilding())
        {
            n += unit->getType().gasPrice();
        }
    }
    return n;
}

// A drone free to build: the closest one to `near` that isn't already morphing into a building
Unit SparkZerg::chooseBuilder(Position near)
{
    Unit best = nullptr;
    int bestDistance = INT_MAX;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType() != UnitTypes::Zerg_Drone || !unit->isCompleted() || builders.count(unit)) continue;
        if (unit->isMorphing() || unit->isConstructing()) continue;
        int distance = unit->getDistance(near);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = unit;
        }
    }
    return best;
}

bool SparkZerg::placeableIgnoringUnits(TilePosition tile, UnitType type, bool checkExplored) const
{
    if (!tile.isValid()) return false;
    int width = type.tileWidth(), height = type.tileHeight();
    for (int x = tile.x; x < tile.x + width; x++)
    {
        for (int y = tile.y; y < tile.y + height; y++)
        {
            TilePosition t(x, y);
            if (!t.isValid() || !Broodwar->isBuildable(t)) return false;
            if (checkExplored && !Broodwar->isExplored(t)) return false;
            if (type.requiresCreep() && !Broodwar->hasCreep(t)) return false;
        }
    }
    return true;
}

// Spiral search for a build spot near `near`: creep and buildability checked, units ignored (the builder pushes
// through), explored tiles required first, then anywhere
TilePosition SparkZerg::findBuildSpot(UnitType type, TilePosition near, bool checkExplored) const
{
    if (placeableIgnoringUnits(near, type, checkExplored)
        && Broodwar->canBuildHere(near, type, nullptr, checkExplored)) return near;
    for (int radius = 1; radius <= 20; radius++)
    {
        for (int dx = -radius; dx <= radius; dx++)
        {
            for (int dy = -radius; dy <= radius; dy++)
            {
                if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                TilePosition tile(near.x + dx, near.y + dy);
                if (!tile.isValid()) continue;
                if (!placeableIgnoringUnits(tile, type, checkExplored)) continue;
                if (!Broodwar->canBuildHere(tile, type, nullptr, checkExplored)) continue;
                return tile;
            }
        }
    }
    return TilePositions::Invalid;
}

// The geyser at one of our bases that has no extractor yet (or being built), nearest to `home` first
Unit SparkZerg::geyserNeedingExtractor() const
{
    for (auto &base : bases)
    {
        if (!base.geyser || !base.geyser->exists()) continue;
        bool ours = false;
        for (auto hatch : hatcheries(false))
        {
            if (hatch->getDistance(base.center) < 500)
            {
                ours = true;
                break;
            }
        }
        if (!ours) continue;
        bool has = false;
        for (auto unit : Broodwar->self()->getUnits())
        {
            if (unit->getType() == UnitTypes::Zerg_Extractor
                && unit->getDistance(base.geyser) < 100)
            {
                has = true;
                break;
            }
        }
        if (!has) return base.geyser;
    }
    return nullptr;
}

bool SparkZerg::build(UnitType type, TilePosition near, bool atNatural)
{
    if (Broodwar->self()->minerals() < type.mineralPrice() || Broodwar->self()->gas() < type.gasPrice()) return false;
    // One pending order of this type at a time: the drone takes a few frames to become a building
    for (auto &entry : builders)
    {
        if (entry.second.type == type && Broodwar->getFrameCount() - entry.second.frame < 120) return false;
    }
    TilePosition spot = near;
    if (type == UnitTypes::Zerg_Extractor)
    {
        Unit geyser = geyserNeedingExtractor();
        if (!geyser) return false;
        spot = geyser->getTilePosition();
    }
    else
    {
        Position basePos = (atNatural && natural) ? Position(natural->depot) + Position(64, 48) : home;
        spot = findBuildSpot(type, TilePosition(basePos), true);
        if (!spot.isValid()) spot = findBuildSpot(type, TilePosition(basePos), false);
        if (!spot.isValid()) return false;
    }
    Unit builder = chooseBuilder(Position(spot) + Position(32, 24));
    if (!builder) return false;
    if (!builder->build(type, spot)) return false;
    builders[builder] = {type, spot, Broodwar->getFrameCount()};
    return true;
}

bool SparkZerg::buildExtractor(BWAPI::Unit geyserUnit)
{
    if (!geyserUnit || !geyserUnit->exists()) return false;
    if (Broodwar->self()->minerals() < UnitTypes::Zerg_Extractor.mineralPrice()) return false;
    Unit builder = chooseBuilder(geyserUnit->getPosition());
    if (!builder) return false;
    if (!builder->build(UnitTypes::Zerg_Extractor, geyserUnit->getTilePosition())) return false;
    builders[builder] = {UnitTypes::Zerg_Extractor, geyserUnit->getTilePosition(), Broodwar->getFrameCount()};
    return true;
}

// The opening: supply-based steps per matchup. After the steps, buildStructures' mid-game logic takes over.
void SparkZerg::chooseBuildOrder()
{
    buildOrder.clear();
    buildOrderStep = 0;
    bool shortRush = rushDistance < 90;  // very close spawns: pool before hatch whatever the race
    if (enemyRace == Races::Zerg || shortRush)
    {
        // Overpool: pool before any greed, lings the moment it finishes
        buildOrder.push_back({9, UnitTypes::Zerg_Overlord});
        buildOrder.push_back({10, UnitTypes::Zerg_Spawning_Pool});
        buildOrder.push_back({15, UnitTypes::Zerg_Extractor});
        if (!earlyPoolSeen) buildOrder.push_back({17, UnitTypes::Zerg_Hatchery, true});
    }
    else if (enemyRace == Races::Protoss)
    {
        // 12 hatch: greedy, but the pool follows at once so lings answer a 2-gate
        buildOrder.push_back({9, UnitTypes::Zerg_Overlord});
        buildOrder.push_back({12, UnitTypes::Zerg_Hatchery, true});
        buildOrder.push_back({11, UnitTypes::Zerg_Spawning_Pool});
        buildOrder.push_back({13, UnitTypes::Zerg_Extractor});
    }
    else  // Terran or unknown
    {
        buildOrder.push_back({9, UnitTypes::Zerg_Overlord});
        buildOrder.push_back({12, UnitTypes::Zerg_Hatchery, true});
        buildOrder.push_back({11, UnitTypes::Zerg_Spawning_Pool});
        buildOrder.push_back({13, UnitTypes::Zerg_Extractor});
    }
}

// Holding an early rush: lings and sunkens before drones, gas or the natural; the army waits at home
bool SparkZerg::rushDefense() const
{
    int frame = Broodwar->getFrameCount();
    if (frame > 12000) return false;  // after ~8:30 it is mid-game defence, not the opening
    return earlyPoolSeen || gatewayRushSeen || barracksRushSeen || bunkerRushSeen || workerRushSeen || proxySeen;
}

const SparkZerg::Base *SparkZerg::nextBase() const
{
    for (auto &base : bases)
    {
        bool taken = false;
        for (auto hatch : hatcheries(false))
        {
            if (hatch->getDistance(base.center) < 500)
            {
                taken = true;
                break;
            }
        }
        if (!taken) return &base;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Economy

// Drones mine; gas gets 3 each; idle drones find work. Defence pulls happen later in the frame and override this.
void SparkZerg::manageDrones()
{
    auto self = Broodwar->self();
    int frame = Broodwar->getFrameCount();

    // Top up gas every half second: 3 drones per completed extractor
    if (frame % 12 == 0)
    {
        for (auto extractor : self->getUnits())
        {
            if (extractor->getType() != UnitTypes::Zerg_Extractor || !extractor->isCompleted()) continue;
            int onGas = 0;
            for (auto drone : self->getUnits())
            {
                if (drone->getType() != UnitTypes::Zerg_Drone || !drone->isCompleted()) continue;
                if (drone->isGatheringGas() && drone->getOrderTarget() == extractor) onGas++;
            }
            while (onGas < 3)
            {
                Unit best = nullptr;
                int bestDistance = INT_MAX;
                for (auto drone : self->getUnits())
                {
                    if (drone->getType() != UnitTypes::Zerg_Drone || !drone->isCompleted()) continue;
                    if (builders.count(drone) || drone->isMorphing()) continue;
                    if (!drone->isGatheringMinerals()) continue;
                    int distance = drone->getDistance(extractor);
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        best = drone;
                    }
                }
                if (!best) break;
                best->gather(extractor);
                onGas++;
            }
        }
    }

    for (auto drone : self->getUnits())
    {
        if (drone->getType() != UnitTypes::Zerg_Drone || !drone->isCompleted()) continue;
        if (builders.count(drone) || drone->isMorphing() || drone->isConstructing()) continue;
        if (drone->isGatheringMinerals() || drone->isGatheringGas()) continue;
        if (drone->isCarryingMinerals() || drone->isCarryingGas())
        {
            drone->returnCargo();
            continue;
        }
        // A drone with an explicit move order is scouting or fleeing: leave it alone
        if (drone->getOrder() == Orders::Move) continue;
        auto mineral = drone->getClosestUnit(IsMineralField, 960);
        if (mineral) drone->gather(mineral);
    }
}

// How many drones the economy wants: ~2 per mineral patch and 3 per gas at our bases, capped
int SparkZerg::wantedDrones() const
{
    int wanted = 0;
    for (auto &base : bases)
    {
        bool ours = false;
        for (auto hatch : hatcheries(false))
        {
            if (hatch->getDistance(base.center) < 500)
            {
                ours = true;
                break;
            }
        }
        if (!ours) continue;
        int patches = 0;
        for (auto mineral : Broodwar->getStaticMinerals())
        {
            if (mineral->getInitialPosition().getApproxDistance(base.center) < 400) patches++;
        }
        int extractors = 0;
        for (auto unit : Broodwar->self()->getUnits())
        {
            if (unit->getType() == UnitTypes::Zerg_Extractor && unit->getDistance(base.center) < 400) extractors++;
        }
        wanted += patches * DronesPerMineralPatch + extractors * 3;
    }
    wanted = std::min(wanted, MaxDrones);
    if (rushDefense()) wanted = std::min(wanted, 12);  // lings first while the rush is on
    return wanted;
}

// Overlords: the first scouts the enemy, the rest spread out as watchers and detectors; all flee anti-air
void SparkZerg::manageOverlords()
{
    auto self = Broodwar->self();
    std::vector<Unit> overlords;
    for (auto unit : self->getUnits())
    {
        if (unit->getType() == UnitTypes::Zerg_Overlord && unit->isCompleted()) overlords.push_back(unit);
    }
    if (overlords.empty()) return;

    // Scout: nearest enemy start first, then the rest in order
    if (!scoutingDone)
    {
        if (!scoutOverlord || !scoutOverlord->exists())
        {
            scoutOverlord = overlords[0];
            scoutTargets.erase(std::remove_if(scoutTargets.begin(), scoutTargets.end(),
                                              [this](TilePosition t)
                                              { return Position(t).getApproxDistance(home) < 64; }),
                               scoutTargets.end());
        }
        if (scoutOverlord && !scoutTargets.empty())
        {
            Position target = Position(scoutTargets[0]) + Position(64, 48);
            if (scoutOverlord->getDistance(target) < 400)
            {
                scoutTargets.erase(scoutTargets.begin());
                if (scoutTargets.empty()) scoutingDone = true;
            }
            else if (scoutOverlord->getOrder() != Orders::Move
                     || scoutOverlord->getTargetPosition().getApproxDistance(target) > 128)
            {
                scoutOverlord->move(target);
            }
            if (enemyBase == Positions::Unknown && scoutOverlord->getDistance(target) < 800)
            {
                // Enemy buildings seen here: this is their base
                for (auto enemy : Broodwar->enemy()->getUnits())
                {
                    if (enemy->getType().isBuilding() && enemy->getDistance(target) < 900)
                    {
                        enemyBase = enemy->getPosition();
                        break;
                    }
                }
            }
        }
        else
        {
            scoutingDone = true;
        }
    }

    // Watchers: spread idle overlords to map corners and the enemy's doorstep, away from static defence
    std::vector<Position> watchSpots;
    int mapW = Broodwar->mapWidth() * 32, mapH = Broodwar->mapHeight() * 32;
    watchSpots.push_back(Position(200, 200));
    watchSpots.push_back(Position(mapW - 200, 200));
    watchSpots.push_back(Position(200, mapH - 200));
    watchSpots.push_back(Position(mapW - 200, mapH - 200));
    if (enemyBase != Positions::Unknown && enemyBase != Positions::Invalid)
    {
        watchSpots.push_back(towards(enemyBase, home, 700));  // outside their base, watching for move-outs
    }
    size_t spot = 0;
    for (auto overlord : overlords)
    {
        if (overlord == scoutOverlord && !scoutingDone) continue;
        // Flee anything that shoots up
        auto threat = overlord->getClosestUnit(IsEnemy && IsVisible && [](Unit u) {
            auto air = u->getType().airWeapon();
            return air != WeaponTypes::None && !u->getType().isWorker();
        }, 400);
        if (threat)
        {
            overlord->move(towards(overlord->getPosition(), overlord->getPosition() * 2 - threat->getPosition(), 224));
            continue;
        }
        if (overlord->isUnderAttack())
        {
            overlord->move(towards(overlord->getPosition(), home, 320));
            continue;
        }
        if (spot < watchSpots.size() && (overlord->getOrder() != Orders::Move
                                         || overlord->getDistance(overlord->getTargetPosition()) < 96))
        {
            if (overlord->getDistance(watchSpots[spot]) > 160) overlord->move(watchSpots[spot]);
            spot++;
        }
    }
}

// Spend larvae: overlords before anything when supply is tight, drones to the economy target, then the army mix
void SparkZerg::trainUnits()
{
    auto self = Broodwar->self();
    std::vector<Unit> nests;
    for (auto hatch : hatcheries(false))
    {
        if (hatch->isCompleted() && !hatch->isMorphing() && !hatch->getLarva().empty()) nests.push_back(hatch);
    }
    if (nests.empty()) return;

    auto tryTrain = [&](UnitType type) -> bool
    {
        if (self->minerals() < type.mineralPrice() || self->gas() < type.gasPrice()) return false;
        int effSupply = self->supplyTotal() + 8 * pendingCount(UnitTypes::Zerg_Overlord);
        if (self->supplyUsed() + type.supplyRequired() > effSupply) return false;
        for (auto hatch : nests)
        {
            if (hatch->getLarva().empty() || !hatch->canTrain(type)) continue;
            if (hatch->train(type)) return true;
        }
        return false;
    };

    auto countCompleted = [&](UnitType type)
    {
        int n = 0;
        for (auto unit : self->getUnits())
        {
            if (unit->getType() == type && unit->isCompleted()) n++;
        }
        return n;
    };

    // 1. Supply: never block. An overlord when within a couple of supply of the cap.
    {
        int effSupply = self->supplyTotal() + 8 * pendingCount(UnitTypes::Zerg_Overlord);
        if (self->supplyUsed() >= effSupply - 4)
        {
            if (tryTrain(UnitTypes::Zerg_Overlord)) return;
        }
    }

    bool poolDone = count(UnitTypes::Zerg_Spawning_Pool, false) > 0;
    bool denDone = count(UnitTypes::Zerg_Hydralisk_Den, false) > 0;
    bool spireDone = count(UnitTypes::Zerg_Spire, false) > 0;
    bool cavernDone = count(UnitTypes::Zerg_Ultralisk_Cavern, false) > 0;
    bool moundDone = count(UnitTypes::Zerg_Defiler_Mound, false) > 0;

    int drones = countCompleted(UnitTypes::Zerg_Drone);
    int lings = countCompleted(UnitTypes::Zerg_Zergling);
    int hydras = countCompleted(UnitTypes::Zerg_Hydralisk);
    int mutas = countCompleted(UnitTypes::Zerg_Mutalisk);
    int scourge = countCompleted(UnitTypes::Zerg_Scourge);
    int ultras = countCompleted(UnitTypes::Zerg_Ultralisk);
    int defilers = countCompleted(UnitTypes::Zerg_Defiler);

    // 2. Rush defence: zerglings until the rush breaks or we have a pack
    if (rushDefense() && poolDone)
    {
        if (lings < 14 && tryTrain(UnitTypes::Zerg_Zergling)) return;
    }

    // 3. Drones to the economy target
    if (drones < wantedDrones())
    {
        if (tryTrain(UnitTypes::Zerg_Drone)) return;
    }

    // 4. Scourge answer to capital ships
    if (airArmySeen && scourge < MaxScourge && spireDone)
    {
        bool capitals = false;
        for (auto &entry : enemyArmy)
        {
            auto t = entry.second.type;
            if (t == UnitTypes::Protoss_Carrier || t == UnitTypes::Terran_Battlecruiser
                || t == UnitTypes::Protoss_Arbiter || t == UnitTypes::Zerg_Guardian)
            {
                capitals = true;
                break;
            }
        }
        if (capitals && tryTrain(UnitTypes::Zerg_Scourge)) return;
    }

    // 5. Hive muscle: a few ultralisks and defilers when the buildings are up
    if (hiveTech)
    {
        if (cavernDone && ultras < 6 && tryTrain(UnitTypes::Zerg_Ultralisk)) return;
        if (moundDone && defilers < MaxDefilers && tryTrain(UnitTypes::Zerg_Defiler)) return;
    }

    // 6. The mutalisk harass pack, once decided
    if (goMutalisks && spireDone && mutas < MutaPackSize)
    {
        if (tryTrain(UnitTypes::Zerg_Mutalisk)) return;
    }

    // 7. The standing army: hydralisks when the den is up, zerglings otherwise
    if (denDone)
    {
        // Keep a ling screen in ZvZ and with ultras; otherwise mass hydras
        bool wantLings = enemyRace == Races::Zerg || (hiveTech && cavernDone);
        if (wantLings && lings < hydras / 2 + 6)
        {
            if (poolDone && tryTrain(UnitTypes::Zerg_Zergling)) return;
        }
        if (tryTrain(UnitTypes::Zerg_Hydralisk)) return;
    }
    if (poolDone && tryTrain(UnitTypes::Zerg_Zergling)) return;

    // 8. Any larva left over becomes a drone (up to a few extra) or a ling
    if (drones < MaxDrones + 6 && tryTrain(UnitTypes::Zerg_Drone)) return;
    if (poolDone) tryTrain(UnitTypes::Zerg_Zergling);
}

// ---------------------------------------------------------------------------------------------------------------------
// Buildings

// A sunken is wanted at a base while holding a rush, or one at each base against Terran mid-game
bool SparkZerg::needSunken(Position at) const
{
    int frame = Broodwar->getFrameCount();
    int wanted = 0;
    if (rushDefense()) wanted = 2;
    else if (frame > 10000 && enemyRace == Races::Terran) wanted = 1;
    else if (frame > 12000 && (gatewayRushSeen || earlyPoolSeen)) wanted = 1;
    if (!wanted) return false;
    int have = 0;
    for (auto unit : Broodwar->self()->getUnits())
    {
        auto t = unit->getType();
        if ((t == UnitTypes::Zerg_Sunken_Colony || t == UnitTypes::Zerg_Creep_Colony) && unit->getDistance(at) < 700) have++;
    }
    return have < wanted;
}

// Spores answer air tech, air armies and cloaked raiders
bool SparkZerg::needSpore(Position at) const
{
    int wanted = 0;
    if (airArmySeen || cloakSeen) wanted = 2;
    else if (airTechSeen) wanted = 1;
    if (!wanted) return false;
    int have = 0;
    for (auto unit : Broodwar->self()->getUnits())
    {
        auto t = unit->getType();
        if ((t == UnitTypes::Zerg_Spore_Colony || t == UnitTypes::Zerg_Creep_Colony) && unit->getDistance(at) < 700) have++;
    }
    return have < wanted;
}

void SparkZerg::buildStructures()
{
    auto self = Broodwar->self();
    int frame = Broodwar->getFrameCount();
    int supply = self->supplyUsed();

    // 1. The opening steps, in order
    if (buildOrderStep < buildOrder.size())
    {
        const Step &step = buildOrder[buildOrderStep];
        if (frame > 9000)
        {
            buildOrderStep = buildOrder.size();  // opening window passed: mid-game logic takes over
        }
        else if (supply >= step.supply)
        {
            if (step.atNatural && rushDefense())
            {
                // The natural waits while a rush is being held; keep the step
            }
            else if (step.type == UnitTypes::Zerg_Extractor)
            {
                if (buildExtractor(geyserNeedingExtractor())) buildOrderStep++;
            }
            else if (step.type == UnitTypes::Zerg_Hatchery && step.atNatural && natural)
            {
                if (build(UnitTypes::Zerg_Hatchery, natural->depot, true)) buildOrderStep++;
            }
            else
            {
                if (build(step.type, TilePosition(home))) buildOrderStep++;
            }
        }
        return;  // one thing at a time during the opening
    }

    // 2. Morph creep colonies into sunkens/spores where defence is wanted
    for (auto unit : self->getUnits())
    {
        if (unit->getType() != UnitTypes::Zerg_Creep_Colony || !unit->isCompleted() || unit->isMorphing()) continue;
        Position at = unit->getPosition();
        if (needSunken(at))
        {
            unit->morph(UnitTypes::Zerg_Sunken_Colony);
        }
        else if (needSpore(at))
        {
            unit->morph(UnitTypes::Zerg_Spore_Colony);
        }
    }

    // 3. Static defence at our bases
    for (auto hatch : hatcheries(true))
    {
        Position at = hatch->getPosition();
        if (needSunken(at) && count(UnitTypes::Zerg_Spawning_Pool, false) > 0)
        {
            build(UnitTypes::Zerg_Creep_Colony, hatch->getTilePosition());
            return;
        }
        if (needSpore(at))
        {
            build(UnitTypes::Zerg_Creep_Colony, hatch->getTilePosition());
            return;
        }
    }

    int hatchCount = (int) hatcheries(false).size();
    int completedHatch = (int) hatcheries(true).size();
    bool poolDone = count(UnitTypes::Zerg_Spawning_Pool, false) > 0;
    bool lairDone = count(UnitTypes::Zerg_Lair, false) + count(UnitTypes::Zerg_Hive, false) > 0;
    bool denDone = count(UnitTypes::Zerg_Hydralisk_Den, false) > 0;

    // 4. Tech, in dependency order
    if (poolDone && !lairDone && pendingCount(UnitTypes::Zerg_Lair) == 0
        && (completedHatch >= 2 || frame > 7000) && !rushDefense())
    {
        for (auto hatch : hatcheries(true))
        {
            if (hatch->getType() == UnitTypes::Zerg_Hatchery && !hatch->isMorphing()
                && self->minerals() >= 150 && self->gas() >= 100)
            {
                hatch->morph(UnitTypes::Zerg_Lair);
                break;
            }
        }
    }
    if (lairDone && !denDone && pendingCount(UnitTypes::Zerg_Hydralisk_Den) == 0
        && count(UnitTypes::Zerg_Hydralisk_Den) == 0)
    {
        if (build(UnitTypes::Zerg_Hydralisk_Den, TilePosition(home))) return;
    }
    if (poolDone && count(UnitTypes::Zerg_Evolution_Chamber) == 0
        && pendingCount(UnitTypes::Zerg_Evolution_Chamber) == 0 && (completedHatch >= 2 || frame > 8000))
    {
        if (build(UnitTypes::Zerg_Evolution_Chamber, TilePosition(home))) return;
    }
    // The mutalisk decision: once, when the lair is on the way — harass if their anti-air looks thin
    if (lairDone && !mutaDecisionMade)
    {
        mutaDecisionMade = true;
        goMutalisks = enemyAntiAir < 4;
    }
    if (goMutalisks && count(UnitTypes::Zerg_Spire) == 0 && pendingCount(UnitTypes::Zerg_Spire) == 0 && lairDone)
    {
        if (build(UnitTypes::Zerg_Spire, TilePosition(home))) return;
    }
    if (completedHatch >= 3 && count(UnitTypes::Zerg_Evolution_Chamber) + pendingCount(UnitTypes::Zerg_Evolution_Chamber) < 2)
    {
        if (build(UnitTypes::Zerg_Evolution_Chamber, TilePosition(home))) return;
    }
    // Hive path: queen's nest, then hive, then cavern and mound
    bool nestDone = count(UnitTypes::Zerg_Queens_Nest, false) > 0;
    bool hiveDone = count(UnitTypes::Zerg_Hive, false) > 0;
    if (!hiveTech && completedHatch >= 3 && lairDone && frame > 13000 && self->gas() > 150)
    {
        hiveTech = true;
    }
    if (hiveTech && !nestDone && pendingCount(UnitTypes::Zerg_Queens_Nest) == 0
        && count(UnitTypes::Zerg_Queens_Nest) == 0)
    {
        if (build(UnitTypes::Zerg_Queens_Nest, TilePosition(home))) return;
    }
    if (hiveTech && nestDone && !hiveDone && pendingCount(UnitTypes::Zerg_Hive) == 0)
    {
        for (auto hatch : hatcheries(true))
        {
            if (hatch->getType() == UnitTypes::Zerg_Lair && !hatch->isMorphing()
                && self->minerals() >= 200 && self->gas() >= 150)
            {
                hatch->morph(UnitTypes::Zerg_Hive);
                break;
            }
        }
    }
    if (hiveDone && count(UnitTypes::Zerg_Ultralisk_Cavern) == 0
        && pendingCount(UnitTypes::Zerg_Ultralisk_Cavern) == 0)
    {
        if (build(UnitTypes::Zerg_Ultralisk_Cavern, TilePosition(home))) return;
    }
    if (hiveDone && count(UnitTypes::Zerg_Defiler_Mound) == 0 && pendingCount(UnitTypes::Zerg_Defiler_Mound) == 0)
    {
        if (build(UnitTypes::Zerg_Defiler_Mound, TilePosition(home))) return;
    }
    // Greater spire when the mutalisks need a ground-pounding follow-up
    if (hiveDone && goMutalisks && count(UnitTypes::Zerg_Mutalisk, false) >= 6
        && count(UnitTypes::Zerg_Greater_Spire) == 0 && pendingCount(UnitTypes::Zerg_Greater_Spire) == 0)
    {
        for (auto unit : self->getUnits())
        {
            if (unit->getType() == UnitTypes::Zerg_Spire && unit->isCompleted() && !unit->isMorphing()
                && self->minerals() >= 100 && self->gas() >= 150)
            {
                unit->morph(UnitTypes::Zerg_Greater_Spire);
                break;
            }
        }
    }

    // 5. Expansions: banked minerals and no immediate threat
    if (threatsNearHome().empty() && !rushDefense())
    {
        const Base *next = nextBase();
        int spareMinerals = self->minerals() - reservedMinerals();
        if (next && spareMinerals > 500 && completedHatch >= 2)
        {
            if (build(UnitTypes::Zerg_Hatchery, next->depot)) return;
        }
        // Macro hatchery at home when rich on three bases
        if (completedHatch >= 3 && spareMinerals > 700)
        {
            int homeHatch = 0;
            for (auto hatch : hatcheries(false))
            {
                if (hatch->getDistance(home) < 600) homeHatch++;
            }
            if (homeHatch < 2 && build(UnitTypes::Zerg_Hatchery, TilePosition(home))) return;
        }
    }
}

// Research and upgrades, in priority order. Each fires only with the resources to spare.
void SparkZerg::manageTech()
{
    auto self = Broodwar->self();
    int frame = Broodwar->getFrameCount();

    auto researchFrom = [&](UnitType building, TechType tech) -> bool
    {
        if (self->hasResearched(tech)) return false;
        for (auto unit : self->getUnits())
        {
            // Burrowing is researched at the hatchery, lair or hive — whichever we have
            bool match = building == UnitTypes::Zerg_Hatchery ? isHatcheryType(unit->getType())
                                                              : unit->getType() == building;
            if (!match || !unit->isCompleted() || unit->isMorphing() || unit->isResearching()) continue;
            if (self->minerals() < tech.mineralPrice() || self->gas() < tech.gasPrice()) return false;
            return unit->research(tech);
        }
        return false;
    };
    auto upgradeFrom = [&](UnitType building, UpgradeType up, int maxLevel) -> bool
    {
        if (self->getUpgradeLevel(up) >= maxLevel) return false;
        for (auto unit : self->getUnits())
        {
            if (unit->getType() != building || !unit->isCompleted() || unit->isUpgrading()) continue;
            int level = self->getUpgradeLevel(up);
            if (self->minerals() < up.mineralPrice(level + 1) || self->gas() < up.gasPrice(level + 1)) return false;
            return unit->upgrade(up);
        }
        return false;
    };

    bool lingHeavy = enemyRace == Races::Zerg;
    int hydras = count(UnitTypes::Zerg_Hydralisk, false);

    // 1. Lurker aspect as soon as the den is up
    if (goLurkers && researchFrom(UnitTypes::Zerg_Hydralisk_Den, TechTypes::Lurker_Aspect)) return;
    // 2. Zergling speed in ZvZ or when lings are the army
    if ((lingHeavy || count(UnitTypes::Zerg_Zergling, false) > 12)
        && upgradeFrom(UnitTypes::Zerg_Spawning_Pool, UpgradeTypes::Metabolic_Boost, 1)) return;
    // 3. First attack upgrade
    if (!rushDefense())
    {
        if (lingHeavy)
        {
            if (upgradeFrom(UnitTypes::Zerg_Evolution_Chamber, UpgradeTypes::Zerg_Melee_Attacks, 2)) return;
        }
        else if (upgradeFrom(UnitTypes::Zerg_Evolution_Chamber, UpgradeTypes::Zerg_Missile_Attacks, 1)) return;
    }
    // 4. Hydralisk range and speed
    if (hydras > 0 || count(UnitTypes::Zerg_Hydralisk_Den, false) > 0)
    {
        if (upgradeFrom(UnitTypes::Zerg_Hydralisk_Den, UpgradeTypes::Grooved_Spines, 1)) return;
        if (upgradeFrom(UnitTypes::Zerg_Hydralisk_Den, UpgradeTypes::Muscular_Augments, 1)) return;
    }
    // 5. Carapace, then more attack
    if (!rushDefense())
    {
        if (upgradeFrom(UnitTypes::Zerg_Evolution_Chamber, UpgradeTypes::Zerg_Carapace, 1)) return;
        if (!lingHeavy && upgradeFrom(UnitTypes::Zerg_Evolution_Chamber, UpgradeTypes::Zerg_Missile_Attacks, 3)) return;
        if (upgradeFrom(UnitTypes::Zerg_Evolution_Chamber, UpgradeTypes::Zerg_Carapace, 3)) return;
    }
    // 6. Burrow for lurkers and defence
    if (frame > 8000 && researchFrom(UnitTypes::Zerg_Hatchery, TechTypes::Burrowing)) return;
    // 7. Defiler spells
    if (researchFrom(UnitTypes::Zerg_Defiler_Mound, TechTypes::Plague)) return;
    if (researchFrom(UnitTypes::Zerg_Defiler_Mound, TechTypes::Consume)) return;
    // 8. Overlord speed mid-game for scouting and detection
    if (frame > 11000 && upgradeFrom(UnitTypes::Zerg_Spire, UpgradeTypes::Pneumatized_Carapace, 1)) return;
    // 9. Late-game extras
    if (frame > 15000)
    {
        if (upgradeFrom(UnitTypes::Zerg_Spawning_Pool, UpgradeTypes::Adrenal_Glands, 1)) return;
        if (upgradeFrom(UnitTypes::Zerg_Ultralisk_Cavern, UpgradeTypes::Chitinous_Plating, 1)) return;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Scouting and information

// The first overlord tours the enemy starts; a ling pair checks the front once the pool is up
void SparkZerg::scoutEnemy()
{
    int frame = Broodwar->getFrameCount();
    if (frame < 2000 || frame > 20000) return;

    // Ling scout: two zerglings to the enemy's doorstep once, mid-opening
    bool poolDone = count(UnitTypes::Zerg_Spawning_Pool, false) > 0;
    if (poolDone && scoutLings.empty() && frame > 4500 && frame < 9000)
    {
        int sent = 0;
        for (auto unit : Broodwar->self()->getUnits())
        {
            if (unit->getType() == UnitTypes::Zerg_Zergling && unit->isCompleted() && sent < 2)
            {
                scoutLings.push_back(unit);
                sent++;
            }
        }
    }
    for (auto it = scoutLings.begin(); it != scoutLings.end();)
    {
        Unit ling = *it;
        if (!ling->exists())
        {
            it = scoutLings.erase(it);
            continue;
        }
        Position target = enemyBase != Positions::Unknown ? enemyBase
                        : (!scoutTargets.empty() ? Position(scoutTargets[0]) + Position(64, 48) : home);
        if (ling->getDistance(target) > 200 && ling->getOrder() != Orders::Move)
        {
            ling->move(target);
        }
        ++it;
    }
}

// Remember every enemy building and recent army unit seen; draw scouting conclusions from them
void SparkZerg::trackEnemy()
{
    int frame = Broodwar->getFrameCount();
    auto enemy = Broodwar->enemy();
    if (!enemy) return;

    if (enemyRace == Races::Unknown && enemy->getRace() != Races::Unknown)
    {
        enemyRace = enemy->getRace();
        // Re-pick the opening if we scouted the race late (before it matters)
        if (frame < 2500) chooseBuildOrder();
    }

    int gateways = 0, barracks = 0;
    int enemyLings = 0, zealots = 0, marines = 0;
    int staticDefense = 0, antiAir = 0;
    bool poolSeen = false;

    for (auto unit : enemy->getUnits())
    {
        if (!unit->exists() || !unit->isVisible()) continue;
        auto type = unit->getType();
        if (type.isBuilding())
        {
            enemyBuildings[unit->getID()] = {type, unit->getPosition()};
            if (type == UnitTypes::Zerg_Spawning_Pool) poolSeen = true;
            if (type == UnitTypes::Protoss_Gateway) gateways++;
            if (type == UnitTypes::Terran_Barracks) barracks++;
            if (type == UnitTypes::Protoss_Photon_Cannon || type == UnitTypes::Terran_Missile_Turret
                || type == UnitTypes::Terran_Bunker || type == UnitTypes::Zerg_Sunken_Colony
                || type == UnitTypes::Zerg_Spore_Colony)
            {
                staticDefense++;
                if (type != UnitTypes::Terran_Bunker && type != UnitTypes::Zerg_Sunken_Colony) antiAir++;
            }
            if (type == UnitTypes::Terran_Bunker && unit->getDistance(home) < 1200) bunkerRushSeen = true;
            // Proxy: an enemy offensive building going up near our main (cannon/pylon/bunker rush)
            if ((type == UnitTypes::Protoss_Photon_Cannon || type == UnitTypes::Protoss_Pylon
                 || type == UnitTypes::Terran_Bunker) && unit->getDistance(home) < 1100 && frame < 9000)
            {
                proxySeen = true;
                proxyPosition = unit->getPosition();
            }
            if ((type == UnitTypes::Protoss_Stargate || type == UnitTypes::Terran_Starport
                 || type == UnitTypes::Zerg_Spire) && frame < 12000) airTechSeen = true;
            if (type == UnitTypes::Protoss_Templar_Archives) templarTechSeen = true;
            // Fast expansion: their natural taken early
            if (type.isResourceDepot() && natural && unit->getDistance(natural->center) > 800
                && unit->getDistance(home) > 800 && frame < 6000)
            {
                fastExpandSeen = true;
            }
            if (type.isResourceDepot() && enemyBase == Positions::Unknown) enemyBase = unit->getPosition();
        }
        else
        {
            int health = unit->getHitPoints() + unit->getShields();
            enemyArmy[unit->getID()] = {type, health, frame};
            if (type == UnitTypes::Zerg_Zergling) enemyLings++;
            if (type == UnitTypes::Protoss_Zealot) zealots++;
            if (type == UnitTypes::Terran_Marine) marines++;
            if (type == UnitTypes::Protoss_Dark_Templar || type == UnitTypes::Terran_Wraith) cloakSeen = true;
            if (type == UnitTypes::Zerg_Lurker && unit->isBurrowed()) cloakSeen = true;
            if (type == UnitTypes::Zerg_Mutalisk || type == UnitTypes::Terran_Wraith || type == UnitTypes::Protoss_Corsair
                || type == UnitTypes::Protoss_Scout)
            {
                airArmySeen = true;
            }
            auto air = type.airWeapon();
            if (air != WeaponTypes::None && type.canAttack() && !type.isWorker() && !type.isBuilding()) antiAir++;
        }
        if (unit->getType().isWorker() && hostile(unit) && unit->getDistance(home) < 900) workerRushSeen = true;
    }

    // Scouting conclusions, each latched once
    if (poolSeen && frame < 3500) earlyPoolSeen = true;                       // pool before ~2:25: rush coming
    if (enemyLings >= 4 && frame < 4200) earlyPoolSeen = true;
    if (gateways >= 2 && frame < 5500) gatewayRushSeen = true;
    if (zealots >= 2 && frame < 5000) gatewayRushSeen = true;
    if (barracks >= 2 && frame < 5500) barracksRushSeen = true;
    if (marines >= 6 && frame < 6000) barracksRushSeen = true;

    int tanks = 0, factories = 0;
    for (auto &entry : enemyBuildings)
    {
        auto t = entry.second.first;
        if (t == UnitTypes::Terran_Siege_Tank_Tank_Mode || t == UnitTypes::Terran_Siege_Tank_Siege_Mode) tanks++;
        if (t == UnitTypes::Terran_Factory) factories++;
    }
    for (auto &entry : enemyArmy)
    {
        auto t = entry.second.type;
        if (t == UnitTypes::Terran_Siege_Tank_Tank_Mode || t == UnitTypes::Terran_Siege_Tank_Siege_Mode
            || t == UnitTypes::Terran_Vulture) tanks++;
    }
    if (factories >= 3 || tanks >= 5) mechSeen = true;

    // Anti-air near their base decides the mutalisk plan
    enemyStaticDefense = staticDefense;
    if (enemyBase != Positions::Unknown)
    {
        int nearBase = 0;
        for (auto &entry : enemyBuildings)
        {
            auto t = entry.second.first;
            if ((t == UnitTypes::Protoss_Photon_Cannon || t == UnitTypes::Terran_Missile_Turret
                 || t == UnitTypes::Zerg_Spore_Colony) && entry.second.second.getApproxDistance(enemyBase) < 900)
            {
                nearBase++;
            }
        }
        enemyAntiAir = nearBase;
        for (auto &entry : enemyArmy)
        {
            auto t = entry.second.type;
            if ((t == UnitTypes::Terran_Marine || t == UnitTypes::Zerg_Hydralisk || t == UnitTypes::Protoss_Dragoon)
                && entry.second.frame > frame - 3000) enemyAntiAir++;
        }
        enemyAntiAir = std::min(enemyAntiAir, 30);
    }

    // Forget army units not seen for a while
    for (auto it = enemyArmy.begin(); it != enemyArmy.end();)
    {
        if (frame - it->second.frame > 5000) it = enemyArmy.erase(it);
        else ++it;
    }
}

// Enemies threatening our bases right now
Unitset SparkZerg::threatsNearHome() const
{
    Unitset threats;
    for (auto enemy : Broodwar->enemy()->getUnits())
    {
        if (!enemy->exists() || !enemy->isVisible() || !hostile(enemy)) continue;
        for (auto hatch : hatcheries(false))
        {
            if (enemy->getDistance(hatch) < 800)
            {
                threats.insert(enemy);
                break;
            }
        }
    }
    return threats;
}

// Drones fight back when a base is under attack: a few attack the nearest threat, the rest keep mining.
// A proxy building near the main gets every drone it needs until it dies.
void SparkZerg::defendMineralLines()
{
    auto threats = threatsNearHome();
    auto self = Broodwar->self();

    // Proxy first: kill the pylon/cannon/bunker before it goes up
    if (proxySeen && proxyPosition != Positions::Invalid)
    {
        Unit proxy = nullptr;
        for (auto enemy : Broodwar->enemy()->getUnits())
        {
            if (enemy->exists() && enemy->isVisible() && enemy->getType().isBuilding()
                && enemy->getDistance(proxyPosition) < 300)
            {
                proxy = enemy;
                break;
            }
        }
        if (!proxy)
        {
            proxySeen = false;  // it died or was never really there
        }
        else
        {
            int pulled = 0;
            for (auto drone : self->getUnits())
            {
                if (drone->getType() != UnitTypes::Zerg_Drone || !drone->isCompleted()) continue;
                if (builders.count(drone) || drone->isMorphing()) continue;
                if (drone->getDistance(proxy) > 900) continue;
                drone->attack(proxy);
                if (++pulled >= 8) break;
            }
            return;
        }
    }

    if (threats.empty()) return;
    int pulled = 0;
    for (auto drone : self->getUnits())
    {
        if (pulled >= DroneDefensePull) break;
        if (drone->getType() != UnitTypes::Zerg_Drone || !drone->isCompleted()) continue;
        if (builders.count(drone) || drone->isMorphing()) continue;
        Unit closest = nullptr;
        int best = INT_MAX;
        for (auto threat : threats)
        {
            int d = drone->getDistance(threat);
            if (d < best)
            {
                best = d;
                closest = threat;
            }
        }
        if (closest && best < 700)
        {
            drone->attack(closest);
            pulled++;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Army

void SparkZerg::addToGroup(UnitType type, int health, double &durability, double &dps)
{
    int maxHp = std::max(1, type.maxHitPoints() + type.maxShields());
    double frac = std::min(1.0, double(health) / maxHp);
    durability += maxHp * frac;
    auto addWeapon = [&](WeaponType weapon)
    {
        if (weapon == WeaponTypes::None) return;
        double d = double(weapon.damageAmount() * weapon.damageFactor()) / std::max(1, weapon.damageCooldown());
        dps += d * 24.0 * frac;  // per second
    };
    addWeapon(type.groundWeapon());
    // Air weapons count when the unit has no ground weapon (scourge, corsairs); otherwise ground dominates
    if (type.groundWeapon() == WeaponTypes::None) addWeapon(type.airWeapon());
}

double SparkZerg::strength(Unit unit)
{
    if (!unit || !unit->exists()) return 0;
    double durability = 0, dps = 0;
    addToGroup(unit->getType(), unit->getHitPoints() + unit->getShields(), durability, dps);
    return groupStrength(durability, dps);
}

int SparkZerg::armySupply() const
{
    int supply = 0;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (isArmy(unit->getType()) && unit->isCompleted()) supply += unit->getType().supplyRequired();
    }
    return supply;
}

void SparkZerg::fight(Unit unit, Position goal, Position anchor, int leash)
{
    // A badly damaged unit steps back so healthier ones take the front (only with enemies close)
    if (unit->getHitPoints() * 3 < unit->getType().maxHitPoints())
    {
        auto closest = unit->getClosestUnit(IsEnemy && IsVisible && CanAttack, 224);
        auto fresher = unit->getClosestUnit(IsOwned && IsCompleted && [&](Unit u)
        {
            return isArmy(u->getType()) && u->getHitPoints() * 2 >= u->getType().maxHitPoints();
        }, 160);
        if (closest && fresher)
        {
            unit->move(towards(unit->getPosition(), unit->getPosition() * 2 - closest->getPosition(), 96));
            return;
        }
    }

    // Hydralisks step back from melee units while their weapon reloads
    if (unit->getType() == UnitTypes::Zerg_Hydralisk && unit->getGroundWeaponCooldown() > 10)
    {
        auto melee = unit->getClosestUnit(IsEnemy && IsVisible && !IsFlying && !IsWorker
                                          && [](Unit u) { return isMelee(u->getType()); }, 96);
        if (melee)
        {
            unit->move(towards(unit->getPosition(), unit->getPosition() * 2 - melee->getPosition(), 64));
            return;
        }
    }

    // Pick a target: the most important kind first, then one already in weapon range, then the closest to dying.
    // Raiding zerglings inside the enemy base dive workers over army units.
    bool raiding = attacking && unit->getType() == UnitTypes::Zerg_Zergling && enemyBase != Positions::Unknown
                   && unit->getDistance(enemyBase) < 900;
    Unit best = nullptr;
    int bestScore = INT_MIN;
    auto self = Broodwar->self();
    auto groundWeapon = unit->getType().groundWeapon(), airWeapon = unit->getType().airWeapon();
    bool hitsGround = groundWeapon != WeaponTypes::None, hitsAir = airWeapon != WeaponTypes::None;
    int groundRange = hitsGround ? self->weaponMaxRange(groundWeapon) : 0;
    int airRange = hitsAir ? self->weaponMaxRange(airWeapon) : 0;
    int range = std::max({groundRange, airRange, 32}) + 64;
    auto inReach = [&](Unit enemy, int slack)
    {
        return unit->getDistance(enemy) <= (enemy->isFlying() ? airRange : groundRange) + slack;
    };
    // Hydralisks focus fire: prefer a target in range that others are already shooting, short of overkill
    bool focus = unit->getType() == UnitTypes::Zerg_Hydralisk;
    auto othersAim = [&](Unit enemy)
    {
        auto it = aimedDamage.find(enemy);
        int aimed = it == aimedDamage.end() ? 0 : it->second;
        if (unit->getOrderTarget() == enemy)
            aimed -= Broodwar->getDamageFrom(unit->getType(), enemy->getType(), self, enemy->getPlayer());
        return aimed;
    };
    auto overkilled = [&](Unit enemy)
    {
        return focus && othersAim(enemy) >= enemy->getHitPoints() + enemy->getShields();
    };
    auto leashed = [&](Unit enemy)
    {
        return leash > 0 && enemy->getDistance(anchor) > leash && !inReach(enemy, 32);
    };
    for (auto enemy : unit->getUnitsInRadius(range + 96, IsEnemy && IsVisible))
    {
        if (!enemy->isDetected() || (enemy->isFlying() ? !hitsAir : !hitsGround) || leashed(enemy)) continue;
        int health = enemy->getHitPoints() + enemy->getShields();
        int score = targetPriority(enemy) * 10000 + (inReach(enemy, 16) ? 5000 : 0) - health - unit->getDistance(enemy);
        if (raiding && enemy->getType().isWorker()) score += 20000;
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
        // frame cancels the attack before it fires
        auto current = unit->getOrderTarget();
        if (current && current->exists() && current->isVisible() && current->getPlayer() == Broodwar->enemy()
            && targetPriority(current) >= targetPriority(best) && (inReach(current, 16) || !inReach(best, 16))
            && !(overkilled(current) && current != best) && !leashed(current))
        {
            if (unit->getOrder() != Orders::AttackUnit) unit->attack(current);
            return;
        }
        if (Broodwar->getFrameCount() - unit->getLastCommandFrame() < 6 && unit->getOrder() == Orders::AttackUnit
            && !(current && current->exists() && leashed(current))) return;
        unit->attack(best);
        return;
    }
    // On a leash with nothing to take on: to the goal and hold there
    if (leash > 0)
    {
        if (unit->getDistance(goal) > 128)
        {
            if (unit->getOrder() != Orders::Move || unit->getTargetPosition().getApproxDistance(goal) > 64) unit->move(goal);
        }
        else if (unit->getOrder() != Orders::HoldPosition)
        {
            unit->holdPosition();
        }
        return;
    }
    if (unit->getOrder() != Orders::AttackMove || unit->getTargetPosition().getApproxDistance(goal) > 64)
    {
        unit->attack(goal);
    }
}

// Mutalisks (and guardians/devourers): stack up, then harass workers and weak spots, running from real anti-air
void SparkZerg::controlMutalisks(const Unitset &army)
{
    int frame = Broodwar->getFrameCount();
    // Guardians: when static defence outguns the mutalisks, morph a few into guardians to crack it
    {
        bool greaterSpire = false;
        int mutaCount = 0, guardians = 0;
        for (auto u : Broodwar->self()->getUnits())
        {
            if (u->getType() == UnitTypes::Zerg_Greater_Spire && u->isCompleted()) greaterSpire = true;
        }
        for (auto m : army)
        {
            if (m->getType() == UnitTypes::Zerg_Mutalisk) mutaCount++;
            if (m->getType() == UnitTypes::Zerg_Guardian) guardians++;
        }
        if (greaterSpire && mutaCount >= 8 && guardians < 4 && enemyStaticDefense >= 6)
        {
            for (auto m : army)
            {
                if (m->getType() != UnitTypes::Zerg_Mutalisk || !m->isCompleted()) continue;
                if (!m->getUnitsInRadius(500, IsEnemy && IsVisible).empty()) continue;
                m->morph(UnitTypes::Zerg_Guardian);
                break;
            }
        }
    }
    Position stack = enemyBase != Positions::Unknown ? towards(enemyBase, home, 420) : rally;
    for (auto muta : army)
    {
        auto type = muta->getType();
        if (type != UnitTypes::Zerg_Mutalisk && type != UnitTypes::Zerg_Guardian && type != UnitTypes::Zerg_Devourer) continue;
        // Hurt: go home
        if (muta->getHitPoints() * 3 < type.maxHitPoints())
        {
            if (muta->getOrder() != Orders::Move) muta->move(home);
            continue;
        }
        // Real anti-air close: leave, unless we already outnumber it badly
        int aa = 0;
        Unit nearestAA = nullptr;
        for (auto enemy : muta->getUnitsInRadius(360, IsEnemy && IsVisible))
        {
            auto air = enemy->getType().airWeapon();
            if (air != WeaponTypes::None && !enemy->getType().isWorker() && !enemy->getType().isBuilding())
            {
                aa++;
                nearestAA = enemy;
            }
        }
        if (aa >= 3 && nearestAA)
        {
            muta->move(towards(muta->getPosition(), muta->getPosition() * 2 - nearestAA->getPosition(), 256));
            continue;
        }
        // A target near the enemy: workers first, then buildings, then anything
        Unit target = nullptr;
        int bestScore = INT_MIN;
        for (auto enemy : muta->getUnitsInRadius(800, IsEnemy && IsVisible))
        {
            if (enemy->isFlying()) continue;
            auto t = enemy->getType();
            int score = (t.isWorker() ? 20000 : (t.isBuilding() ? 5000 : 0)) - muta->getDistance(enemy);
            if (score > bestScore)
            {
                bestScore = score;
                target = enemy;
            }
        }
        if (target)
        {
            if (muta->getOrderTarget() != target) muta->attack(target);
            continue;
        }
        // Otherwise stack: all mutas move to the same point every few seconds so their shots land together
        if (frame % 96 < 12)
        {
            if (muta->getDistance(stack) > 96) muta->move(stack);
        }
        else if (muta->getOrder() != Orders::Move && muta->getDistance(stack) > 320)
        {
            muta->move(stack);
        }
    }
}

// Lurkers: morph from spare hydralisks, burrow with the army when enemies close, unburrow to reposition
void SparkZerg::controlLurkers(const Unitset &army)
{
    bool aspect = Broodwar->self()->hasResearched(TechTypes::Lurker_Aspect);
    bool canBurrow = Broodwar->self()->hasResearched(TechTypes::Burrowing);
    int lurkers = 0, hydras = 0;
    for (auto unit : army)
    {
        if (unit->getType() == UnitTypes::Zerg_Lurker) lurkers++;
        if (unit->getType() == UnitTypes::Zerg_Hydralisk) hydras++;
    }
    // Morph: keep about a third of the hydra force as lurkers, capped, only from hydras far from fighting
    if (goLurkers && aspect && lurkers < 8 && lurkers < hydras / 2)
    {
        for (auto unit : army)
        {
            if (unit->getType() != UnitTypes::Zerg_Hydralisk || !unit->isCompleted()) continue;
            if (!unit->getUnitsInRadius(500, IsEnemy && IsVisible).empty()) continue;
            unit->morph(UnitTypes::Zerg_Lurker);
            break;
        }
    }
    Position goal = attacking && enemyBase != Positions::Unknown
                      ? enemyBase
                      : (naturalFront != Positions::Invalid ? naturalFront : rally);
    for (auto unit : army)
    {
        if (unit->getType() != UnitTypes::Zerg_Lurker || !unit->isCompleted() || unit->isMorphing()) continue;
        bool enemiesNear = !unit->getUnitsInRadius(380, IsEnemy && IsVisible).empty();
        if (unit->isBurrowed())
        {
            // Unburrow to keep up when the fight moved on
            if (!enemiesNear && unit->getDistance(goal) > 700 && canBurrow)
            {
                unit->unburrow();
            }
            continue;
        }
        if (enemiesNear && canBurrow)
        {
            unit->burrow();
            continue;
        }
        // Unburrowed and idle: walk to the goal, then burrow on arrival handled next frames
        if (unit->getDistance(goal) > 200)
        {
            if (unit->getOrder() != Orders::Move) unit->move(goal);
        }
        else if (canBurrow)
        {
            unit->burrow();
        }
    }
}

// Defilers: plague clumps, consume for energy, stay behind the army
void SparkZerg::controlDefilers(const Unitset &army)
{
    auto self = Broodwar->self();
    bool plague = self->hasResearched(TechTypes::Plague);
    bool consume = self->hasResearched(TechTypes::Consume);
    Position anchor = attacking && enemyBase != Positions::Unknown ? enemyBase : rally;
    // Army center to hide behind
    Position center(0, 0);
    int n = 0;
    for (auto unit : army)
    {
        auto t = unit->getType();
        if (t == UnitTypes::Zerg_Hydralisk || t == UnitTypes::Zerg_Zergling || t == UnitTypes::Zerg_Ultralisk)
        {
            center += unit->getPosition();
            n++;
        }
    }
    Position hide = n ? Position(center.x / n, center.y / n) : anchor;
    hide = towards(hide, home, 160);
    for (auto unit : army)
    {
        if (unit->getType() != UnitTypes::Zerg_Defiler || !unit->isCompleted()) continue;
        // Plague the densest clump in range
        if (plague && unit->getEnergy() >= 150)
        {
            Position best = Positions::Invalid;
            int bestCount = 4;
            for (auto enemy : unit->getUnitsInRadius(320, IsEnemy && IsVisible && !IsFlying))
            {
                int clump = 0;
                for (auto other : unit->getUnitsInRadius(320, IsEnemy && IsVisible && !IsFlying))
                {
                    if (other->getDistance(enemy) < 160) clump++;
                }
                if (clump > bestCount)
                {
                    bestCount = clump;
                    best = enemy->getPosition();
                }
            }
            if (best != Positions::Invalid)
            {
                unit->useTech(TechTypes::Plague, best);
                continue;
            }
        }
        // Consume a zergling when low on energy and enemies are near
        if (consume && unit->getEnergy() < 100
            && !unit->getUnitsInRadius(600, IsEnemy && IsVisible).empty())
        {
            auto meal = unit->getClosestUnit(IsOwned && IsCompleted && [](Unit u)
            {
                return u->getType() == UnitTypes::Zerg_Zergling;
            }, 320);
            if (meal)
            {
                unit->useTech(TechTypes::Consume, meal);
                continue;
            }
        }
        // Enemies on top of the defiler: run to the army
        if (!unit->getUnitsInRadius(200, IsEnemy && IsVisible).empty())
        {
            unit->move(hide);
            continue;
        }
        if (unit->getDistance(hide) > 128 && unit->getOrder() != Orders::Move) unit->move(hide);
    }
}

// Scourge: suicide into the most valuable flyers — capitals first, then mutalisks/wraiths, then overlords
void SparkZerg::controlScourge(const Unitset &army)
{
    for (auto unit : army)
    {
        if (unit->getType() != UnitTypes::Zerg_Scourge || !unit->isCompleted()) continue;
        Unit target = nullptr;
        int bestScore = INT_MIN;
        for (auto enemy : unit->getUnitsInRadius(1200, IsEnemy && IsVisible && IsFlying))
        {
            auto t = enemy->getType();
            int value = 0;
            if (t == UnitTypes::Protoss_Carrier || t == UnitTypes::Terran_Battlecruiser
                || t == UnitTypes::Protoss_Arbiter) value = 30000;
            else if (t == UnitTypes::Zerg_Guardian || t == UnitTypes::Zerg_Devourer) value = 20000;
            else if (t.canAttack()) value = 10000;
            else value = 1000;
            int score = value - unit->getDistance(enemy);
            if (score > bestScore)
            {
                bestScore = score;
                target = enemy;
            }
        }
        if (target)
        {
            if (unit->getOrderTarget() != target) unit->attack(target);
        }
        else
        {
            Position wait = attacking && enemyBase != Positions::Unknown ? towards(enemyBase, home, 500) : rally;
            if (unit->getDistance(wait) > 160 && unit->getOrder() != Orders::Move) unit->move(wait);
        }
    }
}

// Overlords with the army are its detectors: follow at a distance, fleeing what shoots up is in manageOverlords
void SparkZerg::controlOverlords(const Unitset &army)
{
    Position center(0, 0);
    int n = 0;
    for (auto unit : army)
    {
        auto t = unit->getType();
        if (t == UnitTypes::Zerg_Hydralisk || t == UnitTypes::Zerg_Zergling || t == UnitTypes::Zerg_Ultralisk
            || t == UnitTypes::Zerg_Lurker)
        {
            center += unit->getPosition();
            n++;
        }
    }
    if (!n) return;
    Position follow(towards(Position(center.x / n, center.y / n), home, 200));
    int assigned = 0;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (assigned >= 2) break;
        if (unit->getType() != UnitTypes::Zerg_Overlord || !unit->isCompleted()) continue;
        if (unit == scoutOverlord && !scoutingDone) continue;
        if (!unit->getUnitsInRadius(400, IsEnemy && IsVisible).empty()) continue;  // fleeing handled elsewhere
        if (unit->getDistance(follow) > 200 && unit->getOrder() != Orders::Move) unit->move(follow);
        assigned++;
    }
}

void SparkZerg::controlArmy()
{
    auto self = Broodwar->self();
    int frame = Broodwar->getFrameCount();

    // Damage already heading at each enemy, so hydralisks focus fire without overkilling
    aimedDamage.clear();
    Unitset army;
    for (auto unit : self->getUnits())
    {
        if (!unit->isCompleted() || unit->isMorphing()) continue;
        if (!isArmy(unit->getType())) continue;
        army.insert(unit);
        if (unit->getOrder() == Orders::AttackUnit)
        {
            auto target = unit->getOrderTarget();
            if (target && target->exists() && target->getPlayer() == Broodwar->enemy())
            {
                aimedDamage[target] += Broodwar->getDamageFrom(unit->getType(), target->getType(), self,
                                                              Broodwar->enemy());
            }
        }
    }
    if (army.empty()) return;

    controlMutalisks(army);
    controlLurkers(army);
    controlDefilers(army);
    controlScourge(army);
    controlOverlords(army);

    // Attack or retreat, judged every half second from the armies' estimated strength
    if (frame % 12 == 0)
    {
        double ours = 0, theirs = 0;
        for (auto unit : army)
        {
            if (unit->getDistance(rally) < 1200) ours += strength(unit);
        }
        for (auto &entry : enemyArmy)
        {
            if (frame - entry.second.frame > 3000) continue;
            double durability = 0, dps = 0;
            addToGroup(entry.second.type, entry.second.health, durability, dps);
            theirs += groupStrength(durability, dps);
        }
        int supply = armySupply();
        if (!attacking && !rushDefense() && supply >= FirstAttackSupply && ours > theirs * AttackRatio
            && frame - lastRetreatFrame > 1200)
        {
            attacking = true;
            gatherStart = frame;
        }
        else if (attacking && theirs > ours * RetreatRatio)
        {
            attacking = false;
            lastRetreatFrame = frame;
        }
        // Nothing known and a real army: go look for them
        if (!attacking && supply >= FirstAttackSupply + 14 && theirs == 0 && enemyBase != Positions::Unknown)
        {
            attacking = true;
            gatherStart = frame;
        }
    }

    Position goal = enemyBase != Positions::Unknown ? enemyBase : rally;
    if (attacking && enemyBase != Positions::Unknown)
    {
        // If their base looks empty, swing through their known buildings
        bool baseEmpty = true;
        for (auto &entry : enemyBuildings)
        {
            if (entry.second.second.getApproxDistance(enemyBase) < 900)
            {
                baseEmpty = false;
                break;
            }
        }
        if (baseEmpty && !enemyBuildings.empty()) goal = enemyBuildings.begin()->second.second;
    }

    auto threats = threatsNearHome();
    for (auto unit : army)
    {
        auto type = unit->getType();
        if (type == UnitTypes::Zerg_Mutalisk || type == UnitTypes::Zerg_Guardian || type == UnitTypes::Zerg_Devourer
            || type == UnitTypes::Zerg_Scourge || type == UnitTypes::Zerg_Defiler || type == UnitTypes::Zerg_Overlord)
            continue;  // handled above
        if (type == UnitTypes::Zerg_Lurker && unit->isBurrowed()) continue;

        // Defence first: threats at our bases are met on a leash
        bool defending = false;
        if (!threats.empty() && !attacking)
        {
            Unit threat = *threats.begin();
            Unit hatch = nullptr;
            int best = INT_MAX;
            for (auto h : hatcheries(false))
            {
                int d = h->getDistance(threat);
                if (d < best)
                {
                    best = d;
                    hatch = h;
                }
            }
            if (hatch && unit->getDistance(hatch) < 1400)
            {
                fight(unit, threat->getPosition(), hatch->getPosition(), LeashDistance);
                defending = true;
            }
        }
        if (defending) continue;

        if (attacking)
        {
            fight(unit, goal);
        }
        else
        {
            // Gather at the natural's front, drifting to the rally if it is far
            Position wait = naturalFront != Positions::Invalid ? naturalFront : rally;
            if (!threats.empty()) wait = home;
            if (unit->getDistance(wait) > RegroupDistance)
            {
                if (unit->getOrder() != Orders::Move
                    || unit->getTargetPosition().getApproxDistance(wait) > 64) unit->move(wait);
            }
            else
            {
                // Skirmishers: lings poke at nearby enemies while the army gathers
                auto near = unit->getClosestUnit(IsEnemy && IsVisible && CanAttack, 420);
                if (near && type == UnitTypes::Zerg_Zergling) fight(unit, near->getPosition(), wait, LeashDistance);
                else if (unit->getOrder() != Orders::HoldPosition
                         && unit->getOrder() != Orders::Move) unit->holdPosition();
            }
        }
    }
}

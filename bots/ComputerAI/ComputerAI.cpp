#include "ComputerAI.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

using namespace BWAPI;
using namespace BWAPI::Filter;

namespace
{
    bool isDepot(UnitType type)
    {
        return type.isResourceDepot();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Setup

void ComputerAI::onStart()
{
    Broodwar->enableFlag(Flag::UserInput);
    race = Broodwar->self()->getRace();
    homeTile = Broodwar->self()->getStartLocation();
    home = Position(homeTile) + Position(64, 48);
    Position center(Broodwar->mapWidth() * 16, Broodwar->mapHeight() * 16);
    rally = home + (center - home) * 320 / std::max(1, home.getApproxDistance(center));
    nextWaveFrame = 7200;  // about 5 minutes in, like the built-in AI's first attack

    // The script: buildings at set supply counts, as the built-in AI's race scripts do; supply buildings are added as
    // needed rather than scripted
    if (race == Races::Terran)
    {
        plan = {{10, UnitTypes::Terran_Barracks}, {12, UnitTypes::Terran_Refinery}, {15, UnitTypes::Terran_Barracks},
                {18, UnitTypes::Terran_Academy}, {22, UnitTypes::Terran_Factory},
                {26, UnitTypes::Terran_Command_Center}, {28, UnitTypes::Terran_Machine_Shop},
                {30, UnitTypes::Terran_Engineering_Bay}, {33, UnitTypes::Terran_Barracks},
                {35, UnitTypes::Terran_Missile_Turret}, {38, UnitTypes::Terran_Refinery},
                {42, UnitTypes::Terran_Factory}, {46, UnitTypes::Terran_Armory}, {52, UnitTypes::Terran_Starport},
                {60, UnitTypes::Terran_Barracks}, {70, UnitTypes::Terran_Command_Center},
                {76, UnitTypes::Terran_Refinery}, {84, UnitTypes::Terran_Factory},
                {88, UnitTypes::Terran_Machine_Shop}};
        upgrades = {{UnitTypes::Terran_Academy, UpgradeTypes::U_238_Shells},
                    {UnitTypes::Terran_Engineering_Bay, UpgradeTypes::Terran_Infantry_Weapons},
                    {UnitTypes::Terran_Machine_Shop, UpgradeTypes::Ion_Thrusters}};
        techs = {{UnitTypes::Terran_Academy, TechTypes::Stim_Packs},
                 {UnitTypes::Terran_Machine_Shop, TechTypes::Tank_Siege_Mode}};
    }
    else if (race == Races::Protoss)
    {
        plan = {{10, UnitTypes::Protoss_Gateway}, {12, UnitTypes::Protoss_Assimilator},
                {14, UnitTypes::Protoss_Cybernetics_Core}, {18, UnitTypes::Protoss_Gateway},
                {24, UnitTypes::Protoss_Forge}, {26, UnitTypes::Protoss_Nexus},
                {30, UnitTypes::Protoss_Robotics_Facility}, {32, UnitTypes::Protoss_Photon_Cannon},
                {34, UnitTypes::Protoss_Assimilator}, {36, UnitTypes::Protoss_Gateway},
                {40, UnitTypes::Protoss_Citadel_of_Adun}, {44, UnitTypes::Protoss_Robotics_Support_Bay},
                {50, UnitTypes::Protoss_Stargate}, {60, UnitTypes::Protoss_Fleet_Beacon},
                {66, UnitTypes::Protoss_Gateway}, {70, UnitTypes::Protoss_Nexus},
                {78, UnitTypes::Protoss_Assimilator}, {88, UnitTypes::Protoss_Stargate}};
        upgrades = {{UnitTypes::Protoss_Cybernetics_Core, UpgradeTypes::Singularity_Charge},
                    {UnitTypes::Protoss_Citadel_of_Adun, UpgradeTypes::Leg_Enhancements},
                    {UnitTypes::Protoss_Forge, UpgradeTypes::Protoss_Ground_Weapons}};
    }
    else
    {
        plan = {{9, UnitTypes::Zerg_Spawning_Pool}, {10, UnitTypes::Zerg_Extractor},
                {13, UnitTypes::Zerg_Hatchery}, {18, UnitTypes::Zerg_Hydralisk_Den}, {22, UnitTypes::Zerg_Lair},
                {26, UnitTypes::Zerg_Evolution_Chamber}, {28, UnitTypes::Zerg_Extractor},
                {32, UnitTypes::Zerg_Spire}, {38, UnitTypes::Zerg_Hatchery}, {48, UnitTypes::Zerg_Hatchery},
                {52, UnitTypes::Zerg_Extractor}};
        upgrades = {{UnitTypes::Zerg_Spawning_Pool, UpgradeTypes::Metabolic_Boost},
                    {UnitTypes::Zerg_Hydralisk_Den, UpgradeTypes::Grooved_Spines},
                    {UnitTypes::Zerg_Hydralisk_Den, UpgradeTypes::Muscular_Augments},
                    {UnitTypes::Zerg_Evolution_Chamber, UpgradeTypes::Zerg_Missile_Attacks}};
    }

    findBases();
}

// Bases: each cluster of mineral fields, with its depot where it is closest to its resources while staying the
// required three tiles clear of them
void ComputerAI::findBases()
{
    std::vector<std::vector<Unit>> clusters;
    for (auto mineral : Broodwar->getStaticMinerals())
    {
        if (mineral->getInitialResources() < 200) continue;  // mineral walls and blockers
        bool added = false;
        for (auto &cluster : clusters)
        {
            for (auto other : cluster)
            {
                if (other->getInitialPosition().getApproxDistance(mineral->getInitialPosition()) < 256)
                {
                    cluster.push_back(mineral);
                    added = true;
                    break;
                }
            }
            if (added) break;
        }
        if (!added) clusters.push_back({mineral});
    }

    for (auto &cluster : clusters)
    {
        if (cluster.size() < 4) continue;
        Position sum(0, 0);
        for (auto mineral : cluster) sum += mineral->getInitialPosition();
        Position centroid = sum / (int) cluster.size();
        std::vector<Unit> resources = cluster;
        for (auto geyser : Broodwar->getStaticGeysers())
        {
            if (geyser->getInitialPosition().getApproxDistance(centroid) < 400) resources.push_back(geyser);
        }

        TilePosition best = TilePositions::None;
        int bestScore = INT_MAX;
        TilePosition c(centroid);
        for (int y = c.y - 12; y <= c.y + 12; y++)
        {
            for (int x = c.x - 12; x <= c.x + 12; x++)
            {
                if (x < 0 || y < 0 || x + 4 > Broodwar->mapWidth() || y + 3 > Broodwar->mapHeight()) continue;
                bool ok = true;
                for (int ty = y; ty < y + 3 && ok; ty++)
                {
                    for (int tx = x; tx < x + 4 && ok; tx++) ok = Broodwar->isBuildable(tx, ty);
                }
                for (auto resource : resources)
                {
                    if (!ok) break;
                    TilePosition r = resource->getInitialTilePosition();
                    UnitType type = resource->getInitialType();
                    ok = x + 4 + 3 <= r.x || r.x + type.tileWidth() + 3 <= x
                         || y + 3 + 3 <= r.y || r.y + type.tileHeight() + 3 <= y;
                }
                if (!ok) continue;
                Position depot = Position(TilePosition(x, y)) + Position(64, 48);
                int score = 0;
                for (auto resource : resources) score += depot.getApproxDistance(resource->getInitialPosition());
                if (score < bestScore)
                {
                    bestScore = score;
                    best = TilePosition(x, y);
                }
            }
        }
        if (best != TilePositions::None) bases.push_back({best, Position(best) + Position(64, 48)});
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Each frame

void ComputerAI::onFrame()
{
    if (Broodwar->isPaused() || !Broodwar->self()) return;
    int frame = Broodwar->getFrameCount();
    if (frame % 4 != 0) return;  // the built-in AI isn't quick either

    for (auto it = enemyBuildings.begin(); it != enemyBuildings.end();)
    {
        // Forget buildings that are gone from where we last saw them
        if (it->first->isVisible()) it->second = it->first->getPosition();
        if (!it->first->isVisible() && Broodwar->isVisible(TilePosition(it->second))) it = enemyBuildings.erase(it);
        else ++it;
    }

    updateBuilders();
    reservedMinerals = reservedGas = 0;
    for (auto &[builder, pending] : builders)
    {
        reservedMinerals += pending.type.mineralPrice();
        reservedGas += pending.type.gasPrice();
    }

    followPlan();
    buildSupply();
    research();
    train();
    manageWorkers();
    manageArmy();
}

void ComputerAI::onUnitShow(Unit unit)
{
    if (unit->getPlayer() == Broodwar->enemy() && unit->getType().isBuilding()) enemyBuildings[unit] = unit->getPosition();
}

void ComputerAI::onUnitDestroy(Unit unit)
{
    enemyBuildings.erase(unit);
    builders.erase(unit);
    wave.erase(unit);
}

// ---------------------------------------------------------------------------------------------------------------------
// Buildings

bool ComputerAI::isArmy(UnitType type)
{
    if (type.isWorker() || type.isBuilding()) return false;
    switch (type)
    {
        case UnitTypes::Enum::Zerg_Overlord:
        case UnitTypes::Enum::Zerg_Larva:
        case UnitTypes::Enum::Zerg_Egg:
        case UnitTypes::Enum::Zerg_Lurker_Egg:
        case UnitTypes::Enum::Zerg_Cocoon:
        case UnitTypes::Enum::Protoss_Interceptor:
        case UnitTypes::Enum::Protoss_Scarab:
        case UnitTypes::Enum::Protoss_Shuttle:
        case UnitTypes::Enum::Terran_Dropship:
        case UnitTypes::Enum::Terran_Vulture_Spider_Mine:
        case UnitTypes::Enum::Terran_Nuclear_Missile:
            return false;
        default:
            return true;
    }
}

// Units of a type we have or are making; depots count as one kind, and lairs count as hatcheries too
int ComputerAI::count(UnitType type) const
{
    auto self = Broodwar->self();
    int n;
    if (isDepot(type))
    {
        n = self->allUnitCount(UnitTypes::Terran_Command_Center) + self->allUnitCount(UnitTypes::Protoss_Nexus)
            + self->allUnitCount(UnitTypes::Zerg_Hatchery) + self->allUnitCount(UnitTypes::Zerg_Lair)
            + self->allUnitCount(UnitTypes::Zerg_Hive);
    }
    else if (type == UnitTypes::Zerg_Lair)
    {
        n = self->allUnitCount(UnitTypes::Zerg_Lair) + self->allUnitCount(UnitTypes::Zerg_Hive);
    }
    else
    {
        n = self->allUnitCount(type);
    }
    for (auto &[builder, pending] : builders)
    {
        if (pending.type == type || isDepot(type) && isDepot(pending.type)) n++;
    }
    return n;
}

bool ComputerAI::affordable(UnitType type) const
{
    auto self = Broodwar->self();
    return self->minerals() - reservedMinerals >= type.mineralPrice() && self->gas() - reservedGas >= type.gasPrice();
}

void ComputerAI::updateBuilders()
{
    int frame = Broodwar->getFrameCount();
    for (auto it = builders.begin(); it != builders.end();)
    {
        auto builder = it->first;
        auto &pending = it->second;
        bool started = builder->getType().isBuilding();  // a drone that became the building
        for (auto unit : Broodwar->getUnitsOnTile(pending.tile))
        {
            if (unit->getPlayer() == Broodwar->self() && unit->getType() == pending.type) started = true;
        }
        bool lost = !builder->exists() || frame - pending.frame > 24 * 60
                    || frame - pending.frame > 48 && builder->isIdle();
        if (started || lost)
        {
            if (!started && builder->exists() && builder->getType().isWorker()) builder->stop();
            it = builders.erase(it);
        }
        else ++it;
    }
}

Unit ComputerAI::freeWorker(Position near) const
{
    Unit best = nullptr;
    int bestDistance = INT_MAX;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (!unit->getType().isWorker() || !unit->isCompleted() || builders.count(unit)) continue;
        if (!unit->isGatheringMinerals() && !unit->isIdle() || unit->isCarryingMinerals()) continue;
        int distance = unit->getDistance(near);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = unit;
        }
    }
    return best;
}

TilePosition ComputerAI::nextExpansion() const
{
    TilePosition best = TilePositions::None;
    int bestDistance = INT_MAX;
    for (auto &base : bases)
    {
        bool taken = false;
        for (auto unit : Broodwar->getUnitsInRadius(base.center, 320))
        {
            if (unit->getType().isResourceDepot()) taken = true;
        }
        for (auto &[building, position] : enemyBuildings)
        {
            if (position.getApproxDistance(base.center) < 400) taken = true;
        }
        if (taken || !Broodwar->hasPath(home, base.center)) continue;
        int distance = home.getApproxDistance(base.center);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = base.tile;
        }
    }
    return best;
}

TilePosition ComputerAI::freeGeyser() const
{
    for (auto depot : Broodwar->self()->getUnits())
    {
        if (!depot->getType().isResourceDepot() || !depot->isCompleted()) continue;
        for (auto geyser : Broodwar->getGeysers())
        {
            if (geyser->getDistance(depot) < 320) return geyser->getTilePosition();
        }
    }
    return TilePositions::None;
}

// Starts a building (or an add-on, or a lair); false if it can't now
bool ComputerAI::start(UnitType type)
{
    if (!affordable(type)) return false;
    if (type == UnitTypes::Zerg_Lair)
    {
        for (auto unit : Broodwar->self()->getUnits())
        {
            if (unit->getType() == UnitTypes::Zerg_Hatchery && unit->isCompleted() && !unit->isMorphing())
            {
                return unit->morph(type);
            }
        }
        return false;
    }
    if (type.isAddon())
    {
        for (auto unit : Broodwar->self()->getUnits())
        {
            if (unit->getType() == type.whatBuilds().first && unit->isCompleted() && !unit->getAddon()
                && !unit->isTraining() && unit->buildAddon(type))
            {
                return true;
            }
        }
        return false;
    }

    TilePosition tile;
    if (type.isRefinery()) tile = freeGeyser();
    else if (type.isResourceDepot()) tile = nextExpansion();
    else tile = Broodwar->getBuildLocation(type, homeTile, 48, race == Races::Zerg);
    if (!tile.isValid()) return false;

    auto worker = freeWorker(Position(tile));
    if (!worker || !worker->build(type, tile)) return false;
    builders[worker] = {type, tile, Broodwar->getFrameCount()};
    reservedMinerals += type.mineralPrice();
    reservedGas += type.gasPrice();
    return true;
}

// The first scripted building we're short of, once its supply is reached; its cost is kept for it until then
void ComputerAI::followPlan()
{
    int supply = Broodwar->self()->supplyUsed() / 2;
    std::map<UnitType, int> wanted;
    for (auto &step : plan)
    {
        if (step.supply > supply) break;
        UnitType kind = isDepot(step.type) ? race.getResourceDepot() : step.type;
        int need = ++wanted[kind] + (isDepot(step.type) ? 1 : 0);  // the starting depot isn't in the script
        if (count(step.type) >= need) continue;
        if (!start(step.type))
        {
            reservedMinerals += step.type.mineralPrice();
            reservedGas += step.type.gasPrice();
        }
        return;
    }
}

void ComputerAI::buildSupply()
{
    auto self = Broodwar->self();
    if (self->supplyTotal() >= 400) return;
    int production = 0;
    for (auto unit : self->getUnits())
    {
        if (unit->getType().isResourceDepot() || unit->getType() == UnitTypes::Terran_Barracks
            || unit->getType() == UnitTypes::Terran_Factory || unit->getType() == UnitTypes::Protoss_Gateway
            || unit->getType() == UnitTypes::Protoss_Robotics_Facility)
        {
            production++;
        }
    }
    int margin = 4 + 4 * production;  // in BWAPI's doubled supply
    int pending = 0;
    UnitType supplyType = race.getSupplyProvider();
    for (auto unit : self->getUnits())
    {
        if (unit->getType() == supplyType && !unit->isCompleted()) pending += supplyType.supplyProvided();
        if (unit->getType() == UnitTypes::Zerg_Egg && unit->getBuildType() == supplyType) pending += 16;
    }
    for (auto &[builder, p] : builders)
    {
        if (p.type == supplyType) pending += supplyType.supplyProvided();
    }
    if (self->supplyTotal() + pending - self->supplyUsed() > margin) return;
    if (pending > 0 && self->supplyTotal() - self->supplyUsed() > 2) return;  // one at a time unless blocked

    if (race == Races::Zerg)
    {
        if (self->minerals() - reservedMinerals < 100) return;
        for (auto unit : self->getUnits())
        {
            if (unit->getType() == UnitTypes::Zerg_Larva && unit->morph(supplyType))
            {
                reservedMinerals += 100;
                return;
            }
        }
    }
    else if (!start(supplyType))
    {
        reservedMinerals += supplyType.mineralPrice();
    }
}

void ComputerAI::research()
{
    for (auto &[at, upgrade] : upgrades)
    {
        if (Broodwar->self()->getUpgradeLevel(upgrade) > 0 || Broodwar->self()->isUpgrading(upgrade)) continue;
        for (auto unit : Broodwar->self()->getUnits())
        {
            if (unit->getType() != at || !unit->isCompleted() || !unit->isIdle()) continue;
            if (Broodwar->self()->minerals() - reservedMinerals >= upgrade.mineralPrice()
                && Broodwar->self()->gas() - reservedGas >= upgrade.gasPrice() && unit->upgrade(upgrade))
            {
                reservedMinerals += upgrade.mineralPrice();
                reservedGas += upgrade.gasPrice();
            }
            break;
        }
    }
    for (auto &[at, tech] : techs)
    {
        if (Broodwar->self()->hasResearched(tech) || Broodwar->self()->isResearching(tech)) continue;
        for (auto unit : Broodwar->self()->getUnits())
        {
            if (unit->getType() != at || !unit->isCompleted() || !unit->isIdle()) continue;
            if (Broodwar->self()->minerals() - reservedMinerals >= tech.mineralPrice()
                && Broodwar->self()->gas() - reservedGas >= tech.gasPrice() && unit->research(tech))
            {
                reservedMinerals += tech.mineralPrice();
                reservedGas += tech.gasPrice();
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Units

void ComputerAI::train()
{
    auto self = Broodwar->self();
    auto have = [&](UnitType type) { return self->completedUnitCount(type) > 0; };
    auto all = [&](UnitType type) { return self->allUnitCount(type); };
    auto canAfford = [&](UnitType type) {
        return self->minerals() - reservedMinerals >= type.mineralPrice() && self->gas() - reservedGas >= type.gasPrice()
               && self->supplyTotal() - self->supplyUsed() >= type.supplyRequired();
    };
    auto make = [&](Unit at, UnitType type) {
        if (!canAfford(type)) return false;
        bool ok = type.whatBuilds().first == UnitTypes::Zerg_Larva ? at->morph(type) : at->train(type);
        if (ok)
        {
            reservedMinerals += type.mineralPrice();
            reservedGas += type.gasPrice();
        }
        return ok;
    };

    int depots = count(race.getResourceDepot());
    int refineries = all(race.getRefinery());
    int workerTarget = std::min(60, 18 * depots + 3 * refineries);
    int workers = all(race.getWorker());
    int army = 0;
    for (auto unit : self->getUnits())
    {
        if (isArmy(unit->getType())) army++;
    }

    for (auto unit : self->getUnits())
    {
        if (!unit->isCompleted()) continue;
        auto type = unit->getType();

        // Carriers and reavers need their ammunition
        if (type == UnitTypes::Protoss_Carrier && unit->getInterceptorCount() < 4 && !unit->isTraining())
        {
            make(unit, UnitTypes::Protoss_Interceptor);
        }
        if (type == UnitTypes::Protoss_Reaver && unit->getScarabCount() < 3 && !unit->isTraining())
        {
            make(unit, UnitTypes::Protoss_Scarab);
        }
        if (unit->isTraining() || unit->isMorphing()) continue;

        if (type.isResourceDepot() && race != Races::Zerg && workers < workerTarget)
        {
            if (make(unit, race.getWorker())) workers++;
        }
        else if (type == UnitTypes::Terran_Barracks)
        {
            int marines = all(UnitTypes::Terran_Marine);
            if (have(UnitTypes::Terran_Academy) && all(UnitTypes::Terran_Medic) * 5 < marines) make(unit, UnitTypes::Terran_Medic);
            else if (have(UnitTypes::Terran_Academy) && all(UnitTypes::Terran_Firebat) * 4 < marines) make(unit, UnitTypes::Terran_Firebat);
            else make(unit, UnitTypes::Terran_Marine);
        }
        else if (type == UnitTypes::Terran_Factory)
        {
            int vultures = all(UnitTypes::Terran_Vulture);
            int tanks = all(UnitTypes::Terran_Siege_Tank_Tank_Mode) + all(UnitTypes::Terran_Siege_Tank_Siege_Mode);
            if (have(UnitTypes::Terran_Armory) && all(UnitTypes::Terran_Goliath) * 2 < vultures + tanks) make(unit, UnitTypes::Terran_Goliath);
            else if (unit->getAddon() && tanks <= vultures) make(unit, UnitTypes::Terran_Siege_Tank_Tank_Mode);
            else make(unit, UnitTypes::Terran_Vulture);
        }
        else if (type == UnitTypes::Terran_Starport)
        {
            make(unit, UnitTypes::Terran_Wraith);
        }
        else if (type == UnitTypes::Protoss_Gateway)
        {
            if (have(UnitTypes::Protoss_Cybernetics_Core) && all(UnitTypes::Protoss_Dragoon) <= all(UnitTypes::Protoss_Zealot))
            {
                make(unit, UnitTypes::Protoss_Dragoon);
            }
            else make(unit, UnitTypes::Protoss_Zealot);
        }
        else if (type == UnitTypes::Protoss_Robotics_Facility)
        {
            if (all(UnitTypes::Protoss_Observer) < 1) make(unit, UnitTypes::Protoss_Observer);
            else if (all(UnitTypes::Protoss_Reaver) < 3) make(unit, UnitTypes::Protoss_Reaver);
        }
        else if (type == UnitTypes::Protoss_Stargate && have(UnitTypes::Protoss_Fleet_Beacon))
        {
            make(unit, UnitTypes::Protoss_Carrier);
        }
        else if (type == UnitTypes::Zerg_Larva)
        {
            int drones = all(UnitTypes::Zerg_Drone);
            if (drones < workerTarget && (drones < 12 || army * 2 >= drones - 8))
            {
                if (make(unit, UnitTypes::Zerg_Drone)) workers++;
            }
            else if (have(UnitTypes::Zerg_Spire) && all(UnitTypes::Zerg_Mutalisk) * 3 < army)
            {
                make(unit, UnitTypes::Zerg_Mutalisk);
            }
            else if (have(UnitTypes::Zerg_Hydralisk_Den) && all(UnitTypes::Zerg_Hydralisk) < 2 * all(UnitTypes::Zerg_Zergling)
                     && self->gas() - reservedGas >= 25)
            {
                make(unit, UnitTypes::Zerg_Hydralisk);
            }
            else if (have(UnitTypes::Zerg_Spawning_Pool))
            {
                make(unit, UnitTypes::Zerg_Zergling);
            }
        }
    }
}

void ComputerAI::manageWorkers()
{
    auto self = Broodwar->self();
    int gasWorkers = 0, refineries = 0;
    for (auto unit : self->getUnits())
    {
        if (unit->getType().isRefinery() && unit->isCompleted()) refineries++;
        if (unit->getType().isWorker() && (unit->isGatheringGas() || unit->isCarryingGas())) gasWorkers++;
    }

    auto threats = threatsAtHome();
    int defenders = 0;
    for (auto unit : self->getUnits())
    {
        if (isArmy(unit->getType()) && unit->isCompleted() && !wave.contains(unit)) defenders++;
    }

    for (auto unit : self->getUnits())
    {
        if (!unit->getType().isWorker() || !unit->isCompleted() || builders.count(unit)) continue;

        // Workers help against an early attack on the base, as the computer's do
        if (!threats.empty() && defenders < 3)
        {
            auto enemy = unit->getClosestUnit(IsEnemy && !IsFlying && IsVisible, 256);
            if (enemy && !unit->isAttacking() && enemy->getDistance(home) < 500)
            {
                unit->attack(enemy);
                continue;
            }
        }
        else if (unit->getOrder() == Orders::AttackUnit)
        {
            unit->stop();
        }

        if (gasWorkers < 3 * refineries && unit->isGatheringMinerals() && !unit->isCarryingMinerals())
        {
            auto refinery = unit->getClosestUnit(IsRefinery && IsOwned && IsCompleted);
            if (refinery && unit->gather(refinery))
            {
                gasWorkers++;
                continue;
            }
        }
        if (unit->isIdle())
        {
            auto depot = unit->getClosestUnit(IsResourceDepot && IsOwned && IsCompleted);
            auto mineral = depot ? depot->getClosestUnit(IsMineralField, 320) : unit->getClosestUnit(IsMineralField);
            if (mineral) unit->gather(mineral);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Army

Unitset ComputerAI::threatsAtHome() const
{
    Unitset threats;
    for (auto enemy : Broodwar->enemy()->getUnits())
    {
        if (!enemy->isVisible() || !enemy->isDetected()) continue;
        if (enemy->getType().isBuilding() && !enemy->getType().canAttack()) continue;
        if (enemy->getType() == UnitTypes::Zerg_Overlord || enemy->getType() == UnitTypes::Protoss_Observer) continue;
        if (enemy->getClosestUnit(IsOwned && (IsBuilding || IsWorker), 400)) threats.insert(enemy);
    }
    return threats;
}

std::vector<Position> ComputerAI::searchTargets() const
{
    std::vector<Position> targets;
    for (auto start : Broodwar->getStartLocations())
    {
        if (start != homeTile) targets.push_back(Position(start) + Position(64, 48));
    }
    std::sort(targets.begin(), targets.end(), [&](Position a, Position b) {
        return home.getApproxDistance(a) < home.getApproxDistance(b);
    });
    for (auto &base : bases) targets.push_back(base.center);
    return targets;
}

// The known enemy building nearest home; otherwise the next place the enemy might be
Position ComputerAI::attackTarget()
{
    Position best = Positions::None;
    int bestDistance = INT_MAX;
    for (auto &[building, position] : enemyBuildings)
    {
        int distance = home.getApproxDistance(position);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = position;
        }
    }
    if (best.isValid()) return best;

    auto targets = searchTargets();
    for (int i = 0; i < (int) targets.size(); i++)
    {
        if (searched.count(i)) continue;
        if (Broodwar->isVisible(TilePosition(targets[i])))
        {
            searched.insert(i);
            continue;
        }
        return targets[i];
    }
    searched.clear();
    return targets.empty() ? home : targets[0];
}

void ComputerAI::manageArmy()
{
    auto self = Broodwar->self();
    int frame = Broodwar->getFrameCount();

    Unitset home_army;
    for (auto unit : self->getUnits())
    {
        if (isArmy(unit->getType()) && unit->isCompleted() && !wave.contains(unit)) home_army.insert(unit);
    }

    // Attack waves: each one bigger, launched a few minutes after the last one ended
    if (wave.empty() || (int) wave.size() * 4 <= waveLaunchSize)
    {
        if (!wave.empty())
        {
            // The wave is spent: what's left comes home and joins the next
            for (auto unit : wave) unit->move(rally);
            home_army.insert(wave.begin(), wave.end());
            wave.clear();
            nextWaveFrame = frame + 3000;
        }
        int needed = std::min(6 + 4 * waveNumber, 40);
        bool maxed = self->supplyUsed() >= 380;
        bool waitedLong = frame > nextWaveFrame + 3600 && (int) home_army.size() >= needed / 2;
        if (frame >= nextWaveFrame && ((int) home_army.size() >= needed || maxed || waitedLong))
        {
            wave = home_army;
            home_army.clear();
            waveLaunchSize = (int) wave.size();
            waveNumber++;
            waveTarget = Positions::None;
        }
    }

    if (!wave.empty())
    {
        Position target = attackTarget();
        bool retarget = target != waveTarget;
        waveTarget = target;
        for (auto unit : wave)
        {
            if (retarget || unit->isIdle()) unit->attack(target);
        }
    }

    // The rest defend the base, or wait at the rally point
    auto threats = threatsAtHome();
    for (auto unit : home_army)
    {
        if (!threats.empty())
        {
            if (unit->isIdle() || unit->getOrder() == Orders::Move) unit->attack(threats.getPosition());
        }
        else if (unit->isIdle() && unit->getDistance(rally) > 192)
        {
            unit->attack(rally);
        }
    }

    // The little micro the computer does: siege tanks near enemies, stim marines in a fight
    for (auto unit : self->getUnits())
    {
        auto type = unit->getType();
        if (type == UnitTypes::Terran_Siege_Tank_Tank_Mode && self->hasResearched(TechTypes::Tank_Siege_Mode)
            && unit->getClosestUnit(IsEnemy && !IsFlying && IsVisible, 384))
        {
            unit->siege();
        }
        else if (type == UnitTypes::Terran_Siege_Tank_Siege_Mode
                 && !unit->getClosestUnit(IsEnemy && !IsFlying && IsVisible, 448) && frame % 96 == 0)
        {
            unit->unsiege();
        }
        else if ((type == UnitTypes::Terran_Marine || type == UnitTypes::Terran_Firebat)
                 && self->hasResearched(TechTypes::Stim_Packs) && !unit->isStimmed() && unit->getHitPoints() > 30
                 && unit->getClosestUnit(IsEnemy && IsVisible && !IsBuilding, 192))
        {
            unit->useTech(TechTypes::Stim_Packs);
        }
    }
}

#pragma once

#include <BWAPI.h>

#include <climits>
#include <map>
#include <set>
#include <vector>

// A Zerg bot written by Muse Spark under the genai rules (C++, BWAPI 4.4, Zerg only, no human-written bot code read;
// ClaudeOpus55 used as a structural template only). Strategy by Muse Spark, code by Muse Spark: safe openings per
// matchup (overpool vs Zerg, 12-hatch vs Protoss/Terran), hydralisk/lurker mid-game with a mutalisk harass option,
// hive into ultralisk/defiler late, sunken defence, overlord scouting, and per-unit micro.
class SparkZerg : public BWAPI::AIModule
{
public:
    void onStart() override;
    void onFrame() override;
    void onUnitDestroy(BWAPI::Unit unit) override;

private:
    struct PendingBuild
    {
        BWAPI::UnitType type;
        BWAPI::TilePosition tile;
        int frame;
    };

    struct Base
    {
        BWAPI::Position center;      // average position of its minerals
        BWAPI::TilePosition depot;   // where the hatchery goes
        BWAPI::Unit geyser = nullptr;
    };

    // Map
    BWAPI::Position home;
    BWAPI::Position rally;
    BWAPI::Position mineralCenter;
    std::vector<Base> bases;          // resource clusters, nearest to home first
    const Base *natural = nullptr;
    BWAPI::Position naturalFront = BWAPI::Positions::Invalid;  // in front of the natural hatchery, away from its minerals
    int rushDistance = INT_MAX;      // ground distance in tiles to the nearest enemy start
    void analyseMap();

    // Units
    BWAPI::Unit mainHatchery = nullptr;
    BWAPI::Unit scoutOverlord = nullptr;
    std::vector<BWAPI::Unit> scoutLings;
    bool scoutingDone = false;
    std::map<BWAPI::Unit, PendingBuild> builders;  // drones currently morphing into buildings, by drone

    // What we know about the enemy
    BWAPI::Race enemyRace = BWAPI::Races::Unknown;
    std::vector<BWAPI::TilePosition> scoutTargets;
    BWAPI::Position enemyBase = BWAPI::Positions::Unknown;
    std::map<int, std::pair<BWAPI::UnitType, BWAPI::Position>> enemyBuildings;  // by unit ID

    // Enemy army units seen recently (by unit ID): type, health when last seen, frame last seen
    struct SeenUnit
    {
        BWAPI::UnitType type;
        int health;
        int frame;
    };
    std::map<int, SeenUnit> enemyArmy;

    // Scouting conclusions
    bool earlyPoolSeen = false;       // spawning pool or zerglings very early: a rush is coming
    bool gatewayRushSeen = false;     // two early gateways or fast zealots
    bool barracksRushSeen = false;    // two early barracks
    bool bunkerRushSeen = false;      // a bunker near our bases
    bool workerRushSeen = false;      // enemy workers attacking ours
    bool proxySeen = false;           // enemy offensive building (pylon/cannon/bunker) near our main
    BWAPI::Position proxyPosition = BWAPI::Positions::Invalid;
    bool fastExpandSeen = false;      // enemy took its natural early
    bool airTechSeen = false;         // stargate, starport or spire: anti-air needed
    bool airArmySeen = false;         // enemy air units: spores and hydras
    bool cloakSeen = false;           // dark templar, wraiths or burrowed lurkers: detection needed
    bool mechSeen = false;            // many factories, tanks or vultures
    bool templarTechSeen = false;     // templar archives: storms coming
    bool mutaThreatSeen = false;      // enemy spire with mutalisks likely: spores now
    int enemyStaticDefense = 0;       // cannons, turrets, bunkers, sunkens, spores seen at the enemy base
    int enemyAntiAir = 0;             // enemy anti-air units and static defence seen

    // Our plans, chosen from scouting
    bool goMutalisks = false;         // mutalisk harass: decided once, when enemy anti-air looks thin
    bool mutaDecisionMade = false;
    bool goLurkers = true;
    bool hiveTech = false;            // on the way to hive / hive done

    // The opening: buildings in order, each from a supply count, chosen by the enemy's race
    struct Step
    {
        int supply;
        BWAPI::UnitType type;
        bool atNatural = false;  // placed at the natural instead of the main
    };
    std::vector<Step> buildOrder;
    size_t buildOrderStep = 0;
    void chooseBuildOrder();
    // Holding an early rush: lings and sunkens before drones, gas or the natural, army waits at home
    bool rushDefense() const;    const Base *nextBase() const;  // nearest cluster without one of our hatcheries

    // Army
    bool attacking = false;
    std::map<BWAPI::Unit, int> aimedDamage;  // damage our units' next shots will do to each aimed-at enemy
    int lastRetreatFrame = -10000;
    int gatherStart = -1;  // when the attacking army last began gathering before contact

    // Economy
    void findBases();
    void manageDrones();
    void manageOverlords();
    void trainUnits();
    void buildStructures();
    void manageTech();
    bool build(BWAPI::UnitType type, BWAPI::TilePosition near, bool atNatural = false);
    bool buildExtractor(BWAPI::Unit geyserUnit);
    int count(BWAPI::UnitType type, bool includePending = true) const;
    int pendingCount(BWAPI::UnitType type) const;
    int larvaeAvailable() const;
    int reservedMinerals() const;
    int reservedGas() const;
    BWAPI::Unit chooseBuilder(BWAPI::Position near);
    BWAPI::TilePosition findBuildSpot(BWAPI::UnitType type, BWAPI::TilePosition near, bool checkExplored = true) const;
    bool placeableIgnoringUnits(BWAPI::TilePosition tile, BWAPI::UnitType type, bool checkExplored) const;
    BWAPI::Unit geyserNeedingExtractor() const;
    BWAPI::Unit baseNeedingDrone() const;
    int wantedDrones() const;
    std::vector<BWAPI::Unit> hatcheries(bool completedOnly) const;
    bool needSunken(BWAPI::Position at) const;
    bool needSpore(BWAPI::Position at) const;
    void defendMineralLines();

    // Information and army
    void scoutEnemy();
    void trackEnemy();
    void controlArmy();
    // With a leash, only enemies within `leash` of `anchor` (or already in weapon range) are taken on, and a unit beyond
    // it with nothing in range walks back to the anchor
    void fight(BWAPI::Unit unit, BWAPI::Position goal, BWAPI::Position anchor = BWAPI::Positions::None, int leash = 0);
    void controlMutalisks(const BWAPI::Unitset &army);
    void controlLurkers(const BWAPI::Unitset &army);
    void controlDefilers(const BWAPI::Unitset &army);
    void controlOverlords(const BWAPI::Unitset &army);
    void controlScourge(const BWAPI::Unitset &army);
    BWAPI::Unitset threatsNearHome() const;
    static double strength(BWAPI::Unit unit);
    static void addToGroup(BWAPI::UnitType type, int health, double &durability, double &dps);
    // Fighting power of a group: (total durability) x (total damage per frame), which values concentration
    static double groupStrength(double durability, double dps) { return durability * dps; }
    int armySupply() const;
};

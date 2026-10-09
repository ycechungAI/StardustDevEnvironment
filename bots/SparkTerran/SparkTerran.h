#pragma once

#include <BWAPI.h>

#include <climits>
#include <map>
#include <set>
#include <vector>

// SparkTerran: a Terran bot written by Muse Spark under StarSkirmish-style rules (C++, BWAPI 4.4.0, Terran, no
// human-written bot code read). It picks a standard macro opening per matchup (2-rax pressure vs Zerg, 1-rax
// wall-in fast expand vs Protoss, defensive tanks vs Terran), then plays marines and medics into siege tanks and
// science vessels, expanding behind its army and fighting only when it judges the fight favourable.
class SparkTerran : public BWAPI::AIModule
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
        BWAPI::TilePosition depot;   // where its command center goes
        BWAPI::Unit geyser = nullptr;
    };

    // The opening: buildings in order, each from a supply count, chosen by the enemy's race
    struct Step
    {
        int supply;
        BWAPI::UnitType type;
        bool atNatural = false;  // placed at the natural (the expansion command center) instead of the main
        bool atRamp = false;     // placed at the ramp top (wall-in pieces, bunkers) instead of the main
    };

    // Enemy army units seen recently (by unit ID): type, health when last seen, frame and position last seen
    struct SeenUnit
    {
        BWAPI::UnitType type;
        int health;
        int frame;
        BWAPI::Position pos;
    };

    // The game plan, picked from the enemy's race
    enum class Plan
    {
        RaxPressure,  // vs Zerg: two barracks of marines, pressure early, factory behind it
        FEWall,       // vs Protoss (and unknown): one barracks, wall the ramp, fast expand, then tanks
        Defensive,    // vs Terran: bunker and tanks at home, expand when safe
    };
    Plan plan = Plan::FEWall;

    // Map
    BWAPI::Position home;
    BWAPI::Position rally;
    BWAPI::Position rampTop;
    BWAPI::Position mineralCenter;
    std::vector<Base> bases;          // resource clusters, nearest to home first
    const Base *natural = nullptr;
    int rushDistance = INT_MAX;       // ground distance in tiles to the nearest enemy start
    void analyseMap();

    // Units
    BWAPI::Unit mainCC = nullptr;
    BWAPI::Unit scout = nullptr;
    bool scoutDone = false;
    std::map<BWAPI::Unit, PendingBuild> builders;
    std::map<BWAPI::Unit, BWAPI::UnitType> pendingAddons;  // building -> addon being built

    // What we know about the enemy
    BWAPI::Race enemyRace = BWAPI::Races::Unknown;
    std::vector<BWAPI::TilePosition> scoutTargets;
    BWAPI::Position enemyBase = BWAPI::Positions::Unknown;
    std::map<int, std::pair<BWAPI::UnitType, BWAPI::Position>> enemyBuildings;  // by unit ID
    std::map<int, SeenUnit> enemyArmy;

    // Scouting conclusions
    bool rushSeen = false;        // early pool, early rax, proxy buildings or worker rush
    bool proxySeen = false;       // enemy buildings near our base
    bool feSeen = false;          // the enemy took a fast natural
    bool airSeen = false;         // spire, stargate or starport with flying attackers on the way
    bool mutasSeen = false;
    bool wraithsSeen = false;
    bool cloakSeen = false;       // lurkers, dark templar or wraiths: detection becomes urgent
    bool dtSeen = false;
    bool lurkersSeen = false;
    bool tanksSeen = false;       // enemy siege tanks
    bool reaversSeen = false;
    bool carriersSeen = false;
    bool mechSeen = false;        // enemy going vultures/tanks/goliaths (TvT)
    bool dropsSeen = false;       // shuttle or dropship

    // The opening
    std::vector<Step> buildOrder;
    size_t buildOrderStep = 0;
    bool zergSwitchDone = false;  // unknown race turned out to be Zerg: the second rax goes down early
    void chooseBuildOrder();

    // Army
    bool attacking = false;
    int wave = 0;
    int lastRetreatFrame = -10000;
    BWAPI::Position attackTarget = BWAPI::Positions::Unknown;
    std::map<BWAPI::Unit, int> aimedDamage;  // damage already assigned to each enemy this frame

    // Economy
    void findBases();
    BWAPI::Position findRampTop() const;
    void manageWorkers();
    void trainUnits();
    void buildStructures();
    void researchUpgrades();
    void tryExpand();
    bool build(BWAPI::UnitType type, BWAPI::TilePosition near);
    bool buildAddon(BWAPI::Unit building, BWAPI::UnitType addon);
    int count(BWAPI::UnitType type, bool includePending = true) const;
    int pendingCount(BWAPI::UnitType type) const;
    int reservedMinerals() const;
    int reservedGas() const;
    BWAPI::Unit chooseBuilder(BWAPI::Position near);
    BWAPI::TilePosition depotSpot();
    BWAPI::TilePosition rampSpot(BWAPI::UnitType type);
    BWAPI::TilePosition findBuildSpot(BWAPI::UnitType type, BWAPI::TilePosition near, bool checkExplored = true) const;
    bool placeable(BWAPI::TilePosition tile, BWAPI::UnitType type, bool checkExplored) const;
    std::vector<BWAPI::Unit> commandCenters(bool completedOnly) const;
    BWAPI::Unit ccNeedingWorkers() const;
    bool refineryWanted() const;

    // Information and army
    void scoutEnemy();
    void trackEnemy();
    void controlArmy();
    void fight(BWAPI::Unit unit, BWAPI::Position goal, BWAPI::Position anchor = BWAPI::Positions::None, int leash = 0);
    void controlMarines(const BWAPI::Unitset &army);
    void controlTanks(const BWAPI::Unitset &army);
    void controlMedics(const BWAPI::Unitset &army);
    void controlVessels(const BWAPI::Unitset &army);
    void controlGoliaths(const BWAPI::Unitset &army);
    void controlAir(const BWAPI::Unitset &army);
    void useComsat();
    void defendBases(const BWAPI::Unitset &army);
    BWAPI::Unitset threatsNear(BWAPI::Position where, int radius) const;
    void defendWorkerRush();
    static double unitPower(BWAPI::Unit unit);
    static double groupPower(const BWAPI::Unitset &units);
    static double seenPower(BWAPI::UnitType type, int health);
    void judgeAttack(const BWAPI::Unitset &army);
    void splitVsLurkers(const BWAPI::Unitset &marines);
};

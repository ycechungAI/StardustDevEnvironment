#pragma once

#include <BWAPI.h>

#include <map>
#include <set>
#include <vector>

// A Protoss bot written by Claude Opus 5.5 under StarSkirmish's rules (C++, BWAPI 4.4, Protoss, no human-written bot
// code; 3 hours of writing and practice games): gateway army of zealots and dragoons that adapts its opening to the
// enemy race, defends at home, takes its natural, and fights as one group only when it judges the fight favourable.
class ClaudeOpus55 : public BWAPI::AIModule
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
        BWAPI::TilePosition depot;   // where its resource depot goes
        BWAPI::Unit geyser = nullptr;
    };

    // Map
    BWAPI::Position home;
    BWAPI::Position rally;
    BWAPI::Position mineralCenter;
    std::vector<Base> bases;          // resource clusters, nearest to home first
    const Base *natural = nullptr;

    // Units
    BWAPI::Unit mainNexus = nullptr;
    BWAPI::Unit scout = nullptr;
    std::map<BWAPI::Unit, PendingBuild> builders;

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

    // Cloaked enemies (dark templar, lurkers, wraiths, mines) seen: detection becomes urgent
    bool cloakSeen = false;
    bool templarArchivesSeen = false;
    bool rushSeen = false;             // early mass gateways/barracks or early zealots/zerglings: hold the expansion

    // Set when the natural is due, so production leaves money for it
    bool expansionDue = false;

    // The opening: buildings in order, each from a supply count, chosen by the enemy's race
    struct Step
    {
        int supply;
        BWAPI::UnitType type;
    };
    std::vector<Step> buildOrder;
    size_t buildOrderStep = 0;
    void chooseBuildOrder();
    const Base *nextBase() const;  // the dark templar tech: the only reason to build a robotics facility

    // Army
    bool attacking = false;
    int wave = 0;
    int lastRetreatFrame = -10000;

    // Economy
    void findBases();
    BWAPI::Position findRampTop() const;
    void manageWorkers();
    void trainUnits();
    void buildStructures();
    bool build(BWAPI::UnitType type, BWAPI::TilePosition near);
    int count(BWAPI::UnitType type, bool includePending = true) const;
    int pendingCount(BWAPI::UnitType type) const;
    int reservedMinerals() const;
    int reservedGas() const;
    BWAPI::Unit chooseBuilder(BWAPI::Position near);
    BWAPI::TilePosition pylonSpot();
    BWAPI::TilePosition findBuildSpot(BWAPI::UnitType type, BWAPI::TilePosition near) const;
    std::vector<BWAPI::Unit> nexuses(bool completedOnly) const;
    BWAPI::Unit nexusNeedingWorkers() const;

    // Information and army
    void scoutEnemy();
    void trackEnemy();
    void controlArmy();
    void fight(BWAPI::Unit unit, BWAPI::Position goal);
    void controlObservers(const BWAPI::Unitset &army);
    BWAPI::Unitset threatsNearHome() const;
    static double strength(BWAPI::Unit unit);
    // Fighting power of a group: (total durability) x (total damage per frame), which values concentration
    static double groupStrength(double durability, double dps) { return durability * dps; }
    static void addToGroup(BWAPI::UnitType type, int health, double &durability, double &dps);
};

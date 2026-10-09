#pragma once

#include <BWAPI.h>

#include <climits>
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
    BWAPI::Position rampTop;
    BWAPI::Position mineralCenter;
    std::vector<Base> bases;          // resource clusters, nearest to home first
    const Base *natural = nullptr;
    BWAPI::Position naturalFront = BWAPI::Positions::Invalid;  // in front of the natural nexus, away from its minerals
    bool cannonOpening = false;       // against Zerg: forge and cannons by the main's minerals before the gateway
    bool hurtNeeded = false;          // this frame's attack needs its damaged units to be decisive
    bool openingCannonsStarted = false;  // the cannon opening's cannons have all been started at least once
    bool forgeExpand = false;         // against Zerg: forge and cannons at the natural, then the nexus there
    int rushDistance = INT_MAX;       // ground distance in tiles to the nearest enemy start
    int naturalEntrances = 0;         // ways into the natural from the enemy's side
    int widestEntrance = 0;           // in tiles
    void analyseMap();

    // Units
    BWAPI::Unit mainNexus = nullptr;
    BWAPI::Unit scout = nullptr;
    int scoutArrivedFrame = -1;       // when the scout reached the enemy main; it circles there for a while
    bool scoutingDone = false;
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
    bool hiddenArmySeen = false;  // dark templar or lurkers: the army waits for an observer before attacking
    bool airSeen = false;              // a spire or mutalisks: cannons in the mineral lines and corsairs
    bool rangedNeeded = false;         // hydralisks, lurkers or mutalisks: zealots are poor, dragoons only
    bool rushSeen = false;             // early mass gateways/barracks or early zealots/zerglings: hold the expansion
    bool barracksRush = false;         // two early barracks: marines are coming, zealots answer them best
    bool stormTech = false;            // mostly marines seen: templar and storm on one base, ahead of dragoon range
    bool heavyTerranSeen = false;      // siege tanks or battlecruisers: against Terran, attack only with a clear edge
    // stormTech with storm not yet researching: the gas is kept for the citadel, the archives and storm
    bool stormPending() const;
    std::pair<int, int> stormCost() const;  // minerals and gas to keep for the next step towards storm
    // A rush seen early and our gateway units not yet a match for it: zealots before probes, gas or the natural, and
    // the army waits by the nexus
    bool rushMode() const;
    // Against a marine rush: the first zealots before gas, the core and more than 16 probes
    bool zealotsFirst() const;
    // No enemy air army seen in the last minute and a half: storm is worth its gas then (the user: two templar if they
    // are all in on ground units, otherwise spend the gas on something else)
    bool groundArmyOnly() const;
    // The enemy can see cloaked units: a detector or an observatory seen, or one of our dark templar was hit
    bool detectionSeen = false;
    // Against Terran or Protoss with no detection seen: dark templar instead of high templar (the user: hide the
    // archives, then dark templar to kill workers unseen, or with the army for its damage when there is gas to spare)
    bool darkTemplarPlan() const;
    std::set<int> darkRaiders;    // dark templar raiding the enemy's mineral lines, by unit ID
    std::set<int> darkAssigned;   // dark templar already sent raiding or to the army
    void controlDarkRaiders();
    BWAPI::TilePosition hiddenSpot() const;  // a powered spot in the main as far as possible from the ramp

    // Set when the natural is due, so production leaves money for it
    bool expansionDue = false;

    // The opening: buildings in order, each from a supply count, chosen by the enemy's race
    struct Step
    {
        int supply;
        BWAPI::UnitType type;
        bool atNatural = false;  // placed at the natural's front (forge, cannons and their pylon) instead of the main
    };
    std::vector<Step> buildOrder;
    size_t buildOrderStep = 0;
    void chooseBuildOrder();
    const Base *nextBase() const;  // the dark templar tech: the only reason to build a robotics facility

    // Army
    bool attacking = false;
    // Damage our units' next shots will do to each enemy they are aiming at, refreshed every frame: dragoons pile onto
    // a target others are already shooting, but not past what kills it
    std::map<BWAPI::Unit, int> aimedDamage;
    // Siege tanks, static defence or a heavy ground army seen: reavers from the robotics facility
    bool reaversWanted = false;
    int wave = 0;
    int lastRetreatFrame = -10000;
    int gatherStart = -1;  // when the attacking army last began gathering before contact

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
    BWAPI::TilePosition findBuildSpot(BWAPI::UnitType type, BWAPI::TilePosition near, bool checkExplored = true,
                                      bool ignoreUnits = false) const;
    bool placeableIgnoringUnits(BWAPI::TilePosition tile, BWAPI::UnitType type, bool checkExplored) const;
    bool secondGatewayWaits(const Step &step) const;
    // The cannon opening's cannons before the gateway: three if an early pool or zerglings are seen in time for them
    // to matter, else two (a third that could not be placed held up the gateway until 3:30)
    int openingCannons() const { return rushSeen && BWAPI::Broodwar->getFrameCount() < 3000 ? 3 : 2; }
    void buildMineralLineCannons();
    BWAPI::UpgradeType nextForgeUpgrade() const;
    bool cannonsNear(BWAPI::Position spot, int wanted);
    std::vector<BWAPI::Unit> nexuses(bool completedOnly) const;
    BWAPI::Unit nexusNeedingWorkers() const;

    // Information and army
    void scoutEnemy();
    void trackEnemy();
    void controlArmy();
    // With a leash, only enemies within `leash` of `anchor` (or already in weapon range) are taken on, and a unit beyond
    // it with nothing in range walks back to the anchor
    void fight(BWAPI::Unit unit, BWAPI::Position goal, BWAPI::Position anchor = BWAPI::Positions::None, int leash = 0);
    void controlObservers(const BWAPI::Unitset &army);
    void controlCorsairs(const BWAPI::Unitset &army);
    // High templar: storm on clumps of enemies, else one stays home and one follows the army, each behind it
    void controlTemplar(const BWAPI::Unitset &army);
    std::vector<std::pair<BWAPI::Position, int>> recentStorms;  // where and when, so two templar don't storm one spot
    int homeTemplar = -1;  // ID of the high templar that stays home with the defenders; the other goes with the army
    BWAPI::Unitset threatsNearHome() const;
    static double strength(BWAPI::Unit unit);
    // Fighting power of a group: (total durability) x (total damage per frame), which values concentration
    static double groupStrength(double durability, double dps) { return durability * dps; }
    void retryOpeningStep(BWAPI::UnitType type);
    static void addToGroup(BWAPI::UnitType type, int health, double &durability, double &dps);
};

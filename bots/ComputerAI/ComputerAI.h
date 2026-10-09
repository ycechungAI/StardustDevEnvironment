#pragma once

#include <BWAPI.h>

#include <map>
#include <set>
#include <vector>

// A stand-in for StarCraft's built-in computer player, which OpenBW doesn't have, to gauge a bot against something of
// about its strength. It plays the way the built-in AI does: a fixed build script for its race, steady workers and
// supply, expansions at set points, and attacks in waves that grow every few minutes, attack-moving at the enemy's
// buildings with no micro beyond sieging tanks and stimming. It plays whichever race it is given.
class ComputerAI : public BWAPI::AIModule
{
public:
    void onStart() override;
    void onFrame() override;
    void onUnitShow(BWAPI::Unit unit) override;
    void onUnitDestroy(BWAPI::Unit unit) override;

private:
    struct Step
    {
        int supply;            // start once this much supply (as shown in game) is in use
        BWAPI::UnitType type;  // building; a resource depot means an expansion
    };

    struct Base
    {
        BWAPI::TilePosition tile;  // where its resource depot goes
        BWAPI::Position center;
    };

    struct Pending
    {
        BWAPI::UnitType type;
        BWAPI::TilePosition tile;
        int frame;
    };

    BWAPI::Race race;
    BWAPI::TilePosition homeTile;
    BWAPI::Position home;
    BWAPI::Position rally;

    std::vector<Step> plan;
    std::vector<std::pair<BWAPI::UnitType, BWAPI::UpgradeType>> upgrades;  // researched at the given building
    std::vector<std::pair<BWAPI::UnitType, BWAPI::TechType>> techs;
    std::vector<Base> bases;

    std::map<BWAPI::Unit, Pending> builders;
    std::map<BWAPI::Unit, BWAPI::Position> enemyBuildings;
    std::set<int> searched;  // indices into searchTargets() already looked at

    BWAPI::Unitset wave;
    int waveNumber = 0;
    int waveLaunchSize = 0;
    int nextWaveFrame = 0;
    BWAPI::Position waveTarget;

    int reservedMinerals = 0;
    int reservedGas = 0;

    void findBases();
    void updateBuilders();
    void followPlan();
    void buildSupply();
    void train();
    void research();
    void manageWorkers();
    void manageArmy();

    bool start(BWAPI::UnitType type);
    bool affordable(BWAPI::UnitType type) const;
    int count(BWAPI::UnitType type) const;
    BWAPI::TilePosition nextExpansion() const;
    BWAPI::TilePosition freeGeyser() const;
    BWAPI::Unit freeWorker(BWAPI::Position near) const;
    std::vector<BWAPI::Position> searchTargets() const;
    BWAPI::Position attackTarget();
    BWAPI::Unitset threatsAtHome() const;

    static bool isArmy(BWAPI::UnitType type);
};

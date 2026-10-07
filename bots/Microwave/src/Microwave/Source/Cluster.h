#pragma once

// Operations boss.
// NOTE Unfinished. Intended to eventually replace CombatCommander.

#include <functional>
#include <BWAPI.h>
#include "UnitData.h"

namespace UAlbertaBot
{
    struct UnitInfo;	// forward declaration

    enum class ClusterStatus
    {
        None          // enemy cluster or not updated yet
        , Advance     // no enemy near, moving forward
		, SafeAdvance // advance on a safe path
        , Attack      // enemy nearby, attacking
        , Regroup     // regrouping (usually retreating)
        , FallBack    // returning to base
    };

    class UnitCluster
    {
		// Time and distance beyond maximum realistic values,
		// so that we can represent "never" and "not anywhere" and do arithmetic on the values
		// without risk of integer overflow.
		int MAX_FRAME = 24 * 60 * 60 * 24;	// 24 hours
		int MAX_DISTANCE = 2 * 32 * 256;	// twice the width of the largest maps in pixels

    public:
        BWAPI::Position center;
        int radius;
        ClusterStatus status;
		int lastAttack;
		int lastRetreat;

        bool air;               // air units
        double speed;           // minimum speed, ignoring static defense, pixels/frame

        size_t count;           // number of units
        int hp;                 // total HP + shields
        double groundDPF;		// DPF = damage per frame
        double airDPF;

        std::string extraText;  // for draw()

        BWAPI::Unitset units;   // not necessarily visible

        UnitCluster();

        void clear();
        void add(const UnitInfo & ui);
        size_t size() const { return count; };
		void setExtraText(const std::string & s) { extraText = s; };

		int timeToTravel(int pixels) const;
		bool includesType(BWAPI::UnitType type) const;

		void draw(BWAPI::Color color, const std::string & label = "") const;
    };

    class Cluster
    {
		BWAPI::Player _self = BWAPI::Broodwar->self();
		BWAPI::Player _enemy = BWAPI::Broodwar->enemy();

		// Time and distance beyond maximum realistic values,
		// so that we can represent "never" and "not anywhere" and do arithmetic on the values
		// without risk of integer overflow.
		int MAX_FRAME = 24 * 60 * 60 * 24;	// 24 hours
		int MAX_DISTANCE = 2 * 32 * 256;	// twice the width of the largest maps in pixels

		// Current frame count, same as Broodwar->getFrameCount().
		int NOW = BWAPI::Broodwar->getFrameCount();

        const int clusterStart = 5 * 32;
        const int clusterRange = 3 * 32;

        std::vector<UnitCluster> enemyClusters;

        int defenderUpdateFrame;
        std::vector<UnitCluster> groundDefenseClusters;
        std::vector<UnitCluster> airDefenseClusters;

        void locateCluster(const std::vector<BWAPI::Position> & points, UnitCluster & cluster);
        void formCluster(const UnitInfo & seed, const UIMap & theUI, BWAPI::Unitset & units, UnitCluster & cluster);
        void clusterUnits(BWAPI::Player player, BWAPI::Unitset & units, std::vector<UnitCluster> & clusters);

        void updateDefenders();

    public:
		Cluster();
        void initialize();

        void cluster(BWAPI::Player player, std::vector<UnitCluster> & clusters);
        void cluster(BWAPI::Player player, const BWAPI::Unitset & units, std::vector<UnitCluster> & clusters);

        void update();

        const std::vector<UnitCluster> & getGroundDefenseClusters();
        const std::vector<UnitCluster> & getAirDefenseClusters();
		const std::vector<UnitCluster> & getDefenseClusters(bool air)
			{ return air ? getAirDefenseClusters() : getGroundDefenseClusters(); };
		const std::vector<UnitCluster> getEnemyClusters() { return enemyClusters; };

        const UnitCluster * getNearestEnemyClusterVs(const BWAPI::Position & pos, bool vsGround, bool vsAir);
		const UnitCluster * getNearestFriendlyClusterVs(const BWAPI::Position & pos, bool vsGround, bool vsAir);
		const UnitCluster * getNearestEnemyClusterSuchThat(
			const BWAPI::Position & pos,
			std::function<bool(const UnitCluster & cluster)> & p);

        void drawClusters() const;

		// yay for singletons!
		static Cluster & Instance();
    };
}
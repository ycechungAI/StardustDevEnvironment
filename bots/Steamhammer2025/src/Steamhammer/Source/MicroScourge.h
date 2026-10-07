#pragma once

#include <queue>

#include "MicroManager.h"

namespace UAlbertaBot
{
	struct UnitInfo;			// forward declaration

    class MicroScourge : public MicroManager
    {
	private:

		// Created for each enemy air cluster when it's time for scourge decisions.
		// Used for that one frame, then discarded. Clusters are ephemeral.
		struct TargetCluster
		{
			const UnitCluster & cluster;
			int defense;
			int priority;

			TargetCluster(const UnitCluster & c, int d)
				: cluster(c)
				, defense(d)
			{}
		};

		int clusterDefense(const UnitCluster & c);
		void findTargetClusters();

		// --

		int _lastAssignmentFrame;

		// The scourge target assignments.
		// It usually takes more than one scourge per target. Organize scourge into teams.
		// This persists from frame to frame and needs to be updated when anything changes.
		// NOTE A scourge that's mistakenly assigned to more than one team will cause trouble.
		BWAPI::Unitset _unassignedScourge;
		BWAPI::Unitset _assignedScourge;
		std::map<BWAPI::Unit, BWAPI::Unitset> _scourgeTeams;		// target unit ==> scourge team

		bool anyDeadTargets() const;
		bool anyDeadScourge() const;
		bool anyNewScourge() const;

		void resetScourge();

		int getTargetPriority(const UnitInfo & ui) const;
		void prioritizeTargets(std::priority_queue<std::pair<BWAPI::Unit, int>> & priorities);
		void assignTeamsToTargets(std::priority_queue<std::pair<BWAPI::Unit, int>> & priorities);
		void assignScourge();

		void microScourge();

    public:
        MicroScourge();

		void update();

        void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);
        void assignTargets(const BWAPI::Unitset & rangedUnits, const BWAPI::Unitset & targets);

        static int getAttackPriority(BWAPI::UnitType targetType);
        BWAPI::Unit getTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets);
    };
}
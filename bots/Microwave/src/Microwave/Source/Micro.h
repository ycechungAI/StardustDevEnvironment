#pragma once

#include <Common.h>
#include <BWAPI.h>

namespace UAlbertaBot
{

namespace Micro
{
	// Pathing
	template<typename F>
	BWAPI::Position findPointOnPath(BWEB::Path& path, F &&pred) {
		BWAPI::TilePosition last = BWAPI::TilePositions::Invalid;

		// For each TilePosition on the path
		for (auto &pos : path.getTiles()) {

			// If last wasn't valid, this is likely the first TilePosition
			if (!last.isValid()) {
				last = pos;
				continue;
			}

			// As long as last doesn't equal pos
			while (last != pos) {
				if (pred(BWAPI::Position(last) + BWAPI::Position(16, 16)))
					return BWAPI::Position(last) + BWAPI::Position(16, 16);

				// Increment or decrement x/y based on where we need to go
				last.x != pos.x ? (last.x > pos.x ? last.x-- : last.x++) : 0;
				last.y != pos.y ? (last.y > pos.y ? last.y-- : last.y++) : 0;
			}
			last = pos;
		}
		return BWAPI::Positions::Invalid;
	}

	void SmartMovePath(BWAPI::Unit attacker, const BWAPI::Position & targetPosition);
	void SmartMovePathBWEB(BWAPI::Unit attacker, const BWAPI::Position & targetPosition);
	void SmartMovePathBWEM(BWAPI::Unit attacker, const BWAPI::Position & targetPosition);
	BWAPI::Position moveToNextWaypoint(BWAPI::Unit unit, BWAPI::Position targetPathPosition, std::deque<const BWEM::ChokePoint*> waypoints);
	void updateMoveWaypoints(BWAPI::Unit unit, BWAPI::Position targetPathPosition, BWAPI::Position currentlyMovingTowards, std::deque<const BWEM::ChokePoint*> waypoints);
	std::deque<const BWEM::ChokePoint *> generateChokePath(BWAPI::Unit unit, BWAPI::Position end);
	BWEM::CPPath getChokePath(BWAPI::Unit unit, BWAPI::Position end);

	BWAPI::Position fleeTo(BWAPI::Unit unit, const BWAPI::Position & danger, int distance);
	void fleePosition(BWAPI::Unit unit, const BWAPI::Position & danger, int distance = 8 * 32);
	void fleeEnemy(BWAPI::Unit unit, BWAPI::Unit enemy, int distance = 8 * 32);

	//Micro
	bool SmartStop(BWAPI::Unit unit);
	void SmartHoldPosition(BWAPI::Unit unit);
    void SmartAttackUnit(BWAPI::Unit attacker, BWAPI::Unit target);
    void SmartAttackMove(BWAPI::Unit attacker, const BWAPI::Position & targetPosition);
	void SmartLingSpread(BWAPI::Unit attacker, BWAPI::Unit target);
    bool SmartMove(BWAPI::Unit attacker, const BWAPI::Position & targetPosition);
	bool SmartMoveSafely(BWAPI::Unit unit, const BWAPI::Position & destination);
	void SmartRightClick(BWAPI::Unit unit, BWAPI::Unit target);
	void MineMinerals(BWAPI::Unit unit, BWAPI::Unit mineralPatch);
    void SmartLaySpiderMine(BWAPI::Unit unit, BWAPI::Position pos);
    void SmartRepair(BWAPI::Unit unit, BWAPI::Unit target);
	bool Scan(const BWAPI::Position & targetPosition);
	bool Stim(BWAPI::Unit unit);
	bool MergeArchon(BWAPI::Unit templar1, BWAPI::Unit templar2);
	bool LarvaTrick(const BWAPI::Unitset & larvas);
	bool SmartReturnCargo(BWAPI::Unit attacker);
	bool SmartBurrow(BWAPI::Unit unit);
	bool SmartUnburrow(BWAPI::Unit unit);
	void SmartKiteTarget(BWAPI::Unit rangedUnit, BWAPI::Unit target);
    void MutaDanceTarget(BWAPI::Unit muta, BWAPI::Unit target);
	void CatchAndAttackUnit(BWAPI::Unit attacker, BWAPI::Unit target);
	BWAPI::Position predictUnitPosition(BWAPI::Unit unit, int frames);

	//From Arrak
	void SmartUseTech(BWAPI::Unit, BWAPI::TechType tech, const BWAPI::Position & = BWAPI::Position(0,0));
	void SmartUseTech(BWAPI::Unit, BWAPI::TechType tech, const BWAPI::Unit & = NULL);
	void SmartTravel(BWAPI::Unit, const BWAPI::Position &, BWAPI::Position = BWAPI::Position(0,0), bool path = false);
	BWAPI::Position GetProjectedPosition(BWAPI::Unit unit, BWAPI::Unit target);
	bool CheckSpecialCases(BWAPI::Unit unit);
	bool AvoidAllies(BWAPI::Unit unit, int radius);
	bool SuperSmartMove(BWAPI::Unit unit, BWAPI::Position pos);
	bool StormDodge(BWAPI::Unit unit);
	bool ScarabDodge(BWAPI::Unit unit);
	bool checkMovable(BWAPI::Position pos, int = 4);
    void Rotate(double &x, double &y, double angle);
    void Normalize(double &x, double &y);
	BWAPI::Position GetKiteVector(BWAPI::Unit unit, BWAPI::Unit target);
	BWAPI::Position GetTangentVector(BWAPI::Unit, BWAPI::Unit, BWAPI::Position);

    void drawAPM(int x, int y);
};

}
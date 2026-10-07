#pragma once

#include <vector>

#include "Base.h"

namespace UAlbertaBot
{
	class The;

	class PotentialBase
	{
	public:
		int left;
		int right;
		int top;
		int bottom;
		BWAPI::TilePosition startTile;

		PotentialBase(int l, int r, int t, int b, BWAPI::TilePosition tile)
			: left(l)
			, right(r)
			, top(t)
			, bottom(b)
			, startTile(tile)
		{
		}
	};

	class Bases
	{
	private:
		std::vector<Base *> bases;
		std::vector<Base *> startingBases;			// starting locations
		Base * startingBase;                        // always set, not always owned by us
		Base * mainBase;							// always set, owned by us iff we own any base
		Base * naturalBase;                         // not always set - some maps have no natural
		Base * frontBase;							// whichever of main and natural is in front
		Base * enemyStartingBase;					// set when and if we find out
		Base * enemyStartingBaseBestGuess;			// estimate based on unit sightings, may be null
		BWAPI::Unitset smallMinerals;		        // patches too small to be worth mining

		int doomedBaseCancelTime;					// the last time buildings were canceled at a doomed base
		const int doomedBaseCancelDelay = 90 * 24;	// don't cancel buildings at doomed bases too often

		bool islandStart;
		bool islandBases;
		std::map<BWAPI::Unit, Base *> baseBlockers;	// neutral building to destroy -> base it belongs to

		// Debug data structures. Not used for any other purpose, can be deleted with their uses.
		std::vector<BWAPI::Unitset> nonbases;
		std::vector<PotentialBase> potentialBases;

		Bases();

		void setBaseIDs();
		bool checkIslandStart() const;
		bool checkIslandBases() const;
		void rememberBaseBlockers();

		void removeUsedResources(BWAPI::Unitset & resources, const Base * base) const;
		void countResources(BWAPI::Unit resource, int & minerals, int & gas) const;
		BWAPI::TilePosition findBasePosition(BWAPI::Unitset resources);
		int baseLocationScore(const BWAPI::TilePosition & tile, BWAPI::Unitset resources) const;
		int tilesBetweenBoxes
		(const BWAPI::TilePosition & topLeftA
			, const BWAPI::TilePosition & bottomRightA
			, const BWAPI::TilePosition & topLeftB
			, const BWAPI::TilePosition & bottomRightB) const;

		bool closeEnough(BWAPI::TilePosition a, BWAPI::TilePosition b) const;

		bool inferEnemyStartFromOverlord();
		bool inferEnemyStartFromCreep();
		void inferEnemyAbsenceFromLackOfCreep();
		void updateEnemyStart();
		void updateBaseOwners();
		void changeMainBase();
		void setFrontBase();
		void updateSmallMinerals();

	public:
		const int BaseRadius = 24 * 32;		// a position this close, in the same zone, is "in the base"

		void initialize();
		void update();
		void checkBuildingPosition(const BWAPI::TilePosition & desired, const BWAPI::TilePosition & actual);

		void drawBaseInfo() const;

		Base * myStart() const { return startingBase; };		// always set
		Base * myMain() const { return mainBase; }				// always set
		Base * myNatural() const { return naturalBase; };		// may be null
		Base * myFront() const;									// may be null
		BWAPI::Position front() const;
		BWAPI::TilePosition frontTile() const;
		bool isIslandStart() const { return islandStart; };
		bool hasIslandBases() const { return islandBases; };

		Base * enemyStart() const { return enemyStartingBase; };			// null if unknown
		Base * enemyStartBestGuess() const { return enemyStartingBaseBestGuess; };	// null if none

		bool connectedToStart(const BWAPI::Position & pos) const;
		bool connectedToStart(const BWAPI::TilePosition & tile) const;

		Base * getBaseAtTilePosition(BWAPI::TilePosition pos);
		const std::vector<Base *> & getAll() const { return bases; };
		const std::vector<Base *> & getStarting() const { return startingBases; };
		const BWAPI::Unitset & getSmallMinerals() const { return smallMinerals; };

		int baseCount(BWAPI::Player player) const;
		int completedBaseCount(BWAPI::Player player) const;
		int freeLandBaseCount() const;
		int remainingMineralPatchCount() const;
		int geyserCount(BWAPI::Player player) const;
		void gasCounts(int & nRefineries, int & nFreeGeysers) const;

		void clearNeutral(BWAPI::Unit unit);

		Base * findBaseByPosition(const BWAPI::TilePosition & tile) const;
		Base * findBaseByPosition(const BWAPI::Position & pos) const;

		static Bases & Instance();
	};
}

#pragma once

#include "Common.h"
#include "UnitData.h"

namespace UAlbertaBot
{
struct BaseInfo
{
	const BWEM::Base *	location;
	BWAPI::Unit			resourceDepot;

	BaseInfo(const BWEM::Base * loc, BWAPI::Unit depot)
		: location(loc)
		, resourceDepot(depot)
	{
	}
};

struct BaseStatus
{
public:
	BWAPI::Unit  	resourceDepot;		// hatchery, etc.; valid iff depotSeen
	BWAPI::Player	owner;              // self, enemy, neutral
	bool			reserved;			// if this is our planned expansion

	// The resourceDepot pointer is set for a base if the depot has been seen.
	// It is possible to infer a base location without seeing the depot.

	BaseStatus()
		: resourceDepot(nullptr)
		, owner(BWAPI::Broodwar->neutral())
		, reserved(false)
	{
	}

	BaseStatus(BWAPI::Unit depot, BWAPI::Player player)
		: resourceDepot(depot)
		, owner(player)
		, reserved(false)
	{
	}

	BaseStatus(BWAPI::Unit depot, BWAPI::Player player, bool res)
		: resourceDepot(depot)
		, owner(player)
		, reserved(res)
	{
	}
};

class InformationManager 
{
	BWAPI::Player _self;
    BWAPI::Player _enemy;
	BWAPI::Playerset _allies;

	bool			_enemyProxy;
	bool			_weHaveCombatUnits;
	bool			_enemyHasCombatUnits;
	bool			_enemyHasStaticAntiAir;
	bool			_enemyHasAirUnits;
	bool			_enemyHasAntiAir;
	bool			_enemyHasAirTech;
	bool			_enemyHasCloakTech;
	bool			_enemyHasMobileCloakTech;
	bool			_enemyHasOverlordHunters;
	bool			_enemyHasLiftedBuildings;
	bool			_enemyHasVultures;
	bool            _enemyCloakedUnitsSeen;
	bool			_enemyHasStaticDetection;
	bool			_enemyHasMobileDetection;
	bool			_enemyHasSiegeMode;
	int				_lastUpdateFrame;

    std::map<BWAPI::Player, UnitData>						_unitData;
	std::map<BWAPI::Player, const BWEM::Base*>				_mainBaseLocations;
	const BWEM::Base *										_myNaturalBaseLocation;  // whether taken yet or not
	const BWEM::Base *										_myThirdBaseLocation;    // whether taken yet or not
	std::map<BWAPI::Player, std::set<const BWEM::Area *> >  _occupiedRegions;        // contains any building
	std::map<const BWEM::Base *, BaseStatus>				_theBases;
	BWAPI::Unitset											_staticDefense;
	BWAPI::Unitset											_immobileDefense;
	
	BWAPI::Position											_mainPosition = BWAPI::Positions::Invalid;
	BWAPI::Position											_naturalPosition = BWAPI::Positions::Invalid;
	BWAPI::TilePosition										_mainTile = BWAPI::TilePositions::Invalid;
	BWAPI::TilePosition										_naturalTile = BWAPI::TilePositions::Invalid;
	const BWEM::Area *										_naturalArea = nullptr;
	const BWEM::Area *										_mainArea = nullptr;
	const BWEM::ChokePoint *								_naturalChoke = nullptr;
	const BWEM::ChokePoint *								_mainChoke = nullptr;

	int airThreatGrid[256][256] = {}; // Enemy air influence map, build tile resolution

	InformationManager();

	void					findStartMain();
	void                    initializeRegionInformation();
	void					initializeNaturalBase();
	//void					initializeNaturalChokepoint();
	void					initializeThirdBase();

	void					baseInferred(const BWEM::Base* base);
	void					baseFound(BWAPI::Unit depot);
	void					baseFound(const BWEM::Base* base, BWAPI::Unit depot);
	void					baseLost(BWAPI::TilePosition basePosition);
	void					baseLost(const BWEM::Base* base);
	void					maybeAddBase(BWAPI::Unit unit);
	bool					closeEnough(BWAPI::TilePosition a, BWAPI::TilePosition b);
	void					chooseNewMainBase();
	void					maybeAddStaticDefense(BWAPI::Unit unit);

	void                    updateUnit(BWAPI::Unit unit);
    void                    updateUnitInfo();
    void                    updateBaseLocationInfo();
	void					updateTheBases();
    void                    updateOccupiedRegions(const BWEM::Area * region, BWAPI::Player player);
	void					updateGoneFromLastPosition();
	void					updateBulletInfo();

	void					updateGrids();

	bool                    isValidUnit(BWAPI::Unit unit);

public:
	BWAPI::Unitset			scarabs;
	BWAPI::Unitset			nukes;
	BWAPI::Unitset			hazards;
	BWAPI::Bulletset		storms;
	BWAPI::Bulletset		emps; //unused
	BWAPI::Bulletset		ensnares; //unused

    void                    update();

    // event driven stuff
	void					onUnitShow(BWAPI::Unit unit)        { updateUnit(unit); maybeAddBase(unit); }
    void					onUnitHide(BWAPI::Unit unit)        { updateUnit(unit); }
	void					onUnitCreate(BWAPI::Unit unit)		{ updateUnit(unit); maybeAddBase(unit); }
    void					onUnitComplete(BWAPI::Unit unit)    { updateUnit(unit); maybeAddStaticDefense(unit); }
	void					onUnitMorph(BWAPI::Unit unit)       { updateUnit(unit); maybeAddBase(unit); }
    void					onUnitRenegade(BWAPI::Unit unit)    { updateUnit(unit); }
    void					onUnitDestroy(BWAPI::Unit unit);
	
	bool					isEnemyBuildingInRegion(const BWEM::Area* region);
    int						getNumUnits(BWAPI::UnitType type,BWAPI::Player player);
    bool					isCombatUnit(BWAPI::UnitType type) const;

    void                    getNearbyForce(std::vector<UnitInfo> & unitInfo,BWAPI::Position p,BWAPI::Player player,int radius);

    const UIMap &           getUnitInfo(BWAPI::Player player) const;
	const UnitData &        getUnitData(BWAPI::Player player) const;
	const UnitInfo &		getUnit(BWAPI::Player player, BWAPI::Unit unit) const;
	void					setUnitLastOrderPath(BWAPI::Unit unit, BWEB::Path path);
	void					setUnitLastOrderPosition(BWAPI::Unit unit, BWAPI::Position position);
	void					setUnitLastMovePath(BWAPI::Unit unit, BWEB::Path path);
	void					setUnitLastMovePosition(BWAPI::Unit unit, BWAPI::Position position);
	void					setUnitLastPathPosition(BWAPI::Unit unit, BWAPI::Position position);
	void					setUnitLastOrderPathBWEM(BWAPI::Unit unit, BWEM::CPPath path);
	void					setUnitLastMoveFrame(BWAPI::Unit unit, int frame);
	void					setLastRetreatSwitchFrame(BWAPI::Unit unit, int frame);
	void					setLastRetreatVal(BWAPI::Unit unit, bool retreatVal);

	std::set<const BWEM::Area *>  getOccupiedRegions(BWAPI::Player player);
	const BWEM::Base *		getMainBaseLocation(BWAPI::Player player);
	const BWEM::Base *		getMyMainBaseLocation();
	const BWEM::Base *		getEnemyMainBaseLocation();
	BWAPI::Player			getBaseOwner(const BWEM::Base * base);
	BWAPI::Unit 			getBaseDepot(const BWEM::Base * base);
	const BWEM::Base *		getMyNaturalLocation();
	const BWEM::Base *		getMyThirdLocation();

	const BWEM::Base *		getClosestBaseLocation(BWAPI::TilePosition tilePosition);
	const BWEM::Base *		getClosestFriendlyBaseLocation(BWAPI::TilePosition tilePosition);
	const BWEM::Base *		getClosestOtherBaseLocation(BWAPI::TilePosition tilePosition);
	const UnitInfo &		getClosestGroundUnit(BWAPI::TilePosition tilePosition);

	std::vector<const BWEM::Base *>	getBases(BWAPI::Player player);
	std::vector<const BWEM::Base *> getMyBases() { return getBases(BWAPI::Broodwar->self()); }
	std::vector<const BWEM::Base *> getEnemyBases() { return getBases(BWAPI::Broodwar->enemy()); }

	bool					getEnemyProxy() { return _enemyProxy; };

	int						getNumBases(BWAPI::Player player);
	int						getNumFreeLandBases();
	int						getMyNumMineralPatches();
	int						getMyNumGeysers();
	int						getAir2GroundSupply(BWAPI::Player player) const;
	bool					getGroundOverAir() const;
	int						getMyNumRefineries();

	bool					isBaseReserved(const BWEM::Base * base);
	void					reserveBase(const BWEM::Base * base);
	void					unreserveBase(const BWEM::Base * base);
	void					unreserveBase(BWAPI::TilePosition baseTilePosition);

	bool					weHaveCombatUnits();

	bool					enemyHasCombatUnits();
	bool					enemyHasAirUnits();
	bool					enemyHasStaticAntiAir();
	bool					enemyHasAntiAir();
	bool					enemyHasAirTech();
	bool                    enemyHasCloakTech();
	bool                    enemyHasMobileCloakTech();
	bool					enemyHasOverlordHunters();
	bool					enemyHasLiftedBuildings();
	bool					enemyHasVultures();
	bool                    enemyCloakedUnitsSeen();
	bool					enemyHasStaticDetection();
	bool					enemyHasMobileDetection();
	bool					enemyHasSiegeMode();

	// Defense logic
	const BWEM::Base *		getLeastDefendedBaseGround(BWAPI::Player player);
	const BWEM::Base *		getLeastDefendedBaseAir(BWAPI::Player player);
	int						getNumEnemyUnitsVsGround(const BWEM::Base * base);
	int						getNumBuildings(const BWEM::Base * basee, BWAPI::UnitType unittype);
	int						getNumSpores(const BWEM::Base * base);
	int						getNumSunkens(const BWEM::Base * base);
	int						getNumCreep(const BWEM::Base * base);
	int						getNumWorkersIncoming();
	int						getEnemyPowerGroundUnitsIncoming();
	int						getEnemyPowerGroundUnits();
	int						getMyPowerGroundWeapon(bool withCreep);

	// Unit mix logic
	int						getPower(BWAPI::Player player);
	int						getCostArmy(BWAPI::Player player);
	int						getCostUnit(BWAPI::Player player, BWAPI::UnitType unittype);
	int						getCostUnit(BWAPI::UnitType unittype);
	int						getCostAir(BWAPI::Player player);
	int						getCostGround(BWAPI::Player player);
	int						getCostStaticDefenseAir(BWAPI::Player player);
	int						getCostStaticDefenseGround(BWAPI::Player player);
	int						getCostAntiAir(BWAPI::Player player);
	int						getCostAntiGround(BWAPI::Player player);
	int						getCostGroundAoE(BWAPI::Player player);
	int						getCostAntiMeleeAoE(BWAPI::Player player);
	int						getCostSmall(BWAPI::Player player);
	int						getCostMedium(BWAPI::Player player);
	int						getCostLarge(BWAPI::Player player);

	// Grids
	int						getAirThreat(BWAPI::TilePosition tile);

	BWAPI::Unit				nearestGroundStaticDefense(BWAPI::Position pos) const;
	BWAPI::Unit				nearestAirStaticDefense(BWAPI::Position pos) const;
	BWAPI::Unit				nearestGroundImmobileDefense(BWAPI::Position pos) const;
	BWAPI::Unit				nearestShieldBattery(BWAPI::Position pos) const; // protoss specific

	int						nScourgeNeeded(); // zerg specific

    void                    drawExtendedInterface();
    void                    drawUnitInformation(int x,int y);
    void                    drawMapInformation();
	void					drawBaseInformation(int x, int y);

	const BWEM::Area *			getNaturalArea();
	const BWEM::Area *			getMainArea();
	const BWEM::ChokePoint *	getNaturalChoke();
	const BWEM::ChokePoint *	getMainChoke();
	BWAPI::TilePosition			getNaturalTile();
	BWAPI::Position				getNaturalPosition();
	BWAPI::TilePosition			getMainTile();
	BWAPI::Position				getMainPosition();

	void						findMainChoke();
	void						findNaturalChoke();

	// yay for singletons!
	static InformationManager & Instance();
};
}

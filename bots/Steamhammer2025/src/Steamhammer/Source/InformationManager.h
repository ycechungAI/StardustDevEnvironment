#pragma once

#include "ResourceInfo.h"
#include "UnitData.h"

namespace UAlbertaBot
{
class Zone;

class InformationManager
{
    BWAPI::Player	_self;
    BWAPI::Player	_enemy;

    bool			_weHaveCombatUnits;
	bool			_weHaveAirUnits;

    bool			_enemyHasCombatUnits;
    bool			_enemyHasStaticAntiAir;
    bool			_enemyHasAntiAir;
    bool			_enemyHasAirTech;
	bool			_enemyHasAirToGround;
    bool			_enemyHasCloakTech;
    bool            _enemyCloakedUnitsSeen;
    bool			_enemyHasMobileCloakTech;
    bool			_enemyHasAirCloakTech;
    bool			_enemyHasOverlordHunters;
    bool			_enemyHasStaticDetection;
    bool			_enemyHasMobileDetection;
	bool			_enemyHasTransport;

	bool			_enemyHasStim;
	bool			_enemyHasOpticFlare;
	bool			_enemyHasRestoration;
	bool            _enemyHasMines;
	bool			_enemyHasSiegeMode;
	bool			_enemyHasIrradiate;
	bool			_enemyHasEMP;
	bool			_enemyHasLockdown;
	bool			_enemyHasYamato;
	bool            _enemyHasNuke;

	bool			_enemyHasStorm;
	bool			_enemyHasMaelstrom;
	bool			_enemyHasDWeb;
	bool			_enemyHasStasis;
	bool			_enemyHasRecall;

	bool			_enemyHasBurrow;
	bool			_enemyHasLurkers;
	bool			_enemyHasEnsnare;
	bool			_enemyHasBroodling;
	bool			_enemyHasPlague;

	int             _enemyGasTiming;

	int				_bunkerShouldFireRange;			// range of marines in a bunker
	BWAPI::Unitset  _loadedBunkers;					// enemy bunkers that are loaded and may shoot at us

    std::map<BWAPI::Player, UnitData>                   _unitData;
    std::map<BWAPI::Player, std::set<const Zone *> >	_occupiedRegions;	// contains any building
    BWAPI::Unitset										_staticDefense;
    BWAPI::Unitset										_ourPylons;
    std::map<BWAPI::Unit, BWAPI::Unitset>				_yourTargets;		// our unit -> [enemy units targeting it]
    BWAPI::Unitset                                      _enemyScans;

	// Nuke dots disappear 3 seconds before the nuke lands.
	// So we track them until they land or are defeated.
	std::map<BWAPI::Position, int>						_enemyNukes;		// nuke target -> expected kaboom time

    // Track a resource container (mineral patch or geyser) by its initial static unit.
    // A mineral patch unit will disappear when it is mined out. A geyser unit will change when taken.
	// The initial static units stay the same, and we remember them here.
    std::map<BWAPI::Unit, ResourceInfo> _resources;

    InformationManager();

    void initializeRegionInformation();
    void initializeResources();

    void maybeClearNeutral(BWAPI::Unit unit);

    void maybeAddStaticDefense(BWAPI::Unit unit);

    void updateUnit(BWAPI::Unit unit);
    void updateUnitInfo();
    void updateBaseLocationInfo();
    void updateOccupiedRegions(const Zone * zone, BWAPI::Player player);
    void updateGoneFromLastPosition();
    void updateYourTargets();
    void updateBullets();
	void updateEnemyOrders();
    void updateResources();
    void updateEnemyGasTiming();
    void updateTerranEnemyAbilities();

	// Fill the given set with every enemy bunker within marine range of any of our units.
	void getEnemyBunkersInRange(BWAPI::Unitset & bunkers);

public:

    void                    initialize();

    void                    update();

    // event driven stuff
    void					onUnitShow(BWAPI::Unit unit)        { updateUnit(unit); }
    void					onUnitHide(BWAPI::Unit unit)        { updateUnit(unit); }
    void					onUnitCreate(BWAPI::Unit unit)		{ updateUnit(unit); }
    void					onUnitComplete(BWAPI::Unit unit)    { updateUnit(unit); maybeAddStaticDefense(unit); }
    void					onUnitMorph(BWAPI::Unit unit)       { updateUnit(unit); }
    void					onUnitRenegade(BWAPI::Unit unit)    { updateUnit(unit); maybeClearNeutral(unit); }
    void					onUnitDestroy(BWAPI::Unit unit);

    bool					isEnemyBuildingInRegion(const Zone * region);

    void                    getNearbyForce(std::vector<UnitInfo> & unitsOut, BWAPI::Position p, BWAPI::Player player, int radius) const;

    const UIMap &           getUnitInfo(BWAPI::Player player) const;

    std::set<const Zone *> &getOccupiedRegions(BWAPI::Player player);

    int						getAir2GroundSupply(BWAPI::Player player) const;

    bool					weHaveCombatUnits();
	bool					weHaveAirUnits();

	// For some spells, figure out whether the enemy has them by looking for their effects.
	void					updateEnemySpells();

    bool					enemyHasCombatUnits();
    bool					enemyHasStaticAntiAir();
    bool					enemyHasAntiAir();
    bool					enemyHasAirTech();
	bool					enemyHasAirToGround();
    bool                    enemyHasCloakTech();
    bool                    enemyCloakedUnitsSeen();
    bool                    enemyHasMobileCloakTech();
    bool                    enemyHasAirCloakTech();
    bool					enemyHasOverlordHunters();
    bool					enemyHasStaticDetection();
    bool					enemyHasMobileDetection();
	bool					enemyHasTransport();

	bool					enemyHasStim()          const { return _enemyHasStim; };
	bool					enemyHasOpticFlare()    const { return _enemyHasOpticFlare; };
	bool					enemyHasRestoration()   const { return _enemyHasRestoration; };
    bool					enemyHasSiegeMode();
	bool					enemyHasMines();
	bool					enemyHasEMP()           const { return _enemyHasEMP; };
	bool					enemyHasIrradiate()     const { return _enemyHasIrradiate; };
	bool					enemyHasLockdown()      const { return _enemyHasLockdown; };
	bool					enemyHasYamato()        const { return _enemyHasYamato; };
	bool					enemyHasNuke();

	bool					enemyHasStorm()         const { return _enemyHasStorm; };
	bool					enemyHasMaelstrom()     const { return _enemyHasMaelstrom; };
	bool					enemyHasDWeb()          const { return _enemyHasDWeb; };
	bool					enemyHasStasis()        const { return _enemyHasStasis; };
	bool					enemyHasRecall()        const { return _enemyHasRecall; };

	bool					enemyHasBurrow();
	bool                    enemyHasLurkers();
	bool					enemyHasBroodling()     const { return _enemyHasBroodling; };
	bool					enemyHasEnsnare()       const { return _enemyHasEnsnare; };
	bool                    enemyHasPlague()        const { return _enemyHasPlague; };

	bool					enemyHasMarineRange()   const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::U_238_Shells) > 0; };
	bool					enemyHasVultureSpeed()  const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Ion_Thrusters) > 0; };
	bool					enemyHasGoliathRange()  const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Charon_Boosters) > 0; };
	bool					enemyHasGhostSight()    const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Ocular_Implants) > 0; };

	bool					enemyHasDragoonRange()  const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Singularity_Charge) > 0; };
	bool					enemyHasZealotSpeed()   const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Leg_Enhancements) > 0; };
	bool					enemyHasReaverDamage()  const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Scarab_Damage) > 0; };
	bool					enemyHas8Interceptors() const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Carrier_Capacity) > 0; };

	bool					enemyHasLingSpeed()     const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Metabolic_Boost) > 0; };
	bool					enemyHasLingAttack()    const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Adrenal_Glands) > 0; };
	bool					enemyHasHydraSpeed()    const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Muscular_Augments) > 0; };
	bool					enemyHasHydraRange()    const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Grooved_Spines) > 0; };
	bool					enemyHasOverlordSpeed() const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) > 0; };
	bool					enemyHasUltraSpeed()    const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Anabolic_Synthesis) > 0; };
	bool					enemyHasUltraArmor()    const { return BWAPI::Broodwar->enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Chitinous_Plating) > 0; };

	int                     enemyGasTiming() const { return _enemyGasTiming; };

    bool                    weHaveCloakTech() const;

    void					enemySeenBurrowing(BWAPI::Unit unit);
    int                     getEnemyBuildingTiming(BWAPI::UnitType type) const;
    int                     remainingBuildTime(BWAPI::UnitType type) const;
    int                     getMySpireTiming() const;

    const BWAPI::Unitset &  getStaticDefense() const { return _staticDefense; };
    const BWAPI::Unitset &  getOurPylons() const { return _ourPylons; };
    const BWAPI::Unitset &  getEnemyScans() const { return _enemyScans; };
	const std::map<BWAPI::Position, int> & getEnemyNukes() const { return _enemyNukes; };

    BWAPI::Unit				nearestGroundStaticDefense(BWAPI::Position pos) const;
    BWAPI::Unit				nearestAirStaticDefense(BWAPI::Position pos) const;
    BWAPI::Unit				nearestShieldBattery(BWAPI::Position pos) const;

	bool					inAirDefenseRange(BWAPI::Unit u) const;

	bool					isEnemyBunkerLoaded(BWAPI::Unit bunker) const;

    int						nScourgeNeeded() const;				// zerg specific
	bool					allOverlordsProtected() const;		// zerg specific

    void                    drawExtendedInterface() const;
    void                    drawUnitInformation(int x,int y);
    void                    drawResourceAmounts() const;

    const UnitData &        getUnitData(BWAPI::Player player) const;
    const UnitInfo *        getUnitInfo(BWAPI::Unit unit) const;        // enemy units only
    const BWAPI::Unitset &	getEnemyFireteam(BWAPI::Unit ourUnit) const;
    int                     getResourceAmount(BWAPI::Unit resource) const;
    bool                    isMineralDestroyed(BWAPI::Unit resource) const;
    bool                    isGeyserTaken(BWAPI::Unit resource) const;
	BWAPI::UnitType			getDetectorType() const;

    static InformationManager & Instance();
};
}

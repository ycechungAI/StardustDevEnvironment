#pragma once

#include "Common.h"
#include "OpsBoss.h"

#include "MicroAirToAir.h"
#include "MicroMelee.h"
#include "MicroRanged.h"

#include "MicroDefilers.h"
#include "MicroDetectors.h"
#include "MicroHighTemplar.h"
#include "MicroIrradiated.h"
#include "MicroLurkers.h"
#include "MicroMedics.h"
#include "MicroMinelaying.h"
#include "MicroMutas.h"
#include "MicroOverlords.h"
#include "MicroQueens.h"
#include "MicroScourge.h"
#include "MicroTanks.h"
#include "MicroTransports.h"

namespace UAlbertaBot
{
class Squad
{
    std::string         _name;
    BWAPI::Unitset      _units;
    bool				_combatSquad;
    int					_combatSimRadius;
    bool				_fightVisibleOnly;  // combat sim uses visible enemies only (vs. all known enemies)
    bool				_meatgrinder;		// combat sim says "win" even if you do only modest damage
	bool				_dropSquad;
    bool				_hasAir;
    bool				_hasGround;
    bool				_canAttackAir;
    bool				_canAttackGround;
    std::string         _regroupStatus;
    size_t              _priority;

    // Values specifically for the combat commander, not otherwise needed.
    int                 _timeMark;          // set and read by combat commander
    int                 _lastAttack;        // last frame a squad cluster attacked
    int                 _lastRetreat;       // last frame a squad cluster retreated
    // NOTE Other cluster actions don't update _lastAttack or _lastRetreat.

    SquadOrder          _order;
    int                 _orderFrame;        // when the order was last changed
    //LurkerTactic        _lurkerTactic;
    BWAPI::Unit			_vanguard;			// the unit closest to the order location, if any

	MicroMinelaying		_microMinelaying;
    MicroIrradiated     _microIrradiated;
    MicroOverlords      _microOverlords;

    MicroAirToAir		_microAirToAir;
    MicroMelee			_microMelee;
    MicroRanged			_microRanged;
    MicroDefilers		_microDefilers;
    MicroDetectors		_microDetectors;
    MicroHighTemplar	_microHighTemplar;
    MicroLurkers		_microLurkers;
    MicroMedics			_microMedics;
    //MicroMutas          _microMutas;
    MicroQueens			_microQueens;
    MicroScourge        _microScourge;
    MicroTanks			_microTanks;
    MicroTransports		_microTransports;

    std::map<BWAPI::Unit, bool> _nearEnemy;

    std::vector<UnitCluster> _clusters;

    static const int ImmobileDefenseRadius = 800;

    BWAPI::Unit     unitClosestToPosition(const BWAPI::Position & pos, const BWAPI::Unitset & units) const;
    BWAPI::Unit		unitClosestToTarget(const BWAPI::Unitset & units) const;

    void			updateUnits();
    void			addUnitsToMicroManagers();
    void			setNearEnemyUnits();
    void			setAllUnits();
    void            setOrderForMicroManagers();

	bool			isDrop() const { return _dropSquad; };

    void			setClusterStatus(UnitCluster & cluster);
    void            setLastAttackRetreat();
    bool            resetClusterStatus(UnitCluster & cluster);
    void            microSpecialUnits(const UnitCluster & cluster);
    void            clusterCombat(const UnitCluster & cluster);
    bool			noCombatUnits(const UnitCluster & cluster) const;
    bool			notNearEnemy(const UnitCluster & cluster);
    bool			joinUp(const UnitCluster & cluster);
	bool			canReachMineralLine(const UnitCluster & cluster) const;
	void			moveCluster(const UnitCluster & cluster, const BWAPI::Position & destination, bool lazy = true);
    void			attackMoveCluster(const UnitCluster & cluster, const BWAPI::Position & destination);
    void			safeMoveCluster(const UnitCluster & cluster, const BWAPI::Position & destination);

    bool			unreadyUnit(BWAPI::Unit u);
	bool			unitNearEnemy(BWAPI::Unit unit);

    bool			needsToRegroup(UnitCluster & cluster);
    BWAPI::Position calcRegroupPosition(const UnitCluster & cluster) const;
    BWAPI::Position finalRegroupPosition(const UnitCluster & cluster) const;
    BWAPI::Unit     nearbyImmobileGroundDefense(const BWAPI::Position & pos) const;
    BWAPI::Unit		nearbyImmobileDefense(const BWAPI::Position & pos, bool versusAir) const;
	const UnitCluster *	findMenacingEnemyCluster(const UnitCluster & cluster) const;
	BWAPI::Position	seekSafeAirRetreat(const UnitCluster & cluster, BWAPI::Unit enemyUnit) const;
	BWAPI::Position	seekSafePositionNear(const UnitCluster & cluster, const BWAPI::TilePosition & tile) const;

	int				clusterPositionCost(const UnitCluster & cluster, const BWAPI::TilePosition & tile) const;

    bool            maybeWatch();
	void			holdPosition();
    void			loadTransport();
    void			stimIfNeeded();

    void			drawCluster(const UnitCluster & cluster) const;

public:

    Squad();
	Squad(const std::string & name, size_t priority, bool isDrop = false);
	~Squad();

    void                update();
    void                addUnit(BWAPI::Unit u);
    void                removeUnit(BWAPI::Unit u);
    void				releaseWorkers();
    bool                containsUnit(BWAPI::Unit u) const;
    bool                containsUnitType(BWAPI::UnitType t) const;
    bool                isEmpty() const;
    void                clear();
    size_t              getPriority() const;
    void                setPriority(const size_t & priority);
    const std::string & getName() const;

    int					mapPartition() const;
    BWAPI::Position     calcCenter() const;

    const BWAPI::Unitset &  getUnits() const;
    BWAPI::Unit			getVanguard() const { return _vanguard; };		// may be null
    void                setOrder(SquadOrder so);
    SquadOrder &        getOrder();
    int                 getOrderFrame() const { return _orderFrame; };
    //void                setLurkerTactic(LurkerTactic tactic);
    const std::string   getRegroupStatus() const;

    int					getCombatSimRadius() const { return _combatSimRadius; };
    void				setCombatSimRadius(int radius) { _combatSimRadius = radius; };

    bool				getFightVisible() const { return _fightVisibleOnly; };
    void				setFightVisible(bool visibleOnly) { _fightVisibleOnly = visibleOnly; };

    bool				getMeatgrinder() const { return _meatgrinder; };
    void				setMeatgrinder(bool toWound) { _meatgrinder = toWound; };

    int                 getTimeMark() const { return _timeMark; };
    void                setTimeMark(int mark) { _timeMark = mark; };
    int                 getLastAttack() const { return _lastAttack; };
    int                 getLastRetreat() const { return _lastRetreat; };

    bool			    hasAir()			const { return _hasAir; };
    bool			    hasGround()			const { return _hasGround; };
    bool			    canAttackAir()		const { return _canAttackAir; };
    bool			    canAttackGround()	const { return _canAttackGround; };
    bool			    hasDetector()		const { return !_microDetectors.getUnits().empty(); };
    bool			    hasCombatUnits()	const;
	int					countUnits(BWAPI::UnitType type) const;
	bool				isAtOrderPosition() const;
    bool			    isOverlordHunterSquad() const;
	BWAPI::Unit			getSampleGroundUnit() const;
	BWAPI::Unit			getAnyScourge() const;

    // Distance from the given position to the order position.
	// If it's a ground squad, the ground distances should be set, and these will use ground distance.
    int                 getDistance(const BWAPI::TilePosition & tile) const;
    int                 getDistance(const BWAPI::Position & pos) const;
};
}
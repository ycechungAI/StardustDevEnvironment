#pragma once

#include "Common.h"
#include "StrategyManager.h"
#include "CombatSimulation.h"
#include "SquadOrder.h"
#include "Cluster.h"

#include "MeleeManager.h"
#include "RangedManager.h"
#include "DetectorManager.h"
#include "LurkerManager.h"
#include "TankManager.h"
#include "TransportManager.h"
#include "MedicManager.h"
#include "SurveyerManager.h"
#include "CasterManager.h"

namespace UAlbertaBot
{
    
	class Squad
	{
		BWAPI::Player		_self = BWAPI::Broodwar->self();
		BWAPI::Player		_enemy = BWAPI::Broodwar->enemy();

		// Time and distance beyond maximum realistic values,
		// so that we can represent "never" and "not anywhere" and do arithmetic on the values
		// without risk of integer overflow.
		int MAX_FRAME = 24 * 60 * 60 * 24;	// 24 hours
		int MAX_DISTANCE = 2 * 32 * 256;	// twice the width of the largest maps in pixels

		// Current frame count, same as Broodwar->getFrameCount().
		int NOW = BWAPI::Broodwar->getFrameCount();

		std::string         _name;
		BWAPI::Unitset      _units;
		bool				_combatSquad;
		int					_combatSimRadius;
		BWAPI::Position		_center;
		BWAPI::Unit			_vanguard;
		BWAPI::Position		_lastOrderPosition;
		bool				_fightVisibleOnly;  // combat sim uses visible enemies only (vs. all known enemies)
		bool				_enemyHasAir;
		bool				_enemyHasGround;
		bool				_enemyCanAttackAir;
		bool				_enemyCanAttackGround;
		bool				_hasAir;
		bool				_hasGround;
		bool				_canAttackAir;
		bool				_canAttackGround;
		std::string         _regroupStatus;
		bool				_attackAtMax;       // turns true when we are at max supply
		int                 _lastRetreatSwitch;
		bool                _lastRetreatSwitchVal;
		int					_lastScoreVal;
		size_t              _priority;

		// Values specifically for the combat commander, not otherwise needed.
		int                 _timeMark;          // set and read by combat commander
		int                 _lastAttack;        // last frame a squad cluster attacked
		int                 _lastRetreat;       // last frame a squad cluster retreated

		SquadOrder          _order;
		MeleeManager        _meleeManager;
		RangedManager       _rangedManager;
		DetectorManager     _detectorManager;
		SurveyerManager     _surveyerManager;
		LurkerManager       _lurkerManager;
		TankManager         _tankManager;
		TransportManager    _transportManager;
		MedicManager        _medicManager;
		CasterManager		_casterManager;

		std::map<BWAPI::Unit, bool>	_nearEnemy;
		std::vector<UnitCluster> _clusters;

		BWAPI::Unit     unitClosestToPosition(const BWAPI::Position & pos, const BWAPI::Unitset & units) const;
		BWAPI::Unit		unitClosestToTarget(const BWAPI::Unitset & units) const;

		void			updateUnits();
		void			addUnitsToMicroManagers();
		void			setNearEnemyUnits();
		void			setAllUnits();
		void			setAllUnitsEnemy();
	
		bool			unitNearEnemy(BWAPI::Unit unit);
		bool			needsToRegroup();
		bool			needsToRegroupCluster(UnitCluster & cluster);
		int				squadUnitsNear(BWAPI::Position p);

		void			setClusterStatus(UnitCluster & cluster);
		void            setLastAttackRetreat();
		bool            resetClusterStatus(UnitCluster & cluster);
		void            microSpecialUnits(const UnitCluster & cluster);
		void            clusterCombat(const UnitCluster & cluster);
		bool			noCombatUnits(const UnitCluster & cluster) const;
		bool			notNearEnemy(const UnitCluster & cluster);

		void			drawCluster(const UnitCluster & cluster) const;

	public:

		Squad(const std::string & name, SquadOrder order, size_t priority);
		Squad();
		~Squad();

		void                update();
		void                setSquadOrder(const SquadOrder & so);
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
		const std::string &	getRegroupStatus() const { return _regroupStatus; };
    
		BWAPI::Position     calcCenter() const;
		BWAPI::Position     calcRegroupPosition();
		BWAPI::Position		calcRegroupPositionCluster(const UnitCluster & cluster);
		BWAPI::Position		finalRegroupPosition() const;
		BWAPI::Unit			unitClosestToEnemy() const;
		BWAPI::Unit			nearbyImmobileDefense(const BWAPI::Position & pos) const;

		const BWAPI::Unitset &  getUnits() const;
		const SquadOrder &  getSquadOrder()	const;

		int					getCombatSimRadius() const { return _combatSimRadius; };
		void				setCombatSimRadius(int radius) { _combatSimRadius = radius; };

		bool				getFightVisible() const { return _fightVisibleOnly; };
		void				setFightVisible(bool visibleOnly) { _fightVisibleOnly = visibleOnly; };

		const bool			hasAir()			const { return _hasAir; };
		const bool			hasGround()			const { return _hasGround; };
		const bool			canAttackAir()		const { return _canAttackAir; };
		const bool			canAttackGround()	const { return _canAttackGround; };
		const bool			hasDetector()		const { return !_detectorManager.getUnits().empty(); };
		const bool			hasCombatUnits()	const;	
	};
}
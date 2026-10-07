#pragma once

#include "Common.h"
#include "Squad.h"
#include "SquadData.h"
#include "InformationManager.h"
#include "StrategyManager.h"

namespace UAlbertaBot
{
class CombatCommander
{
	SquadData       _squadData;
    BWAPI::Unitset  _combatUnits;
    bool            _initialized;

	BWAPI::Position	_reconTarget;
	int				_lastReconTargetChange;         // frame number

    void            updateScoutDefenseSquad();
	void            updateDefenseSquads();
	void            updateAttackSquads();
	void			updateReconSquad();
    void            updateDropSquads();
	void            updateIdleSquad();
	void			updateSurveySquad();

	bool            wantSquadDetectors() const;
	void			maybeAssignDetector(Squad & squad, bool wantDetector);

	void			doLarvaTrick();

	int				weighReconUnit(const BWAPI::Unit unit) const;
	int				weighReconUnit(const BWAPI::UnitType type) const;

	bool			isFlyingSquadUnit(const BWAPI::UnitType type) const;
	bool			isOptionalFlyingSquadUnit(const BWAPI::UnitType type) const;
	bool			isGroundAntiAirSquadUnit(const BWAPI::UnitType type) const;
	bool			isGroundSquadUnit(const BWAPI::UnitType type) const;

	void			cancelDyingBuildings();

	int             getNumType(BWAPI::Unitset & units, BWAPI::UnitType type);

	BWAPI::Unit     findClosestDefender(const Squad & defenseSquad, BWAPI::Position pos, bool flyingDefender, bool pullWorkers);
	BWAPI::Unit     findClosestWorkerToTarget(BWAPI::Unitset & unitsToAssign, BWAPI::Unit target);

	void			chooseReconTarget(const Squad * squad);
	BWAPI::Position getReconLocation(const Squad * squad);
	SquadOrder		getAttackOrder(const Squad * squad);
	BWAPI::Position getAttackLocation(const Squad * squad);
	BWAPI::Position	getDefenseLocation();

    void            initializeSquads();
    void            verifySquadUniqueMembership();
    void            assignFlyingDefender(Squad & squad);
    void            emptySquad(Squad & squad, BWAPI::Unitset & unitsToAssign);
    int             getNumGroundDefendersInSquad(Squad & squad);
    int             getNumAirDefendersInSquad(Squad & squad);

	void            updateDefenseSquadUnits(Squad & defenseSquad, const size_t & flyingDefendersNeeded, const size_t & groundDefendersNeeded, bool pullWorkers);
	int             defendWithWorkers();

    int             numZerglingsInOurBase();
    bool            beingBuildingRushed();

public:

	CombatCommander();

	BWAPI::Position getSurveyLocation();
	void update(const BWAPI::Unitset & combatUnits);

	void drawSquadInformation(int x, int y);

	static CombatCommander & Instance();
};
}

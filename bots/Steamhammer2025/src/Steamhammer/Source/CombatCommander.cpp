#include <queue>

#include "CombatCommander.h"

#include "Bases.h"
#include "BuildingManager.h"
#include "BuildingPlacer.h"
#include "MapGrid.h"
#include "Micro.h"
#include "Moveout.h"
#include "OpsStrategy.h"
#include "ProductionManager.h"
#include "Random.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// Squad priorities: Which can steal units from others.
// Anyone can steal from the Idle squad.
const size_t IdlePriority = 0;
const size_t OverlordPriority = 1;
const size_t AttackPriority = 2;
const size_t ClearancePriority = 3;
const size_t ReconPriority = 4;
const size_t BaseDefensePriority = 5;
const size_t EarlyDefensePriority = 6;	// only until we have combat units
const size_t MinePriority = 7;			// terran only, lay spider mines
const size_t WatchPriority = 7;			// zerg only, watch bases for enemy expansions
const size_t DropPriority = 8;			// don't steal from Drop squad for anything else
const size_t ScourgePriority = 9;		// all scourge and only scourge
const size_t IrradiatedPriority = 10;   // irradiated organic units

// The attack squads.
const int DefendFrontRadius = 400;
const int AttackRadius = 800;

// Reconnaissance squad.
const int ReconTargetTimeout = 40 * 24;
const int ReconRadius = 400;

// If there are this many carriers or more, carriers are in the flying squad
// and act independently.
const int CarrierIndependenceCount = 4;

CombatCommander::CombatCommander() 
    : _initialized(false)
	, _scourgeTarget(BWAPI::Positions::None)
    , _isWatching(false)
    , _reconSquadAlive(false)
    , _reconTarget(nullptr)   // it will be set when the squad is filled
    , _lastReconTargetChange(0)
	, _clearanceTarget(nullptr)
    , _carrierCount(0)
{
}

// Called once at the start of the game.
// You can also create new squads at other times.
void CombatCommander::initializeSquads()
{
    // The idle squad includes workers at work (not idle at all) and unassigned units.
    _squadData.createSquad("Idle", IdlePriority).getOrder().setStatus("Work");

    // These squads don't care what order they are given.
    // They analyze the situation for themselves.
    if (the.selfRace() == BWAPI::Races::Zerg)
    {
        // The overlord squad has only overlords, but not all overlords.
        // They may be assigned elsewhere too.
        _squadData.createSquad("Overlord", OverlordPriority).getOrder().setStatus("Look");

        // The scourge squad has all the scourge.
        if (the.selfRace() == BWAPI::Races::Zerg)
        {
            _squadData.createSquad("Scourge", ScourgePriority).getOrder().setStatus("Wait");
        }
    }
    
    // The ground squad will pressure an enemy base.
    _squadData.createSquad("Ground", AttackPriority);

    // The flying squad separates air units so they can act independently.
    _squadData.createSquad("Flying", AttackPriority);

    // The recon squad carries out reconnaissance in force to find and deny enemy bases.
    // It is filled in when enough units are available.
	_squadData.createSquad("Recon", ReconPriority);

    // The early defense squad handles worker rushes and undefended proxies until combat units are out.
	// The radius should be large enough for everything workers may be sent against. Or they'll go idle.
    _squadData.createSquad("EarlyDefense", EarlyDefensePriority);

	// If we're expecting to drop, create a drop squad.
    // It is initially ordered to hold ground until it can load up and go.
    if (StrategyManager::Instance().dropIsPlanned())
    {
        _squadData.createSquad("Drop", DropPriority, true).
            setOrder(SquadOrder(SquadOrderTypes::Attack, the.bases.myMain(), AttackRadius, false, "Wait for transport"));
    }
}

void CombatCommander::update(const BWAPI::Unitset & combatUnits)
{
    if (!_initialized)
    {
        initializeSquads();
        _initialized = true;
    }

    _combatUnits = combatUnits;

	if (the.now() % 2 == 0)
	{
		updateScourgeSquad();			// scourge needs to react fast
	}

	int frame8 = the.now() % 8;

	if (frame8 == 1)					// non-combat squads
	{
		updateIdleSquad();
		updateOverlordSquad(); 
    }
	else if (frame8 == 2)				// combat squads
	{
		the.opsStrategy.update();		// does nothing yet

		updateDropSquads();
		updateEarlyDefenseSquad();
		updateBaseDefenseSquads();
		updateMineSquad();
		updateWatchSquads();
		updateReconSquad();
		updateClearanceSquad();
		updateAttackSquads();
	}
	else if (frame8 % 4 == 3)
    {
		updateIrradiatedSquad();
		doComsatScan();
    }

    if (the.now() % 20 == 1)
    {
        doLarvaTrick();
    }

    loadOrUnloadBunkers();

    _squadData.update();          // update() all the squads

    cancelDyingItems();
}

// Clean up any data structures that may otherwise not be unwound in the correct order.
// This fixes an end-of-game bug diagnosed by Bruce Nielsen.
// This onEnd() doesn't care who won.
void CombatCommander::onEnd()
{
    _squadData.clearSquadData();
}

// Called by LurkerSkill.
//void CombatCommander::setGeneralLurkerTactic(LurkerTactic tactic)
//{
//    _lurkerOrders.generalTactic = tactic;
//}
//
//// Called by LurkerSkill.
//void CombatCommander::addLurkerOrder(LurkerOrder & order)
//{
//    _lurkerOrders.orders[order.tactic] = order;
//}
//
//// Called by LurkerSkill.
//void CombatCommander::clearLurkerOrder(LurkerTactic tactic)
//{
//    _lurkerOrders.orders.erase(tactic);
//}

// Called from the.info when it sees lurker spines and the source is unknown or undetected.
// See doComsatScan() for the regular scans.
void CombatCommander::scanLurkerNear(const BWAPI::Position & pos)
{
	if (the.selfRace() != BWAPI::Races::Terran)
	{
		return;
	}

	if (the.my.completed.count(BWAPI::UnitTypes::Terran_Comsat_Station) == 0)
	{
		return;
	}

	// micro.Scan() takes care of details like finding a scanner with energy and making sure we haven't already scanned there.
	(void)the.micro.Scan(pos);
}

void CombatCommander::updateIdleSquad()
{
    Squad & idleSquad = _squadData.getSquad("Idle");

    for (BWAPI::Unit unit : _combatUnits)
    {
        // if it hasn't been assigned to a squad yet, put it in the low priority idle squad
        if (_squadData.canAssignUnitToSquad(unit, idleSquad))
        {
            _squadData.assignUnitToSquad(unit, idleSquad);
        }
    }
}

// Put irradiated organic units into the Irradiated squad.
// Exceptions: Queens and defilers.
void CombatCommander::updateIrradiatedSquad()
{
    Squad * radSquad = nullptr;
    if (_squadData.squadExists("Irradiated"))
    {
        radSquad = & _squadData.getSquad("Irradiated");
    }

    for (BWAPI::Unit unit : _combatUnits)
    {
        if (unit->isIrradiated())
        {
			if (unit->getType().isOrganic() && (!radSquad || !radSquad->containsUnit(unit)))
			{
				if (unit->getType() == BWAPI::UnitTypes::Zerg_Queen && unit->getEnergy() > 65 ||
					unit->getType() == BWAPI::UnitTypes::Zerg_Defiler && the.self()->hasResearched(BWAPI::TechTypes::Consume))
				{
					// Ignore these spellcasters. They may want to cast while irradiated.
				}
				else
				{
					if (!radSquad)
					{
						_squadData.createSquad("Irradiated", IrradiatedPriority).getOrder().setStatus("Ouch!");
						radSquad = &_squadData.getSquad("Irradiated");
					}

					_squadData.assignUnitToSquad(unit, *radSquad);		// should be always possible
				}
			}
        }
        else if (radSquad && radSquad->containsUnit(unit))
        {
            // Irradiation wore off. Reassign the unit to the Idle squad.
            // It will be reassigned onward the same frame, since this is called early.
			_squadData.assignUnitToSquad(unit, _squadData.getSquad("Idle"));
		}
    }

    if (radSquad && radSquad->isEmpty())
    {
        _squadData.removeSquad("Irradiated");
    }
}

// Put all overlords which are not otherwise assigned into the Overlord squad.
void CombatCommander::updateOverlordSquad()
{
    // If we don't have an overlord squad (because we're not zerg), then do nothing.
    // It is created in initializeSquads().
    if (!_squadData.squadExists("Overlord"))
    {
        return;
    }

    Squad & ovieSquad = _squadData.getSquad("Overlord");
    for (BWAPI::Unit unit : _combatUnits)
    {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord && _squadData.canAssignUnitToSquad(unit, ovieSquad))
        {
            _squadData.assignUnitToSquad(unit, ovieSquad);
        }
    }
}

void CombatCommander::chooseScourgeTarget(const Squad & scourgeSquad)
{
	_scourgeTarget = the.bases.myMain()->getPosition();
	BWAPI::Unit anyScourge = scourgeSquad.getAnyScourge();
	if (!anyScourge)
	{
		return;						// the scourge squad has no scourge
	}

	// The score is based on time to intercept, with adjustments. Lower is better.
    double bestScore = INT_MAX;

	BWAPI::Position bestTarget = the.bases.myMain()->getPosition();

    for (const auto & kv : the.info.getUnitData(the.enemy()).getUnits())
    {
        const UnitInfo & ui(kv.second);

        // Skip inappropriate units and units known to have moved away some time ago.
        if (!ui.type.isFlyer() ||           // excludes all buildings, including lifted buildings
            ui.type.isSpell() ||
            ui.type == BWAPI::UnitTypes::Protoss_Interceptor ||
            ui.type == BWAPI::UnitTypes::Zerg_Overlord ||
            ui.goneFromLastPosition && the.now() - ui.updateFrame > 10 * 24 ||
			ui.type == BWAPI::UnitTypes::Terran_Wraith && ui.unit && ui.unit->isVisible() && !ui.unit->isDetected())		// undetected wraith
        {
            continue;
        }

        double score = -MicroScourge::getAttackPriority(ui.type);

        if (ui.unit && ui.unit->isVisible())
        {
            score -= 2.0;
        }

		// See if we can intercept.
		BWAPI::Position target;
		double t;					// time in frames
		ui.interceptInfo(anyScourge, target, t);
		t /= 23.8;					// ~time in seconds

		if (t < 0)
		{
			// We can't intercept it.
			// But maybe head for where it was, if there is no better target.
			score += 120.0;
			target = ui.lastPosition;
		}
		else if (target.isValid())
		{
			// It's good so far. Pass the target through with its regular score.
			score += t;
		}
		else
		{
			// Maybe run to an edge, but never a corner.
			if ((target.x <= 0 || target.x >= BWAPI::Broodwar->mapWidth() - 1) &&
				(target.y <= 0 || target.y >= BWAPI::Broodwar->mapHeight() - 1))
			{
				continue;
			}

			// Clip to map limits and worsen the score.
			// NOTE Bad effects are possible, like getting stuck on an edge.
			target.makeValid();
			score += t + 12.0;
		}

		// If the target point is protected by enemy static air defense,
		// it's much less interesting, but still may be worth sticking near to.
		if (the.airHitsFixed.inRange(BWAPI::TilePosition(target)))
		{
			score += 30.0;
		}

		if (score < bestScore)
        {
            bestScore = score;
			bestTarget = target;
        }
    }

	_scourgeTarget = bestTarget;
}

// Put all scourge into the Scourge squad.
void CombatCommander::updateScourgeSquad()
{
    // If we don't have a scourge squad, then do nothing.
    // It is created in initializeSquads() for zerg only.
    if (!_squadData.squadExists("Scourge"))
    {
        return;
    }

    Squad & scourgeSquad = _squadData.getSquad("Scourge");
    for (BWAPI::Unit unit : _combatUnits)
    {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Scourge && _squadData.canAssignUnitToSquad(unit, scourgeSquad))
        {
            _squadData.assignUnitToSquad(unit, scourgeSquad);
        }
    }

    // We want an overlord to come along if the enemy has arbiters or cloaked wraiths,
    // but only if we have overlord speed.
    bool wantDetector =
        the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) != 0 &&
        the.info.enemyHasAirCloakTech();
    maybeAssignDetector(scourgeSquad, wantDetector);

	if (scourgeSquad.isEmpty())
	{
		return;
	}

	// Choose a target and figure out where to intercept it.
    chooseScourgeTarget(scourgeSquad);

    // Issue the order.
    scourgeSquad.setOrder(SquadOrder(SquadOrderTypes::OmniAttack, _scourgeTarget, 300, false, _scourgeTarget == the.bases.myMain()->getPosition() ? "Stand by" : "Chase"));
}

// Comparison function for a sort done below.
bool compareFirst(const std::pair<int, Base *> & left, const std::pair<int, Base *> & right)
{
    return left.first < right.first;
}

// Update the minelaying squad, the squad of vultures that are laying spider mines
// in predetermined positions.
void CombatCommander::updateMineSquad()
{
	std::string squadName = "Minelaying";

	// TODO Disabled until the crashing bug affecting BASIL is fixed.
	//      Crash every time when the 1st or 2nd fixed mine is laid. Symptoms say bad pointer.
	//      Since it works perfectly for me and all pointers test as good, that may take time.
	return;

	if (the.selfRace() != BWAPI::Races::Terran || !the.self()->hasResearched(BWAPI::TechTypes::Spider_Mines))
	{
		return;
	}

	if (the.ops.allHands())
	{
		// All hands are needed. Drop the minelaying squad.
		if (_squadData.squadExists(squadName))
		{
			_squadData.removeSquad(squadName);
		}
		return;
	}

	// We need the squad object. Create it if it's not already there.
	if (!_squadData.squadExists(squadName))
	{
		_squadData.createSquad(squadName, MinePriority);
	}

	Squad & mineSquad = _squadData.getSquad(squadName);
	mineSquad.setOrder(SquadOrder(SquadOrderTypes::LayMines, "at work"));

	// Remove vultures that have laid all their mines from the squad.
	for (BWAPI::Unit u : mineSquad.getUnits())
	{
		if (u->getSpiderMineCount() == 0)
		{
			mineSquad.removeUnit(u);
			break;		// one at a time is enough
		}
	}
	
	// Assign vultures with mines to the squad, up to a limit.
	// Assign at most 1/5 of combat units to lay mines.
	size_t minVultures = _combatUnits.size() / 5;
	size_t nVultures =
		std::min(minVultures, size_t(std::max(1, the.my.completed.count(BWAPI::UnitTypes::Terran_Factory))));
	if (mineSquad.getUnits().size() < nVultures)
	{
		for (BWAPI::Unit u : _combatUnits)
		{
			if (u->getType() == BWAPI::UnitTypes::Terran_Vulture &&
				u->getSpiderMineCount() > 0 &&
				_squadData.canAssignUnitToSquad(u, mineSquad))
			{
				_squadData.assignUnitToSquad(u, mineSquad);
				if (mineSquad.getUnits().size() >= nVultures)
				{
					break;
				}
			}
		}
	}

	if (!mineSquad.getUnits().empty())
	{
		BWAPI::Unit vulture = *mineSquad.getUnits().begin();
		if (!the.mines.getJob(vulture))
		{
			// There are no fixed jobs available. Disband the squad.
			_squadData.removeSquad(squadName);
		}
	}
}

// Update the watch squads, which set a sentry in each free base to see enemy expansions
// and possibly stop them. It clears spider mines as a side effect (perhaps not reliably).
// One squad per base being watched: A free base may get a watch squad of 1 zergling.
// For now, only zerg keeps watch squads, and only when units are available.
void CombatCommander::updateWatchSquads()
{
    // Only if we're zerg. Not implemented for other races.
    if (the.selfRace() != BWAPI::Races::Zerg)
    {
        return;
    }

    // We choose bases to watch relative to the enemy start, so we must know it first.
    if (!the.bases.enemyStart())
    {
        return;
    }

    // What to assign to Watch squads.
    const bool hasBurrow = the.self()->hasResearched(BWAPI::TechTypes::Burrowing);
    const int nLings = the.my.completed.count(BWAPI::UnitTypes::Zerg_Zergling);
    const int groundStrength =
        nLings +
        the.my.completed.count(BWAPI::UnitTypes::Zerg_Hydralisk) +
        2 * the.my.completed.count(BWAPI::UnitTypes::Zerg_Lurker) +
        3 * the.my.completed.count(BWAPI::UnitTypes::Zerg_Ultralisk);
    const int perWatcher = (hasBurrow && the.enemyRace() != BWAPI::Races::Zerg) ? 9 : 12;
    if (nLings == 0 || the.bases.freeLandBaseCount() == 0)
    {
        // We have either nothing to watch with, or nothing to watch over.
        _isWatching = false;
    }

    // When _isWatching is set, we ensure at least one watching zergling (if any exist).
    // Otherwise we might disband the most important squad intermittently, losing its value.
    int nWatchers = std::min(nLings, Clip(groundStrength / perWatcher, _isWatching ? 1 : 0, hasBurrow ? 4 : 2));

    // Sort free bases by nearness to enemy main, which must be known (we check above).
    // Distance scores for good bases, score -1 for others.
    // NOTE If the enemy main is destroyed, it becomes the top priority for watching.
    std::vector<std::pair<int, Base *>> baseScores;
    for (Base * base : the.bases.getAll())
    {
        if (nWatchers > 0 &&
            base->getOwner() == the.neutral() &&
            the.bases.connectedToStart(base->getTilePosition()) &&
            !base->isReserved() &&
            !the.placer.isReserved(base->getTilePosition()) &&
            !BuildingManager::Instance().isBasePlanned(base) &&
            the.groundHitsFixed.at(base->getCenterTile()) == 0)
        {
            baseScores.push_back(std::pair<int, Base *>(base->getTileDistance(the.bases.enemyStart()->getTilePosition()), base));
        }
        else
        {
            baseScores.push_back(std::pair<int, Base *>(-1, base));
        }
    }
    std::stable_sort(baseScores.begin(), baseScores.end(), compareFirst);
    
	// Iterate through all bases, from highest to lowest score.
	// Add or remove the sentry for each base, if needed.
    for (std::pair<int, Base *> baseScore : baseScores)
    {
        Base * base = baseScore.second;
        int score = baseScore.first;

        std::stringstream squadName;
        BWAPI::TilePosition tile(base->getTilePosition() + BWAPI::TilePosition(2, 1));
        squadName << "Watch " << tile.x << "," << tile.y;

        if (score < 0 && !_squadData.squadExists(squadName.str()))
        {
            continue;
        }

        // We need the squad object. Create it if it's not already there.
		if (!_squadData.squadExists(squadName.str()))
		{
			_squadData.createSquad(squadName.str(), WatchPriority);
		}
		Squad & watchSquad = _squadData.getSquad(squadName.str());

		// The order can change, so update it every time.
		if (Config::Skills::HumanOpponent && hasBurrow)
		{
			watchSquad.setOrder(SquadOrder(SquadOrderTypes::Watch, getHiddenWatchLocation(base), 0, true, "Watch"));
		}
		else
		{
			watchSquad.setOrder(SquadOrder(SquadOrderTypes::Watch, base, 0, true, "Watch"));
		}
		watchSquad.setCombatSimRadius(128);     // small radius
        watchSquad.setFightVisible(true);       // combat sim sees only visible enemy units (not all known enemies)

        // Add or remove the squad's watcher unit, or sentry.
        bool hasWatcher = watchSquad.containsUnitType(BWAPI::UnitTypes::Zerg_Zergling);
        if (hasWatcher)
        {
            if (score < 0 || nWatchers <= 0)
            {
                // Has watcher and should not.
                for (BWAPI::Unit unit : watchSquad.getUnits())
                {
                    if (unit->getType() == BWAPI::UnitTypes::Zerg_Zergling)
                    {
                        watchSquad.removeUnit(unit);
                        break;
                    }
                }
            }
            else
            {
                // Has watcher as it should. Count it.
                --nWatchers;
            }
        }
        else
        {
            if (score >= 0 && nWatchers > 0)
            {
                // Has no watcher and should have one.
                for (BWAPI::Unit unit : _combatUnits)
                {
                    if (unit->getType() == BWAPI::UnitTypes::Zerg_Zergling &&
                        _squadData.canAssignUnitToSquad(unit, watchSquad) &&
						unit->getTilePosition().isValid() &&						// not loaded in an overlord
						base->getSafeTileDistance(unit->getTilePosition()) >= 0)	// can reach target location safely
                    {
                        _squadData.assignUnitToSquad(unit, watchSquad);
                        --nWatchers;		// we used one up
                        // If we have burrow, we want to keep watching at least one neutral base. Flag it.
                        if (hasBurrow)
                        {
                            _isWatching = true;
                        }
                        break;
                    }
                }
            }
        }

        // Drop the squad if it is no longer needed. Don't clutter the squad display.
        if (watchSquad.isEmpty())
        {
            _squadData.removeSquad(squadName.str());
        }
    }
}

// Destroy undefended enemy buildings with minimal force.
void CombatCommander::updateClearanceSquad()
{
	const std::string squadName = "Clearance";

	// Only if we're zerg. Not implemented for other races.
	if (the.selfRace() != BWAPI::Races::Zerg)
	{
		return;
	}

	if (the.ops.allHands())
	{
		// All hands are needed. Drop the clearance squad.
		if (_squadData.squadExists(squadName))
		{
			_squadData.removeSquad(squadName);
		}
		return;
	}

	if (!isValidClearanceTarget())
	{
		_clearanceTarget = nullptr;
	}

	// If the squad is created, it will need >= 2 lings. Leave some over.
	const int nLings = the.my.completed.count(BWAPI::UnitTypes::Zerg_Zergling);

	bool assignSquad = nLings >= 5;
	bool urgent = nLings < 16;		// if we don't have much, only seek to clear urgent targets
	if (assignSquad && !_clearanceTarget)
	{
		_clearanceTarget = findClearanceSquadTarget(urgent);
	}

	if (!assignSquad || !_clearanceTarget)
	{
		if (_squadData.squadExists(squadName))
		{
			_squadData.removeSquad(squadName);
		}
		return;
	}

	Squad & clearanceSquad = _squadData.squadExists(squadName)
		? _squadData.getSquad(squadName)
		: _squadData.createSquad(squadName, ClearancePriority);

	// `urgent` means we don't have many lings, and need to keep to priority targets.
	size_t squadSize = urgent ? 2 : std::min(8, nLings / 4);

	// Assign zerglings to bring the squad up to full size.
	// NOTE We don't release zerglings if the squad is bigger than it should be.
	if (clearanceSquad.getUnits().size() < squadSize)
	{
		for (BWAPI::Unit unit : _combatUnits)
		{
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Zergling &&
				_squadData.canAssignUnitToSquad(unit, clearanceSquad))
			{
				_squadData.assignUnitToSquad(unit, clearanceSquad);
				if (clearanceSquad.getUnits().size() >= squadSize)
				{
					break;
				}
			}
		}
		if (!clearanceSquad.isEmpty())
		{
			const UnitInfo * ui = the.info.getUnitData(the.enemy()).getUnitInfo(_clearanceTarget);		// isValidClearanceTarget() says it's safe
			clearanceSquad.setOrder(SquadOrder(SquadOrderTypes::Attack, ui->lastPosition, 5 * 32, "Clear obstacle"));
		}
	}

	// Drop the squad if it is no longer needed. Don't clutter the squad display.
	// It'll be empty if all zerglings had higher-priority jobs.
	if (clearanceSquad.isEmpty())
	{
		_squadData.removeSquad(squadName);
	}
}

// Check whether the current clearance target is (still) good, or needs to change.
bool CombatCommander::isValidClearanceTarget() const
{
	// If it's not set, or if it's a building that has lifted off, it's invalid.
	if (!_clearanceTarget || _clearanceTarget->isFlying())
	{
		return false;
	}

	const UnitInfo * ui = the.info.getUnitData(the.enemy()).getUnitInfo(_clearanceTarget);
	return
		ui &&
		(!ui->goneFromLastPosition || ui->updateFrame >= the.now() - 36) &&
		the.bases.myMain()->getSafeTileDistance(BWAPI::TilePosition(ui->lastPosition)) >= 0;
}

// Find a target for the clearance squad to clear out of the way.
// The target is an undefended enemy building in an inconvenient place,
// or a non-combat unit blocking the natural.
// If urgent==true, include only enemies in one of our bases.
BWAPI::Unit CombatCommander::findClearanceSquadTarget(bool urgent) const
{
	// Priority 1. Unit blocking the natural from being built.
	Base * natural = the.bases.myNatural();
	if (natural &&
		natural->getOwner() != the.self() &&
		the.bases.myMain()->getSafeTileDistance(natural->getTilePosition()) >= 0)
	{
		// It's safe to move there, so no enemy combat unit is known to be there.

		for (const auto & kv : the.info.getUnitData(the.enemy()).getUnits())
		{
			const UnitInfo & ui(kv.second);

			// NOTE An enemy can be outside the hatchery rectangle but still overlap and block it.
			//      We check a range of 3 tiles outside the rectangle to catch anything nearby.
			// NOTE Some of the expanded rectangle may be off the map. It doesn't matter.
			if ((!ui.goneFromLastPosition || ui.updateFrame >= the.now() - 36) &&		// still there or recently gone
				ui.lastPosition.x >= natural->getPosition().x - 3 * 32 &&
				ui.lastPosition.x <= natural->getPosition().x + 7 * 32 &&
				ui.lastPosition.y >= natural->getPosition().y - 3 * 32 &&
				ui.lastPosition.y <= natural->getPosition().y + 6 * 32 &&
				!ui.type.isFlyer() &&													// not an air unit
				!ui.lifted &&															// not a floating building
				!ui.type.isSpell())
			{
				//BWAPI::Broodwar->printf("clearance squad natural target %s", ui.type.getName().c_str());
				return ui.unit;
			}
		}
	}

	// Priority 2. Building in any of our bases (urgent).
	// Priority 3. Undefended enemy building elsewhere on the map (not urgent).
	BWAPI::Unit elsewhere = nullptr;
	for (const auto & kv : the.info.getUnitData(the.enemy()).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isBuilding() &&
			!ui.goneFromLastPosition &&
			!ui.lifted &&
			the.bases.myMain()->getSafeTileDistance(BWAPI::TilePosition(ui.lastPosition)) >= 0)		// is safely reachable
		{
			for (Base * base : the.bases.getAll())
			{
				if (base->getOwner() == the.self() && base->isInBase(ui.lastPosition))
				{
					//BWAPI::Broodwar->printf("clearance squad base target %s", ui.type.getName().c_str());
					return ui.unit;
				}
			}
			elsewhere = ui.unit;
		}
	}

	if (!urgent)
	{
		//if (elsewhere)
		//{
		//	BWAPI::Broodwar->printf("clearance squad elsewhere target %s", elsewhere->getType().getName().c_str());
		//}
		return elsewhere;
	}

	return nullptr;
}

// Update the small recon squad which tries to find and deny enemy bases.
// Units available to the recon squad each have a "weight".
// Weights sum to no more than MaxWeight, set below.
void CombatCommander::updateReconSquad()
{
	const std::string squadName("Recon");

	// The Recon squad is not needed very early in the game.
	// If rushing, we want to dedicate all units to the rush.
	if (the.now() < 6 * 24 * 60)
	{
		return;
	}

	const int MaxWeight = 11;
	Squad & reconSquad = _squadData.getSquad("Recon");

	chooseReconTarget(reconSquad);

	// If nowhere needs seeing, disband the squad. We're done for now.
	// It can happen that the Watch squad or spider mines see all neutral bases.
	// Or the same if all hands are needed for combat.
	if (!_reconTarget || the.ops.allHands())
	{
		reconSquad.clear();
		_reconSquadAlive = false;
		return;
	}

	// Issue the order.
	reconSquad.setOrder(SquadOrder(SquadOrderTypes::Attack, _reconTarget, ReconRadius, true, "Reconnaissance in force"));

	// Special case: If we started on an island, then the recon squad consists
	// entirely of one detector--an overlord if we're lucky enough to be zerg.
	if (the.bases.isIslandStart())
	{
		if (reconSquad.getUnits().size() == 0)
		{
			for (BWAPI::Unit unit : _combatUnits)
			{
				if (unit->getType().isDetector() && _squadData.canAssignUnitToSquad(unit, reconSquad))
				{
					_squadData.assignUnitToSquad(unit, reconSquad);
					break;
				}
			}
		}
		_reconSquadAlive = !reconSquad.isEmpty();
		return;
	}

	// What is already in the squad?
	int squadWeight = 0;
	int nMarines = 0;
	int nMedics = 0;
	bool hasDetector = false;
	bool hasWorker = false;
	for (BWAPI::Unit unit : reconSquad.getUnits())
	{
		squadWeight += weighReconUnit(unit);
		if (unit->getType() == BWAPI::UnitTypes::Terran_Marine)
		{
			++nMarines;
		}
		else if (unit->getType() == BWAPI::UnitTypes::Terran_Medic)
		{
			++nMedics;
		}
		else if (unit->getType().isDetector())
		{
			hasDetector = true;
		}
		else if (unit->getType().isWorker())
		{
			hasWorker = true;
		}
	}

	// Even if the squadWeight == 0, we may still have a detector. That's OK.

	// What is available to put into the squad?
	int availableWeight = 0;
	for (BWAPI::Unit unit : _combatUnits)
	{
		availableWeight += weighReconUnit(unit);
	}

	// The allowed weight of the recon squad. It should steal few units.
	int weightLimit = availableWeight >= 24
		? 2 + (availableWeight - 24) / 6
		: 0;
	if (weightLimit > MaxWeight)
	{
		weightLimit = MaxWeight;
	}

	// If the recon squad weighs more than it should, clear it and continue.
	// Also if all marines are gone, but medics remain.
	// Units will be added back in if they should be.
	if (squadWeight > weightLimit ||
		nMarines == 0 && nMedics > 0)
	{
		reconSquad.clear();
		squadWeight = 0;
		nMarines = nMedics = 0;
		hasDetector = false;
		hasWorker = false;
	}

	bool wantDetector =
		the.my.completed.count(the.info.getDetectorType()) >= 2 &&
		(the.selfRace() != BWAPI::Races::Terran || the.info.enemyHasCloakTech());

	if (hasDetector && !wantDetector)
	{
		for (BWAPI::Unit unit : reconSquad.getUnits())
		{
			if (unit->getType().isDetector())
			{
				reconSquad.removeUnit(unit);
				break;
			}
		}
		hasDetector = false;
	}

	// Add units up to the weight limit.
	// In this loop, add no medics, and few enough marines to allow for 2 medics.
	for (BWAPI::Unit unit : _combatUnits)
	{
		if (squadWeight >= weightLimit)
		{
			break;
		}
		BWAPI::UnitType type = unit->getType();
		int weight = weighReconUnit(type);
		if (weight > 0 && squadWeight + weight <= weightLimit && _squadData.canAssignUnitToSquad(unit, reconSquad))
		{
			if (type == BWAPI::UnitTypes::Terran_Marine)
			{
				if (nMarines * weight < MaxWeight - 2 * weighReconUnit(BWAPI::UnitTypes::Terran_Medic))
				{
					_squadData.assignUnitToSquad(unit, reconSquad);
					squadWeight += weight;
					nMarines += 1;
				}
			}
			else if (type != BWAPI::UnitTypes::Terran_Medic)
			{
				_squadData.assignUnitToSquad(unit, reconSquad);
				squadWeight += weight;
			}
		}
		else if (type.isDetector() && wantDetector && !hasDetector && _squadData.canAssignUnitToSquad(unit, reconSquad))
		{
			// The detector adds no weight. Code above depends on it.
			_squadData.assignUnitToSquad(unit, reconSquad);
			hasDetector = true;
		}
	}

	// Finally, fill in any needed medics.
	if (nMarines > 0 && nMedics < 2)
	{
		for (BWAPI::Unit unit : _combatUnits)
		{
			if (squadWeight >= weightLimit || nMedics >= 2)
			{
				break;
			}
			if (unit->getType() == BWAPI::UnitTypes::Terran_Medic &&
				_squadData.canAssignUnitToSquad(unit, reconSquad))
			{
				_squadData.assignUnitToSquad(unit, reconSquad);
				squadWeight += weighReconUnit(BWAPI::UnitTypes::Terran_Medic);
				nMedics += 1;
			}
		}
	}

	bool wantWorker =
		squadWeight == 0 &&
		the.selfRace() != BWAPI::Races::Zerg &&
		the.my.completed.count(the.selfRace().getWorker()) > 22;

	// If we add a worker, it may stick around even after we don't need it.
	// That's OK.
	if (wantWorker && !hasWorker)
	{
		BWAPI::Unit worker = WorkerManager::Instance().getCombatWorker();
		if (worker)
		{
			_squadData.assignUnitToSquad(worker, reconSquad);
		}
	}

	_reconSquadAlive = !reconSquad.isEmpty();
}

// The recon squad is allowed up to a certain "weight" of units.
int CombatCommander::weighReconUnit(const BWAPI::Unit unit) const
{
    return weighReconUnit(unit->getType());
}

// The recon squad is allowed up to a certain "weight" of units.
int CombatCommander::weighReconUnit(const BWAPI::UnitType type) const
{
	if (type == BWAPI::UnitTypes::Terran_Marine) return 2;
	if (type == BWAPI::UnitTypes::Terran_Medic) return 2;
	if (type == BWAPI::UnitTypes::Terran_Vulture) return 4;
	if (type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) return 6;
	if (type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) return 6;

	if (type == BWAPI::UnitTypes::Protoss_Zealot) return 4;
	if (type == BWAPI::UnitTypes::Protoss_Dragoon) return 4;
	if (type == BWAPI::UnitTypes::Protoss_Dark_Templar) return 6;

	if (type == BWAPI::UnitTypes::Zerg_Zergling) return 2;
    if (type == BWAPI::UnitTypes::Zerg_Hydralisk) return 3;

    return 0;
}

// Keep the same reconnaissance target or switch to a new one, depending.
void CombatCommander::chooseReconTarget(const Squad & reconSquad)
{
	bool change = false;       // switch targets?

    Base * nextTarget = getReconLocation();
	const bool enemyHasMines = the.info.enemyHasMines() || the.info.enemyHasBurrow();

    // There is nowhere that we need to see. Change to the invalid target.
    if (!nextTarget)
    {
        change = true;
    }

    // If the current target is invalid, find one.
    else if (!_reconTarget)
    {
        change = true;
    }

    // If we have spent too long on one target, then probably we haven't been able to reach it.
    else if (the.now() - _lastReconTargetChange >= ReconTargetTimeout)
    {
        change = true;
    }

    // The recon squad just lost all its members, a sign that it was going somewhere too dangerous.
    // Though it could be that they were all assigned back to the Ground squad.
    else if (_reconSquadAlive && reconSquad.isEmpty())
    {
        change = true;
    }

	// Two cases:
	// 1. If the enemy has mines, move to trigger any mine that may be at the target location.
	// 2. Otherwise, if the target is in sight of any unit and empty of enemies, we're done.
    else if (
		enemyHasMines && reconSquad.isAtOrderPosition() ||
		!enemyHasMines && BWAPI::Broodwar->isVisible(_reconTarget->getCenterTile()))
	{
		bool noChange = false;
        BWAPI::Unitset enemies;
        MapGrid::Instance().getUnits(enemies, _reconTarget->getCenter(), ReconRadius, false, true);
        // We don't particularly care about air units, even when we could engage them.
        for (auto it = enemies.begin(); it != enemies.end(); ++it)
        {
            if (!(*it)->isFlying())
            {
				noChange = true;
				break;
            }
        }
        if (!noChange)
        {
            change = true;
        }
    }

    if (change)
    {
        _reconTarget = nextTarget;
        _lastReconTargetChange = the.now();
    }
}

// Choose an empty base location for the recon squad to check out, or null if none.
// Called only by setReconTarget().
Base * CombatCommander::getReconLocation() const
{
    std::vector<Base *> choices;

    // The choices are neutral bases reachable by ground and not currently in view.
    // Or, if we started on an island, the choices are neutral bases anywhere.
    for (Base * base : the.bases.getAll())
    {
        if (base->getOwner() == the.neutral() &&
            !base->isVisible() &&
            (the.bases.isIslandStart() || the.bases.connectedToStart(base->getTilePosition())))
        {
            choices.push_back(base);
        }
    }

    if (choices.empty())
    {
        return nullptr;
    }

    // Choose randomly.
    // We may choose the same target we already have. That's OK; if there's another choice,
    // we'll probably switch to it soon.
    Base * base = choices.at(Random::Instance().index(choices.size()));
    return base;
}

// Form the ground squad and the flying squad, the main attack squads.
// NOTE Arbiters and guardians go into the ground squad.
//      Devourers are flying squad if it exists, otherwise ground.
//		Carriers are flying squad if it already exists or if the carrier count is high enough.
//      Detectors are assigned specially.
//		Scourge are in their own squad.
//      Other air units always go into the flying squad.
void CombatCommander::updateAttackSquads()
{
    Squad & groundSquad = _squadData.getSquad("Ground");
    Squad & flyingSquad = _squadData.getSquad("Flying");

    // _carrierCount is used only here and in dependencies.
    _carrierCount = the.my.completed.count(BWAPI::UnitTypes::Protoss_Carrier);

    bool flyingSquadExists = false;
    for (BWAPI::Unit unit : flyingSquad.getUnits())
    {
        if (isFlyingSquadUnit(unit->getType()))
        {
            flyingSquadExists = true;
            break;
        }
    }

    for (BWAPI::Unit unit : _combatUnits)
    {
        if (isFlyingSquadUnit(unit->getType()))
        {
            if (_squadData.canAssignUnitToSquad(unit, flyingSquad))
            {
                _squadData.assignUnitToSquad(unit, flyingSquad);
            }
        }

        // Certain flyers go into the flying squad only if it already exists.
        // Otherwise they go into the ground squad.
        else if (isOptionalFlyingSquadUnit(unit->getType()))
        {
            if (flyingSquadExists)
            {
                if (groundSquad.containsUnit(unit))
                {
                    groundSquad.removeUnit(unit);
                }
                if (_squadData.canAssignUnitToSquad(unit, flyingSquad))
                {
                    _squadData.assignUnitToSquad(unit, flyingSquad);
                }
            }
            else
            {
                if (flyingSquad.containsUnit(unit))
                {
                    flyingSquad.removeUnit(unit);
                }
                if (_squadData.canAssignUnitToSquad(unit, groundSquad))
                {
                    _squadData.assignUnitToSquad(unit, groundSquad);
                }
            }
        }

        // isGroundSquadUnit() is defined as a catchall, so it has to go last.
        else if (isGroundSquadUnit(unit->getType()))
        {
            if (_squadData.canAssignUnitToSquad(unit, groundSquad))
            {
                _squadData.assignUnitToSquad(unit, groundSquad);
            }
        }
    }

    // Add or remove detectors.
    bool wantDetector = wantSquadDetectors();
    maybeAssignDetector(groundSquad, wantDetector);
    maybeAssignDetector(flyingSquad, wantDetector);

    groundSquad.setOrder(getAttackOrder(&groundSquad));
    //groundSquad.setLurkerTactic(_lurkerOrders.generalTactic);

    flyingSquad.setOrder(getAttackOrder(&flyingSquad));
}

// Unit definitely belongs in the Flying squad.
bool CombatCommander::isFlyingSquadUnit(const BWAPI::UnitType type) const
{
    return
        type == BWAPI::UnitTypes::Zerg_Mutalisk ||
        type == BWAPI::UnitTypes::Terran_Wraith ||
        type == BWAPI::UnitTypes::Terran_Valkyrie ||
        type == BWAPI::UnitTypes::Terran_Battlecruiser ||
        type == BWAPI::UnitTypes::Protoss_Corsair ||
        type == BWAPI::UnitTypes::Protoss_Scout ||
        _carrierCount >= CarrierIndependenceCount && type == BWAPI::UnitTypes::Protoss_Carrier;
}

// Unit belongs in the Flying squad if the Flying squad exists, otherwise the Ground squad.
// If carriers are independent, then the flying squad exists.
bool CombatCommander::isOptionalFlyingSquadUnit(const BWAPI::UnitType type) const
{
    return
        type == BWAPI::UnitTypes::Zerg_Devourer ||
        type == BWAPI::UnitTypes::Protoss_Carrier;
}

// Unit belongs in the ground squad.
// With the current definition, it includes everything except workers, so it captures
// everything that is not already taken. It must be the last condition checked.
// NOTE This includes guardians and queens.
bool CombatCommander::isGroundSquadUnit(const BWAPI::UnitType type) const
{
    return
        !type.isDetector() &&
        !type.isWorker();
}

// Despite the name, this supports only 1 drop squad which has 1 transport.
// Furthermore, it can only drop once and doesn't know how to reset itself to try again.
// Still, it's a start and it can be deadly.
void CombatCommander::updateDropSquads()
{
    // If we don't have a drop squad, then we don't want to drop.
    // It is created in initializeSquads().
    if (!_squadData.squadExists("Drop"))
    {
        return;
    }

    Squad & dropSquad = _squadData.getSquad("Drop");

    // The squad is initialized with a Hold order.
    // There are 3 phases, and in each phase the squad is given a different order:
    // Collect units (Hold); load the transport (Load); go drop (Drop).

    if (dropSquad.getOrder().getType() == SquadOrderTypes::Drop)
    {
		// Update the order in case the drop location has changed.
		// We don't always know the enemy base location until we get there.
		dropSquad.setOrder(SquadOrder(SquadOrderTypes::Drop, getDropLocation(dropSquad), 300, false, "Go drop!"));

		// The squad takes care of the rest.
        return;
    }

    // If we get here, we haven't been ordered to Drop yet.

    // What units do we have, what units do we need?
    BWAPI::Unit transportUnit = nullptr;
    int transportSpotsRemaining = 8;      // all transports are the same size
    bool anyUnloadedUnits = false;
    const auto & dropUnits = dropSquad.getUnits();

	// Recognize the transport unit, and count transportSpotsRemaining.
    for (BWAPI::Unit unit : dropUnits)
    {
        if (unit->exists())
        {
            if (unit->isFlying() && unit->getType().spaceProvided() > 0)
            {
                transportUnit = unit;
            }
            else
            {
                transportSpotsRemaining -= unit->getType().spaceRequired();
                if (!unit->isLoaded())
                {
                    anyUnloadedUnits = true;
                }
            }
        }
    }

    if (transportUnit && transportSpotsRemaining == 0)
    {
		// The drop squad has all its units.
        if (anyUnloadedUnits)
        {
            // The drop squad is complete. Load up.
			// If we're zerg, we have to wait for the drop upgrade to finish.
            // See Squad::loadTransport().
			if (the.selfRace() != BWAPI::Races::Zerg ||
				the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Ventral_Sacs) > 0)
			{
				dropSquad.setOrder(SquadOrder(SquadOrderTypes::Load, transportUnit->getPosition(), AttackRadius, false, "Load up"));
			}
        }
        else
        {
            // We're full. Change the order to Drop.
            dropSquad.setOrder(SquadOrder(SquadOrderTypes::Drop, getDropLocation(dropSquad), 300, false, "Go drop!"));
        }
    }
    else
    {
        // The drop squad is not complete. Look for more units.
        for (BWAPI::Unit unit : _combatUnits)
        {
            // If the squad doesn't have a transport, try to add one.
            if (!transportUnit &&
                unit->getType().spaceProvided() > 0 && unit->isFlying() &&
				(the.selfRace() != BWAPI::Races::Zerg || the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Ventral_Sacs) > 0) &&
                _squadData.canAssignUnitToSquad(unit, dropSquad))
            {
                _squadData.assignUnitToSquad(unit, dropSquad);
                transportUnit = unit;
            }

            // If the unit fits and is good to drop, add it to the squad.
            // Rewrite unitIsGoodToDrop() to select the units of your choice to drop.
            // Simplest to stick to units that occupy the same space in a transport, to avoid difficulties
            // like "add zealot, add dragoon, can't add another dragoon--but transport is not full, can't go".
            else if (unit->getType().spaceRequired() <= transportSpotsRemaining &&
                unitIsGoodToDrop(dropSquad, unit) &&
                _squadData.canAssignUnitToSquad(unit, dropSquad))
            {
                _squadData.assignUnitToSquad(unit, dropSquad);
                transportSpotsRemaining -= unit->getType().spaceRequired();
            }
        }
    }
}

// Handle early defense with workers, covering the main and natural.
// The squad is created (empty) at the start of the game.
// It is disbanded permanently when the first real combat unit is complete.
void CombatCommander::updateEarlyDefenseSquad()
{
	if (!_squadData.squadExists("EarlyDefense"))
	{
		return;		// squad has been disbanded
	}
	
	// Disband the squad if combat units are out.
	if (the.info.weHaveCombatUnits())
	{
		_squadData.removeSquad("EarlyDefense");
		return;
	}

	Squad & squad = _squadData.getSquad("EarlyDefense");

	// Get the region of our starting base.
    const int myZone = the.zone.at(the.bases.myStart()->getTilePosition());

	// Count these things.
	// The regular defense squad handles ZvZ early defense needs.
	int nEnemyWorkers = 0;
	int nEnemyMarines = 0;
	int nEnemyBarracks = 0;				// includes gateways
	int nEnemyEmptyBunkers = 0;			// empty or unfinished
	int nEnemyPylons = 0;
	int nEnemyUnfinishedCannons = 0;

    // Get all of the known enemy units in this region.
	// Units that are defended (we have no safe path to them) are excluded.
    std::set<const UnitInfo *> enemyUnitsInRegion;
	int closestEnemyDist = MAX_DISTANCE;
	const UnitInfo * closestEnemy = nullptr;
	for (const auto & kv : the.info.getUnitInfo(the.enemy()))
	{
		const UnitInfo & ui = kv.second;

		if (!ui.goneFromLastPosition && myZone == the.zone.at(ui.lastPosition))
        {
			// Reachable units (not defended by e.g. cannons), and not TOO far away.
			int dist = the.bases.myMain()->getSafeTileDistance(ui.lastPosition);
			if (dist >= 0 && dist <= 18)
			{
				enemyUnitsInRegion.insert(&ui);

				if (dist < closestEnemyDist)
				{
					closestEnemyDist = dist;
					closestEnemy = &ui;
				}

				if (ui.type.isWorker())
				{
					++nEnemyWorkers;
				}
				else if (ui.type == BWAPI::UnitTypes::Terran_Marine)
				{
					++nEnemyMarines;
				}
				else if (ui.type == BWAPI::UnitTypes::Terran_Barracks && !ui.lifted ||
					ui.type == BWAPI::UnitTypes::Protoss_Gateway)
				{
					++nEnemyBarracks;
				}
				else if (ui.type == BWAPI::UnitTypes::Terran_Bunker &&
					(!ui.isCompleted() || !the.info.isEnemyBunkerLoaded(ui.unit)))
				{
					++nEnemyEmptyBunkers;
				}
				else if (ui.type == BWAPI::UnitTypes::Protoss_Pylon)
				{
					++nEnemyPylons;
				}
				else if (ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon && !ui.isCompleted())
				{
					++nEnemyUnfinishedCannons;
				}
			}
		}
    }

	// How many workers to pull for defense?
	size_t assignDefenders = 0;
	// Go for an enemy unit if there is one, otherwise an enemy pylon, otherwise another enemy building.
	if (nEnemyWorkers == 1 &&
		enemyUnitsInRegion.size() == 1)
	{
		// Defend against a single worker scout only as configured.
		if (Config::Micro::ScoutDefenseRadius > 0 &&
			Config::Micro::ScoutDefenseRadius > the.bases.myMain()->getCenter().getApproxDistance(closestEnemy->lastPosition))
		{
			assignDefenders = 1;
		}
	}
	else if (closestEnemy)
	{
		// Otherwise defend fully.
		assignDefenders =
			nEnemyWorkers + (nEnemyWorkers > 1 ? 1 : 0) +
			2 * nEnemyMarines + 4 * nEnemyBarracks + 4 * nEnemyEmptyBunkers +
			3 * nEnemyPylons + 3 * nEnemyUnfinishedCannons;
	}

	// Pull or release workers.
	if (assignDefenders <= 0)
	{
		// The squad should be empty. Make it so.
		squad.clear();
	}
	else
    {
		squad.setOrder(SquadOrder(SquadOrderTypes::Attack, closestEnemy->lastPosition, 7 * 32, "go!"));

		// Not enough. Assign more.
		while (squad.getUnits().size() < assignDefenders)
        {
            BWAPI::Unit workerDefender = findClosestWorkerToTarget(_combatUnits, closestEnemy->lastPosition);

            if (workerDefender)
            {
                if (_squadData.canAssignUnitToSquad(workerDefender, squad))
                {
                    _squadData.assignUnitToSquad(workerDefender, squad);
                }
				else
				{
					break;
				}
            }
			else
			{
				break;
			}
        }

		// Too many. Release some.
		while (squad.getUnits().size() > assignDefenders)
		{
			BWAPI::Unit weakest = nullptr;
			int weakestHP = 100;		// more than any worker
			for (BWAPI::Unit u : squad.getUnits())
			{
				int hp = u->getHitPoints() + u->getShields();
				if (hp < weakestHP)
				{
					weakest = u;
					weakestHP = hp;
				}
			}
			if (weakest)
			{
				squad.removeUnit(weakest);
			}
			else
			{
				break;		// defensive programming: no infinite loop
			}
		}
	}
}

void CombatCommander::updateBaseDefenseSquads()
{
    const int baseDefenseRadius = 19 * 32;
    const int baseDefenseHysteresis = 10 * 32;
    const int pullWorkerDistance = 8 * 32;
    const int pullWorkerVsBuildingDistance = baseDefenseRadius;
    const int pullWorkerHysteresis = 4 * 32;

	// NOTE This delay feature ameliorates some problems but causes others.
	//      That's why the duration is only 1 second.
	const int extraFrames = 1 * 24;     // stay this long after the last enemy is gone

	if (_combatUnits.empty())
    { 
        return; 
    }

	for (Base * base : the.bases.getAll())
	{
		std::stringstream squadName;
		squadName << "Base " << base->getTilePosition().x << "," << base->getTilePosition().y;

		// Don't defend inside an enemy base.
		// It will end badly when we are stealing gas or otherwise proxying.
		// Don't defend a neutral base where we own "too few" buildings.
		if (base->getOwner() == the.enemy() ||
			base->getOwner() != the.self() && base->getBuildingsCost() < 800)
		{
			// Clear any defense squad.
			if (_squadData.squadExists(squadName.str()))
			{
				_squadData.removeSquad(squadName.str());
			}
			continue;
		}

		// Start to defend when enemy comes within baseDefenseRadius.
		// Stop defending when enemy leaves baseDefenseRadius + baseDefenseHysteresis.
		const int defenseRadius = _squadData.squadExists(squadName.str())
			? baseDefenseRadius + baseDefenseHysteresis
			: baseDefenseRadius;

		const Zone * zone = the.zone.ptr(base->getTilePosition());
		UAB_ASSERT(zone, "bad base location");

		// Assume for now that the base is not in danger.
		// We may prove otherwise below.
		base->setUnderAttack(false);
		base->setOverlordDanger(false);
		base->setWorkerDanger(false);
		base->setDoomed(false);

		// Find any enemy units that are bothering us.
		// Also note how far away the closest one is.
		int closestEnemyDistance = MAX_DISTANCE;
		BWAPI::Unit closestEnemy = nullptr;
		BWAPI::Unit closestRealEnemy = nullptr;		// exclude harmless flyers like overlords
		int nEnemySupply = 0;
		int nEnemyWorkers = 0;
		int nEnemyGround = 0;
		int nEnemyAir = 0;
		int nEnemyAirToAir = 0;						// subset of nEnemyAir units
		bool enemyHitsGround = false;
		bool enemyHitsAir = false;
		bool enemyHasCloak = false;

		for (BWAPI::Unit unit : the.enemy()->getUnits())
		{
			BWAPI::UnitType type = unit->getType();
			if (unit->isInvincible() || type.isSpell())
			{
				continue;
			}
			const int dist = unit->getDistance(base->getCenter());
			if (dist < defenseRadius ||
				dist < defenseRadius + 384 && zone == the.zone.ptr(unit->getTilePosition()))
			{
				if (type == BWAPI::UnitTypes::Protoss_Photon_Cannon && unit->isPowered() &&
					the.selfRace() == BWAPI::Races::Zerg)
				{
					// Do not assign defense against cannons.
					// Defensive sunkens will hold them. It's better to go to the enemy main.
					continue;
				}
				if (dist < closestEnemyDistance)
				{
					closestEnemyDistance = dist;
					closestEnemy = unit;

					// Specific air units are harmless. All ground units are "real" enemies, including e.g. supply depots.
					if (!unit->isFlying() ||
						!(type.isBuilding() || type == BWAPI::UnitTypes::Protoss_Observer || type == BWAPI::UnitTypes::Zerg_Overlord))
					{
						closestRealEnemy = unit;
					}
				}

				// Count some non-attack buildings (not overlords) for later. We'll treat them specially,
				// to leave more units free to attack the enemy main.
				if (type == BWAPI::UnitTypes::Terran_Supply_Depot ||
					type == BWAPI::UnitTypes::Terran_Engineering_Bay ||
					type == BWAPI::UnitTypes::Protoss_Pylon ||
					!unit->isPowered())
				{
					++nEnemySupply;
				}
				else if (type.isWorker())
				{
					++nEnemyWorkers;
				}
				else if (unit->isFlying())
				{
					if (type == BWAPI::UnitTypes::Terran_Battlecruiser ||
						type == BWAPI::UnitTypes::Protoss_Arbiter)
					{
						// NOTE Carriers don't need extra, they show interceptors.
						nEnemyAir += 4;
					}
					else if (type == BWAPI::UnitTypes::Protoss_Scout)
					{
						nEnemyAir += 3;
					}
					else if (type == BWAPI::UnitTypes::Zerg_Guardian ||
						type == BWAPI::UnitTypes::Zerg_Devourer)
					{
						nEnemyAir += 2;
					}
					else
					{
						++nEnemyAir;

						// Fast air-to-air enemies that we sometimes choose to ignore.
						if (type == BWAPI::UnitTypes::Terran_Valkyrie ||
							type == BWAPI::UnitTypes::Protoss_Corsair ||
							type == BWAPI::UnitTypes::Zerg_Scourge)
						{
							++nEnemyAirToAir;
						}
					}
				}
				else
				{
					// Workers don't count as ground units here.
					if (type == BWAPI::UnitTypes::Terran_Goliath ||
						type == BWAPI::UnitTypes::Protoss_Zealot ||
						type == BWAPI::UnitTypes::Protoss_Dragoon ||
						type == BWAPI::UnitTypes::Protoss_Dark_Templar ||
						type == BWAPI::UnitTypes::Zerg_Lurker ||
						type == BWAPI::UnitTypes::Zerg_Creep_Colony)
					{
						nEnemyGround += 2;
					}
					else if (type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
						type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
						type == BWAPI::UnitTypes::Protoss_Archon ||
						type == BWAPI::UnitTypes::Protoss_Reaver ||
						type == BWAPI::UnitTypes::Zerg_Ultralisk)
					{
						nEnemyGround += 4;
					}
					else
					{
						++nEnemyGround;
					}
				}
				if (UnitUtil::TypeCanAttackGround(type))
				{
					enemyHitsGround = true;
				}
				if (UnitUtil::CanAttackAir(unit))
				{
					enemyHitsAir = true;
				}
				if (unit->isBurrowed() ||
					unit->isCloaked() ||
					type.hasPermanentCloak() ||
					type == BWAPI::UnitTypes::Terran_Wraith && the.info.enemyHasAirCloakTech() ||
					type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine ||
					type == BWAPI::UnitTypes::Protoss_Arbiter ||
					type == BWAPI::UnitTypes::Zerg_Lurker ||
					type == BWAPI::UnitTypes::Zerg_Lurker_Egg)
				{
					enemyHasCloak = true;
				}
			}
		}

		// If the enemy has no dangerous ground units, assign defenders to clear enemy "supply" units instead.
		// See above for what that includes.
		if (nEnemyGround == 0)
		{
			nEnemyGround = nEnemySupply;
		}

		if (!closestEnemy &&
			_squadData.squadExists(squadName.str()) &&
			the.now() > _squadData.getSquad(squadName.str()).getTimeMark() + extraFrames)
		{
			// No enemies and the extra time margin has passed. Drop the defense squad.
			_squadData.removeSquad(squadName.str());
			continue;
		}

		// We need a defense squad. If there isn't one, create it.
		// Its goal is not the base location itself, but the enemy closest to it, to ensure
		// that defenders will get close enough to the enemy to act.
		if (closestEnemy && !_squadData.squadExists(squadName.str()))
		{
			_squadData.createSquad(squadName.str(), BaseDefensePriority);
		}
		if (!_squadData.squadExists(squadName.str()))
		{
			continue;
		}
		if (!closestEnemy)
		{
			// Retain the existing members of the squad for extraFrames in case the enemy returns.
			continue;
		}
		Squad & defenseSquad = _squadData.getSquad(squadName.str());
		if (closestRealEnemy)
		{
			// Exclude floating ebays, overlords, etc. Skip them unless they are all there is.
			closestEnemy = closestRealEnemy;
		}
		BWAPI::Position targetPosition = closestEnemy ? closestEnemy->getPosition() : base->getPosition();
		defenseSquad.setOrder(SquadOrder(SquadOrderTypes::Defend, targetPosition, defenseRadius, false, "Defend base"));
		//defenseSquad.setLurkerTactic(LurkerTactic::Aggressive);     // ignore the generalTactic
		// There is somebody to fight, so remember that the squad is active as of this frame.
		defenseSquad.setTimeMark(the.now());

		// Next, figure out how many units we need to assign.

		// A simpleminded way of figuring out how much defense we need.
		const int numDefendersPerEnemyUnit = 2;

		int flyingDefendersNeeded = numDefendersPerEnemyUnit * nEnemyAir;
		int groundDefendersNeeded = nEnemyWorkers + numDefendersPerEnemyUnit * nEnemyGround;

		// Count static defense as defenders.
		// Ignore bunkers; they're more complicated.
		// Cannons are double-counted as air and ground, which can be a mistake.
		bool sunkenDefender = false;
		for (BWAPI::Unit unit : the.self()->getUnits())
		{
			if ((unit->getType() == BWAPI::UnitTypes::Terran_Missile_Turret ||
				unit->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony) &&
				zone == the.zone.ptr(unit->getTilePosition()) &&
				unit->isCompleted() && unit->isPowered())
			{
				flyingDefendersNeeded -= 3;
			}
			if ((unit->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
				unit->isCompleted() && unit->isPowered() &&
				zone == the.zone.ptr(unit->getTilePosition()))
			{
				sunkenDefender = true;
				groundDefendersNeeded -= 4;
			}
		}

		// Don't let the number of defenders go negative.
		flyingDefendersNeeded = nEnemyAir ? std::max(flyingDefendersNeeded, 2) : 0;
		if (nEnemyGround > 0)
		{
			groundDefendersNeeded = std::max(groundDefendersNeeded, 2 + nEnemyWorkers / 2);
		}
		else if (nEnemyWorkers > 0)
		{
			// Workers only, no other attackers.
			groundDefendersNeeded = std::max(groundDefendersNeeded, 1 + nEnemyWorkers / 2);
		}
		else
		{
			groundDefendersNeeded = 0;
		}

		// Drop unneeded defenders.
		if (groundDefendersNeeded <= 0 && flyingDefendersNeeded <= 0)
		{
			defenseSquad.clear();
			continue;
		}
		if (groundDefendersNeeded <= 0)
		{
			// Drop any defenders which can't shoot air.
			BWAPI::Unitset drop;
			for (BWAPI::Unit unit : defenseSquad.getUnits())
			{
				if (!unit->getType().isDetector() && !UnitUtil::TypeCanAttackAir(unit->getType()))
				{
					drop.insert(unit);
				}
			}
			for (BWAPI::Unit unit : drop)
			{
				defenseSquad.removeUnit(unit);
			}
			// And carry on. We may still want to add air defenders.
		}
		if (flyingDefendersNeeded <= 0)
		{
			// Drop any defenders which can't shoot ground.
			BWAPI::Unitset drop;
			for (BWAPI::Unit unit : defenseSquad.getUnits())
			{
				if (!unit->getType().isDetector() && !UnitUtil::TypeCanAttackGround(unit->getType()))
				{
					drop.insert(unit);
				}
			}
			for (BWAPI::Unit unit : drop)
			{
				defenseSquad.removeUnit(unit);
			}
			// And carry on. We may still want to add ground defenders.
		}

		const bool wePulledWorkers =
			std::any_of(defenseSquad.getUnits().begin(), defenseSquad.getUnits().end(), BWAPI::Filter::IsWorker);

		// Pull workers only in narrow conditions.
		// Versus units, we pull only a short distance to reduce mining losses.
		// Versus proxy buildings, we may need to pull a longer distance.
		const bool enemyProxy = buildingRush();
		const int workerDist = enemyProxy ? pullWorkerVsBuildingDistance : pullWorkerDistance;
		const bool pullWorkers =
			Config::Micro::WorkersDefendRush &&
			closestEnemyDistance <= (wePulledWorkers ? workerDist + pullWorkerHysteresis : workerDist) &&
			(enemyProxy || !sunkenDefender && numZerglingsInOurBase() > 2);

		if (wePulledWorkers && !pullWorkers)
		{
			defenseSquad.releaseWorkers();
		}

		// Don't assign hydras to defend against air-to-air units unless they're needed.
		// If there are many valkyries or corsairs, they're needed even if all overlords are protected--
		// but not needed it we have no air units there at all.
		bool skipHydras = false;
		if (nEnemyAir == nEnemyAirToAir && the.selfRace() == BWAPI::Races::Zerg)
		{
			bool anythingToDefend = false;
			for (BWAPI::Unit u : the.self()->getUnits())
			{
				if (u->isFlying() && base->isInBase(u))
				{
					anythingToDefend = true;
					if (!the.info.inAirDefenseRange(u))
					{
						skipHydras = true;
						break;
					}
				}
			}

			if (!skipHydras && anythingToDefend)
			{
				skipHydras = the.enemyRace() == BWAPI::Races::Terran && nEnemyAir >= 3 || the.enemyRace() == BWAPI::Races::Protoss && nEnemyAir >= 6;
			}
		}

        // Now find the actual units to assign.
        updateDefenseSquadUnits(defenseSquad, flyingDefendersNeeded, groundDefendersNeeded, pullWorkers, enemyHitsAir, skipHydras);

        // Assign a detector if appropriate.
        const bool wantDetector =
            !enemyHitsAir ||
            enemyHasCloak && int(defenseSquad.getUnits().size()) >= flyingDefendersNeeded + groundDefendersNeeded;
        maybeAssignDetector(defenseSquad, wantDetector);

		// If the enemy hits ground, then the base is "under attack".
		// The flag says nothing about how dangerous the attack may be. Could just be
		// an enemy worker scouting around.
		if (enemyHitsGround && groundDefendersNeeded > 0)
		{
			base->setUnderAttack(true);
		}

        // Estimate roughly whether overlords here may be in danger.
        if ((base->getDepot() ? the.airHitsFixed.inRange(base->getDepot()) : the.airHitsFixed.at(base->getCenterTile()))
                ||
            enemyHitsAir &&
            groundDefendersNeeded + flyingDefendersNeeded > 1 &&
            closestEnemyDistance <= 7 * 32 &&
            int(defenseSquad.getUnits().size()) / 2 < groundDefendersNeeded + flyingDefendersNeeded)
        {
            base->setOverlordDanger(true);
        }

        // Estimate roughly whether the workers may be in danger.
        // If they are not at immediate risk, they should keep mining and we should even be willing to transfer in more.
        if ((base->getDepot() ? the.groundHitsFixed.inRange(base->getDepot()) : the.groundHitsFixed.at(base->getCenterTile()))
                ||
            enemyHitsGround &&
            groundDefendersNeeded > 1 &&
            closestEnemyDistance <= (the.info.enemyHasSiegeMode() ? 10 * 32 : 6 * 32) &&
            int(defenseSquad.getUnits().size()) / 2 < groundDefendersNeeded + flyingDefendersNeeded)
        {
            base->setWorkerDanger(true);
        }

        // Decide whether the base cannot be saved and should be abandoned.
		// It's an extreme conclusion, be cautious in drawing it.
        // NOTE Doomed does not imply worker danger.
        // NOTE This does not disband the defense squad. It is meant to reduce wasted spending on the base.
        //      A doomed base may be saved after all, especially if the enemy messes up.
        if (enemyHitsGround &&
            groundDefendersNeeded + flyingDefendersNeeded >= 8 &&
            int(defenseSquad.getUnits().size()) * 3 < groundDefendersNeeded + flyingDefendersNeeded &&
            (closestEnemyDistance <= 6 * 32 || !base->getDepot() || the.groundHitsFixed.inRange(base->getDepot())) &&
            base->getNumUnits(UnitUtil::GetGroundStaticDefenseType(the.selfRace())) == 0)
        {
            base->setDoomed(true);
        }

        // Final check: If the squad is empty, clear it after all.
        // No advantage in having it stick around for extraFrames time.
        if (defenseSquad.getUnits().empty())
        {
            _squadData.removeSquad(squadName.str());
        }
    }
}

void CombatCommander::updateDefenseSquadUnits(Squad & defenseSquad
	, const size_t & flyingDefendersNeeded
	, const size_t & groundDefendersNeeded
	, bool pullWorkers
	, bool enemyHasAntiAir
	, bool skipHydras)
{
    const BWAPI::Unitset & squadUnits = defenseSquad.getUnits();

    // Count what is already in the squad, being careful not to double-count a unit as air and ground defender.
    size_t flyingDefendersInSquad = 0;
    size_t groundDefendersInSquad = 0;
    size_t versusBoth = 0;
    for (BWAPI::Unit defender : squadUnits)
    {
        bool versusAir = UnitUtil::TypeCanAttackAir(defender->getType());
        bool versusGround = UnitUtil::TypeCanAttackGround(defender->getType());
        if (versusGround && versusAir)
        {
            ++versusBoth;
        }
        else if (versusGround)
        {
            ++groundDefendersInSquad;
        }
        else if (versusAir)
        {
            ++flyingDefendersInSquad;
        }
    }
    // Assign dual-purpose units to whatever side needs them, priority to ground.
    if (groundDefendersNeeded > groundDefendersInSquad)
    {
        size_t add = std::min(versusBoth, groundDefendersNeeded - groundDefendersInSquad);
        groundDefendersInSquad += add;
        versusBoth -= add;
    }
    if (flyingDefendersNeeded > flyingDefendersInSquad)
    {
        size_t add = std::min(versusBoth, flyingDefendersNeeded - flyingDefendersInSquad);
        flyingDefendersInSquad += add;
    }

    //BWAPI::Broodwar->printf("defenders %d/%d %d/%d",
    //	groundDefendersInSquad, groundDefendersNeeded, flyingDefendersInSquad, flyingDefendersNeeded);

    // Add flying defenders.
    size_t flyingDefendersAdded = 0;
    BWAPI::Unit defenderToAdd;
    while (flyingDefendersNeeded > flyingDefendersInSquad + flyingDefendersAdded)
    {
		defenderToAdd = findClosestDefender(defenseSquad, defenseSquad.getOrder().getPosition(), true, false, enemyHasAntiAir, skipHydras);
		if (!defenderToAdd)
		{
			break;
		}
        _squadData.assignUnitToSquad(defenderToAdd, defenseSquad);
        ++flyingDefendersAdded;
    }

    // Add ground defenders.
    size_t groundDefendersAdded = 0;
    while (groundDefendersNeeded > groundDefendersInSquad + groundDefendersAdded)
    {
		defenderToAdd = findClosestDefender(defenseSquad, defenseSquad.getOrder().getPosition(), false, pullWorkers, enemyHasAntiAir, false);
		if (!defenderToAdd)
		{
			break;
		}
		if (defenderToAdd->getType().isWorker())
        {
            UAB_ASSERT(pullWorkers, "pulled worker defender mistakenly");
            WorkerManager::Instance().setCombatWorker(defenderToAdd);
        }
        _squadData.assignUnitToSquad(defenderToAdd, defenseSquad);
        ++groundDefendersAdded;
    }
}

// Choose a defender to join the base defense squad.
BWAPI::Unit CombatCommander::findClosestDefender(const Squad & defenseSquad, BWAPI::Position pos, bool flyingDefender, bool pullWorkers, bool enemyHasAntiAir, bool skipHydras)
{
    BWAPI::Unit closestDefender = nullptr;
    int minDistance = MAX_DISTANCE;

    for (BWAPI::Unit unit : _combatUnits) 
    {
        if (flyingDefender && !UnitUtil::TypeCanAttackAir(unit->getType()) ||
            !flyingDefender && !UnitUtil::TypeCanAttackGround(unit->getType()) ||
			skipHydras && unit->getType() == BWAPI::UnitTypes::Zerg_Hydralisk)
        {
            continue;
        }

        if (!_squadData.canAssignUnitToSquad(unit, defenseSquad))
        {
            continue;
        }

        int dist = unit->getDistance(pos);

        // Pull workers only if requested, and not from distant bases.
        if (unit->getType().isWorker())
        {
            if (!pullWorkers || dist > 18 * 32)
            {
                continue;
            }
            // Pull workers only if other units are considerably farther away.
            dist += 12 * 32;
        }

        // If the enemy can't shoot up, prefer air units as defenders.
        if (!enemyHasAntiAir && unit->isFlying())
        {
            dist -= 12 * 32;     // may become negative - that's OK
        }

        if (dist < minDistance)
        {
            closestDefender = unit;
            minDistance = dist;
        }
    }

    return closestDefender;
}

// NOTE This implementation is kind of cheesy. Orders ought to be delegated to a squad.
//      It can cause double-commanding, which is bad.
void CombatCommander::loadOrUnloadBunkers()
{
    if (the.self()->getRace() != BWAPI::Races::Terran)
    {
        return;
    }

    for (const auto bunker : the.self()->getUnits())
    {
        if (bunker->getType() == BWAPI::UnitTypes::Terran_Bunker)
        {
            // BWAPI::Broodwar->drawCircleMap(bunker->getPosition(), 12 * 32, BWAPI::Colors::Cyan);
            // BWAPI::Broodwar->drawCircleMap(bunker->getPosition(), 18 * 32, BWAPI::Colors::Orange);
            
            // Are there enemies close to the bunker?
            bool enemyIsNear = false;

            // 1. Is any enemy unit within a small radius?
            BWAPI::Unitset enemiesNear = BWAPI::Broodwar->getUnitsInRadius(bunker->getPosition(), 12 * 32,
                BWAPI::Filter::IsEnemy);
            if (enemiesNear.empty())
            {
                // 2. Is a fast enemy unit within a wider radius?
                enemiesNear = BWAPI::Broodwar->getUnitsInRadius(bunker->getPosition(), 18 * 32,
                    BWAPI::Filter::IsEnemy &&
                        (BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Vulture ||
                         BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Mutalisk)
                    );
                enemyIsNear = !enemiesNear.empty();
            }
            else
            {
                enemyIsNear = true;
            }

            if (enemyIsNear)
            {
                // Load one marine at a time if there is free space.
                if (bunker->getSpaceRemaining() > 0)
                {
                    BWAPI::Unit marine = BWAPI::Broodwar->getClosestUnit(
                        bunker->getPosition(),
                        BWAPI::Filter::IsOwned && BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Marine,
                        12 * 32);
                    if (marine)
                    {
                        the.micro.Load(bunker, marine);
                    }
                }
            }
            else
            {
                the.micro.UnloadAll(bunker);
            }
        }
    }
}

// Should squads have detectors assigned? Does not apply to all squads.
// Yes if the enemy has cloaked units. Also yes if the enemy is protoss and has observers
// and we have cloaked units--we want to shoot down those observers.
// Otherwise no if the detectors are in danger of dying.
bool CombatCommander::wantSquadDetectors() const
{
    if (the.enemy()->getRace() == BWAPI::Races::Protoss &&
        the.info.enemyHasMobileDetection() &&
        the.info.weHaveCloakTech())
    {
        return true;
    }

    return
        the.self()->getRace() == BWAPI::Races::Protoss ||      // observers should be safe-ish
        !the.info.enemyHasAntiAir() ||
        the.info.enemyCloakedUnitsSeen();
}

// Add or remove a given squad's detector, subject to availability.
// Because this checks the content of the squad, it should be called
// after any other units are added or removed.
void CombatCommander::maybeAssignDetector(Squad & squad, bool wantDetector)
{
    if (squad.hasDetector())
    {
        // If the detector is the only thing left in the squad, we don't want to keep it.
        if (!wantDetector || squad.getUnits().size() == 1)
        {
            for (BWAPI::Unit unit : squad.getUnits())
            {
                if (unit->getType().isDetector())
                {
                    squad.removeUnit(unit);
                    return;
                }
            }
        }
    }
    else
    {
        // Don't add a detector to an empty squad.
        if (wantDetector && !squad.getUnits().empty())
        {
            for (BWAPI::Unit unit : _combatUnits)
            {
                if (unit->getType().isDetector() && _squadData.canAssignUnitToSquad(unit, squad))
                {
                    _squadData.assignUnitToSquad(unit, squad);
                    return;
                }
            }
        }
    }
}

// Scan enemy cloaked units. Sometimes do routine scans.
void CombatCommander::doComsatScan()
{
    if (the.selfRace() != BWAPI::Races::Terran)
    {
        return;
    }

    if (the.my.completed.count(BWAPI::UnitTypes::Terran_Comsat_Station) == 0)
    {
        return;
    }

	// Add up scan energy.
	int totalScanEnergy = 0;		// of scanners with enough energy to scan
	for (BWAPI::Unit scanner : the.self()->getUnits())
	{
		if (scanner->getType() == BWAPI::UnitTypes::Terran_Comsat_Station && scanner->getEnergy() >= 50)
		{
			totalScanEnergy += scanner->getEnergy();
		}
	}

	if (totalScanEnergy == 0)
	{
		return;
	}

	// Does the enemy have undetected cloaked units that we may be able to engage?
    for (BWAPI::Unit unit : the.enemy()->getUnits())
    {
        if (unit->isVisible() &&
            (	!unit->isDetected() ||
				// unit->getType() == BWAPI::UnitTypes::Protoss_Arbiter ||		// this causes too much scanning
				unit->getOrder() == BWAPI::Orders::Burrowing
			) &&
			!unit->getType().isSpell() &&
			unit->getPosition().isValid() &&
			// Only if it is not already in view of a turret or science vessel.
			!UnitUtil::FriendlyDetectorInRange(unit->getPosition()))
        {
			//BWAPI::Broodwar->printf("cloaked enemy %s", unit->getType().getName().c_str());
			//BWAPI::Broodwar->printf("    is not already detected - flying = %d", unit->isFlying());
			// Only if there is some chance that we can threaten the unit.
			const UnitCluster * us = the.ops.getNearestFriendlyClusterVs(unit->getPosition(), !unit->isFlying(), unit->isFlying());
			if (us && unit->getDistance(us->center) < 6 * 32)
			{
				//BWAPI::Broodwar->printf("    possibly can be attacked");
				// At most one scan per call. We don't check whether it succeeds.
				(void)the.micro.Scan(unit->getPosition());
				if (the.enemyRace() == BWAPI::Races::Zerg)
				{
					// Burrow is zerg only and the only zerg cloaking tech.
					the.info.enemySeenBurrowing(unit);
				}
				return;
			}
        }
    }

	// Scan less often late in the game. Bases change more slowly then.
	int scanInterval = 4 * 60 * 24;
	if (the.now() > 14 * 60 * 24)
	{
		scanInterval = 6 * 60 * 24;
	}

	// If we haven't seen the enemy main in "a long time", scan it.
	if (the.bases.enemyStart())
	{
		int lastSeen = the.grid.lastSeenFrame(the.bases.enemyStart()->getPosition());
		if (lastSeen == 0 || totalScanEnergy >= 80 && the.now() - lastSeen > scanInterval)
		{
			(void)the.micro.Scan(the.bases.enemyStart()->getCenter());
			return;
		}
	}

	if (totalScanEnergy < 100)
	{
		return;
	}

	// Similarly for the enemy natural.
	if (the.bases.enemyStart() && the.bases.enemyStart()->getNatural())
	{
		int lastSeen = the.grid.lastSeenFrame(the.bases.enemyStart()->getNatural()->getPosition());
		if (lastSeen == 0 || the.now() - lastSeen > scanInterval)
		{
			(void)the.micro.Scan(the.bases.enemyStart()->getNatural()->getCenter());
			return;
		}
	}

	// If a scanner is near max energy, scan now so we don't waste any.
	// We scan whatever base we have least recently seen.
	for (BWAPI::Unit scanner : the.self()->getUnits())
	{
		if (scanner->getType() == BWAPI::UnitTypes::Terran_Comsat_Station && scanner->getEnergy() >= 190)
		{
			Base * b = leastRecentlySeenBase();
			if (b)
			{
				(void)the.micro.Scan(leastRecentlySeenBase()->getCenter());
				return;		// only one at a time
			}
		}
	}
}

// Find the base we have least recently seen, with a minimum limit.
// Null is an OK return.
Base * CombatCommander::leastRecentlySeenBase() const
{
	Base * oldestBase = nullptr;
	int oldestTime = the.now() - 3 * 60 * 24;

	for (Base * base : the.bases.getAll())
	{
		int t = the.grid.lastSeenFrame(base->getPosition());
		if (t < oldestTime)
		{
			oldestBase = base;
			oldestTime = t;
		}
	}

	return oldestBase;
}

// Do the larva trick, if conditions are favorable.
void CombatCommander::doLarvaTrick()
{
    if (the.selfRace() == BWAPI::Races::Zerg &&
        the.my.all.count(BWAPI::UnitTypes::Zerg_Drone) < Config::Macro::AbsoluteMaxWorkers)
    {
        for (Base * base : the.bases.getAll())
        {
            if (base->isMyCompletedBase() &&
                base->getMineralOffset().x < 0 &&
                base->getNumMineralWorkers() < base->getMaxMineralWorkers())
            {
                the.micro.LarvaTrick(base->getDepot()->getLarva());
            }
        }
    }
}

// What units do you want to drop into the enemy base from a transport?
// Terran does 6 marine + 2 medic drop or vulture drop.
// Protoss does 4 zealot drop or reaver + 2 zealot drop or 4 DT drop.
// Zerg does lurker drop.
bool CombatCommander::unitIsGoodToDrop(const Squad & squad, const BWAPI::Unit unit) const
{
	if (the.selfRace() == BWAPI::Races::Terran)
	{
		if (the.my.all.count(BWAPI::UnitTypes::Terran_Vulture) < 4 &&
			the.my.all.count(BWAPI::UnitTypes::Terran_Marine) > 0 &&
			the.my.all.count(BWAPI::UnitTypes::Terran_Medic) > 0)
		{
			// Bio drop: Load up 6 marines and 2 medics.
			return
				unit->getType() == BWAPI::UnitTypes::Terran_Marine &&
				squad.countUnits(BWAPI::UnitTypes::Terran_Marine) < 6
				||
				unit->getType() == BWAPI::UnitTypes::Terran_Medic &&
				squad.countUnits(BWAPI::UnitTypes::Terran_Medic) < 2;
		}
		// Otherwise it's a vulture drop.
		return unit->getType() == BWAPI::UnitTypes::Terran_Vulture;
	}

	if (the.selfRace() == BWAPI::Races::Protoss)
	{
		if (the.my.all.count(BWAPI::UnitTypes::Protoss_Templar_Archives) ||
			the.production.getQueue().anyInQueue(BWAPI::UnitTypes::Protoss_Templar_Archives) ||
			BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Templar_Archives))
		{
			// If we have or will get templar archives, accept only dark templar.
			return unit->getType() == BWAPI::UnitTypes::Protoss_Dark_Templar;
		}
		// Otherwise accept only zealots and reavers.
		return
			unit->getType() == BWAPI::UnitTypes::Protoss_Zealot ||
			unit->getType() == BWAPI::UnitTypes::Protoss_Reaver;
	}

	// Zerg.
	return unit->getType() == BWAPI::UnitTypes::Zerg_Lurker;

	/* the below does not work to allow ling drops, because the drop squad starts being assigned
	 * before lurker research starts.
		if (the.self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect) ||
			the.self()->isResearching(BWAPI::TechTypes::Lurker_Aspect))
		{
			return unit->getType() == BWAPI::UnitTypes::Zerg_Lurker;
		}
		return unit->getType() == BWAPI::UnitTypes::Zerg_Zergling;
	*/
}

// Called once per frame from update(), above.
// Get our money back at the last second for stuff that is about to be destroyed.
// Special case for a zerg sunken colony while it is morphing: It will lose up to
// 100 hp when the morph finishes, so cancel if it would be weak when it finishes.
// NOTE See BuildingManager::cancelBuilding() for another way to cancel buildings.
void CombatCommander::cancelDyingItems()
{
    for (BWAPI::Unit unit : the.self()->getUnits())
    {
        BWAPI::UnitType type = unit->getType();
        if (type.isBuilding() && !unit->isCompleted() ||
            type == BWAPI::UnitTypes::Zerg_Egg ||
            type == BWAPI::UnitTypes::Zerg_Lurker_Egg ||
            type == BWAPI::UnitTypes::Zerg_Cocoon)
        {
            if (UnitUtil::ExpectedSurvivalTime(unit) <= 1 * 24 ||
                type == BWAPI::UnitTypes::Zerg_Sunken_Colony && unit->getHitPoints() < 130 && unit->getRemainingBuildTime() < 24 && unit->isUnderAttack())
            {
                //BWAPI::Broodwar->printf("canceling %s hp %d", UnitTypeName(unit).c_str(), unit->getHitPoints() + unit->getShields());
                (void) the.micro.Cancel(unit);
            }
        }
    }
}

// How good is it to pull this worker for combat?
int CombatCommander::workerPullScore(BWAPI::Unit worker)
{
    return
        (worker->getHitPoints() == worker->getType().maxHitPoints() ? 10 : 0) +
        (worker->getShields() == worker->getType().maxShields() ? 4 : 0) +
        (worker->isCarryingGas() ? -3 : 0) +
        (worker->isCarryingMinerals() ? -2 : 0);
}

// Pull workers off of mining and into the attack squad.
// The argument n can be zero or negative or huge. Nothing awful will happen.
// Tries to pull the "best" workers for combat, as decided by workerPullScore() above.
void CombatCommander::pullWorkers(int n)
{
    struct Compare
    {
        auto operator()(BWAPI::Unit left, BWAPI::Unit right) const -> bool
        {
            return workerPullScore(left) < workerPullScore(right);
        }
    };

    std::priority_queue<BWAPI::Unit, std::vector<BWAPI::Unit>, Compare> workers;

    Squad & groundSquad = _squadData.getSquad("Ground");

    for (BWAPI::Unit unit : _combatUnits)
    {
        if (unit->getType().isWorker() &&
            WorkerManager::Instance().isFree(unit) &&
            _squadData.canAssignUnitToSquad(unit, groundSquad))
        {
            workers.push(unit);
        }
    }

    int nLeft = n;

    while (nLeft > 0 && !workers.empty())
    {
        BWAPI::Unit worker = workers.top();
        workers.pop();
        _squadData.assignUnitToSquad(worker, groundSquad);
        --nLeft;
    }
}

// Release workers from the attack squad.
void CombatCommander::releaseWorkers()
{
    Squad & groundSquad = _squadData.getSquad("Ground");
    groundSquad.releaseWorkers();
}

void CombatCommander::drawSquadInformation(int x, int y)
{
    _squadData.drawSquadInformation(x, y);
}

// Create an order to stay home at a safe location for an air squad.
SquadOrder CombatCommander::getSafeAirToAirSquadOrder() const
{
	BWAPI::Position target = the.bases.myMain()->getCenter();

	// Seek static air defense for protection.
	int closestRange = MAX_DISTANCE;
	BWAPI::Unit closestDefense = nullptr;
	for (BWAPI::Unit defense : the.info.getStaticDefense())
	{
		if (defense->isCompleted() && 
			defense->getType().airWeapon() != BWAPI::WeaponTypes::None)
		{
			int range = target.getApproxDistance(defense->getPosition());
			if (range < closestRange)
			{
				closestRange = range;
				closestDefense = defense;
			}
		}
	}

	if (closestDefense)
	{
		target = closestDefense->getPosition();
	}

	return SquadOrder(SquadOrderTypes::Hold, target, 8, false, "Build up");
}

// Create an attack order for the given squad.
// For a squad with ground units, ignore targets which are not accessible by ground.
SquadOrder CombatCommander::getAttackOrder(Squad * squad)
{
	// A "ground squad" is a squad with any ground unit, so that it wants to use a ground distance map if possible.
	const bool isGroundSquad = squad->hasGround();

	// 0. Decide whether the squad should stay defensive.
	// Some squads, especially terran, want to stay home and build up mass before moving out.
	bool moveout = the.moveout.go(squad, isGroundSquad ? SquadType::GroundAttack : SquadType::Air);
	if (isGroundSquad)
	{
		// The buildup mode can alter what units are produced.
		StrategyManager::Instance().setBuildupMode(!moveout);
	}

	// 1. If we're defensive, look for a front line to hold. No attacks.
	// Only ground squads can be defensive in this sense.
	if (isGroundSquad && !moveout)
	{
		return
			SquadOrder(SquadOrderTypes::Attack, getDefensiveBase(), DefendFrontRadius, isGroundSquad, "Defend front");
	}

	// If !moveout, then air squads look for a safe place at home.
	if (!isGroundSquad && !moveout)
	{
		return getSafeAirToAirSquadOrder();
	}

	// 2. Possibly contain the enemy.
	// Cases where we don't try to contain:
	//   Air squads. Air-only squads act independently of any containment.
	//   We're maxed.
	if (the.opsStrategy.getAggression() == AggressionLevel::Contain &&
		isGroundSquad &&
		!the.ops.attackAtMax())
	{
		Base * b = getContainBase();
		if (b)
		{
			return SquadOrder(SquadOrderTypes::Attack, b, DefendFrontRadius, isGroundSquad, "Hold containment");
		}

		// Otherwise fall through and be aggressive.
	}

    // 4. Otherwise we are aggressive. Look for a spot to attack.
    Base * base = nullptr;
    BWAPI::Position pos = BWAPI::Positions::None;
    std::string key = "none";
    getAttackLocation(squad, base, pos,  key);

    SquadOrder order;
    if (base)
    {
        order = SquadOrder(SquadOrderTypes::Attack, base, AttackRadius, isGroundSquad, "Attack base");
    }
    else
    {
        UAB_ASSERT(pos.isValid(), "bad attack location");
        order = SquadOrder(SquadOrderTypes::Attack, pos, AttackRadius, isGroundSquad, "Attack " + key);
    }
    order.setKey(key);
    return order;
}

// Choose a point of attack for the given squad (which may be null--no squad at all).
// For a squad with ground units, ignore targets which are not accessible by ground.
// Return either a base or a position, not both.
// NOTE Default values are passed in and will be overridden if a location is found.
void CombatCommander::getAttackLocation(Squad * squad, Base * & returnBase, BWAPI::Position & returnPos, std::string & returnKey)
{
    // If the squad is empty, minimize effort.
    if (squad && squad->getUnits().empty())
    {
        returnBase = the.bases.myMain();
        returnKey = "nothing";
        return;
    }

    // Ground and air considerations.
    // NOTE The squad doesn't recalculate these values until it updates, which hasn't happened yet.
    //      So any changes we just made to the squad's units won't be reflected here yet. The slight
    //      delay doesn't cause any known problem (and changes that matter don't happen often).
    bool hasGround = true;
    bool hasAir = false;
    bool canAttackGround = true;
    bool canAttackAir = false;
    if (squad)
    {
        hasGround = squad->hasGround();
        hasAir = squad->hasAir();
        canAttackGround = squad->canAttackGround();
        canAttackAir = squad->canAttackAir();
    }

    // If the squad has no combat units, or is unable to attack, return home.
    if (!hasGround && !hasAir || !canAttackGround && !canAttackAir)
    {
        returnBase = the.bases.myMain();
        returnKey = "nothing";
        return;
    }

    // Assume the squad is at our start position.
    // NOTE In principle, different members of the squad may be in different map partitions,
    //      unable to reach each others' positions by ground. We ignore that complication.
    // NOTE Since we aren't doing islands, all squads are reachable from the start position,
    //      except in the case of accidentally pushing units through a barrier.
    const int squadPartition = the.partitions.id(the.bases.myStart()->getTilePosition());

    // 1. If we haven't been able to attack, look for an undefended target on the ground, either
    // a building or a ground unit that can't shoot up.
    // Only if the squad is all air. This is mainly for mutalisks.
	// NOTE Turned off. It does not improve play.
    if (false &&
		squad &&
        !hasGround &&
        canAttackGround &&
        squad->getVanguard() &&
        the.now() - squad->getLastAttack() > 2 * 24 &&		// no cluster has attacked in this time
		the.now() - squad->getLastRetreat() <= 8 &&			// at least one cluster has retreated in this short time
        the.now() - squad->getOrderFrame() > 3 * 24)		// the order was long enough ago for an attack to have happened
    {
        int bestScore = -MAX_DISTANCE;      // higher is better
        BWAPI::Position target = BWAPI::Positions::None;

        for (const auto & kv : the.info.getUnitInfo(the.enemy()))
        {
            const UnitInfo & ui = kv.second;

            if (!UnitUtil::TypeCanAttackAir(ui.type) &&
                ui.lastPosition.isValid() &&
                !ui.goneFromLastPosition &&
				(canAttackAir || !ui.lifted) &&		// guardians can't hit a lifted barracks
                !defendedTarget(ui.lastPosition, false, true))
            {
				// Higher score is better.
                int score = -squad->getVanguard()->getDistance(ui.lastPosition);		// far away is bad

				// Extra score for valuable units.
				if (ui.type.isResourceDepot())
				{
					score += 16 * 32;
				}
                else if (ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
                {
                    score += 10 * 32;
                }
                else if (ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
                         ui.type == BWAPI::UnitTypes::Protoss_High_Templar ||
                         ui.type == BWAPI::UnitTypes::Protoss_Reaver ||
                         ui.type == BWAPI::UnitTypes::Zerg_Lurker ||
					     ui.type == BWAPI::UnitTypes::Zerg_Defiler)
                {
                    score += 6 * 32;
                }

                if (score > bestScore)
                {
                    bestScore = score;
                    target = ui.lastPosition;
					//BWAPI::Broodwar->printf("    %s: candidate undefended %s @ %d,%d", squad->getName().c_str(), ui.type.getName().c_str(), target.x, target.y);
				}
            }
        }

        if (target.isValid())
        {
            //BWAPI::Broodwar->printf("  %s: undefended target @ %d,%d", squad->getName().c_str(), target.x, target.y);

            returnPos = target;
            returnKey = "undefended";
            return;
        }
    }

    // 2. Attack the enemy base or floating CC with the weakest defense.
	// An "enemy" base is one that either the enemy owns, or has multiple enemy
	// buildings other than static defense.
	// Only if the squad can attack ground.
	Base * targetBase = nullptr;
	BWAPI::Position target = BWAPI::Positions::None;		// used only if targetBase is not set
	int bestScore = -MAX_DISTANCE;

	// 2.1 Enemy bases.
	if (canAttackGround)
	{
		for (Base * base : the.bases.getAll())
		{
			// Ground squads ignore enemy bases which they cannot reach.
			if (hasGround && squadPartition != the.partitions.id(base->getTilePosition()))
			{
				continue;
			}

			const bool enemyOwned = base->getOwner() == the.enemy();

			// TODO The "attack base buildings" feature needs more work.
			if (enemyOwned /* || nEnemyBuildings >= 3 */)
			{
				// Higher is better. The score is commonly negative.
				int score = 0;

				// A base owned by the enemy is higher priority.
				//if (enemyOwned)
				//{
				//	score += 4;
				//}

				// A base with an undefended mineral line is higher priority.
				if (hasGround && the.bases.myMain()->getSafeTileDistance(base->getTilePosition()) >= 0 ||
					hasAir && the.bases.myMain()->getAirSafeTileDistance(base->getTilePosition()) >= 0)
				{
					score += 2;
				}

				// A base with many enemy buildings has higher priority.
				int nEnemyBuildings = base->getNumEnemyNondefenseBuildings();
				score += nEnemyBuildings / 8;

				// A base with low remaining minerals is lower priority.
				if (base->getLastKnownMinerals() < 2000)
				{
					score -= 1;
					if (base->getLastKnownMinerals() < 500)
					{
						score -= 1;
					}
				}

				// Ground squads mildly prefer to attack a base other than the enemy main. It's a simple heuristic.
				// Air squads prefer to go for the main first.
				if (base == the.bases.enemyStart())
				{
					score += hasGround ? -1 : 2;
				}

				// The squad vanguard is the unit closest to the current attack location.
				// Prefer a new attack location which is close, preferably even closer.
				if (squad && squad->getVanguard())
				{
					score -= squad->getVanguard()->getDistance(base->getCenter()) / (16 * 32);
				}

				std::vector<UnitInfo> enemies;
				int enemyDefenseRange = the.info.enemyHasSiegeMode() ? 12 * 32 : 8 * 32;
				the.info.getNearbyForce(enemies, base->getCenter(), the.enemy(), enemyDefenseRange);
				for (const UnitInfo & enemy : enemies)
				{
					// Count enemies that are buildings or slow-moving units good for defense.
					if (enemy.type.isBuilding() ||
						enemy.type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
						enemy.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
						enemy.type == BWAPI::UnitTypes::Protoss_Arbiter ||
						enemy.type == BWAPI::UnitTypes::Protoss_Carrier ||
						enemy.type == BWAPI::UnitTypes::Protoss_High_Templar ||
						enemy.type == BWAPI::UnitTypes::Protoss_Reaver ||
						enemy.type == BWAPI::UnitTypes::Zerg_Lurker ||
						enemy.type == BWAPI::UnitTypes::Zerg_Guardian)
					{
						// If the enemy unit could attack (some units of) the squad, count it.
						if (hasGround && UnitUtil::TypeCanAttackGround(enemy.type) ||			// doesn't recognize casters
							hasAir && UnitUtil::TypeCanAttackAir(enemy.type) ||					// doesn't recognize casters
							enemy.type == BWAPI::UnitTypes::Protoss_High_Templar)				// spellcaster
						{
							--score;
						}
					}
				}
				if (score > bestScore)
				{
					targetBase = base;
					bestScore = score;
				}
			}
		}
	}

	// 2.2 Enemy floating CCs. Attack with a flying squad.
	if (the.enemyRace() == BWAPI::Races::Terran && hasAir && canAttackAir && !hasGround)
	{
		for (const auto & kv : the.info.getUnitInfo(the.enemy()))
		{
			const UnitInfo & ui = kv.second;

			if (ui.type == BWAPI::UnitTypes::Terran_Command_Center && ui.lifted && !ui.goneFromLastPosition)
			{
				// The scoring intentionally prefers attacking landed bases over floating CCs.
				int score = 2;

				// The squad vanguard is the unit closest to the current attack location.
				// Prefer a new attack location which is close, preferably even closer.
				if (squad && squad->getVanguard())
				{
					score -= squad->getVanguard()->getDistance(ui.lastPosition) / (16 * 32);
				}

				std::vector<UnitInfo> enemies;
				int enemyDefenseRange = 8 * 32;
				the.info.getNearbyForce(enemies, ui.lastPosition, the.enemy(), enemyDefenseRange);
				for (const UnitInfo & enemy : enemies)
				{
					// Count enemies that are good for air defense.
					if (enemy.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
						enemy.type == BWAPI::UnitTypes::Terran_Marine)
					{
						--score;
					}
					else if (enemy.type == BWAPI::UnitTypes::Terran_Bunker ||
						enemy.type == BWAPI::UnitTypes::Terran_Goliath ||
						enemy.type == BWAPI::UnitTypes::Terran_Wraith ||
						enemy.type == BWAPI::UnitTypes::Terran_Valkyrie)
					{
						if (UnitUtil::TypeCanAttackAir(enemy.type))
						{
							score -= 2;
						}
					}
					else if (enemy.type == BWAPI::UnitTypes::Terran_Battlecruiser)
					{
						score -= 4;
					}
				}
				if (score > bestScore)
				{
					targetBase = nullptr;
					target = ui.lastPosition;
					bestScore = score;
				}
			}
		}
	}

	if (targetBase)
	{
		returnBase = targetBase;
		returnKey = "base";
		return;
	}
	if (target.isValid())
	{
		returnBase = nullptr;
		returnPos = target;
		returnKey = "floating CC";
		return;
	}

    // 3. Attack known enemy buildings.
    // We assume that a terran can lift the buildings; otherwise, the squad must be able to attack ground.
    if (canAttackGround || the.enemyRace() == BWAPI::Races::Terran)
    {
        for (const auto & kv : the.info.getUnitInfo(the.enemy()))
        {
            const UnitInfo & ui = kv.second;

            // Special case for refinery buildings because their ground reachability is tricky to check.
            // TODO This causes a bug where a ground squad may be ordered to attack an enemy refinery
            //      that is on an island and out of reach.
            if (ui.type.isBuilding() &&
                !ui.type.isAddon() &&
                ui.lastPosition.isValid() &&
                !ui.goneFromLastPosition &&
                (ui.type.isRefinery() || squadPartition == the.partitions.id(ui.lastPosition)))
                // (!hasGround || (!ui.type.isRefinery() && squadPartition == the.partitions.id(ui.lastPosition))))
            {
                if (ui.lifted)
                {
                    // The building is lifted (or was when last seen). Only if the squad can hit it.
                    if (canAttackAir)
                    {
                        returnPos = ui.lastPosition;
                        return;
                    }
                }
                else
                {
                    // The building is not thought to be lifted.
                    returnPos = ui.lastPosition;
                    returnKey = "building";
                    return;
                }
            }
        }
    }

    // 4. Attack visible enemy units.
    const BWAPI::Position squadCenter = squad
        ? squad->calcCenter()
        : the.bases.myStart()->getPosition();
    BWAPI::Unit bestUnit = nullptr;
    int bestDistance = MAX_DISTANCE;
    for (BWAPI::Unit unit : the.enemy()->getUnits())
    {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Larva ||
            !unit->isDetected() ||
            unit->isStasised() ||
            unit->getType().isSpell())
        {
            continue;
        }

        // Ground squads ignore enemy units which are not accessible by ground, except when nearby.
        // The "when nearby" exception allows a chance to attack enemies that are in range,
        // even if they are beyond a barrier. It's very rough.
        int distance = squad && squad->getVanguard() ? unit->getDistance(squad->getVanguard()) : unit->getDistance(squadCenter);
        if (hasGround &&
            squadPartition != the.partitions.id(unit->getPosition()) &&
            distance > 300)
        {
            continue;
        }

        if (unit->isFlying() && canAttackAir || !unit->isFlying() && canAttackGround)
        {
            if (distance < bestDistance)
            {
                bestUnit = unit;
                bestDistance = distance;
            }
        }
    }
    if (bestUnit)
    {
        returnPos = bestUnit->getPosition();
        returnKey = "unit";
        return;
    }

    // 6. Attack the remembered locations of unseen enemy units which might still be there.
    // Choose the one most recently seen.
    int lastSeenFrame = 0;
    BWAPI::Position lastSeenPos = BWAPI::Positions::None;
    BWAPI::UnitType lastSeenType = BWAPI::UnitTypes::None;      // for debugging only
    for (const auto & kv : the.info.getUnitData(the.enemy()).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.updateFrame < the.now() &&
            ui.updateFrame > lastSeenFrame &&
            !ui.goneFromLastPosition &&
            !ui.type.isSpell() &&
            (hasAir || the.partitions.id(ui.lastPosition) == squadPartition) &&
            ((ui.type.isFlyer() || ui.lifted) && canAttackAir || (!ui.type.isFlyer() || !ui.lifted) && canAttackGround))
        {
            lastSeenFrame = ui.updateFrame;
            lastSeenPos = ui.lastPosition;
            lastSeenType = ui.type;
        }
    }
    if (lastSeenPos.isValid())
    {
        returnPos = lastSeenPos;
        returnKey = "possible unit";
        return;
    }

	// 6. If we have a best guess at the enemy start, go there.
	// (If we know the enemy start, then we do not have a best guess.)
	if (the.bases.enemyStartBestGuess())
	{
		returnBase = the.bases.enemyStartBestGuess();
		returnPos = the.bases.enemyStartBestGuess()->getCenter();
		returnKey = "best guess base";
		return;
	}

    // 7. We know nothing, so explore the map until we find something.
    returnPos = MapGrid::Instance().getLeastExplored(!hasAir, squadPartition, hasGround ? squad->getSampleGroundUnit() : nullptr);
    returnKey = "explore";
}

// Does the enemy have defenders (static or mobile) near the given position?
bool CombatCommander::defendedTarget(const BWAPI::Position & pos, bool vsGround, bool vsAir) const
{
	const int ShortTime = 4 * 24;		// time in frames

	if (vsGround)
	{
		if (the.groundHits(BWAPI::TilePosition(pos)) > 0)
		{
			return true;
		}

		const std::vector<UnitCluster> & antigroundClusters = the.ops.getGroundDefenseClusters();
		for (const UnitCluster & cluster : antigroundClusters)
		{
			int dist = pos.getApproxDistance(cluster.center);

			// The enemy defenders are "too close" if they can get within 8 tiles in a short time.
			if (dist <= 8 * 32 + ShortTime * cluster.speed)
			{
				return true;
			}
		}
	}

	if (vsAir)
    {
        if (the.airHits(BWAPI::TilePosition(pos)) > 0)
        {
            return true;
        }

        const std::vector<UnitCluster> & antiairClusters = the.ops.getAirDefenseClusters();
        for (const UnitCluster & cluster : antiairClusters)
        {
            int dist = pos.getApproxDistance(cluster.center);

            // The enemy defenders are "too close" if they can get within 8 tiles in a short time.
            if (dist <= 8 * 32 + ShortTime * cluster.speed)
            {
                return true;
            }
        }
    }

    return false;
}

// Find a position the base's mineral line where a burrowed zergling in a Watch squad
// will see an enemy base being constructed, without blocking it.
// This is more effective against human opponents, who easily solve blocking zerglings.
BWAPI::Position	CombatCommander::getHiddenWatchLocation(Base * base) const
{
	BWAPI::Position pos = base->getCenter();

	if (base->getMineralOffset().x > 0)
	{
		pos.x += 4 * 32;
	}
	else if (base->getMineralOffset().x < 0)
	{
		pos.x -= 4 * 32;
	}

	if (base->getMineralOffset().y > 0)
	{
		pos.y += 3 * 32 + 16;
	}
	else if (base->getMineralOffset().y < 0)
	{
		pos.y -= 3 * 32 + 16;
	}

	return pos;
}

// Choose a point of attack for the given drop squad.
BWAPI::Position CombatCommander::getDropLocation(const Squad & squad)
{
    // 1. The enemy starting base, if known.
    Base * enemyStart = the.bases.enemyStart();
    if (enemyStart)
    {
        return enemyStart->getPosition();
    }

    // 2. Any known enemy base.
    for (Base * base : the.bases.getAll())
    {
        if (base->getOwner() == the.enemy())
        {
			return base->getPosition();
        }
    }

    // 3. Any known enemy buildings.
    for (const auto & kv : the.info.getUnitInfo(the.enemy()))
    {
        const UnitInfo & ui = kv.second;

        if (ui.type.isBuilding() && ui.lastPosition.isValid() && !ui.goneFromLastPosition)
        {
            return ui.lastPosition;
        }
    }

    // 4. We can't see anything, so explore the map until we find something.
    return MapGrid::Instance().getLeastExplored();
}

// We're being defensive. Get the location to defend.
Base * CombatCommander::getDefensiveBase()
{
    // We are guaranteed to always have a main base location, even if it has been destroyed.
    Base * base = the.bases.myMain();

    // We may have taken our natural. If so, call that the front line.
    Base * natural = the.bases.myNatural();
    if (natural && the.self() == natural->getOwner())
    {
        base = natural;
    }

    return base;
}

// We want to hold a containment.
// Check whether we can contain by holding a base, and if so return the base.
Base * CombatCommander::getContainBase()
{
	// It's a simplified test: The enemy start has a natural that the enemy does not own.
	// Will fail on maps with a rear or distant natural.
	if (the.bases.enemyStart() &&
		the.bases.enemyStart()->getNatural() &&
		the.bases.enemyStart()->getNatural()->getOwner() != the.enemy())
	{
		return the.bases.enemyStart()->getNatural();
	}

	return nullptr;
}

// Choose one worker to pull.
BWAPI::Unit CombatCommander::findClosestWorkerToTarget(BWAPI::Unitset & unitsToAssign, const BWAPI::Position & target)
{
    BWAPI::Unit closestMineralWorker = nullptr;
    int closestDist = MAX_DISTANCE;
    
    for (BWAPI::Unit unit : unitsToAssign)
    {
        if (unit->getType().isWorker() && WorkerManager::Instance().isFree(unit))
        {
            int dist = unit->getDistance(target);
            if (unit->isCarryingMinerals())
            {
                dist += 96;
            }

            if (dist < closestDist)
            {
                closestMineralWorker = unit;
                dist = closestDist;
            }
        }
    }

    return closestMineralWorker;
}

int CombatCommander::numZerglingsInOurBase() const
{
    const int concernRadius = 300;
    int zerglings = 0;
    
    BWAPI::Position myBasePosition(the.bases.myStart()->getPosition());

    for (BWAPI::Unit unit : the.enemy()->getUnits())
    {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Zergling &&
            unit->getDistance(myBasePosition) < concernRadius)
        {
            ++zerglings;
        }
    }

    return zerglings;
}

// Is an enemy building near our base? If so, we may pull workers.
// Don't pull against a completed bunker, cannon, or sunken; we'll only lose workers.
bool CombatCommander::buildingRush() const
{
    // If we have units, there will likely be no gain in pulling workers.
    if (the.info.weHaveCombatUnits())
    {
        return false;
    }

    BWAPI::Position myBasePosition(the.bases.myStart()->getPosition());

    for (BWAPI::Unit unit : the.enemy()->getUnits())
    {
        if (unit->getType().isBuilding() &&
            unit->getDistance(myBasePosition) < 600 &&
            !unit->isLifted() &&
            (!unit->isCompleted() || unit->getType().groundWeapon() == BWAPI::WeaponTypes::None))
        {
            return true;
        }
    }

    return false;
}

CombatCommander & CombatCommander::Instance()
{
    static CombatCommander instance;
    return instance;
}

#include "InformationManager.h"

#include "The.h"

#include "Bases.h"
#include "CombatCommander.h"
#include "MapTools.h"
#include "ProductionManager.h"
#include "Random.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

InformationManager::InformationManager()
    : _self(BWAPI::Broodwar->self())
    , _enemy(BWAPI::Broodwar->enemy())
    
    , _weHaveCombatUnits(false)
    , _enemyHasCombatUnits(false)

    , _enemyHasStaticAntiAir(false)
    , _enemyHasAntiAir(false)
    , _enemyHasAirTech(false)
	, _enemyHasAirToGround(false)
    , _enemyHasCloakTech(false)
    , _enemyCloakedUnitsSeen(false)
    , _enemyHasMobileCloakTech(false)
    , _enemyHasOverlordHunters(false)
    , _enemyHasStaticDetection(false)
    , _enemyHasMobileDetection(_enemy->getRace() == BWAPI::Races::Zerg)
	, _enemyHasTransport(false)

	, _enemyHasStim(false)
	, _enemyHasOpticFlare(false)
	, _enemyHasRestoration(false)
	, _enemyHasMines(false)
	, _enemyHasSiegeMode(false)
	, _enemyHasIrradiate(false)
	, _enemyHasEMP(false)
	, _enemyHasLockdown(false)
	, _enemyHasYamato(false)
	, _enemyHasNuke(false)

    , _enemyHasStorm(false)
	, _enemyHasMaelstrom(false)
	, _enemyHasDWeb(false)
	, _enemyHasStasis(false)
	, _enemyHasRecall(false)

	, _enemyHasBurrow(false)
	, _enemyHasLurkers(false)
	, _enemyHasEnsnare(false)
	, _enemyHasBroodling(false)
	, _enemyHasPlague(false)

	, _enemyGasTiming(0)

	, _bunkerShouldFireRange(5 * 32)
{
}

void InformationManager::initialize()
{
    initializeRegionInformation();
    initializeResources();
}

// Set up _occupiedLocations.
void InformationManager::initializeRegionInformation()
{
    updateOccupiedRegions(the.zone.ptr(the.bases.myStart()->getTilePosition()), _self);
}

// Initial mineral and gas values.
void InformationManager::initializeResources()
{
    for (BWAPI::Unit patch : BWAPI::Broodwar->getStaticMinerals())
    {
        _resources.insert(std::pair<BWAPI::Unit, ResourceInfo>(patch, ResourceInfo(patch)));
    }
    for (BWAPI::Unit geyser : BWAPI::Broodwar->getStaticGeysers())
    {
        _resources.insert(std::pair<BWAPI::Unit, ResourceInfo>(geyser, ResourceInfo(geyser)));
    }
}

void InformationManager::update()
{
    updateUnitInfo();
    updateGoneFromLastPosition();
    updateBaseLocationInfo();
	updateEnemySpells();
	updateYourTargets();
    updateBullets();
	updateEnemyOrders();
	updateResources();
    updateEnemyGasTiming();
    updateTerranEnemyAbilities();

	// For testing enemy fire teams.
	//for (BWAPI::Unit u : the.self()->getUnits())
	//{
	//	for (BWAPI::Unit enemy : getEnemyFireteam(u))
	//	{
	//		BWAPI::Broodwar->drawLineMap(enemy->getPosition(), u->getPosition(), BWAPI::Colors::Yellow);
	//	}
	//}
}

void InformationManager::updateUnitInfo() 
{
    _unitData[_enemy].removeBadUnits();
    _unitData[_self].removeBadUnits();

    for (BWAPI::Unit unit : _enemy->getUnits())
    {
        updateUnit(unit);
    }

    // Remove destroyed pylons from _ourPylons.
    for (auto pylonIt = _ourPylons.begin(); pylonIt != _ourPylons.end(); )
    {
        if (!(*pylonIt)->exists())
        {
            pylonIt = _ourPylons.erase(pylonIt);
        }
        else
        {
            ++pylonIt;
        }
    }

    bool anyNewPylons = false;

    for (BWAPI::Unit unit : _self->getUnits())
    {
        updateUnit(unit);

        // Add newly-completed pylons to _ourPylons, and notify BuildingManager.
        if (unit->getType() == BWAPI::UnitTypes::Protoss_Pylon &&
            unit->isCompleted() &&
            !_ourPylons.contains(unit))
        {
            _ourPylons.insert(unit);
            anyNewPylons = true;
        }
    }

    if (anyNewPylons)
    {
        BuildingManager::Instance().unstall();
    }
}

void InformationManager::updateBaseLocationInfo() 
{
    _occupiedRegions[_self].clear();
    _occupiedRegions[_enemy].clear();
    
    // The enemy occupies a region if it has a building there.
    for (const auto & kv : _unitData[_enemy].getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isBuilding() && !ui.goneFromLastPosition && !ui.lifted)
        {
            updateOccupiedRegions(the.zone.ptr(BWAPI::TilePosition(ui.lastPosition)), _enemy);
        }
    }

    // We occupy a region if we have a building there.
    for (const BWAPI::Unit unit : _self->getUnits())
    {
        if (unit->getType().isBuilding() && unit->getPosition().isValid())
        {
            updateOccupiedRegions(the.zone.ptr(unit->getTilePosition()), _self);
        }
    }
}

void InformationManager::updateOccupiedRegions(const Zone * zone, BWAPI::Player player) 
{
    // if the region is valid (flying buildings may be in nullptr regions)
    if (zone)
    {
        // add it to the list of occupied regions
        _occupiedRegions[player].insert(zone);
    }
}

// If we can see the last known location of a remembered unit and the unit is not there,
// set the unit's goneFromLastPosition flag (unless it is burrowed or burrowing).
void InformationManager::updateGoneFromLastPosition()
{
    // We don't need to check every frame.
    // 1. The game supposedly only resets visible tiles when frame % 100 == 99.
    // 2. If the unit has only been gone from its location for a short time, it probably
    //    didn't go far (though it might have been recalled or gone through a nydus).
    // On the other hand, burrowed units can disappear from view more quickly.
    // 3. Detection is updated immediately, so we might overlook having detected
    //    a burrowed unit if we don't update often enough.
    // 4. We also might miss a unit in the process of burrowing.
    // All in all, we should check fairly often.
    if (the.now() % 6 == 5)
    {
        _unitData[_enemy].updateGoneFromLastPosition();
    }

    if (Config::Debug::DrawHiddenEnemies)
    {
        for (const auto & kv : _unitData[_enemy].getUnits())
        {
            const UnitInfo & ui(kv.second);

            if (ui.unit)
            {
				if (ui.goneFromLastPosition)
				{
					if (!ui.unit->isVisible())
					{
						// Draw a small X.
						BWAPI::Broodwar->drawLineMap(
							ui.lastPosition + BWAPI::Position(-2, -2),
							ui.lastPosition + BWAPI::Position(2, 2),
							BWAPI::Colors::Red);
						BWAPI::Broodwar->drawLineMap(
							ui.lastPosition + BWAPI::Position(2, -2),
							ui.lastPosition + BWAPI::Position(-2, 2),
							BWAPI::Colors::Red);
					}
				}
                else
                {
					// Bunkers: Are they thought to have marines inside?
					// Label them loaded or empty.
					if (ui.type == BWAPI::UnitTypes::Terran_Bunker && ui.isCompleted())
					{
						bool loaded = the.info.isEnemyBunkerLoaded(ui.unit);
						char color = loaded ? orange : green;
						std::string text = loaded ? "LOADED" : "empty";

						BWAPI::Broodwar->drawTextMap(ui.lastPosition + BWAPI::Position(-20, 0),
							"%c%s", color, text.c_str());
					}
					else if (!ui.unit->isVisible())
					{
						// Draw a small circle.
						BWAPI::Color color = ui.burrowed ? BWAPI::Colors::Yellow : BWAPI::Colors::Green;
						BWAPI::Broodwar->drawCircleMap(ui.lastPosition, 4, color);
					}
                }
            }

            // Units that are in sight range but undetected.
            if (ui.unit && ui.unit->isVisible() && !ui.unit->isDetected())
            {
                // Draw a larger circle.
                BWAPI::Broodwar->drawCircleMap(ui.lastPosition, 8, BWAPI::Colors::Purple);

                BWAPI::Broodwar->drawTextMap(ui.lastPosition + BWAPI::Position(10, 6),
                    "%c%s", white, UnitTypeName(ui.type).c_str());
            }
		}
    }
}

// For each of our units, keep track of which enemies are targeting it.
// It changes frequently, so this is updated every frame.
void InformationManager::updateYourTargets()
{
    _yourTargets.clear();

    // We only know the targets for visible enemy units.
    for (BWAPI::Unit enemy : _enemy->getUnits())
    {
        BWAPI::Unit target = enemy->getOrderTarget();
        if (target && target->getPlayer() == _self && (enemy->getType() == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine || UnitUtil::AttackOrder(enemy)))
        {
            _yourTargets[target].insert(enemy);
            //BWAPI::Broodwar->drawLineMap(enemy->getPosition(), target->getPosition(), BWAPI::Colors::Yellow);
        }
    }
}

// Detect whether the enemy has cast any of various spells.
// Check whether enemy bunkers are loaded with units that may shoot at us. (SCVs and medics don't.)
// TODO Also an unfinished experiment to locate undetected lurkers by their spines.
void InformationManager::updateBullets()
{
    BWAPI::Bulletset bullets = BWAPI::Broodwar->getBullets();

	// All enemy bunkers that are in range to potentially fire on us with marines.
	// Any that did not actually fire should be removed from the _loadedBunkers set.
	// THOUGH NOTE: It might have firebats instead.
	BWAPI::Unitset enemyBunkersInRange;
	getEnemyBunkersInRange(enemyBunkersInRange);

    for (BWAPI::Bullet bullet : bullets)
    {
		if (!bullet->exists())
		{
			// This is frequent. We get no info about the bullet, so skip it.
		}
		else if (bullet->getType() == BWAPI::BulletTypes::Gauss_Rifle_Hit &&
			bullet->getPlayer() == the.enemy() &&
			!bullet->getSource())						// should mean the marine is in a container, that is, a bunker
		{
			for (auto it = enemyBunkersInRange.begin(); it != enemyBunkersInRange.end(); )
			{
				// If the bunker is close enough to fire, assume it is firing.
				// NOTE bullet->getAngle() gives weird numbers that don't seem helpful.
				if (bullet->getTargetPosition().getApproxDistance((*it)->getPosition()) <= _bunkerShouldFireRange)
				{
					_loadedBunkers.insert(*it);
					it = enemyBunkersInRange.erase(it);		// other bullets from the same bunker don't matter
				}
				else
				{
					++it;
				}
			}
		}
		else if (bullet->getType() == BWAPI::BulletTypes::EMP_Missile && bullet->getPlayer() == the.enemy())
		{
			// Latch: Once they have EMP, they always have it.
			_enemyHasEMP = true;
		}
		else if (bullet->getType() == BWAPI::BulletTypes::Psionic_Storm && bullet->getPlayer() == the.enemy())
        {
            // Latch: Once they have storm, they always have it.
            _enemyHasStorm = true;
        }
		else if (bullet->getType() == BWAPI::BulletTypes::Ensnare && bullet->getPlayer() == the.enemy())
		{
			// Latch: Once they have ensnare, they always have it.
			_enemyHasEnsnare = true;
		}
		else if (bullet->getType() == BWAPI::BulletTypes::Plague_Cloud && bullet->getPlayer() == the.enemy())
		{
			// Latch: Once they have plague, they always have it.
			_enemyHasPlague = true;
		}
        else if (bullet->getType() == BWAPI::BulletTypes::Subterranean_Spines && bullet->getPlayer() == the.enemy())
        {
			_enemyHasCloakTech = true;
			_enemyHasMobileCloakTech = true;
			_enemyHasLurkers = true;

			// If we're terran and the lurker is undetected, try to scan for it.
			if (bullet->getSource() == nullptr || !bullet->getSource()->isDetected())
			{
				CombatCommander::Instance().scanLurkerNear(bullet->getPosition());
			}
        }
		else if (bullet->getType() == BWAPI::BulletTypes::Yamato_Gun && bullet->getPlayer() == the.enemy())
		{
			// Latch: One they have yamato, they always have it.
			_enemyHasYamato = true;
		}
	}

	// Bunkers that did not fire are assumed to be empty.
	// NOTE It's not very reliable. Each marine in the bunker has its own cooldown.
	for (BWAPI::Unit bunker : enemyBunkersInRange)
	{
		_loadedBunkers.erase(bunker);
	}
}

// Attempt to detect spell usage by looking at enemy orders.
// So far, Restoration only. This is not likely to work well.
void InformationManager::updateEnemyOrders()
{
	if (the.enemyRace() == BWAPI::Races::Terran && !_enemyHasRestoration)
	{
		for (BWAPI::Unit u : the.enemy()->getUnits())
		{
			// Medic restoration does not have a bullet.
			// The only other way to detect it is to track unit status to see if effects are removed,
			// and Steamhammer does not track in that much detail.
			if (u->getOrder() == BWAPI::Orders::CastRestoration)		// if the medic is out of sight, we don't see the order
			{
				_enemyHasRestoration = true;
				return;
			}
		}
	}

	if (the.enemyRace() == BWAPI::Races::Zerg && !_enemyHasEnsnare && the.your.seen.count(BWAPI::UnitTypes::Zerg_Queen))
	{
		for (BWAPI::Unit u : the.enemy()->getUnits())
		{
			// We might accidentally ensnare our own units, so check enemy queen orders.
			// We could get lucky and catch it. updateBullets() checks the ensnare bullet, more likely to work.
			if (u->getOrder() == BWAPI::Orders::CastEnsnare)
			{
				_enemyHasEnsnare = true;
				return;
			}
		}
	}
}

// Find all bunkers that might potentially shoot at us. Any of these that doesn't
// shoot will be assumed empty (it's at least empty of marines).
// BumkerShouldFireRange is the range of non-range-upgraded marines in a bunker.
// It helps prevent errors where a bunker is mistakenly marked empty.
// NOTE Being in a bunker gives units +1 tile of range.
void InformationManager::getEnemyBunkersInRange(BWAPI::Unitset & bunkers)
{
	if (the.enemyRace() != BWAPI::Races::Terran)
	{
		return;
	}

	// A bunker adds 1 tile to the range of units inside.
	// The value varies because the enemy could get marine range at any time.
	// We set it here and it is used here and later.
	_bunkerShouldFireRange = (5 + (enemyHasMarineRange() ? 1 : 0)) * 32;

	for (const auto & kv : the.info.getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Terran_Bunker && ui.isCompleted() && ui.unit && ui.unit->exists())
		{
			if (BWAPI::Broodwar->getClosestUnit(
				ui.lastPosition,
				BWAPI::Filter::IsOwned,
				_bunkerShouldFireRange))
			{
				bunkers.insert(ui.unit);
			}
		}
	}
}

// Update any visible mineral patches or vespene geysers with their remaining amounts.
void InformationManager::updateResources()
{
	for (auto it = _resources.begin(); it != _resources.end(); ++it)
	{
		it->second.update();
	}
}

// Find the earliest sign of the enemy spending gas (not merely mining gas).
// For now, notice units that require gas. We don't try to detect enemy research.
// Zero means the enemy has not visibly used gas yet.
void InformationManager::updateEnemyGasTiming()
{
    if (_enemyGasTiming)
    {
        // Already know it.
        return;
    }

    for (const std::pair<BWAPI::UnitType, int> & unitCount : the.your.seen.getCounts())
    {
        BWAPI::UnitType t = unitCount.first;

        // A larva or egg says it costs 1 gas.
        // So does a spider mine, but it does cost gas to research.
        // I think other units tell the truth about their gas costs.
        // The vulture and shuttle are the only non-gas units that need gas to make (for the factory or robo),
        // so we don't need to look through all of the.your.inferred.getCounts().
        if (t.gasPrice() > 0 && t != BWAPI::UnitTypes::Zerg_Larva && t != BWAPI::UnitTypes::Zerg_Egg ||
            t == BWAPI::UnitTypes::Terran_Vulture ||
            t == BWAPI::UnitTypes::Protoss_Shuttle)
       {
            _enemyGasTiming = the.now();
            return;
        }
    }
}

// Keep track of active enemy nukes and scans every frame, if the enemy is terran.
void InformationManager::updateTerranEnemyAbilities()
{
	if (the.enemyRace() == BWAPI::Races::Terran)
    {
		// If there is a nuke dot, the enemy has nukes.
		// Latch: Once _enemyHasNuke is set, it is never cleared.
		if (!BWAPI::Broodwar->getNukeDots().empty())
		{
			// Steamhammer does not nuke, so it's the enemy.
			// NOTE When Steamhammer gets the ability, it can filter out its own nukes here.
			_enemyHasNuke = true;

			// Track each nuke even after the nuke dot disappears.
			// NOTE This could be improved by detecting when the nuke is canceled or the ghost is killed.
			//      So far, we keep the nuke info around until it times out after it lands--or would have landed.
			for (const BWAPI::Position & target : BWAPI::Broodwar->getNukeDots())
			{
				if (_enemyNukes.find(target) == _enemyNukes.end())
				{
					// Assume we saw the nuke dot the moment it appeared.
					// This can be wrong for dots we run across during scouting.
					// 450 frames is a slight overestimate, for safety.
					_enemyNukes[target] = the.now() + 450;
				}
			}
		}

		if (!_enemyNukes.empty())
		{
			// Forget nukes that have expired. They landed or the ghost was killed.
			for (auto it = _enemyNukes.begin(); it != _enemyNukes.end(); )
			{
				if (it->second < the.now())
				{
					it = _enemyNukes.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		_enemyScans.clear();
        for (BWAPI::Unit u : the.enemy()->getUnits())
        {
            if (u->getType() == BWAPI::UnitTypes::Spell_Scanner_Sweep)
            {
                // NOTE getRemoveTimer() returns 0 for enemy scans.
                _enemyScans.insert(u);
				_enemyHasMobileDetection = true;
            }
        }
    }
}

bool InformationManager::isEnemyBuildingInRegion(const Zone * zone) 
{
    // Invalid zones aren't considered the same.
    if (!zone)
    {
        return false;
    }

    for (const auto & kv : _unitData[_enemy].getUnits())
    {
        const UnitInfo & ui(kv.second);
        if (ui.type.isBuilding() && !ui.goneFromLastPosition && !ui.lifted)
        {
            if (zone->id() == the.zone.at(ui.lastPosition)) 
            {
                return true;
            }
        }
    }

    return false;
}

const UIMap & InformationManager::getUnitInfo(BWAPI::Player player) const
{
    return getUnitData(player).getUnits();
}

std::set<const Zone *> & InformationManager::getOccupiedRegions(BWAPI::Player player)
{
    return _occupiedRegions[player];
}

int InformationManager::getAir2GroundSupply(BWAPI::Player player) const
{
    int supply = 0;

    for (const auto & kv : getUnitData(player).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isFlyer() && UnitUtil::TypeCanAttackGround(ui.type))
        {
            supply += ui.type.supplyRequired();
        }
    }

    return supply;
}

void InformationManager::drawExtendedInterface() const
{
    if (!Config::Debug::DrawUnitHealthBars)
    {
        return;
    }

    int verticalOffset = -10;

    // draw enemy units
    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        BWAPI::UnitType type(ui.type);
        int hitPoints = ui.lastHP;
        int shields = ui.lastShields;

        const BWAPI::Position & pos = ui.lastPosition;

        int left    = pos.x - type.dimensionLeft();
        int right   = pos.x + type.dimensionRight();
        int top     = pos.y - type.dimensionUp();
        int bottom  = pos.y + type.dimensionDown();

        if (!BWAPI::Broodwar->isVisible(BWAPI::TilePosition(ui.lastPosition)))
        {
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, top), BWAPI::Position(right, bottom), BWAPI::Colors::Grey, false);
            BWAPI::Broodwar->drawTextMap(BWAPI::Position(left + 3, top + 4), "%s %c",
                ui.type.getName().c_str(),
                ui.goneFromLastPosition ? 'X' : ' ');
        }
        
        if (!type.isResourceContainer() && type.maxHitPoints() > 0)
        {
            double hpRatio = (double)hitPoints / (double)type.maxHitPoints();
        
            BWAPI::Color hpColor = BWAPI::Colors::Green;
            if (hpRatio < 0.66) hpColor = BWAPI::Colors::Orange;
            if (hpRatio < 0.33) hpColor = BWAPI::Colors::Red;

            int ratioRight = left + (int)((right-left) * hpRatio);
            int hpTop = top + verticalOffset;
            int hpBottom = top + 4 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), hpColor, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }

        if (!type.isResourceContainer() && type.maxShields() > 0)
        {
            double shieldRatio = (double)shields / (double)type.maxShields();
        
            int ratioRight = left + (int)((right-left) * shieldRatio);
            int hpTop = top - 3 + verticalOffset;
            int hpBottom = top + 1 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), BWAPI::Colors::Blue, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }
    }

    // draw neutral units and our units
    for (BWAPI::Unit unit : BWAPI::Broodwar->getAllUnits())
    {
        if (unit->getPlayer() == _enemy)
        {
            continue;
        }

        const BWAPI::Position & pos = unit->getPosition();

        int left    = pos.x - unit->getType().dimensionLeft();
        int right   = pos.x + unit->getType().dimensionRight();
        int top     = pos.y - unit->getType().dimensionUp();

        //BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, top), BWAPI::Position(right, bottom), BWAPI::Colors::Grey, false);

        if (!unit->getType().isResourceContainer() && unit->getType().maxHitPoints() > 0)
        {
            double hpRatio = (double)unit->getHitPoints() / (double)unit->getType().maxHitPoints();
        
            BWAPI::Color hpColor = BWAPI::Colors::Green;
            if (hpRatio < 0.66) hpColor = BWAPI::Colors::Orange;
            if (hpRatio < 0.33) hpColor = BWAPI::Colors::Red;

            int ratioRight = left + (int)((right-left) * hpRatio);
            int hpTop = top + verticalOffset;
            int hpBottom = top + 4 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), hpColor, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }

        if (!unit->getType().isResourceContainer() && unit->getType().maxShields() > 0)
        {
            double shieldRatio = (double)unit->getShields() / (double)unit->getType().maxShields();
        
            int ratioRight = left + (int)((right-left) * shieldRatio);
            int hpTop = top - 3 + verticalOffset;
            int hpBottom = top + 1 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), BWAPI::Colors::Blue, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }

        if (unit->getType().isResourceContainer() && unit->getInitialResources() > 0)
        {
            
            double mineralRatio = (double)unit->getResources() / (double)unit->getInitialResources();
        
            int ratioRight = left + (int)((right-left) * mineralRatio);
            int hpTop = top + verticalOffset;
            int hpBottom = top + 4 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), BWAPI::Colors::Cyan, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }
    }
}

void InformationManager::drawUnitInformation(int x, int y) 
{
    if (!Config::Debug::DrawEnemyUnitInfo)
    {
        return;
    }

    BWAPI::Broodwar->drawTextScreen(x, y-10, "\x03 Self Loss:\x04 Minerals: \x1f%d \x04Gas: \x07%d", _unitData[_self].getMineralsLost(), _unitData[_self].getGasLost());
    BWAPI::Broodwar->drawTextScreen(x, y, "\x03 Enemy Loss:\x04 Minerals: \x1f%d \x04Gas: \x07%d", _unitData[_enemy].getMineralsLost(), _unitData[_enemy].getGasLost());
    BWAPI::Broodwar->drawTextScreen(x, y+20, "\x04 UNIT NAME");
    BWAPI::Broodwar->drawTextScreen(x+140, y+20, "\x04#");
    BWAPI::Broodwar->drawTextScreen(x+160, y+20, "\x04X");

    int yspace = 0;

    for (BWAPI::UnitType t : BWAPI::UnitTypes::allUnitTypes()) 
    {
        int numUnits = the.your.seen.count(t) + the.your.inferred.count(t);
        int numDeadUnits = _unitData[_enemy].getNumDeadUnits(t);

        if (numUnits || numDeadUnits) 
        {
            char color = white;

            if (t.isDetector())			{ color = purple; }
            else if (t.canAttack())		{ color = red; }		
            else if (t.isBuilding())	{ color = yellow; }

            BWAPI::Broodwar->drawTextScreen(x, y+40+((yspace)*10), " %c%s", color, t.getName().c_str());
            BWAPI::Broodwar->drawTextScreen(x+140, y+40+((yspace)*10), "%c%d", color, numUnits);
            BWAPI::Broodwar->drawTextScreen(x+160, y+40+((yspace++)*10), "%c%d", color, numDeadUnits);
        }
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isBuilding())
        {
            char color = ui.completed ? cyan : orange;
            BWAPI::Broodwar->drawTextMap(ui.lastPosition.x, ui.lastPosition.y - 20, "%c%d", color, ui.completeBy);
        }
    }
}

void InformationManager::drawResourceAmounts() const
{
    if (!Config::Debug::DrawResourceAmounts)
    {
        return;
    }

    const BWAPI::Position offset(-20, -16);
    const BWAPI::Position nextLineOffset(-24, -6);

    for (const std::pair<BWAPI::Unit, ResourceInfo> & r : _resources)
    {
        BWAPI::Position xy = r.first->getInitialPosition();
        if (r.second.isAccessible())
        {
            BWAPI::Broodwar->drawTextMap(xy + offset, "%c%d", white, r.second.getAmount());
        }
        else
        {
            char color = r.second.isMineralPatch() ? (r.second.isDestroyed() ? darkRed : cyan) : green;
            BWAPI::Broodwar->drawTextMap(xy + offset, "%c%d", color, r.second.getAmount());
            BWAPI::Broodwar->drawTextMap(xy + nextLineOffset, "%c@ %d", color, r.second.getFrame());
        }
    }
}

void InformationManager::maybeClearNeutral(BWAPI::Unit unit)
{
    if (unit && unit->getPlayer() == the.neutral() && unit->getType().isBuilding())
    {
        the.bases.clearNeutral(unit);
    }
}

// Both completed AND UNCOMPLETED static defense buildings, including shield batteries.
void InformationManager::maybeAddStaticDefense(BWAPI::Unit unit)
{
    if (unit &&
		unit->exists() &&
		unit->getPlayer() == _self &&
		UnitUtil::IsStaticDefense(unit->getType()))
    {
        _staticDefense.insert(unit);
    }
}

void InformationManager::updateUnit(BWAPI::Unit unit)
{
    if (unit->getPlayer() == _self || unit->getPlayer() == _enemy)
    {
        _unitData[unit->getPlayer()].updateUnit(unit);
    }
}

void InformationManager::onUnitDestroy(BWAPI::Unit unit) 
{

    if (unit->getPlayer() == _self || unit->getPlayer() == _enemy)
    {
        _unitData[unit->getPlayer()].removeUnit(unit);

        // If it is our static defense, remove it.
        if (unit->getPlayer() == _self && UnitUtil::IsStaticDefense(unit->getType()))
        {
            _staticDefense.erase(unit);
        }
    }
    else
    {
        // Should be neutral.
        the.bases.clearNeutral(unit);
    }
}

// Only returns units expected to be completed.
void InformationManager::getNearbyForce(std::vector<UnitInfo> & unitsOut, BWAPI::Position p, BWAPI::Player player, int radius) const
{
    for (const auto & kv : getUnitData(player).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (UnitUtil::IsCombatSimUnit(ui) &&
            !ui.goneFromLastPosition &&
            ui.isCompleted() &&
            ui.powered)
        {
            if (ui.type == BWAPI::UnitTypes::Terran_Medic)
            {
                // Spellcasters that the combat simulator is able to simulate.
                if (ui.lastPosition.getDistance(p) <= radius + 64)
                {
                    unitsOut.push_back(ui);
                }
            }
            else
            {
                // Non-spellcasters, aka units with weapons that have a range.

                // Determine its attack range, in the worst case.
                int range = UnitUtil::GetMaxAttackRange(ui.type);

                // Include it if it can attack into the radius we care about (with fudge factor).
                if (range && ui.lastPosition.getDistance(p) <= radius + range + 32)
                {
                    unitsOut.push_back(ui);
                }
            }
            // NOTE FAP does not support detectors.
            // else if (ui.type.isDetector() && ui.lastPosition.getDistance(p) <= (radius + 250))
            // {
            //	unitsOut.push_back(ui);
            // }
        }
    }
}

// We have completed combat units (excluding workers).
// This is a latch, initially false and set true forever when we get our first combat units.
bool InformationManager::weHaveCombatUnits()
{
    // Latch: Once we have combat units, pretend we always have them.
    if (_weHaveCombatUnits)
    {
        return true;
    }

    for (BWAPI::Unit u : _self->getUnits())
    {
        if (!u->getType().isWorker() &&
            !u->getType().isBuilding() &&
            u->isCompleted() &&
            !u->getType().isSpell() &&
            u->getType() != BWAPI::UnitTypes::Zerg_Larva &&
            u->getType() != BWAPI::UnitTypes::Zerg_Overlord)
        {
            _weHaveCombatUnits = true;
            return true;
        }
    }

    return false;
}

// We have air units. If we're zerg, we have overlords from the start.
// Floating buildings count. They need to navigate too.
bool InformationManager::weHaveAirUnits()
{
	// Latch: Once we have air units, pretend we always have them.
	if (_weHaveAirUnits)
	{
		return true;
	}

	if (the.selfRace() == BWAPI::Races::Zerg)
	{
		_weHaveAirUnits = true;
		return true;
	}

	for (BWAPI::Unit u : _self->getUnits())
	{
		if (u->isFlying() && u->isCompleted())
		{
			_weHaveAirUnits = true;
			return true;
		}
	}

	return false;
}

// For some spells, the best or only way to find out whether the enemy has them
// is to look at units to see if the spells are ordered or in effect.
// NOTE If Steamhammer casts the same spell on itself, it has to remember
//      what unit it is on, so that this does not take it as an enemy ability!
//      So far, it doesn't cast any of the spells it can detect.
// NOTE Restoration: We have to remember what units we've parasited, etc.
//      so we can detect Restoration by the effect being suddenly removed.
//      In most cases, we won't see the enemy medic casting.
//      It's not implemented.
// NOTE Plague: We don't want to check our units for it, because we often
//      miss when aiming at enemy units. We'll get wrong answers.
//      We detect plague in updateBullets().
void InformationManager::updateEnemySpells()
{
	if (the.enemyRace() == BWAPI::Races::Terran)
	{
		if (!_enemyHasStim)
		{
			for (BWAPI::Unit u : the.enemy()->getUnits())
			{
				if (u->isStimmed())
				{
					_enemyHasStim = true;
					break;
				}
			}
		}
		if (!_enemyHasOpticFlare)
		{
			for (BWAPI::Unit u : the.self()->getUnits())
			{
				if (u->isBlind())
				{
					_enemyHasOpticFlare = true;
					break;
				}
			}
		}
		if (!_enemyHasIrradiate)
		{
			for (BWAPI::Unit u : the.self()->getUnits())
			{
				if (u->isIrradiated())
				{
					_enemyHasIrradiate = true;
					break;
				}
			}
		}
		if (the.your.seen.count(BWAPI::UnitTypes::Terran_Ghost) > 0 && !_enemyHasLockdown)
		{
			for (BWAPI::Unit u : the.self()->getUnits())
			{
				if (u->isLockedDown())
				{
					_enemyHasLockdown = true;
					break;
				}
			}
		}
		return;
	}
	
	if (the.enemyRace() == BWAPI::Races::Protoss)
	{
		if (the.your.seen.count(BWAPI::UnitTypes::Protoss_Arbiter) > 0 && !_enemyHasStasis)
		{
			// NOTE This doesn't notice if the enemy uses stasis on their own units.
			for (BWAPI::Unit u : the.self()->getUnits())
			{
				if (u->isStasised())
				{
					_enemyHasStasis = true;
					break;
				}
			}
		}
		if (the.your.seen.count(BWAPI::UnitTypes::Protoss_Dark_Archon) > 0 && !_enemyHasMaelstrom)
		{
			for (BWAPI::Unit u : the.self()->getUnits())
			{
				if (u->isMaelstrommed())
				{
					_enemyHasMaelstrom = true;
					break;
				}
			}
		}
		if (the.your.seen.count(BWAPI::UnitTypes::Protoss_Corsair) > 0 && !_enemyHasDWeb)
		{
			for (BWAPI::Unit u : the.enemy()->getUnits())
			{
				if (u->getType() == BWAPI::UnitTypes::Spell_Disruption_Web)
				{
					_enemyHasDWeb = true;
					break;
				}
			}
		}
		return;
	}

	if (the.enemyRace() == BWAPI::Races::Zerg)
	{
		// Check if they own any broodlings.
		if (the.your.seen.count(BWAPI::UnitTypes::Zerg_Queen) > 0 && !_enemyHasBroodling)
		{
			for (BWAPI::Unit u : the.enemy()->getUnits())
			{
				if (u->getType() == BWAPI::UnitTypes::Zerg_Broodling)
				{
					_enemyHasBroodling = true;
					break;
				}
			}
		}

		return;
	}
}

// Enemy has completed combat units (excluding workers).
bool InformationManager::enemyHasCombatUnits()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasCombatUnits)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (!ui.type.isWorker() &&
            !ui.type.isBuilding() &&
            ui.isCompleted() &&
            ui.type != BWAPI::UnitTypes::Zerg_Larva &&
            ui.type != BWAPI::UnitTypes::Zerg_Overlord)
        {
            _enemyHasCombatUnits = true;
            return true;
        }
    }

    return false;
}

// Enemy has spore colonies, photon cannons, or turrets.
bool InformationManager::enemyHasStaticAntiAir()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasStaticAntiAir)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
            ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
            ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony)
        {
            _enemyHasStaticAntiAir = true;
            return true;
        }
    }

    return false;
}

// Enemy has mobile units that can shoot up, or the tech to produce them.
bool InformationManager::enemyHasAntiAir()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasAntiAir)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (
            // For terran, anything other than SCV, command center, depot is a hit.
            // Surely nobody makes ebay before barracks!
            (_enemy->getRace() == BWAPI::Races::Terran &&
            ui.type != BWAPI::UnitTypes::Terran_SCV &&
            ui.type != BWAPI::UnitTypes::Terran_Command_Center &&
            ui.type != BWAPI::UnitTypes::Terran_Supply_Depot)

            ||

            // Otherwise, any mobile unit that has an air weapon.
            (!ui.type.isBuilding() && UnitUtil::TypeCanAttackAir(ui.type))

            ||

            // Or a building for making such a unit.
            // The cyber core only counts once it is finished, other buildings earlier.
            ui.type == BWAPI::UnitTypes::Protoss_Cybernetics_Core && ui.isCompleted() ||
            ui.type == BWAPI::UnitTypes::Protoss_Stargate ||
            ui.type == BWAPI::UnitTypes::Protoss_Fleet_Beacon ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
            ui.type == BWAPI::UnitTypes::Zerg_Hydralisk_Den ||
            ui.type == BWAPI::UnitTypes::Zerg_Spire ||
            ui.type == BWAPI::UnitTypes::Zerg_Greater_Spire

            )
        {
            _enemyHasAntiAir = true;
            return true;
        }
    }

    return false;
}

// Enemy has air units or air-producing tech.
// Overlords and lifted buildings are excluded.
// A queen's nest is not air tech--it's usually a prerequisite for hive
// rather than to make queens. So we have to see a queen for it to count.
// Protoss robo fac and terran starport are taken to imply air units.
bool InformationManager::enemyHasAirTech()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasAirTech)
    {
        return true;
    }

	if (enemyHas8Interceptors())
	{
		_enemyHasAirTech = true;
		_enemyHasAirToGround = true;
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isFlyer() && ui.type != BWAPI::UnitTypes::Zerg_Overlord && !ui.type.isSpell() ||
            ui.type == BWAPI::UnitTypes::Terran_Starport ||
            ui.type == BWAPI::UnitTypes::Terran_Control_Tower ||
            ui.type == BWAPI::UnitTypes::Terran_Science_Facility ||
            ui.type == BWAPI::UnitTypes::Terran_Covert_Ops ||
            ui.type == BWAPI::UnitTypes::Terran_Physics_Lab ||
            ui.type == BWAPI::UnitTypes::Protoss_Stargate ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
            ui.type == BWAPI::UnitTypes::Protoss_Fleet_Beacon ||
            ui.type == BWAPI::UnitTypes::Protoss_Robotics_Facility ||
            ui.type == BWAPI::UnitTypes::Protoss_Robotics_Support_Bay ||
            ui.type == BWAPI::UnitTypes::Protoss_Observatory ||
            ui.type == BWAPI::UnitTypes::Zerg_Spire ||
            ui.type == BWAPI::UnitTypes::Zerg_Greater_Spire)
        {
            _enemyHasAirTech = true;
            return true;
        }
    }

    return false;
}

// Enemy has air units that can shoot down, excluding vessels and queens.
// Assume battlecruisers if we see physics lab. Though note: Krasi0 builds one and doesn't use it.
// Assume arbiters if we see arbiter tribunal, and carriers if we see fleet beacon.
bool InformationManager::enemyHasAirToGround()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasAirToGround)
	{
		return true;
	}

	if (enemyHas8Interceptors())
	{
		_enemyHasAirTech = true;
		_enemyHasAirToGround = true;
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Terran_Wraith ||
			ui.type == BWAPI::UnitTypes::Terran_Physics_Lab ||
			ui.type == BWAPI::UnitTypes::Terran_Battlecruiser ||
			ui.type == BWAPI::UnitTypes::Protoss_Scout ||
			ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
			ui.type == BWAPI::UnitTypes::Protoss_Arbiter ||
			ui.type == BWAPI::UnitTypes::Protoss_Fleet_Beacon ||
			ui.type == BWAPI::UnitTypes::Protoss_Carrier ||
			ui.type == BWAPI::UnitTypes::Zerg_Mutalisk ||
			ui.type == BWAPI::UnitTypes::Zerg_Scourge ||		// not air-to-ground but the same tech
			ui.type == BWAPI::UnitTypes::Zerg_Guardian)
		{
			_enemyHasAirTech = true;
			_enemyHasAirToGround = true;
			return true;
		}
	}

	return false;
}

// This test is good for "can I benefit from detection?"
// NOTE The enemySeenBurrowing() call also sets _enemyHasCloakTech . So do lurker spines.
bool InformationManager::enemyHasCloakTech()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasCloakTech)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.hasPermanentCloak() ||                             // DT, observer
            ui.type.isCloakable() ||                                   // wraith, ghost
            ui.type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine ||
            ui.type == BWAPI::UnitTypes::Protoss_Citadel_of_Adun ||    // assume DT
            ui.type == BWAPI::UnitTypes::Protoss_Templar_Archives ||   // assume DT
            ui.type == BWAPI::UnitTypes::Protoss_Observatory ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker_Egg ||
            ui.unit->isBurrowed())
        {
            _enemyHasCloakTech = true;
            return true;
        }
    }

    return false;
}

// This test means more "can I be SURE that I will benefit from detection?"
// It only counts actual cloaked units, not merely the tech for them,
// and does not worry about observers.
// NOTE The enemySeenBurrowing() call also sets _enemyCloakedUnitsSeen. So do lurker spines.
// NOTE If they have cloaked units, they have cloak tech. Set all appropriate flags.
bool InformationManager::enemyCloakedUnitsSeen()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyCloakedUnitsSeen)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isCloakable() ||                                    // wraith, ghost
            ui.type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine ||
            ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker_Egg ||
            ui.unit->isBurrowed())
        {
            _enemyHasCloakTech = true;
            _enemyCloakedUnitsSeen = true;
            _enemyHasMobileCloakTech = true;
            return true;
        }
    }

    return false;
}

// This test is better for "do I need detection to live?"
// It doesn't worry about spider mines, observers, or burrowed units except lurkers.
// NOTE If they have cloaked units, they have cloak tech. Set all appropriate flags.
bool InformationManager::enemyHasMobileCloakTech()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasMobileCloakTech)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isCloakable() ||                                   // wraith, ghost
            ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar ||
            ui.type == BWAPI::UnitTypes::Protoss_Citadel_of_Adun ||    // assume DT
            ui.type == BWAPI::UnitTypes::Protoss_Templar_Archives ||   // assume DT
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker_Egg)
        {
            _enemyHasCloakTech = true;
            _enemyHasMobileCloakTech = true;
            return true;
        }
    }

    return false;
}

// Enemy has cloaked wraiths or has arbiters.
// Observers do not count.
bool InformationManager::enemyHasAirCloakTech()
{
	if (_enemyHasAirCloakTech)
	{
		return true;
	}

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        // We have to see a wraith that is cloaked to be sure.
        if (ui.type == BWAPI::UnitTypes::Terran_Wraith && ui.unit->isCloaked())
        {
            _enemyHasCloakTech = true;
            _enemyHasMobileCloakTech = true;
            _enemyHasAirCloakTech = true;
			_enemyCloakedUnitsSeen = true;
			return true;
        }

        if (ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter)
        {
            _enemyHasCloakTech = true;
            _enemyHasMobileCloakTech = true;
            _enemyHasAirCloakTech = true;
			_enemyCloakedUnitsSeen = true;
			return true;
        }
    }

    return false;
}

// Enemy has air units good for hunting down overlords.
// A stargate counts, but not a fleet beacon or arbiter tribunal.
// A starport does not count; it may well be for something else.
bool InformationManager::enemyHasOverlordHunters()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasOverlordHunters)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type == BWAPI::UnitTypes::Terran_Wraith ||
            ui.type == BWAPI::UnitTypes::Terran_Valkyrie ||
            ui.type == BWAPI::UnitTypes::Terran_Battlecruiser ||
            ui.type == BWAPI::UnitTypes::Protoss_Corsair ||
            ui.type == BWAPI::UnitTypes::Protoss_Scout ||
            ui.type == BWAPI::UnitTypes::Protoss_Carrier ||
            ui.type == BWAPI::UnitTypes::Protoss_Stargate ||
            ui.type == BWAPI::UnitTypes::Zerg_Spire ||
            ui.type == BWAPI::UnitTypes::Zerg_Greater_Spire ||
            ui.type == BWAPI::UnitTypes::Zerg_Mutalisk ||
            ui.type == BWAPI::UnitTypes::Zerg_Scourge)
        {
            _enemyHasOverlordHunters = true;
            _enemyHasAirTech = true;
            return true;
        }
    }

    return false;
}

void InformationManager::enemySeenBurrowing(BWAPI::Unit unit)
{
    _enemyHasCloakTech = true;
    _enemyCloakedUnitsSeen = true;
	if (unit->getType() == BWAPI::UnitTypes::Zerg_Lurker)
	{
		_enemyHasMobileCloakTech = true;	// burrowed lurkers are arbitrarily considered "mobile"
	}
	else
	{
		_enemyHasBurrow = true;				// other burrowed units are considered "immobile"
	}
}

// Look up when an enemy building finished, or is predicted to finish.
// If none, give a time far in the future.
// This is for checking the timing of enemy tech buildings.
int InformationManager::getEnemyBuildingTiming(BWAPI::UnitType type) const
{
    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type == type)
        {
            return ui.completeBy;
        }
    }

    // "Infinite" time in the future.
    return MAX_FRAME;
}

// Return the remaining build time for one of our buildings, given only its type.
// This is for checking the timing of our tech buildings.
// If already completed, return 0. If not under construction, return "never".
int InformationManager::remainingBuildTime(BWAPI::UnitType type) const
{
    for (BWAPI::Unit unit : the.self()->getUnits())
    {
        if (unit->getType() == type)
        {
            return unit->getRemainingBuildTime();
        }
    }

    return MAX_FRAME;
}

// If we are making more than one spire, return the earliest.
int InformationManager::getMySpireTiming() const
{
    if (the.my.completed.count(BWAPI::UnitTypes::Zerg_Spire) > 0 ||
        the.my.all.count(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0)
    {
        // The spire is complete, return an early time.
        return 1;
    }

    int frame = MAX_FRAME;
    if (the.my.all.count(BWAPI::UnitTypes::Zerg_Spire) > 0)
    {
        for (BWAPI::Unit unit : _self->getUnits())
        {
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Spire)
            {
                int f = the.now() + unit->getRemainingBuildTime();
                if (f < frame)
                {
                    frame = f;
                }
            }
        }
    }

    return frame;
}

// Enemy has spore colonies, photon cannons, turrets, or spider mines.
// It's the same as enemyHasStaticAntiAir() except for spider mines.
// Spider mines only catch cloaked ground units, so this routine is not for countering wraiths.
bool InformationManager::enemyHasStaticDetection()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasStaticDetection)
    {
        return true;
    }

    if (enemyHasStaticAntiAir())
    {
        _enemyHasStaticDetection = true;
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine)
        {
            _enemyHasStaticDetection = true;
            return true;
        }
    }

    return false;
}

// Enemy has overlords, observers, comsat, or science vessels.
bool InformationManager::enemyHasMobileDetection()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasMobileDetection)
    {
        return true;
    }

    // If the enemy is zerg, they have overlords.
    // If they went random, we may not have known until now.
    if (_enemy->getRace() == BWAPI::Races::Zerg)
    {
        _enemyHasMobileDetection = true;
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

		// Scanner sweeps are noticed elsewhere.
        if (ui.type == BWAPI::UnitTypes::Terran_Comsat_Station ||
            ui.type == BWAPI::UnitTypes::Terran_Science_Facility ||
            ui.type == BWAPI::UnitTypes::Terran_Science_Vessel ||
            ui.type == BWAPI::UnitTypes::Protoss_Observatory ||
            ui.type == BWAPI::UnitTypes::Protoss_Observer)
        {
            _enemyHasMobileDetection = true;
            return true;
        }
    }

    return false;
}

// The enemy may use drop, take islands, etc.
bool InformationManager::enemyHasTransport()
{
	if (_enemyHasTransport)
	{
		return true;
	}
	
	if (the.enemyRace() == BWAPI::Races::Zerg)
	{
		_enemyHasTransport = the.enemy()->getUpgradeLevel(BWAPI::UpgradeTypes::Ventral_Sacs) > 0;
	}
	else
	{
		_enemyHasTransport =
			the.your.ever.count(BWAPI::UnitTypes::Terran_Dropship) > 0 ||
			the.your.ever.count(BWAPI::UnitTypes::Protoss_Shuttle) > 0;
	}
	return _enemyHasTransport;
}

bool InformationManager::enemyHasSiegeMode()
{
    // Only terran can get siege mode. Ignore the possibility of protoss mind control.
    if (_enemy->getRace() != BWAPI::Races::Terran)
    {
        return false;
    }

    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasSiegeMode)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        // If the tank is in the process of sieging, it is still in tank mode.
        // If it is unsieging, it is still in siege mode. So this condition catches everything.
        if (ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
            ui.unit->getOrder() == BWAPI::Orders::Sieging)
        {
            _enemyHasSiegeMode = true;
            return true;
        }
    }

    return false;
}

// Enemy vultures can lay spider mines.
bool InformationManager::enemyHasMines()
{
	if (the.enemyRace() != BWAPI::Races::Terran)
	{
		return false;
	}

	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasMines)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine ||
			ui.unit->isVisible() && ui.unit->getOrder() == BWAPI::Orders::PlaceMine)
		{
			_enemyHasMines = true;
			return true;
		}
	}

	return false;
}

// NOTE updateNukeDots() can also set _enemyhasNuke.
bool InformationManager::enemyHasNuke()
{
	if (the.enemyRace() != BWAPI::Races::Terran)
	{
		return false;
	}

	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasNuke)
	{
		return true;
	}

	if (the.your.ever.count(BWAPI::UnitTypes::Terran_Nuclear_Silo) > 0 ||
		the.your.ever.count(BWAPI::UnitTypes::Terran_Nuclear_Missile) > 0)
	{
			_enemyHasNuke = true;
			return true;
	}

	return false;
}

// The enemy zerg has burrow tech (with or without lurkers).
bool InformationManager::enemyHasBurrow()
{
	if (the.enemyRace() != BWAPI::Races::Zerg)
	{
		return false;
	}

	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasBurrow)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.burrowed &&
			ui.type != BWAPI::UnitTypes::Zerg_Lurker)
		{
			_enemyHasCloakTech = true;
			_enemyCloakedUnitsSeen = true;
			_enemyHasBurrow = true;
			return true;
		}
	}

	return false;
}

// NOTE updateBullets() checks for lurker spines.
bool InformationManager::enemyHasLurkers()
{
	if (the.enemyRace() != BWAPI::Races::Zerg)
	{
		return false;
	}

	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasLurkers)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Zerg_Lurker || ui.type == BWAPI::UnitTypes::Zerg_Lurker_Egg)
		{
			_enemyHasCloakTech = true;
			_enemyHasMobileCloakTech = true;
			_enemyCloakedUnitsSeen = true;
			_enemyHasLurkers = true;
			return true;
		}
	}

	return false;
}

bool InformationManager::weHaveCloakTech() const
{
    return
        BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Cloaking_Field) ||
        BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Personnel_Cloaking) ||
        the.my.completed.count(BWAPI::UnitTypes::Protoss_Dark_Templar) > 0 ||
        the.my.completed.count(BWAPI::UnitTypes::Protoss_Arbiter) > 0 ||
        BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Burrowing) ||
        BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect);
}

// Our nearest static defense building that can hit ground, by air distance.
// Null if none.
// NOTE This assumes that we never put medics or SCVs into a bunker.
BWAPI::Unit InformationManager::nearestGroundStaticDefense(BWAPI::Position pos) const
{
    int closestDist = MAX_DISTANCE;
    BWAPI::Unit closest = nullptr;
    for (BWAPI::Unit building : _staticDefense)
    {
        if (building->getType() == BWAPI::UnitTypes::Terran_Bunker && !building->getLoadedUnits().empty() ||
            building->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
            building->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony)
        {
            int dist = building->getDistance(pos);
            if (dist < closestDist)
            {
                closestDist = dist;
                closest = building;
            }
        }
    }
    return closest;
}

// Our nearest static defense building that can hit air, by air distance.
// Null if none.
// NOTE This assumes that we only put marines into a bunker: If it is loaded, it can shoot air.
// If we ever put firebats or SCVs or medics into a bunker, we'll have to do a fancier check.
BWAPI::Unit InformationManager::nearestAirStaticDefense(BWAPI::Position pos) const
{
    int closestDist = MAX_DISTANCE;
    BWAPI::Unit closest = nullptr;
    for (BWAPI::Unit building : _staticDefense)
    {
        if (building->getType() == BWAPI::UnitTypes::Terran_Missile_Turret ||
            building->getType() == BWAPI::UnitTypes::Terran_Bunker && !building->getLoadedUnits().empty() ||
            building->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon || 
            building->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony)
        {
            int dist = building->getDistance(pos);
            if (dist < closestDist)
            {
                closestDist = dist;
                closest = building;
            }
        }
    }
    return closest;
}

// Our nearest shield battery, by air distance.
// Null if none.
BWAPI::Unit InformationManager::nearestShieldBattery(BWAPI::Position pos) const
{
    if (_self->getRace() == BWAPI::Races::Protoss)
    {
        int closestDist = MAX_DISTANCE;
        BWAPI::Unit closest = nullptr;
        for (BWAPI::Unit building : _staticDefense)
        {
            if (building->getType() == BWAPI::UnitTypes::Protoss_Shield_Battery)
            {
                int dist = building->getDistance(pos);
                if (dist < closestDist)
                {
                    closestDist = dist;
                    closest = building;
                }
            }
        }
        return closest;
    }
    return nullptr;
}

// The given unit is next to a turret/cannon/spore colony.
bool InformationManager::inAirDefenseRange(BWAPI::Unit u) const
{
	return BWAPI::Broodwar->getClosestUnit(
		u->getPosition(),
		(BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Missile_Turret || BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Photon_Cannon || BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			BWAPI::Filter::GetPlayer == the.self() && BWAPI::Filter::IsCompleted && BWAPI::Filter::IsPowered,
		2 * 32 + 16
	) != nullptr;
}

// Look up whether we believe a given enemy bunker is loaded.
// updateBullets() adds it to the _loadedBunkers set if it is seen shooting,
// and removes it if it is seen not shooting when we are in range. In between,
// we assume without evidence that it remains as it was when we last checked.
bool InformationManager::isEnemyBunkerLoaded(BWAPI::Unit bunker) const
{
	return _loadedBunkers.contains(bunker);
}

// Zerg specific calculation: How many scourge hits are needed
// to kill the enemy's known air fleet?
// This counts individual units--you get 2 scourge per egg.
// One scourge does 110 normal damage.
// NOTE This ignores air armor, which might make a difference in rare cases.
int InformationManager::nScourgeNeeded() const
{
    int count = 0;

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        // A few unit types should not usually be scourged. Skip them.
        if (ui.type.isFlyer() &&
            ui.type != BWAPI::UnitTypes::Spell_Scanner_Sweep &&
            ui.type != BWAPI::UnitTypes::Zerg_Overlord &&
            ui.type != BWAPI::UnitTypes::Zerg_Scourge &&
            ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
        {
            int hp = ui.type.maxHitPoints() + ui.type.maxShields();      // assume the worst
            count += (hp + 109) / 110;
        }
    }

    return count;
}

// True when all overlords are next to spore colonies.
// It frees hydras to do more important work.
// NOTE If the enemy has cloaked units, ovies may be on the field as detectors.
//      Then the hydras are as distractible as ever.
// The caller should check that we're zerg, or the call only wastes time.
bool InformationManager::allOverlordsProtected() const
{
	for (BWAPI::Unit u : the.self()->getUnits())
	{
		if (u->getType() == BWAPI::UnitTypes::Zerg_Overlord)
		{
			if (!BWAPI::Broodwar->getClosestUnit(
				u->getPosition(),
				BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Spore_Colony,
				4 * 32
			))
			{
				return false;
			}
		}
	}
	return true;
}

const UnitData & InformationManager::getUnitData(BWAPI::Player player) const
{
    return _unitData.find(player)->second;
}

// Enemy units only. Return null if the enemy unit is not found.
const UnitInfo * InformationManager::getUnitInfo(BWAPI::Unit unit) const
{
    const auto & enemies = getUnitData(_enemy).getUnits();
    const auto it = enemies.find(unit);
    if (it != enemies.end())
    {
        return & (*it).second;
    }
    return nullptr;
}

// Return the set of enemy units targeting a given one of our units.
const BWAPI::Unitset & InformationManager::getEnemyFireteam(BWAPI::Unit ourUnit) const
{
    auto it = _yourTargets.find(ourUnit);
    if (it != _yourTargets.end())
    {
        return (*it).second;
    }
    return EmptyUnitSet;
}

// Return the last seen resource amount of a mineral patch or vespene geyser.
// NOTE Pass in the static unit of the resource container, or it won't work.
int InformationManager::getResourceAmount(BWAPI::Unit resource) const
{
    auto it = _resources.find(resource);
    if (it == _resources.end())
    {
        return 0;
    }
    return it->second.getAmount();
}

// Return whether a mineral patch has been destroyed by being mined out.
// NOTE Pass in the static unit of the resource container, or it won't work.
bool InformationManager::isMineralDestroyed(BWAPI::Unit resource) const
{
    auto it = _resources.find(resource);
    if (it == _resources.end())
    {
        return false;
    }
    return it->second.isDestroyed();
}

// Is this geyser, possibly out of sight, covered with a refinery (at last report)?
// NOTE Pass in a static initial geyser unit, or else getInitialPosition() will not work.
bool InformationManager::isGeyserTaken(BWAPI::Unit resource) const
{
    if (resource->isVisible())
    {
        // Our own refineries are always visible.
        return resource->getType().isRefinery();
    }

    // The geyser is either neutral or enemy, but in any case out of sight.
    // Check enemy units to see if it is known to be owned by the enemy.
    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        // If an enemy unit is at the same position, it can only be a refinery.
        if (ui.lastPosition == resource->getInitialPosition())
        {
            return true;
        }
    }

    return false;
}

// Return the unit type of our mobile detectors.
// TODO this seems to belong in UnitUtil instead
BWAPI::UnitType InformationManager::getDetectorType() const
{
	if (the.selfRace() == BWAPI::Races::Terran)
	{
		return BWAPI::UnitTypes::Terran_Science_Vessel;
	}
	if (the.selfRace() == BWAPI::Races::Protoss)
	{
		return BWAPI::UnitTypes::Protoss_Observer;
	}
	return BWAPI::UnitTypes::Zerg_Overlord;
}

InformationManager & InformationManager::Instance()
{
    static InformationManager instance;
    return instance;
}

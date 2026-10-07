#include "UnitUtil.h"
#include "UnitData.h"
#include "InformationManager.h"

using namespace UAlbertaBot;

// Building morphed from another, not constructed.
bool UnitUtil::IsMorphedBuildingType(BWAPI::UnitType type)
{
	return
		type == BWAPI::UnitTypes::Zerg_Sunken_Colony ||
		type == BWAPI::UnitTypes::Zerg_Spore_Colony ||
		type == BWAPI::UnitTypes::Zerg_Lair ||
		type == BWAPI::UnitTypes::Zerg_Hive ||
		type == BWAPI::UnitTypes::Zerg_Greater_Spire;
}

// Unit morphed from another, not spawned from a drone.
bool UnitUtil::IsMorphedUnitType(BWAPI::UnitType type)
{
	return
		type == BWAPI::UnitTypes::Zerg_Lurker ||
		type == BWAPI::UnitTypes::Zerg_Guardian ||
		type == BWAPI::UnitTypes::Zerg_Devourer;
}

// A partial substitute for t2.isSuccessorOf(t1) from BWAPI 4.2.0.
bool UnitUtil::BuildingIsMorphedFrom(BWAPI::UnitType t2, BWAPI::UnitType t1)
{
	return
		t1 == BWAPI::UnitTypes::Zerg_Creep_Colony && t2 == BWAPI::UnitTypes::Zerg_Sunken_Colony ||
		t1 == BWAPI::UnitTypes::Zerg_Creep_Colony && t2 == BWAPI::UnitTypes::Zerg_Spore_Colony ||
		t1 == BWAPI::UnitTypes::Zerg_Hatchery     && t2 == BWAPI::UnitTypes::Zerg_Lair ||
		t1 == BWAPI::UnitTypes::Zerg_Lair         && t2 == BWAPI::UnitTypes::Zerg_Hive ||
		t1 == BWAPI::UnitTypes::Zerg_Spire        && t2 == BWAPI::UnitTypes::Zerg_Greater_Spire;
}

// A lair or hive is a completed resource depot even if not a completed unit.
bool UnitUtil::IsCompletedResourceDepot(BWAPI::Unit unit)
{
	return
		unit &&
		unit->getType().isResourceDepot() &&
		(unit->isCompleted() || unit->getType() == BWAPI::UnitTypes::Zerg_Lair || unit->getType() == BWAPI::UnitTypes::Zerg_Hive);
}

// Building that requires pylon power.
bool UnitUtil::NeedsPylonPower(BWAPI::UnitType type)
{
	return
		type.getRace() == BWAPI::Races::Protoss &&
		type.isBuilding() &&
		type != BWAPI::UnitTypes::Protoss_Pylon &&
		type != BWAPI::UnitTypes::Protoss_Assimilator &&
		type != BWAPI::UnitTypes::Protoss_Nexus;
}

bool UnitUtil::IsStaticDefense(BWAPI::UnitType type)
{
	return
		type == BWAPI::UnitTypes::Zerg_Sunken_Colony ||
		type == BWAPI::UnitTypes::Zerg_Spore_Colony ||
		type == BWAPI::UnitTypes::Terran_Bunker ||
		type == BWAPI::UnitTypes::Terran_Missile_Turret ||
		type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
		type == BWAPI::UnitTypes::Protoss_Shield_Battery;
}

// Immobile defense buildings and combat units.
bool UnitUtil::IsImmobileDefense(BWAPI::UnitType type)
{
	return
		type == BWAPI::UnitTypes::Zerg_Sunken_Colony ||
		type == BWAPI::UnitTypes::Zerg_Spore_Colony ||
		type == BWAPI::UnitTypes::Zerg_Lurker ||
		type == BWAPI::UnitTypes::Terran_Bunker ||
		type == BWAPI::UnitTypes::Terran_Missile_Turret ||
		type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
		type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
		type == BWAPI::UnitTypes::Protoss_Shield_Battery;
}

// To stop static defense from being built, stop these things.
bool UnitUtil::IsComingStaticDefense(BWAPI::UnitType type)
{
	return
		type == BWAPI::UnitTypes::Zerg_Creep_Colony ||
		type == BWAPI::UnitTypes::Terran_Bunker ||
		type == BWAPI::UnitTypes::Terran_Missile_Turret ||
		type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
		type == BWAPI::UnitTypes::Protoss_Shield_Battery;
}

// This is an enemy combat unit for purposes of combat simulation.
// If it's our unit, it's better to call IsCombatSimUnit() directly with the BWAPI::Unit.
bool UnitUtil::IsCombatSimUnit(const UnitInfo & ui)
{
	UAB_ASSERT(ui.unit, "no unit");
	return ui.unit->exists()
		? UnitUtil::IsCombatSimUnit(ui.unit)
		: UnitUtil::IsCombatSimUnit(ui.type);
}

// This is a combat unit for purposes of combat simulation.
bool UnitUtil::IsCombatSimUnit(BWAPI::Unit unit)
{
	if (!unit->isCompleted() ||
		!unit->isPowered() ||
		unit->isLockedDown() |
		unit->isMaelstrommed() ||
		unit->isUnderDisruptionWeb() ||
		unit->isStasised())
	{
		return false;
	}

	// A worker counts as a combat unit if it has been given an order to attack.
	if (unit->getType().isWorker())
	{
		return
			unit->getOrder() == BWAPI::Orders::AttackMove ||
			unit->getOrder() == BWAPI::Orders::AttackTile ||
			unit->getOrder() == BWAPI::Orders::AttackUnit ||
            unit->getOrder() == BWAPI::Orders::Patrol;
	}

	return IsCombatSimUnit(unit->getType());
}

// This is a combat unit type for purposes of combat simulation.
// Treat workers as non-combat units (overridden above for some workers).
// The combat simulation does not support spells other than medic healing and stim,
// and it does not understand detectors.
// The combat sim treats carriers as the attack unit, not their interceptors (bftjoe).
bool UnitUtil::IsCombatSimUnit(BWAPI::UnitType type)
{
	if (type.isWorker())
	{
		return false;
	}

	if (type == BWAPI::UnitTypes::Protoss_Interceptor)
	{
		return false;
	}

	return
		TypeCanAttackAir(type) ||
		TypeCanAttackGround(type) ||
		type == BWAPI::UnitTypes::Terran_Medic;
}

bool UnitUtil::IsCombatUnit(BWAPI::UnitType type)
{
	// No workers, buildings, or carrier interceptors (which are not controllable).
	// Buildings include static defense buildings; they are not put into squads.
	if (type.isWorker() ||
		type.isBuilding() ||
		type == BWAPI::UnitTypes::Protoss_Interceptor)  // apparently, they canAttack()
	{
		return false;
	}

	return
		type.canAttack() ||                             // includes carriers and reavers
		type.isDetector() ||
		type == BWAPI::UnitTypes::Zerg_Queen ||
		type == BWAPI::UnitTypes::Zerg_Defiler ||
		type == BWAPI::UnitTypes::Terran_Medic ||
		type == BWAPI::UnitTypes::Protoss_High_Templar ||
		type == BWAPI::UnitTypes::Protoss_Dark_Archon ||
		type.isFlyer() && type.spaceProvided() > 0;     // transports
}

bool UnitUtil::IsCombatUnit(BWAPI::Unit unit)
{
	UAB_ASSERT(unit != nullptr, "Unit was null");

	return unit && unit->isCompleted() && IsCombatUnit(unit->getType());
}

bool UnitUtil::IsValidUnit(BWAPI::Unit unit)
{
	return unit
		&& unit->exists()
		&& (unit->isCompleted() || IsMorphedBuildingType(unit->getType()))
		&& unit->getHitPoints() > 0
		&& unit->getType() != BWAPI::UnitTypes::Unknown
		&& (unit->getPosition().isValid() || unit->isLoaded())     // position is invalid if loaded in transport or bunker
		&& unit->getPlayer() == BWAPI::Broodwar->self();           // catches mind controlled units
}

bool UnitUtil::IsRangedUnit(BWAPI::UnitType type)
{
	return
		type.groundWeapon().maxRange() > 32 ||
		type.isFlyer() ||
		type == BWAPI::UnitTypes::Protoss_Reaver;
}

Rect UnitUtil::GetRect(BWAPI::Unit unit)
{
    Rect r;

    r.x = unit->getLeft();
    r.y = unit->getTop();
    r.height = unit->getBottom() - unit->getTop();
    r.width = unit->getLeft() - unit->getRight();

    return r;
}

double UnitUtil::GetDistanceBetweenTwoRectangles(Rect & rect1, Rect & rect2)
{
    Rect & mostLeft = rect1.x < rect2.x ? rect1 : rect2;
    Rect & mostRight = rect2.x < rect1.x ? rect1 : rect2;
    Rect & upper = rect1.y < rect2.y ? rect1 : rect2;
    Rect & lower = rect2.y < rect1.y ? rect1 : rect2;

    int diffX = std::max(0, mostLeft.x == mostRight.x ? 0 : mostRight.x - (mostLeft.x + mostLeft.width));
    int diffY = std::max(0, upper.y == lower.y ? 0 : lower.y - (upper.y + upper.height));
    
    return std::sqrtf(static_cast<float>(diffX*diffX + diffY*diffY));
}

bool UnitUtil::CanAttack(BWAPI::Unit attacker, BWAPI::Unit target)
{
	return CanAttack(attacker->getType(), target->getType());
}

// Accounts for cases where units can attack without a weapon of their own.
// Ignores spellcasters, which have limitations on their attacks.
// For example, high templar can attack air or ground mobile units, but can't attack buildings.
bool UnitUtil::CanAttack(BWAPI::UnitType attacker, BWAPI::UnitType target)
{
	return target.isFlyer() ? TypeCanAttackAir(attacker) : TypeCanAttackGround(attacker);
}

bool UnitUtil::TypeCanAttack(BWAPI::UnitType attacker)
{
	return TypeCanAttackAir(attacker) || TypeCanAttackGround(attacker);
}

bool UnitUtil::CanAttackAir(BWAPI::Unit attacker)
{
	return TypeCanAttackAir(attacker->getType());
}

bool UnitUtil::TypeCanAttackAir(BWAPI::UnitType attacker)
{
	return attacker.airWeapon() != BWAPI::WeaponTypes::None ||
		attacker == BWAPI::UnitTypes::Terran_Bunker ||
		attacker == BWAPI::UnitTypes::Protoss_Carrier;
}

bool UnitUtil::CanAttackGround(BWAPI::Unit attacker)
{
	return TypeCanAttackGround(attacker->getType());
}

bool UnitUtil::TypeCanAttackGround(BWAPI::UnitType attacker)
{
	return attacker.groundWeapon() != BWAPI::WeaponTypes::None ||
		attacker == BWAPI::UnitTypes::Terran_Bunker || 
		attacker == BWAPI::UnitTypes::Protoss_Carrier ||
		attacker == BWAPI::UnitTypes::Protoss_Reaver;
}

double UnitUtil::CalculateLTD(BWAPI::Unit attacker, BWAPI::Unit target)
{
    BWAPI::WeaponType weapon = GetWeapon(attacker, target);

	if (weapon == BWAPI::WeaponTypes::None || weapon.damageCooldown() <= 0)
    {
        return 0;
    }

	return double(weapon.damageAmount()) / weapon.damageCooldown();
}

// Is the given unit in danger of enemy weapons fire, with the given safety margin (in pixels)?
// If so, return the nearest enemy that puts the unit in danger.
// NOTE The check is quick and dirty, not exact. It will make mistakes.
BWAPI::Unit UnitUtil::inWeaponsDanger(BWAPI::Unit unit, int margin)
{
	BWAPI::Unit enemy;
	if (unit->isFlying())
	{
		// Flying unit.
		enemy = BWAPI::Broodwar->getClosestUnit(
			unit->getPosition(),
			BWAPI::Filter::GetPlayer == BWAPI::Broodwar->enemy() &&
			(BWAPI::Filter::AirWeapon != BWAPI::WeaponTypes::None || BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Bunker || BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Carrier) &&
			BWAPI::Filter::IsCompleted,
			margin + 7 * 32
		);
	}
	else
	{
		// Ground unit.
		enemy = BWAPI::Broodwar->getClosestUnit(
			unit->getPosition(),
			BWAPI::Filter::GetPlayer == BWAPI::Broodwar->enemy() &&
			!BWAPI::Filter::IsWorker &&
			(BWAPI::Filter::GroundWeapon != BWAPI::WeaponTypes::None || BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Bunker || BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Reaver) &&
			BWAPI::Filter::IsCompleted,
			margin + (InformationManager::Instance().enemyHasSiegeMode() ? 12 * 32 : 8 * 32)
		);
	}

	if (enemy && unit->getDistance(enemy) < margin + UnitUtil::GetAttackRangeAssumingUpgrades(enemy->getType(), unit->getType()))
	{
		return enemy;
	}
	return nullptr;
}

// Is the tile position in danger of enemy weapons fire, with the given safety margin (in pixels)?
// If so, return the the enemies that puts this tile in danger.
// NOTE The check is quick and dirty, not exact. It will make mistakes.
BWAPI::Unitset UnitUtil::inWeaponsDanger(BWAPI::TilePosition tile, int margin, bool flying)
{
	BWAPI::Unitset enemies;
	BWAPI::Unitset enemiesInRange;

	if (flying)
	{
		// Flying
		enemies = BWAPI::Broodwar->getUnitsInRadius(
			BWAPI::Position(tile),
			margin + 7 * 32,
			BWAPI::Filter::GetPlayer == BWAPI::Broodwar->enemy() &&
			(BWAPI::Filter::AirWeapon != BWAPI::WeaponTypes::None || BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Bunker || BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Carrier) &&
			BWAPI::Filter::IsCompleted
		);
	}
	else
	{
		// Ground
		enemies = BWAPI::Broodwar->getUnitsInRadius(
			BWAPI::Position(tile),
			margin + (InformationManager::Instance().enemyHasSiegeMode() ? 12 * 32 : 8 * 32),
			BWAPI::Filter::GetPlayer == BWAPI::Broodwar->enemy() &&
			!BWAPI::Filter::IsWorker &&
			(BWAPI::Filter::GroundWeapon != BWAPI::WeaponTypes::None || BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Bunker || BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Reaver) &&
			BWAPI::Filter::IsCompleted
		);
	}

	for (auto enemy : enemies)
	{
		int distance = (int)BWAPI::Position(tile).getApproxDistance(enemy->getPosition());
		int range = (int)GetTrueGroundRange(enemy->getType(), BWAPI::Broodwar->enemy());
		if (flying)
			range = (int)GetTrueAirRange(enemy->getType(), BWAPI::Broodwar->enemy());
		if (enemy && distance < margin + range)
		{
			enemiesInRange.insert(enemy);
		}
	}

	return enemies;
}

BWAPI::WeaponType UnitUtil::GetWeapon(BWAPI::Unit attacker, BWAPI::Unit target)
{
	return GetWeapon(attacker->getType(), target->getType());
}

// Handle carriers and reavers correctly in the case of floating buildings.
// We have to check unit->isFlying() because unitType->isFlyer() is not correct
// for a lifted terran building.
// There is no way to handle bunkers here. You have to know what's in it.
BWAPI::WeaponType UnitUtil::GetWeapon(BWAPI::UnitType attacker, BWAPI::Unit target)
{
	if (attacker == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return GetWeapon(BWAPI::UnitTypes::Protoss_Interceptor, target);
	}
	if (attacker == BWAPI::UnitTypes::Protoss_Reaver)
	{
		return GetWeapon(BWAPI::UnitTypes::Protoss_Scarab, target);
	}
	return target->isFlying() ? attacker.airWeapon() : attacker.groundWeapon();
}

// There is no way to handle bunkers here. You have to know what's in it.
BWAPI::WeaponType UnitUtil::GetWeapon(BWAPI::UnitType attacker, BWAPI::UnitType target)
{
	if (attacker == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return GetWeapon(BWAPI::UnitTypes::Protoss_Interceptor, target);
	}
	if (attacker == BWAPI::UnitTypes::Protoss_Reaver)
	{
		return GetWeapon(BWAPI::UnitTypes::Protoss_Scarab, target);
	}
	return target.isFlyer() ? attacker.airWeapon() : attacker.groundWeapon();
}

// Tries to take possible range upgrades into account, making pessimistic assumptions about the target.
// Returns 0 if the attacker does not have a way to attack the target.
// NOTE Does not check whether our reaver, carrier, or bunker has units inside that can attack.
int UnitUtil::GetAttackRange(BWAPI::Unit attacker, BWAPI::Unit target)
{
	// Reavers, carriers, and bunkers have "no weapon" but still have an attack range.
	if (attacker->getType() == BWAPI::UnitTypes::Protoss_Reaver && !target->isFlying())
	{
		return 8 * 32;
	}
	if (attacker->getType() == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return 8 * 32;
	}
	if (attacker->getType() == BWAPI::UnitTypes::Terran_Bunker)
	{
		if (attacker->getPlayer() == BWAPI::Broodwar->enemy() ||
			BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::U_238_Shells))
		{
			return 6 * 32;
		}
		return 5 * 32;
	}

	const BWAPI::WeaponType weapon = GetWeapon(attacker, target);
	if (weapon == BWAPI::WeaponTypes::None)
	{
		return 0;
	}

	int range = weapon.maxRange();

	// Count range upgrades,
	// for ourselves if we have researched it,
	// for the enemy always (by pessimistic assumption).
	if (attacker->getType() == BWAPI::UnitTypes::Protoss_Dragoon)
	{
		if (attacker->getPlayer() == BWAPI::Broodwar->enemy() ||
			BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Singularity_Charge))
		{
			range = 6 * 32;
		}
	}
	else if (attacker->getType() == BWAPI::UnitTypes::Terran_Marine)
	{
		if (attacker->getPlayer() == BWAPI::Broodwar->enemy() ||
			BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::U_238_Shells))
		{
			range = 5 * 32;
		}
	}
	else if (attacker->getType() == BWAPI::UnitTypes::Terran_Goliath && target->isFlying())
	{
		if (attacker->getPlayer() == BWAPI::Broodwar->enemy() ||
			BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Charon_Boosters))
		{
			range = 8 * 32;
		}
	}
	else if (attacker->getType() == BWAPI::UnitTypes::Zerg_Hydralisk)
	{
		if (attacker->getPlayer() == BWAPI::Broodwar->enemy() ||
			BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Grooved_Spines))
		{
			range = 5 * 32;
		}
	}

	return range;
}

// Range is zero if the attacker cannot attack the target at all.
int UnitUtil::GetAttackRangeAssumingUpgrades(BWAPI::UnitType attacker, BWAPI::UnitType target)
{
	// Reavers, carriers, and bunkers have "no weapon" but still have an attack range.
	if (attacker == BWAPI::UnitTypes::Protoss_Reaver && !target.isFlyer())
	{
		return 8 * 32;
	}
	if (attacker == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return 8 * 32;
	}
	if (attacker == BWAPI::UnitTypes::Terran_Bunker)
	{
		return 6 * 32;
	}

	BWAPI::WeaponType weapon = GetWeapon(attacker, target);
	if (weapon == BWAPI::WeaponTypes::None)
	{
		return 0;
	}

	int range = weapon.maxRange();

	// Assume that any upgrades are researched.
	if (attacker == BWAPI::UnitTypes::Protoss_Dragoon)
	{
		range = 6 * 32;
	}
	else if (attacker == BWAPI::UnitTypes::Terran_Marine)
	{
		range = 5 * 32;
	}
	else if (attacker == BWAPI::UnitTypes::Terran_Goliath && target.isFlyer())
	{
		range = 8 * 32;
	}
	else if (attacker == BWAPI::UnitTypes::Zerg_Hydralisk)
	{
		range = 5 * 32;
	}

	return range;
}

// Returns a unit type's ground range taking upgrades into account
double UnitUtil::GetTrueGroundRange(BWAPI::UnitType type, BWAPI::Player player)
{
	// Range upgrade check for Dragoons ground attack
	if (type == BWAPI::UnitTypes::Protoss_Dragoon && player->getUpgradeLevel(BWAPI::UpgradeTypes::Singularity_Charge))
	{
		return 192.0;
	}

	// Range upgrade check for Marines and Hydralisks ground attack
	else if ((type == BWAPI::UnitTypes::Terran_Marine && player->getUpgradeLevel(BWAPI::UpgradeTypes::U_238_Shells)) ||
		(type == BWAPI::UnitTypes::Zerg_Hydralisk && player->getUpgradeLevel(BWAPI::UpgradeTypes::Grooved_Spines)))
	{
		return 160.0;
	}

	// Range upgrade check for Bunkers (Marines) ground attack
	else if (type == BWAPI::UnitTypes::Terran_Bunker)
	{
		if (player->getUpgradeLevel(BWAPI::UpgradeTypes::U_238_Shells))
		{
			return 192.0;
		}
		else
		{
			return 160.0;
		}
	}

	// Range assumption for High Templars
	else if (type == BWAPI::UnitTypes::Protoss_High_Templar)
	{
		return 288.0;
	}

	// Range assumption for Reavers
	else if (type == BWAPI::UnitTypes::Protoss_Reaver)
	{
		return 256.0;
	}

	// Range assumption for Carriers
	else if (type == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return 256.0;
	}

	// Else return the Units base range for ground weapons
	return double(type.groundWeapon().maxRange());
}

// Returns a unit type's air range taking upgrades into account
double UnitUtil::GetTrueAirRange(BWAPI::UnitType type, BWAPI::Player player)
{
	// Range upgrade check for Dragoons air attack
	if (type == BWAPI::UnitTypes::Protoss_Dragoon && player->getUpgradeLevel(BWAPI::UpgradeTypes::Singularity_Charge))
	{
		return 192.0;
	}

	// Range upgrade check for Marines and Hydralisks air attack
	else if ((type == BWAPI::UnitTypes::Terran_Marine && player->getUpgradeLevel(BWAPI::UpgradeTypes::U_238_Shells)) ||
		(type == BWAPI::UnitTypes::Zerg_Hydralisk && player->getUpgradeLevel(BWAPI::UpgradeTypes::Grooved_Spines)))
	{
		return 160.0;
	}

	// Range upgrade check for Goliaths air attack
	else if (type == BWAPI::UnitTypes::Terran_Goliath && player->getUpgradeLevel(BWAPI::UpgradeTypes::Charon_Boosters))
	{
		return 256.0;
	}

	// Range upgrade check for Bunkers (Marines) air attack
	else if (type == BWAPI::UnitTypes::Terran_Bunker)
	{
		if (player->getUpgradeLevel(BWAPI::UpgradeTypes::U_238_Shells))
		{
			return 192.0;
		}
		else
		{
			return 160.0;
		}
	}

	// Range assumption for High Templars
	else if (type == BWAPI::UnitTypes::Protoss_High_Templar)
	{
		return 288.0;
	}

	// Range assumption for Carriers
	else if (type == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return 256.0;
	}

	// Else return the Units base range for air weapons
	return double(type.airWeapon().maxRange());
}

double UnitUtil::GetTrueGroundDamage(BWAPI::UnitType type, BWAPI::Player player)
{
	// Damage upgrade check for Reavers and correction of initial damage
	if (type == BWAPI::UnitTypes::Protoss_Reaver)
	{
		if (player->getUpgradeLevel(BWAPI::UpgradeTypes::Scarab_Damage))
		{
			return 125.00;
		}
		else
		{
			return 100.00;
		}
	}

	// Damage assumption for Carriers ground attack
	if (type == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return 100.00;
	}

	// Damage assumption for Bunkers ground attack
	else if (type == BWAPI::UnitTypes::Terran_Bunker)
	{
		return 25.0;
	}

	// Damage correction for Zealots and Firebats which attack twice for 8 damage
	else if (type == BWAPI::UnitTypes::Terran_Firebat ||
		type == BWAPI::UnitTypes::Protoss_Zealot)
	{
		return 16.0;
	}

	// Else return the Units base ground weapon damage
	return type.groundWeapon().damageAmount();
}

double UnitUtil::GetTrueAirDamage(BWAPI::UnitType type, BWAPI::Player player)
{
	// Damage assumption for Carriers air attack
	if (type == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return 100.00;
	}

	// Damage assumption for Bunkers air attack
	if (type == BWAPI::UnitTypes::Terran_Bunker)
	{
		return 25.0;
	}

	// Else return the Units base air weapon damage
	return type.airWeapon().damageAmount();
}

// Returns the units speed taking upgrades into account
double UnitUtil::GetTrueSpeed(BWAPI::UnitType type, BWAPI::Player player)
{
	double speed = type.topSpeed();

	if ((type == BWAPI::UnitTypes::Zerg_Zergling && player->getUpgradeLevel(BWAPI::UpgradeTypes::Metabolic_Boost)) ||
		(type == BWAPI::UnitTypes::Zerg_Hydralisk && player->getUpgradeLevel(BWAPI::UpgradeTypes::Muscular_Augments)) ||
		(type == BWAPI::UnitTypes::Zerg_Ultralisk && player->getUpgradeLevel(BWAPI::UpgradeTypes::Anabolic_Synthesis)) ||
		(type == BWAPI::UnitTypes::Protoss_Shuttle && player->getUpgradeLevel(BWAPI::UpgradeTypes::Gravitic_Drive)) ||
		(type == BWAPI::UnitTypes::Protoss_Observer && player->getUpgradeLevel(BWAPI::UpgradeTypes::Gravitic_Boosters)) ||
		(type == BWAPI::UnitTypes::Protoss_Zealot && player->getUpgradeLevel(BWAPI::UpgradeTypes::Leg_Enhancements)) ||
		(type == BWAPI::UnitTypes::Terran_Vulture && player->getUpgradeLevel(BWAPI::UpgradeTypes::Ion_Thrusters)))
	{
		speed = speed * 1.5;
	}
	else if (type == BWAPI::UnitTypes::Zerg_Overlord && player->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace))
	{
		speed = speed * 4.01;
	}
	else if (type == BWAPI::UnitTypes::Protoss_Scout && player->getUpgradeLevel(BWAPI::UpgradeTypes::Muscular_Augments))
	{
		speed = speed * 1.33;
	}
	return speed;
}

// All our units, whether completed or not.
size_t UnitUtil::GetAllUnitCount(BWAPI::UnitType type)
{
    size_t count = 0;
    for (const auto & unit : BWAPI::Broodwar->self()->getUnits())
    {
        // trivial case: unit which exists matches the type
        if (unit->getType() == type)
        {
            count++;
        }

        // case where a zerg egg contains the unit type
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg && unit->getBuildType() == type)
        {
            count += type.isTwoUnitsInOneEgg() ? 2 : 1;
        }

		// case where a zerg cocoon contains the unit type
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Cocoon && unit->getBuildType() == type)
		{
			count++;
		}

		// case where a lurker egg contains the unit type
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Lurker_Egg && unit->getBuildType() == type)
		{
			count++;
		}

        // case where a building has started constructing a unit but it doesn't yet have a unit associated with it
        if (unit->getRemainingTrainTime() > 0)
        {
            BWAPI::UnitType trainType = unit->getLastCommand().getUnitType();

            if (trainType == type && unit->getRemainingTrainTime() == trainType.buildTime())
            {
                count++;
            }
        }
    }

    return count;
}

// Only our completed units.
size_t UnitUtil::GetCompletedUnitCount(BWAPI::UnitType type)
{
	size_t count = 0;
	for (const auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		if (unit->getType() == type && unit->isCompleted())
		{
			count++;
		}
	}

	return count;
}

BWAPI::Unit UnitUtil::GetClosestUnitTypeToTarget(BWAPI::UnitType type, BWAPI::Position target)
{
	BWAPI::Unit closestUnit = nullptr;
	int closestDist = 100000;

	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		if (unit->getType() == type)
		{
			int dist = unit->getDistance(target);
			if (!closestUnit || dist < closestDist)
			{
				closestUnit = unit;
				closestDist = dist;
			}
		}
	}

	return closestUnit;
}

BWAPI::Unit UnitUtil::GetClosestUnitType(BWAPI::Player player, BWAPI::UnitType unittype, BWAPI::Position position)
{
	BWAPI::Unit closestUnit = nullptr;
	int closestDist = 100000;

	for (auto & unit : player->getUnits())
	{
		if (unit->getType() == unittype)
		{
			int dist = unit->getDistance(position);
			if (!closestUnit || dist < closestDist)
			{
				closestUnit = unit;
				closestDist = dist;
			}
		}
	}

	return closestUnit;
}

//indicates whether target is a threat to unit
//Dangerous units includes units that may be a threat to allies, as well
//Note, this function does not include carriers
bool UnitUtil::IsThreat(BWAPI::Unit unit, BWAPI::Unit target, bool includeDangerous) {
	if (!unit || !target || !unit->exists() || !target->exists()) {
		UAB_ASSERT(false, "Invalid arg to UnitUtil::isThreat");
		return false;
	}
	if (target->getPlayer() == unit->getPlayer()) {
		UAB_ASSERT(false, "Target in UnitUtil::isThreat has same owner as unit!");
		return false;
	}

	BWAPI::UnitType type = target->getType();

	if (type.isWorker()) {
		if (target->isAttacking() && !unit->isFlying()) return true;
		return false;
	}

	if (type == BWAPI::UnitTypes::Terran_Bunker ||
		(!unit->isFlying() && type == BWAPI::UnitTypes::Protoss_Reaver) ||
		type == BWAPI::UnitTypes::Protoss_Carrier) {
		return true;
	}

	if (includeDangerous) { //consider spellcasters and dangerous units even if they can't hit us
		//additional high threat targets
		if (type == BWAPI::UnitTypes::Protoss_Observer ||
			type == BWAPI::UnitTypes::Protoss_Shuttle ||
			type == BWAPI::UnitTypes::Protoss_High_Templar ||
			type == BWAPI::UnitTypes::Zerg_Defiler ||
			type == BWAPI::UnitTypes::Protoss_Reaver ||
			(target->getEnergy() >= 70 && //harder-to-kill spellcasters, but only with energy
			(type == BWAPI::UnitTypes::Protoss_Dark_Archon ||
			type == BWAPI::UnitTypes::Zerg_Queen ||
			type == BWAPI::UnitTypes::Terran_Science_Vessel)) ||
			//type == BWAPI::UnitTypes::Zerg_Zergling || //mutalisks should see these as threats
			type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
			type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
			type == BWAPI::UnitTypes::Zerg_Cocoon) {
			return true;
		}

		if ((unit->isCloaked() || unit->isBurrowed() || unit->getType() == BWAPI::UnitTypes::Zerg_Lurker) && type.isDetector()) return true;

		if (target->isAttacking()) return true; //anything hitting allies is a threat to us - missile turrets, corsairs
	}

	return unit->getType().isFlyer() ? type.airWeapon() != BWAPI::WeaponTypes::None
		: type.groundWeapon() != BWAPI::WeaponTypes::None;
}

bool UnitUtil::CanAttackInSwarm(BWAPI::Unit unit) {
	auto type = unit->getType();
	if (type.isWorker()) return false; //not the best unit to be attacking with
	if (type == BWAPI::UnitTypes::Zerg_Lurker ||
		type == BWAPI::UnitTypes::Protoss_Archon ||
		type == BWAPI::UnitTypes::Protoss_Reaver) return true;

	auto weapon = type.groundWeapon();
	if (weapon == BWAPI::WeaponTypes::None) return false;
	if (weapon.maxRange() > 32) return false; //most melee unit ranges

	//disqualifies spells
	if (weapon.damageType() != BWAPI::DamageTypes::Concussive &&
		weapon.damageType() != BWAPI::DamageTypes::Explosive &&
		weapon.damageType() != BWAPI::DamageTypes::Normal) return false;

	return true;
}

double UnitUtil::DamageModifier(BWAPI::UnitType attacker, BWAPI::UnitType target)
{
	BWAPI::WeaponType weapon = GetWeapon(attacker, target);

	if (weapon == BWAPI::WeaponTypes::None || weapon.damageCooldown() <= 0)
		return 1;

	BWAPI::DamageType damagetype = weapon.damageType();
	double damage = (double)weapon.damageAmount();
	double damageModifier = 1;

	// Concussive damage
	if (damagetype == BWAPI::DamageTypes::Concussive) {
		if (target.size() == BWAPI::UnitSizeTypes::Large)
			damageModifier = 1 / 4;
		else if (target.size() == BWAPI::UnitSizeTypes::Medium)
			damageModifier = 1 / 2;
	}

	// Explosive damage
	else if (damagetype == BWAPI::DamageTypes::Explosive) {
		if (target.size() == BWAPI::UnitSizeTypes::Small)
			damageModifier = 1 / 2;
		else if (target.size() == BWAPI::UnitSizeTypes::Medium)
			damageModifier = 3 / 4;
	}

	// Bounce splash
	if (attacker == BWAPI::UnitTypes::Zerg_Mutalisk)
		damageModifier = damageModifier * (1 + (1 / 3) + (1 / 9));

	// Radial splash
	else if (attacker == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
		damageModifier = damageModifier * (1 + (1 / 2) + (1 / 4));

	// Line splash
	else if (attacker == BWAPI::UnitTypes::Zerg_Lurker || attacker == BWAPI::UnitTypes::Terran_Firebat)
		damageModifier = damageModifier * GetAttackRangeAssumingUpgrades(attacker, target);

	return damageModifier;
}

double UnitUtil::CalculateDPS(BWAPI::UnitType attacker, BWAPI::UnitType target)
{
	BWAPI::WeaponType weapon = GetWeapon(attacker, target);

	if (weapon == BWAPI::WeaponTypes::None || weapon.damageCooldown() <= 0)
	{
		return 0;
	}

	//double speedModifier = attacker.topSpeed() / target.topSpeed();
	double rangeModifier = GetAttackRangeAssumingUpgrades(attacker, target) / GetAttackRangeAssumingUpgrades(target, attacker);
	double damageModifier = DamageModifier(attacker, target);
	double damage = (double)weapon.damageAmount();
	double cooldown = (double)weapon.damageCooldown();

	// score is damage per frame
	return damage * damageModifier * rangeModifier / cooldown;
}

double UnitUtil::GroundDPF(BWAPI::Player player, BWAPI::UnitType type)
{
	BWAPI::WeaponType weapon = GetGroundWeapon(type);
	const int cooldown = player->weaponDamageCooldown(type);

	if (weapon == BWAPI::WeaponTypes::None || cooldown <= 0)
	{
		return 0.0;
	}

	return double(player->damage(weapon)) / cooldown;
}

double UnitUtil::AirDPF(BWAPI::Player player, BWAPI::UnitType type)
{
	BWAPI::WeaponType weapon = GetAirWeapon(type);
	const int cooldown = player->weaponDamageCooldown(type);

	if (weapon == BWAPI::WeaponTypes::None || cooldown <= 0)
	{
		return 0.0;
	}

	return double(player->damage(weapon)) / cooldown;
}

BWAPI::WeaponType UnitUtil::GetGroundWeapon(BWAPI::Unit attacker)
{
	return GetGroundWeapon(attacker->getType());
}

BWAPI::WeaponType UnitUtil::GetGroundWeapon(BWAPI::UnitType attacker)
{
	// We pretend that a bunker has marines in it. It's only a guess.
	if (attacker == BWAPI::UnitTypes::Terran_Bunker)
	{
		return (BWAPI::UnitTypes::Terran_Marine).groundWeapon();
	}
	if (attacker == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return (BWAPI::UnitTypes::Protoss_Interceptor).groundWeapon();
	}
	if (attacker == BWAPI::UnitTypes::Protoss_Reaver)
	{
		return (BWAPI::UnitTypes::Protoss_Scarab).groundWeapon();
	}

	return attacker.groundWeapon();
}

BWAPI::WeaponType UnitUtil::GetAirWeapon(BWAPI::Unit attacker)
{
	return GetAirWeapon(attacker->getType());
}

BWAPI::WeaponType UnitUtil::GetAirWeapon(BWAPI::UnitType attacker)
{
	if (attacker == BWAPI::UnitTypes::Terran_Bunker)
	{
		return (BWAPI::UnitTypes::Terran_Marine).airWeapon();
	}
	if (attacker == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return (BWAPI::UnitTypes::Protoss_Interceptor).airWeapon();
	}

	return attacker.airWeapon();
}

// Can the enemy detect a unit at this position?
// This is nearly accurate for known enemy detectors, possibly off at the edges.
// It doesn't try to check whether the enemy can see the unit, which is independent of detection.
// E.g., a lurker on a cliff might be detected but safe because it is unseen.
bool UnitUtil::EnemyDetectorInRange(BWAPI::Position pos)
{
	return nullptr != BWAPI::Broodwar->getClosestUnit(
		pos,
		BWAPI::Filter::IsDetector && BWAPI::Filter::IsEnemy && BWAPI::Filter::IsFlyer && !BWAPI::Filter::IsBlind,
		11 * 32
	);
}

bool UnitUtil::EnemyDetectorInRange(BWAPI::Unit unit)
{
	return EnemyDetectorInRange(unit->getPosition());
}
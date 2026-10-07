#pragma once

#include <Common.h>
#include <BWAPI.h>

namespace UAlbertaBot
{
struct UnitInfo;

namespace UnitUtil
{      
	bool IsMorphedBuildingType(BWAPI::UnitType unitType);
	bool IsMorphedUnitType(BWAPI::UnitType unitType);
	bool BuildingIsMorphedFrom(BWAPI::UnitType t2, BWAPI::UnitType t1);
	bool IsCompletedResourceDepot(BWAPI::Unit unit);
	
	bool NeedsPylonPower(BWAPI::UnitType type);
	bool IsStaticDefense(BWAPI::UnitType type);
	bool IsImmobileDefense(BWAPI::UnitType type);
	bool IsComingStaticDefense(BWAPI::UnitType type);

	bool IsCombatSimUnit(const UnitInfo & ui);
	bool IsCombatSimUnit(BWAPI::Unit unit);
	bool IsCombatSimUnit(BWAPI::UnitType type);
	bool IsCombatUnit(BWAPI::Unit unit);
	bool IsCombatUnit(BWAPI::UnitType type);
    bool IsValidUnit(BWAPI::Unit unit);
	bool IsRangedUnit(BWAPI::UnitType type);
    
	bool CanAttackAir(BWAPI::Unit unit);
	bool TypeCanAttackAir(BWAPI::UnitType attacker);
    bool CanAttackGround(BWAPI::Unit unit);
	bool TypeCanAttackGround(BWAPI::UnitType attacker);
    bool IsGroundTarget(BWAPI::Unit unit);
    bool IsAirTarget(BWAPI::Unit unit);
    bool CanAttack(BWAPI::Unit attacker, BWAPI::Unit target);
    bool CanAttack(BWAPI::UnitType attacker, BWAPI::UnitType target);
	bool TypeCanAttack(BWAPI::UnitType attacker);
    double CalculateLTD(BWAPI::Unit attacker, BWAPI::Unit target);

	BWAPI::Unit inWeaponsDanger(BWAPI::Unit unit, int margin);
	BWAPI::Unitset inWeaponsDanger(BWAPI::TilePosition tile, int margin, bool flying);

    int GetAttackRange(BWAPI::Unit attacker, BWAPI::Unit target);
	int GetAttackRangeAssumingUpgrades(BWAPI::UnitType attacker, BWAPI::UnitType target);

	double GetTrueGroundRange(BWAPI::UnitType type, BWAPI::Player player);
	double GetTrueAirRange(BWAPI::UnitType type, BWAPI::Player player);
	double GetTrueGroundDamage(BWAPI::UnitType type, BWAPI::Player player);
	double GetTrueAirDamage(BWAPI::UnitType type, BWAPI::Player player);
	double GetTrueSpeed(BWAPI::UnitType type, BWAPI::Player player);

	int GetTransportSize(BWAPI::UnitType type);

    size_t GetAllUnitCount(BWAPI::UnitType type);
	size_t GetCompletedUnitCount(BWAPI::UnitType type);
	
    BWAPI::Unit GetClosestUnitTypeToTarget(BWAPI::UnitType type, BWAPI::Position target);
	BWAPI::Unit GetClosestUnitType(BWAPI::Player player, BWAPI::UnitType type, BWAPI::Position position);

	BWAPI::WeaponType GetGroundWeapon(BWAPI::Unit attacker);
	BWAPI::WeaponType GetGroundWeapon(BWAPI::UnitType attacker);
	BWAPI::WeaponType GetAirWeapon(BWAPI::Unit attacker);
	BWAPI::WeaponType GetAirWeapon(BWAPI::UnitType attacker);
	BWAPI::WeaponType GetWeapon(BWAPI::Unit attacker, BWAPI::Unit target);
	BWAPI::WeaponType GetWeapon(BWAPI::UnitType attacker, BWAPI::Unit target);
    BWAPI::WeaponType GetWeapon(BWAPI::UnitType attacker, BWAPI::UnitType target);

	double CalculateDPS(BWAPI::UnitType attacker, BWAPI::UnitType target);
	double DamageModifier(BWAPI::UnitType attacker, BWAPI::UnitType target);
	double GroundDPF(BWAPI::Player player, BWAPI::UnitType type);
	double AirDPF(BWAPI::Player player, BWAPI::UnitType type);

	bool EnemyDetectorInRange(BWAPI::Position pos);
	bool EnemyDetectorInRange(BWAPI::Unit unit);

	//From Arrak
	bool IsThreat(BWAPI::Unit unit, BWAPI::Unit target, bool includeDangerous = true);
	bool CanAttackInSwarm(BWAPI::Unit unit);
    double GetDistanceBetweenTwoRectangles(Rect & rect1, Rect & rect2);
    Rect GetRect(BWAPI::Unit unit);
};
}
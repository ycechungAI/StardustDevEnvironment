#include "FAP.h"
#include "BWAPI.h"

UAlbertaBot::FastAPproximation fap;

namespace UAlbertaBot {

	FastAPproximation::FastAPproximation() {}

	void FastAPproximation::addUnitPlayer1(FAPUnit fu) {
		player1.push_back(fu);
	}

	void FastAPproximation::addIfCombatUnitPlayer1(FAPUnit fu) {
		if (fu.unitType == BWAPI::UnitTypes::Protoss_Interceptor)
			return;
		if (fu.groundDamage || fu.airDamage || fu.unitType == BWAPI::UnitTypes::Terran_Medic)
			addUnitPlayer1(fu);
	}

	void FastAPproximation::addUnitPlayer2(FAPUnit fu) {
		player2.push_back(fu);
	}

	void FastAPproximation::addIfCombatUnitPlayer2(FAPUnit fu) {
		if (fu.unitType == BWAPI::UnitTypes::Protoss_Interceptor)
			return;
		if (fu.groundDamage || fu.airDamage || fu.unitType == BWAPI::UnitTypes::Terran_Medic)
			addUnitPlayer2(fu);
	}

	void FastAPproximation::simulate(int nFrames) {
		while (nFrames--) {
			if (player1.empty() || player2.empty())
				break;

			didSomething = false;

			isimulate();

			if (!didSomething)
				break;
		}
	}

	const auto score = [](const FastAPproximation::FAPUnit &fu) {
		if (fu.health && fu.maxHealth)
			//return ((fu.score * fu.health) / (fu.maxHealth * 2)) +
			return ((fu.score * (fu.health * 3 + fu.shields) + fu.score) / (fu.maxHealth * 3 + fu.maxShields)) +
			(fu.unitType == BWAPI::UnitTypes::Terran_Bunker) * BWAPI::UnitTypes::Terran_Marine.destroyScore() * 4;
		return 0;
	};

	std::pair <int, int> FastAPproximation::playerScores() const {
		std::pair <int, int> res;

		for (const auto &u : player1)
			res.first += score(u);

		for (const auto &u : player2)
			res.second += score(u);

		return res;
	}

	std::pair <int, int> FastAPproximation::playerScoresUnits() const {
		std::pair <int, int> res;

		for (const auto &u : player1)
			if (!u.unitType.isBuilding())
				res.first += score(u);

		for (const auto &u : player2)
			if (!u.unitType.isBuilding())
				res.second += score(u);

		return res;
	}

	std::pair <int, int> FastAPproximation::playerScoresBuildings() const {
		std::pair <int, int> res;

		for (const auto &u : player1)
			if (u.unitType.isBuilding())
				res.first += score(u);

		for (const auto &u : player2)
			if (u.unitType.isBuilding())
				res.second += score(u);

		return res;
	}

	std::pair<std::vector<FastAPproximation::FAPUnit>*, std::vector<FastAPproximation::FAPUnit>*> FastAPproximation::getState() {
		return{ &player1, &player2 };
	}

	void FastAPproximation::clearState() {
		player1.clear(), player2.clear();
	}

	void FastAPproximation::dealDamage(const FastAPproximation::FAPUnit &fu, int damage, BWAPI::DamageType damageType, const FastAPproximation::FAPUnit &attacker) const {
		damage <<= 8;

		if (!fu.flying && !attacker.flying &&
			attacker.groundMaxRange > 32 &&
			fu.elevation != -1 && attacker.elevation != -1 && fu.elevation > attacker.elevation)
		{
			damage >>= 1;
		}

		int remainingShields = fu.shields - damage + (fu.shieldArmor << 8);
		if (remainingShields > 0) {
			fu.shields = remainingShields;
			return;
		}
		else if (fu.shields) {
			damage -= fu.shields + (fu.shieldArmor << 8);
			fu.shields = 0;
		}

		if (!damage)
			return;

		damage -= fu.armor << 8;

		if (damageType == BWAPI::DamageTypes::Concussive) {
			if (fu.unitSize == BWAPI::UnitSizeTypes::Large)
				damage = damage / 4;
			else if (fu.unitSize == BWAPI::UnitSizeTypes::Medium)
				damage = damage / 2;
		}
		else if (damageType == BWAPI::DamageTypes::Explosive) {
			if (fu.unitSize == BWAPI::UnitSizeTypes::Small)
				damage = damage / 2;
			else if (fu.unitSize == BWAPI::UnitSizeTypes::Medium)
				damage = (damage * 3) / 4;
		}

		fu.health -= std::max(128, damage);
	}

	int FastAPproximation::distButNotReally(const FastAPproximation::FAPUnit &u1, const FastAPproximation::FAPUnit &u2) const {
		return (u1.x - u2.x) * (u1.x - u2.x) + (u1.y - u2.y) * (u1.y - u2.y);
	}

	bool FastAPproximation::isSuicideUnit(BWAPI::UnitType ut) {
		return (ut == BWAPI::UnitTypes::Zerg_Scourge || 
				ut == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine || 
				ut == BWAPI::UnitTypes::Zerg_Infested_Terran || 
				ut == BWAPI::UnitTypes::Protoss_Scarab);
	}

	void FastAPproximation::unitsim(const FastAPproximation::FAPUnit &fu, std::vector <FastAPproximation::FAPUnit> &enemyUnits) {
		if (fu.attackCooldownRemaining) {
			didSomething = true;
			return;
		}

		if (!(fu.groundDamage || fu.airDamage)) {
			return;
		}

		auto closestEnemy = enemyUnits.end();
		int closestDist;

		for (auto enemyIt = enemyUnits.begin(); enemyIt != enemyUnits.end(); ++enemyIt) {
			if (enemyIt->flying) {
				if (fu.airDamage) {
					int d = distButNotReally(fu, *enemyIt);
					if ((closestEnemy == enemyUnits.end() || d < closestDist) && d >= fu.airMinRange) {
						closestDist = d;
						closestEnemy = enemyIt;
					}
				}
			}
			else {
				if (fu.groundDamage) {
					int d = distButNotReally(fu, *enemyIt);
					if ((closestEnemy == enemyUnits.end() || d < closestDist) && d >= fu.groundMinRange) {
						closestDist = d;
						closestEnemy = enemyIt;
					}
				}
			}
		}

		if (closestEnemy != enemyUnits.end() && sqrt(closestDist) <= fu.speed && !(fu.x == closestEnemy->x && fu.y == closestEnemy->y)) {
			fu.x = closestEnemy->x;
			fu.y = closestEnemy->y;
			closestDist = 0;

			didSomething = true;
		}

		if (closestEnemy != enemyUnits.end() && closestDist <= (closestEnemy->flying ? fu.airMaxRange : fu.groundMaxRange)) {
			if (closestEnemy->flying) {
				dealDamage(*closestEnemy, fu.airDamage, fu.airDamageType, fu);
				fu.attackCooldownRemaining = fu.airCooldown;
			}
			else {
				dealDamage(*closestEnemy, fu.groundDamage, fu.groundDamageType, fu);
				fu.attackCooldownRemaining = fu.groundCooldown;
			}

			if (closestEnemy->health < 1) {
				auto temp = *closestEnemy;
				*closestEnemy = enemyUnits.back();
				enemyUnits.pop_back();
				unitDeath(temp, enemyUnits);
			}

			didSomething = true;
			return;
		}
		else if (closestEnemy != enemyUnits.end() && sqrt(closestDist) > fu.speed && fu.speed >= 1.0f) {
			int dx = closestEnemy->x - fu.x;
			int dy = closestEnemy->y - fu.y;

			fu.x += (int)(dx*(fu.speed / sqrt(dx*dx + dy*dy)));
			fu.y += (int)(dy*(fu.speed / sqrt(dx*dx + dy*dy)));

			didSomething = true;
			return;
		}
	}

	void FastAPproximation::medicsim(const FAPUnit & fu, std::vector<FAPUnit> &friendlyUnits) {
		auto closestHealable = friendlyUnits.end();
		int closestDist;

		for (auto it = friendlyUnits.begin(); it != friendlyUnits.end(); ++it) {
			if (it->isOrganic && it->health < it->maxHealth && !it->didHealThisFrame) {
				int d = distButNotReally(fu, *it);
				if (closestHealable == friendlyUnits.end() || d < closestDist) {
					closestHealable = it;
					closestDist = d;
				}
			}
		}

		if (closestHealable != friendlyUnits.end()) {
			fu.x = closestHealable->x;
			fu.y = closestHealable->y;

			closestHealable->health += 150;

			if (closestHealable->health > closestHealable->maxHealth)
				closestHealable->health = closestHealable->maxHealth;

			closestHealable->didHealThisFrame = true;
		}
	}

	bool FastAPproximation::suicideSim(const FAPUnit & fu, std::vector<FAPUnit>& enemyUnits) {
		auto closestEnemy = enemyUnits.end();
		int closestDist;

		for (auto enemyIt = enemyUnits.begin(); enemyIt != enemyUnits.end(); ++enemyIt) {
			if (enemyIt->flying) {
				if (fu.airDamage) {
					int d = distButNotReally(fu, *enemyIt);
					if ((closestEnemy == enemyUnits.end() || d < closestDist) && d >= fu.airMinRange) {
						closestDist = d;
						closestEnemy = enemyIt;
					}
				}
			}
			else {
				if (fu.groundDamage) {
					int d = distButNotReally(fu, *enemyIt);
					if ((closestEnemy == enemyUnits.end() || d < closestDist) && d >= fu.groundMinRange) {
						closestDist = d;
						closestEnemy = enemyIt;
					}
				}
			}
		}

		if (closestEnemy != enemyUnits.end() && sqrt(closestDist) <= fu.speed) {
			if (closestEnemy->flying)
				dealDamage(*closestEnemy, fu.airDamage, fu.airDamageType, fu);
			else
				dealDamage(*closestEnemy, fu.groundDamage, fu.groundDamageType, fu);

			if (closestEnemy->health < 1) {
				auto temp = *closestEnemy;
				*closestEnemy = enemyUnits.back();
				enemyUnits.pop_back();
				unitDeath(temp, enemyUnits);
			}

			didSomething = true;
			return true;
		}
		else if (closestEnemy != enemyUnits.end() && sqrt(closestDist) > fu.speed) {
			int dx = closestEnemy->x - fu.x;
			int dy = closestEnemy->y - fu.y;

			fu.x += (int)(dx*(fu.speed / sqrt(dx*dx + dy*dy)));
			fu.y += (int)(dy*(fu.speed / sqrt(dx*dx + dy*dy)));

			didSomething = true;
		}

		return false;
	}

	void FastAPproximation::isimulate() {
		for (auto fu = player1.begin(); fu != player1.end();) {
			if (isSuicideUnit(fu->unitType)) {
				bool result = suicideSim(*fu, player2);
				if (result)
					fu = player1.erase(fu);
				else
					++fu;
			}
			else {
				if (fu->unitType == BWAPI::UnitTypes::Terran_Medic)
					medicsim(*fu, player1);
				else
					unitsim(*fu, player2);
				++fu;
			}
		}

		for (auto fu = player2.begin(); fu != player2.end();) {
			if (isSuicideUnit(fu->unitType)) {
				bool result = suicideSim(*fu, player1);
				if (result)
					fu = player2.erase(fu);
				else
					++fu;
			}
			else {
				if (fu->unitType == BWAPI::UnitTypes::Terran_Medic)
					medicsim(*fu, player2);
				else
					unitsim(*fu, player1);
				++fu;
			}
		}

		for (auto &fu : player1) {
			if (fu.attackCooldownRemaining)
				--fu.attackCooldownRemaining;
			if (fu.didHealThisFrame)
				fu.didHealThisFrame = false;

			if (fu.unitType.getRace() == BWAPI::Races::Zerg &&
				fu.unitType != BWAPI::UnitTypes::Zerg_Egg &&
				fu.unitType != BWAPI::UnitTypes::Zerg_Lurker_Egg &&
				fu.unitType != BWAPI::UnitTypes::Zerg_Larva) {
				if (fu.health < fu.maxHealth)
					fu.health += 4;
				if (fu.health > fu.maxHealth)
					fu.health = fu.maxHealth;
			}
			else if (fu.unitType.getRace() == BWAPI::Races::Protoss) {
				if (fu.shields < fu.maxShields)
					fu.shields += 7;
				if (fu.shields > fu.maxShields)
					fu.shields = fu.maxShields;
			}
		}

		for (auto &fu : player2) {
			if (fu.attackCooldownRemaining)
				--fu.attackCooldownRemaining;
			if (fu.didHealThisFrame)
				fu.didHealThisFrame = false;

			if (fu.unitType.getRace() == BWAPI::Races::Zerg &&
				fu.unitType != BWAPI::UnitTypes::Zerg_Egg &&
				fu.unitType != BWAPI::UnitTypes::Zerg_Lurker_Egg &&
				fu.unitType != BWAPI::UnitTypes::Zerg_Larva) {
				if (fu.health < fu.maxHealth)
					fu.health += 4;
				if (fu.health > fu.maxHealth)
					fu.health = fu.maxHealth;
			}
			else if (fu.unitType.getRace() == BWAPI::Races::Protoss) {
				if (fu.shields < fu.maxShields)
					fu.shields += 7;
				if (fu.shields > fu.maxShields)
					fu.shields = fu.maxShields;
			}
		}
	}

	void FastAPproximation::unitDeath(const FAPUnit &fu, std::vector<FAPUnit> &itsFriendlies) {
		if (fu.unitType == BWAPI::UnitTypes::Terran_Bunker) {
			convertToUnitType(fu, BWAPI::UnitTypes::Terran_Marine);

			for (unsigned i = 0; i < 4; ++i)
				itsFriendlies.push_back(fu);
		}
	}

	void FastAPproximation::convertToUnitType(const FAPUnit &fu, BWAPI::UnitType ut) {
		UAlbertaBot::UnitInfo ed;
		ed.lastPosition = { fu.x, fu.y };
		ed.player = fu.player;
		ed.type = ut;

		FAPUnit funew(ed);
		funew.attackCooldownRemaining = fu.attackCooldownRemaining;
		funew.elevation = fu.elevation;

		fu.operator=(funew);
	}

	FastAPproximation::FAPUnit::FAPUnit(BWAPI::Unit u) : FAPUnit(UAlbertaBot::UnitInfo(u)) {}

	FastAPproximation::FAPUnit::FAPUnit(UAlbertaBot::UnitInfo ed) :
		x(ed.lastPosition.x),
		y(ed.lastPosition.y),

		speed(ed.player->topSpeed(ed.type)),

		health(ed.lastHealth),
		maxHealth(ed.type.maxHitPoints()),

		shields(ed.lastShields),
		shieldArmor(ed.player->getUpgradeLevel(BWAPI::UpgradeTypes::Protoss_Plasma_Shields)),
		maxShields(ed.type.maxShields()),
		armor(ed.player->armor(ed.type)),
		flying(ed.type.isFlyer()),

		groundDamage(ed.player->damage(ed.type.groundWeapon())),
		groundCooldown(ed.type.groundWeapon().damageFactor() && ed.type.maxGroundHits() ? ed.player->weaponDamageCooldown(ed.type) / (ed.type.groundWeapon().damageFactor() * ed.type.maxGroundHits()) : 0),
		groundMaxRange(ed.player->weaponMaxRange(ed.type.groundWeapon())),
		groundMinRange(ed.type.groundWeapon().minRange()),
		groundDamageType(ed.type.groundWeapon().damageType()),

		airDamage(ed.player->damage(ed.type.airWeapon())),
		airCooldown(ed.type.airWeapon().damageFactor() && ed.type.maxAirHits() ? ed.type.airWeapon().damageCooldown() / (ed.type.airWeapon().damageFactor() * ed.type.maxAirHits()) : 0),
		airMaxRange(ed.player->weaponMaxRange(ed.type.airWeapon())),
		airMinRange(ed.type.airWeapon().minRange()),
		airDamageType(ed.type.airWeapon().damageType()),

		unitType(ed.type),
		isOrganic(ed.type.isOrganic()),
		score(ed.type.destroyScore()),
		player(ed.player) {

		static int nextId = 0;
		id = nextId++;

		if (ed.type == BWAPI::UnitTypes::Protoss_Carrier) {
			groundDamage = ed.player->damage(BWAPI::UnitTypes::Protoss_Interceptor.groundWeapon());

			if (ed.unit && ed.unit->isVisible()) {
				auto interceptorCount = ed.unit->getInterceptorCount();
				if (interceptorCount) {
					groundCooldown = (int)round(37.0f / interceptorCount);
				}
				else {
					groundDamage = 0;
					groundCooldown = 5;
				}
			}
			else {
				if (ed.player) {
					groundCooldown = (int)round(37.0f / (ed.player->getUpgradeLevel(BWAPI::UpgradeTypes::Carrier_Capacity) ? 8 : 4));
				}
				else {
					groundCooldown = (int)round(37.0f / 8);
				}
			}

			groundDamageType = BWAPI::UnitTypes::Protoss_Interceptor.groundWeapon().damageType();
			groundMaxRange = 32 * 8;

			airDamage = groundDamage;
			airDamageType = groundDamageType;
			airCooldown = groundCooldown;
			airMaxRange = groundMaxRange;
		}
		else if (ed.type == BWAPI::UnitTypes::Terran_Bunker) {
			groundDamage = ed.player->damage(BWAPI::WeaponTypes::Gauss_Rifle);
			groundCooldown = BWAPI::UnitTypes::Terran_Marine.groundWeapon().damageCooldown() / 4;
			groundMaxRange = ed.player->weaponMaxRange(BWAPI::UnitTypes::Terran_Marine.groundWeapon()) + 32;

			airDamage = groundDamage;
			airCooldown = groundCooldown;
			airMaxRange = groundMaxRange;
		}
		else if (ed.type == BWAPI::UnitTypes::Protoss_Reaver) {
			groundDamage = ed.player->damage(BWAPI::WeaponTypes::Scarab);
		}
		else if (ed.type == BWAPI::UnitTypes::Protoss_Archon)
		{
			// Very roughly estimate splash damage by having archons do 2x actual damage
			groundDamage *= 2;
		}
		
		// Destroy score is not a good value measurement for static ground defense, so set them manually
		if (ed.type == BWAPI::UnitTypes::Protoss_Photon_Cannon)
		{
			score = 750; // approximate at 1.5 dragoons
		}
		else if (ed.type == BWAPI::UnitTypes::Terran_Missile_Turret)
		{
			score = 1000; // approximate at 2 dragoons
		}
		else if (ed.type == BWAPI::UnitTypes::Zerg_Sunken_Colony)
		{
			score = 1000; // approximate at 2 dragoons
		}
		else if (ed.type == BWAPI::UnitTypes::Zerg_Spore_Colony)
		{
			score = 1000; // approximate at 2 dragoons
		}
		
		// Override score for units where the destroy score is inappropriate
		if (ed.type == BWAPI::UnitTypes::Zerg_Hydralisk)
			score = 150; // slightly less than a zealot
		else if (ed.type == BWAPI::UnitTypes::Terran_Vulture)
			score = 250; // slightly more than a zealot
		else if (ed.type == BWAPI::UnitTypes::Terran_Ghost)
			score = 150; // slightly less than a zealot
		else if (ed.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
			score = 900; // almost two dragoons
		else if (ed.type == BWAPI::UnitTypes::Zerg_Mutalisk)
			score = 400; // slightly less than a dragoon
		else if (ed.type == BWAPI::UnitTypes::Terran_Wraith)
			score = 500; // equivalent a dragoon
		else if (ed.type == BWAPI::UnitTypes::Protoss_Scout)
			score = 700; // a bit more than a dragoon
		else if (ed.type == BWAPI::UnitTypes::Protoss_Dark_Templar)
			score = 500; // equivalent to a dragoon
		else if (ed.type == BWAPI::UnitTypes::Zerg_Ultralisk)
			score = 1750; // 3-and-a-half goons
		else if (ed.type == BWAPI::UnitTypes::Protoss_Archon)
			score = 1750; // 3-and-a-half goons
		else if (ed.type == BWAPI::UnitTypes::Protoss_Arbiter)
			score = 1200; // don't really sim this correctly but 2050 is way too much

		// Stimmed units shoot faster, unless they are also ensnared.
		if (ed.unit && ed.unit->isStimmed() && !ed.unit->isEnsnared()) {
			groundCooldown /= 2;
			airCooldown /= 2;
		}

		// Ensnared units move and shoot more slowly.
		if (ed.unit && ed.unit->isEnsnared())
		{
			// Half speed movement.
			// NOTE The result is incorrect for stimmed units and units with a speed upgrade.
			//      But it's close enough for now.
			speed /= 2.0;

			// Cooldown increased by 25%, with exceptions.
			if (ed.type == BWAPI::UnitTypes::Zerg_Zergling && groundCooldown < 8)
			{
				// Zergling with the adrenal glands upgrade returns to its base cooldown of 8.
				groundCooldown = 8;
			}
			else if (
				ed.type != BWAPI::UnitTypes::Terran_Goliath &&
				ed.type != BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode &&
				ed.type != BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode &&
				ed.type != BWAPI::UnitTypes::Zerg_Ultralisk &&
				!ed.unit->isStimmed())      // handled by earlier stimmed unit adjustment
			{
				groundCooldown = 5 * groundCooldown / 4;
				airCooldown = 5 * airCooldown / 4;
			}
		}

		//if (ed.unit && ed.unit->isVisible() && !ed.unit->isFlying()) {
			elevation = BWAPI::Broodwar->getGroundHeight(BWAPI::TilePosition(ed.lastPosition));
		//}

		groundMaxRange *= groundMaxRange;
		groundMinRange *= groundMinRange;
		airMaxRange *= airMaxRange;
		airMinRange *= airMinRange;

		health <<= 8;
		maxHealth <<= 8;
		shields <<= 8;
		maxShields <<= 8;
	}

	const FastAPproximation::FAPUnit &FastAPproximation::FAPUnit::operator=(const FAPUnit & other) const {
		x = other.x, y = other.y;
		health = other.health, maxHealth = other.maxHealth;
		shields = other.shields, maxShields = other.maxShields;
		speed = other.speed, armor = other.armor, flying = other.flying, 
		unitSize = other.unitSize;
		groundDamage = other.groundDamage, groundCooldown = other.groundCooldown, 
		groundMaxRange = other.groundMaxRange, groundMinRange = other.groundMinRange, 
		groundDamageType = other.groundDamageType;
		airDamage = other.airDamage, airCooldown = other.airCooldown, 
		airMaxRange = other.airMaxRange, airMinRange = other.airMinRange, 
		airDamageType = other.airDamageType;
		score = other.score;
		attackCooldownRemaining = other.attackCooldownRemaining;
		unitType = other.unitType; 
		isOrganic = other.isOrganic;
		didHealThisFrame = other.didHealThisFrame;
		elevation = other.elevation;
		player = other.player;

		return *this;
	}

	bool FastAPproximation::FAPUnit::operator<(const FAPUnit & other) const {
		return id < other.id;
	}

}
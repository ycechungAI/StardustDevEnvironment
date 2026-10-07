// Micro static defense buildings.
// For this first attempt, only sunkens.

#include "MicroStaticDefense.h"

#include "The.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Approximate hits to kill, ignoring armor, upgrades, regeneration, and other attackers.
// NOTE This is for sunken colonies only. They do explosive damage.
int MicroTargetRecord::findHitsToKill(BWAPI::Unit t) const
{
	// Don't ignore targets under dark swarm, but call them very hard to kill.
	if (t->isUnderDarkSwarm())
	{
		return 100;
	}

	const int baseDamage = 40;
	int hits = 0;
	int shieldPassthrough = 0;		// body damage after a hit finishes off a shield

	// Protoss shield damage.
	// Treat a terran defense matrix as a shield of strength 100. We don't know its current true strength.
	if (t->isDefenseMatrixed())
	{
		shieldPassthrough += 100 % baseDamage;
		hits += 100 / baseDamage + (shieldPassthrough > 0 ? 1 : 0);
	}
	if (t->getType().getRace() == BWAPI::Races::Protoss)
	{
		shieldPassthrough += t->getType().maxShields() % baseDamage;
		hits += t->getShields() / baseDamage + (shieldPassthrough > 0 ? 1 : 0);
	}

	// Body damage.
	int damage = baseDamage;
	if (t->getType().size() == BWAPI::UnitSizeTypes::Small)
	{
		damage /= 2;
		shieldPassthrough /= 2;
	}
	else if (t->getType().size() == BWAPI::UnitSizeTypes::Medium)
	{
		damage = 3 * damage / 4;
		shieldPassthrough = 3 * damage / 4;
	}

	int hp = t->getHitPoints() - shieldPassthrough;

	if (hp > 0)
	{
		hits += 1 + hp / damage;
	}

	return hits;
}

MicroTargetRecord::MicroTargetRecord()
	: lastUpdateFrame(0)
	, hitsToKill(0)
{
}

MicroTargetRecord::MicroTargetRecord(BWAPI::Unit t)
	: lastUpdateFrame(the.now())
	, hitsToKill(findHitsToKill(t))
{
}

void MicroTargetRecord::update(BWAPI::Unit target)
{
	lastUpdateFrame = the.now();
	hitsToKill = findHitsToKill(target);
};

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// How many defenders are already intending to attack this enemy?
// Used to avoid wasting shots on overkill.
int MicroStaticDefense::pendingHits(BWAPI::Unit defender, BWAPI::Unit enemy) const
{
	int count = 0;

	for (BWAPI::Unit d : _defenders)
	{
		if (d != defender)
		{
			auto it = _attacksOut.find(d);
			if (it != _attacksOut.end() && it->second == enemy)
			{
				++count;
			}
		}
	}

	return count;
}

// Gang up on the weakest enemy that is not already doomed by intended attacks.
BWAPI::Unit MicroStaticDefense::findTarget(BWAPI::Unit defender) const
{
	BWAPI::Unit bestTarget = nullptr;
	int bestHitsToKill = 999999;
	for (auto t = _targets.begin(); t != _targets.end(); ++t)
	{
		BWAPI::Unit enemy = t->first;
		if (!enemy->exists() || defender->getDistance(enemy) > MaxRange)
		{
			 continue;
		}
		int hitsToKill = t->second.hitsToKill - pendingHits(defender, enemy);
		if (hitsToKill < bestHitsToKill && hitsToKill > 0)
		{
			if (hitsToKill == 1)
			{
				return enemy;
			}
			bestTarget = enemy;
			bestHitsToKill = hitsToKill;
		}
	}
	return bestTarget;
}

void MicroStaticDefense::updateDefenders()
{
	_defenders.clear();

	for (BWAPI::Unit u : the.info.getStaticDefense())
	{
		if (u->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony && u->isCompleted())
		{
			_defenders.insert(u);
		}
	}
}

void MicroStaticDefense::updateEnemies()
{
	bool any = false;

	for (BWAPI::Unit defender : _defenders)
	{
		for (BWAPI::Unit enemy : BWAPI::Broodwar->getUnitsInRadius(
				defender->getPosition(),
				MaxRange,
				BWAPI::Filter::IsEnemy && !BWAPI::Filter::IsFlying)
			)
		{
			any = true;
			auto it = _targets.find(enemy);
			if (it == _targets.end())
			{
				_targets[enemy] = MicroTargetRecord(enemy);		// add new enemy
			}
			else if (it->second.lastUpdateFrame != the.now())
			{
				it->second.update(enemy);						// update existing enemy
			}
		}
	}

	// Housekeeping.
	if (!any)
	{
		_targets.clear();
	}
}

void MicroStaticDefense::assignTargetsAndFire()
{
	for (BWAPI::Unit d : _defenders)
	{
		//BWAPI::Broodwar->drawTextMap(d->getPosition() + BWAPI::Position(0, -12), "%c%d %d", white,
		//	d->getGroundWeaponCooldown(), d->getOrderTimer());

		{
			BWAPI::Unit enemy = findTarget(d);
			if (enemy)
			{
				the.micro.AttackUnit(d, enemy);
				_attacksOut[d] = enemy;
				//BWAPI::Broodwar->drawTextMap(d->getPosition().x, d->getPosition().y - 22, "%ctarget %d", orange, _targets.at(enemy).hitsToKill);
				//BWAPI::Broodwar->drawLineMap(d->getPosition(), enemy->getPosition(), BWAPI::Colors::Red);
			}
			else
			{
				//BWAPI::Broodwar->drawTextMap(d->getPosition().x, d->getPosition().y - 22, "%cno target", cyan);
			}
		}
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

MicroStaticDefense::MicroStaticDefense()
{
}

void MicroStaticDefense::update()
{
	updateDefenders();
	updateEnemies();
	assignTargetsAndFire();

	// draw();
}

void MicroStaticDefense::draw()
{
	for (BWAPI::Unit defender : _defenders)
	{
		if (defender->getTarget())
		{
			BWAPI::Broodwar->drawLineMap(defender->getPosition(), defender->getTarget()->getPosition(), BWAPI::Colors::Orange);
		}
		if (defender->getOrderTarget())
		{
			BWAPI::Broodwar->drawLineMap(defender->getPosition(), defender->getOrderTarget()->getPosition(), BWAPI::Colors::Yellow);
		}
		if (_attacksOut.find(defender) != _attacksOut.end() && _attacksOut[defender]->exists())
		{
			BWAPI::Broodwar->drawLineMap(defender->getPosition(), _attacksOut[defender]->getPosition(), BWAPI::Colors::Red);
		}
		if (defender->isAttacking())
		{
			BWAPI::Broodwar->drawTextMap(defender->getPosition(), "%catk", yellow);
		}
		if (defender->isAttackFrame())
		{
			BWAPI::Broodwar->drawTextMap(defender->getPosition() + BWAPI::Position(0, 10), "%catk frame", orange);
		}
	}

	for (auto t = _targets.begin(); t != _targets.end(); ++t)
	{
		if (t->first->exists() && t->first->getPosition().isValid())
		{
			BWAPI::Broodwar->drawTextMap(t->first->getPosition(), "%c%d", white, t->second.hitsToKill);
		}
	}
}
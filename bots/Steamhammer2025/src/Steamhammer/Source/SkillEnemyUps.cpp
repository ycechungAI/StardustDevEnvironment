#include "SkillEnemyUps.h"

#include "InformationManager.h"
#include "The.h"

using namespace UAlbertaBot;

// Enemy research and upgrade timings.
// Record the first time each enemy research or upgrade is detected, in frames.
// We ignore energy upgrades because their effects are not very important to the opponent.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Private.

void UAlbertaBot::SkillEnemyUps::recordTech(BWAPI::TechType t)
{
	if (techs.find(t) == techs.end())
	{
		techs.insert(std::pair(t, the.now()));
	}
}

// Enemy upgrades are free for us to see at any time.
// (Their tech has to be inferred from its effects, though.)
void UAlbertaBot::SkillEnemyUps::updateUpgrades(const std::vector<BWAPI::UpgradeType> & us)
{
	for (BWAPI::UpgradeType u : us)
	{
		int level = the.enemy()->getUpgradeLevel(u);
		if (level > 0 && ups.find(std::pair(u, level)) == ups.end())
		{
			ups.insert(std::pair(std::pair(u, level), the.now()));
		}
	}
}

void UAlbertaBot::SkillEnemyUps::updateTerran()
{
	// NOTE Steamhammer makes only a weak attempt to detect restoration. It can easily miss it.
	if (the.info.enemyHasStim())        recordTech(BWAPI::TechTypes::Stim_Packs);
	if (the.info.enemyHasOpticFlare())  recordTech(BWAPI::TechTypes::Optical_Flare);
	if (the.info.enemyHasRestoration()) recordTech(BWAPI::TechTypes::Restoration);
	if (the.info.enemyHasMines())       recordTech(BWAPI::TechTypes::Spider_Mines);
	if (the.info.enemyHasSiegeMode())   recordTech(BWAPI::TechTypes::Tank_Siege_Mode);
	if (the.info.enemyHasIrradiate())   recordTech(BWAPI::TechTypes::Irradiate);
	if (the.info.enemyHasEMP())         recordTech(BWAPI::TechTypes::EMP_Shockwave);
	if (the.info.enemyHasLockdown())    recordTech(BWAPI::TechTypes::Lockdown);
	if (the.info.enemyHasYamato())      recordTech(BWAPI::TechTypes::Yamato_Gun);
	if (the.info.enemyHasNuke())        recordTech(BWAPI::TechTypes::Nuclear_Strike);

	updateUpgrades(
		{ BWAPI::UpgradeTypes::U_238_Shells
		, BWAPI::UpgradeTypes::Ion_Thrusters
		, BWAPI::UpgradeTypes::Charon_Boosters
		, BWAPI::UpgradeTypes::Ocular_Implants

		, BWAPI::UpgradeTypes::Terran_Infantry_Weapons
		, BWAPI::UpgradeTypes::Terran_Infantry_Armor
		, BWAPI::UpgradeTypes::Terran_Vehicle_Weapons
		, BWAPI::UpgradeTypes::Terran_Vehicle_Plating
		, BWAPI::UpgradeTypes::Terran_Ship_Weapons
		, BWAPI::UpgradeTypes::Terran_Ship_Plating
		});
}

void UAlbertaBot::SkillEnemyUps::updateProtoss()
{
	// Not included: Hallucination. No bot uses it as far as I know.
	// Also left out: Mind control. Agt least one bot uses it, but it's rare.
	if (the.info.enemyHasStorm())      recordTech(BWAPI::TechTypes::Psionic_Storm);
	if (the.info.enemyHasMaelstrom())  recordTech(BWAPI::TechTypes::Maelstrom);
	if (the.info.enemyHasDWeb())       recordTech(BWAPI::TechTypes::Disruption_Web);
	if (the.info.enemyHasStasis())     recordTech(BWAPI::TechTypes::Stasis_Field);
	if (the.info.enemyHasRecall())     recordTech(BWAPI::TechTypes::Recall);

	updateUpgrades(
		{ BWAPI::UpgradeTypes::Singularity_Charge
		, BWAPI::UpgradeTypes::Leg_Enhancements
		, BWAPI::UpgradeTypes::Scarab_Damage
		, BWAPI::UpgradeTypes::Carrier_Capacity

		, BWAPI::UpgradeTypes::Protoss_Ground_Weapons
		, BWAPI::UpgradeTypes::Protoss_Ground_Armor
		, BWAPI::UpgradeTypes::Protoss_Air_Weapons
		, BWAPI::UpgradeTypes::Protoss_Air_Armor
		, BWAPI::UpgradeTypes::Protoss_Plasma_Shields
		});
}

void UAlbertaBot::SkillEnemyUps::updateZerg()
{
	if (the.info.enemyHasLurkers())    recordTech(BWAPI::TechTypes::Lurker_Aspect);
	if (the.info.enemyHasEnsnare())    recordTech(BWAPI::TechTypes::Ensnare);
	if (the.info.enemyHasBroodling())  recordTech(BWAPI::TechTypes::Spawn_Broodlings);
	if (the.info.enemyHasPlague())     recordTech(BWAPI::TechTypes::Plague);

	updateUpgrades(
		{ BWAPI::UpgradeTypes::Ventral_Sacs
		, BWAPI::UpgradeTypes::Pneumatized_Carapace
		, BWAPI::UpgradeTypes::Metabolic_Boost
		, BWAPI::UpgradeTypes::Adrenal_Glands
		, BWAPI::UpgradeTypes::Muscular_Augments
		, BWAPI::UpgradeTypes::Grooved_Spines
		, BWAPI::UpgradeTypes::Chitinous_Plating
		, BWAPI::UpgradeTypes::Anabolic_Synthesis

		, BWAPI::UpgradeTypes::Zerg_Melee_Attacks
		, BWAPI::UpgradeTypes::Zerg_Missile_Attacks
		, BWAPI::UpgradeTypes::Zerg_Carapace
		, BWAPI::UpgradeTypes::Zerg_Flyer_Attacks
		, BWAPI::UpgradeTypes::Zerg_Flyer_Carapace
		});
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Public.

SkillEnemyUps::SkillEnemyUps()
    : Skill("enemy ups")
{
    // We should not see any enemy research or upgrade before this.
	// Time at forge or evo is 600 build + 4000 research, ignoring mineral gathering.
	// Fastest is to research burrow at a hatchery, 1200 frames. But that's rare,
	// and besides, you have to gather 100 gas first, which takes time.
	_nextUpdateFrame = 4600;
}

// Write the timings for the current game to a string.
std::string SkillEnemyUps::putData() const
{
    std::stringstream s;

	// Techs: <tech #> <frame>.
    for (const std::pair<BWAPI::TechType, int> & techTime : techs)
    {
        s << ' ' << int(techTime.first) << ' ' << techTime.second;
    }

	s << " +";				// separator

	// Upgrades: <upgrade #> <level> <frame>.
	for (const std::pair<std::pair<BWAPI::UpgradeType, int>, int> & upTime : ups)
	{
		s << ' ' << int(upTime.first.first) << ' ' << int(upTime.first.second) << ' ' << upTime.second;
	}

	return s.str();
}

// Read in past timings for one game record from a string.
void SkillEnemyUps::getData(GameRecord & r, const std::string & line)
{
	// TODO unimplemented
}

// Unit frame values are scouting times--when it was first seen.
// Building values are the building's completion time--possibly in the future.
// Don't record MAX_FRAME completion times. They don't convey information.
void SkillEnemyUps::update()
{
	if (the.enemyRace() == BWAPI::Races::Terran)
	{
		updateTerran();
	}
	else if (the.enemyRace() == BWAPI::Races::Protoss)
	{
		updateProtoss();
	}
	else if (the.enemyRace() == BWAPI::Races::Zerg)
	{
		updateZerg();
	}
	// If enemy race is Unknown, skip it.

	// Knowing the timings to this accuracy is good enough.
    _nextUpdateFrame = the.now() + 2 * 24 + 1;
}

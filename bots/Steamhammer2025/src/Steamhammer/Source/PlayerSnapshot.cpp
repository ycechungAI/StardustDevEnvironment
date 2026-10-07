#include "Common.h"
#include "PlayerSnapshot.h"

#include "Bases.h"
#include "InformationManager.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Is this unit type to be excluded from the game record?
// We leave out boring units like interceptors. Larvas are interesting.
// Neutral boring unit types don't need to be listed here.
bool PlayerSnapshot::excludeType(BWAPI::UnitType type) const
{
    return
        type == BWAPI::UnitTypes::Zerg_Egg ||
        type == BWAPI::UnitTypes::Protoss_Interceptor ||
        type == BWAPI::UnitTypes::Protoss_Scarab;
}

void PlayerSnapshot::reset(BWAPI::Player side)
{
    player = side;
    numBases = the.bases.baseCount(player);
    unitCounts.clear();
}

// If we don't already know about the enemy building, add it along with inferences from it.
void PlayerSnapshot::maybeAddBuilding(const PlayerSnapshot & ever, BWAPI::UnitType buildingType)
{
	if (ever.getCounts().find(buildingType) == ever.getCounts().end() &&
		!buildingType.isResourceDepot())
	{
		unitCounts[buildingType] = 1;
		inferUnseenRequirements(ever, buildingType);
	}
}

// The enemy might have upgrades that we can infer buildings from.
// The upgrades for different races are mixed together in the enum,
// so it's most efficient to list them for each race explicitly.
const std::vector<BWAPI::UpgradeType> TerranUpgrades =
	{ BWAPI::UpgradeTypes::Terran_Infantry_Armor
	, BWAPI::UpgradeTypes::Terran_Vehicle_Plating
	, BWAPI::UpgradeTypes::Terran_Ship_Plating
	, BWAPI::UpgradeTypes::Terran_Infantry_Weapons
	, BWAPI::UpgradeTypes::Terran_Vehicle_Weapons
	, BWAPI::UpgradeTypes::Terran_Ship_Weapons
	, BWAPI::UpgradeTypes::U_238_Shells
	, BWAPI::UpgradeTypes::Ion_Thrusters
	, BWAPI::UpgradeTypes::Titan_Reactor
	, BWAPI::UpgradeTypes::Ocular_Implants
	, BWAPI::UpgradeTypes::Moebius_Reactor
	, BWAPI::UpgradeTypes::Apollo_Reactor
	, BWAPI::UpgradeTypes::Colossus_Reactor
	, BWAPI::UpgradeTypes::Caduceus_Reactor
	, BWAPI::UpgradeTypes::Charon_Boosters
	};

const std::vector<BWAPI::UpgradeType> ProtossUpgrades =
	{ BWAPI::UpgradeTypes::Protoss_Ground_Armor
	, BWAPI::UpgradeTypes::Protoss_Air_Armor
	, BWAPI::UpgradeTypes::Protoss_Ground_Weapons
	, BWAPI::UpgradeTypes::Protoss_Air_Weapons
	, BWAPI::UpgradeTypes::Protoss_Plasma_Shields
	, BWAPI::UpgradeTypes::Singularity_Charge
	, BWAPI::UpgradeTypes::Leg_Enhancements
	, BWAPI::UpgradeTypes::Scarab_Damage
	, BWAPI::UpgradeTypes::Reaver_Capacity
	, BWAPI::UpgradeTypes::Gravitic_Drive
	, BWAPI::UpgradeTypes::Sensor_Array
	, BWAPI::UpgradeTypes::Gravitic_Boosters
	, BWAPI::UpgradeTypes::Khaydarin_Amulet
	, BWAPI::UpgradeTypes::Apial_Sensors
	, BWAPI::UpgradeTypes::Gravitic_Thrusters
	, BWAPI::UpgradeTypes::Carrier_Capacity
	, BWAPI::UpgradeTypes::Khaydarin_Core
	, BWAPI::UpgradeTypes::Argus_Jewel
	, BWAPI::UpgradeTypes::Argus_Talisman
	};

const std::vector<BWAPI::UpgradeType> ZergUpgrades =
	{ BWAPI::UpgradeTypes::Zerg_Carapace
	, BWAPI::UpgradeTypes::Zerg_Flyer_Carapace
	, BWAPI::UpgradeTypes::Zerg_Melee_Attacks
	, BWAPI::UpgradeTypes::Zerg_Missile_Attacks
	, BWAPI::UpgradeTypes::Zerg_Flyer_Attacks
	, BWAPI::UpgradeTypes::Ventral_Sacs
	, BWAPI::UpgradeTypes::Antennae
	, BWAPI::UpgradeTypes::Pneumatized_Carapace
	, BWAPI::UpgradeTypes::Metabolic_Boost
	, BWAPI::UpgradeTypes::Adrenal_Glands
	, BWAPI::UpgradeTypes::Muscular_Augments
	, BWAPI::UpgradeTypes::Grooved_Spines
	, BWAPI::UpgradeTypes::Gamete_Meiosis
	, BWAPI::UpgradeTypes::Metasynaptic_Node
	, BWAPI::UpgradeTypes::Chitinous_Plating
	, BWAPI::UpgradeTypes::Anabolic_Synthesis
	};

void PlayerSnapshot::inferRequirementsFromUpgrades(const PlayerSnapshot & ever)
{
	const std::vector<BWAPI::UpgradeType> & ups =
		the.enemyRace() == BWAPI::Races::Terran ? TerranUpgrades :
		(the.enemyRace() == BWAPI::Races::Protoss ? ProtossUpgrades : ZergUpgrades);

	for (BWAPI::UpgradeType up : ups)
	{
		int level = the.enemy()->getUpgradeLevel(up);
		if (level > 0)
		{
			maybeAddBuilding(ever, up.whatUpgrades());
			BWAPI::UnitType required = up.whatsRequired(level);
			if (required != BWAPI::UnitTypes::None)
			{
				maybeAddBuilding(ever, required);
			}
		}
	}
}

// Trace back the tech tree to see what tech buildings the enemy required in order to have
// produced a unit of type t. Record any requirements that we haven't yet seen.
// Don't record required units. An SCV is required for a barracks, but don't count the SCV.
// A hydralisk is required for a lurker, but it is used up in the making.
// Used only in takeEnemyInferred() to fill in the.your.inferred.
// NOTE To infer correctly after the enemy uses mind control, you need to know what unit types
//      the enemy has mind controlled. Steamhammer does not track that info.
// NOTE Inferring a lair, hive, or greater spire can incorrectly cause belief in an extra
//      hatchery or spire that does not exist. The data structure does not support this inference
//      correctly. E.g. 'ever' has 1 hatchery, and we infer a lair from seeing a mutalisk. The
//      lair may have been morphed from the hatchery, but 'ever' records what we've seen and the
//      inference cannot override that. We see 1 known hatchery + 1 inferred lair, in two
//      different data structures.
//		It can sometimes falsely infer a creep colony.
void PlayerSnapshot::inferUnseenRequirements(const PlayerSnapshot & ever, BWAPI::UnitType t)
{
    if (t.isWorker() ||
		t.supplyProvided() > 0 ||		// resource depot or supply unit
		t == BWAPI::UnitTypes::Zerg_Larva || t == BWAPI::UnitTypes::Zerg_Egg)
    {
		// Workers are excluded to prevent a possible infinite recursion.
		// BWAPI believes that a larva or egg costs 1 gas. They are excluded
		// to avoid mistakenly inferring a refinery (and it'd waste time anyway).
        return;
    }
    std::map<BWAPI::UnitType, int> requirements = t.requiredUnits();
    if (t == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine)
    {
        requirements[BWAPI::UnitTypes::Terran_Factory] = 1;
        requirements[BWAPI::UnitTypes::Terran_Machine_Shop] = 1;
    }
    if (t.gasPrice() > 0)
    {
        requirements[the.enemyRace().getRefinery()] = 1;
    }
    for (std::pair<BWAPI::UnitType, int> requirement : requirements)
    {
        BWAPI::UnitType requiredType = requirement.first;
        if (ever.getCounts().find(requiredType) == ever.getCounts().end() &&
            unitCounts.find(requiredType) == unitCounts.end())
        {
            if (requiredType.isBuilding() &&
                !UnitUtil::BuildingIsMorphedFrom(requiredType, t) &&
                requiredType.getRace() == the.enemyRace())        // exclude some mistakes due to mind control
            {
				//Logger::LogAppendToFile(Config::IO::ErrorLogFilename, "  infer %s <- %s\n", t.getName().c_str(), requiredType.getName().c_str());
				unitCounts[requiredType] = 1;
            }
            inferUnseenRequirements(ever, requiredType);
        }
    }
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

PlayerSnapshot::PlayerSnapshot()
    : player(BWAPI::Broodwar->neutral())
    , numBases(0)
{
}

PlayerSnapshot::PlayerSnapshot(BWAPI::Player side)
{
    if (side == the.self())
    {
        takeSelf();
    }
    else if (side == the.enemy())
    {
        takeEnemy();
    }
    else
    {
        UAB_ASSERT(false, "bad player");
    }
}

// Create a snapshot from a set of units, excluding none.
PlayerSnapshot::PlayerSnapshot(const BWAPI::Unitset & units)
{
    if (units.empty())
    {
        return;
    }

    reset((*units.begin())->getPlayer());
    for (BWAPI::Unit unit : units)
    {
        ++unitCounts[unit->getType()];
    }
}

// Include my valid, completed units.
void PlayerSnapshot::takeSelf()
{
    reset(the.self());

    for (BWAPI::Unit unit : the.self()->getUnits())
    {
        if (unit->isCompleted() && !excludeType(unit->getType()))
        {
			++unitCounts[unit->getType()];
        }
    }
}

// Also include my uncompleted units.
void PlayerSnapshot::takeSelfAll()
{
    reset(the.self());

    for (BWAPI::Unit unit : the.self()->getUnits())
    {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg)
        {
			int n = unit->getBuildType().isTwoUnitsInOneEgg() ? 2 : 1;
            unitCounts[unit->getBuildType()] += n;
        }
        else if (unit->getType() == BWAPI::UnitTypes::Zerg_Lurker_Egg || unit->getType() == BWAPI::UnitTypes::Zerg_Cocoon)
        {
			++unitCounts[unit->getBuildType()];
        }
        else if (!excludeType(unit->getType()))
        {
			++unitCounts[unit->getType()];
        }

        // If the unit is a building, it may be training another unit that we should count.
        if (unit->isTraining() && unit->getBuildType() != BWAPI::UnitTypes::None && !excludeType(unit->getBuildType()))
        {
			++unitCounts[unit->getBuildType()];
        }
    }
}

// Include enemy units that have been seen.
// Include incomplete buildings, but not other incomplete units.
// The plan recognizer pays attention to incomplete buildings.
void PlayerSnapshot::takeEnemy()
{
    reset(the.enemy());

    for (const auto & kv : the.info.getUnitData(the.enemy()).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if ((ui.completed || ui.type.isBuilding()) && !excludeType(ui.type))
        {
			++unitCounts[ui.type];
        }
    }
}

// Count 1 for an enemy type that has ever been seen. Don't erase old values.
// This must be complete for takeEnemyInferred() below to work correctly.
// Must coordinate with initialEverTypeCount below.
void PlayerSnapshot::takeEnemyEver(const PlayerSnapshot & seen)
{
    if (the.enemyRace() == BWAPI::Races::Terran)
    {
        unitCounts[BWAPI::UnitTypes::Terran_Command_Center] = 1;
        unitCounts[BWAPI::UnitTypes::Terran_SCV] = 1;
    }
    else if (the.enemyRace() == BWAPI::Races::Protoss)
    {
        unitCounts[BWAPI::UnitTypes::Protoss_Nexus] = 1;
        unitCounts[BWAPI::UnitTypes::Protoss_Probe] = 1;
    }
    else if (the.enemyRace() == BWAPI::Races::Zerg)
    {
        unitCounts[BWAPI::UnitTypes::Zerg_Hatchery] = 1;
        unitCounts[BWAPI::UnitTypes::Zerg_Larva] = 1;
        unitCounts[BWAPI::UnitTypes::Zerg_Drone] = 1;
        unitCounts[BWAPI::UnitTypes::Zerg_Overlord] = 1;
    }
    for (const std::pair<BWAPI::UnitType, int> & unitCount : seen.getCounts())
    {
        unitCounts[unitCount.first] = 1;
    }
}

// The number of unit types at game start in the.your.ever; that is, the initial value of
// the.your.ever.unitCounts.size(). Used for telling when we've seen something new.
// Must coordinate with takeEnemyEver() above.
int PlayerSnapshot::initialEverTypeCount() const
{
    if (the.enemyRace() == BWAPI::Races::Terran)
    {
        return 2;
    }
    else if (the.enemyRace() == BWAPI::Races::Protoss)
    {
        return 2;
    }
    else if (the.enemyRace() == BWAPI::Races::Zerg)
    {
        return 4;
    }

    // Random.
    return 0;
}

// Only enemy units whose existence can be inferred.
// For example, if we have ever seen a marine, then we know that there is (or was) a barracks.
// We pass in the marine; this only counts the barracks.
void PlayerSnapshot::takeEnemyInferred(const PlayerSnapshot & ever)
{
	if (the.enemyRace() == BWAPI::Races::Unknown)
	{
		return;
	}

    reset(the.enemy());

	inferRequirementsFromUpgrades(ever);

    for (const std::pair<BWAPI::UnitType, int> & unitCount : ever.getCounts())
    {
        BWAPI::UnitType t = unitCount.first;

        if (!excludeType(t))
        {
            inferUnseenRequirements(ever, t);
        }
    }
}

int PlayerSnapshot::count() const
{
	int total = 0;
	for (const std::pair<BWAPI::UnitType, int> & unitCount : unitCounts)
	{
		total += unitCount.second;
	}
	return total;
}

int PlayerSnapshot::count(BWAPI::UnitType type) const
{
    auto it = unitCounts.find(type);
    if (it == unitCounts.end())
    {
        return 0;
    }
    return it->second;
}

int PlayerSnapshot::countWorkers() const
{
    return
        count(BWAPI::UnitTypes::Terran_SCV) +
        count(BWAPI::UnitTypes::Protoss_Probe) +
        count(BWAPI::UnitTypes::Zerg_Drone);
}

// Count supply "by hand"--useful for finding the lower limit of the enemy supply used.
int PlayerSnapshot::getSupply() const
{
    int supply = 0;

    for (const std::pair<BWAPI::UnitType, int> & unitCount : unitCounts)
    {
        if (!unitCount.first.isBuilding())
        {
            supply += unitCount.first.supplyRequired() * unitCount.second;
        }
    }

    return supply;
}

std::string PlayerSnapshot::debugString() const
{
    std::stringstream ss;

    ss << numBases;

    for (const std::pair<BWAPI::UnitType, int> & unitCount : unitCounts)
    {
        ss << ' ' << unitCount.first.getName() << ':' << unitCount.second;
    }

    ss << '\n';

    return ss.str();
}
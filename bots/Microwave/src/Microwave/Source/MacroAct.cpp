#include "MacroAct.h"
#include "ProductionManager.h"

#include <regex>

using namespace UAlbertaBot;

MacroLocation MacroAct::getMacroLocationFromString(const std::string & s)
{
	if (s == "macro")
	{
		return MacroLocation::Macro;
	}
	if (s == "main")
	{
		return MacroLocation::Main;
	}
	if (s == "natural")
	{
		return MacroLocation::Natural;
	}
	if (s == "third")
	{
		return MacroLocation::Third;
	}
	if (s == "expo")
	{
		return MacroLocation::Expo;
	}
	if (s == "min only")
	{
		return MacroLocation::MinOnly;
	}
	if (s == "gas only")
	{
		return MacroLocation::GasOnly;
	}
	if (s == "hidden")
	{
		return MacroLocation::Hidden;
	}
	if (s == "center")
	{
		return MacroLocation::Center;
	}
	if (s == "enemy main")
	{
		return MacroLocation::EnemyMain;
	}
	if (s == "enemy natural")
	{
		return MacroLocation::EnemyNatural;
	}
	if (s == "proxy")
	{
		return MacroLocation::Proxy;
	}

	UAB_ASSERT(false, "config file - bad location '%s' after @", s.c_str());

	return MacroLocation::Anywhere;
}

MacroAct::MacroAct()
	: _type(MacroActs::Default)
	, _race(BWAPI::Races::None)
	, _macroLocation(MacroLocation::Anywhere)
	, _reservedPosition(BWAPI::TilePositions::None)
{
}

// Create a MacroAct from its name, like "drone" or "hatchery @ minonly".
// String comparison here is case-insensitive.
MacroAct::MacroAct(const std::string & name)
	: _type(MacroActs::Default)
	, _race(BWAPI::Races::None)
	, _macroLocation(MacroLocation::Anywhere)
	, _reservedPosition(BWAPI::TilePositions::None)
{
	std::string inputName(name);
	std::replace(inputName.begin(), inputName.end(), '_', ' ');
	std::transform(inputName.begin(), inputName.end(), inputName.begin(), ::tolower);

	// Commands like "go gas until 100". 100 is the amount.
	if (inputName.substr(0, 3) == std::string("go "))
	{
		for (const MacroCommandType t : MacroCommand::allCommandTypes())
		{
			std::string commandName = MacroCommand::getName(t);
			if (MacroCommand::hasArgument(t))
			{
				// There's an argument. Match the command name and parse out the argument.
				std::regex commandWithArgRegex(commandName + " (\\d+)");
				std::smatch m;
				if (std::regex_match(inputName, m, commandWithArgRegex)) {
					int amount = GetIntFromString(m[1].str());
					if (amount >= 0) {
						*this = MacroAct(t, amount);
						return;
					}
				}
			}
			else
			{
				// No argument. Just compare for equality.
				if (commandName == inputName)
				{
					*this = MacroAct(t);
					return;
				}
			}
		}
	}

	MacroLocation specifiedMacroLocation(MacroLocation::Anywhere);    // the default

	// Buildings can specify a location, like "hatchery @ expo".
	// It's meaningless and ignored for anything except a building.
	// Here we parse out the building and its location.
	// Since buildings are units, only UnitType below sets _macroLocation.
	std::regex macroLocationRegex("([a-z_ ]+[a-z])\\s*\\@\\s*([a-z][a-z ]+)");
	std::smatch m;
	if (std::regex_match(inputName, m, macroLocationRegex)) {
		specifiedMacroLocation = getMacroLocationFromString(m[2].str());
		// Don't change inputName before using the results from the regex.
		// Fix via gnuborg, who credited it to jaj22.
		inputName = m[1].str();
	}

	for (const BWAPI::UnitType & unitType : BWAPI::UnitTypes::allUnitTypes())
	{
		// check to see if the names match exactly
		std::string typeName = unitType.getName();
		std::replace(typeName.begin(), typeName.end(), '_', ' ');
		std::transform(typeName.begin(), typeName.end(), typeName.begin(), ::tolower);
		if (typeName == inputName)
		{
			*this = MacroAct(unitType);
			_macroLocation = specifiedMacroLocation;
			return;
		}

		// check to see if the names match without the race prefix
		std::string raceName = unitType.getRace().getName();
		std::transform(raceName.begin(), raceName.end(), raceName.begin(), ::tolower);
		if ((typeName.length() > raceName.length()) && (typeName.compare(raceName.length() + 1, typeName.length(), inputName) == 0))
		{
			*this = MacroAct(unitType);
			_macroLocation = specifiedMacroLocation;
			return;
		}
	}

	for (const BWAPI::TechType & techType : BWAPI::TechTypes::allTechTypes())
	{
		std::string typeName = techType.getName();
		std::replace(typeName.begin(), typeName.end(), '_', ' ');
		std::transform(typeName.begin(), typeName.end(), typeName.begin(), ::tolower);
		if (typeName == inputName)
		{
			*this = MacroAct(techType);
			return;
		}
	}

	for (const BWAPI::UpgradeType & upgradeType : BWAPI::UpgradeTypes::allUpgradeTypes())
	{
		std::string typeName = upgradeType.getName();
		std::replace(typeName.begin(), typeName.end(), '_', ' ');
		std::transform(typeName.begin(), typeName.end(), typeName.begin(), ::tolower);
		if (typeName == inputName)
		{
			*this = MacroAct(upgradeType);
			return;
		}
	}

	UAB_ASSERT_WARNING(false, "Could not find MacroAct with name: %s", name.c_str());
}

MacroAct::MacroAct(BWAPI::UnitType t)
	: _unitType(t)
	, _type(MacroActs::Unit)
	, _race(t.getRace())
	, _macroLocation(MacroLocation::Anywhere)
	, _reservedPosition(BWAPI::TilePositions::None)
{
}

MacroAct::MacroAct(BWAPI::UnitType t, MacroLocation loc)
	: _unitType(t)
	, _type(MacroActs::Unit)
	, _race(t.getRace())
	, _macroLocation(loc)
	, _reservedPosition(BWAPI::TilePositions::None)
{
}

MacroAct::MacroAct(BWAPI::TechType t)
	: _techType(t)
	, _type(MacroActs::Tech)
	, _race(t.getRace())
	, _macroLocation(MacroLocation::Anywhere)
	, _reservedPosition(BWAPI::TilePositions::None)
{
}

MacroAct::MacroAct(BWAPI::UpgradeType t)
	: _upgradeType(t)
	, _type(MacroActs::Upgrade)
	, _race(t.getRace())
	, _macroLocation(MacroLocation::Anywhere)
	, _reservedPosition(BWAPI::TilePositions::None)
{
}

MacroAct::MacroAct(MacroCommandType t)
	: _macroCommandType(t)
	, _type(MacroActs::Command)
	, _race(BWAPI::Races::None)
	, _macroLocation(MacroLocation::Anywhere)
	, _reservedPosition(BWAPI::TilePositions::None)
{
}

MacroAct::MacroAct(MacroCommandType t, int amount)
	: _macroCommandType(t, amount)
	, _type(MacroActs::Command)
	, _race(BWAPI::Races::None)     // irrelevant
	, _macroLocation(MacroLocation::Anywhere)
	, _reservedPosition(BWAPI::TilePositions::None)
{
}

const size_t & MacroAct::type() const
{
	return _type;
}

bool MacroAct::isUnit() const
{
	return _type == MacroActs::Unit;
}

bool MacroAct::isTech() const
{
	return _type == MacroActs::Tech;
}

bool MacroAct::isUpgrade() const
{
	return _type == MacroActs::Upgrade;
}

bool MacroAct::isCommand() const
{
	return _type == MacroActs::Command;
}

const BWAPI::Race & MacroAct::getRace() const
{
	return _race;
}

bool MacroAct::isBuilding()	const
{
	return _type == MacroActs::Unit && _unitType.isBuilding();
}

bool MacroAct::isRefinery()	const
{
	return _type == MacroActs::Unit && _unitType.isRefinery();
}

// The standard supply unit, ignoring the hatchery (which provides 1 supply).
bool MacroAct::isSupply() const
{
	return isUnit() &&
		(_unitType == BWAPI::UnitTypes::Terran_Supply_Depot
		|| _unitType == BWAPI::UnitTypes::Protoss_Pylon
		|| _unitType == BWAPI::UnitTypes::Zerg_Overlord);
}

bool MacroAct::hasReservedPosition() const
{
	return _reservedPosition != BWAPI::TilePositions::None;
}

bool MacroAct::hasThen() const
{
	return !!_then;
}

const BWAPI::UnitType & MacroAct::getUnitType() const
{
	UAB_ASSERT(_type == MacroActs::Unit, "getUnitType of non-unit");
	return _unitType;
}

const BWAPI::TechType & MacroAct::getTechType() const
{
	UAB_ASSERT(_type == MacroActs::Tech, "getTechType of non-tech");
	return _techType;
}

const BWAPI::UpgradeType & MacroAct::getUpgradeType() const
{
	UAB_ASSERT(_type == MacroActs::Upgrade, "getUpgradeType of non-upgrade");
	return _upgradeType;
}

const MacroCommand MacroAct::getCommandType() const
{
	UAB_ASSERT(_type == MacroActs::Command, "getCommandType of non-command");
	return _macroCommandType;
}

const MacroLocation MacroAct::getMacroLocation() const
{
	return _macroLocation;
}

const BWAPI::TilePosition MacroAct::getReservedPosition() const
{
	UAB_ASSERT(_reservedPosition != BWAPI::TilePositions::None, "no reserved position");
	return _reservedPosition;
}

const MacroAct & MacroAct::getThen() const
{
	UAB_ASSERT(!!_then, "getThen without then");
	return *_then;
}

int MacroAct::supplyRequired() const
{
	if (isUnit())
	{
		return _unitType.supplyRequired();
	}
	return 0;
}

int MacroAct::mineralPrice() const
{
	if (isCommand()) {
		if (_macroCommandType.getType() == MacroCommandType::ExtractorTrick ||
			_macroCommandType.getType() == MacroCommandType::ExtractorTrickZergling) {
			// 50 for the extractor and 50 for the drone/zergling. Never mind that you get some back.
			return 100;
		}
		return 0;
	}
	return isUnit() ? _unitType.mineralPrice() : (isTech() ? _techType.mineralPrice() : _upgradeType.mineralPrice());
}

int MacroAct::gasPrice() const
{
	if (isCommand()) {
		return 0;
	}
	return isUnit() ? _unitType.gasPrice() : (isTech() ? _techType.gasPrice() : _upgradeType.gasPrice());
}

BWAPI::UnitType MacroAct::whatBuilds() const
{
	if (isCommand()) {
		return BWAPI::UnitType(BWAPI::UnitTypes::None);
	}
	return isUnit() ? _unitType.whatBuilds().first : (isTech() ? _techType.whatResearches() : _upgradeType.whatUpgrades());
}

std::string MacroAct::getName() const
{
	if (isUnit())
	{
		return _unitType.getName();
	}
	if (isTech())
	{
		return _techType.getName();
	}
	if (isUpgrade())
	{
		return _upgradeType.getName();
	}
	if (isCommand())
	{
		return _macroCommandType.getName();
	}

	UAB_ASSERT(false, "bad MacroAct");
	return "error";
}

// Check the units needed for producing a unit type, beyond its producer.
bool MacroAct::hasTech() const
{
	// If it's not a unit, let's assume we're good.
	if (!isUnit())
	{
		return true;
	}

	// What we have.
	std::set<BWAPI::UnitType> ourUnitTypes;
	for (BWAPI::Unit unit : BWAPI::Broodwar->self()->getUnits())
	{
		if(unit->isCompleted())
		{ 
			ourUnitTypes.insert(unit->getType());
		}
	}

	// What we need. We only pay attention to the unit type, not the count,
	// which is needed only for merging archons and dark archons (which is not done via MacroAct).
	for (const std::pair<BWAPI::UnitType, int> & typeAndCount : getUnitType().requiredUnits())
	{
		BWAPI::UnitType requiredType = typeAndCount.first;
		if (ourUnitTypes.find(requiredType) == ourUnitTypes.end())
		{
			// BWAPI::Broodwar->printf("missing tech: %s requires %s", getName().c_str(), requiredType.getName().c_str());
			// We don't have a type we need. We don't have the tech.
			return false;
		}
	}

	// We have the technology.
	return true;
}


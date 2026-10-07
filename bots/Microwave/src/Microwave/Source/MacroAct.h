#pragma once

#include "Common.h"
#include "MacroCommand.h"

namespace UAlbertaBot
{

	enum class MacroLocation
	{
		Anywhere       // default location
		, Macro        // macro hatchery or main base building
		, Main         // macro hatchery or main base building
		, Natural      // "natural" first expansion base
		, Third        // "third" expansion base
		, Expo         // gas expansion hatchery
		, MinOnly      // any expansion hatchery (mineral-only or gas, whatever's next)
		, GasOnly      // gas expansion, minerals optional
		, Hidden       // gas expansion hatchery far from both main bases
		, Center       // Center of the map
		, EnemyMain    // Enemy "main" in plain sight
		, EnemyNatural // Enemy "natural" in plain sight
		, Proxy        // hidden in or in range of the enemy main or natural
		, LeastDefendedGround    // Baselocation with least static ground defense
		, LeastDefendedAir       // Baselocation with least static air defense
	};

	namespace MacroActs
	{
		enum { Unit, Tech, Upgrade, Command, Default };
	}

	class MacroAct
	{
		size_t				_type;
		BWAPI::Race			_race;

		BWAPI::UnitType		_unitType;
		BWAPI::TechType		_techType;
		BWAPI::UpgradeType	_upgradeType;
		MacroCommand		_macroCommandType;

		MacroLocation		_macroLocation;

		mutable
		BWAPI::TilePosition _reservedPosition;

		std::shared_ptr<MacroAct> _then;

		MacroLocation		getMacroLocationFromString(const std::string & s);

	public:

		MacroAct();
		MacroAct(const std::string & name);
		MacroAct(BWAPI::UnitType t);
		MacroAct(BWAPI::UnitType t, MacroLocation loc);
		MacroAct(BWAPI::TechType t);
		MacroAct(BWAPI::UpgradeType t);
		MacroAct(MacroCommandType t);
		MacroAct(MacroCommandType t, int amount);

		bool    isUnit()		const;
		bool    isTech()		const;
		bool    isUpgrade()	    const;
		bool    isCommand()	    const;
		bool    isBuilding()	const;
		bool    isRefinery()	const;
		bool	isSupply()		const;

		bool    hasReservedPosition()	const;
		bool	hasThen()				const;
		void    setThen(MacroAct& m) { _then = std::make_shared<MacroAct>(m); };

		const size_t & type() const;
		const BWAPI::Race & getRace() const;

		const BWAPI::UnitType & getUnitType() const;
		const BWAPI::TechType & getTechType() const;
		const BWAPI::UpgradeType & getUpgradeType() const;
		const MacroCommand getCommandType() const;
		const MacroLocation getMacroLocation() const;
		const BWAPI::TilePosition getReservedPosition() const;
		const MacroAct & getThen() const;

		int supplyRequired() const;
		int mineralPrice()   const;
		int gasPrice()       const;

		BWAPI::UnitType whatBuilds() const;
		std::string getName() const;

		bool hasTech() const;
	};
}
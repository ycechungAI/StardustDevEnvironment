#pragma once

#include "StrategyBoss.h"

namespace UAlbertaBot
{
	class StrategyBossProtoss : public StrategyBoss
	{
	private:
		// The primary unit mix.
		enum class Mix
		{
			None
			, Zealots
			, Dragoons
			, DragoonZealot
			, DragoonReaver
			, ZealotArchon
			, MassScouts
			, Carriers
		};

		// Minor components of the army.
		enum class Aux
		{
			  FIRST = 0
			, Observers = 0
			, Reavers
			, DTs
			, Archons
			, Corsairs
			, Scouts
			// , DarkArchon
			// , Arbiter
			, LAST
		};

		const int MaxObservers = 3;		// treat the observer target as fixed

	protected:
		std::string getMixString() const;
		std::string getAuxString() const;

	private:
		// Our unit mix.
		Mix _mix;
		std::set<Aux> _auxes;				// extras, minor additions to the unit mix

		bool isAirMix() const;

		void manageBases();

		void setBackstopRules();
		void setForecastRules();
		void setGroundUpgradeRules();
		void setAirUpgradeRules();
		void setMixUpgradeRules();			// air or ground, depending
		void setAuxUpgradeRules();

		void setSpecialMatchupRules();
		void setMixRules();
		void setAuxRules(Aux aux, bool bothObsAndReaver);
		void setAuxesRules();
		void setRegularRules();

		void setProbeRules();

		void updateAuxPvT();
		void updateMixPvT();
		void setRulesXvT();

		void updateAuxPvP();
		void updateMixPvP();
		void setRulesXvP();

		void updateAuxPvZ();
		void updateMixPvZ();
		void setRulesXvZ();

		bool _emergencyNoBase;
		bool _emergencyEnemyAttack;
		bool _emergencyNeedDetection;
		bool _emergencyNeedAirDefense;
		bool _emergencyNaturalContain;
		bool _emergencyEnemyDead;

		void setBasicsRules();
		void recognizeEmergency();
		void setEmergencyRules();

	public:

		StrategyBossProtoss();
	};
}
#pragma once

#include "Common.h"
#include "InformationManager.h"
#include "WorkerManager.h"
#include "BuildOrder.h"
#include "BuildOrderQueue.h"
#include "UnitUtil.h"
#include "OpponentPlan.h"

namespace UAlbertaBot
{
typedef std::pair<MacroAct, size_t> MetaPair;
typedef std::vector<MetaPair> MetaPairVector;

// Unit choices for main unit mix and tech target.
// This deliberately omits support units like queens and defilers.
enum class TechUnit : int
{
	None
	, Zerglings
	, Hydralisks
	, Lurkers
	, Mutalisks
	, Scourge
	, Guardians
	, Devourers
	, Ultralisks
	, Size
};

struct Strategy
{
	std::string _name;
	BWAPI::Race _race;
	int         _wins;
	int         _losses;
	int         _games;
	double      _winRate;
	int         _sameWins;
	int         _sameGames;
	int         _otherWins;
	int         _otherGames;
	double      _weightedWins;
	double      _weightedGames;
	double      _weightedWinrate;
	std::string _openingGroup;
	BuildOrder  _buildOrder;

	Strategy()
		: _name("None")
		, _race(BWAPI::Races::None)
		, _wins(0)
		, _losses(0)
		, _games(0)
		, _winRate(0.0)
		, _sameWins(0)
		, _sameGames(0)
		, _otherWins(0)
		, _otherGames(0)
		, _weightedWins(0.0)
		, _weightedGames(0.0)
		, _weightedWinrate(0.0)
		, _openingGroup("")
	{
	}

	Strategy(const std::string & name, const BWAPI::Race & race, const std::string & openingGroup, const BuildOrder & buildOrder)
		: _name(name)
		, _race(race)
		, _wins(0)
		, _losses(0)
		, _games(0)
		, _winRate(0.0)
		, _sameWins(0)
		, _sameGames(0)
		, _otherWins(0)
		, _otherGames(0)
		, _weightedWins(0.0)
		, _weightedGames(0.0)
		, _weightedWinrate(0.0)
		, _openingGroup(openingGroup)
		, _buildOrder(buildOrder)
	{
	}
};

struct HistoryRecord
{
	// Each history record is labeled with a version number, to allow some backward compatibility file format changes.
	// Set the version number to the last Microwave release which changed the file format.
	std::string	fileFormatVersion;

	long int			time;
	int					mapStartLocations;
	std::string			mapName;

	std::string			myVersion;
	std::string			myName;
	BWAPI::Race			myRace;

	std::string			enemyName;
	BWAPI::Race			enemyRace;
	bool				enemyIsRandom;

	bool				isWinner;
	std::string			myStrategyName;
	OpeningPlan			recognizedEnemyPlan;
	OpeningPlan			expectedEnemyPlan;

	int					frameGameEnds;
	int					frameGasSteal;					// 0 if we didn't try to steal gas
	bool				gasStealHappened;				// true if a refinery building was started
	int					frameEnemyScoutsOurBase;

	HistoryRecord()
		: fileFormatVersion("v2.7")
		, time(static_cast<long>(std::time(nullptr)))
		, mapStartLocations(BWAPI::Broodwar->getStartLocations().size())
		, mapName("")
		, myVersion(Config::BotInfo::Version)
		, myName("")
		, myRace(BWAPI::Races::Unknown)
		, enemyName("")
		, enemyRace(BWAPI::Races::Unknown)
		, enemyIsRandom(false)
		, isWinner(false)
		, myStrategyName("")
		, recognizedEnemyPlan(OpeningPlan::Unknown)
		, expectedEnemyPlan(OpeningPlan::Unknown)
		, frameGameEnds(0)
	{
	}
};

class StrategyManager 
{
	StrategyManager();

	// Values that cannot or should not be exceeded.
	const int						absoluteMaxSupply = 400;
	const int						absoluteMaxDrones = 80;

	BWAPI::Player					_self;
	BWAPI::Player					_enemy;
	BWAPI::Race					    _selfRace;
	BWAPI::Race					    _enemyRace;
	bool							_enemyIsRandom;
	
	// OpponentModel
	OpponentPlan					_recognizedEnemyPlan;
	OpeningPlan						_expectedEnemyPlan;
	OpeningPlan						_initialExpectedEnemyPlan;
	std::string						_recommendedOpening;
	bool							_singleStrategy;

	std::map<std::string, Strategy> _strategies;
	std::map<std::string, int>		_strategiesMatchup;
	std::vector<HistoryRecord>		_historyRecords;
	HistoryRecord					_currentRecord;
    int                             _totalGamesPlayed;
    const BuildOrder                _emptyBuildOrder;
	int                             _lingScore; 
	int                             _hydraScore;
	int								_mutaScore;
	int								_lurkerScore;
	int								_ultraScore;
	int								_guardianScore;
	int								_devourerScore;
	std::string						_techTarget;
	std::string						_minUnit;
	std::string						_gasUnit;
	BuildOrder						_buildOrderZerg;
	double							_droneEco;
	int								_droneMax;
	bool							_hasIslandBases;
	bool							_isWinner = false;

	// For choosing the tech target and the unit mix.
	std::array<int, int(TechUnit::Size)> techScores;

	        void	                writeResults();
			void	                writeHistory();
			void	                writeResultsFromHistory();
			void					resetStrategies();
	const	int					    getScore(BWAPI::Player player) const;
	const	double				    getUCBValue(const size_t & strategy) const;
	const	bool				    shouldExpandNow() const;
	const	MetaPairVector		    getProtossBuildOrderGoal() const;
	const	MetaPairVector		    getTerranBuildOrderGoal() const;

			bool					isBeingBuilt(BWAPI::UnitType unitType);

			int						getLingScore();
			int						getHydraScore();
			int						getMutaScore();
			int						getLurkerScore();
			int						getUltraScore();
			int						getGuardianScore();
			int						getDevourerScore();

			int						updateLingScore();
			int						updateHydraScore();
			int						updateMutaScore();
			int						updateLurkerScore();
			int						updateUltraScore();
			int						updateGuardianScore();

			BWAPI::Race				ReadRace(std::string str);
			std::string				WriteRace(BWAPI::Race race);

			// OpponentModel
			void					reconsiderEnemyPlan();
			bool					unitTypeArrivesBy(BWAPI::UnitType type, int frame, int count);

			OpeningPlan				getRecognizedEnemyPlan() const;
			OpeningPlan				getExpectedEnemyPlan() const;
			std::string				getRecognizedEnemyPlanString() const;
			std::string				getExpectedEnemyPlanString() const;

			std::string				getOpeningForEnemyPlan(OpeningPlan enemyPlan);

			bool					sameMatchup(const HistoryRecord & record) const;

public:
    
	static	StrategyManager &	    Instance();

			void					update();
			void				    onEnd(const bool isWinner);
            void                    addStrategy(const std::string & name, const Strategy & strategy);
			void                    addStrategyMatchup(const std::string & name, int weight);
			void	                setBestRandomStrategy();
			void	                setRandomStrategy();
			void	                setExplorerStrategy();
            void                    setLearnedStrategy();
			void                    setLearnedStrategyFromHistory();
            void	                readResults();
			void	                readHistory();
			void	                readResultsFromHistory();
			void					printInfoOnStart();

	const	MetaPairVector		    getBuildOrderGoal();
	const	BuildOrder &            getOpeningBookBuildOrder() const;

	// OpponentModel
	const	std::string &			getRecommendedOpening() const { return _recommendedOpening; };
			OpeningPlan				predictEnemyPlan() const;
			void					considerSingleStrategy();
			void					setInitialExpectedEnemyPlan();
			void                    setLearnedStrategyFromEnemyPlan();
			std::map<std::string, double> getStrategyWeightFactors() const;
			std::map<std::string, double> getStrategyAgingFactors() const;
			double					getAgingWinrate() const;

			void					getZergBuildOrder(BuildOrderQueue & queue);
			void					getTerranBuildOrder(BuildOrderQueue & queue);
			void					getProtossBuildOrder(BuildOrderQueue & queue);
			void					handleUrgentProductionIssues(BuildOrderQueue & queue);
			bool					detectSupplyBlock(BuildOrderQueue & queue);
			void					drawStrategyInformation(int x, int y);
			int						maxWorkers();
			int						minWorkers();
			double					droneEco();
			bool					enoughArmy() const;
			void					chooseGasUnit();
			void					chooseMinUnit();
			void					chooseTechTarget();

			std::string techTargetToString(TechUnit target);
			TechUnit techTargetToTextUnit(std::string target);
			void setAvailableTechUnits(std::array<bool, int(TechUnit::Size)> & available);
			int techTier(TechUnit techUnit) const;
};

}
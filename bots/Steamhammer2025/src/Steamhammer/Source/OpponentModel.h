#pragma once

#include "Common.h"
#include "GameRecordNow.h"
#include "OpponentPlan.h"

namespace UAlbertaBot
{
    class OpponentModel
    {
    public:

		// The "eval" here is the opening evaluation stored in the game record.
		// It is a probability-to-win estimate made when the opening build finishes.
		// "Does this look like a good build so far?"

        // Data structure used locally in deciding on the opening.
		// It's info about a single opening.
		// It's used for two purposes; see OpponentSummary just below.
        struct OpeningInfoType
        {
            int sameWins;		// on the same map as this game, or following the same plan as this game
            int sameGames;
            int otherWins;		// across all other maps/plans
            int otherGames;
            double weightedWins;
            double weightedGames;

			int evalGames;		// count of games for which an evaluation is given
			double totalEval;	// sum of all evaluations, for calculating the mean

            OpeningInfoType()
                : sameWins(0)
                , sameGames(0)
                , otherWins(0)
                , otherGames(0)
				, evalGames(0)
				, totalEval(0.0)
                // The weighted values don't need to be initialized up front.
            {
            }

			double getMeanEval() const { return evalGames ? (totalEval / evalGames) : -1.0; };
		};

		// Overall info about openings versus this opponent (i.e., in this set of game records).
        struct OpponentSummary
        {
            int totalWins;
            int totalGames;
			int totalEvalGames;		// count of games for which an evaluation is given
			double totalEval;		// sum of all evaluations, for calculating the mean
            std::map<std::string, OpeningInfoType> openingInfo;		// opening name -> opening info
            OpeningInfoType planInfo;								// summary of the recorded enemy plans

            OpponentSummary()
                : totalWins(0)
                , totalGames(0)
            {
			}

			double getMeanEval() const { return totalEvalGames ? (totalEval / totalEvalGames) : -1.0; };
        };

    private:

        OpponentPlan _planRecognizer;

        std::string _filename;
        GameRecordNow _gameRecord;                      // the current game
        std::vector<GameRecord *> _pastGameRecords;     // from learning files

        GameRecord * _bestMatch;				// not currently usable

        // Advice for the rest of the bot.
        OpponentSummary _summary;
        bool _singleStrategy;					// enemy seems to always do the same thing, false until proven true
        OpeningPlan _initialExpectedEnemyPlan;  // first predicted enemy plan, before play starts
        OpeningPlan _expectedEnemyPlan;		    // in-game predicted enemy plan
        // NOTE There is also an actual recognized enemy plan. It is kept in _planRecognizer.getPlan().
        std::string _recommendedOpening;

        OpeningPlan predictEnemyPlan() const;

        void considerSingleStrategy();
        void considerOpenings();
        void singleStrategyEnemyOpenings();
        void multipleStrategyEnemyOpenings();
        double weightedWinRate(double weightedWins, double weightedGames) const;
        void reconsiderEnemyPlan();
        void setBestMatch();

        std::string getExploreOpening(const OpponentSummary & opponentSummary);
        std::string getOpeningForEnemyPlan(OpeningPlan enemyPlan);

    public:
        OpponentModel();

        void setOpening() { _gameRecord.setOpening(Config::Strategy::StrategyName); };
		void setOpeningScore(double score) { _gameRecord.setOpeningScore(score); };
		void setWin(bool isWinner) { _gameRecord.setWin(isWinner); };

        void read();
        void write();

        void update();

        void predictEnemy(int lookaheadFrames, PlayerSnapshot & snap) const;

        const std::vector<GameRecord *> & getRecords() const { return _pastGameRecords; };
        const OpponentSummary & getSummary() const { return _summary; };
        bool        sameMatchup(const GameRecord & record) const;
		int			nGamesSameMatchup() const;

        bool		isEnemySingleStrategy() const { return _singleStrategy; };
        OpeningPlan getEnemyPlan() const;
        std::string getEnemyPlanString() const;
        OpeningPlan getInitialExpectedEnemyPlan() const { return _initialExpectedEnemyPlan; };
        OpeningPlan getExpectedEnemyPlan() const { return _expectedEnemyPlan; };
        std::string getExpectedEnemyPlanString() const;
        OpeningPlan getBestGuessEnemyPlan() const;
        OpeningPlan getDarnLikelyEnemyPlan() const;

        const std::string & getRecommendedOpening() const { return _recommendedOpening; };

        static OpponentModel & Instance();
    };

}
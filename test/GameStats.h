#pragma once

#include <BWAPI.h>
#include "BW/BWData.h"

#include <array>
#include <map>
#include <optional>
#include <string>

// What each player has lost so far, counted from the engine's kill events. OpenBW doesn't keep BW's score counters
// (Player::allUnitsKilled(), allUnitsLost() and allBuildingsLost() are always 0), so the harness counts them itself.
// Units and buildings are counted apart, like BW's own score: "units" never includes buildings.
struct Losses
{
    std::array<int, 12> units{};      // by owner
    std::array<int, 12> buildings{};  // by owner

    // Counts a unit the engine has just killed, if it is a real loss for its player
    void count(BW::Unit unit);
};

// One player's numbers, read from the engine's own state (so both sides are complete, unlike what a bot can see)
struct PlayerStats
{
    std::string name;
    BWAPI::Race race;
    int minerals = 0;
    int gas = 0;
    int supplyUsed = 0;  // in StarCraft's displayed units (BW counts half-supply)
    int supplyMax = 0;
    int workers = 0;
    int mineralsGathered = 0;
    int gasGathered = 0;
    int unitsKilled = 0;
    int unitsLost = 0;
    int buildingsLost = 0;

    // Reads the player whose in-game (character) name is characterName; name is what to call it.
    // Kills and losses come from losses: a player is credited with every unit the other players lost.
    static std::optional<PlayerStats> read(BW::Game game, const std::string &characterName, const std::string &name,
                                           const Losses &losses);
};

// Elo ratings written by tools/elo.py to replays/ratings.json, by player name; empty if there are none yet
std::map<std::string, double> ReadRatings();

// Draws the stats screen in the top-left corner of the game window
void DrawStatsScreen(BWAPI::Game *game, const PlayerStats &me, const PlayerStats &opponent,
                     const std::map<std::string, double> &ratings);

// One-line summary of a finished game, for the test output
std::string StatsSummary(const PlayerStats &me, const PlayerStats &opponent);

// Appends a finished game to replays/results.csv (the history tools/elo.py rates). result is WON, LOST or DRAW.
void AppendResult(const PlayerStats &me, const PlayerStats &opponent, const std::string &result, int frames,
                  const std::string &mapName, int seed, const std::string &replayFile);

#pragma once

#include <BWAPI.h>
#include "BW/BWData.h"

#include <array>
#include <chrono>
#include <cstdio>
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
    int armySupply = 0;  // supply used by everything but workers
    int mineralsGathered = 0;
    int gasGathered = 0;
    int unitsKilled = 0;
    int unitsLost = 0;
    int buildingsLost = 0;
    int units = 0;      // everything but buildings, made or in production (larvae aside)
    int buildings = 0;  // finished or not
    int value = 0;      // what all of them cost, minerals + gas

    // Reads the player whose in-game (character) name is characterName; name is what to call it.
    // Kills and losses come from losses: a player is credited with every unit the other players lost.
    static std::optional<PlayerStats> read(BW::Game game, const std::string &characterName, const std::string &name,
                                           const Losses &losses);
};

// The result of a game stopped at the frame or time limit, judged instead of calling it a draw: a player down to one
// building and one unit (or less) has lost; otherwise the player whose units and buildings are worth a quarter more
// (and at least 500 more) has won; otherwise it is a draw. Returns WON, LOST or DRAW for `me`, and why in `why`.
std::string JudgeAtLimit(const PlayerStats &me, const PlayerStats &opponent, std::string &why);

// Every unit type the player has (made or in production) with its count, e.g. " Opponent: Zerg_Drone=9 Zerg_Zergling=6"
std::string ObserveUnitCounts(BW::Game game, const std::string &characterName, const std::string &name);

// Elo ratings written by tools/elo.py to replays/ratings.json, by player name; empty if there are none yet
std::map<std::string, double> ReadRatings();

// Draws the stats screen in the top-left corner of the game window
void DrawStatsScreen(BWAPI::Game *game, const PlayerStats &me, const PlayerStats &opponent,
                     const std::map<std::string, double> &ratings);

// Draws a one-line toolbar along the top of the game window: each player's army supply (and the most it has had),
// minerals and gas right now
void DrawToolbar(BWAPI::Game *game, const PlayerStats &me, const PlayerStats &opponent);

// Writes the game's numbers to a small JSON file about twice a second, for tools/live_stats.py to show while games
// run (headless or with a window). STARDUST_LIVE_FILE names the file: live.json in the working folder by default,
// 0 to turn it off. The file is replaced in one step, so a reader never sees half of it.
class LiveStats
{
public:
    LiveStats(int game, const std::string &mapName, int seed, int frameLimit);

    // Writes the current numbers, if half a second has passed since the last write
    void update(BW::Game game, int frame, const PlayerStats &me, const PlayerStats &opponent);

    // Writes the last numbers again with the result: WON, LOST or DRAW, and how it was decided when the game was
    // stopped at a limit
    void finish(int frame, const std::string &result, const std::string &decided = "");

    // While the user has paused the game: rewrites the numbers now and then, marked as paused, so the file doesn't
    // look stale. Called with false when the game goes on.
    void keepAlive(bool paused);

private:
    void write();

    std::string path;
    std::string document;  // the JSON last written, without its closing brace, so finish() can add to it
    int game;
    std::string mapName;
    int seed;
    int frameLimit;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point lastWrite;
    int current = 0;
    std::string result;
    std::string decided;
    bool paused = false;
};

// Writes what the game window's observer camera saw (fights and their first hits, sneak attacks, drops, spells,
// expansions, army moves, camera cuts) to a JSON-lines file, one event per line after a game_start line, for
// commentary and tools. STARDUST_EVENTS_FILE names the file: events.jsonl in the working folder by default, 0 to turn
// it off. Headless games have no observer camera, so they write no file.
class GameEvents
{
public:
    GameEvents(bool enabled, BW::Game game, int gameNumber, const std::string &mapName, int seed,
               const std::string &myName, const std::string &opponentName);
    ~GameEvents();
    GameEvents(const GameEvents &) = delete;
    GameEvents &operator=(const GameEvents &) = delete;

    void write(const BW::CameraEvent &event);

    // An event of the harness's own (pause, resume)
    void write(const std::string &kind, int frame);

private:
    std::string playerName(int player);
    void writeLine(const std::string &line);

    std::string path;
    std::FILE *file = nullptr;
    BW::Game game;
    int gameNumber;
    std::string mapName;
    int seed;
    std::string myName;
    std::string opponentName;
};

// One-line summary of a finished game, for the test output
std::string StatsSummary(const PlayerStats &me, const PlayerStats &opponent);

// Appends a finished game to replays/results.csv (the history tools/elo.py rates). result is WON, LOST or DRAW.
void AppendResult(const PlayerStats &me, const PlayerStats &opponent, const std::string &result, int frames,
                  const std::string &mapName, int seed, const std::string &replayFile);

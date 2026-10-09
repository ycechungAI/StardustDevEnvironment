#include "GameStats.h"

#include "BW/UnitStatusFlags.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unistd.h>

namespace
{
    const char *ResultsFile = "replays/results.csv";
    const char *RatingsFile = "replays/ratings.json";

    std::string gameTime(int frames)
    {
        // 24 frames per game second at "fastest" speed
        int seconds = frames / 24;
        std::ostringstream out;
        out << seconds / 60 << ":" << std::setw(2) << std::setfill('0') << seconds % 60;
        return out.str();
    }

    std::string rating(const std::map<std::string, double> &ratings, const std::string &name)
    {
        auto it = ratings.find(name);
        if (it == ratings.end()) return "1500 (new)";
        return std::to_string((int) std::lround(it->second));
    }

    // CSV fields are names and numbers; quote anything with a comma or quote just in case
    std::string csvField(const std::string &value)
    {
        if (value.find_first_of(",\"") == std::string::npos) return value;
        std::string quoted = "\"";
        for (char c : value)
        {
            if (c == '"') quoted += '"';
            quoted += c;
        }
        return quoted + "\"";
    }
}

void Losses::count(BW::Unit unit)
{
    // Only the players' own units: not neutral minerals, geysers or critters (nor units of a player who has left,
    // which the engine makes neutral)
    int owner = unit.playerID();
    if (owner < 0 || owner >= 8) return;

    BWAPI::UnitType type(unit.unitType());
    if (type.isNeutral() || type.isSpell()) return;

    // Not real units: a reaver's scarabs, a carrier's interceptors, spider mines and nukes are spent as ammunition,
    // broodlings die when their time runs out, and hallucinations are fake
    if (type == BWAPI::UnitTypes::Protoss_Scarab || type == BWAPI::UnitTypes::Protoss_Interceptor
        || type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine || type == BWAPI::UnitTypes::Terran_Nuclear_Missile
        || type == BWAPI::UnitTypes::Zerg_Broodling)
    {
        return;
    }
    if (unit.statusFlag(BW::StatusFlags::IsHallucination)) return;

    // The engine also "kills" something cancelled while it is being built or morphed; a unit killed by damage has 0 hp
    if (!unit.statusFlag(BW::StatusFlags::Completed) && unit.hitPoints() > 0) return;

    if (type.isBuilding())
    {
        buildings[owner]++;
    }
    else
    {
        units[owner]++;
    }
}

std::optional<PlayerStats> PlayerStats::read(BW::Game game, const std::string &characterName, const std::string &name,
                                             const Losses &losses)
{
    for (int owner = 0; owner < 12; owner++)
    {
        auto player = game.getPlayer(owner);
        if (characterName != player.szName()) continue;

        PlayerStats stats;
        stats.name = name;
        stats.race = BWAPI::Race(player.nRace());
        int race = player.nRace();
        if (race < 0 || race > 2) race = 2;
        stats.minerals = player.minerals();
        stats.gas = player.gas();
        stats.supplyUsed = player.suppliesUsed(race) / 2;
        stats.supplyMax = std::min(200, player.suppliesAvailable(race) / 2);
        stats.workers = player.unitCountsAll(BWAPI::UnitTypes::Terran_SCV.getID())
                        + player.unitCountsAll(BWAPI::UnitTypes::Protoss_Probe.getID())
                        + player.unitCountsAll(BWAPI::UnitTypes::Zerg_Drone.getID());
        stats.armySupply = std::max(0, stats.supplyUsed - stats.workers);
        stats.mineralsGathered = player.cumulativeMinerals();
        stats.gasGathered = player.cumulativeGas();
        stats.unitsLost = losses.units[owner];
        stats.buildingsLost = losses.buildings[owner];
        for (int other = 0; other < 8; other++)
        {
            if (other != owner) stats.unitsKilled += losses.units[other];
        }
        return stats;
    }
    return std::nullopt;
}

std::string ObserveUnitCounts(BW::Game game, const std::string &characterName, const std::string &name)
{
    for (int owner = 0; owner < 12; owner++)
    {
        auto player = game.getPlayer(owner);
        if (characterName != player.szName()) continue;

        std::ostringstream out;
        out << " " << name << ":";
        for (auto type : BWAPI::UnitTypes::allUnitTypes())
        {
            if (type.getID() >= 228) continue;
            int count = player.unitCountsAll(type.getID());
            if (count > 0) out << " " << type.getName() << "=" << count;
        }
        return out.str();
    }
    return "";
}

std::map<std::string, double> ReadRatings()
{
    std::map<std::string, double> ratings;
    std::ifstream file(RatingsFile);
    if (!file.good()) return ratings;
    try
    {
        auto json = nlohmann::json::parse(file);
        auto &table = json.at("ratings");
        for (auto it = table.begin(); it != table.end(); ++it)
        {
            ratings[it.key()] = it.value().get<double>();
        }
    }
    catch (std::exception &)
    {
        // A half-written or hand-edited file: show no ratings rather than stop the game
    }
    return ratings;
}

void DrawStatsScreen(BWAPI::Game *game, const PlayerStats &me, const PlayerStats &opponent,
                     const std::map<std::string, double> &ratings)
{
    const int left = 8, top = 26, columnMe = 110, columnOpponent = 220, lineHeight = 11;
    std::vector<std::tuple<std::string, std::string, std::string>> rows = {
            {"Elo", rating(ratings, me.name), rating(ratings, opponent.name)},
            {"Race", me.race.getName(), opponent.race.getName()},
            {"Minerals", std::to_string(me.minerals), std::to_string(opponent.minerals)},
            {"Gas", std::to_string(me.gas), std::to_string(opponent.gas)},
            {"Supply", std::to_string(me.supplyUsed) + "/" + std::to_string(me.supplyMax),
             std::to_string(opponent.supplyUsed) + "/" + std::to_string(opponent.supplyMax)},
            {"Workers", std::to_string(me.workers), std::to_string(opponent.workers)},
            {"Mined (min/gas)", std::to_string(me.mineralsGathered) + "/" + std::to_string(me.gasGathered),
             std::to_string(opponent.mineralsGathered) + "/" + std::to_string(opponent.gasGathered)},
            {"Units killed", std::to_string(me.unitsKilled), std::to_string(opponent.unitsKilled)},
            {"Units lost", std::to_string(me.unitsLost), std::to_string(opponent.unitsLost)},
            {"Buildings lost", std::to_string(me.buildingsLost), std::to_string(opponent.buildingsLost)},
    };

    int bottom = top + lineHeight * (int) (rows.size() + 3) + 4;
    game->drawBoxScreen(left - 4, top - 4, left + 330, bottom, BWAPI::Colors::Black, true);
    game->drawBoxScreen(left - 4, top - 4, left + 330, bottom, BWAPI::Colors::Grey, false);

    game->drawTextScreen(left, top, "%c%s  %c[s] hide stats  [r] save replay", BWAPI::Text::White,
                         gameTime(game->getFrameCount()).c_str(), BWAPI::Text::Grey);
    int y = top + lineHeight + 2;
    game->drawTextScreen(left + columnMe, y, "%c%s", BWAPI::Text::Green, me.name.c_str());
    game->drawTextScreen(left + columnOpponent, y, "%c%s", BWAPI::Text::Red, opponent.name.c_str());
    for (auto &[label, mine, theirs] : rows)
    {
        y += lineHeight;
        game->drawTextScreen(left, y, "%c%s", BWAPI::Text::Grey, label.c_str());
        game->drawTextScreen(left + columnMe, y, "%c%s", BWAPI::Text::White, mine.c_str());
        game->drawTextScreen(left + columnOpponent, y, "%c%s", BWAPI::Text::White, theirs.c_str());
    }
}

void DrawToolbar(BWAPI::Game *game, const PlayerStats &me, const PlayerStats &opponent)
{
    // The largest army each side has had this game
    static std::map<std::string, int> maxArmy;
    int &myMax = maxArmy[me.name];
    int &theirMax = maxArmy[opponent.name];
    myMax = std::max(myMax, me.armySupply);
    theirMax = std::max(theirMax, opponent.armySupply);

    game->drawBoxScreen(0, 0, 640, 15, BWAPI::Colors::Black, true);
    game->drawBoxScreen(0, 0, 640, 15, BWAPI::Colors::Grey, false);
    auto side = [&](int x, char colour, const PlayerStats &stats, int most)
    {
        game->drawTextScreen(x, 2, "%c%s  %cArmy %c%d %c(max %d)  %cMin %c%d  %cGas %c%d", colour, stats.name.c_str(),
                             BWAPI::Text::Grey, BWAPI::Text::White, stats.armySupply, BWAPI::Text::Grey, most,
                             BWAPI::Text::Grey, BWAPI::Text::White, stats.minerals, BWAPI::Text::Grey,
                             BWAPI::Text::White, stats.gas);
    };
    side(6, BWAPI::Text::Green, me, myMax);
    side(326, BWAPI::Text::Red, opponent, theirMax);
}

std::string StatsSummary(const PlayerStats &me, const PlayerStats &opponent)
{
    std::ostringstream out;
    out << me.name << " vs " << opponent.name
        << " | mined " << me.mineralsGathered << "/" << me.gasGathered << " vs " << opponent.mineralsGathered << "/"
        << opponent.gasGathered
        << " | killed " << me.unitsKilled << " vs " << opponent.unitsKilled
        << " | lost " << me.unitsLost << " vs " << opponent.unitsLost
        << " | supply " << me.supplyUsed << " vs " << opponent.supplyUsed;
    return out.str();
}

void AppendResult(const PlayerStats &me, const PlayerStats &opponent, const std::string &result, int frames,
                  const std::string &mapName, int seed, const std::string &replayFile)
{
    std::filesystem::create_directories("replays");
    bool newFile = !std::filesystem::exists(ResultsFile);

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::ostringstream line;
    if (newFile)
    {
        line << "time,player,opponent,result,frames,map,seed,player_race,opponent_race,"
                "player_minerals_mined,player_gas_mined,player_units_killed,player_units_lost,"
                "opponent_minerals_mined,opponent_gas_mined,opponent_units_killed,opponent_units_lost,replay\n";
    }
    line << std::put_time(std::localtime(&now), "%Y-%m-%dT%H:%M:%S") << ","
         << csvField(me.name) << "," << csvField(opponent.name) << "," << result << "," << frames << ","
         << csvField(mapName) << "," << seed << "," << me.race.getName() << "," << opponent.race.getName() << ","
         << me.mineralsGathered << "," << me.gasGathered << "," << me.unitsKilled << "," << me.unitsLost << ","
         << opponent.mineralsGathered << "," << opponent.gasGathered << "," << opponent.unitsKilled << ","
         << opponent.unitsLost << "," << csvField(replayFile) << "\n";

    // One write call per game, so lines from games running in parallel don't interleave
    std::string text = line.str();
    if (auto file = std::fopen(ResultsFile, "a"))
    {
        std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
    }
}

LiveStats::LiveStats(int game, const std::string &mapName, int seed, int frameLimit)
        : game(game), mapName(mapName), seed(seed), frameLimit(frameLimit), started(std::chrono::steady_clock::now())
{
    auto file = std::getenv("STARDUST_LIVE_FILE");
    if (!file || !*file) path = "live.json";
    else if (std::string(file) != "0") path = file;
}

void LiveStats::update(BW::Game game, int frame, const PlayerStats &me, const PlayerStats &opponent)
{
    if (path.empty()) return;
    auto now = std::chrono::steady_clock::now();
    if (now - lastWrite < std::chrono::milliseconds(500)) return;
    lastWrite = now;

    auto player = [&](const PlayerStats &stats, const char *characterName)
    {
        nlohmann::json out = {
                {"name",             stats.name},
                {"race",             stats.race.getName()},
                {"minerals",         stats.minerals},
                {"gas",              stats.gas},
                {"supplyUsed",       stats.supplyUsed},
                {"supplyMax",        stats.supplyMax},
                {"workers",          stats.workers},
                {"armySupply",       stats.armySupply},
                {"mineralsGathered", stats.mineralsGathered},
                {"gasGathered",      stats.gasGathered},
                {"unitsKilled",      stats.unitsKilled},
                {"unitsLost",        stats.unitsLost},
                {"buildingsLost",    stats.buildingsLost},
        };
        // Every unit type the player has (made or in production), and the totals of units and buildings
        int units = 0;
        int buildings = 0;
        nlohmann::json types = nlohmann::json::object();
        for (int owner = 0; owner < 12; owner++)
        {
            auto bwPlayer = game.getPlayer(owner);
            if (characterName != std::string(bwPlayer.szName())) continue;
            for (auto type : BWAPI::UnitTypes::allUnitTypes())
            {
                if (type.getID() >= 228) continue;
                int count = bwPlayer.unitCountsAll(type.getID());
                if (count <= 0) continue;
                types[type.getName()] = count;
                (type.isBuilding() ? buildings : units) += count;
            }
            break;
        }
        out["units"] = units;
        out["buildings"] = buildings;
        out["unitTypes"] = types;
        return out;
    };

    nlohmann::json doc = {
            {"pid",         (int) getpid()},
            {"game",        this->game},
            {"map",         mapName},
            {"seed",        seed},
            {"frameLimit",  frameLimit},
            {"window",      std::get<0>(game.GameScreenBuffer()) > 0},
            {"players",     {player(me, "Tests"), player(opponent, "Opponent")}},
    };
    document = doc.dump();
    document.pop_back();  // the closing brace
    current = frame;
    write();
}

void LiveStats::finish(int frame, const std::string &result)
{
    if (path.empty() || document.empty()) return;
    current = frame;
    this->result = result;
    write();
}

void LiveStats::keepAlive(bool paused)
{
    if (path.empty() || document.empty()) return;
    bool changed = paused != this->paused;
    this->paused = paused;
    auto now = std::chrono::steady_clock::now();
    if (!changed && now - lastWrite < std::chrono::milliseconds(500)) return;
    lastWrite = now;
    write();
}

void LiveStats::write()
{
    auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::ostringstream text;
    text << document << ",\"frame\":" << current << ",\"wallSeconds\":" << std::fixed << std::setprecision(1)
         << seconds << ",\"state\":\"" << (!result.empty() ? "over" : paused ? "paused" : "playing")
         << "\",\"result\":\"" << result
         << "\",\"updated\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
    auto temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file.good()) return;
        file << text.str();
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
}

GameEvents::GameEvents(bool enabled, BW::Game game, int gameNumber, const std::string &mapName, int seed,
                       const std::string &myName, const std::string &opponentName)
        : game(game), gameNumber(gameNumber), mapName(mapName), seed(seed), myName(myName), opponentName(opponentName)
{
    if (!enabled) return;
    auto file = std::getenv("STARDUST_EVENTS_FILE");
    if (!file || !*file) path = "events.jsonl";
    else if (std::string(file) != "0") path = file;
    // The file is made at the first event; one left from an earlier game would be mistaken for this one's
    if (!path.empty()) std::remove(path.c_str());
}

GameEvents::~GameEvents()
{
    if (file) std::fclose(file);
}

std::string GameEvents::playerName(int player)
{
    if (player < 0 || player >= 12) return "";
    std::string name = game.getPlayer(player).szName();
    if (name == "Tests") return myName;
    if (name == "Opponent") return opponentName;
    return name;
}

void GameEvents::writeLine(const std::string &line)
{
    if (path.empty()) return;
    if (!file)
    {
        file = std::fopen(path.c_str(), "w");
        if (!file)
        {
            path.clear();
            return;
        }
        nlohmann::json players = nlohmann::json::object();
        for (int i = 0; i < 8; i++)
        {
            auto player = game.getPlayer(i);
            std::string name = player.szName();
            if (name != "Tests" && name != "Opponent") continue;
            players[std::to_string(i)] = {{"name", playerName(i)}, {"race", BWAPI::Race(player.nRace()).getName()}};
        }
        nlohmann::json start = {{"kind", "game_start"}, {"game", gameNumber}, {"map", mapName}, {"seed", seed},
                                {"players", players}};
        auto text = start.dump() + "\n";
        std::fwrite(text.data(), 1, text.size(), file);
    }
    std::fwrite(line.data(), 1, line.size(), file);
    std::fflush(file);
}

void GameEvents::write(const BW::CameraEvent &event)
{
    if (path.empty()) return;
    nlohmann::json out = {
            {"frame",   event.frame},
            {"seconds", std::round(event.frame * 0.42) / 10},
            {"kind",    event.kind},
            {"x",       event.x},
            {"y",       event.y},
    };
    if (event.player >= 0) out["player"] = playerName(event.player);
    if (event.target >= 0) out["target"] = playerName(event.target);
    if (event.id >= 0) out["id"] = event.id;
    if (event.score != 0) out["score"] = event.score;
    auto units = [&](const std::vector<std::array<int, 3>> &list)
    {
        nlohmann::json byPlayer = nlohmann::json::object();
        for (auto &[player, type, count] : list)
        {
            byPlayer[playerName(player)][BWAPI::UnitType(type).getName()] = count;
        }
        return byPlayer;
    };
    if (!event.units.empty()) out["units"] = units(event.units);
    if (!event.lost.empty()) out["lost"] = units(event.lost);
    for (auto &[name, value] : event.values) out[name] = value;
    for (auto &[name, text] : event.texts) out[name] = text;
    writeLine(out.dump() + "\n");
}

void GameEvents::write(const std::string &kind, int frame)
{
    if (path.empty()) return;
    nlohmann::json out = {{"frame", frame}, {"seconds", std::round(frame * 0.42) / 10}, {"kind", kind}};
    writeLine(out.dump() + "\n");
}

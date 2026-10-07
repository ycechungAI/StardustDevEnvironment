#include "GameStats.h"

#include "BW/UnitStatusFlags.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

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
    const int left = 8, top = 8, columnMe = 110, columnOpponent = 220, lineHeight = 11;
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

// Plays a replay headlessly in OpenBW and prints what happened in it as JSON lines, for tools/replays.py to summarise.
//
//   replay_dump <replay.rep> [--data <directory with the MPQs>]
//
// Lines, each a JSON object with "t" giving its kind:
//   meta     the map, length and players (name, race, start location)
//   new      a unit or building appeared (started building, or a second zergling from an egg)
//   morph    a unit changed type (larva to egg, egg to unit, drone to building, building upgrades like lair)
//   done     a unit or building finished (first seen completed under its current type)
//   dead     a unit or building was killed, with the player that last attacked it
//   upgrade  an upgrade level or tech finished
//   snap     once a second: each player's minerals, gas, mined totals, supply, workers, army, and the units it has
//            near each enemy base (counted per enemy player)
//   comp     every 10 seconds: each player's completed units by type
//   end      the result: players eliminated or victorious, and the last frame

#include "bwgame.h"
#include "actions.h"
#include "replay.h"

#include <BWAPI/UnitType.h>
#include <BWAPI/UpgradeType.h>
#include <BWAPI/TechType.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace bwgame::ui
{
    void log_str(a_string str)
    {
        fwrite(str.data(), str.size(), 1, stderr);
    }

    void fatal_error_str(a_string str)
    {
        fprintf(stderr, "fatal error: %s\n", str.c_str());
        std::exit(1);
    }
}

using namespace bwgame;

namespace
{
    std::string json(const std::string &s)
    {
        std::string out = "\"";
        for (unsigned char c : s)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
                out += (char) c;
            }
            else if (c < 0x20)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
            {
                out += (char) c;
            }
        }
        return out + "\"";
    }

    template<typename... T>
    std::string sfmt(const char *f, T &&... args)
    {
        auto s = format(f, std::forward<T>(args)...);
        return {s.data(), s.size()};
    }

    std::string typeName(int id)
    {
        return BWAPI::UnitType(id).getName();
    }

    const char *raceName(race_t race)
    {
        switch (race)
        {
            case race_t::zerg: return "Zerg";
            case race_t::terran: return "Terran";
            case race_t::protoss: return "Protoss";
            default: return "None";
        }
    }

    // Types that come and go all game and say nothing about the players' plans
    bool ignored(UnitTypes type)
    {
        return type == UnitTypes::Zerg_Larva || type == UnitTypes::Protoss_Scarab
               || type == UnitTypes::Protoss_Interceptor || type == UnitTypes::Terran_Nuclear_Missile
               || type == UnitTypes::Spell_Scanner_Sweep || type == UnitTypes::Spell_Disruption_Web
               || type == UnitTypes::Spell_Dark_Swarm;
    }

    struct tracked
    {
        UnitTypes type;
        bool done;
    };

    struct dumper : replay_functions
    {
        explicit dumper(state &st, action_state &action_st, replay_state &replay_st)
                : replay_functions(st, action_st, replay_st) {}

        std::map<std::pair<size_t, int>, tracked> units;
        std::vector<std::string> results;

        static std::pair<size_t, int> key(const unit_t *u)
        {
            return {u->index, (int) u->unit_id_generation};
        }

        void on_kill_unit(unit_t *u) override
        {
            if (u->owner > 7 || ignored(u->unit_type->id) || u_hallucination(u)) return;
            // A zergling or scourge costs half of what its egg did
            int split = ut_two_units_in_one_egg(u) ? 2 : 1;
            printf("{\"t\":\"dead\",\"f\":%d,\"p\":%d,\"type\":%s,\"x\":%d,\"y\":%d,\"by\":%d,\"done\":%s,"
                   "\"m\":%d,\"g\":%d}\n",
                   st.current_frame, u->owner, json(typeName((int) u->unit_type->id)).c_str(), u->position.x,
                   u->position.y, u->last_attacking_player, u_completed(u) ? "true" : "false",
                   u->unit_type->mineral_cost / split, u->unit_type->gas_cost / split);
        }

        void on_unit_destroy(unit_t *u) override
        {
            units.erase(key(u));
        }

        void on_player_eliminated(int owner) override
        {
            results.push_back(sfmt("{\"player\":%d,\"eliminated\":%d}", owner, st.current_frame));
        }

        void on_victory_state(int owner, int state) override
        {
            results.push_back(sfmt("{\"player\":%d,\"victory_state\":%d,\"frame\":%d}", owner, state, st.current_frame));
        }

        bool active(int p) const
        {
            auto c = st.players[p].controller;
            return p < 8 && (c == player_t::controller_occupied || c == player_t::controller_computer
                             || c == player_t::controller_computer_game || c == player_t::controller_user_left);
        }

        void scanUnits()
        {
            for (int p = 0; p < 8; p++)
            {
                for (unit_t *u : ptr(st.player_units[p]))
                {
                    auto type = u->unit_type->id;
                    if (ignored(type) || u_hallucination(u)) continue;
                    auto k = key(u);
                    auto it = units.find(k);
                    bool completed = u_completed(u);
                    if (it == units.end())
                    {
                        std::string into;
                        if (!u->build_queue.empty() && u->build_queue.front())
                        {
                            into = ",\"into\":" + json(typeName((int) u->build_queue.front()->id));
                        }
                        printf("{\"t\":\"new\",\"f\":%d,\"p\":%d,\"type\":%s,\"x\":%d,\"y\":%d%s}\n", st.current_frame, p,
                               json(typeName((int) type)).c_str(), u->position.x, u->position.y, into.c_str());
                        it = units.emplace(k, tracked{type, false}).first;
                    }
                    else if (it->second.type != type)
                    {
                        std::string into;
                        if (!u->build_queue.empty() && u->build_queue.front())
                        {
                            into = ",\"into\":" + json(typeName((int) u->build_queue.front()->id));
                        }
                        printf("{\"t\":\"morph\",\"f\":%d,\"p\":%d,\"from\":%s,\"type\":%s,\"x\":%d,\"y\":%d%s}\n",
                               st.current_frame, p, json(typeName((int) it->second.type)).c_str(),
                               json(typeName((int) type)).c_str(), u->position.x, u->position.y, into.c_str());
                        it->second = tracked{type, false};
                    }
                    if (completed && !it->second.done)
                    {
                        printf("{\"t\":\"done\",\"f\":%d,\"p\":%d,\"type\":%s,\"x\":%d,\"y\":%d}\n", st.current_frame, p,
                               json(typeName((int) type)).c_str(), u->position.x, u->position.y);
                        it->second.done = true;
                    }
                }
            }
        }

        std::array<std::array<int, 64>, 8> upgrades{};
        std::array<std::array<bool, 64>, 8> techs{};

        void scanUpgrades()
        {
            for (int p = 0; p < 8; p++)
            {
                if (!active(p)) continue;
                for (int i = 0; i < 61; i++)
                {
                    int level = st.upgrade_levels[p][(UpgradeTypes) i];
                    if (level > upgrades[p][i])
                    {
                        printf("{\"t\":\"upgrade\",\"f\":%d,\"p\":%d,\"name\":%s,\"level\":%d}\n", st.current_frame, p,
                               json(BWAPI::UpgradeType(i).getName()).c_str(), level);
                        upgrades[p][i] = level;
                    }
                }
                for (int i = 0; i < 44; i++)
                {
                    bool researched = st.tech_researched[p][(TechTypes) i];
                    if (researched && !techs[p][i])
                    {
                        // Techs a race starts with (stim needs research, but e.g. Infestation doesn't) show at frame 0
                        if (st.current_frame > 0)
                        {
                            printf("{\"t\":\"upgrade\",\"f\":%d,\"p\":%d,\"name\":%s,\"level\":1}\n", st.current_frame, p,
                                   json(BWAPI::TechType(i).getName()).c_str());
                        }
                        techs[p][i] = true;
                    }
                }
            }
        }

        void snapshot(bool composition)
        {
            // Resource depots, to count who is in whose base
            std::vector<std::pair<int, xy>> depots;
            for (int p = 0; p < 8; p++)
            {
                for (unit_t *u : ptr(st.player_units[p]))
                {
                    if (ut_resource_depot(u)) depots.emplace_back(p, u->position);
                }
            }

            std::string out = sfmt("{\"t\":\"snap\",\"f\":%d,\"players\":[", st.current_frame);
            std::string comp = sfmt("{\"t\":\"comp\",\"f\":%d,\"players\":[", st.current_frame);
            bool first = true;
            for (int p = 0; p < 8; p++)
            {
                if (!active(p)) continue;
                int race = (int) st.players[p].race;
                if (race < 0 || race > 2) race = 0;
                int workers = 0, army = 0, armySupply2 = 0; // supply is counted in halves, for zerglings and scourge
                std::map<std::string, int> types;
                std::map<int, std::pair<int, int>> near; // enemy player -> (army, workers) near its depots
                for (unit_t *u : ptr(st.player_units[p]))
                {
                    if (ignored(u->unit_type->id) || u_hallucination(u)) continue;
                    if (!u_completed(u)) continue;
                    if (composition) types[typeName((int) u->unit_type->id)]++;
                    if (ut_building(u)) continue;
                    bool worker = ut_worker(u);
                    if (worker) workers++;
                    else if (u->unit_type->id != UnitTypes::Zerg_Overlord && u->unit_type->id != UnitTypes::Zerg_Egg
                             && u->unit_type->id != UnitTypes::Zerg_Cocoon && u->unit_type->id != UnitTypes::Zerg_Lurker_Egg)
                    {
                        army++;
                        armySupply2 += u->unit_type->supply_required.raw_value;
                    }
                    for (auto &[owner, pos] : depots)
                    {
                        if (owner == p) continue;
                        if (xy_length(u->position - pos) < 400)
                        {
                            auto &n = near[owner];
                            if (worker) n.second++;
                            else if (u->unit_type->id != UnitTypes::Zerg_Overlord) n.first++;
                            break;
                        }
                    }
                }
                std::string nearStr;
                for (auto &[owner, counts] : near)
                {
                    if (!nearStr.empty()) nearStr += ",";
                    nearStr += sfmt("\"%d\":[%d,%d]", owner, counts.first, counts.second);
                }
                if (!first)
                {
                    out += ",";
                    comp += ",";
                }
                first = false;
                out += sfmt("{\"p\":%d,\"min\":%d,\"gas\":%d,\"mined\":%d,\"gassed\":%d,\"supply\":%g,\"max\":%g,"
                              "\"workers\":%d,\"army\":%d,\"army_supply\":%g,\"near\":{%s}}",
                              p, st.current_minerals[p], st.current_gas[p], st.total_minerals_gathered[p],
                              st.total_gas_gathered[p], st.supply_used[p][race].raw_value / 2.0,
                              std::min(200.0, st.supply_available[p][race].raw_value / 2.0), workers, army, armySupply2 / 2.0,
                              nearStr.c_str());
                if (composition)
                {
                    std::string typesStr;
                    for (auto &[name, count] : types)
                    {
                        if (!typesStr.empty()) typesStr += ",";
                        typesStr += json(name) + ":" + std::to_string(count);
                    }
                    comp += sfmt("{\"p\":%d,\"units\":{%s}}", p, typesStr.c_str());
                }
            }
            printf("%s]}\n", out.c_str());
            if (composition) printf("%s]}\n", comp.c_str());
        }

        void meta()
        {
            std::string players;
            for (int p = 0; p < 8; p++)
            {
                if (!active(p)) continue;
                if (!players.empty()) players += ",";
                auto start = game_st.start_locations[p];
                players += sfmt("{\"p\":%d,\"name\":%s,\"race\":\"%s\",\"start\":[%d,%d]}", p,
                                  json(replay_st.player_name[p].c_str()).c_str(), raceName(st.players[p].race),
                                  start.x, start.y);
            }
            // Minerals and geysers, so bases can be found
            std::string resources;
            for (unit_t *u : ptr(st.player_units[11]))
            {
                auto type = u->unit_type->id;
                if (type != UnitTypes::Resource_Mineral_Field && type != UnitTypes::Resource_Mineral_Field_Type_2
                    && type != UnitTypes::Resource_Mineral_Field_Type_3 && type != UnitTypes::Resource_Vespene_Geyser)
                {
                    continue;
                }
                if (!resources.empty()) resources += ",";
                resources += sfmt("[%d,%d,%d]", u->position.x, u->position.y, type == UnitTypes::Resource_Vespene_Geyser ? 1 : 0);
            }
            printf("{\"t\":\"meta\",\"map\":%s,\"frames\":%d,\"width\":%d,\"height\":%d,\"players\":[%s],"
                   "\"resources\":[%s]}\n",
                   json(replay_st.map_name.c_str()).c_str(), replay_st.end_frame, (int) game_st.map_width,
                   (int) game_st.map_height, players.c_str(), resources.c_str());
        }

        void run()
        {
            meta();
            scanUnits();
            while (!is_done())
            {
                next_frame();
                scanUnits();
                if (st.current_frame % 24 == 0)
                {
                    scanUpgrades();
                    snapshot(st.current_frame % 240 == 0);
                }
            }
            std::string res;
            for (auto &r : results)
            {
                if (!res.empty()) res += ",";
                res += r;
            }
            printf("{\"t\":\"end\",\"f\":%d,\"results\":[%s]}\n", st.current_frame, res.c_str());
        }
    };
}

int main(int argc, char **argv)
{
    std::string replay;
    std::string data = ".";
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) data = argv[++i];
        else replay = arg;
    }
    if (replay.empty())
    {
        fprintf(stderr, "usage: replay_dump <replay.rep> [--data <directory with StarDat.mpq, BrooDat.mpq, patch_rt.mpq>]\n");
        return 2;
    }

    game_player player(data.c_str());
    auto &st = player.st();
    action_state action_st;
    replay_state replay_st;
    dumper d(st, action_st, replay_st);
    d.load_replay_file(replay.c_str());
    d.run();
    return 0;
}

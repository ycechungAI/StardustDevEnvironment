#include "BWTest.h"
#include "BotRegistry.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

const RegisteredBot *FindBot(const std::string &name)
{
    auto lower = [](std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    };
    for (const auto &bot : RegisteredBots())
    {
        if (lower(bot.name) == lower(name)) return &bot;
    }
    return nullptr;
}

namespace
{
    std::string botList()
    {
        std::ostringstream list;
        for (const auto &bot : RegisteredBots())
        {
            list << "  " << bot.name << " (" << bot.race << ")" << std::endl;
        }
        return list.str();
    }
}

// Lists the opponent bots that can be played with Bots.Play
TEST(Bots, List)
{
    std::cout << "Opponent bots:" << std::endl << botList();
}

// Plays STARDUST_OPPONENT=<bot name> for STARDUST_GAMES games (default 1), on STARDUST_TEST_MAP or random SSCAIT
// maps. Replays are named <bot>_<map>_<seed>_WON / _LOST / _DRAW (a draw: the frame or time limit ended the game).
// STARDUST_BOT=<bot name> plays as that bot instead of the Python port (for example Stardust2025, the original C++
// Stardust); its replays are named <us>_vs_<bot>_...
TEST(Bots, Play)
{
    const RegisteredBot *us = nullptr;
    if (auto botName = std::getenv("STARDUST_BOT"); botName && *botName)
    {
        us = FindBot(botName);
        if (!us)
        {
            FAIL() << "No bot named " << botName << " to play as. Available:" << std::endl << botList();
        }
    }

    auto opponentName = std::getenv("STARDUST_OPPONENT");
    if (!opponentName || !*opponentName)
    {
        FAIL() << "Set STARDUST_OPPONENT to one of:" << std::endl << botList();
    }
    auto bot = FindBot(opponentName);
    if (!bot)
    {
        FAIL() << "No bot named " << opponentName << ". Available:" << std::endl << botList();
    }

    int games = 1;
    if (auto gamesSetting = std::getenv("STARDUST_GAMES"); gamesSetting && *gamesSetting)
    {
        games = std::max(1, std::atoi(gamesSetting));
    }

    int count = 0;
    int lost = 0;
    int drawn = 0;
    while (count < games)
    {
        BWTest test;
        test.opponentRace = bot->race;
        test.opponentModule = bot->create;
        test.opponentName = bot->name;
        if (auto seed = std::getenv("STARDUST_TEST_SEED"); seed && *seed) test.randomSeed = std::atoi(seed);
        if (us)
        {
            test.myRace = us->race;
            test.myModule = us->create;
            test.myName = us->name;
        }
        test.onEndMine = [&](bool won)
        {
            // Leaving at the frame or time limit means neither bot won
            const char *result = won ? "WON" : (test.limitReached ? "DRAW" : "LOST");
            std::ostringstream replayName;
            if (us) replayName << us->name << "_vs_";
            replayName << bot->name << "_" << test.map->shortname() << "_" << test.randomSeed << "_" << result;
            test.replayName = replayName.str();
            if (!won && test.limitReached) drawn++;
            else if (!won) lost++;

            count++;
            // One line per game for tools/round_robin.py
            std::cout << "[result] us=" << test.myName << " opponent=" << bot->name << " result=" << result
                      << " map=" << test.map->shortname() << " seed=" << test.randomSeed << std::endl;
            std::cout << "---------------------------------------------" << std::endl;
            std::cout << "VS " << bot->name << " AFTER " << count << " GAME" << (count == 1 ? "" : "S") << ": "
                      << (count - lost - drawn) << " won; " << lost << " lost; " << drawn << " drawn" << std::endl;
            std::cout << "---------------------------------------------" << std::endl;
        };
        test.expectWin = false;
        test.run();
    }
}

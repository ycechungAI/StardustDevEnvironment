// The two functions BWAPI looks for in a bot DLL, around the bot named in bot_config.h (written by CMakeLists.txt).
// Each game's result is also appended to bwapi-data/write/<bot>_results.txt in the StarCraft folder, so a run of games
// can be tallied afterwards.
#include <BWAPI.h>
#include <cstdio>
#include <string>
#include "bot_config.h"

namespace
{
    class RecordedBot : public BOT_CLASS
    {
    public:
        void onEnd(bool isWinner) override
        {
            BOT_CLASS::onEnd(isWinner);

            auto game = BWAPI::BroodwarPtr;
            std::string enemies;
            for (auto enemy : game->enemies())
            {
                if (!enemies.empty()) enemies += ", ";
                enemies += enemy->getRace().getName() + " " + enemy->getType().getName();
            }
            if (FILE *file = std::fopen("bwapi-data/write/" BOT_NAME "_results.txt", "a"))
            {
                std::fprintf(file, "%s vs %s on %s after %d frames\n", isWinner ? "WON " : "LOST", enemies.c_str(),
                             game->mapFileName().c_str(), game->getFrameCount());
                std::fclose(file);
            }
        }
    };
}

extern "C" __declspec(dllexport) void gameInit(BWAPI::Game *game)
{
    BWAPI::BroodwarPtr = game;
}

extern "C" __declspec(dllexport) BWAPI::AIModule *newAIModule()
{
    return new RecordedBot();
}

#include "BWTest.h"

#include "BW/BWData.h"
#include "GameStats.h"
#include "PythonAIModule.h"
#include <chrono>
#include <thread>
#include <csignal>
#include <execinfo.h>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <random>

#include "Log.h"

namespace
{
    std::mt19937 rng((std::random_device()) ());

    template<typename It>
    It randomElement(It start, It end)
    {
        std::uniform_int_distribution<> dis(0, std::distance(start, end) - 1);
        std::advance(start, dis(rng));
        return start;
    }

    int scheduleInitialUnitCreation(std::vector<UnitTypeAndPosition> &initialUnits,
                                    std::unordered_map<int, std::vector<UnitTypeAndPosition>> &initialUnitsByFrame)
    {
        // Rules for creating units:
        // - First create workers and overlords
        // - Then create pylons
        // - Then create non-combat buildings
        // - Then create combat buildings
        // - Then create remaining units

        bool changed = false;
        int frame = 0;

        // Scan for workers
        for (auto it = initialUnits.begin(); it != initialUnits.end();)
        {
            if (it->type.isWorker() || it->type == BWAPI::UnitTypes::Zerg_Overlord)
            {
                initialUnitsByFrame[frame].push_back(*it);
                it = initialUnits.erase(it);
                changed = true;
            }
            else
            {
                it++;
            }
        }

        if (changed) frame++;

        // Scan for buildings where we want to wait for creep
        for (auto it = initialUnits.begin(); it != initialUnits.end();)
        {
            if (it->waitForCreep)
            {
                initialUnitsByFrame[frame].push_back(*it);
                it = initialUnits.erase(it);
                frame += 1000;
            }
            else
            {
                it++;
            }
        }

        // Scan for pylons
        changed = false;
        for (auto it = initialUnits.begin(); it != initialUnits.end();)
        {
            if (it->type == BWAPI::UnitTypes::Protoss_Pylon)
            {
                initialUnitsByFrame[frame].push_back(*it);
                it = initialUnits.erase(it);
                changed = true;
            }
            else
            {
                it++;
            }
        }

        if (changed) frame++;

        // Scan for non-combat buildings
        changed = false;
        for (auto it = initialUnits.begin(); it != initialUnits.end();)
        {
            if (it->type.isBuilding() && !it->type.canAttack())
            {
                initialUnitsByFrame[frame].push_back(*it);
                it = initialUnits.erase(it);
                changed = true;
            }
            else
            {
                it++;
            }
        }

        if (changed) frame++;

        // Scan for combat buildings
        changed = false;
        for (auto it = initialUnits.begin(); it != initialUnits.end();)
        {
            if (it->type.isBuilding())
            {
                initialUnitsByFrame[frame].push_back(*it);
                it = initialUnits.erase(it);
                changed = true;
            }
            else
            {
                it++;
            }
        }

        if (changed) frame++;

        // Add remaining units
        for (auto &initialUnit : initialUnits)
        {
            initialUnitsByFrame[frame].push_back(initialUnit);
        }

        return frame;
    }

    void printBacktrace()
    {
        void *array[20];
        size_t size;

        // get void*'s for all entries on the stack
        size = backtrace(array, 20);

        // print out all the frames to stderr
        backtrace_symbols_fd(array, size, STDERR_FILENO);
    }

    void moveFileToReadIfExists(const std::string &filename)
    {
        if (!std::filesystem::exists(filename)) return;

        std::filesystem::create_directories("bwapi-data/read");

        std::filesystem::rename(
                filename,
                (std::ostringstream() << "bwapi-data/read/" << filename.substr(filename.rfind('/') + 1)).str());
    }

    void signalHandler(int sig, bool opponent)
    {
        if (opponent)
        {
            std::cerr << "Opponent crashed with signal " << sig << std::endl;
        }
        else
        {
            EXPECT_FALSE(true);
            std::cerr << "Crashed with signal " << sig << std::endl;
        }

        fprintf(stderr, "Error: signal %d:\n", sig);
        printBacktrace();
        exit(1);
    }
}

BWAPI::Position UnitTypeAndPosition::getCenterPosition()
{
    auto pos = tilePosition.isValid() ? BWAPI::Position(tilePosition) : position;

    if (type.isBuilding())
    {
        return BWAPI::Position(pos.x + type.tileWidth() * 16, pos.y + type.tileHeight() * 16);
    }

    return BWAPI::Position(pos.x + type.dimensionLeft() + 1, pos.y + type.dimensionUp() + 1);
}

void BWTest::run()
{
    // STARDUST_TEST_MAP=<name> overrides the map, e.g. to reproduce a map-specific problem
    if (auto mapOverride = std::getenv("STARDUST_TEST_MAP"); mapOverride && *mapOverride)
    {
        map = Maps::GetOne(mapOverride);
    }

    // STARDUST_TEST_FRAME_LIMIT=<frames> overrides the frame limit, e.g. to only look at startup
    if (auto frameLimitOverride = std::getenv("STARDUST_TEST_FRAME_LIMIT"); frameLimitOverride && *frameLimitOverride)
    {
        frameLimit = std::atoi(frameLimitOverride);
        expectWin = false;
    }

    // Ensure a map is selected
    if (!map)
    {
        // If no map set is defined on the test, default to all SSCAIT maps
        if (maps.empty())
        {
            maps = Maps::Get("sscai");
        }

        map = std::make_shared<Maps::MapMetadata>(*randomElement(maps.begin(), maps.end()));
    }

    // If the random seed is -1, generate one
    if (randomSeed == -1)
    {
        std::uniform_int_distribution<> distribution(1, 100000);
        randomSeed = distribution(rng);
    }

    initialUnitFrames = std::max(
            scheduleInitialUnitCreation(myInitialUnits, myInitialUnitsByFrame),
            scheduleInitialUnitCreation(opponentInitialUnits, opponentInitialUnitsByFrame));

    auto opponentPid = fork();
    if (opponentPid == 0)
    {
        auto handler = [](int sig)
        {
            signalHandler(sig, true);
        };
        signal(SIGFPE, handler);
        signal(SIGSEGV, handler);
        signal(SIGABRT, handler);

        // Only our own game gets a window (in builds with OPENBW_ENABLE_UI). The window belongs to the main thread,
        // which doesn't exist in this forked process.
        setenv("OPENBW_ENABLE_UI", "0", 1);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        runGame(true);
        _exit(EXIT_SUCCESS);
    }

    auto handler = [](int sig)
    {
        signalHandler(sig, false);
    };

    signal(SIGFPE, handler);
    signal(SIGSEGV, handler);
    signal(SIGABRT, handler);

    runGame(false);

    // Give the opponent 5 seconds to exit
    int tries = 0;
    while (true)
    {
        if (waitpid(opponentPid, nullptr, WNOHANG) != -1)
        {
            std::cout << "Opponent process exited" << std::endl;
            break;
        }

        // Kill after 5 seconds
        tries++;
        if (tries == 50)
        {
            kill(opponentPid, SIGKILL);
            std::cout << "Opponent process killed" << std::endl;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void BWTest::runGame(bool opponent)
{
    BW::GameOwner gameOwner;
    BWAPI::BroodwarImpl_handle h(gameOwner.getGame());
    h->setCharacterName(opponent ? "Opponent" : "Tests");
    h->setGameType(BWAPI::GameTypes::Melee);
    BWAPI::BroodwarImpl.bwgame.setMapFileName(map->filename);
    BWAPI::Race race = opponent ? opponentRace : myRace;
    h->createMultiPlayerGame([&]()
                             {
                                 if (h->self())
                                 {
                                     if (h->self()->getRace() != race) h->self()->setRace(race);
                                 }
                                 else
                                 {
                                     h->switchToPlayer(h->getPlayer(opponent ? 1 : 0));
                                 }

                                 int playerCount = 0;
                                 for (int i = 0; i < BW::PLAYABLE_PLAYER_COUNT; ++i)
                                 {
                                     BWAPI::Player p = h->getPlayer(i);
                                     if (p->getType() != BWAPI::PlayerTypes::Player
                                         && p->getType() != BWAPI::PlayerTypes::Computer)
                                         continue;
                                     ++playerCount;
                                 }
                                 if (playerCount >= 2)
                                 {
                                     h->setRandomSeed(randomSeed);
                                     h->startGame();
                                 }
                             });

    std::cout << "Game started" << (opponent ? " (opponent)" : "") << "! "
              << "framelimit=" << frameLimit
              << "; timelimit=" << timeLimit
              << "; map=" << map->filename
              << "; seed=" << randomSeed
              << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    if (opponent)
    {
        if (opponentModule)
        {
            auto module = opponentModule();
            module->afterOnStart = [this, &h]()
            {
                h->setLocalSpeed(0);

                if (onStartOpponent) onStartOpponent();
            };
            h->setAIModule(module);
        }
    }
    else
    {
        BWAPI::AIModule *module;
        if (myModule)
        {
            module = myModule();
        }
        else
        {
            auto pythonModule = new PythonAIModule();
            if (initialUnitFrames > 0) pythonModule->frameSkip = initialUnitFrames + BWAPI::Broodwar->getLatencyFrames();
            module = pythonModule;
        }
        module->afterOnStart = [this, &h]()
        {
            h->setLocalSpeed(0);

            if (onStartMine) onStartMine();
        };
        h->setAIModule(module);
        Log::SetOutputToConsole(true);
    }
    h->update();

    for (int frame = 0; frame <= initialUnitFrames; frame++)
    {
        if (frame > 0) h->update();

        auto &initialUnits = opponent ? opponentInitialUnitsByFrame[frame] : myInitialUnitsByFrame[frame];
        for (auto &unitAndPosition : initialUnits)
        {
            h->createUnit(h->self(), unitAndPosition.type, unitAndPosition.getCenterPosition());
        }

        gameOwner.getGame().nextFrame();
    }

    frameLimit += initialUnitFrames;

    bool leftGame = false;
    bool reachedLimit = false;
    auto startTime = std::chrono::high_resolution_clock::now();

    // The stats as of the last frame played (once a player has left the game, the engine has removed its units)
    std::optional<PlayerStats> lastMyStats, lastOpponentStats;

    // Both players' losses, counted from every kill the engine makes (whoever can see it)
    Losses losses;
    if (!opponent)
    {
        gameOwner.getGame().setOnKillUnit([&losses, &leftGame](BW::Unit unit)
                                          {
                                              if (!leftGame) losses.count(unit);
                                          });
    }

    // In the game window: [s] toggles the stats screen, [r] saves the replay so far
    bool showStats = true;
    auto ratings = ReadRatings();
    auto opponentDisplayName = opponentName.empty() ? std::string("Opponent") : opponentName;
    auto handleWindow = [&]()
    {
        auto game = gameOwner.getGame();
        for (int key : game.takeKeyPresses())
        {
            if (key == 's')
            {
                showStats = !showStats;
            }
            else if (key == 'r')
            {
                std::ostringstream replayFilename;
                auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                replayFilename << "replays/" << myName << "_vs_" << opponentDisplayName << "_" << map->shortname()
                               << "_" << randomSeed << "_frame" << h->getFrameCount() << "_"
                               << std::put_time(std::localtime(&tt), "%Y%m%d_%H%M%S") << ".rep";
                std::filesystem::create_directories("replays");
                game.saveReplay(replayFilename.str());
                std::cout << "Saved replay " << replayFilename.str() << std::endl;
                h->printf("Saved replay %s", replayFilename.str().c_str());
            }
        }

        if (!leftGame)
        {
            lastMyStats = PlayerStats::read(game, "Tests", myName, losses);
            lastOpponentStats = PlayerStats::read(game, "Opponent", opponentDisplayName, losses);
        }
        if (showStats && lastMyStats && lastOpponentStats && std::get<0>(game.GameScreenBuffer()) > 0)
        {
            DrawStatsScreen(BWAPI::BroodwarPtr, *lastMyStats, *lastOpponentStats, ratings);
        }
    };
    while (!gameOwner.getGame().gameOver())
    {
        try
        {
            h->update();

            if (!leftGame)
            {
                if (opponent)
                {
                    if (onFrameOpponent) onFrameOpponent();
                }
                else
                {
                    if (onFrameMine) onFrameMine();
                }
            }

            if (!opponent) handleWindow();

            if (!leftGame && h->getFrameCount() == frameLimit)
            {
                std::cout << "Frame limit reached; leaving game" << std::endl;
                leftGame = reachedLimit = true;
                h->leaveGame();
            }

            if (!leftGame)
            {
                auto now = std::chrono::high_resolution_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count() > timeLimit)
                {
                    std::cout << "Time limit reached; leaving game" << std::endl;
                    leftGame = reachedLimit = true;
                    h->leaveGame();
                }
            }

            gameOwner.getGame().nextFrame();
        }
        catch (std::exception &ex)
        {
            std::cerr << "Exception caught in frame (" << (opponent ? "opponent" : "mine") << "): " << ex.what() << std::endl;
            printBacktrace();
            if (!leftGame)
            {
                leftGame = true;
                h->leaveGame();
            }
        }
    }

    std::cout << "Game over " << (opponent ? "(opponent) " : "") << "after " << h->getFrameCount() << " frames" << std::endl;
    if (!opponent) gameOwner.getGame().setOnKillUnit(nullptr);

    h->update();

    if (opponent)
    {
        if (onEndOpponent) onEndOpponent(gameOwner.getGame().won());
    }
    else
    {
        if (onEndMine) onEndMine(gameOwner.getGame().won());
    }

    try
    {
        h->onGameEnd();
    }
    catch (std::exception &ex)
    {
        std::cerr << "Exception caught in game end (" << (opponent ? "opponent" : "mine") << "): " << ex.what() << std::endl;
        printBacktrace();
    }

    if (!opponent)
    {
        auto result = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start).count();
        std::cout << "Total game time: " << result << "s" << std::endl;

        if (expectWin) EXPECT_TRUE(gameOwner.getGame().won());

        // Create an ID for this game based on the test case and timestamp
        std::ostringstream gameId;
        if (replayName.empty())
        {
            gameId << ::testing::UnitTest::GetInstance()->current_test_info()->test_case_name();
            gameId << "_" << ::testing::UnitTest::GetInstance()->current_test_info()->name();
        }
        else
        {
            gameId << replayName;
        }
        auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto tm = std::localtime(&tt);
        gameId << "_" << std::put_time(tm, "%Y%m%d_%H%M%S");
        if (::testing::UnitTest::GetInstance()->current_test_info()->result()->Failed())
        {
            gameId << "_FAIL";
        }
        else
        {
            gameId << "_PASS";
        }

        // Print the game's stats, and record it in the results history if we know who the opponent was
        auto &me = lastMyStats;
        auto &them = lastOpponentStats;
        if (me && them)
        {
            std::cout << "STATS " << StatsSummary(*me, *them) << std::endl;
            // STARDUST_NO_RESULTS=1 keeps practice games out of the results history (and so the Elo ratings)
            auto noResults = std::getenv("STARDUST_NO_RESULTS");
            if (!opponentName.empty() && !(noResults && *noResults && std::string(noResults) != "0"))
            {
                std::string result = gameOwner.getGame().won() ? "WON" : (reachedLimit ? "DRAW" : "LOST");
                AppendResult(*me, *them, result, h->getFrameCount(), map->shortname(), randomSeed,
                             gameId.str() + ".rep");
            }
        }

        // If enabled, write the replay file
        // Otherwise remove the cvis directory
        if (writeReplay)
        {
            std::ostringstream replayFilename;
            replayFilename << "replays/" << gameId.str() << ".rep";
            std::filesystem::create_directories("replays");
            BWAPI::BroodwarImpl.bwgame.saveReplay(replayFilename.str());

            // Move the cvis directory
            if (std::filesystem::exists("bwapi-data/write/cvis"))
            {
                std::ostringstream cvisFilename;
                cvisFilename << "replays/" << gameId.str() << ".rep.cvis";
                std::filesystem::rename("bwapi-data/write/cvis", cvisFilename.str());
            }

            // Move log files
            if (!Log::LogFileName().empty() && std::filesystem::exists(Log::LogFileName()))
            {
                std::ostringstream logDirectory;
                logDirectory << "replays/" << gameId.str() << ".rep.log";
                std::filesystem::create_directories(logDirectory.str());

                std::ostringstream newLogFilename;
                newLogFilename << logDirectory.str() << "/" << Log::LogFileName().substr(Log::LogFileName().rfind('/') + 1);
                std::filesystem::rename(Log::LogFileName(), newLogFilename.str());
            }
        }
        else
        {
            if (std::filesystem::exists("bwapi-data/write/cvis"))
            {
                std::filesystem::remove_all("bwapi-data/write/cvis");
            }
        }
    }
    else
    {
        // Move opponent learning files to read
        moveFileToReadIfExists("bwapi-data/write/om_Startest.txt"); // Steamhammer
        moveFileToReadIfExists("bwapi-data/write/omlocutus_startest.txt"); // Locutus
    }
    h->bwgame.leaveGame();
}
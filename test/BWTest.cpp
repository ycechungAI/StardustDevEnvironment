#include "BWTest.h"

#include "BW/BWData.h"
#include "GameStats.h"
#include "PythonAIModule.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <csignal>
#include <deque>
#include <fstream>
#include <mutex>
#include <sys/mman.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;
#include <execinfo.h>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <random>

#include "Log.h"

namespace
{
    std::mt19937 rng((std::random_device()) ());

    // Hang watchdog (STARDUST_HANG_SECONDS=<n>): each game process records its frame and the step of the game loop it
    // is in, in memory shared across the fork. If either stops for n seconds, the main process writes what led up to
    // it to replays/unfinished/ (the run's conditions, recent game state, stack samples, the replay so far), then ends
    // the game, or with STARDUST_HANG_FREEZE=1 pauses both processes for a debugger.
    enum Phase { Starting, Update, OnFrame, Window, NextFrame, Paused, GameEnd, Done };
    const char *phaseNames[] = {"starting the game", "update (bots' onFrame)", "test onFrame hook", "window/stats",
                                "nextFrame (waiting on the other process)", "paused by the user", "game end", "done"};
    struct Heartbeat
    {
        std::atomic<int> frame{-1};
        std::atomic<int> phase{Starting};
        std::atomic<long long> beatMs{0};
        std::atomic<int> pid{0};
        // [p] in the game window pauses both processes at this frame (-1: not paused); only [0]'s is used
        std::atomic<int> pauseAt{-1};
    };
    Heartbeat *heartbeats = nullptr;  // [0] our game, [1] the opponent's
    std::atomic<int> gameNumber{0};   // a process can play several games; each one's watchdog stops with it

    long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void beat(bool opponent, Phase phase, int frame = -1)
    {
        if (!heartbeats) return;
        auto &hb = heartbeats[opponent ? 1 : 0];
        hb.phase = phase;
        if (frame >= 0) hb.frame = frame;
        hb.beatMs = nowMs();
    }

    std::mutex historyMutex;
    std::deque<std::string> history;  // recent game state, newest last
    std::function<void(const std::string &)> saveReplaySoFar;

    // The watchdog runs while the stuck thread may hold any runtime lock (iostream, locale, malloc), so it formats text
    // into fixed buffers by hand and writes it with write(): a first version using iostream blocked at its first number.
    struct RawText
    {
        char buf[16384];
        size_t len = 0;
        RawText &operator<<(const char *text)
        {
            while (*text && len < sizeof(buf) - 1) buf[len++] = *text++;
            buf[len] = 0;
            return *this;
        }
        RawText &operator<<(long long n)
        {
            char digits[24];
            int count = 0;
            bool negative = n < 0;
            unsigned long long u = negative ? -(unsigned long long)n : (unsigned long long)n;
            do { digits[count++] = (char)('0' + u % 10); u /= 10; } while (u);
            if (negative) *this << "-";
            while (count && len < sizeof(buf) - 1) buf[len++] = digits[--count];
            buf[len] = 0;
            return *this;
        }
    };

    // Runs a program without the shell or std::system, and waits up to timeoutMs for it
    void runAndWait(std::vector<const char *> args, long long timeoutMs)
    {
        args.push_back(nullptr);
        pid_t child;
        if (posix_spawnp(&child, args[0], nullptr, nullptr, const_cast<char *const *>(args.data()), environ) != 0)
        {
            return;
        }
        auto start = nowMs();
        while (waitpid(child, nullptr, WNOHANG) == 0)
        {
            if (nowMs() - start > timeoutMs)
            {
                kill(child, SIGKILL);
                waitpid(child, nullptr, 0);
                return;
            }
            usleep(100000);
        }
    }

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
        // Only async-signal-safe calls in here: the crash can come while the crashing thread holds the iostream,
        // stdio or malloc lock, and a handler that needs the same lock deadlocks, which leaves the game frozen
        // forever instead of ended. A second signal (a crash in here, or gtest aborting because it has already shut
        // down) exits at once instead of looping.
        static volatile sig_atomic_t handling = 0;
        if (handling) _exit(1);
        handling = 1;

        RawText message;
        message << (opponent ? "Opponent crashed with signal " : "Crashed with signal ") << (long long)sig << "\n";
        (void)!write(STDERR_FILENO, message.buf, message.len);
        printBacktrace();

        // _exit, not exit: after a crash, running the bot's static destructors can hang forever (BunkerBoxer's map
        // printer spun at 100% CPU in BMP::WriteToFile, leaving orphaned processes behind). The exit code of 1 fails
        // the run.
        _exit(1);
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

    // Shared with the forked opponent process, for the hang watchdog
    if (!heartbeats)
    {
        heartbeats = static_cast<Heartbeat *>(mmap(nullptr, sizeof(Heartbeat) * 2, PROT_READ | PROT_WRITE,
                                                   MAP_SHARED | MAP_ANON, -1, 0));
        new(heartbeats) Heartbeat[2];
    }
    heartbeats[0].frame = heartbeats[1].frame = -1;
    heartbeats[0].pauseAt = -1;
    int thisGame = ++gameNumber;
    beat(false, Starting);
    beat(true, Starting);

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
        // Only our own game writes the status feed for tools/watch.py
        unsetenv("OPENBW_STATUS_FILE");

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        runGame(true);
        beat(true, Done);
        _exit(EXIT_SUCCESS);
    }

    auto handler = [](int sig)
    {
        signalHandler(sig, false);
    };

    signal(SIGFPE, handler);
    signal(SIGSEGV, handler);
    signal(SIGABRT, handler);

    heartbeats[0].pid = getpid();
    heartbeats[1].pid = opponentPid;
    auto hangSeconds = std::getenv("STARDUST_HANG_SECONDS") ? std::atoi(std::getenv("STARDUST_HANG_SECONDS")) : 0;
    if (hangSeconds > 0)
    {
        auto conditions = (std::ostringstream() << myName << " (" << myRace << ") vs "
                                                << (opponentName.empty() ? "Opponent" : opponentName) << " ("
                                                << opponentRace << "); map " << map->filename << "; seed " << randomSeed
                                                << "; frame limit " << frameLimit << "; time limit " << timeLimit
                                                << "s").str();
        auto baseName = (std::ostringstream() << myName << "_vs_"
                                              << (opponentName.empty() ? "Opponent" : opponentName) << "_"
                                              << map->shortname() << "_" << randomSeed).str();
        // Everything the watchdog writes is prepared now, while no thread can be stuck
        auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto started = (std::ostringstream() << std::put_time(std::localtime(&tt), "%Y%m%d_%H%M%S")).str();
        std::filesystem::create_directories("replays/unfinished");
        std::thread([=]()
                    {
                        while (true)
                        {
                            std::this_thread::sleep_for(std::chrono::seconds(2));
                            if (gameNumber != thisGame || heartbeats[0].phase == Done) return;
                            int stuck = -1;
                            for (int i = 0; i < 2; i++)
                            {
                                if (heartbeats[i].phase != Done
                                    && nowMs() - heartbeats[i].beatMs > hangSeconds * 1000LL)
                                {
                                    stuck = i;
                                }
                            }
                            if (stuck == -1) continue;

                            // Snapshot the heartbeats first: the stack samples take seconds
                            int frames[2], phases[2], pids[2];
                            long long ages[2];
                            for (int i = 0; i < 2; i++)
                            {
                                frames[i] = heartbeats[i].frame;
                                phases[i] = heartbeats[i].phase;
                                pids[i] = heartbeats[i].pid;
                                ages[i] = (nowMs() - heartbeats[i].beatMs) / 1000;
                            }

                            RawText base;
                            base << "replays/unfinished/" << baseName.c_str() << "_frame" << (long long)frames[0]
                                 << "_started" << started.c_str() << "_HUNG";

                            // 1. Stacks of both processes, before anything that might block
                            for (int i = 0; i < 2; i++)
                            {
                                RawText samplePath;
                                samplePath << base.buf << (i == 0 ? "_ours" : "_opponent") << ".sample.txt";
                                RawText pid;
                                pid << (long long)pids[i];
                                runAndWait({"sample", pid.buf, "3", "-file", samplePath.buf}, 20000);
                            }

                            // 2. The report
                            RawText report;
                            report << "HUNG: no progress for " << (long long)hangSeconds << "s\n" << conditions.c_str()
                                   << "\n\n";
                            for (int i = 0; i < 2; i++)
                            {
                                report << (i == 0 ? "Our game" : "Opponent's game") << " (pid " << (long long)pids[i]
                                       << "): frame " << (long long)frames[i] << ", in " << phaseNames[phases[i]]
                                       << ", last progress " << ages[i] << "s ago"
                                       << (i == stuck ? "  <-- stuck" : "") << "\n";
                            }
                            report << "\nGame state before the hang (every 480 frames, newest last):\n";
                            if (historyMutex.try_lock())
                            {
                                for (auto &line : history) report << line.c_str() << "\n";
                                historyMutex.unlock();
                            }
                            else
                            {
                                report << "(unavailable: the stuck thread holds it)\n";
                            }
                            RawText reportPath;
                            reportPath << base.buf << ".txt";
                            int fd = open(reportPath.buf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                            if (fd >= 0)
                            {
                                (void)!write(fd, report.buf, report.len);
                                close(fd);
                            }

                            RawText line;
                            line << "HUNG " << conditions.c_str() << "; frames " << (long long)frames[0] << "/"
                                 << (long long)frames[1] << "; stuck in "
                                 << (stuck == 0 ? "our game: " : "the opponent's game: ") << phaseNames[phases[stuck]]
                                 << "; saved " << base.buf << ".*\n";
                            (void)!write(STDOUT_FILENO, line.buf, line.len);

                            // 3. The replay so far: this can block on the stuck thread, so it gets 15 seconds
                            if (saveReplaySoFar)
                            {
                                RawText replayPath;
                                replayPath << base.buf << ".rep";
                                std::string path = replayPath.buf;
                                std::thread([path]() { saveReplaySoFar(path); }).detach();
                                std::this_thread::sleep_for(std::chrono::seconds(15));
                            }

                            auto freeze = std::getenv("STARDUST_HANG_FREEZE");
                            if (freeze && *freeze && freeze[0] != '0')
                            {
                                RawText frozen;
                                frozen << "Frozen for debugging: lldb -p " << (long long)pids[0] << " / lldb -p "
                                       << (long long)pids[1] << "\n";
                                (void)!write(STDOUT_FILENO, frozen.buf, frozen.len);
                                kill(pids[1], SIGSTOP);
                                kill(getpid(), SIGSTOP);
                                continue;
                            }
                            kill(pids[1], SIGKILL);
                            _exit(3);
                        }
                    }).detach();
    }

    runGame(false);
    beat(false, Done);

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
    if (!opponent) gameOwner.getGame().setPlayerNames(myName, opponentName.empty() ? "Opponent" : opponentName);
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

    // The numbers tools/live_stats.py shows while the game runs
    LiveStats live(gameNumber.load(), map->shortname(), randomSeed, frameLimit);

    // What the game window's observer camera saw (events.jsonl, for commentary and tools)
    GameEvents events(!opponent, gameOwner.getGame(), gameNumber.load(), map->shortname(), randomSeed, myName,
                      opponentName.empty() ? std::string("Opponent") : opponentName);

    // In the game window: [s] toggles the stats screen, [r] saves the replay so far, [p] pauses the game (both
    // processes stop before the same frame; the hang watchdog sees them as paused, not stuck). The observer camera
    // has its own keys: space switches between it and the user's camera, [a] brings it back.
    bool showStats = true;
    auto ratings = ReadRatings();
    auto opponentDisplayName = opponentName.empty() ? std::string("Opponent") : opponentName;
    auto handleKeys = [&]()
    {
        auto game = gameOwner.getGame();
        for (int key : game.takeKeyPresses())
        {
            if (key == 's')
            {
                showStats = !showStats;
            }
            else if (key == 'p' && heartbeats && !leftGame)
            {
                if (heartbeats[0].pauseAt >= 0)
                {
                    heartbeats[0].pauseAt = -1;
                }
                else
                {
                    // Far enough ahead that neither process has passed it (they are within the latency of each other)
                    heartbeats[0].pauseAt = std::max(h->getFrameCount(), heartbeats[1].frame.load()) + 24;
                }
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
        for (auto &e : game.takeCameraEvents()) events.write(e);
    };
    auto handleWindow = [&]()
    {
        auto game = gameOwner.getGame();
        handleKeys();

        if (!leftGame)
        {
            lastMyStats = PlayerStats::read(game, "Tests", myName, losses);
            lastOpponentStats = PlayerStats::read(game, "Opponent", opponentDisplayName, losses);
            if (lastMyStats && lastOpponentStats) live.update(game, h->getFrameCount(), *lastMyStats, *lastOpponentStats);

            // STARDUST_OBSERVE=<frames> prints both players' unit counts at that interval, like watching the replay
            static int observeInterval = std::getenv("STARDUST_OBSERVE") ? std::atoi(std::getenv("STARDUST_OBSERVE")) : 0;
            if (observeInterval > 0 && h->getFrameCount() % observeInterval == 0)
            {
                std::cout << "OBSERVE " << h->getFrameCount() << ObserveUnitCounts(game, "Tests", myName)
                          << " ||" << ObserveUnitCounts(game, "Opponent", opponentDisplayName) << std::endl;
            }

            if (heartbeats && h->getFrameCount() % 480 == 0 && lastMyStats && lastOpponentStats)
            {
                auto line = (std::ostringstream() << "frame " << h->getFrameCount() << ": "
                                                  << StatsSummary(*lastMyStats, *lastOpponentStats) << " |"
                                                  << ObserveUnitCounts(game, "Tests", myName) << " ||"
                                                  << ObserveUnitCounts(game, "Opponent", opponentDisplayName)).str();
                std::lock_guard<std::mutex> lock(historyMutex);
                history.push_back(line);
                if (history.size() > 30) history.pop_front();
            }
        }
        if (lastMyStats && lastOpponentStats && std::get<0>(game.GameScreenBuffer()) > 0)
        {
            DrawToolbar(BWAPI::BroodwarPtr, *lastMyStats, *lastOpponentStats);
        }
        if (showStats && lastMyStats && lastOpponentStats && std::get<0>(game.GameScreenBuffer()) > 0)
        {
            DrawStatsScreen(BWAPI::BroodwarPtr, *lastMyStats, *lastOpponentStats, ratings);
        }
        // Drawn from before the pause starts: the window shows the last frame's drawing while the game is paused
        if (heartbeats && heartbeats[0].pauseAt >= 0 && std::get<0>(game.GameScreenBuffer()) > 0)
        {
            int x = game.screenWidth() / 2 - 60;
            int y = game.screenHeight() / 2 - 40;
            BWAPI::Broodwar->drawBoxScreen(x - 6, y - 4, x + 126, y + 14, BWAPI::Colors::Black, true);
            BWAPI::Broodwar->drawTextScreen(x, y, "%cPAUSED  %c[p] resume", BWAPI::Text::Yellow, BWAPI::Text::White);
        }
    };

    // Waits while the user has paused the game: both processes stop here, before the same frame, so neither waits
    // in nextFrame for the other (OpenBW drops a player after a minute without hearing from it)
    auto waitWhilePaused = [&]()
    {
        if (!heartbeats || leftGame) return;
        int pauseAt = heartbeats[0].pauseAt;
        if (pauseAt < 0 || h->getFrameCount() < pauseAt) return;

        auto pausedAt = std::chrono::high_resolution_clock::now();
        if (!opponent)
        {
            std::cout << "Paused at frame " << h->getFrameCount() << std::endl;
            events.write("pause", h->getFrameCount());
        }
        while (heartbeats[0].pauseAt >= 0)
        {
            beat(opponent, Paused, h->getFrameCount());
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (opponent)
            {
                // The main process has gone on (or its game is over)
                int phase = heartbeats[0].phase;
                if (phase == GameEnd || phase == Done || heartbeats[0].frame > h->getFrameCount()) break;
            }
            else
            {
                handleKeys();
                live.keepAlive(true);
                if (gameOwner.getGame().gameClosed()) heartbeats[0].pauseAt = -1;
            }
        }
        // The pause doesn't count towards the time limit
        startTime += std::chrono::high_resolution_clock::now() - pausedAt;
        if (!opponent)
        {
            live.keepAlive(false);
            std::cout << "Resumed" << std::endl;
            events.write("resume", h->getFrameCount());
        }
    };
    if (!opponent)
    {
        std::lock_guard<std::mutex> lock(historyMutex);
        saveReplaySoFar = [&gameOwner](const std::string &filename) { gameOwner.getGame().saveReplay(filename); };
    }
    while (!gameOwner.getGame().gameOver())
    {
        try
        {
            beat(opponent, Update, h->getFrameCount());
            h->update();

            beat(opponent, OnFrame);
            // STARDUST_TEST_HANG_AT=<frame> stalls the opponent's process there, to test the hang watchdog
            static int hangAt = std::getenv("STARDUST_TEST_HANG_AT") ? std::atoi(std::getenv("STARDUST_TEST_HANG_AT")) : -1;
            if (opponent && h->getFrameCount() == hangAt)
            {
                while (true) std::this_thread::sleep_for(std::chrono::seconds(60));
            }
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

            beat(opponent, Window);
            if (!opponent) handleWindow();

            if (!leftGame && h->getFrameCount() == frameLimit)
            {
                std::cout << "Frame limit reached; leaving game" << std::endl;
                leftGame = reachedLimit = true;
                if (!opponent) limitReached = true;
                h->leaveGame();
            }

            if (!leftGame)
            {
                auto now = std::chrono::high_resolution_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count() > timeLimit)
                {
                    std::cout << "Time limit reached; leaving game" << std::endl;
                    leftGame = reachedLimit = true;
                    if (!opponent) limitReached = true;
                    h->leaveGame();
                }
            }

            waitWhilePaused();

            beat(opponent, NextFrame);
            gameOwner.getGame().nextFrame();

            // For tools/run_games.py and tools/round_robin.py, which watch every game's speed whatever the bots
            if (!opponent && !leftGame && h->getFrameCount() % 1000 == 0)
            {
                auto seconds = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - startTime).count();
                std::cout << "[progress] frame=" << h->getFrameCount() << " seconds=" << seconds << std::endl;
            }
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

    beat(opponent, GameEnd);
    if (!opponent)
    {
        if (heartbeats) heartbeats[0].pauseAt = -1;
        for (auto &e : gameOwner.getGame().takeCameraEvents()) events.write(e);
        std::lock_guard<std::mutex> lock(historyMutex);
        saveReplaySoFar = nullptr;
    }
    // Read now: once the game has ended, the frame count goes back to 0
    int framesPlayed = h->getFrameCount();
    std::cout << "Game over " << (opponent ? "(opponent) " : "") << "after " << framesPlayed << " frames" << std::endl;
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
                AppendResult(*me, *them, result, framesPlayed, map->shortname(), randomSeed,
                             gameId.str() + ".rep");
            }
        }
        live.finish(framesPlayed, gameOwner.getGame().won() ? "WON" : (reachedLimit ? "DRAW" : "LOST"));

        // If enabled, write the replay file
        // Otherwise remove the cvis directory
        if (writeReplay)
        {
            std::ostringstream replayFilename;
            replayFilename << "replays/" << gameId.str() << ".rep";
            std::filesystem::create_directories("replays");
            BWAPI::BroodwarImpl.bwgame.saveReplay(replayFilename.str());
            std::cout << "REPLAY " << gameId.str() << ".rep" << std::endl;  // tools/run_games.py reads this

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
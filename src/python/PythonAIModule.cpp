#include "PythonAIModule.h"

#include "PythonModules.h"

#include <pybind11/embed.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#ifndef STARDUST_PYTHON_DIR
#define STARDUST_PYTHON_DIR ""
#endif
#ifndef STARDUST_VENV_DIR
#define STARDUST_VENV_DIR ""
#endif

namespace py = pybind11;

namespace
{
    std::string envOr(const char *name, const char *fallback)
    {
        const char *value = std::getenv(name);
        return (value && *value) ? value : fallback;
    }

    // The interpreter is created on first use and deliberately never finalized: the test harness runs
    // several games per process, and Python does not support re-initializing after finalization.
    void ensureInterpreter()
    {
        if (Py_IsInitialized()) return;

        // Without finalization, buffered stdout would be lost at exit
        setenv("PYTHONUNBUFFERED", "1", 0);

        // Leave signal handling to the host process
        py::initialize_interpreter(false);

        py::dict paths;
        paths["bot_path"] = envOr("STARDUST_PYTHON_PATH", STARDUST_PYTHON_DIR);
        paths["venv"] = envOr("VIRTUAL_ENV", STARDUST_VENV_DIR);
        py::exec(R"(
import os, site, sys
for entry in reversed(bot_path.split(os.pathsep)):
    if entry and entry not in sys.path:
        sys.path.insert(0, entry)
if venv:
    site_packages = os.path.join(venv, "lib", f"python{sys.version_info.major}.{sys.version_info.minor}", "site-packages")
    if os.path.isdir(site_packages):
        site.addsitedir(site_packages)
)", py::globals(), paths);
    }

    std::string formatPythonError(py::error_already_set &error)
    {
        try
        {
            auto lines = py::module_::import("traceback").attr("format_exception")(error.value());
            return py::str("").attr("join")(lines).cast<std::string>();
        }
        catch (...)
        {
            return error.what();
        }
    }

    // Tracks onFrame time against the usual tournament rules: a bot loses if it has 320 frames over 55ms,
    // 10 frames over 1s, or a single frame over 10s.
    struct FrameStats
    {
        int frames = 0;
        int over55ms = 0;
        int over1s = 0;
        int over10s = 0;
        double totalMs = 0;
        double maxMs = 0;

        void record(double ms)
        {
            frames++;
            totalMs += ms;
            maxMs = std::max(maxMs, ms);
            if (ms > 55) over55ms++;
            if (ms > 1000) over1s++;
            if (ms > 10000) over10s++;
        }

        std::string summary() const
        {
            std::ostringstream os;
            os << std::fixed << std::setprecision(2)
               << "Python onFrame: " << frames << " frames, avg " << (frames ? totalMs / frames : 0) << "ms, max " << maxMs
               << "ms; frames >55ms: " << over55ms << "/320, >1s: " << over1s << "/10, >10s: " << over10s << "/1";
            return os.str();
        }
    };
}

struct PythonAIModule::Impl
{
    py::object bot;
    bool raiseErrors = true;
    FrameStats frameStats;

    py::object onStart, onEnd, onFrame, onSendText, onReceiveText, onPlayerLeft, onNukeDetect, onUnitDiscover,
            onUnitEvade, onUnitShow, onUnitHide, onUnitCreate, onUnitDestroy, onUnitMorph, onUnitRenegade, onSaveGame,
            onUnitComplete;

    Impl()
    {
        raiseErrors = envOr("STARDUST_PY_ERRORS", "raise") != "log";

        try
        {
            auto bwapiModule = py::module_::import("bwapi");
            publish_broodwar(bwapiModule);

            std::string spec = envOr("STARDUST_BOT", "stardust:create_bot");
            auto colon = spec.find(':');
            if (colon == std::string::npos)
            {
                throw std::runtime_error("STARDUST_BOT must look like module:factory, got '" + spec + "'");
            }
            auto factory = py::module_::import(spec.substr(0, colon).c_str()).attr(spec.substr(colon + 1).c_str());
            bot = factory();
        }
        catch (py::error_already_set &e)
        {
            handleError("bot creation", e, true);
        }

        auto handler = [this](const char *name)
        {
            py::object fn = py::getattr(bot, name, py::none());
            return fn.is_none() ? py::object() : fn;
        };
        onStart = handler("onStart");
        onEnd = handler("onEnd");
        onFrame = handler("onFrame");
        onSendText = handler("onSendText");
        onReceiveText = handler("onReceiveText");
        onPlayerLeft = handler("onPlayerLeft");
        onNukeDetect = handler("onNukeDetect");
        onUnitDiscover = handler("onUnitDiscover");
        onUnitEvade = handler("onUnitEvade");
        onUnitShow = handler("onUnitShow");
        onUnitHide = handler("onUnitHide");
        onUnitCreate = handler("onUnitCreate");
        onUnitDestroy = handler("onUnitDestroy");
        onUnitMorph = handler("onUnitMorph");
        onUnitRenegade = handler("onUnitRenegade");
        onSaveGame = handler("onSaveGame");
        onUnitComplete = handler("onUnitComplete");
    }

    void handleError(const char *where, py::error_already_set &e, bool alwaysRaise = false)
    {
        std::string message = std::string("Python error in ") + where + ":\n" + formatPythonError(e);
        std::cerr << message << std::endl;
        if (raiseErrors || alwaysRaise) throw std::runtime_error(message);
    }

    template<typename... Args>
    void call(const char *name, const py::object &handler, Args &&...args)
    {
        if (!handler) return;
        try
        {
            handler(std::forward<Args>(args)...);
        }
        catch (py::error_already_set &e)
        {
            handleError(name, e);
        }
    }
};

PythonAIModule::PythonAIModule()
{
    ensureInterpreter();
    py::gil_scoped_acquire gil;
    impl = std::make_unique<Impl>();
}

PythonAIModule::~PythonAIModule()
{
    py::gil_scoped_acquire gil;
    impl.reset();
}

void PythonAIModule::onStart()
{
    py::gil_scoped_acquire gil;
    auto bwapiModule = py::module_::import("bwapi");
    publish_broodwar(bwapiModule);
    impl->call("onStart", impl->onStart);
}

void PythonAIModule::onEnd(bool isWinner)
{
    py::gil_scoped_acquire gil;
    impl->call("onEnd", impl->onEnd, isWinner);
    std::cout << impl->frameStats.summary() << std::endl;
}

void PythonAIModule::onFrame()
{
    if (currentFrame < frameSkip)
    {
        currentFrame++;
        return;
    }

    py::gil_scoped_acquire gil;
    auto start = std::chrono::steady_clock::now();
    impl->call("onFrame", impl->onFrame);
    std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    impl->frameStats.record(elapsed.count());
}

void PythonAIModule::onSendText(std::string text)
{
    py::gil_scoped_acquire gil;
    impl->call("onSendText", impl->onSendText, text);
}

void PythonAIModule::onReceiveText(BWAPI::Player player, std::string text)
{
    py::gil_scoped_acquire gil;
    impl->call("onReceiveText", impl->onReceiveText, player, text);
}

void PythonAIModule::onPlayerLeft(BWAPI::Player player)
{
    py::gil_scoped_acquire gil;
    impl->call("onPlayerLeft", impl->onPlayerLeft, player);
}

void PythonAIModule::onNukeDetect(BWAPI::Position target)
{
    py::gil_scoped_acquire gil;
    impl->call("onNukeDetect", impl->onNukeDetect, target);
}

void PythonAIModule::onUnitDiscover(BWAPI::Unit unit)
{
    py::gil_scoped_acquire gil;
    impl->call("onUnitDiscover", impl->onUnitDiscover, unit);
}

void PythonAIModule::onUnitEvade(BWAPI::Unit unit)
{
    py::gil_scoped_acquire gil;
    impl->call("onUnitEvade", impl->onUnitEvade, unit);
}

void PythonAIModule::onUnitShow(BWAPI::Unit unit)
{
    py::gil_scoped_acquire gil;
    impl->call("onUnitShow", impl->onUnitShow, unit);
}

void PythonAIModule::onUnitHide(BWAPI::Unit unit)
{
    py::gil_scoped_acquire gil;
    impl->call("onUnitHide", impl->onUnitHide, unit);
}

void PythonAIModule::onUnitCreate(BWAPI::Unit unit)
{
    py::gil_scoped_acquire gil;
    impl->call("onUnitCreate", impl->onUnitCreate, unit);
}

void PythonAIModule::onUnitDestroy(BWAPI::Unit unit)
{
    py::gil_scoped_acquire gil;
    impl->call("onUnitDestroy", impl->onUnitDestroy, unit);
}

void PythonAIModule::onUnitMorph(BWAPI::Unit unit)
{
    py::gil_scoped_acquire gil;
    impl->call("onUnitMorph", impl->onUnitMorph, unit);
}

void PythonAIModule::onUnitRenegade(BWAPI::Unit unit)
{
    py::gil_scoped_acquire gil;
    impl->call("onUnitRenegade", impl->onUnitRenegade, unit);
}

void PythonAIModule::onSaveGame(std::string gameName)
{
    py::gil_scoped_acquire gil;
    impl->call("onSaveGame", impl->onSaveGame, gameName);
}

void PythonAIModule::onUnitComplete(BWAPI::Unit unit)
{
    py::gil_scoped_acquire gil;
    impl->call("onUnitComplete", impl->onUnitComplete, unit);
}

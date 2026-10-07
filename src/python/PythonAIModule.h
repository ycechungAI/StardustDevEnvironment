#pragma once

#include <BWAPI.h>

#include <memory>

// BWAPI AIModule that hosts a bot written in Python.
//
// It embeds a CPython interpreter (shared by all games in the process), creates the bot by calling the
// factory named by STARDUST_BOT (default "stardust:create_bot") and forwards every BWAPI callback to the
// bot method of the same name (onStart, onFrame, onUnitCreate, ...) when the bot defines it.
//
// Environment variables:
//   STARDUST_BOT          module:factory that returns the bot object (default stardust:create_bot)
//   STARDUST_PYTHON_PATH  directories prepended to sys.path (default: the repo's python/ directory)
//   VIRTUAL_ENV           virtualenv whose site-packages are made importable (default: the repo's .venv)
//   STARDUST_PY_ERRORS    "raise" (default) turns Python exceptions into C++ exceptions; "log" prints and continues
class PythonAIModule : public BWAPI::AIModule
{
public:
    PythonAIModule();

    ~PythonAIModule() override;

    // Number of frames to skip before the bot starts playing (used by tests that create initial units)
    int frameSkip = 0;

    void onStart() override;
    void onEnd(bool isWinner) override;
    void onFrame() override;
    void onSendText(std::string text) override;
    void onReceiveText(BWAPI::Player player, std::string text) override;
    void onPlayerLeft(BWAPI::Player player) override;
    void onNukeDetect(BWAPI::Position target) override;
    void onUnitDiscover(BWAPI::Unit unit) override;
    void onUnitEvade(BWAPI::Unit unit) override;
    void onUnitShow(BWAPI::Unit unit) override;
    void onUnitHide(BWAPI::Unit unit) override;
    void onUnitCreate(BWAPI::Unit unit) override;
    void onUnitDestroy(BWAPI::Unit unit) override;
    void onUnitMorph(BWAPI::Unit unit) override;
    void onUnitRenegade(BWAPI::Unit unit) override;
    void onSaveGame(std::string gameName) override;
    void onUnitComplete(BWAPI::Unit unit) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    int currentFrame = 0;
};

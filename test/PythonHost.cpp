// Tests for the C++ host of the Python bot. They don't start a game, so they run without the
// StarCraft data files. The bots they load are in python/tests/host_check.py.

#include "gtest/gtest.h"
#include "PythonAIModule.h"

#include <cstdlib>

namespace
{
    // Sets an environment variable for the lifetime of the object
    struct ScopedEnv
    {
        std::string name;

        ScopedEnv(const char *name, const char *value) : name(name) { setenv(name, value, 1); }

        ~ScopedEnv() { unsetenv(name.c_str()); }
    };
}

TEST(PythonHost, CreatesDefaultBot)
{
    // Imports the stardust package, which needs the embedded bwapi and instrumentation modules
    EXPECT_NO_THROW(PythonAIModule());
}

TEST(PythonHost, ForwardsCallbacks)
{
    ScopedEnv bot("STARDUST_BOT", "tests.host_check:create_recording_bot");

    PythonAIModule module;
    module.frameSkip = 2;
    EXPECT_NO_THROW({
        module.onStart();
        module.onSendText("hello");
        module.onNukeDetect(BWAPI::Position(10, 20));
        module.onUnitShow(nullptr);
        module.onFrame();
        module.onFrame();
        module.onFrame();
        module.onEnd(true);  // the bot verifies the calls it received
    });
}

TEST(PythonHost, RaisesPythonErrorsByDefault)
{
    ScopedEnv bot("STARDUST_BOT", "tests.host_check:create_failing_bot");

    PythonAIModule module;
    try
    {
        module.onFrame();
        FAIL() << "expected the Python exception to propagate";
    }
    catch (const std::runtime_error &ex)
    {
        EXPECT_NE(std::string(ex.what()).find("RuntimeError: boom"), std::string::npos) << ex.what();
    }
}

TEST(PythonHost, CanLogPythonErrorsInstead)
{
    ScopedEnv bot("STARDUST_BOT", "tests.host_check:create_failing_bot");
    ScopedEnv errors("STARDUST_PY_ERRORS", "log");

    PythonAIModule module;
    EXPECT_NO_THROW(module.onFrame());
}

TEST(PythonHost, ReportsBotImportErrors)
{
    ScopedEnv bot("STARDUST_BOT", "no_such_module:create_bot");

    EXPECT_THROW(PythonAIModule(), std::runtime_error);
}

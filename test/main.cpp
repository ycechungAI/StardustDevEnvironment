#include <gtest/gtest.h>

#include "BW/BWData.h"

#include <cstdio>
#include <iostream>
#include <unistd.h>

// Replaces gtest_main so that, in builds with OPENBW_ENABLE_UI, the game window runs on the main thread (which
// macOS requires) and the tests run on a second thread. Without the UI this just runs the tests.
int main(int argc, char **argv)
{
    printf("Running main() from %s\n", __FILE__);
    testing::InitGoogleTest(&argc, argv);

    int result = 0;
    BW::sacrificeThreadForUI([&result]
                             {
                                 result = RUN_ALL_TESTS();
                             });

    // Leave without running static destructors: every bot's library is loaded, and some of their globals don't
    // survive it (BunkerBoxer's BWEM map printer writes an image at exit, which crashes, or with its map data in a bad
    // state loops forever and leaves the process running)
    std::cout.flush();
    std::cerr.flush();
    fflush(nullptr);
    _exit(result);
}

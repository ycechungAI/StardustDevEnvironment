#include <gtest/gtest.h>

#include "BW/BWData.h"

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
    return result;
}

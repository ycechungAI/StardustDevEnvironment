cmake_minimum_required(VERSION 3.25)
project(StardustDevEnvironment)

set(CMAKE_CXX_STANDARD 17)

# Keep a parallel build within STARDUST_BUILD_MEMORY_GB: one compile or link takes up to STARDUST_GB_PER_JOB, so at most
# memory / per-job of them run at once (and no more than the cores). Ninja enforces it through job pools; with Makefiles
# pass the job count printed below to -j, as a bare -j starts every compile at once.
set(STARDUST_BUILD_MEMORY_GB 10 CACHE STRING "Most memory a parallel build may use, in GB")
set(STARDUST_GB_PER_JOB 2 CACHE STRING "Most memory one compile or link uses, in GB (OpenBW and the Python bindings are the heaviest)")
cmake_host_system_information(RESULT STARDUST_CORES QUERY NUMBER_OF_LOGICAL_CORES)
math(EXPR STARDUST_BUILD_JOBS "${STARDUST_BUILD_MEMORY_GB} / ${STARDUST_GB_PER_JOB}")
if (STARDUST_BUILD_JOBS LESS 1)
    set(STARDUST_BUILD_JOBS 1)
elseif (STARDUST_BUILD_JOBS GREATER STARDUST_CORES)
    set(STARDUST_BUILD_JOBS ${STARDUST_CORES})
endif ()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS stardust_jobs=${STARDUST_BUILD_JOBS})
set(CMAKE_JOB_POOL_COMPILE stardust_jobs)
set(CMAKE_JOB_POOL_LINK stardust_jobs)
file(WRITE ${CMAKE_BINARY_DIR}/build_jobs.txt "${STARDUST_BUILD_JOBS}\n")  # read by build.sh
if (NOT CMAKE_GENERATOR MATCHES "Ninja")
    message(STATUS "Build with at most -j ${STARDUST_BUILD_JOBS} to stay within ${STARDUST_BUILD_MEMORY_GB} GB "
            "(cmake --build ${CMAKE_BINARY_DIR} -j ${STARDUST_BUILD_JOBS}); a bare -j has no limit")
endif ()

# Vendored zstd and googletest declare very old minimum CMake versions, which CMake 4 rejects
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Build the bot against the repo's virtualenv interpreter when there is one
if (NOT DEFINED Python_EXECUTABLE AND EXISTS ${CMAKE_SOURCE_DIR}/.venv/bin/python)
    set(Python_EXECUTABLE ${CMAKE_SOURCE_DIR}/.venv/bin/python)
endif ()

IF(MSVC)
    add_definitions(-DWITHOUT_POSIX)
    add_subdirectory(3rdparty/BWAPILIB)
ELSE(MSVC)
    set(OPENBW_DIR ${CMAKE_SOURCE_DIR}/3rdparty/openbw/openbw)

    add_subdirectory(3rdparty/openbw/bwapi)
ENDIF(MSVC)

add_subdirectory(3rdparty/zstd/build/cmake)
add_subdirectory(3rdparty/BWEM)
add_subdirectory(3rdparty/FAP)
add_subdirectory(src)

IF(NOT MSVC)
    add_subdirectory(test)
    add_subdirectory(tools/replay)
ENDIF(NOT MSVC)

# Renders the live game window's HUD panel to an image, to check its layout without running a game
add_executable(hud_preview tools/hud_preview.cpp)
target_include_directories(hud_preview PRIVATE ${CMAKE_SOURCE_DIR}/3rdparty/openbw/openbw)

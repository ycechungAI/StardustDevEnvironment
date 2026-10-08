cmake_minimum_required(VERSION 3.25)
project(StardustDevEnvironment)

set(CMAKE_CXX_STANDARD 17)

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
ENDIF(NOT MSVC)

# Renders the live game window's HUD panel to an image, to check its layout without running a game
add_executable(hud_preview tools/hud_preview.cpp)
target_include_directories(hud_preview PRIVATE ${CMAKE_SOURCE_DIR}/3rdparty/openbw/openbw)

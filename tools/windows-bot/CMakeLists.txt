# Builds a bot from bots/<BOT>/ as a BWAPI 4.4 DLL for the real StarCraft 1.16.1 on Windows, to play it outside
# OpenBW, for example against StarCraft's built-in computer players (OpenBW doesn't have them). See README.md here.
#
# Only for bots that need nothing but BWAPI, with an AIModule class and header named after their folder
# (bots/ClaudeOpus55/ClaudeOpus55.h declaring class ClaudeOpus55).
cmake_minimum_required(VERSION 3.16)
project(WindowsBot CXX)

set(BOT ClaudeOpus55 CACHE STRING "Bot folder under bots/, also the name of its AIModule class and header")
set(BWAPI_DIR "$ENV{BWAPI_DIR}" CACHE PATH "BWAPI 4.4.0 install folder, with include/ and lib/")

if (NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "StarCraft 1.16.1 is 32-bit: build with Visual Studio for Win32 (cmake -A Win32)")
endif ()

find_path(BWAPI_INCLUDE_DIR BWAPI.h PATHS "${BWAPI_DIR}/include" NO_DEFAULT_PATH)
find_library(BWAPI_LIBRARY BWAPILIB PATHS "${BWAPI_DIR}/lib" "${BWAPI_DIR}/Release" NO_DEFAULT_PATH)
if (NOT BWAPI_INCLUDE_DIR OR NOT BWAPI_LIBRARY)
    message(FATAL_ERROR "BWAPI 4.4.0 not found in '${BWAPI_DIR}': install it, or pass -DBWAPI_DIR=<its folder>")
endif ()

set(BOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../bots/${BOT}")
file(GLOB_RECURSE BOT_SOURCES CONFIGURE_DEPENDS "${BOT_DIR}/*.cpp")
if (NOT BOT_SOURCES)
    message(FATAL_ERROR "No sources in ${BOT_DIR}")
endif ()

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/bot_config.h"
        "#include \"${BOT}.h\"\n#define BOT_CLASS ${BOT}\n#define BOT_NAME \"${BOT}\"\n")

add_library(${BOT} SHARED dllmain.cpp ${BOT_SOURCES})
target_include_directories(${BOT} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${BOT_DIR}" "${BWAPI_INCLUDE_DIR}")
target_compile_features(${BOT} PRIVATE cxx_std_17)
target_compile_definitions(${BOT} PRIVATE NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
target_compile_options(${BOT} PRIVATE /utf-8)
target_link_libraries(${BOT} PRIVATE "${BWAPI_LIBRARY}")

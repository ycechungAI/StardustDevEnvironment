cmake_minimum_required(VERSION 3.24)
project(tests)

# The opponents (from Stardust's test environment) need C++20
set(CMAKE_CXX_STANDARD 20)

add_subdirectory(3rdparty/googletest)
add_subdirectory(3rdparty/opponents/Steamhammer)
add_subdirectory(3rdparty/opponents/Locutus)
add_subdirectory(3rdparty/opponents/Iron)
add_subdirectory(3rdparty/opponents/McRave)

include_directories(${CMAKE_SOURCE_DIR}/3rdparty/googletest/include)

include_directories(SYSTEM
        ${CMAKE_SOURCE_DIR}/3rdparty/openbw/bwapi/bwapi/Util/Source
        ${CMAKE_SOURCE_DIR}/3rdparty/openbw/bwapi/bwapi/BWAPI/openbw
        ${CMAKE_SOURCE_DIR}/3rdparty/openbw/bwapi/bwapi/BWAPI/openbw/BWAPI
        ${CMAKE_SOURCE_DIR}/3rdparty/openbw/bwapi/bwapi/BWAPI/Source
        ${CMAKE_SOURCE_DIR}/3rdparty/openbw/bwapi/bwapi/BWAPICore
        ${CMAKE_SOURCE_DIR}/3rdparty/openbw/bwapi/bwapi/OpenBWData
        ${CMAKE_SOURCE_DIR}/3rdparty/openbw/bwapi/bwapi/Shared
        ${CMAKE_SOURCE_DIR}/3rdparty/openbw/openbw
)

set(TEST_INCLUDES
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/3rdparty  # nlohmann/json.hpp
        )
include_directories(${TEST_INCLUDES})

file(GLOB_RECURSE SRC_FILES ${PROJECT_SOURCE_DIR} *.cpp)
list(FILTER SRC_FILES EXCLUDE REGEX ".*3rdparty.*")

# Opponent bots: the built-in ones, then every bots/*/bot.cmake (see bots/README.md)
include(${CMAKE_SOURCE_DIR}/bots/StardustBots.cmake)
stardust_register_bot(NAME Steamhammer RACE Zerg TARGET Steamhammer HEADER UAlbertaBotModule.h
                      CREATE "new UAlbertaBot::UAlbertaBotModule()")
stardust_register_bot(NAME Locutus RACE Protoss TARGET Locutus HEADER LocutusBotModule.h
                      CREATE "new Locutus::LocutusBotModule()")
stardust_register_bot(NAME Iron RACE Terran TARGET Iron HEADER Iron.h CREATE "new iron::Iron()")
stardust_register_bot(NAME McRave RACE Protoss TARGET McRave HEADER Header.h CREATE "new McRaveModule()")
stardust_add_bot_folders(${CMAKE_SOURCE_DIR}/bots)
stardust_write_bot_registry(${CMAKE_CURRENT_BINARY_DIR}/BotRegistry.cpp)

add_executable(tests ${SRC_FILES} ${CMAKE_CURRENT_BINARY_DIR}/BotRegistry.cpp)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_definitions(tests PUBLIC DEBUG=1)
endif()

target_link_libraries(tests gtest BWAPI BWAPILIB StardustAI Steamhammer Locutus Iron McRave ${STARDUST_BOT_LIBRARIES})
target_compile_options(tests PRIVATE -Wall -Werror)

file(COPY ${CMAKE_SOURCE_DIR}/test/3rdparty/maps DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bwapi-data/AI)
file(COPY ${CMAKE_SOURCE_DIR}/test/3rdparty/bwta DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/bwapi-data/AI)
file(COPY ${CMAKE_SOURCE_DIR}/test/3rdparty/opponents/Steamhammer/Steamhammer_2.4.2.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/bwapi-data/AI)
file(COPY ${CMAKE_SOURCE_DIR}/test/3rdparty/opponents/Locutus/Locutus.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/bwapi-data/AI)

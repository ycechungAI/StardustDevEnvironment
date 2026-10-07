# Dragon (AIIDE 2021 version), a Terran bot built on Facebook's CherryPi framework. See bots/recipes/Dragon/recipe.json.
# Built the way its own CMake builds it for Windows: the bot's src/ and common/ in one library, with its bundled
# BWEM, gflags, glog and fmt, and without the Torch, ZeroMQ and replay-file parts it doesn't use when playing.

# The bundled gflags, glog and fmt, as static libraries compiled into the bot only
set(dragon_saved_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(GFLAGS_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(GFLAGS_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GFLAGS_BUILD_gflags_LIB ON CACHE BOOL "" FORCE)
set(GFLAGS_BUILD_gflags_nothreads_LIB OFF CACHE BOOL "" FORCE)
set(GFLAGS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GFLAGS_IS_SUBPROJECT ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/3rdparty/gflags ${CMAKE_CURRENT_BINARY_DIR}/gflags EXCLUDE_FROM_ALL)
set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)   # glog's own flag definitions; Dragon's flags still go through gflags
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/3rdparty/glog ${CMAKE_CURRENT_BINARY_DIR}/glog EXCLUDE_FROM_ALL)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/3rdparty/fmt ${CMAKE_CURRENT_BINARY_DIR}/fmt EXCLUDE_FROM_ALL)
foreach (dep gflags_static glog fmt)
    target_compile_options(${dep} PRIVATE -w)
    set_target_properties(${dep} PROPERTIES CXX_STANDARD 17)   # fmt 5 uses std::result_of, gone in C++20
endforeach ()
set(BUILD_SHARED_LIBS ${dragon_saved_shared})
unset(CMAKE_CXX_VISIBILITY_PRESET)
unset(CMAKE_VISIBILITY_INLINES_HIDDEN)

stardust_bot(
        NAME Dragon
        RACE Terran
        SOURCES src/*.cpp common/*.cpp 3rdparty/bwem/*.cpp
        EXCLUDE src/main.cpp src/CMakeFiles/* common/autograd/*
        INCLUDE_DIRS src 3rdparty 3rdparty/include 3rdparty/torchcraft/include 3rdparty/cereal/include
        HEADER dragonbot.h
        CREATE "dragonNewAIModule()"
        CXX_STANDARD 17
        DEFINITIONS NDEBUG C10_USE_GLOG CEREAL_THREAD_SAFE _USE_MATH_DEFINES NOMINMAX _LIBCPP_DISABLE_AVAILABILITY
                    _LIBCPP_ENABLE_CXX17_REMOVED_FEATURES
)
target_link_libraries(bot_Dragon PRIVATE gflags_static glog fmt libzstd_static)
target_include_directories(bot_Dragon SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/3rdparty/zstd/lib)
# Without the harness's own 3rdparty folder: on a case-insensitive disk its BWEM/ answers Dragon's <bwem/...> includes
get_target_property(dragon_includes bot_Dragon INCLUDE_DIRECTORIES)
list(REMOVE_ITEM dragon_includes ${CMAKE_SOURCE_DIR}/3rdparty)
set_target_properties(bot_Dragon PROPERTIES INCLUDE_DIRECTORIES "${dragon_includes}")

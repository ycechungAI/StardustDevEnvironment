cmake_minimum_required(VERSION 3.25)
project(StardustAI)

set(CMAKE_CXX_STANDARD 17)

# pybind11 provides the BWAPI <-> Python bridge. Python is located via FindPython; the top-level
# CMakeLists points it at the repo's .venv when one exists so the bot and the venv share an interpreter.
include(FetchContent)
set(PYBIND11_FINDPYTHON ON)
FetchContent_Declare(pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v3.0.4
        GIT_SHALLOW TRUE
        SYSTEM)
FetchContent_MakeAvailable(pybind11)

include_directories(${CMAKE_SOURCE_DIR}/3rdparty)

# Logging and CherryVis output (plain C++)
file(GLOB INSTRUMENTATION_SRCS ${PROJECT_SOURCE_DIR}/Instrumentation/*.cpp)
add_library(Instrumentation OBJECT ${INSTRUMENTATION_SRCS})
set_target_properties(Instrumentation PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(Instrumentation PUBLIC ${PROJECT_SOURCE_DIR}/Instrumentation)
target_link_libraries(Instrumentation PUBLIC BWAPILIB libzstd_static)

# Python bindings for BWAPI and the instrumentation, compiled once and linked into both the bot (as embedded
# modules) and the offline extension modules. generated/ is produced by tools/gen_bwapi_bindings.py.
file(GLOB BINDING_SRCS
        ${PROJECT_SOURCE_DIR}/python/BWAPIModule.cpp
        ${PROJECT_SOURCE_DIR}/python/InstrumentationModule.cpp
        ${PROJECT_SOURCE_DIR}/python/generated/*.cpp)
add_library(PythonBindings OBJECT ${BINDING_SRCS})
set_target_properties(PythonBindings PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
target_include_directories(PythonBindings PUBLIC ${PROJECT_SOURCE_DIR}/python)
target_link_libraries(PythonBindings PUBLIC pybind11::pybind11 Instrumentation)

# Bindings for the native libraries Stardust keeps in C++: BWEM (map analysis) and FAP (combat simulation).
# FAP needs C++20.
add_library(NativeBindings OBJECT
        ${PROJECT_SOURCE_DIR}/python/BWEMModule.cpp
        ${PROJECT_SOURCE_DIR}/python/FAPModule.cpp)
set_target_properties(NativeBindings PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden CXX_STANDARD 20)
target_include_directories(NativeBindings PUBLIC ${PROJECT_SOURCE_DIR}/python)
target_link_libraries(NativeBindings PUBLIC pybind11::pybind11 BWAPILIB BWEM FAP)

# The bot: a BWAPI AI module that embeds Python and runs the bot in python/stardust
add_library(StardustAI SHARED
        ${PROJECT_SOURCE_DIR}/Dll.cpp
        ${PROJECT_SOURCE_DIR}/python/PythonAIModule.cpp
        ${PROJECT_SOURCE_DIR}/python/EmbeddedModules.cpp)
target_include_directories(StardustAI PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/python
        ${PROJECT_SOURCE_DIR}/Instrumentation)
target_link_libraries(StardustAI
        PUBLIC BWAPILIB libzstd_static
        PRIVATE PythonBindings NativeBindings Instrumentation BWEM pybind11::embed)
target_compile_definitions(StardustAI PRIVATE
        STARDUST_PYTHON_DIR="${CMAKE_SOURCE_DIR}/python"
        STARDUST_VENV_DIR="${CMAKE_SOURCE_DIR}/.venv")

# The binding modules as regular extensions, so they can be imported from plain Python (pytest etc.).
# Built into <build>/python.
foreach (MODULE bwapi instrumentation bwem fap)
    pybind11_add_module(${MODULE}_offline MODULE ${PROJECT_SOURCE_DIR}/python/offline/OfflineModule.cpp)
    set_target_properties(${MODULE}_offline PROPERTIES
            OUTPUT_NAME ${MODULE}
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python)
    target_compile_definitions(${MODULE}_offline PRIVATE
            OFFLINE_MODULE_NAME=${MODULE}
            OFFLINE_MODULE_INIT=init_${MODULE}_module)
    if (MODULE STREQUAL "bwem" OR MODULE STREQUAL "fap")
        target_link_libraries(${MODULE}_offline PRIVATE NativeBindings BWEM)
    else ()
        target_link_libraries(${MODULE}_offline PRIVATE PythonBindings Instrumentation)
    endif ()
endforeach ()

IF (NOT MSVC)
    foreach (TARGET Instrumentation PythonBindings StardustAI)
        target_compile_options(${TARGET} PRIVATE -Wall -Werror)
    endforeach ()
ENDIF (NOT MSVC)

# Replay tools for tools/replays.py: replay_dump reads a replay headlessly; replay_viewer (UI builds only) plays one in
# a window. Both go to <build>/tools.

add_executable(replay_dump replay_dump.cpp)
target_include_directories(replay_dump SYSTEM PRIVATE ${OPENBW_DIR})
target_link_libraries(replay_dump BWAPILIB)
set_target_properties(replay_dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)

if (OPENBW_ENABLE_UI)
    add_executable(replay_viewer replay_viewer.cpp)
    target_include_directories(replay_viewer SYSTEM PRIVATE ${OPENBW_DIR})
    target_compile_definitions(replay_viewer PRIVATE OPENBW_ENABLE_UI)
    target_link_libraries(replay_viewer openbw_ui)
    set_target_properties(replay_viewer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)
endif ()

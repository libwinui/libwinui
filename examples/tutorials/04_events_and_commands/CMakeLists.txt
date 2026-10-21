# SPDX-License-Identifier: MIT
# Tutorial: events and commands.
add_executable(tutorial_04_events_and_commands WIN32 main.cpp)
target_link_libraries(tutorial_04_events_and_commands PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_04_events_and_commands PRIVATE /utf-8)
endif()

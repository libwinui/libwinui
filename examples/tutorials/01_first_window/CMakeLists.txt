# SPDX-License-Identifier: MIT
# Tutorial: your first window.
add_executable(tutorial_01_first_window WIN32 main.cpp)
target_link_libraries(tutorial_01_first_window PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_01_first_window PRIVATE /utf-8)
endif()

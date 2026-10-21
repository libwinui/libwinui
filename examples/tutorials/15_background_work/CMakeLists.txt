# SPDX-License-Identifier: MIT
# Tutorial: background work and the UI thread.
add_executable(tutorial_15_background_work WIN32 main.cpp)
target_link_libraries(tutorial_15_background_work PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_15_background_work PRIVATE /utf-8)
endif()

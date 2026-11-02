# SPDX-License-Identifier: MIT
# Tutorial: lists.
add_executable(tutorial_07_lists WIN32 main.cpp)
target_link_libraries(tutorial_07_lists PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_07_lists PRIVATE /utf-8)
endif()

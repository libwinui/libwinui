# SPDX-License-Identifier: MIT
# Tutorial: layout.
add_executable(tutorial_02_layout WIN32 main.cpp)
target_link_libraries(tutorial_02_layout PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_02_layout PRIVATE /utf-8)
endif()

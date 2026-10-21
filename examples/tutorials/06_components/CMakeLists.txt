# SPDX-License-Identifier: MIT
# Tutorial: components.
add_executable(tutorial_06_components WIN32 main.cpp)
target_link_libraries(tutorial_06_components PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_06_components PRIVATE /utf-8)
endif()

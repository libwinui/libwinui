# SPDX-License-Identifier: MIT
# Tutorial: mixing imperative and declarative.
add_executable(tutorial_13_mixing_imperative_declarative WIN32 main.cpp)
target_link_libraries(tutorial_13_mixing_imperative_declarative PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_13_mixing_imperative_declarative PRIVATE /utf-8)
endif()

# SPDX-License-Identifier: MIT
# Tutorial: a form.
add_executable(tutorial_05_a_form WIN32 main.cpp)
target_link_libraries(tutorial_05_a_form PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_05_a_form PRIVATE /utf-8)
endif()

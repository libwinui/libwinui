# SPDX-License-Identifier: MIT
# Tutorial: a form built from data.
add_executable(tutorial_16_form_from_data WIN32 main.cpp)
target_link_libraries(tutorial_16_form_from_data PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_16_form_from_data PRIVATE /utf-8)
endif()

# SPDX-License-Identifier: MIT
# Tutorial: your own element.
add_executable(tutorial_11_your_own_element WIN32 main.cpp)
target_link_libraries(tutorial_11_your_own_element PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_11_your_own_element PRIVATE /utf-8)
endif()

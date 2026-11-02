# SPDX-License-Identifier: MIT
# Tutorial: the virtual list.
add_executable(tutorial_08_virtual_list WIN32 main.cpp)
target_link_libraries(tutorial_08_virtual_list PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_08_virtual_list PRIVATE /utf-8)
endif()

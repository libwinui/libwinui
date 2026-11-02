# SPDX-License-Identifier: MIT
# Added by the top-level CMakeLists only where a backend exists (WIN32 GUI subsystem).
add_executable(virtual_list_demo WIN32 main.cpp)
target_link_libraries(virtual_list_demo PRIVATE winui::main)

if(MSVC)
    target_compile_options(virtual_list_demo PRIVATE /utf-8)
endif()

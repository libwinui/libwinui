# SPDX-License-Identifier: MIT
# Added by the top-level CMakeLists only where a backend exists (WIN32 GUI subsystem).
add_executable(tile_view_demo WIN32 main.cpp)
target_link_libraries(tile_view_demo PRIVATE winui::main)

if(MSVC)
    target_compile_options(tile_view_demo PRIVATE /utf-8)
endif()

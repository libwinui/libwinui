# SPDX-License-Identifier: MIT
# Added by the top-level CMakeLists only where a backend exists (WIN32 GUI subsystem).
add_executable(tray WIN32 main.cpp)
target_link_libraries(tray PRIVATE winui::main)

if(MSVC)
    target_compile_options(tray PRIVATE /utf-8)
endif()

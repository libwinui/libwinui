# SPDX-License-Identifier: MIT
# Window states and native chrome. Added by the top-level CMakeLists only where a backend
# exists (WIN32 GUI subsystem).
add_executable(window_chrome WIN32 main.cpp)
target_link_libraries(window_chrome PRIVATE winui::main)

if(MSVC)
    target_compile_options(window_chrome PRIVATE /utf-8)
endif()

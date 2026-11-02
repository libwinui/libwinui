# SPDX-License-Identifier: MIT
# Added by the top-level CMakeLists only where a backend exists (WIN32 GUI subsystem).
add_executable(hello WIN32 main.cpp)
target_link_libraries(hello PRIVATE winui::main)

if(MSVC)
    target_compile_options(hello PRIVATE /utf-8)
endif()

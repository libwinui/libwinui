# SPDX-License-Identifier: MIT
# Tutorial: state and bindings.
add_executable(tutorial_03_state_and_bindings WIN32 main.cpp)
target_link_libraries(tutorial_03_state_and_bindings PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_03_state_and_bindings PRIVATE /utf-8)
endif()

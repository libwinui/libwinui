# SPDX-License-Identifier: MIT
# Tutorial: hosting native controls.
add_executable(tutorial_12_hosting_native_controls WIN32 main.cpp)
target_link_libraries(tutorial_12_hosting_native_controls PRIVATE winui::main example_native_controls)

if(MSVC)
    target_compile_options(tutorial_12_hosting_native_controls PRIVATE /utf-8)
endif()

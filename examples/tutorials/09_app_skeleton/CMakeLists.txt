# SPDX-License-Identifier: MIT
# Tutorial: an app skeleton.
add_executable(tutorial_09_app_skeleton WIN32 main.cpp)
target_link_libraries(tutorial_09_app_skeleton PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_09_app_skeleton PRIVATE /utf-8)
endif()

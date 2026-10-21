# SPDX-License-Identifier: MIT
# Tutorial: dialogs, flyouts and menus.
add_executable(tutorial_10_dialogs_flyouts_menus WIN32 main.cpp)
target_link_libraries(tutorial_10_dialogs_flyouts_menus PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_10_dialogs_flyouts_menus PRIVATE /utf-8)
endif()

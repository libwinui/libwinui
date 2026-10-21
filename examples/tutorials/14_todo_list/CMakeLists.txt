# SPDX-License-Identifier: MIT
# Tutorial: a todo list.
add_executable(tutorial_14_todo_list WIN32 main.cpp)
target_link_libraries(tutorial_14_todo_list PRIVATE winui::main)

if(MSVC)
    target_compile_options(tutorial_14_todo_list PRIVATE /utf-8)
endif()

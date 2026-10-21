# SPDX-License-Identifier: MIT
# A project outside the libwinui tree that finds an installed libwinui the way an app does.
# CI installs the library, configures this against the install prefix, builds both programs
# and runs the headless one.
cmake_minimum_required(VERSION 3.21)
project(winui_package_check LANGUAGES CXX)

find_package(winui CONFIG REQUIRED COMPONENTS testing)

# An app: links winui::main, which also gives it a plain int main() as a WIN32 executable.
add_executable(package_app WIN32 app.cpp)
target_link_libraries(package_app PRIVATE winui::main)

# A UI test: links winui::testing and runs without a window on screen.
add_executable(package_headless headless.cpp)
target_link_libraries(package_headless PRIVATE winui::testing)

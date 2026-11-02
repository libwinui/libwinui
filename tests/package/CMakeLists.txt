# SPDX-License-Identifier: MIT
# A project outside the libwinui tree that finds an installed libwinui the way an app does.
# CI installs the library, configures this against the install prefix, builds the programs
# and runs the headless ones.
cmake_minimum_required(VERSION 3.28)

# A package whose modules use import std hands that on: CMake 4.4 offers it only behind this
# experimental gate, which has to be open before project() (CMake 4.5 has it without).
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.4 AND CMAKE_VERSION VERSION_LESS 4.5)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
endif()

project(winui_package_check LANGUAGES CXX)

find_package(winui CONFIG REQUIRED COMPONENTS testing)

# An app on the headers: winui::headers_main gives it a plain int main() as a WIN32 executable.
add_executable(package_app WIN32 app.cpp)
target_link_libraries(package_app PRIVATE winui::headers_main)

# A UI test: links winui::testing_headers and runs without a window on screen.
add_executable(package_headless headless.cpp)
target_link_libraries(package_headless PRIVATE winui::testing_headers)

# The same UI test through `import winui.testing;`, when the package has the modules. The 3.28
# minimum turns on the module scanning an import needs.
if(TARGET winui::testing)
    add_executable(package_headless_module headless.cpp)
    target_link_libraries(package_headless_module PRIVATE winui::testing)
    # A package whose modules use import std: MSVC needs the std module's object linked in too
    get_target_property(winui_module_std winui::winui CXX_MODULE_STD)
    if(winui_module_std)
        set_target_properties(package_headless_module PROPERTIES CXX_MODULE_STD ON)
    endif()
    target_compile_definitions(package_headless_module PRIVATE PACKAGE_USE_MODULES)
endif()

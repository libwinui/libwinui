# SPDX-License-Identifier: MIT
# Development tools, in plain C++ with no dependencies:
#
#   winui_check_snippets  checks the gallery's `.code` snippets against the specs (ctest: gallery_snippets)
#   winui_api_surface     writes or checks api/surface*.txt from Doxygen's XML (targets api-surface, api-check)
#   winui_doc_examples    turns the headers' @code examples into sources the build compiles (cmake/DocExamples.cmake)
#
# Built with the library when libwinui is the top-level project, or on its own, which is how CI's
# checks job builds them: cmake -S tools -B build-tools && cmake --build build-tools
cmake_minimum_required(VERSION 3.28)
project(libwinui_tools LANGUAGES CXX)

add_executable(winui_check_snippets check_snippets.cpp)
add_executable(winui_api_surface api_surface.cpp)
add_executable(winui_doc_examples doc_examples.cpp)

foreach(tool IN ITEMS winui_check_snippets winui_api_surface winui_doc_examples)
    set_target_properties(${tool} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(COMMAND winui_set_warnings)
        winui_set_warnings(${tool})
    endif()
endforeach()

if(NOT PROJECT_IS_TOP_LEVEL AND LIBWINUI_BUILD_TESTS)
    add_test(NAME gallery_snippets COMMAND winui_check_snippets "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()

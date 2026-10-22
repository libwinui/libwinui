# SPDX-License-Identifier: MIT
# The C++ modules, opt-in with LIBWINUI_BUILD_MODULES:
#
#   winui::module          `import winui;`, over winui::winui
#   winui::testing_module  `import winui.testing;`, over winui::testing, with winui re-exported
#
# modules/*.cppm are generated with api/surface*.txt by tools/api_surface.cpp (the api-surface
# target) and checked by api-check. They include the headers in the global module fragment, so a
# module build and a header build of the same app link the same library.
#
# A target that imports a module needs module scanning on. The top-level CMakeLists.txt turns it
# off for libwinui's other targets, so each target here turns it on. A consumer whose own minimum is
# 3.28 or newer has it on by default.

function(winui_add_module target export_name file library)
    add_library(${target} STATIC)
    add_library(winui::${export_name} ALIAS ${target})
    target_sources(${target}
        PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS ${PROJECT_SOURCE_DIR}/modules
            FILES ${PROJECT_SOURCE_DIR}/modules/${file})
    target_link_libraries(${target} PUBLIC ${library})
    set_target_properties(${target} PROPERTIES
        EXPORT_NAME ${export_name}
        OUTPUT_NAME ${target}
        DEBUG_POSTFIX d
        CXX_EXTENSIONS OFF
        CXX_SCAN_FOR_MODULES ON
        POSITION_INDEPENDENT_CODE ON)
    if(COMMAND winui_set_warnings)
        winui_set_warnings(${target})
    endif()
    # CMake passes clang-cl -TP next to the module flags, which make it unused: an error under /WX.
    if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target} PRIVATE -Wno-unused-command-line-argument)
    endif()
endfunction()

winui_add_module(winui_module module winui.cppm winui::winui)
winui_add_module(winui_testing_module testing_module winui.testing.cppm winui::testing)
target_link_libraries(winui_testing_module PUBLIC winui_module)

# winui_add_module_tutorial(<tutorial target> <main.cpp>): the same tutorial again as
# <target>_module, built from a copy of main.cpp that imports winui instead of including it. The
# import goes after the last #include, as the standard headers have to come first.
function(winui_add_module_tutorial tutorial source)
    file(READ ${source} code)
    string(REPLACE "#include <winui/winui.hpp>\n" "" code "${code}")
    string(REGEX MATCHALL "#include [<\"][^\n]*\n" includes "${code}")
    if(includes)
        list(GET includes -1 last)
        string(FIND "${code}" "${last}" at REVERSE)
        string(LENGTH "${last}" length)
        math(EXPR at "${at} + ${length}")
        string(SUBSTRING "${code}" 0 ${at} head)
        string(SUBSTRING "${code}" ${at} -1 tail)
        set(code "${head}import winui;\n${tail}")
    else()
        set(code "import winui;\n${code}")
    endif()
    # Rewritten only when it changed, so a configure doesn't rebuild every copy.
    set(copy ${CMAKE_CURRENT_BINARY_DIR}/module_tutorials/${tutorial}.cpp)
    file(WRITE ${copy}.new "${code}")
    file(COPY_FILE ${copy}.new ${copy} ONLY_IF_DIFFERENT)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
    add_executable(${tutorial}_module WIN32 ${copy})
    target_link_libraries(${tutorial}_module PRIVATE winui::main winui::module)
    set_target_properties(${tutorial}_module PROPERTIES CXX_SCAN_FOR_MODULES ON CXX_EXTENSIONS OFF)
    if(COMMAND winui_set_warnings)
        winui_set_warnings(${tutorial}_module)
    endif()
endfunction()

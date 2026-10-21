# SPDX-License-Identifier: MIT
# clang-tidy, after cpp-best-practices/cmake_template's StaticAnalyzers.cmake. The checks are in
# .clang-tidy at the repository root.
#
#   tidy                     target: runs clang-tidy over every translation unit of libwinui and
#                            its tests (run-clang-tidy, in parallel), from compile_commands.json
#   LIBWINUI_ENABLE_CLANG_TIDY  option: runs clang-tidy on every file as it is compiled instead,
#                            which makes each compile slower but reports where you work
#
# Both need a clang-tidy (LLVM 17 or newer for the checks used); LIBWINUI_CLANG_TIDY_EXECUTABLE
# overrides the one found on PATH.

option(LIBWINUI_ENABLE_CLANG_TIDY "Run clang-tidy on every file as it is compiled" OFF)
set(LIBWINUI_TIDY_JOBS 0 CACHE STRING "Parallel clang-tidy processes for the tidy target; 0 is one per core")

find_program(LIBWINUI_CLANG_TIDY_EXECUTABLE clang-tidy
             HINTS "$ENV{ProgramFiles}/LLVM/bin")

if(LIBWINUI_STANDALONE)
    # What the tidy target, clangd and most editors read.
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if(LIBWINUI_ENABLE_CLANG_TIDY)
    if(LIBWINUI_CLANG_TIDY_EXECUTABLE)
        set(LIBWINUI_CLANG_TIDY_COMMAND
            "${LIBWINUI_CLANG_TIDY_EXECUTABLE}"
            # Flags clang does not know (MSVC's /w14xxx, GCC's -Wlogical-op) are not findings.
            --extra-arg=-Wno-unknown-warning-option
            --extra-arg=-Wno-unused-command-line-argument)
        if(LIBWINUI_WARNINGS_AS_ERRORS)
            list(APPEND LIBWINUI_CLANG_TIDY_COMMAND --warnings-as-errors=*)
        endif()
    else()
        message(WARNING "libwinui: LIBWINUI_ENABLE_CLANG_TIDY is on, but clang-tidy was not found")
    endif()
endif()

# CXX_CLANG_TIDY on one of our own targets, when LIBWINUI_ENABLE_CLANG_TIDY asks for it.
function(winui_enable_clang_tidy target)
    if(LIBWINUI_CLANG_TIDY_COMMAND)
        set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${LIBWINUI_CLANG_TIDY_COMMAND}")
    endif()
endfunction()

if(LIBWINUI_STANDALONE AND LIBWINUI_CLANG_TIDY_EXECUTABLE)
    get_filename_component(winui_llvm_bin "${LIBWINUI_CLANG_TIDY_EXECUTABLE}" DIRECTORY)
    find_program(LIBWINUI_RUN_CLANG_TIDY NAMES run-clang-tidy run-clang-tidy.py HINTS "${winui_llvm_bin}")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(LIBWINUI_RUN_CLANG_TIDY AND Python3_Interpreter_FOUND)
        # Only our own sources: the _deps (Catch2, stb) and the generated C++/WinRT headers are
        # not ours to fix. Header findings are filtered by .clang-tidy's HeaderFilterRegex.
        add_custom_target(tidy
            COMMAND "${Python3_EXECUTABLE}" "${LIBWINUI_RUN_CLANG_TIDY}"
                    -clang-tidy-binary "${LIBWINUI_CLANG_TIDY_EXECUTABLE}"
                    -p "${CMAKE_BINARY_DIR}"
                    -quiet
                    $<$<BOOL:${LIBWINUI_TIDY_JOBS}>:-j=${LIBWINUI_TIDY_JOBS}>
                    -extra-arg=-Wno-unknown-warning-option
                    -extra-arg=-Wno-unused-command-line-argument
                    "^(?!.*[/\\\\]_deps[/\\\\]).*[/\\\\](src|tests|examples)[/\\\\].*\\.cpp$"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Running clang-tidy over libwinui"
            USES_TERMINAL
            VERBATIM)
    else()
        message(STATUS "libwinui: tidy target unavailable (needs run-clang-tidy next to clang-tidy and Python 3)")
    endif()
endif()

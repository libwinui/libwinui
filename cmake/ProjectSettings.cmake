# SPDX-License-Identifier: MIT
# Project-wide settings, after cpp-best-practices/cmake_template's PreventInSourceBuilds,
# StandardProjectSettings, InterproceduralOptimization and Hardening modules.
#
#   LIBWINUI_ENABLE_IPO        link-time optimization for the optimized configurations
#   LIBWINUI_ENABLE_HARDENING  the compiler's and linker's exploit mitigations on our own targets
#                           (winui_enable_hardening(<target>))

# An in-source build litters the tree with CMake's files and makes a clean checkout impossible.
file(REAL_PATH "${CMAKE_SOURCE_DIR}" winui_source_dir)
file(REAL_PATH "${CMAKE_BINARY_DIR}" winui_binary_dir)
if(winui_source_dir STREQUAL winui_binary_dir)
    message(FATAL_ERROR "libwinui: in-source builds are not supported; configure into a build directory "
                        "(cmake -S . -B build) and delete the CMakeCache.txt and CMakeFiles/ just created here.")
endif()

if(LIBWINUI_STANDALONE)
    # A single-config generator with no build type builds neither optimized nor debuggable code.
    get_property(winui_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(NOT winui_multi_config AND NOT CMAKE_BUILD_TYPE)
        message(STATUS "libwinui: no CMAKE_BUILD_TYPE given, using RelWithDebInfo")
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build." FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
    endif()
endif()

option(LIBWINUI_ENABLE_IPO "Enable link-time optimization for the optimized configurations" OFF)
if(LIBWINUI_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT winui_ipo_supported OUTPUT winui_ipo_output)
    if(winui_ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
    else()
        message(WARNING "libwinui: LIBWINUI_ENABLE_IPO is on, but the toolchain cannot do it: ${winui_ipo_output}")
    endif()
endif()

option(LIBWINUI_ENABLE_HARDENING "Build with the compiler's and linker's exploit mitigations" OFF)

# The mitigations on one of our own targets, when LIBWINUI_ENABLE_HARDENING asks for them.
function(winui_enable_hardening target)
    if(NOT LIBWINUI_ENABLE_HARDENING)
        return()
    endif()
    if(MSVC)
        # /DYNAMICBASE and /NXCOMPAT are the linker's defaults already.
        target_compile_options(${target} PRIVATE /sdl /guard:cf)
        get_target_property(type ${target} TYPE)
        if(type STREQUAL "EXECUTABLE" OR type STREQUAL "SHARED_LIBRARY")
            target_link_options(${target} PRIVATE /guard:cf /CETCOMPAT)
        endif()
    else()
        include(CheckCXXCompilerFlag)
        target_compile_definitions(${target} PRIVATE _GLIBCXX_ASSERTIONS)
        target_compile_options(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3>)
        foreach(flag -fstack-protector-strong -fcf-protection -fstack-clash-protection)
            string(MAKE_C_IDENTIFIER "LIBWINUI_HAS${flag}" var)
            check_cxx_compiler_flag(${flag} ${var})
            if(${var})
                target_compile_options(${target} PRIVATE ${flag})
            endif()
        endforeach()
    endif()
endfunction()

# ---- build speed ----
#   LIBWINUI_ENABLE_UNITY_BUILD  compiles the sources of each of our targets in batches of
#                             LIBWINUI_UNITY_BUILD_BATCH_SIZE files per translation unit
#   LIBWINUI_ENABLE_PCH          precompiles the standard headers every source file pulls in
option(LIBWINUI_ENABLE_UNITY_BUILD "Compile our sources as unity batches" OFF)
set(LIBWINUI_UNITY_BUILD_BATCH_SIZE 16 CACHE STRING "Source files per unity translation unit")
option(LIBWINUI_ENABLE_PCH "Precompile the standard headers" OFF)

# Only the standard library: it is most of every translation unit (<chrono> and <filesystem> alone
# are about 80k preprocessed lines under MSVC) and never changes, so the header is built once.
# Our own headers stay out, or every edit to one would rebuild the precompiled header.
set(LIBWINUI_PCH_HEADERS
    <algorithm> <array> <chrono> <cmath> <cstddef> <cstdint> <filesystem> <functional> <map>
    <memory> <optional> <span> <string> <string_view> <unordered_map> <utility> <variant> <vector>)

# Unity batching and the precompiled header on one of our own targets, when the options ask.
# `PCH_ONLY` leaves unity batching off: the tests' files each keep small helpers of the same name
# (click(), down(), make()), which is what a test file should look like.
function(winui_enable_build_speedups target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "PCH_ONLY" "" "")
    if(LIBWINUI_ENABLE_UNITY_BUILD AND NOT arg_PCH_ONLY)
        set_target_properties(${target} PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_BATCH_SIZE ${LIBWINUI_UNITY_BUILD_BATCH_SIZE})
        # The generated animated icon sources each define the same tables of easing curves.
        get_target_property(sources ${target} SOURCES)
        list(FILTER sources INCLUDE REGEX "Animated[A-Za-z]+VisualSource\.cpp$")
        set_source_files_properties(${sources} TARGET_DIRECTORY ${target} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endif()
    if(LIBWINUI_ENABLE_PCH)
        target_precompile_headers(${target} PRIVATE ${LIBWINUI_PCH_HEADERS})
    endif()
endfunction()

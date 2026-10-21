# SPDX-License-Identifier: MIT
# Catch2 for the tests, built once per machine and compiler and shared by every build tree and
# worktree, instead of 107 more objects in each tree's clean build.
#
#   LIBWINUI_SHARED_CATCH2  use (and on first configure, build) the shared copy           (default ON)
#   LIBWINUI_CATCH2_ROOT    where shared copies live            (default %LOCALAPPDATA%/libwinui)
#
# The shared copy is keyed by compiler and version, built Debug and Release with C++23 and the
# default /MD runtime, which is what every tree here uses. Anything that has to match flags the
# copy doesn't have falls back to fetching Catch2 into the tree as before: AddressSanitizer
# (MSVC's vector annotations must match across every object), compilers without the MSVC ABI and
# single-config generators.

set(LIBWINUI_CATCH2_VERSION 3.6.0)
option(LIBWINUI_SHARED_CATCH2 "Use one Catch2 build shared by every build tree on this machine" ON)
set(LIBWINUI_CATCH2_ROOT "$ENV{LOCALAPPDATA}/libwinui" CACHE PATH "Where the shared Catch2 builds live")

get_property(winui_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(LIBWINUI_SHARED_CATCH2 AND MSVC AND winui_multi_config AND NOT LIBWINUI_ENABLE_ASAN
   AND NOT CMAKE_MSVC_RUNTIME_LIBRARY AND NOT "$ENV{LOCALAPPDATA}" STREQUAL "")
    string(MAKE_C_IDENTIFIER "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}" winui_catch2_key)
    # The trailing revision changes whenever the way the copy is built changes, so an older copy is
    # never reused.
    set(winui_catch2_dir "${LIBWINUI_CATCH2_ROOT}/catch2-${LIBWINUI_CATCH2_VERSION}-${winui_catch2_key}-r3")
    set(winui_catch2_prefix "${winui_catch2_dir}/install")

    # Two trees configuring at once (worktrees) build it once: the second waits for the lock and
    # then finds the install.
    file(MAKE_DIRECTORY "${winui_catch2_dir}")
    file(LOCK "${winui_catch2_dir}/.lock" GUARD FILE TIMEOUT 1800)
    if(NOT EXISTS "${winui_catch2_prefix}/.complete")
        message(STATUS "Building the shared Catch2 ${LIBWINUI_CATCH2_VERSION} into ${winui_catch2_dir}")
        file(REMOVE_RECURSE "${winui_catch2_dir}/src" "${winui_catch2_dir}/build" "${winui_catch2_prefix}")
        find_package(Git REQUIRED)
        # Quiet unless a step fails; then its whole output goes into the error.
        function(winui_catch2_step)
            execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "Building the shared Catch2 failed:\n${out}")
            endif()
        endfunction()
        winui_catch2_step("${GIT_EXECUTABLE}" clone --depth 1 --branch v${LIBWINUI_CATCH2_VERSION}
                          https://github.com/catchorg/Catch2.git "${winui_catch2_dir}/src")
        winui_catch2_step("${CMAKE_COMMAND}" -S "${winui_catch2_dir}/src" -B "${winui_catch2_dir}/build"
                          -G "${CMAKE_GENERATOR}" "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                          -DCMAKE_CXX_STANDARD=23 -DCMAKE_CXX_STANDARD_REQUIRED=ON
                          -DBUILD_TESTING=OFF -DCATCH_INSTALL_DOCS=OFF -DCATCH_INSTALL_EXTRAS=ON
                          "-DCMAKE_INSTALL_PREFIX=${winui_catch2_prefix}")
        foreach(config Debug Release)
            winui_catch2_step("${CMAKE_COMMAND}" --build "${winui_catch2_dir}/build" --config ${config} --parallel 4)
            winui_catch2_step("${CMAKE_COMMAND}" --install "${winui_catch2_dir}/build" --config ${config})
        endforeach()
        # The build folder stays: the Debug .lib points the linker at the .pdb files in it.
        file(REMOVE_RECURSE "${winui_catch2_dir}/src")
        file(TOUCH "${winui_catch2_prefix}/.complete")
    endif()
    file(LOCK "${winui_catch2_dir}/.lock" RELEASE)

    find_package(Catch2 ${LIBWINUI_CATCH2_VERSION} EXACT CONFIG REQUIRED
                 PATHS "${winui_catch2_prefix}" NO_DEFAULT_PATH)
    # Only Debug and Release are built; the other optimized configs use the same /MD runtime.
    foreach(target Catch2::Catch2 Catch2::Catch2WithMain)
        set_target_properties(${target} PROPERTIES
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
            MAP_IMPORTED_CONFIG_MINSIZEREL     Release)
    endforeach()
    # The install puts Catch.cmake (catch_discover_tests) next to its package config.
    set(LIBWINUI_CATCH2_CMAKE_DIR "${Catch2_DIR}")
    message(STATUS "Using the shared Catch2 in ${winui_catch2_prefix}")
else()
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v${LIBWINUI_CATCH2_VERSION}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    # Catch2 has to be compiled with the standard the tests use: its headers declare more (the
    # std::string_view support, for one) from C++17 on, and the library has to define it.
    target_compile_features(Catch2 PRIVATE cxx_std_23)
    target_compile_features(Catch2WithMain PRIVATE cxx_std_23)
    set(LIBWINUI_CATCH2_CMAKE_DIR "${catch2_SOURCE_DIR}/extras")
endif()

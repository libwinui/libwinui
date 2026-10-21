# SPDX-License-Identifier: MIT
# Sanitizers, after cpp-best-practices/cmake_template's Sanitizers.cmake.
#
#   LIBWINUI_ENABLE_ASAN    AddressSanitizer (MSVC, clang-cl, Clang, GCC)
#   LIBWINUI_ENABLE_UBSAN   UndefinedBehaviorSanitizer (Clang and GCC, not on Windows)
#
# Applied directory-wide, before any target is created, so libwinui, Catch2, the tests and the
# examples are all instrumented alike: mixing instrumented and plain objects is what produces
# false container-overflow reports.

option(LIBWINUI_ENABLE_ASAN  "Build with AddressSanitizer"            OFF)
option(LIBWINUI_ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)

set(LIBWINUI_SANITIZERS "")
if(LIBWINUI_ENABLE_ASAN)
    list(APPEND LIBWINUI_SANITIZERS address)
endif()
if(LIBWINUI_ENABLE_UBSAN)
    if(MSVC OR WIN32)
        message(WARNING "libwinui: UndefinedBehaviorSanitizer is not available on Windows; ignoring LIBWINUI_ENABLE_UBSAN")
    else()
        list(APPEND LIBWINUI_SANITIZERS undefined)
    endif()
endif()

if(LIBWINUI_SANITIZERS)
    list(JOIN LIBWINUI_SANITIZERS "," winui_sanitizer_list)
    if(MSVC)
        # cl and clang-cl both take /fsanitize=address. Incremental linking and /RTC are
        # incompatible with it. Use Debug or RelWithDebInfo: the reports need the PDBs.
        add_compile_options(/fsanitize=${winui_sanitizer_list})
        add_link_options(/INCREMENTAL:NO)
        string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
            # clang-cl does not link the runtime on its own.
            add_link_options(/INFERASANLIBS)
        endif()
        # The runtime is a DLL next to cl.exe; winui_copy_asan_runtime() puts it next to an
        # executable, so the tests (and catch_discover_tests, which runs them at build time) start.
        get_filename_component(winui_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        # /MDd links the debug flavor of the runtime, /MD the release one.
        file(GLOB LIBWINUI_ASAN_RUNTIME
             "${winui_cl_dir}/clang_rt.asan_dynamic-x86_64.dll"
             "${winui_cl_dir}/clang_rt.asan_dbg_dynamic-x86_64.dll"
             "${winui_cl_dir}/../lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
        if(NOT LIBWINUI_ASAN_RUNTIME)
            message(WARNING "libwinui: clang_rt.asan_dynamic-x86_64.dll not found next to the compiler; "
                            "put its folder on PATH to run instrumented executables")
        endif()
    else()
        add_compile_options(-fsanitize=${winui_sanitizer_list} -fno-omit-frame-pointer)
        add_link_options(-fsanitize=${winui_sanitizer_list})
    endif()
    message(STATUS "libwinui: sanitizers: ${winui_sanitizer_list}")
endif()

# Copies the ASan runtime DLLs next to `target` after every build, when there are any to copy.
function(winui_copy_asan_runtime target)
    if(LIBWINUI_ASAN_RUNTIME)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBWINUI_ASAN_RUNTIME} "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()

# SPDX-License-Identifier: MIT
# Phase 8's measurements: winui_bench (timings, written as JSON), winui_startup (startup
# milestones), winui_timers (what after()/every() cost on a real window) and winui_idle (what each
# gallery page costs while nobody touches it). None is run by ctest;
# the counts that must not regress are the [perf-count] tests in winui_tests. Numbers mean something from an optimized build (RelWithDebInfo, Release).
add_executable(winui_bench
    main.cpp
    Offscreen.cpp
    Controls.cpp
    Lists.cpp
    Gallery.cpp
    Memory.cpp
    ${PROJECT_SOURCE_DIR}/tests/support/AllocationCounter.cpp)
target_link_libraries(winui_bench PRIVATE winui::testing gallery_pages)
target_compile_definitions(winui_bench PRIVATE LIBWINUI_BENCH_CONFIG="$<CONFIG>")

add_executable(winui_startup Startup.cpp)
target_link_libraries(winui_startup PRIVATE winui::testing gallery_pages)

add_executable(winui_timers Timers.cpp)
target_link_libraries(winui_timers PRIVATE winui::testing)

add_executable(winui_idle Idle.cpp)
target_link_libraries(winui_idle PRIVATE winui::testing gallery_pages)

foreach(target IN ITEMS winui_bench winui_startup winui_timers winui_idle)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    winui_set_warnings(${target})
    winui_copy_asan_runtime(${target})
    winui_enable_hardening(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endforeach()

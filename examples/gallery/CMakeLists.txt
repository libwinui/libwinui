# SPDX-License-Identifier: MIT
# Added by the top-level CMakeLists only where a backend exists. One source file per
# navigation group, except Motion, whose pages are split across pages_motion*.cpp to keep
# each translation unit's compile time down. The pages are a library of their own, so winui_bench
# (benchmarks/) can build them without a window.
add_library(
    gallery_pages STATIC
    gallery.cpp
    page_about.cpp
    pages_menus_and_toolbars.cpp
    pages_collections.cpp
    pages_basic_input.cpp
    pages_date_and_time.cpp
    pages_status_and_info.cpp
    pages_dialogs_and_flyouts.cpp
    pages_scrolling.cpp
    pages_layout.cpp
    pages_navigation.cpp
    pages_media.cpp
    pages_styles.cpp
    pages_text.cpp
    pages_motion.cpp
    pages_motion_connected.cpp
    pages_motion_connected_cards.cpp
    pages_motion_easing.cpp
    pages_motion_implicit.cpp
    pages_motion_theme.cpp
    pages_windowing.cpp
    pages_native.cpp
    pages_system.cpp)
target_link_libraries(gallery_pages PUBLIC winui::winui example_native_controls)
target_include_directories(gallery_pages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    # /bigobj: an optimized build of a page file outgrows the default section limit.
    target_compile_options(gallery_pages PRIVATE /utf-8 /bigobj)
endif()

# The libwinui logo as the program icon
set(GALLERY_ICON "${CMAKE_CURRENT_SOURCE_DIR}/../../docs/assets/libwinui.ico")
configure_file(gallery.rc.in "${CMAKE_CURRENT_BINARY_DIR}/gallery.rc" @ONLY)
add_executable(gallery WIN32 main.cpp "${CMAKE_CURRENT_BINARY_DIR}/gallery.rc")
# The resource compiler doesn't tell which files it read
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/gallery.rc" PROPERTIES OBJECT_DEPENDS "${GALLERY_ICON}")
target_link_libraries(gallery PRIVATE winui::main gallery_pages)
if(MSVC)
    target_compile_options(gallery PRIVATE /utf-8)
endif()

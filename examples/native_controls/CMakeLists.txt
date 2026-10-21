# SPDX-License-Identifier: MIT
# Example code, not part of libwinui: two controls that host an operating system window class
# (EDIT and RICHEDIT50W) through ui::NativeControl, the library's extension point for that. The
# gallery and the "Hosting native controls" tutorial use them, and the tests exercise
# ui::NativeControl through them. Windows only.
add_library(example_native_controls STATIC edit_box.cpp rich_edit_box.cpp)
target_include_directories(example_native_controls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(example_native_controls PUBLIC winui::winui)
target_compile_definitions(example_native_controls PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(example_native_controls PRIVATE /utf-8)
endif()

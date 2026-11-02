# SPDX-License-Identifier: MIT
# Example code, not part of libwinui: two controls that host an operating system window class
# (EDIT and RICHEDIT50W) through ui::NativeControl, the library's extension point for that. The
# gallery and the "Hosting native controls" tutorial use them, and the tests exercise
# ui::NativeControl through them. Windows only.
set(native_controls_files edit_box.cpp edit_box.hpp rich_edit_box.cpp rich_edit_box.hpp strings.hpp)

function(native_controls_library target link)
    add_library(${target} STATIC ${ARGN})
    target_link_libraries(${target} PUBLIC ${link})
    target_compile_definitions(${target} PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()

# The examples import the module.
if(LIBWINUI_BUILD_MODULES AND LIBWINUI_BUILD_EXAMPLES)
    native_controls_library(example_native_controls winui::winui edit_box.cpp rich_edit_box.cpp)
    target_include_directories(example_native_controls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# The tests use the headers, so they get a copy that includes <winui/winui.hpp> instead.
if(LIBWINUI_BUILD_TESTS)
    set(copy_dir ${CMAKE_CURRENT_BINARY_DIR}/headers)
    foreach(file IN LISTS native_controls_files)
        file(READ ${file} code)
        string(REPLACE "import winui;" "#include <winui/winui.hpp>" code "${code}")
        # Rewritten only when it changed, so a configure doesn't rebuild the copy.
        file(WRITE ${copy_dir}/${file}.new "${code}")
        file(COPY_FILE ${copy_dir}/${file}.new ${copy_dir}/${file} ONLY_IF_DIFFERENT)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${file})
    endforeach()
    native_controls_library(example_native_controls_headers winui::headers
                            ${copy_dir}/edit_box.cpp ${copy_dir}/rich_edit_box.cpp)
    target_include_directories(example_native_controls_headers PUBLIC ${copy_dir})
endif()

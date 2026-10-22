# SPDX-License-Identifier: MIT
include(${PROJECT_SOURCE_DIR}/cmake/Catch2.cmake)

add_executable(winui_tests
    test_geometry.cpp
    test_render.cpp
    test_bevel.cpp
    test_focus_rect.cpp
    test_focus_scope.cpp
    test_element_state.cpp
    test_open_popup.cpp
    test_automation_providers.cpp
    test_generic_events.cpp
    test_button.cpp
    test_check_box.cpp
    test_choices.cpp
    test_control_links.cpp
    test_radio_button.cpp
    test_radio_buttons.cpp
    test_toggle_switch.cpp
    test_strings.cpp
    test_toggle_button.cpp
    test_split_button.cpp
    test_cursor.cpp
    test_slider.cpp
    test_combo_box_read_only.cpp
    test_combo_box.cpp
    test_control_header.cpp
    test_auto_suggest_box.cpp
    test_number_box.cpp
    test_date_format.cpp
    test_pips_pager.cpp
    test_sections.cpp
    test_pager.cpp
    test_carousel.cpp
    test_scroll_control.cpp
    test_rating.cpp
    test_calendar_view.cpp
    test_calendar_date_picker.cpp
    test_date_picker.cpp
    test_time_picker.cpp
    test_progress_bar.cpp
    test_progress_ring.cpp
    test_info_bar.cpp
    test_info_badge.cpp
    test_text_box.cpp
    test_spell_check.cpp
    test_rich_text.cpp
    test_text_wrapping.cpp
    test_text_layout.cpp
    test_unicode_text.cpp
    test_text.cpp
    test_password_box.cpp
    test_rich_edit_box.cpp
    test_theme.cpp
    test_theme_colors.cpp
    test_classic_palette.cpp
    test_layout.cpp
    test_stack.cpp
    test_grid.cpp
    test_sizing.cpp
    test_splitter.cpp
    test_enabled.cpp
    test_resize_gripper.cpp
    test_visual_tree.cpp
    test_children.cpp
    test_automation.cpp
    test_automation_events.cpp
    test_printing.cpp
    test_scroll.cpp
    test_annotated_scroll_bar.cpp
    test_parallax.cpp
    test_list_view.cpp
    test_table_view.cpp
    test_tree_view.cpp
    test_expander.cpp
    test_group_box.cpp
    test_two_pane.cpp
    test_swipe.cpp
    test_pull_to_refresh.cpp
    test_selector_bar.cpp
    test_breadcrumb.cpp
    test_title_bar.cpp
    test_window_frame.cpp
    test_window_state.cpp
    test_timed_waits.cpp
    test_window_tick.cpp
    test_win32_window.cpp
    test_color_picker.cpp
    test_icon_slots.cpp
    test_image.cpp
    test_animated_visual_player.cpp
    test_animated_icon.cpp
    test_person_picture.cpp
    test_ink_canvas.cpp
    test_ink_toolbar.cpp
    test_toolbar.cpp
    test_menu_bar.cpp
    test_context_menu.cpp
    test_menu_flyout.cpp
    test_command_bar.cpp
    test_command.cpp
    test_shortcuts.cpp
    test_flyout.cpp
    test_tool_tip.cpp
    test_dialog.cpp
    test_popup.cpp
    test_animation.cpp
    test_navigation_frame.cpp
    test_layout_panels.cpp
    test_for_each.cpp
    test_list_view_base.cpp
    test_shapes.cpp
    test_paint.cpp
    test_styles.cpp
    test_side_pane.cpp
    test_teaching_tip.cpp
    test_tabs.cpp
    test_navigation_shell.cpp
    test_composition.cpp
    test_layers.cpp
    test_native_control.cpp
    test_edit_box.cpp
    test_app_loop.cpp
    test_clipboard.cpp
    test_launcher.cpp
    test_message_box.cpp
    test_external_drop.cpp
    test_jump_list.cpp
    test_notification.cpp
    test_single_instance.cpp
    test_storage_pickers.cpp
    test_tray.cpp
    test_bitmap.cpp
    test_image_decode.cpp
    test_command_line.cpp
    test_golden.cpp
    test_scheduler.cpp
    test_thread_check.cpp
    test_coroutine.cpp
    test_parallel_layout.cpp
    test_property.cpp
    test_computed.cpp
    test_observable_vector.cpp
    test_dynamic.cpp
    test_retire.cpp
    test_views.cpp
    test_perf_count.cpp
    support/AllocationCounter.cpp
    support/EraThemes.cpp
    test_spec.cpp
    test_windows_h_first.cpp)

target_link_libraries(winui_tests PRIVATE winui::testing Catch2::Catch2WithMain)
set_target_properties(winui_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    # /bigobj: test_spec.cpp builds every spec, more sections than the default object format holds.
    set_source_files_properties(test_spec.cpp PROPERTIES COMPILE_OPTIONS /bigobj)
endif()
# The backend's own classes (Win32Clipboard, Win32SpellChecker, ...) are private headers under
# src/; the tests of the backend include them from there.
target_include_directories(winui_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
if(TARGET example_native_controls)
    # ui::NativeControl is tested through the example controls that host EDIT and RICHEDIT.
    target_link_libraries(winui_tests PRIVATE example_native_controls)
endif()
target_compile_definitions(winui_tests PRIVATE
    LIBWINUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

winui_set_warnings(winui_tests)
winui_copy_asan_runtime(winui_tests)
winui_enable_clang_tidy(winui_tests)
winui_enable_hardening(winui_tests)
winui_enable_build_speedups(winui_tests PCH_ONLY)

list(APPEND CMAKE_MODULE_PATH ${LIBWINUI_CATCH2_CMAKE_DIR})
include(Catch)
# A test case that ends in SKIP() exits with 4; ctest reports it as skipped, not failed.
catch_discover_tests(winui_tests PROPERTIES SKIP_RETURN_CODE 4)

# The thread checks compiled out, in every configuration. Not linked to the library on purpose: a
# call into the check machinery left behind would be an unresolved symbol. See the file.
add_executable(winui_thread_checks_off thread_checks_off.cpp)
target_include_directories(winui_thread_checks_off PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_features(winui_thread_checks_off PRIVATE cxx_std_23)
target_compile_definitions(winui_thread_checks_off PRIVATE LIBWINUI_THREAD_CHECKS=0)
target_link_libraries(winui_thread_checks_off PRIVATE Threads::Threads)
set_target_properties(winui_thread_checks_off PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(winui_thread_checks_off PRIVATE /utf-8)
endif()
winui_set_warnings(winui_thread_checks_off)
add_test(NAME thread_checks_off COMMAND winui_thread_checks_off)

# The C++ modules: a headless UI built through `import winui.testing;` alone. See the file.
if(TARGET winui::testing_module)
    add_executable(winui_module_import module_import.cpp)
    target_link_libraries(winui_module_import PRIVATE winui::testing_module)
    set_target_properties(winui_module_import PROPERTIES CXX_SCAN_FOR_MODULES ON CXX_EXTENSIONS OFF)
    winui_set_warnings(winui_module_import)
    add_test(NAME module_import COMMAND winui_module_import)
endif()

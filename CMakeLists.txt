# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.21)

project(libwinui
    VERSION 0.1.0
    DESCRIPTION "A native, data-oriented, testable Windows UI framework (vertical slice 1)"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(LIBWINUI_BUILD_TESTS    "Build libwinui unit tests"      ON)
option(LIBWINUI_BUILD_EXAMPLES "Build libwinui example apps"    ON)
option(LIBWINUI_BUILD_SHARED   "Build the shared library too"   OFF)
# The debug thread-affinity checks (include/winui/core/ThreadCheck.hpp). AUTO follows the build
# type (on without NDEBUG); ON and OFF force them for the library and everything linking it.
set(LIBWINUI_THREAD_CHECKS AUTO CACHE STRING "Thread-affinity checks: AUTO, ON or OFF")
set_property(CACHE LIBWINUI_THREAD_CHECKS PROPERTY STRINGS AUTO ON OFF)
# TraceLogging spans for layout, render, present, tick and startup (src/core/Trace.hpp), so
# Windows Performance Analyzer shows a frame. Off: the spans compile to nothing.
option(LIBWINUI_ENABLE_TRACING "Emit TraceLogging events from the library" OFF)

# include/winui/core/Version.hpp spells the version out for the preprocessor; it has to say the
# same as project(VERSION) above.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/include/winui/core/Version.hpp winui_version_defines
     REGEX "^#define LIBWINUI_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(winui_version_part IN ITEMS MAJOR MINOR PATCH)
    set(winui_header_${winui_version_part} "")
    foreach(winui_version_line IN LISTS winui_version_defines)
        if(winui_version_line MATCHES "^#define LIBWINUI_VERSION_${winui_version_part} ([0-9]+)$")
            set(winui_header_${winui_version_part} ${CMAKE_MATCH_1})
        endif()
    endforeach()
endforeach()
set(winui_header_version "${winui_header_MAJOR}.${winui_header_MINOR}.${winui_header_PATCH}")
if(NOT winui_header_version STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR "include/winui/core/Version.hpp says ${winui_header_version}, project(VERSION) says ${PROJECT_VERSION}")
endif()

# Standalone vs. add_subdirectory() consumer.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(LIBWINUI_STANDALONE ON)
else()
    set(LIBWINUI_STANDALONE OFF)
endif()

if(LIBWINUI_STANDALONE AND NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

include(GNUInstallDirs)
include(FetchContent)
include(cmake/ProjectSettings.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/StaticAnalyzers.cmake)

# The job system uses std::jthread. GCC and Clang need -pthread linked explicitly.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Third-party: stb_image / stb_image_write, for golden PNGs and Image::decode() off Windows.
# The repo ships no CMakeLists, so we only add its include dir.
# ---------------------------------------------------------------------------
# The commit's archive, since a shallow clone only reaches a commit while it is still master's tip.
FetchContent_Declare(
    stb
    URL      https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.zip
    URL_HASH SHA256=8e59f72b0780690cda64726804269f638a3be77b9d1506ea95f443f7964bccf0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(stb)
set(LIBWINUI_STB_INCLUDE_DIR "${stb_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# C++/WinRT (Windows only). Pinned here instead of using whatever projection headers the
# installed SDK ships; cppwinrt.exe generates them once at configure time.
# ---------------------------------------------------------------------------
if(WIN32)
    FetchContent_Declare(
        cppwinrt_nuget
        URL "https://www.nuget.org/api/v2/package/Microsoft.Windows.CppWinRT/3.0.260818.1"
        URL_HASH SHA256=a8993608ae9263a8288e1a1d9ee2684a5632f4acc59e1e89ddd86b3b889a7367
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(cppwinrt_nuget)

    set(LIBWINUI_CPPWINRT_EXE "${cppwinrt_nuget_SOURCE_DIR}/bin/cppwinrt.exe")
    set(LIBWINUI_CPPWINRT_GEN_DIR "${CMAKE_BINARY_DIR}/cppwinrt_generated")

    # Windows.UI.ViewManagement for the OS accent ramp, Windows.System for the launcher.
    if(NOT EXISTS "${LIBWINUI_CPPWINRT_GEN_DIR}/winrt/Windows.UI.ViewManagement.h" OR
       NOT EXISTS "${LIBWINUI_CPPWINRT_GEN_DIR}/winrt/Windows.System.h")
        message(STATUS "libwinui: generating C++/WinRT projection headers (Windows.UI.ViewManagement, Windows.System)")
        execute_process(
            COMMAND "${LIBWINUI_CPPWINRT_EXE}" -input sdk -output "${LIBWINUI_CPPWINRT_GEN_DIR}"
                    -include Windows.Foundation -include Windows.UI.ViewManagement -include Windows.System
            RESULT_VARIABLE LIBWINUI_CPPWINRT_GEN_RESULT
            OUTPUT_VARIABLE LIBWINUI_CPPWINRT_GEN_OUTPUT
            ERROR_VARIABLE LIBWINUI_CPPWINRT_GEN_OUTPUT)
        if(NOT LIBWINUI_CPPWINRT_GEN_RESULT EQUAL 0)
            message(FATAL_ERROR "cppwinrt.exe projection generation failed:\n${LIBWINUI_CPPWINRT_GEN_OUTPUT}")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
# Platform-independent core: compiles and is tested on any OS.
set(LIBWINUI_CORE_SOURCES
    src/core/Version.cpp
    src/core/ObservableVector.cpp
    src/core/VisualId.cpp
    src/core/PerfCounters.cpp
    src/core/Trace.cpp
    src/core/Scheduler.cpp
    src/core/ThreadCheck.cpp
    src/ui/Coroutine.cpp
    src/core/Utf8.cpp
    src/core/Utf8Path.cpp
    src/core/CommandLine.cpp
    src/core/DateFormat.cpp
    src/core/NumberFormat.cpp
    src/core/Strings.cpp
    src/core/Input.cpp
    src/ui/input/Button.cpp
    src/ui/input/ToggleButton.cpp
    src/ui/input/DropdownButton.cpp
    src/ui/input/SplitButton.cpp
    src/ui/input/CheckBox.cpp
    src/ui/input/RadioButton.cpp
    src/ui/input/RadioButtons.cpp
    src/ui/input/RadioGroup.cpp
    src/ui/input/ToggleSwitch.cpp
    src/ui/input/Slider.cpp
    src/ui/input/ColorPicker.cpp
    src/ui/input/ComboBox.cpp
    src/ui/ControlHeader.cpp
    src/ui/text/AutoSuggestBox.cpp
    src/ui/text/NumberBox.cpp
    src/ui/navigation/PipsPager.cpp
    src/ui/navigation/Sections.cpp
    src/ui/navigation/Pager.cpp
    src/ui/navigation/Carousel.cpp
    src/ui/input/Rating.cpp
    src/ui/datetime/CalendarView.cpp
    src/ui/datetime/CalendarDatePicker.cpp
    src/ui/datetime/DatePicker.cpp
    src/ui/datetime/PickerEngine.cpp
    src/ui/datetime/TimePicker.cpp
    src/ui/status/ProgressBar.cpp
    src/ui/status/ProgressRing.cpp
    src/ui/status/InfoBar.cpp
    src/ui/status/InfoBadge.cpp
    src/ui/text/Text.cpp
    src/ui/text/Inlines.cpp
    src/ui/media/Icon.cpp
    src/ui/text/TextBox.cpp
    src/ui/text/PasswordBox.cpp
    src/ui/text/RichEditBox.cpp
    src/ui/text/Rtf.cpp
    src/ui/text/RichText.cpp
    src/ui/panels/Stack.cpp
    src/ui/panels/Grid.cpp
    src/ui/panels/Panel.cpp
    src/ui/containers/Border.cpp
    src/ui/media/Shape.cpp
    src/ui/panels/Canvas.cpp
    src/ui/containers/DensityScope.cpp
    src/ui/containers/ScaleToFit.cpp
    src/ui/containers/Shortcuts.cpp
    src/ui/panels/WrapPanel.cpp
    src/ui/panels/TilePanel.cpp
    src/ui/panels/RelativePanel.cpp
    src/ui/DragDrop.cpp
    src/ui/layouts/Layout.cpp
    src/ui/layouts/FlowLayoutAlgorithm.cpp
    src/ui/layouts/StackLayout.cpp
    src/ui/layouts/TileLayout.cpp
    src/ui/layouts/WrapLayout.cpp
    src/ui/layouts/LinedFlowLayout.cpp
    src/ui/panels/LayoutPanel.cpp
    src/ui/collections/ForEach.cpp
    src/ui/panes/SidePane.cpp
    src/ui/panes/Splitter.cpp
    src/ui/panes/ResizeGripper.cpp
    src/ui/scrolling/ScrollBarOverlay.cpp
    src/ui/scrolling/Scroll.cpp
    src/ui/scrolling/AnnotatedScrollBar.cpp
    src/ui/scrolling/Parallax.cpp
    src/ui/collections/VirtualListControl.cpp
    src/ui/collections/ListViewBase.cpp
    src/ui/collections/TableView.cpp
    src/ui/collections/TreeView.cpp
    src/ui/containers/Expander.cpp
    src/ui/containers/GroupBox.cpp
    src/ui/panes/TwoPane.cpp
    src/ui/scrolling/Swipe.cpp
    src/ui/scrolling/PullToRefresh.cpp
    src/ui/navigation/SelectorBar.cpp
    src/ui/navigation/Breadcrumb.cpp
    src/ui/hosting/TitleBar.cpp
    src/ui/media/Image.cpp
    src/ui/media/AnimatedVisualPlayer.cpp
    src/ui/media/AnimatedIcon.cpp
    src/ui/media/AnimatedBackVisualSource.cpp
    src/ui/media/AnimatedChevronDownSmallVisualSource.cpp
    src/ui/media/AnimatedChevronRightDownSmallVisualSource.cpp
    src/ui/media/AnimatedChevronUpDownSmallVisualSource.cpp
    src/ui/media/AnimatedFindVisualSource.cpp
    src/ui/media/AnimatedGlobalNavigationButtonVisualSource.cpp
    src/ui/media/AnimatedSettingsVisualSource.cpp
    src/ui/media/PersonPicture.cpp
    src/ui/media/InkRender.cpp
    src/ui/media/InkStencils.cpp
    src/ui/media/InkCanvas.cpp
    src/ui/media/InkToolbar.cpp
    src/ui/containers/Toolbar.cpp
    src/ui/menus/ContextMenu.cpp
    src/ui/menus/MenuBar.cpp
    src/ui/menus/MenuFlyout.cpp
    src/ui/menus/MenuItem.cpp
    src/ui/menus/MenuPresenter.cpp
    src/ui/Command.cpp
    src/ui/menus/AppBarButton.cpp
    src/ui/menus/CommandBar.cpp
    src/ui/menus/CommandBarFlyout.cpp
    src/ui/dialogs/Flyout.cpp
    src/ui/dialogs/ToolTip.cpp
    src/ui/dialogs/Dialog.cpp
    src/ui/dialogs/Popup.cpp
    src/ui/dialogs/TeachingTip.cpp
    src/ui/navigation/Tabs.cpp
    src/ui/navigation/NavigationShell.cpp
    src/ui/hosting/NativeControl.cpp
    src/ui/hosting/ContentHost.cpp
    src/ui/ExitGhosts.cpp
    src/ui/VisualTree.cpp
    src/ui/AutomationTree.cpp
    src/ui/LayoutSnapshot.cpp
    src/ui/themes/Theme.cpp
    src/ui/themes/ControlChrome.cpp
    src/ui/themes/Palette.cpp
    src/ui/themes/Mdl2Palette.cpp
    src/ui/themes/MetroPalette.cpp
    src/ui/themes/ThemeSource.cpp
    src/ui/themes/ThemeMdl2.cpp
    src/ui/themes/ThemeMetro.cpp
    src/ui/themes/ClassicPalette.cpp
    src/ui/themes/ThemeEarly.cpp
    src/ui/themes/LunaWindowFrame.cpp
    src/ui/themes/AeroWindowFrame.cpp
    src/ui/themes/MdlWindowFrame.cpp
    src/ui/themes/EarlyPalette.cpp
    src/ui/Window.cpp
    src/ui/PaintTracker.cpp
    src/ui/WindowFrame.cpp
    src/ui/WindowRef.cpp
    src/ui/TrayIcon.cpp
    src/ui/Element.cpp
    src/ui/Animation.cpp
    src/ui/navigation/NavigationFrame.cpp
    src/ui/ConnectedAnimation.cpp
    src/ui/collections/ListView.cpp
    src/ui/collections/TileView.cpp
    src/ui/hosting/DragArea.cpp
    src/ui/panels/Fill.cpp
    src/ui/panels/Spacer.cpp
    src/ui/spec/Common.cpp
    src/ui/spec/Dynamic.cpp
    src/render/Bitmap.cpp
    src/render/SvgImage.cpp
    src/render/ImageSource.cpp
    src/render/PathGeometry.cpp
    src/render/Brush.cpp
    src/render/IconSource.cpp
    src/render/Describe.cpp
    src/render/RenderRecording.cpp
    src/ui/Launcher.cpp
    src/ui/Printing.cpp
    src/ui/Clipboard.cpp
    src/ui/JumpList.cpp
    src/ui/MessageBox.cpp
    src/ui/Notification.cpp
    src/render/TextLayout.cpp
    src/render/TextAnalyzer.cpp
    src/render/Bevel.cpp
    src/render/Chrome.cpp
    src/render/FocusRing.cpp
    src/render/StbImpl.cpp)

# Windows backend: only added on Windows.
set(LIBWINUI_WIN32_SOURCES
    src/platform/windows/Win32WindowHost.cpp
    src/platform/windows/DWriteCache.cpp
    src/platform/windows/Win32Icon.cpp
    src/platform/windows/Win32Taskbar.cpp
    src/platform/windows/Win32ThemeSource.cpp
    src/platform/windows/DCompCompositor.cpp
    src/platform/windows/Win32NativeHost.cpp
    src/platform/windows/Win32AutomationProvider.cpp
    src/platform/windows/ShellDialogs.cpp
    src/platform/windows/Tray.cpp
    src/platform/windows/WicDecoder.cpp
    src/platform/windows/Win32Launcher.cpp
    src/platform/windows/Win32Printing.cpp
    src/platform/windows/Win32PrintPreview.cpp
    src/platform/windows/Win32Clipboard.cpp
    src/platform/windows/Win32AppIdentity.cpp
    src/platform/windows/Win32JumpList.cpp
    src/platform/windows/Win32MessageBox.cpp
    src/platform/windows/Win32MessageWindow.cpp
    src/platform/windows/Win32Notifier.cpp
    src/platform/windows/Win32SingleInstance.cpp
    src/platform/windows/Win32SpellChecker.cpp
    src/platform/windows/Win32TextAnalyzer.cpp
    src/platform/windows/Win32ContextMenuHost.cpp
    src/platform/windows/DarkMode.cpp)

set(LIBWINUI_SOURCES ${LIBWINUI_CORE_SOURCES})
if(WIN32)
    list(APPEND LIBWINUI_SOURCES ${LIBWINUI_WIN32_SOURCES})
else()
    list(APPEND LIBWINUI_SOURCES src/platform/NoBackend.cpp)
endif()

# The testing tier (winui::testing): include/winui/testing/ and testing.hpp. A library of its own, so
# an app never links the test doubles.
set(LIBWINUI_TESTING_SOURCES
    src/testing/TestRenderer.cpp
    src/testing/HeadlessUI.cpp
    src/testing/OffscreenCapture.cpp
    src/testing/PerfCounters.cpp)

# ---------------------------------------------------------------------------
# A helper that applies common settings to a library target.
# ---------------------------------------------------------------------------
function(winui_configure_target target)
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    # C++23 is a requirement of the target, so whatever links it is compiled as C++23 too; no
    # directory-wide CMAKE_CXX_STANDARD.
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(${target} PUBLIC Threads::Threads)
    # PUBLIC: Property and Element are laid out differently with the checks, so whatever links the
    # library must agree with it.
    if(LIBWINUI_THREAD_CHECKS STREQUAL "ON")
        target_compile_definitions(${target} PUBLIC LIBWINUI_THREAD_CHECKS=1)
    elseif(LIBWINUI_THREAD_CHECKS STREQUAL "OFF")
        target_compile_definitions(${target} PUBLIC LIBWINUI_THREAD_CHECKS=0)
    endif()
    if(LIBWINUI_ENABLE_TRACING)
        target_compile_definitions(${target} PRIVATE LIBWINUI_ENABLE_TRACING=1)
    endif()

    # stb headers (PNG/JPEG codecs). Build-only: the implementation is compiled into this
    # library, so consumers never need stb on their include path.
    target_include_directories(${target} SYSTEM PRIVATE ${LIBWINUI_STB_INCLUDE_DIR})

    if(WIN32)
        # d3d11/dxgi/dcomp for the compositor overlay visuals, dwmapi and windowsapp for the
        # accent color, uiautomationcore for UI Automation, comctl32 for SetWindowSubclass.
        target_link_libraries(${target} PUBLIC d2d1 dxguid dwrite user32 gdi32 ole32 shell32 comctl32 windowscodecs d3d11 dxgi dcomp comdlg32 prntvpt winspool dwmapi windowsapp uiautomationcore shcore shlwapi uxtheme advapi32 imm32 wtsapi32 version)

        target_include_directories(${target} SYSTEM PRIVATE ${LIBWINUI_CPPWINRT_GEN_DIR})
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE
                                                     WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()

    winui_set_warnings(${target})
    winui_enable_clang_tidy(${target})
    winui_enable_hardening(${target})
    winui_enable_build_speedups(${target})
    if(MSVC)
        # PUBLIC, so a consumer's "ä" is UTF-8 too: every string in the API is UTF-8, and
        # core/Utf8.hpp turns a build without the flag into a compile error.
        target_compile_options(${target} PUBLIC /utf-8)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Static library (the canonical target: winui::winui)
# ---------------------------------------------------------------------------
add_library(winui STATIC ${LIBWINUI_SOURCES})
add_library(winui::winui ALIAS winui)
winui_configure_target(winui)
set_target_properties(winui PROPERTIES
    OUTPUT_NAME winui
    DEBUG_POSTFIX d
    POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Shared library (optional). WINDOWS_EXPORT_ALL_SYMBOLS, so no per-symbol dllexport.
# There is no ABI promise: the headers are mostly inline and controls hold their state by value, so
# a binary only works with the headers, compiler and options it was built with. The soname is
# therefore the full version, never just the major one.
# ---------------------------------------------------------------------------
if(LIBWINUI_BUILD_SHARED)
    add_library(winui_shared SHARED ${LIBWINUI_SOURCES})
    add_library(winui::winui_shared ALIAS winui_shared)
    winui_configure_target(winui_shared)
    # The import library gets a name of its own: winui.lib is the static library's.
    set_target_properties(winui_shared PROPERTIES
        OUTPUT_NAME winui
        ARCHIVE_OUTPUT_NAME winui_shared
        DEBUG_POSTFIX d
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        VERSION ${PROJECT_VERSION})
endif()

# ---------------------------------------------------------------------------
# winui::testing: the testing tier, opt-in. OffscreenCapture draws through the backend's private
# D2DRenderer, so this target (and only this one) sees src/ privately; nothing of it leaks into the
# installed headers.
# ---------------------------------------------------------------------------
function(winui_configure_testing_target target library)
    winui_configure_target(${target})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${target} PUBLIC ${library})
endfunction()

add_library(winui_testing STATIC ${LIBWINUI_TESTING_SOURCES})
add_library(winui::testing ALIAS winui_testing)
winui_configure_testing_target(winui_testing winui::winui)
set_target_properties(winui_testing PROPERTIES
    OUTPUT_NAME winui_testing
    EXPORT_NAME testing
    DEBUG_POSTFIX d
    POSITION_INDEPENDENT_CODE ON)

if(LIBWINUI_BUILD_SHARED)
    add_library(winui_testing_shared SHARED ${LIBWINUI_TESTING_SOURCES})
    add_library(winui::testing_shared ALIAS winui_testing_shared)
    winui_configure_testing_target(winui_testing_shared winui::winui_shared)
    set_target_properties(winui_testing_shared PROPERTIES
        OUTPUT_NAME winui_testing
        ARCHIVE_OUTPUT_NAME winui_testing_shared
        EXPORT_NAME testing_shared
        DEBUG_POSTFIX d
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        VERSION ${PROJECT_VERSION})
endif()

# ---------------------------------------------------------------------------
# winui::main: the one target an app links. It pulls in winui::winui and lets a WIN32 app use a
# plain `int main()`. winui::main_shared is the same over winui::winui_shared.
# ---------------------------------------------------------------------------
# Separate from winui::winui: the tests bring Catch2's main, and a DLL has no such entry point.
function(winui_add_main target export_name library)
    add_library(${target} INTERFACE)
    add_library(winui::${export_name} ALIAS ${target})
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${export_name})
    target_link_libraries(${target} INTERFACE ${library})
    if(WIN32 AND (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
        target_link_options(${target} INTERFACE "LINKER:/ENTRY:mainCRTStartup")
    endif()
endfunction()

winui_add_main(winui_main main winui::winui)
if(LIBWINUI_BUILD_SHARED)
    winui_add_main(winui_main_shared main_shared winui::winui_shared)
endif()

# ---------------------------------------------------------------------------
# Example controls the tests and the examples share (hosting an OS window class)
# ---------------------------------------------------------------------------
if(WIN32 AND LIBWINUI_STANDALONE AND (LIBWINUI_BUILD_TESTS OR LIBWINUI_BUILD_EXAMPLES))
    add_subdirectory(examples/native_controls)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(LIBWINUI_BUILD_TESTS AND LIBWINUI_STANDALONE)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Development tools (tools/): the gallery snippet check, the API surface writer
# ---------------------------------------------------------------------------
if(LIBWINUI_STANDALONE)
    add_subdirectory(tools)
    if(LIBWINUI_BUILD_TESTS)
        include(cmake/DocExamples.cmake)
    endif()
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
# Only where a backend exists: an example has no platform guards of its own.
if(LIBWINUI_BUILD_EXAMPLES AND LIBWINUI_STANDALONE AND WIN32)
    add_subdirectory(examples/hello)
    add_subdirectory(examples/virtual_list_demo)
    add_subdirectory(examples/tile_view_demo)
    add_subdirectory(examples/gallery)
    add_subdirectory(examples/window_chrome)
    add_subdirectory(examples/tray)
    # One project per tutorial page (docs/tutorials/), so the code the site shows compiles.
    add_subdirectory(examples/tutorials/01_first_window)
    add_subdirectory(examples/tutorials/02_layout)
    add_subdirectory(examples/tutorials/03_state_and_bindings)
    add_subdirectory(examples/tutorials/04_events_and_commands)
    add_subdirectory(examples/tutorials/05_a_form)
    add_subdirectory(examples/tutorials/06_components)
    add_subdirectory(examples/tutorials/07_lists)
    add_subdirectory(examples/tutorials/08_virtual_list)
    add_subdirectory(examples/tutorials/09_app_skeleton)
    add_subdirectory(examples/tutorials/10_dialogs_flyouts_menus)
    add_subdirectory(examples/tutorials/11_your_own_element)
    add_subdirectory(examples/tutorials/12_hosting_native_controls)
    add_subdirectory(examples/tutorials/13_mixing_imperative_declarative)
    add_subdirectory(examples/tutorials/14_todo_list)
    add_subdirectory(examples/tutorials/15_background_work)
    add_subdirectory(examples/tutorials/16_form_from_data)
endif()

# ---------------------------------------------------------------------------
# Benchmarks (benchmarks/): timings and startup milestones, never run by ctest. They build the
# gallery's pages, so they need the examples.
# ---------------------------------------------------------------------------
if(LIBWINUI_BUILD_TESTS AND LIBWINUI_BUILD_EXAMPLES AND LIBWINUI_STANDALONE AND WIN32)
    add_subdirectory(benchmarks)
endif()

# Every example target gets the library's warning set too.
if(LIBWINUI_STANDALONE AND WIN32 AND (LIBWINUI_BUILD_TESTS OR LIBWINUI_BUILD_EXAMPLES))
    get_property(winui_subdirs DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY SUBDIRECTORIES)
    foreach(winui_subdir IN LISTS winui_subdirs)
        if(winui_subdir MATCHES "/examples/")
            winui_configure_directory_targets(${winui_subdir})
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Install / export
# ---------------------------------------------------------------------------
install(TARGETS winui winui_main EXPORT winuiTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(LIBWINUI_BUILD_SHARED)
    install(TARGETS winui_shared winui_main_shared EXPORT winuiTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
# The testing tier in an export set of its own, the package's `testing` component:
# find_package(winui CONFIG REQUIRED COMPONENTS testing) gets winui::testing.
install(TARGETS winui_testing EXPORT winuiTestingTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(LIBWINUI_BUILD_SHARED)
    install(TARGETS winui_testing_shared EXPORT winuiTestingTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# An app that links the library has to carry both notices.
install(FILES LICENSE THIRD-PARTY-NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/winui)
install(EXPORT winuiTargets
    FILE winuiTargets.cmake
    NAMESPACE winui::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/winui)
install(EXPORT winuiTestingTargets
    FILE winuiTestingTargets.cmake
    NAMESPACE winui::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/winui)

# find_package(winui): the config file, and a version file that accepts only this exact version
# (there is no ABI promise, see the shared library above).
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/winuiConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/winuiConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/winui)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/winuiConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/winuiConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/winuiConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/winui)

# ---------------------------------------------------------------------------
# Formatting
# ---------------------------------------------------------------------------
if(LIBWINUI_STANDALONE)
    set(CLANG_FORMAT_EXECUTABLE clang-format CACHE FILEPATH "clang-format executable, requires version 22")
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -DCLANG_FORMAT_EXECUTABLE=${CLANG_FORMAT_EXECUTABLE} -DFORMAT_LIST_DIR=${CMAKE_BINARY_DIR} -P ./cmake/Format.cmake
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Documentation (zensical.toml, docs/): the `docs-reference` and `docs` targets
# ---------------------------------------------------------------------------
if(LIBWINUI_STANDALONE)
    include(cmake/Docs.cmake)
endif()
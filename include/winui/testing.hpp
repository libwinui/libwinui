// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// The testing tier, opt-in: everything for testing a UI without a window on screen, in
/// namespace `ui::testing`. `<winui/winui.hpp>` does not include it.
///
/// - HeadlessUI: a window with a deterministic text measurer and a host that records what the
///   window asked for, to lay out, click, type and advance time.
/// - TestRenderer: records the render commands a control emits.
/// - The test doubles for the platform services: TestClipboard, TestCompositor,
///   TestContextMenuHost, TestJumpListHost, TestLauncher, TestMessageBoxHost, TestNativeHost,
///   TestNotifier and TestSpellChecker, installed with seams(), or for the process-wide clipboard
///   and launcher with set_clipboard() and set_launcher().
/// - OffscreenCapture: the on-screen renderer drawing a window into a bitmap, for pixel tests.
///
/// Example:
/// @code
/// #include <winui/testing.hpp>
/// #include <winui/winui.hpp>
///
/// ui::ButtonControl        button{"Hello"};
/// ui::testing::TestRenderer renderer;
/// button.render(renderer);
/// REQUIRE(renderer.count() == 3); // background, border stroke, text
/// @endcode

#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/OffscreenCapture.hpp"
#include "winui/testing/PerfCounters.hpp"
#include "winui/testing/TestAutomationEvents.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/testing/TestContextMenuHost.hpp"
#include "winui/testing/TestJumpListHost.hpp"
#include "winui/testing/TestLauncher.hpp"
#include "winui/testing/TestMessageBoxHost.hpp"
#include "winui/testing/TestNativeHost.hpp"
#include "winui/testing/TestNotifier.hpp"
#include "winui/testing/TestPrintHost.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/TestSpellChecker.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/testing/WindowSeams.hpp"

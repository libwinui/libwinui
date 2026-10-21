// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "winui/core/Geometry.hpp"
#include "winui/ui/TrayIcon.hpp"
#include "winui/ui/WindowStyle.hpp"

#include <catch2/catch_test_macros.hpp>

using ui::place_tray_flyout;
using ui::Point;
using ui::Rect;
using ui::Size;

namespace
{

// A 1920x1080 monitor with a 48px taskbar on each edge in turn.
constexpr Rect  BottomWork{0, 0, 1920, 1032};
constexpr Rect  TopWork{0, 48, 1920, 1032};
constexpr Rect  LeftWork{48, 0, 1872, 1080};
constexpr Rect  RightWork{0, 0, 1872, 1080};
constexpr Size  Flyout{360, 400};
constexpr float Margin = 12;

} // namespace

TEST_CASE("tray flyout: a bottom taskbar puts it above, centered on the icon", "[tray]")
{
    const Point at = place_tray_flyout(Rect{1000, 1040, 32, 32}, BottomWork, Flyout, Margin);
    CHECK(at.x == 1016 - 180);
    CHECK(at.y == 1032 - 12 - 400);
}

TEST_CASE("tray flyout: it stays clear of the corner beside the clock", "[tray]")
{
    const Point at = place_tray_flyout(Rect{1860, 1040, 32, 32}, BottomWork, Flyout, Margin);
    CHECK(at.x == 1920 - 12 - 360);
    CHECK(at.y == 1032 - 12 - 400);
}

TEST_CASE("tray flyout: and clear of the start corner", "[tray]")
{
    const Point at = place_tray_flyout(Rect{20, 1040, 32, 32}, BottomWork, Flyout, Margin);
    CHECK(at.x == 12);
}

TEST_CASE("tray flyout: a top taskbar puts it below", "[tray]")
{
    const Point at = place_tray_flyout(Rect{1000, 8, 32, 32}, TopWork, Flyout, Margin);
    CHECK(at.x == 1016 - 180);
    CHECK(at.y == 48 + 12);
}

TEST_CASE("tray flyout: a side taskbar puts it beside, centered on the icon vertically", "[tray]")
{
    const Point left = place_tray_flyout(Rect{8, 900, 32, 32}, LeftWork, Flyout, Margin);
    CHECK(left.x == 48 + 12);
    CHECK(left.y == 1080 - 12 - 400); // clamped: centering would run off the bottom

    const Point right = place_tray_flyout(Rect{1880, 300, 32, 32}, RightWork, Flyout, Margin);
    CHECK(right.x == 1872 - 12 - 360);
    CHECK(right.y == 316 - 200);
}

TEST_CASE("tray flyout: an anchor inside the work area puts it above the anchor", "[tray]")
{
    // The Windows 11 overflow flyout sits above the taskbar, inside the work area. So does an
    // auto-hide taskbar while shown, the work area then being the whole monitor.
    const Point overflow = place_tray_flyout(Rect{1500, 900, 32, 32}, BottomWork, Flyout, Margin);
    CHECK(overflow.x == 1516 - 180);
    CHECK(overflow.y == 900 - 12 - 400);

    // The cursor fallback (no icon rect) is a zero-size anchor.
    const Point cursor = place_tray_flyout(Rect{1500, 1070, 0, 0}, Rect{0, 0, 1920, 1080}, Flyout, Margin);
    CHECK(cursor.y == 1070 - 12 - 400);
}

TEST_CASE("tray flyout: one taller than the work area is pinned to its top", "[tray]")
{
    const Point at = place_tray_flyout(Rect{1500, 900, 32, 32}, BottomWork, Size{360, 2000}, Margin);
    CHECK(at.y == 12);
}

#endif // _WIN32

TEST_CASE("tray flyout: no drop shadow by default, as the shell's own flyouts", "[tray]")
{
    // Start, Quick Settings, volume and the calendar draw no shadow.
    CHECK_FALSE(ui::TrayFlyoutOptions{}.shadow);
    CHECK(ui::tray_flyout_options().style.frame == ui::Frame::None);
    CHECK(ui::tray_flyout_options({.shadow = true}).style.frame == ui::Frame::Borderless);
    // And a plain popup stays square unless it asks.
    CHECK_FALSE(ui::WindowStyle{}.round_popup_corners);
}

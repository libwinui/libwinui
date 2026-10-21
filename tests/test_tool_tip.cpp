// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/OverlayShadow.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
PointerEvent move(Point p)
{
    return {p, PointerButton::None, PointerAction::Move};
}
PointerEvent leave()
{
    return {{}, PointerButton::None, PointerAction::Leave};
}
PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}

const Point Outside{-500.0f, -500.0f};

// A non-focusable TextControl anchor: a popup owner that is not focusable must still be found
// through the window's walk of live elements.
struct ToolTipWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    ToolTipControl*      tip;
    TextControl*         anchor;
    TextControl*         content;

    ToolTipWindow()
    {
        auto owned = std::make_unique<ToolTipControl>();
        tip        = owned.get();
        anchor     = tip->set_anchor(std::make_unique<TextControl>("Save"));
        content    = tip->set_content(std::make_unique<TextControl>("Saves the current document"));
        window.set_content(std::move(owned));
        window.layout({400.0f, 400.0f}, measurer);
    }

    void relayout()
    {
        window.layout({400.0f, 400.0f}, measurer);
    }
};
} // namespace

TEST_CASE("New ToolTip starts closed with the default 800ms delay and Above placement", "[tool_tip]")
{
    ToolTipControl t;
    REQUIRE_FALSE(t.is_open());
    REQUIRE(t.delay() == 800ms);
    REQUIRE(t.placement() == PopupPlacement::Above);
}

TEST_CASE("Hovering the anchor opens the tooltip only once elapsed time crosses the delay", "[tool_tip][popup]")
{
    ToolTipWindow w;
    const Point   center = w.tip->bounds().center();

    ui::testing::seams(w.window).handle_pointer(move(center));
    REQUIRE_FALSE(w.tip->is_open());

    // Two ticks that only cross 800ms together: the delay accumulates across calls.
    w.window.tick(500ms);
    REQUIRE_FALSE(w.tip->is_open());

    w.window.tick(500ms);
    REQUIRE(w.tip->is_open());
}

TEST_CASE("Moving off the anchor before the delay elapses cancels it", "[tool_tip][popup]")
{
    ToolTipWindow w;
    const Point   center = w.tip->bounds().center();

    ui::testing::seams(w.window).handle_pointer(move(center));
    w.window.tick(500ms);
    REQUIRE_FALSE(w.tip->is_open());

    ui::testing::seams(w.window).handle_pointer(move(Outside));
    w.window.tick(500ms); // would have crossed 800ms combined with the first tick, if not for the move-away
    REQUIRE_FALSE(w.tip->is_open());
}

TEST_CASE("Leave immediately hides an already-open tooltip", "[tool_tip][popup]")
{
    ToolTipWindow w;
    const Point   center = w.tip->bounds().center();

    ui::testing::seams(w.window).handle_pointer(move(center));
    w.window.tick(900ms);
    REQUIRE(w.tip->is_open());

    ui::testing::seams(w.window).handle_pointer(leave());
    REQUIRE_FALSE(w.tip->is_open());
}

TEST_CASE("A click on the anchor immediately hides an already-open tooltip", "[tool_tip][popup]")
{
    ToolTipWindow w;
    const Point   center = w.tip->bounds().center();

    ui::testing::seams(w.window).handle_pointer(move(center));
    w.window.tick(900ms);
    REQUIRE(w.tip->is_open());

    ui::testing::seams(w.window).handle_pointer(down(center));
    REQUIRE_FALSE(w.tip->is_open());
}

TEST_CASE("The popup request reflects the ToolTip's own state", "[tool_tip][popup]")
{
    ToolTipWindow w;
    REQUIRE_FALSE(ui::testing::popup_open(*w.tip));

    const Point center = w.tip->bounds().center();
    ui::testing::seams(w.window).handle_pointer(move(center));
    w.window.tick(900ms);

    REQUIRE(ui::testing::popup_open(*w.tip));
    // The popup content is a background-card wrapper around w.content, not w.content itself.
    REQUIRE(ui::testing::popup_content(*w.tip) != nullptr);
    // Docked to the pointer, inflated by the default mouse offset (20).
    REQUIRE(ui::testing::popup_anchor(*w.tip) == Rect{center.x - 20.0f, center.y - 20.0f, 40.0f, 40.0f});
    REQUIRE((ui::testing::ElementAccess::popup_request(*w.tip)->placement_alignment == ui::PopupPlacementAlignment::Center));
}

TEST_CASE("Window::tick() positions the popup once open, and a light dismiss hides it", "[tool_tip][popup]")
{
    ToolTipWindow w;
    const Point   center = w.tip->bounds().center();
    ui::testing::seams(w.window).handle_pointer(move(center));
    w.window.tick(900ms);
    w.relayout(); // sync_popups() runs inside layout(), not tick() or handle_pointer()

    REQUIRE(w.tip->is_open());
    REQUIRE(w.content->bounds().width > 0.0f);

    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    REQUIRE_FALSE(w.tip->is_open());
}

TEST_CASE("Popup content sizes to its own natural width, not clipped to the anchor's width", "[tool_tip][popup]")
{
    // The surface once measured content bounded by the anchor's width and wrapped long text
    // behind a short anchor. The text stays under the 320 DIP maximum width to test only that.
    HeadlessTextMeasurer measurer;
    const std::string    long_text = "A longer explanation than Save";

    auto            owned  = std::make_unique<ToolTipControl>();
    ToolTipControl* tip    = owned.get();
    TextControl*    anchor = tip->set_anchor(std::make_unique<TextControl>("Save"));
    tip->set_content(std::make_unique<TextControl>(long_text));

    Window window;
    window.set_stretch_content(false); // laid out for the control at its desired size, centered
    window.set_content(std::move(owned));
    window.layout({800.0f, 400.0f}, measurer);

    // Measure through the real Element::measure() path instead of guessing TextControl's style.
    TextControl probe{long_text};
    const float natural_text_width = probe.measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light()).width;
    REQUIRE(natural_text_width > anchor->bounds().width); // the premise of this test

    ui::testing::seams(window).handle_pointer(move(tip->bounds().center()));
    window.tick(900ms);
    window.layout({800.0f, 400.0f}, measurer);

    REQUIRE(tip->is_open());
    REQUIRE(tip->content()->bounds().width >= natural_text_width - 1.0f);
}

TEST_CASE("Popup surface insets content by its border padding (9,6,9,8)", "[tool_tip][popup][layout]")
{
    // The padding is 9,6,9,8: left/right and bottom differ from top.
    ToolTipWindow w;
    ui::testing::seams(w.window).handle_pointer(move(w.tip->bounds().center()));
    w.window.tick(900ms);
    w.relayout();

    const Rect surface = ui::testing::popup_content(*w.tip)->bounds();
    const Rect content = w.content->bounds();
    REQUIRE_THAT(content.left() - surface.left(), WithinAbs(9.0, 1e-2));
    REQUIRE_THAT(surface.right() - content.right(), WithinAbs(9.0, 1e-2));
    REQUIRE_THAT(content.top() - surface.top(), WithinAbs(6.0, 1e-2));
    REQUIRE_THAT(surface.bottom() - content.bottom(), WithinAbs(8.0, 1e-2));
}

TEST_CASE("Popup surface width is capped at ToolTipMaxWidth (320) even for very long content",
          "[tool_tip][popup][layout]")
{
    HeadlessTextMeasurer measurer;
    const std::string    very_long_text(400, 'x'); // wide enough to exceed 320px unconstrained

    auto            owned = std::make_unique<ToolTipControl>();
    ToolTipControl* tip   = owned.get();
    tip->set_anchor(std::make_unique<TextControl>("Save"));
    tip->set_content(std::make_unique<TextControl>(very_long_text));

    Window window;
    window.set_content(std::move(owned));
    window.layout({1200.0f, 400.0f}, measurer);

    ui::testing::seams(window).handle_pointer(move(tip->bounds().center()));
    window.tick(900ms);
    window.layout({1200.0f, 400.0f}, measurer);

    REQUIRE(tip->is_open());
    REQUIRE(ui::testing::popup_content(*tip)->bounds().width <= 320.0f + 1e-2f);
}

// A Luna tooltip is the unthemed comctl32 one, because Luna declines to theme the part.
TEST_CASE("Luna's tooltip draws square, black-framed and tight", "[tool_tip][popup][render][retro][luna]")
{
    HeadlessTextMeasurer measurer;
    const Theme          luna = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    auto            owned   = std::make_unique<ToolTipControl>();
    ToolTipControl* tip     = owned.get();
    TextControl*    content = nullptr;
    tip->set_anchor(std::make_unique<TextControl>("Save"));
    content = tip->set_content(std::make_unique<TextControl>("Saves the current document"));

    Window window;
    window.pin_theme(luna);
    window.set_content(std::move(owned));
    window.layout({400.0f, 400.0f}, measurer);
    ui::testing::seams(window).handle_pointer(move(tip->bounds().center()));
    window.tick(900ms);
    window.layout({400.0f, 400.0f}, measurer);
    REQUIRE(tip->is_open());

    TestRenderer renderer;
    ui::testing::popup_content(*tip)->render(renderer, luna);
    REQUIRE(renderer.count() >= 2);

    const auto& body  = std::get<FillRoundedRect>(renderer.commands[0]);
    const auto& frame = std::get<StrokeRoundedRect>(renderer.commands[1]);
    CHECK(body.color == Color::from_argb(0xFFFFFFE1));
    CHECK(body.radius == 0.0f); // square, though `luna.corner_radius` is the push button's 3
    CHECK(frame.radius == 0.0f);
    CHECK(frame.color == colors::Black);
    CHECK(frame.color != luna.surfaces.field_border);

    // `Padding = 1,1,3,1` inside the 1 px frame.
    const Rect surface = ui::testing::popup_content(*tip)->bounds();
    const Rect inner   = content->bounds();
    CHECK_THAT(inner.left() - surface.left(), WithinAbs(2.0, 1e-2));
    CHECK_THAT(surface.right() - inner.right(), WithinAbs(4.0, 1e-2));
    CHECK_THAT(inner.top() - surface.top(), WithinAbs(2.0, 1e-2));
    CHECK_THAT(surface.bottom() - inner.bottom(), WithinAbs(2.0, 1e-2));
}

// A tooltip rounds at corner_radius, not overlay_corner_radius, so its shadow has to use
// the owner's radius or the shadow's corners show.
TEST_CASE("A tooltip's drop shadow is drawn at the tooltip's own corner radius", "[tool_tip][popup][render]")
{
    const Theme    fluent = Theme::light();
    ToolTipControl tip;
    tip.set_content(std::make_unique<TextControl>("tip"));
    tip.show_at(Point{0.0f, 0.0f});
    REQUIRE(ui::testing::popup_open(tip));

    CHECK(ui::testing::ElementAccess::popup_request(tip)->chrome.corner_radius(fluent) ==
          ui::detail::tooltip_radius(fluent));
    CHECK(ui::testing::ElementAccess::popup_request(tip)->chrome.corner_radius(fluent) == fluent.corner_radius);
    CHECK(fluent.corner_radius != fluent.overlay_corner_radius); // 4 vs 8: the mismatch in Fluent

    const Theme luna = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});
    CHECK(ui::testing::ElementAccess::popup_request(tip)->chrome.corner_radius(luna) == 0.0f);
    CHECK(luna.corner_radius != 0.0f); // ...and the mismatch in Luna, the other way round
}

namespace
{
// A ToolTip well inside a large window, so neither the flip nor the edge clamp gets involved.
struct PlacedToolTip
{
    HeadlessTextMeasurer measurer;
    Window               window;
    ToolTipControl*      tip;

    PlacedToolTip()
    {
        auto owned = std::make_unique<ToolTipControl>();
        tip        = owned.get();
        tip->set_anchor(std::make_unique<TextControl>("TextBlock with a tooltip"));
        tip->set_content(std::make_unique<TextControl>("Tip"));
        tip->set_margin(Edges{.left = 200.0f, .top = 200.0f});
        tip->set_horizontal_alignment(Alignment::Leading);
        tip->set_vertical_alignment(Alignment::Leading);
        window.set_content(std::move(owned));
        window.layout({800.0f, 600.0f}, measurer);
    }

    Rect open_at(Point p)
    {
        ui::testing::seams(window).handle_pointer(move(p));
        window.tick(900ms);
        window.layout({800.0f, 600.0f}, measurer);
        return ui::testing::popup_content(*tip)->bounds();
    }
};
} // namespace

TEST_CASE("A mouse-opened tooltip is centered on the pointer, 20px above it", "[tool_tip][popup][layout]")
{
    PlacedToolTip t;
    const Point   p{t.tip->bounds().x + 30.0f, t.tip->bounds().center().y};
    const Rect    surface = t.open_at(p);

    REQUIRE(t.tip->is_open());
    REQUIRE_THAT(surface.bottom(), WithinAbs(p.y - 20.0, 1e-3));
    REQUIRE_THAT(surface.center().x, WithinAbs(p.x, 1e-3));
}

TEST_CASE("A negative vertical_offset turns the dock rect inside out and drops the tooltip below the pointer",
          "[tool_tip][popup][layout]")
{
    PlacedToolTip t;
    t.tip->set_vertical_offset(-80.0f);
    const Point p       = t.tip->bounds().center();
    const Rect  surface = t.open_at(p);

    REQUIRE_THAT(surface.bottom(), WithinAbs(p.y + 80.0, 1e-3));
    REQUIRE_THAT(surface.center().x, WithinAbs(p.x - 0.0, 1e-3));
}

TEST_CASE("PlacementRect docks to the rect instead of the pointer", "[tool_tip][popup][layout]")
{
    PlacedToolTip t;
    const Rect    anchor = t.tip->bounds();
    t.tip->set_placement(PopupPlacement::Right);
    t.tip->set_placement_rect(Rect{0.0f, 0.0f, anchor.width, anchor.height});
    t.tip->set_horizontal_offset(0.0f);
    const Rect surface = t.open_at(Point{anchor.x + 5.0f, anchor.y + 2.0f});

    REQUIRE_THAT(surface.left(), WithinAbs(anchor.right(), 1e-3));
    REQUIRE_THAT(surface.center().y, WithinAbs(anchor.center().y, 1e-3));
}

TEST_CASE("A ToolTip casts its popup shadow from Z 16, a step below a flyout", "[tooltip]")
{
    ToolTipControl tip;
    tip.set_content(std::make_unique<TextControl>("tip"));
    tip.show_at(Point{0.0f, 0.0f});
    REQUIRE(ui::testing::popup_open(tip));
    CHECK(ui::testing::ElementAccess::popup_request(tip)->chrome.elevation == elevation_z::ToolTip);
    // A request that leaves the elevation unset gets a flyout's.
    CHECK_FALSE(PopupRequest{}.chrome.elevation.has_value());
}

TEST_CASE("A click on a ToolTip's anchor leaves no focus ring", "[tool_tip][focus]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    auto                 owned  = std::make_unique<ToolTipControl>();
    ButtonControl*       anchor = owned->set_anchor(std::make_unique<ButtonControl>("Save"));
    anchor->set_horizontal_alignment(Alignment::Leading);
    anchor->set_vertical_alignment(Alignment::Leading);
    window.set_content(std::move(owned));
    window.layout({400.0f, 400.0f}, measurer);

    ui::testing::seams(window).handle_key({Key::Enter, KeyAction::Up});
    const Point center = anchor->bounds().center();
    ui::testing::seams(window).handle_pointer(down(center));
    ui::testing::seams(window).handle_pointer({center, PointerButton::Left, PointerAction::Up});
    REQUIRE(anchor->focused());
    CHECK_FALSE(anchor->focus_visible());

    ui::testing::seams(window).handle_key({Key::Escape, KeyAction::Down});
    CHECK(anchor->focus_visible());
}

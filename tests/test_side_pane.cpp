// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/panes/SidePane.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}
void click(Window& w, Point p)
{
    ui::testing::seams(w).handle_pointer(down(p));
    ui::testing::seams(w).handle_pointer(up(p));
}

struct SplitWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    ButtonControl*       page_button    = nullptr;
    SidePaneControl*     split          = nullptr;
    ButtonControl*       pane_first     = nullptr;
    ButtonControl*       pane_second    = nullptr;
    ButtonControl*       content_button = nullptr;
    int                  content_clicks = 0;
    int                  closing        = 0;
    int                  closed         = 0;
    bool                 cancel         = false;

    SplitWindow()
    {
        auto stack  = std::make_unique<StackControl>(Orientation::Horizontal);
        page_button = stack->add(std::make_unique<ButtonControl>("Page"));
        split       = stack->add(std::make_unique<SidePaneControl>());
        split->set_width(400.0f);
        split->set_height(300.0f);
        auto pane   = std::make_unique<StackControl>(Orientation::Vertical);
        pane_first  = pane->add(std::make_unique<ButtonControl>("First"));
        pane_second = pane->add(std::make_unique<ButtonControl>("Second"));
        split->set_pane(std::move(pane));
        content_button = split->set_content(std::make_unique<ButtonControl>("Content"));
        content_button->set_horizontal_alignment(Alignment::Trailing);
        content_button->on_click([this] { ++content_clicks; });
        split->on_pane_closing(
            [this]
            {
                ++closing;
                return cancel ? ui::Cancel : ui::Proceed;
            });
        split->on_pane_closed([this] { ++closed; });
        window.set_content(std::move(stack));
        relayout();
    }

    void relayout()
    {
        window.layout({800.0f, 400.0f}, measurer);
    }
};
} // namespace

TEST_CASE("SidePane starts with its documented defaults", "[split-view]")
{
    SidePaneControl split;
    CHECK_FALSE(split.is_pane_open());
    CHECK(split.display_mode() == SidePaneDisplayMode::Overlay);
    CHECK(split.pane_placement() == SidePanePlacement::Left);
    CHECK(split.open_pane_length() == std::optional<float>{320.0f}); // the open pane's default length
    CHECK(split.compact_pane_length() == 48.0f);                     // the compact pane's default length
    CHECK(split.light_dismiss_overlay_mode() == LightDismissOverlayMode::Auto);
}

TEST_CASE("Each display mode gives the content its column and shows its part of the pane", "[split-view][layout]")
{
    SplitWindow w;
    const Rect  view = w.split->bounds();

    CHECK(w.split->content_bounds().width == view.width);
    CHECK(w.split->visible_pane_bounds().width == 0.0f);
    CHECK(w.split->pane_bounds().width == 320.0f);

    w.split->set_pane_open(true);
    w.relayout();
    CHECK(w.split->content_bounds().width == view.width);
    CHECK(w.split->visible_pane_bounds().width == 320.0f);
    CHECK(w.pane_first->bounds().x < view.x + 320.0f);

    w.split->set_display_mode(SidePaneDisplayMode::Inline);
    w.relayout();
    CHECK_THAT(w.split->content_bounds().x, WithinAbs(view.x + 320.0f, 1e-3));
    CHECK_THAT(w.split->content_bounds().width, WithinAbs(80.0f, 1e-3));

    w.split->set_display_mode(SidePaneDisplayMode::CompactInline);
    w.split->set_pane_open(false);
    w.relayout();
    CHECK_THAT(w.split->content_bounds().x, WithinAbs(view.x + 48.0f, 1e-3));
    CHECK(w.split->visible_pane_bounds().width == 48.0f);
    CHECK(w.split->pane_bounds().width == 320.0f); // laid out full width, clipped to the strip

    w.split->set_display_mode(SidePaneDisplayMode::CompactOverlay);
    w.split->set_pane_open(true);
    w.relayout();
    CHECK_THAT(w.split->content_bounds().x, WithinAbs(view.x + 48.0f, 1e-3));
    CHECK(w.split->visible_pane_bounds().width == 320.0f);

    w.split->set_pane_placement(SidePanePlacement::Right);
    w.relayout();
    CHECK_THAT(w.split->content_bounds().x, WithinAbs(view.x, 1e-3));
    CHECK_THAT(w.split->visible_pane_bounds().right(), WithinAbs(view.right(), 1e-3));
}

TEST_CASE("An unset open_pane_length takes the pane's own width", "[split-view][layout]")
{
    SplitWindow w;
    w.split->set_open_pane_length(std::nullopt);
    w.split->set_display_mode(SidePaneDisplayMode::Inline);
    w.split->set_pane_open(true);
    w.relayout();
    const float pane_width = w.split->pane_bounds().width;
    CHECK(pane_width < 320.0f);
    CHECK(pane_width > 0.0f);
    CHECK_THAT(w.split->content_bounds().x, WithinAbs(w.split->bounds().x + pane_width, 1e-3));
}

TEST_CASE("A click on the content light-dismisses an overlay pane and goes no further", "[split-view][input]")
{
    SplitWindow w;
    w.split->set_pane_open(true);
    w.relayout();

    click(w.window, w.content_button->bounds().center());
    CHECK_FALSE(w.split->is_pane_open());
    CHECK(w.closing == 1);
    CHECK(w.closed == 1);
    CHECK(w.content_clicks == 0);

    w.relayout();
    click(w.window, w.content_button->bounds().center());
    CHECK(w.content_clicks == 1);
}

TEST_CASE("on_pane_closing can cancel a light dismiss but not an explicit close", "[split-view][input]")
{
    SplitWindow w;
    w.cancel = true;
    w.split->set_pane_open(true);
    w.relayout();
    click(w.window, w.content_button->bounds().center());
    CHECK(w.split->is_pane_open());
    CHECK(w.closing == 1);

    w.split->set_pane_open(false);
    CHECK_FALSE(w.split->is_pane_open());
    CHECK(w.closing == 2);
}

TEST_CASE("An inline pane is not light-dismissable", "[split-view][input]")
{
    SplitWindow w;
    w.split->set_display_mode(SidePaneDisplayMode::Inline);
    w.split->set_pane_open(true);
    w.relayout();
    CHECK_FALSE(w.split->can_light_dismiss());
    click(w.window, w.content_button->bounds().center());
    CHECK(w.split->is_pane_open());
    CHECK(w.content_clicks == 1);
}

TEST_CASE("Opening an overlay pane takes focus into it, Tab stays there, closing gives it back", "[split-view][focus]")
{
    SplitWindow w;
    click(w.window, w.page_button->bounds().center());
    REQUIRE(w.window.focused() == w.page_button);

    w.split->set_pane_open(true);
    w.relayout();
    CHECK(w.window.focused() == w.pane_first);

    // Tab cycles the pane alone.
    ui::testing::seams(w.window).handle_key({Key::Tab, KeyAction::Down});
    CHECK(w.window.focused() == w.pane_second);
    ui::testing::seams(w.window).handle_key({Key::Tab, KeyAction::Down});
    CHECK(w.window.focused() == w.pane_first);

    // Escape closes it and focus goes back to the page.
    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    CHECK_FALSE(w.split->is_pane_open());
    w.relayout();
    CHECK(w.window.focused() == w.page_button);
}

TEST_CASE("Switching an open pane to Inline releases the focus trap", "[split-view][focus]")
{
    SplitWindow w;
    w.split->set_pane_open(true);
    w.relayout();
    REQUIRE(w.window.focused() == w.pane_first);
    w.split->set_display_mode(SidePaneDisplayMode::Inline);
    w.relayout();
    ui::testing::seams(w.window).handle_key({Key::Tab, KeyAction::Down});
    ui::testing::seams(w.window).handle_key({Key::Tab, KeyAction::Down});
    CHECK(w.window.focused() == w.content_button);
}

TEST_CASE("The overlay pane slides in on the overlay curve", "[split-view][animation]")
{
    SplitWindow w;
    w.window.tick(16ms); // the host drives ticks: transitions run from here on
    w.split->set_pane_open(true);
    w.relayout();
    CHECK(w.split->visible_pane_bounds().width < 1.0f);

    w.window.tick(35ms); // a tenth of 350ms: the (0.1,0.9,0.2,1) spline is already well out
    const float early = w.split->visible_pane_bounds().width;
    CHECK(early > 32.0f);
    CHECK(early < 320.0f);
    CHECK(w.pane_first->bounds().x < w.split->bounds().x); // sliding in from the left

    w.window.tick(400ms);
    CHECK(w.split->visible_pane_bounds().width == 320.0f);
    CHECK(w.pane_first->bounds().x >= w.split->pane_bounds().x); // settled inside its column
}

TEST_CASE("The pane draws the theme's pane color, or its own, clipped to what shows", "[split-view][render]")
{
    SplitWindow w;
    w.split->set_display_mode(SidePaneDisplayMode::CompactOverlay);
    w.relayout();
    const Theme theme = Theme::light();

    TestRenderer r;
    w.split->render(r, theme);
    bool found = false;
    for (const auto& c : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&c);
            fill != nullptr && theme.surfaces.split_view_pane && fill->color == *theme.surfaces.split_view_pane)
        {
            found = true;
        }
    }
    CHECK(found);
    CHECK(theme.surfaces.split_view_pane == std::optional<Color>{Color::from_argb(0xFFF2F2F2)});
    CHECK(Theme::fluent(Palette::fluent_dark()).surfaces.split_view_pane ==
          std::optional<Color>{Color::from_argb(0xFF171717)});
}

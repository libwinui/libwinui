// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/scrolling/Swipe.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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
PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}

// Short labels measure under the 68dip minimum item width, so every item is exactly 68 and
// the Reveal extents (68 for one item, 136 for two) do not depend on the measurer.
SwipeItem make_item(std::string text, int& invoked)
{
    return SwipeItem{.text = std::move(text), .on_invoked = [&invoked] { ++invoked; }};
}

struct Fixture
{
    SwipeControl   swipe;
    ButtonControl* content = nullptr;
    int            clicks  = 0;

    explicit Fixture(SwipeItems left = {}, SwipeItems right = {})
    {
        content = swipe.set_content(std::make_unique<ButtonControl>("Row"));
        content->on_click([this] { ++clicks; });
        swipe.set_left_items(std::move(left));
        swipe.set_right_items(std::move(right));

        HeadlessTextMeasurer measurer;
        (void)swipe.measure({300.0f, 80.0f}, measurer, Theme::default_light());
        swipe.arrange({0, 0, 300, 80});
    }

    // Lets the 250 ms snap (and an Execute swipe's invoke-then-close) play out.
    void settle()
    {
        for (int i = 0; i < 4; ++i)
        {
            swipe.tick(300ms);
        }
    }
};
} // namespace

TEST_CASE("A new SwipeControl is not focusable, has no content, and is closed", "[swipe-control]")
{
    SwipeControl swipe;
    REQUIRE_FALSE(swipe.focusable());
    REQUIRE(swipe.content() == nullptr);
    REQUIRE_FALSE(swipe.is_open());
    REQUIRE(swipe.offset() == 0.0f);
}

TEST_CASE("arrange() alone leaves content at its unshifted base position", "[swipe-control][layout]")
{
    int     invoked = 0;
    Fixture f{SwipeItems{.items = {make_item("Go", invoked)}, .mode = SwipeMode::Reveal}};
    REQUIRE_THAT(f.content->bounds().left(), WithinAbs(0.0f, 1e-3));
}

TEST_CASE("Dragging right reveals content shifted right, clamped to the left items' total width",
          "[swipe-control][input]")
{
    int invoked = 0;
    Fixture f{SwipeItems{.items = {make_item("A", invoked), make_item("B", invoked)}, .mode = SwipeMode::Reveal}}; // extent 136

    f.swipe.handle_pointer(down({50, 40}));
    f.swipe.handle_pointer(move({90, 40})); // dx = 40, past the drag threshold
    REQUIRE_THAT(f.swipe.offset(), WithinAbs(40.0, 1e-3));

    f.swipe.handle_pointer(move({400, 40})); // dx = 350, clamped to the 136 extent
    REQUIRE_THAT(f.swipe.offset(), WithinAbs(136.0, 1e-3));

    TestRenderer r;
    f.swipe.render(r, Theme::light());
    REQUIRE_THAT(f.content->bounds().left(), WithinAbs(136.0, 1e-3));
}

TEST_CASE("Dragging is clamped to zero on a side with no items", "[swipe-control][input]")
{
    int     invoked = 0;
    Fixture f{SwipeItems{.items = {make_item("A", invoked)}, .mode = SwipeMode::Reveal}}; // right side has no items

    f.swipe.handle_pointer(down({50, 40}));
    f.swipe.handle_pointer(move({0, 40})); // dragging left, but right_items_ is empty
    REQUIRE_THAT(f.swipe.offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Reveal mode: released past min(extent, 100) it slides open, short of it slides shut",
          "[swipe-control][input]")
{
    int invoked = 0;
    {
        Fixture f{SwipeItems{.items = {make_item("A", invoked), make_item("B", invoked)}, .mode = SwipeMode::Reveal}}; // extent 136
        f.swipe.handle_pointer(down({50, 40}));
        f.swipe.handle_pointer(move({140, 40})); // offset 90 < 100
        f.swipe.handle_pointer(up({140, 40}));
        REQUIRE_THAT(f.swipe.offset(), WithinAbs(90.0, 1e-3)); // the slide has only started
        f.settle();
        REQUIRE_THAT(f.swipe.offset(), WithinAbs(0.0, 1e-3));
    }
    {
        Fixture f{SwipeItems{.items = {make_item("A", invoked), make_item("B", invoked)}, .mode = SwipeMode::Reveal}};
        f.swipe.handle_pointer(down({50, 40}));
        f.swipe.handle_pointer(move({160, 40})); // offset 110 >= 100
        f.swipe.handle_pointer(up({160, 40}));
        f.swipe.tick(125ms);
        REQUIRE(f.swipe.offset() > 110.0f); // on its way
        REQUIRE(f.swipe.offset() < 136.0f);
        f.settle();
        REQUIRE_THAT(f.swipe.offset(), WithinAbs(136.0, 1e-3));
    }
    {
        // A strip narrower than 100 opens once dragged its own width.
        Fixture f{SwipeItems{.items = {make_item("A", invoked)}, .mode = SwipeMode::Reveal}}; // extent 68
        f.swipe.handle_pointer(down({50, 40}));
        f.swipe.handle_pointer(move({400, 40}));
        f.swipe.handle_pointer(up({400, 40}));
        f.settle();
        REQUIRE_THAT(f.swipe.offset(), WithinAbs(68.0, 1e-3));

        // From open, any drag back short of fully open closes it.
        f.swipe.handle_pointer(down({150, 40}));
        f.swipe.handle_pointer(move({130, 40}));
        f.swipe.handle_pointer(up({130, 40}));
        f.settle();
        REQUIRE_THAT(f.swipe.offset(), WithinAbs(0.0, 1e-3));
    }
}

TEST_CASE("Tapping a fully revealed item invokes it and closes the swipe", "[swipe-control][input]")
{
    int     invoked = 0;
    Fixture f{SwipeItems{.items = {make_item("A", invoked), make_item("B", invoked)}, .mode = SwipeMode::Reveal}};

    f.swipe.handle_pointer(down({50, 40}));
    f.swipe.handle_pointer(move({400, 40})); // drag fully open (extent 136)
    f.swipe.handle_pointer(up({400, 40}));
    f.settle();
    REQUIRE_THAT(f.swipe.offset(), WithinAbs(136.0, 1e-3));

    // item 0 ("A") occupies [0, 68) along the revealed strip.
    f.swipe.handle_pointer(down({10, 40}));
    f.swipe.handle_pointer(up({10, 40}));
    REQUIRE(invoked == 1);
    f.settle();
    REQUIRE_THAT(f.swipe.offset(), WithinAbs(0.0, 1e-3));
    REQUIRE(f.clicks == 0); // content_ never saw this gesture
}

TEST_CASE("behavior_on_invoked RemainOpen leaves the strip open; Invoked sees the control and its item",
          "[swipe-control][input]")
{
    SwipeControl* seen_control = nullptr;
    std::string   seen_text;
    SwipeItem     item{.text                = "Flag",
                       .behavior_on_invoked = SwipeBehaviorOnInvoked::RemainOpen,
                       .on_invoked          = [&](SwipeItemInvokedEventArgs& args)
                       {
                       seen_control   = &args.swipe_control;
                       seen_text      = args.item.text;
                       args.item.text = "Unmark"; // as the sample renames its item
                       }};
    Fixture       f{SwipeItems{.items = {item}, .mode = SwipeMode::Reveal}};

    f.swipe.handle_pointer(down({50, 40}));
    f.swipe.handle_pointer(move({400, 40}));
    f.swipe.handle_pointer(up({400, 40}));
    f.settle();
    f.swipe.handle_pointer(down({10, 40}));
    f.swipe.handle_pointer(up({10, 40}));
    f.settle();

    REQUIRE(seen_control == &f.swipe);
    REQUIRE(seen_text == "Flag");
    REQUIRE(f.swipe.left_items().items.front().text == "Unmark");
    REQUIRE_THAT(f.swipe.offset(), WithinAbs(68.0, 1e-3));
}

TEST_CASE("Tapping the content area while open dismisses without forwarding to content_", "[swipe-control][input]")
{
    int     invoked = 0;
    Fixture f{SwipeItems{.items = {make_item("A", invoked)}, .mode = SwipeMode::Reveal}};

    f.swipe.handle_pointer(down({50, 40}));
    f.swipe.handle_pointer(move({400, 40}));
    f.swipe.handle_pointer(up({400, 40}));
    f.settle();
    REQUIRE(f.swipe.is_open());

    const Point center = f.swipe.bounds().center(); // past the revealed strip, over content
    f.swipe.handle_pointer(down(center));
    f.swipe.handle_pointer(up(center));
    f.settle();

    REQUIRE_FALSE(f.swipe.is_open());
    REQUIRE(f.clicks == 0);
}

TEST_CASE("A press outside an open Reveal strip closes it", "[swipe-control][input]")
{
    int     invoked = 0;
    Fixture f{SwipeItems{.items = {make_item("A", invoked)}, .mode = SwipeMode::Reveal}};
    f.swipe.handle_pointer(down({50, 40}));
    f.swipe.handle_pointer(move({400, 40}));
    f.swipe.handle_pointer(up({400, 40}));
    f.settle();
    REQUIRE(f.swipe.is_open());

    f.swipe.handle_pointer(down({50, 200})); // below the control
    f.settle();
    REQUIRE_FALSE(f.swipe.is_open());
    REQUIRE(invoked == 0);
}

TEST_CASE("An ordinary click passes through to content_ when closed", "[swipe-control][input]")
{
    Fixture     f;
    const Point center = f.swipe.bounds().center();
    f.swipe.handle_pointer(down(center));
    f.swipe.handle_pointer(up(center));
    REQUIRE(f.clicks == 1);
}

TEST_CASE("Execute mode: past the threshold the item slides fully open, is invoked there, and closes",
          "[swipe-control][input]")
{
    int invoked = 0;
    {
        // The Execute item is as wide as the control (300), so it can be dragged that far, and
        // commits at min(300, 100).
        Fixture f{SwipeItems{.items = {make_item("Go", invoked)}, .mode = SwipeMode::Execute}};
        f.swipe.handle_pointer(down({10, 40}));
        f.swipe.handle_pointer(move({400, 40}));
        REQUIRE_THAT(f.swipe.offset(), WithinAbs(300.0, 1e-3));
        f.swipe.handle_pointer(move({120, 40})); // back to dx 110
        f.swipe.handle_pointer(up({120, 40}));
        REQUIRE(invoked == 0); // nothing on the release itself
        f.swipe.tick(300ms);   // fully open: invoked, and the close starts
        REQUIRE(invoked == 1);
        f.settle();
        REQUIRE_THAT(f.swipe.offset(), WithinAbs(0.0, 1e-3));
    }
    {
        Fixture f{SwipeItems{.items = {make_item("Go", invoked)}, .mode = SwipeMode::Execute}};
        f.swipe.handle_pointer(down({50, 40}));
        f.swipe.handle_pointer(move({149, 40})); // dx 99, short of the 100 threshold
        f.swipe.handle_pointer(up({149, 40}));
        f.settle();
        REQUIRE(invoked == 1); // unchanged from the block above
        REQUIRE_THAT(f.swipe.offset(), WithinAbs(0.0, 1e-3));
    }
}

TEST_CASE("Execute mode renders the pre-threshold fill until past the threshold, then the accent fill",
          "[swipe-control][render]")
{
    // An execute item has a muted
    // look until the drag commits.
    int     invoked = 0;
    Theme   theme   = Theme::light();
    Fixture f{SwipeItems{.items = {make_item("Go", invoked)}, .mode = SwipeMode::Execute}};

    f.swipe.handle_pointer(down({50, 40}));
    f.swipe.handle_pointer(move({100, 40})); // dx 50, short of the 100 threshold
    REQUIRE_THAT(f.swipe.offset(), WithinAbs(50.0, 1e-3));
    {
        TestRenderer r;
        f.swipe.render(r, theme);
        REQUIRE(r.commands.size() >= 2);
        REQUIRE(std::get<FillRoundedRect>(r.commands[1]).color ==
                ui::detail::controls(theme).swipe_item_pre_threshold.background);
    }

    f.swipe.handle_pointer(move({170, 40})); // dx 120, past it
    {
        TestRenderer r;
        f.swipe.render(r, theme);
        REQUIRE(r.commands.size() >= 2);
        REQUIRE(std::get<FillRoundedRect>(r.commands[1]).color == theme.accent_button_normal.background);
    }
}

TEST_CASE("An Execute item's brush spans the whole control, clipped to what is revealed", "[swipe-control][render]")
{
    const LinearGradientBrush purple{{{0.0f, Color::from_argb(0xFF8990F9)}, {1.0f, Color::from_argb(0xFF5C1DF4)}},
                                     GradientAxis::Horizontal};
    Fixture f{{}, SwipeItems{.items = {SwipeItem{.text = "Lock", .background = Brush{purple}}}, .mode = SwipeMode::Execute}};
    f.swipe.handle_pointer(down({250, 40}));
    f.swipe.handle_pointer(move({200, 40})); // 50 revealed at the right
    TestRenderer r;
    f.swipe.render(r, Theme::light());
    REQUIRE(std::get<PushClip>(r.commands[0]).bounds == Rect{250, 0, 50, 80});
    const auto& fill = std::get<DrawGeometry>(r.commands[1]);
    REQUIRE(fill.brush_box == Rect{0, 0, 300, 80});
}

TEST_CASE("set_left_items()/set_right_items() replace the list and close any open swipe", "[swipe-control]")
{
    int     invoked = 0;
    Fixture f{SwipeItems{.items = {make_item("A", invoked)}, .mode = SwipeMode::Reveal}};

    f.swipe.handle_pointer(down({50, 40}));
    f.swipe.handle_pointer(move({400, 40}));
    f.swipe.handle_pointer(up({400, 40}));
    f.settle();
    REQUIRE(f.swipe.is_open());

    f.swipe.set_left_items(SwipeItems{.items = {make_item("C", invoked)}, .mode = SwipeMode::Reveal});
    REQUIRE_FALSE(f.swipe.is_open());
}

TEST_CASE("render() clips the revealed strip and wraps content in its own PushClip/PopClip pair",
          "[swipe-control][render]")
{
    int     invoked = 0;
    Fixture f{SwipeItems{.items = {make_item("A", invoked)}, .mode = SwipeMode::Reveal}}; // extent 68

    f.swipe.handle_pointer(down({50, 40}));
    f.swipe.handle_pointer(move({400, 40})); // fully open at 68
    REQUIRE_THAT(f.swipe.offset(), WithinAbs(68.0, 1e-3));

    TestRenderer r;
    f.swipe.render(r, Theme::light());

    REQUIRE(r.count_of<PushClip>() == 2);
    REQUIRE(r.count_of<PopClip>() == 2);
    REQUIRE(std::get<PushClip>(r.commands.front()).bounds == Rect{0, 0, 68, 80});
}

TEST_CASE("Reveal mode renders items in the neutral swipe_item_default look, not accent", "[swipe-control][render]")
{
    int     invoked = 0;
    Theme   theme   = Theme::light();
    Fixture f{SwipeItems{.items = {make_item("A", invoked)}, .mode = SwipeMode::Reveal}}; // extent 68

    f.swipe.handle_pointer(down({50, 40}));
    f.swipe.handle_pointer(move({400, 40})); // fully open
    REQUIRE_THAT(f.swipe.offset(), WithinAbs(68.0, 1e-3));

    TestRenderer r;
    f.swipe.render(r, theme);
    REQUIRE(r.commands.size() >= 2);
    REQUIRE(std::get<FillRoundedRect>(r.commands[1]).color == ui::detail::controls(theme).swipe_item_default.background);
}

TEST_CASE("measure_override() floors the control's height at 40px whenever it has any swipe items",
          "[swipe-control][layout]")
{
    // The items' minimum height floors the control itself, since the revealed strip always spans
    // its full height.
    int invoked = 0;

    SwipeControl   swipe;
    ButtonControl* content = swipe.set_content(std::make_unique<ButtonControl>("Row"));
    (void)content;
    swipe.set_left_items(SwipeItems{.items = {make_item("A", invoked)}, .mode = SwipeMode::Reveal});

    HeadlessTextMeasurer measurer;
    const Size           desired = swipe.measure({300.0f, 300.0f}, measurer, Theme::default_light());
    REQUIRE(desired.height >= 40.0f);
}

TEST_CASE("measure_override() applies no height floor when there are no swipe items at all", "[swipe-control][layout]")
{
    SwipeControl swipe;
    swipe.set_content(std::make_unique<ButtonControl>("Row"));

    HeadlessTextMeasurer measurer;
    const Size           with_items    = swipe.measure({300.0f, 300.0f}, measurer, Theme::default_light());
    const float          content_alone = with_items.height;
    REQUIRE(content_alone < 40.0f); // a bare Button is shorter than the 40px floor
}

TEST_CASE("content always joins focusables regardless of the current offset", "[swipe-control][focus]")
{
    HeadlessTextMeasurer measurer;
    int                  invoked = 0;

    auto           swipe_owned = std::make_unique<SwipeControl>();
    SwipeControl*  swipe       = swipe_owned.get();
    ButtonControl* inner       = swipe->set_content(std::make_unique<ButtonControl>("Inner"));
    swipe->set_left_items(SwipeItems{.items = {make_item("A", invoked)}, .mode = SwipeMode::Reveal});

    Window window;
    window.set_content(std::move(swipe_owned));
    window.layout({300, 80}, measurer);

    std::vector<Element*> focusables;
    window.content()->collect_focusables(focusables);
    REQUIRE(std::find(focusables.begin(), focusables.end(), static_cast<Element*>(inner)) != focusables.end());
}

// SwipeControl clips its content to exactly its bounds, so a border stroked on the content's
// bounds would lose its outer half. Under every theme the row's outline has to stay inside it.
TEST_CASE("A bordered row inside a SwipeControl keeps its whole outline inside itself, under every theme",
          "[swipe][render][regression]")
{
    for (ThemeVariant v :
         {ThemeVariant::Fluent2,
          ThemeVariant::Fluent,
          ThemeVariant::Mdl2,
          ThemeVariant::Metro,
          ThemeVariant::Aero,
          ThemeVariant::Luna,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x,
          ThemeVariant::Windows1x})
    {
        CAPTURE(static_cast<int>(v));
        const Theme          theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        HeadlessTextMeasurer measurer;

        SwipeControl swipe;
        swipe.set_content(std::make_unique<ButtonControl>("Row"));
        swipe.set_right_items(SwipeItems{.items = {SwipeItem{.text = "Delete"}}});
        const Size d = swipe.measure(Size{600.0f, 400.0f}, measurer, theme);
        const Rect bounds{0.0f, 0.0f, 600.0f, d.height};
        swipe.arrange(bounds);

        TestRenderer renderer;
        swipe.render(renderer, theme);

        // The Button fills the clip exactly: layout keeps no room for a focus ring.
        const Rect row = swipe.content()->bounds();
        REQUIRE(row == bounds);

        auto inside = [&](const Rect& path, float width)
        {
            const float half = width * 0.5f;
            CHECK(path.left() - half >= row.left());
            CHECK(path.top() - half >= row.top());
            CHECK(path.right() + half <= row.right());
            CHECK(path.bottom() + half <= row.bottom());
        };
        for (const auto& cmd : renderer.commands)
        {
            if (std::holds_alternative<StrokeRoundedRect>(cmd))
            {
                const auto& stroke = std::get<StrokeRoundedRect>(cmd);
                inside(stroke.bounds, stroke.stroke_width);
            }
            else if (std::holds_alternative<StrokeRoundedRectGradient>(cmd))
            {
                const auto& stroke = std::get<StrokeRoundedRectGradient>(cmd);
                inside(stroke.bounds, stroke.stroke_width);
            }
        }
    }
}

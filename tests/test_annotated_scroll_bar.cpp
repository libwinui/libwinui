// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/AnnotatedScrollBar.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
PointerEvent leave()
{
    return {{}, PointerButton::None, PointerAction::Leave};
}

std::unique_ptr<StackControl> make_tall_stack(int n)
{
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->set_spacing(0.0f);
    for (int i = 0; i < n; ++i)
    {
        stack->add(std::make_unique<TextControl>("Item"));
    }
    return stack;
}
} // namespace

TEST_CASE("AnnotatedScrollBar measures a fixed rail width with no labels, wider with them",
          "[annotated_scroll_bar][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    const Size                bare = bar.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(bare.width, WithinAbs(30.0, 1e-3));

    bar.set_annotations({{.text = "2024", .scroll_offset = 0.0}, {.text = "2023", .scroll_offset = 500.0}});
    const Size labeled = bar.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE(labeled.width > bare.width);
}

TEST_CASE("AnnotatedScrollBar is focusable exactly when enabled", "[annotated_scroll_bar][focus]")
{
    AnnotatedScrollBarControl bar;
    REQUIRE(bar.focusable());

    bar.set_enabled(false);
    REQUIRE_FALSE(bar.focusable());

    std::vector<Element*> focusables;
    bar.collect_focusables(focusables);
    REQUIRE(focusables.empty());
}

TEST_CASE("Clicking the rail jumps Value to the clicked offset", "[annotated_scroll_bar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(1000.0);
    bar.set_viewport_size(200.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    double requested = -1.0;
    // Standalone (no target()), the caller echoes the requested offset back through set_value().
    bar.on_scroll_to_requested(
        [&](double offset)
        {
            requested = offset;
            bar.set_value(offset);
        });

    const Point mid{15.0f, 150.0f};
    bar.handle_pointer(down(mid));

    REQUIRE(requested > 0.0);
    REQUIRE(requested < 1000.0);
    REQUIRE_THAT(bar.value(), WithinAbs(requested, 1e-6));
}

TEST_CASE("Dragging the rail continues to update Value on Move", "[annotated_scroll_bar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(1000.0);
    bar.set_viewport_size(200.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    std::vector<double> requests;
    bar.on_scroll_to_requested([&](double offset) { requests.push_back(offset); });

    bar.handle_pointer(down({15.0f, 100.0f}));
    REQUIRE(requests.size() == 1);

    bar.handle_pointer(move({15.0f, 200.0f}));
    REQUIRE(requests.size() == 2);
    REQUIRE(requests[1] > requests[0]); // moved further down the rail -> larger offset

    bar.handle_pointer(up({15.0f, 200.0f}));
    bar.handle_pointer(move({15.0f, 250.0f}));
    REQUIRE(requests.size() == 2); // released: no longer dragging
}

TEST_CASE("The thumb reaches the rail's literal bottom edge at value()==maximum(), independent of label spacing",
          "[annotated_scroll_bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(1000.0);
    bar.set_viewport_size(200.0);
    // Labels only place labels. The thumb's mapping must ignore them, even a label past Maximum.
    bar.set_annotations({{.text = "Top", .scroll_offset = 0.0}, {.text = "End", .scroll_offset = 900.0}});
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    // thumb_bounds() is private, so read the thumb back off the render stream: the last
    // FillRoundedRect is always the thumb.
    const auto last_fill = [&]
    {
        TestRenderer r;
        bar.render(r, theme);
        for (auto it = r.commands.rbegin(); it != r.commands.rend(); ++it)
        {
            if (std::holds_alternative<FillRoundedRect>(*it))
            {
                return std::get<FillRoundedRect>(*it).bounds;
            }
        }
        FAIL("no FillRoundedRect in the render stream");
        return Rect{};
    };

    bar.set_value(1000.0); // == maximum()
    REQUIRE_THAT(last_fill().bottom(), WithinAbs(static_cast<double>(bar.bounds().bottom() - 16.0f), 1e-3));

    bar.set_value(0.0);
    REQUIRE_THAT(last_fill().top(), WithinAbs(static_cast<double>(bar.bounds().top() + 16.0f), 1e-3));
}

TEST_CASE("Dragging to the rail's literal bottom pixel requests exactly Maximum", "[annotated_scroll_bar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(1000.0);
    bar.set_viewport_size(200.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    double requested = -1.0;
    bar.on_scroll_to_requested([&](double offset) { requested = offset; });

    // One pixel above the down button, which wins the boundary pixel it shares with the rail.
    // The offset saturates at Maximum before this y anyway.
    bar.handle_pointer(down({15.0f, bar.bounds().bottom() - 17.0f}));
    REQUIRE_THAT(requested, WithinAbs(1000.0, 1e-3));
}

TEST_CASE("The top step button decreases Value, the bottom one increases it", "[annotated_scroll_bar][input]")
{
    // The top step button subtracts small_change.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(100.0);
    bar.set_viewport_size(40.0);
    bar.set_small_change(10.0);
    bar.set_value(50.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    std::vector<double> deltas;
    bar.on_scroll_by_requested([&](double delta) { deltas.push_back(delta); });

    bar.handle_pointer(down({15.0f, 8.0f})); // inside the top (up) button
    bar.handle_pointer(up({15.0f, 8.0f}));
    REQUIRE(deltas.size() == 1);
    REQUIRE_THAT(deltas[0], WithinAbs(-10.0, 1e-9));

    bar.handle_pointer(down({15.0f, 292.0f})); // inside the bottom (down) button
    bar.handle_pointer(up({15.0f, 292.0f}));
    REQUIRE(deltas.size() == 2);
    REQUIRE_THAT(deltas[1], WithinAbs(10.0, 1e-9));
}

TEST_CASE("A step button auto-disables at its own end of the range", "[annotated_scroll_bar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(100.0);
    bar.set_value(0.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    int calls = 0;
    bar.on_scroll_by_requested([&](double) { ++calls; });

    bar.handle_pointer(down({15.0f, 8.0f}));
    bar.handle_pointer(up({15.0f, 8.0f}));
    REQUIRE(calls == 0);

    bar.set_value(100.0);
    bar.handle_pointer(down({15.0f, 292.0f}));
    bar.handle_pointer(up({15.0f, 292.0f}));
    REQUIRE(calls == 0);
}

TEST_CASE("small_change() defaults to viewport_size / 8 when not set explicitly", "[annotated_scroll_bar]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(1000.0);
    bar.set_viewport_size(80.0);
    bar.set_value(500.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    double delta = 0.0;
    bar.on_scroll_by_requested([&](double d) { delta = d; });

    bar.handle_pointer(down({15.0f, 292.0f})); // bottom button: increases Value
    bar.handle_pointer(up({15.0f, 292.0f}));
    REQUIRE_THAT(delta, WithinAbs(10.0, 1e-9)); // 80 / 8
}

TEST_CASE("Home and End jump to Minimum and Maximum", "[annotated_scroll_bar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(500.0);
    bar.set_value(200.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});
    ui::testing::set_focused(bar, true);

    double requested = -1.0;
    bar.on_scroll_to_requested([&](double offset) { requested = offset; });

    bar.handle_key({Key::End, KeyAction::Down});
    REQUIRE_THAT(requested, WithinAbs(500.0, 1e-9));

    bar.handle_key({Key::Home, KeyAction::Down});
    REQUIRE_THAT(requested, WithinAbs(0.0, 1e-9));
}

TEST_CASE("on_scrolling returning true cancels the request", "[annotated_scroll_bar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(1000.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    bar.on_scrolling([] { return ui::Cancel; });

    bool requested = false;
    bar.on_scroll_to_requested([&](double) { requested = true; });

    bar.handle_pointer(down({15.0f, 150.0f}));
    REQUIRE_FALSE(requested);
}

TEST_CASE("set_target wires a Scroll: live range/value and forwarded scroll requests", "[annotated_scroll_bar][target]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    auto           view_owned = std::make_unique<ScrollControl>();
    ScrollControl* view       = view_owned.get();
    view->set_child(make_tall_stack(50)); // tall content -> a real scrollable range
    (void)view->measure({300, 200}, m, theme);
    view->arrange({0, 0, 300, 200});
    REQUIRE(view->scrollable_height() > 0.0f);

    AnnotatedScrollBarControl bar;
    bar.set_target(view);
    REQUIRE_THAT(bar.maximum(), WithinAbs(static_cast<double>(view->scrollable_height()), 1e-3));
    REQUIRE_THAT(bar.viewport_size(), WithinAbs(200.0, 1e-3));
    REQUIRE_THAT(bar.value(), WithinAbs(0.0, 1e-3));

    (void)bar.measure({30, 200}, m, theme);
    bar.arrange({0, 0, 30, 200});
    bar.handle_pointer(down({15.0f, 100.0f}));

    REQUIRE(view->vertical_offset() > 0.0f);
    REQUIRE_THAT(bar.value(), WithinAbs(static_cast<double>(view->vertical_offset()), 1e-3));

    bar.set_target(nullptr);
    REQUIRE_THAT(bar.maximum(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("set_target suppresses the target's own ScrollBarOverlay for as long as it's attached",
          "[annotated_scroll_bar][target]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    auto           view_a_owned = std::make_unique<ScrollControl>();
    ScrollControl* view_a       = view_a_owned.get();
    view_a->set_child(make_tall_stack(50));
    (void)view_a->measure({300, 200}, m, theme);
    view_a->arrange({0, 0, 300, 200});

    auto           view_b_owned = std::make_unique<ScrollControl>();
    ScrollControl* view_b       = view_b_owned.get();
    view_b->set_child(make_tall_stack(50));
    (void)view_b->measure({300, 200}, m, theme);
    view_b->arrange({0, 0, 300, 200});

    REQUIRE(view_a->show_scrollbar());
    REQUIRE(view_b->show_scrollbar());

    AnnotatedScrollBarControl bar;
    bar.set_target(view_a);
    REQUIRE_FALSE(view_a->show_scrollbar()); // this control now owns that UI instead

    bar.set_target(view_b);
    REQUIRE(view_a->show_scrollbar());
    REQUIRE_FALSE(view_b->show_scrollbar());

    bar.set_target(nullptr);
    REQUIRE(view_b->show_scrollbar());
}

TEST_CASE("No detail-label popup opens while on_detail_text_requested is unset", "[annotated_scroll_bar][popup]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(1000.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    bar.handle_pointer(move({15.0f, 150.0f}));
    REQUIRE_FALSE(ui::testing::popup_open(bar));
    REQUIRE(ui::testing::popup_content(bar) == nullptr);
}

TEST_CASE("Hovering the rail opens the detail-label popup with the requested content", "[annotated_scroll_bar][popup]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(1000.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    bar.on_detail_text_requested(
        [](double offset) { return offset > 500.0 ? std::string{"Late"} : std::string{"Early"}; });

    bar.handle_pointer(move({15.0f, 60.0f})); // near the top of the rail -> a small offset
    REQUIRE(ui::testing::popup_open(bar));
    Element* content = ui::testing::popup_content(bar);
    REQUIRE(content != nullptr);

    bar.handle_pointer(leave());
    REQUIRE_FALSE(ui::testing::popup_open(bar));
}

TEST_CASE("AnnotatedScrollBar renders its two step buttons, the rail track, and the thumb",
          "[annotated_scroll_bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(500.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    TestRenderer r;
    bar.render(r, theme);

    // Theme::light() is Fluent, whose step buttons are chromeless: only the rail track and
    // the thumb fill.
    REQUIRE(r.count_of<FillRoundedRect>() == 2);   // rail track + thumb; buttons paint no chrome
    REQUIRE(r.count_of<StrokeRoundedRect>() == 0); // chromeless buttons have no border
    REQUIRE(r.count_of<DrawTextRun>() == 2);       // the two step-button glyphs, no labels set
}

TEST_CASE("AnnotatedScrollBar's step buttons draw ordinary bordered chrome under a non-Fluent theme",
          "[annotated_scroll_bar][render]")
{
    // Only Theme::fluent() sets scroll_button_chromeless, so a classic theme keeps bordered buttons.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::classic(ClassicPalette::windows_standard(), false, "MS Sans Serif");

    AnnotatedScrollBarControl bar;
    bar.set_maximum(500.0);
    (void)bar.measure({30, 300}, m, theme);
    bar.arrange({0, 0, 30, 300});

    TestRenderer r;
    bar.render(r, theme);
    REQUIRE(r.count_of<FillRoundedRect>() == 4);   // 2 buttons + rail track + thumb
    REQUIRE(r.count_of<StrokeRoundedRect>() == 2); // 2 buttons' borders
}

TEST_CASE("Labels render as right-aligned text and collapse when colliding", "[annotated_scroll_bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    AnnotatedScrollBarControl bar;
    bar.set_maximum(1000.0);
    bar.set_viewport_size(200.0); // headroom below the last label's offset

    bar.set_annotations({{.text = "2024", .scroll_offset = 0.0}, {.text = "2020", .scroll_offset = 1000.0}});
    Size sz = bar.measure({Unbounded, 300}, m, theme);
    bar.arrange({0, 0, sz.width, 300});

    TestRenderer r1;
    bar.render(r1, theme);
    REQUIRE(r1.count_of<DrawTextRun>() == 2 + 2); // 2 step-button glyphs + 2 labels

    bar.set_annotations({{.text = "2024", .scroll_offset = 0.0}, {.text = "2024.001", .scroll_offset = 0.01}});
    sz = bar.measure({Unbounded, 300}, m, theme);
    bar.arrange({0, 0, sz.width, 300});

    TestRenderer r2;
    bar.render(r2, theme);
    REQUIRE(r2.count_of<DrawTextRun>() == 2 + 1); // one of the two colliding labels is collapsed
}

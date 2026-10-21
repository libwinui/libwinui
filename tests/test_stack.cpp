// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/panels/Fill.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/Spacer.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
// Records the `available` extent it was last measured with.
class MeasureProbe final : public Element
{
public:
    mutable Size last_available{-1.0f, -1.0f};

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
    {
        last_available = available;
        return Size{10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
};

std::unique_ptr<StackControl> make_button_stack(int n, float spacing)
{
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->set_spacing(spacing);
    for (int i = 0; i < n; ++i)
    {
        stack->add(std::make_unique<ButtonControl>("Item"));
    }
    return stack;
}
} // namespace

TEST_CASE("TextControl measures its text and emits one DrawTextRun", "[text]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    TextControl          t{"Hello"};

    const Size desired = t.measure({Unbounded, Unbounded}, m, theme);
    const Size text    = m.measure("Hello", theme.type.body); // the Body rung, with its line height
    REQUIRE_THAT(desired.width, WithinAbs(text.width, 1e-4));
    REQUIRE_THAT(desired.height, WithinAbs(text.height, 1e-4));

    t.arrange({10, 20, desired.width, desired.height});
    TestRenderer r;
    t.render(r, theme);
    REQUIRE(r.count() == 1);
    REQUIRE(r.count_of<DrawTextRun>() == 1);
    REQUIRE(std::get<DrawTextRun>(r.commands[0]).text == "Hello");
}

TEST_CASE("IconControl measures a square glyph footprint and emits one DrawTextRun", "[icon]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    IconControl          icon{glyphs::Accept};

    const Size desired = icon.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(desired.width, WithinAbs(glyphs::Accept.size, 1e-4));
    REQUIRE_THAT(desired.height, WithinAbs(glyphs::Accept.size, 1e-4));

    icon.arrange({10, 20, desired.width, desired.height});
    TestRenderer r;
    icon.render(r, theme);
    REQUIRE(r.count() == 1);
    REQUIRE(r.count_of<DrawTextRun>() == 1);
    REQUIRE(std::get<DrawTextRun>(r.commands[0]).text == encode_utf8(glyphs::Accept.codepoint));
}

TEST_CASE("Vertical stack desired size = sum of children + spacing", "[stack][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    const float spacing = 12.0f;
    auto        stack   = make_button_stack(3, spacing);
    const Size  desired = stack->measure({Unbounded, Unbounded}, m, theme);

    // Each button measures the same, so height is 3*button + 2*spacing; width is one button.
    const Size one = ButtonControl{"Item"}.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(desired.height, WithinAbs(3 * one.height + 2 * spacing, 1e-3));
    REQUIRE_THAT(desired.width, WithinAbs(one.width, 1e-3));
}

TEST_CASE("Vertical stack arranges children top-to-bottom with spacing and cross-centering", "[stack][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    const float spacing = 10.0f;
    auto        stack   = make_button_stack(2, spacing);
    stack->set_child_alignment(Alignment::Center);

    const Size desired = stack->measure({Unbounded, Unbounded}, m, theme);
    stack->arrange({0, 0, desired.width, desired.height});

    const auto& a = stack->children()[0]->bounds();
    const auto& b = stack->children()[1]->bounds();

    REQUIRE_THAT(a.top(), WithinAbs(0.0f, 0.5));
    // Second child starts one child-height + spacing below the first.
    REQUIRE_THAT(b.top(), WithinAbs(a.bottom() + spacing, 0.5));
    // Both are horizontally centered within the panel (same width here => x ~ 0).
    REQUIRE_THAT(a.center().x, WithinAbs(desired.width / 2.0, 0.5));
    REQUIRE_THAT(b.center().x, WithinAbs(desired.width / 2.0, 0.5));
}

TEST_CASE("Stretch cross-alignment sizes children to the panel width", "[stack][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    auto stack = std::make_unique<StackControl>();
    stack->set_child_alignment(Alignment::Stretch);
    stack->add(std::make_unique<ButtonControl>("A"));
    stack->add(std::make_unique<ButtonControl>("Wiiiiiide"));

    const Size desired = stack->measure({Unbounded, Unbounded}, m, theme);
    stack->arrange({0, 0, desired.width, desired.height});

    for (const auto& child : stack->children())
    {
        REQUIRE_THAT(child->bounds().width, WithinAbs(desired.width, 0.5));
    }
}

TEST_CASE("Horizontal stack lays children left-to-right", "[stack][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    auto stack = std::make_unique<StackControl>(Orientation::Horizontal);
    stack->set_spacing(5.0f);
    stack->add(std::make_unique<ButtonControl>("A"));
    stack->add(std::make_unique<ButtonControl>("B"));

    const Size desired = stack->measure({Unbounded, Unbounded}, m, theme);
    stack->arrange({0, 0, desired.width, desired.height});

    const auto& a = stack->children()[0]->bounds();
    const auto& b = stack->children()[1]->bounds();
    REQUIRE_THAT(b.left(), WithinAbs(a.right() + 5.0, 0.5));
    REQUIRE_THAT(a.top(), WithinAbs(b.top(), 0.5)); // same row
}

TEST_CASE("Window centers a stack and routes a click to the inner button", "[stack][headless][input]")
{
    HeadlessUI ui = create_headless();

    // Keep observers to the children before their ownership moves into the stack.
    auto           label_owned = ui.text("Title");
    auto           save_owned  = ui.button("Save");
    TextControl*   label       = label_owned.get();
    ButtonControl* save        = save_owned.get();
    StackControl*  stack = ui.window().set_content(ui.vertical_stack(std::move(label_owned), std::move(save_owned)));
    ui.layout({800, 600});

    REQUIRE_THAT(stack->bounds().center().x, WithinAbs(400.0, 0.6));
    REQUIRE_THAT(stack->bounds().center().y, WithinAbs(300.0, 0.6));

    int clicks = 0;
    save->on_click([&] { ++clicks; });

    const Point p = save->bounds().center();
    ui::testing::seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Up});
    REQUIRE(clicks == 1);

    // A press on the (non-interactive) label region does not click the button.
    const Point lp = label->bounds().center();
    ui::testing::seams(ui.window()).handle_pointer({lp, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({lp, PointerButton::Left, PointerAction::Up});
    REQUIRE(clicks == 1);
}

TEST_CASE("StackControl routes Wheel only to the child whose bounds contain the cursor", "[stack][input][wheel-routing]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    // A StackControl measures children unbounded along the stack, so
    // a Scroll straight inside one could never scroll. A fixed Grid cell gives it a viewport.
    auto root = std::make_unique<StackControl>(Orientation::Vertical);
    root->set_spacing(0.0f);
    TextControl* title = root->add(std::make_unique<TextControl>("Title"));
    auto         cell  = std::make_unique<GridControl>();
    cell->add_row(GridLength::dip(120.0f));
    cell->add_column(GridLength::dip(300.0f));
    ScrollControl* scroll = cell->add(std::make_unique<ScrollControl>(), 0, 0);
    scroll->set_child(make_button_stack(20, 0.0f));
    root->add(std::move(cell));

    (void)root->measure({300, 300}, m, theme);
    root->arrange({0, 0, 300, 300});

    REQUIRE_THAT(scroll->vertical_offset(), WithinAbs(0.0, 1e-3));

    // Wheel goes only to the child under the cursor, so a turn over the title is not consumed.
    REQUIRE_FALSE(root->handle_wheel({title->bounds().center(), PointerButton::None, PointerAction::Wheel, 40.0f}));
    REQUIRE_THAT(scroll->vertical_offset(), WithinAbs(0.0, 1e-3));

    REQUIRE(root->handle_wheel({scroll->bounds().center(), PointerButton::None, PointerAction::Wheel, 40.0f}));
    REQUIRE_THAT(scroll->vertical_offset(), WithinAbs(40.0, 1e-3));
}

TEST_CASE("Tab cycles focus across the focusable controls only", "[stack][headless][focus]")
{
    HeadlessUI ui = create_headless();

    auto           label_owned  = ui.text("Title"); // not focusable
    auto           save_owned   = ui.button("Save");
    auto           cancel_owned = ui.button("Cancel");
    ButtonControl* save         = save_owned.get();
    ButtonControl* cancel       = cancel_owned.get();
    ui.window().set_content(ui.vertical_stack(std::move(label_owned), std::move(save_owned), std::move(cancel_owned)));
    ui.layout({400, 300});

    Window& win = ui.window();
    REQUIRE(win.focused() == nullptr);

    win.focus_content();
    REQUIRE(win.focused() == save); // first *focusable* (label is skipped)
    REQUIRE(save->focused());

    ui::testing::seams(win).handle_key({Key::Tab, KeyAction::Down});
    REQUIRE(win.focused() == cancel);
    REQUIRE_FALSE(save->focused());
    REQUIRE(cancel->focused());

    ui::testing::seams(win).handle_key({Key::Tab, KeyAction::Down}); // wraps back to the first
    REQUIRE(win.focused() == save);

    win.blur();
    REQUIRE(win.focused() == nullptr);
    REQUIRE_FALSE(save->focused());
}

TEST_CASE("Shift+Tab cycles focus backward across the focusable controls only", "[stack][headless][focus]")
{
    HeadlessUI ui = create_headless();

    auto           label_owned  = ui.text("Title"); // not focusable
    auto           save_owned   = ui.button("Save");
    auto           cancel_owned = ui.button("Cancel");
    ButtonControl* save         = save_owned.get();
    ButtonControl* cancel       = cancel_owned.get();
    ui.window().set_content(ui.vertical_stack(std::move(label_owned), std::move(save_owned), std::move(cancel_owned)));
    ui.layout({400, 300});

    Window& win = ui.window();

    win.focus_content();
    REQUIRE(win.focused() == save); // first *focusable* (label is skipped)

    // Shift+Tab from the first focusable wraps to the last.
    ui::testing::seams(win).handle_key({Key::Tab, KeyAction::Down, {.shift = true}});
    REQUIRE(win.focused() == cancel);
    REQUIRE_FALSE(save->focused());
    REQUIRE(cancel->focused());

    // Shift+Tab again steps back to save.
    ui::testing::seams(win).handle_key({Key::Tab, KeyAction::Down, {.shift = true}});
    REQUIRE(win.focused() == save);

    // Plain Tab still goes forward from wherever Shift+Tab left off.
    ui::testing::seams(win).handle_key({Key::Tab, KeyAction::Down});
    REQUIRE(win.focused() == cancel);
}

TEST_CASE("A disabled button is skipped in the tab order", "[stack][headless][focus]")
{
    HeadlessUI     ui      = create_headless();
    auto           a_owned = ui.button("A");
    auto           b_owned = ui.button("B");
    auto           c_owned = ui.button("C");
    ButtonControl* a       = a_owned.get();
    ButtonControl* b       = b_owned.get();
    ButtonControl* c       = c_owned.get();
    b->set_enabled(false);
    ui.window().set_content(ui.vertical_stack(std::move(a_owned), std::move(b_owned), std::move(c_owned)));
    ui.layout({400, 300});

    Window& win = ui.window();
    win.focus_content();
    REQUIRE(win.focused() == a);
    ui::testing::seams(win).handle_key({Key::Tab, KeyAction::Down});
    REQUIRE(win.focused() == c); // b (disabled) is skipped
}

TEST_CASE("Children are measured unbounded along the stack axis", "[stack][layout]")
{
    // A stack offers infinity along the stack and
    // the panel's own extent across it.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    StackControl  vertical{Orientation::Vertical};
    MeasureProbe* v_child = vertical.add(std::make_unique<MeasureProbe>());
    (void)vertical.measure({300.0f, 200.0f}, m, theme);
    REQUIRE_THAT(v_child->last_available.width, WithinAbs(300.0, 1e-3));
    REQUIRE(v_child->last_available.height == Unbounded);

    StackControl  horizontal{Orientation::Horizontal};
    MeasureProbe* h_child = horizontal.add(std::make_unique<MeasureProbe>());
    (void)horizontal.measure({300.0f, 200.0f}, m, theme);
    REQUIRE(h_child->last_available.width == Unbounded);
    REQUIRE_THAT(h_child->last_available.height, WithinAbs(200.0, 1e-3));
}

TEST_CASE("insert() places a child at an index, clamped to the end, and parents it", "[stack]")
{
    StackControl panel;
    TextControl* last  = panel.add(std::make_unique<TextControl>("Status"));
    TextControl* first = panel.insert(0, std::make_unique<TextControl>("Extra"));
    TextControl* end   = panel.insert(99, std::make_unique<TextControl>("End"));

    REQUIRE(panel.children().size() == 3);
    CHECK(panel.children()[0].get() == first);
    CHECK(panel.children()[1].get() == last);
    CHECK(panel.children()[2].get() == end);
    CHECK(first->parent() == &panel);

    CHECK(panel.remove(first));
    CHECK(panel.children().front().get() == last);
}

TEST_CASE("a Spacer takes the room a stack's children leave", "[stack][spacer]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme   = Theme::light();
    const auto           lay_out = [&](Element& root, Size size)
    {
        (void)root.measure(size, measurer, theme);
        root.arrange(Rect{0.0f, 0.0f, size.width, size.height});
    };

    ui::Control<ui::Text>    left;
    ui::Control<ui::Spacer>  gap;
    ui::Control<ui::Text>    right;
    std::unique_ptr<Element> bar = ui::build(ui::HorizontalStack{
        .spacing = 10.0f,
        .content =
            {
                ui::Text{.text = "L", .width = 40.0f, .control = left},
                ui::Spacer{.control = gap},
                ui::Text{.text = "R", .width = 60.0f, .control = right},
            },
    });

    // The stack asks for no more than its children: the spacer adds nothing of its own.
    CHECK(bar->measure({Unbounded, Unbounded}, measurer, theme).width == 120.0f);

    // Given 300, the 180 left over go to the spacer and the last child ends at the far edge.
    lay_out(*bar, {300.0f, 40.0f});
    CHECK(left->bounds().x == 0.0f);
    CHECK(gap->bounds().width == 180.0f);
    CHECK(right->bounds().x == 240.0f);
    CHECK(right->bounds().right() == 300.0f);

    // Collapsed, it gives the room back and its neighbors close up.
    gap->set_visibility(Visibility::Collapsed);
    lay_out(*bar, {300.0f, 40.0f});
    CHECK(right->bounds().x == 50.0f); // one gap of 10 between the two that are left, not two
    CHECK(bar->measure({Unbounded, Unbounded}, measurer, theme).width == 110.0f);
}

TEST_CASE("several Spacers share the leftover room by weight, in either direction", "[stack][spacer]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    ui::Control<ui::Spacer>  one;
    ui::Control<ui::Spacer>  three;
    ui::Control<ui::Text>    middle;
    std::unique_ptr<Element> column = ui::build(ui::VerticalStack{
        .content =
            {
                ui::Spacer{.control = one},
                ui::Text{.text = "middle", .height = 20.0f, .control = middle},
                ui::Spacer{.weight = 3.0f, .min_height = 10.0f, .control = three},
            },
    });
    // The minimum gap counts toward what the stack asks for.
    CHECK(column->measure({100.0f, Unbounded}, measurer, theme).height == 30.0f);

    (void)column->measure({100.0f, 230.0f}, measurer, theme);
    column->arrange(Rect{0.0f, 0.0f, 100.0f, 230.0f});
    // 200 left over: 50 above, 150 below on top of the 10 it already had.
    CHECK(one->bounds().height == 50.0f);
    CHECK(middle->bounds().y == 50.0f);
    CHECK(three->bounds().height == 160.0f);
}

TEST_CASE("a Fill gets the room the other children of a vertical stack leave", "[stack][fill]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    auto                     owned = std::make_unique<MeasureProbe>();
    MeasureProbe*            probe = owned.get();
    ui::Control<ui::Fill>    fill;
    ui::Control<ui::Text>    footer;
    std::unique_ptr<Element> column = ui::build(ui::VerticalStack{
        .spacing = 10.0f,
        .content =
            {
                ui::Text{.text = "header", .height = 20.0f},
                ui::Fill{.control = fill, .content = std::unique_ptr<Element>{std::move(owned)}},
                ui::Text{.text = "footer", .height = 30.0f, .control = footer},
            },
    });

    // 200 less the header, the footer and two gaps: the fill is measured against 130 and asks
    // for all of it, so the stack asks for the whole 200.
    CHECK(column->measure({100.0f, 200.0f}, measurer, theme).height == 200.0f);
    CHECK(probe->last_available.height == 130.0f);
    column->arrange(Rect{0.0f, 0.0f, 100.0f, 200.0f});
    CHECK(fill->bounds().y == 30.0f);
    CHECK(fill->bounds().height == 130.0f);
    CHECK(footer->bounds().y == 170.0f);
}

TEST_CASE("several Fills share the room by weight in a horizontal stack, spacers included", "[stack][fill]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    ui::Control<ui::Fill>    one;
    ui::Control<ui::Fill>    three;
    ui::Control<ui::Spacer>  gap;
    std::unique_ptr<Element> row = ui::build(ui::HorizontalStack{
        .content =
            {
                ui::Text{.text = "L", .width = 40.0f},
                ui::Fill{.control = one, .content = ui::Text{.text = "a"}},
                ui::Fill{.weight = 3.0f, .control = three, .content = ui::Text{.text = "b"}},
            },
    });
    (void)row->measure({440.0f, 40.0f}, measurer, theme);
    row->arrange(Rect{0.0f, 0.0f, 440.0f, 40.0f});
    CHECK(one->bounds().width == 100.0f);
    CHECK(three->bounds().x == 140.0f);
    CHECK(three->bounds().width == 300.0f);

    std::unique_ptr<Element> shared = ui::build(ui::HorizontalStack{
        .content =
            {
                ui::Fill{.control = one, .content = ui::Text{.text = "a"}},
                ui::Spacer{.control = gap},
            },
    });
    (void)shared->measure({400.0f, 40.0f}, measurer, theme);
    shared->arrange(Rect{0.0f, 0.0f, 400.0f, 40.0f});
    CHECK(one->bounds().width == 200.0f);
    CHECK(gap->bounds().width == 200.0f);
}

TEST_CASE("a collapsed Fill takes no room and no share", "[stack][fill]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    ui::Control<ui::Fill>    hidden;
    ui::Control<ui::Fill>    shown;
    std::unique_ptr<Element> column = ui::build(ui::VerticalStack{
        .content =
            {
                ui::Fill{.visible = false, .control = hidden, .content = ui::Text{.text = "a"}},
                ui::Fill{.control = shown, .content = ui::Text{.text = "b"}},
            },
    });
    (void)column->measure({100.0f, 300.0f}, measurer, theme);
    column->arrange(Rect{0.0f, 0.0f, 100.0f, 300.0f});
    CHECK(shown->bounds().height == 300.0f);
}

TEST_CASE("a Scroll in a Fill is bounded and scrolls", "[stack][fill]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    ui::Control<ui::Scroll>  scroll;
    std::unique_ptr<Element> column = ui::build(ui::VerticalStack{
        .content =
            {
                ui::Text{.text = "header", .height = 20.0f},
                ui::Fill{.content = ui::Scroll{.control = scroll, .content = ui::Text{.text = "long", .height = 1000.0f}}},
            },
    });
    (void)column->measure({100.0f, 200.0f}, measurer, theme);
    column->arrange(Rect{0.0f, 0.0f, 100.0f, 200.0f});
    CHECK(scroll->bounds().height == 180.0f);
    CHECK(scroll->scrollable_height() == 820.0f);
}

TEST_CASE("in a stack with no bound along its axis a Fill is as big as its content", "[stack][fill]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    auto                     owned = std::make_unique<MeasureProbe>();
    MeasureProbe*            probe = owned.get();
    ui::Control<ui::Fill>    fill;
    std::unique_ptr<Element> page = ui::build(ui::Scroll{
        .content =
            ui::VerticalStack{
                .content =
                    {
                        ui::Text{.text = "header", .height = 20.0f},
                        ui::Fill{.control = fill, .content = std::unique_ptr<Element>{std::move(owned)}},
                    },
            },
    });
    (void)page->measure({100.0f, 200.0f}, measurer, theme);
    page->arrange(Rect{0.0f, 0.0f, 100.0f, 200.0f});
    CHECK(probe->last_available.height == Unbounded);
    // The scroll view arranges the short page at its own height, and the fill takes what is left.
    CHECK(fill->bounds().height == 180.0f);

    std::unique_ptr<Element> tall = ui::build(ui::Scroll{
        .content =
            ui::VerticalStack{
                .content =
                    {
                        ui::Text{.text = "header", .height = 20.0f},
                        ui::Fill{.control = fill, .content = ui::Text{.text = "long", .height = 500.0f}},
                    },
            },
    });
    (void)tall->measure({100.0f, 200.0f}, measurer, theme);
    tall->arrange(Rect{0.0f, 0.0f, 100.0f, 200.0f});
    CHECK(fill->bounds().height == 500.0f);
}

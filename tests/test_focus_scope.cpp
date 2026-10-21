// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/containers/Expander.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

namespace
{
KeyEvent key_down(Key k, bool shift = false)
{
    return {k, KeyAction::Down, {.shift = shift}};
}
KeyEvent key_up(Key k)
{
    return {k, KeyAction::Up};
}

struct Group
{
    ExpanderControl* expander = nullptr;
    ButtonControl*   first    = nullptr;
    ButtonControl*   second   = nullptr;
};

Group make_group(HeadlessUI& ui, StackControl& parent, bool expanded, bool scope)
{
    Group g;
    auto  stack        = std::make_unique<StackControl>(Orientation::Vertical);
    auto  first_owned  = ui.button("One");
    auto  second_owned = ui.button("Two");
    g.first            = stack->add(std::move(first_owned));
    g.second           = stack->add(std::move(second_owned));

    auto expander = std::make_unique<ExpanderControl>("Group", expanded);
    expander->set_focus_scope(scope);
    expander->set_content(std::move(stack));
    g.expander = parent.add(std::move(expander));
    return g;
}
} // namespace

TEST_CASE("A container scope keeps its content out of the outer tab ring", "[focus][scope]")
{
    HeadlessUI    ui    = create_headless();
    auto          stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl* root  = stack.get();
    ui.window().set_content(std::move(stack));

    const Group flat  = make_group(ui, *root, /*expanded=*/true, /*scope=*/false);
    const Group scope = make_group(ui, *root, /*expanded=*/true, /*scope=*/true);
    ui.layout({400, 600});

    Window& win = ui.window();
    win.focus_content();

    REQUIRE(win.focused() == flat.expander);
    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    REQUIRE(win.focused() == flat.first);
    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    REQUIRE(win.focused() == flat.second);

    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    REQUIRE(win.focused() == scope.expander);
    ui::testing::seams(win).handle_key(key_down(Key::Tab)); // wraps, rather than descending into the scope
    REQUIRE(win.focused() == flat.expander);
}

TEST_CASE("Enter steps into a container scope, Escape steps back out", "[focus][scope]")
{
    HeadlessUI    ui    = create_headless();
    auto          stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl* root  = stack.get();
    ui.window().set_content(std::move(stack));

    const Group    group       = make_group(ui, *root, /*expanded=*/true, /*scope=*/true);
    auto           after_owned = ui.button("After");
    ButtonControl* after       = root->add(std::move(after_owned));
    ui.layout({400, 600});

    Window& win = ui.window();
    win.focus_content();
    REQUIRE(win.focused() == group.expander);
    REQUIRE(win.entered_focus_scope() == nullptr);

    ui::testing::seams(win).handle_key(key_down(Key::Enter));
    REQUIRE(win.entered_focus_scope() == group.expander);
    REQUIRE(win.focused() == group.first); // focus lands on the first thing inside

    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    REQUIRE(win.focused() == group.second);
    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    REQUIRE(win.focused() == group.first); // wrapped inside the scope, not out of it
    ui::testing::seams(win).handle_key(key_down(Key::Tab, /*shift=*/true));
    REQUIRE(win.focused() == group.second);

    ui::testing::seams(win).handle_key(key_down(Key::Escape));
    REQUIRE(win.entered_focus_scope() == nullptr);
    REQUIRE(win.focused() == group.expander);
    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    REQUIRE(win.focused() == after);
}

TEST_CASE("Entering a scope with nothing inside is refused and the key falls through", "[focus][scope]")
{
    HeadlessUI    ui    = create_headless();
    auto          stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl* root  = stack.get();
    ui.window().set_content(std::move(stack));

    const Group group = make_group(ui, *root, /*expanded=*/false, /*scope=*/true);
    ui.layout({400, 600});

    Window& win = ui.window();
    win.focus_content();
    REQUIRE(win.focused() == group.expander);

    // Collapsed, the content is not live, so there is nothing to enter.
    ui::testing::seams(win).handle_key(key_down(Key::Enter));
    REQUIRE(win.entered_focus_scope() == nullptr);
    REQUIRE(win.focused() == group.expander);

    ui::testing::seams(win).handle_key(key_down(Key::Space));
    ui::testing::seams(win).handle_key(key_up(Key::Space));
    REQUIRE(group.expander->expanded());
    ui.layout({400, 600});

    ui::testing::seams(win).handle_key(key_down(Key::Enter));
    REQUIRE(win.entered_focus_scope() == group.expander);
    REQUIRE(win.focused() == group.first);
}

TEST_CASE("An entered scope unwinds when it stops being enterable", "[focus][scope]")
{
    HeadlessUI    ui    = create_headless();
    auto          stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl* root  = stack.get();
    ui.window().set_content(std::move(stack));

    const Group group = make_group(ui, *root, /*expanded=*/true, /*scope=*/true);
    ui.layout({400, 600});

    Window& win = ui.window();
    win.focus_content();
    ui::testing::seams(win).handle_key(key_down(Key::Enter));
    REQUIRE(win.entered_focus_scope() == group.expander);

    // Nothing tells the Window; the next rebuild truncates the scope stack there.
    group.expander->set_expanded(false);
    ui.layout({400, 600});

    REQUIRE(win.entered_focus_scope() == nullptr);
    REQUIRE(win.focused() == nullptr); // the button it was on no longer exists in any ring
    win.focus_content();
    REQUIRE(win.focused() == group.expander);
}

TEST_CASE("An entered scope destroyed before the next layout is forgotten at once", "[focus][scope]")
{
    HeadlessUI    ui    = create_headless();
    auto          stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl* root  = stack.get();
    ui.window().set_content(std::move(stack));

    const Group    group       = make_group(ui, *root, /*expanded=*/true, /*scope=*/true);
    auto           after_owned = ui.button("After");
    ButtonControl* after       = root->add(std::move(after_owned));
    ui.layout({400, 600});

    Window& win = ui.window();
    win.focus_content();
    ui::testing::seams(win).handle_key(key_down(Key::Enter));
    REQUIRE(win.entered_focus_scope() == group.expander);

    REQUIRE(root->remove(group.expander)); // destroyed now: no dispatch is running
    REQUIRE(win.entered_focus_scope() == nullptr);
    REQUIRE(win.focused() == nullptr);

    // Escape and Tab arrive before any layout() and step over the dead scope.
    ui::testing::seams(win).handle_key(key_down(Key::Escape));
    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    REQUIRE(win.focused() == after);
}

TEST_CASE("A click is never trapped by an entered scope", "[focus][scope]")
{
    HeadlessUI    ui    = create_headless();
    auto          stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl* root  = stack.get();
    ui.window().set_content(std::move(stack));

    const Group    group       = make_group(ui, *root, /*expanded=*/true, /*scope=*/true);
    auto           after_owned = ui.button("After");
    ButtonControl* after       = root->add(std::move(after_owned));
    ui.layout({400, 600});

    Window& win = ui.window();
    win.focus_content();
    ui::testing::seams(win).handle_key(key_down(Key::Enter));
    REQUIRE(win.entered_focus_scope() == group.expander);

    ui::testing::seams(win).handle_pointer({after->bounds().center(), PointerButton::Left, PointerAction::Down});
    REQUIRE(win.entered_focus_scope() == nullptr);
    REQUIRE(win.focused() == after);
}

TEST_CASE("An item-navigating scope moves a cursor without changing the selection", "[focus][scope][list]")
{
    HeadlessUI    ui    = create_headless();
    auto          stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl* root  = stack.get();
    ui.window().set_content(std::move(stack));

    auto list = std::make_unique<ListViewControl>();
    list->set_items({"Alpha", "Beta", "Gamma"});
    list->set_selected_index(0);
    list->set_item_height(32.0f);
    list->set_focus_scope(true);
    ListViewControl* view = root->add(std::move(list));

    std::vector<std::optional<std::size_t>> selections;
    view->on_selection_changed([&selections](std::optional<std::size_t> i) { selections.push_back(i); });
    ui.layout({400, 600});

    Window& win = ui.window();
    win.focus_content();
    REQUIRE(win.focused() == view);
    REQUIRE(view->focus_index() == std::nullopt); // no cursor until entered

    ui::testing::seams(win).handle_key(key_down(Key::Enter));
    REQUIRE(win.entered_focus_scope() == view);
    REQUIRE(win.focused() == view); // rows are never in the ring, so the list stays focused
    REQUIRE(view->focus_index() == 0);

    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    REQUIRE(view->focus_index() == 1);
    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    REQUIRE(view->focus_index() == 2);
    ui::testing::seams(win).handle_key(key_down(Key::Tab)); // wraps within the list rather than leaving it
    REQUIRE(view->focus_index() == 0);
    ui::testing::seams(win).handle_key(key_down(Key::Tab, /*shift=*/true));
    REQUIRE(view->focus_index() == 2);
    REQUIRE(win.entered_focus_scope() == view);
    REQUIRE(selections.empty());
    REQUIRE(view->selected_index() == 0);

    ui::testing::seams(win).handle_key(key_down(Key::Enter));
    REQUIRE(view->selected_index() == 2);
    REQUIRE(selections == std::vector<std::optional<std::size_t>>{2});

    ui::testing::seams(win).handle_key(key_down(Key::Escape));
    REQUIRE(win.entered_focus_scope() == nullptr);
    REQUIRE(view->focus_index() == std::nullopt);
    ui::testing::seams(win).handle_key(key_down(Key::ArrowUp));
    REQUIRE(view->selected_index() == 1);
}

TEST_CASE("A list that is not a scope keeps its arrows-select behavior", "[focus][scope][list]")
{
    HeadlessUI    ui    = create_headless();
    auto          stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl* root  = stack.get();
    ui.window().set_content(std::move(stack));

    auto list = std::make_unique<ListViewControl>();
    list->set_items({"Alpha", "Beta", "Gamma"});
    ListViewControl* view = root->add(std::move(list));
    ui.layout({400, 600});

    Window& win = ui.window();
    win.focus_content();
    ui::testing::seams(win).handle_key(key_down(Key::Enter)); // not a scope: nothing to enter
    REQUIRE(win.entered_focus_scope() == nullptr);

    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    REQUIRE(view->selected_index() == 1);
    REQUIRE(view->focus_index() == std::nullopt);
}

TEST_CASE("Focusing an element below the fold scrolls it into view", "[focus][scroll]")
{
    HeadlessUI ui    = create_headless();
    auto       stack = std::make_unique<StackControl>(Orientation::Vertical);

    std::vector<ButtonControl*> buttons;
    buttons.reserve(12);
    for (int i = 0; i < 12; ++i)
    {
        buttons.push_back(stack->add(ui.button("Row")));
    }

    auto           scroll_owned = std::make_unique<ScrollControl>();
    ScrollControl* scroll       = scroll_owned.get();
    scroll->set_child(std::move(stack));
    ui.window().set_content(std::move(scroll_owned));
    ui.layout({400, 120}); // a viewport far shorter than the twelve rows

    Window& win = ui.window();
    REQUIRE(scroll->scrollable_height() > 0.0f);

    win.focus_content();
    REQUIRE(win.focused() == buttons.front());
    REQUIRE(scroll->vertical_offset() == 0.0f); // the first row was already in view

    for (std::size_t i = 1; i < buttons.size(); ++i)
    {
        ui::testing::seams(win).handle_key(key_down(Key::Tab));
        REQUIRE(win.focused() == buttons[i]);
        // The focus ring's band counts as part of what must be visible.
        const Rect row = buttons[i]->bounds();
        REQUIRE(row.top() >= scroll->bounds().top());
        REQUIRE(row.bottom() <= scroll->bounds().bottom());
    }
    REQUIRE(scroll->vertical_offset() > 0.0f);

    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    REQUIRE(win.focused() == buttons.front());
    REQUIRE(scroll->vertical_offset() == 0.0f);
}

namespace
{
// Stands in for a grid that draws all its cards itself and names the part to reveal on focus.
class TallCanvas final : public Element
{
public:
    std::optional<Rect> area_in_bounds; // relative to bounds().top_left(); nullopt declines

    [[nodiscard]] bool focusable() const override
    {
        return true;
    }
    [[nodiscard]] std::optional<Rect> bring_into_view_area() const override
    {
        if (!area_in_bounds)
        {
            return std::nullopt;
        }
        return Rect{bounds().x + area_in_bounds->x,
                    bounds().y + area_in_bounds->y,
                    area_in_bounds->width,
                    area_in_bounds->height};
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {300.0f, 1000.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
};
} // namespace

TEST_CASE("An element decides what focusing it brings into view", "[focus][scroll]")
{
    HeadlessUI     ui           = create_headless();
    auto           canvas_owned = std::make_unique<TallCanvas>();
    TallCanvas*    canvas       = canvas_owned.get();
    auto           scroll_owned = std::make_unique<ScrollControl>();
    ScrollControl* scroll       = scroll_owned.get();
    scroll->set_child(std::move(canvas_owned));
    ui.window().set_content(std::move(scroll_owned));
    ui.layout({400, 120});
    REQUIRE(scroll->scrollable_height() > 0.0f);
    Window& win = ui.window();

    SECTION("nullopt leaves the scroller where it is")
    {
        scroll->scroll_to(scroll->horizontal_offset(), 300.0f, {.animation_mode = ui::ScrollAnimationMode::Disabled});
        win.focus_content();
        REQUIRE(win.focused() == canvas);
        REQUIRE(scroll->vertical_offset() == 300.0f);
    }
    SECTION("a smaller area is revealed instead of the whole element")
    {
        canvas->area_in_bounds = Rect{0.0f, 500.0f, 300.0f, 60.0f};
        win.focus_content();
        REQUIRE(scroll->vertical_offset() > 0.0f); // the default, the whole canvas, would pin it at 0
        const float card_top = canvas->bounds().top() + 500.0f;
        REQUIRE(card_top >= scroll->bounds().top());
        REQUIRE(card_top + 60.0f <= scroll->bounds().bottom());
    }
}

TEST_CASE("The entered row's focus ring stays inside the list", "[focus][scope][list][render]")
{
    HeadlessUI    ui    = create_headless();
    auto          stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl* root  = stack.get();
    ui.window().set_content(std::move(stack));

    auto list = std::make_unique<ListViewControl>();
    list->set_items({"Alpha", "Beta", "Gamma"});
    list->set_selected_index(0);
    list->set_item_height(32.0f);
    list->set_focus_scope(true);
    ListViewControl* view = root->add(std::move(list));
    ui.layout({400, 600});

    Window& win = ui.window();
    win.focus_content();
    ui::testing::seams(win).handle_key(key_down(Key::Enter));
    ui::testing::seams(win).handle_key(key_down(Key::Tab)); // cursor on row 1, a row with neighbors on every side
    REQUIRE(view->focus_index() == 1);

    TestRenderer renderer;
    ui::testing::render_with_focus_ring(*view, renderer, win.theme());

    // A row inside the list rings inside itself. Every stroked band must land inside the list.
    const Rect  bounds = view->bounds();
    std::size_t rings  = 0;
    for (const RenderCommand& command : renderer.commands)
    {
        const auto* stroke = std::get_if<StrokeRoundedRect>(&command);
        if (stroke == nullptr)
        {
            continue;
        }
        ++rings;
        const float half = stroke->stroke_width * 0.5f;
        REQUIRE(stroke->bounds.left() - half >= bounds.left());
        REQUIRE(stroke->bounds.right() + half <= bounds.right());
        REQUIRE(stroke->bounds.top() - half >= bounds.top());
        REQUIRE(stroke->bounds.bottom() + half <= bounds.bottom());
    }
    REQUIRE(rings == 2); // the Fluent two-ring visual, both rings accounted for
}

// ---------------------------------------------------------------------------
// Directional (XY) focus navigation
// ---------------------------------------------------------------------------

TEST_CASE("Arrow keys move focus to the nearest element in that direction", "[focus][xy]")
{
    HeadlessUI ui = create_headless();

    auto           left = std::make_unique<StackControl>(Orientation::Vertical);
    ButtonControl* top  = left->add(ui.button("Top"));
    ButtonControl* mid  = left->add(ui.button("Middle"));
    ButtonControl* low  = left->add(ui.button("Bottom"));

    auto           right = std::make_unique<StackControl>(Orientation::Vertical);
    ButtonControl* page  = right->add(ui.button("Page"));
    ButtonControl* page2 = right->add(ui.button("Page two"));

    auto row = std::make_unique<StackControl>(Orientation::Horizontal);
    row->set_spacing(40.0f);
    row->add(std::move(left));
    row->add(std::move(right));
    ui.window().set_content(std::move(row));
    ui.layout({600, 400});

    Window& win = ui.window();
    win.focus_content();
    REQUIRE(win.focused() == top);

    // Down walks its own column rather than crossing to a nearer-by-score control.
    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    REQUIRE(win.focused() == mid);
    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    REQUIRE(win.focused() == low);
    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    REQUIRE(win.focused() == low); // nothing below: focus stays put, no wrap

    ui::testing::seams(win).handle_key(key_down(Key::ArrowUp));
    REQUIRE(win.focused() == mid);

    ui::testing::seams(win).handle_key(key_down(Key::ArrowRight));
    REQUIRE(win.focused() == page2); // the row that lines up with `mid`
    ui::testing::seams(win).handle_key(key_down(Key::ArrowLeft));
    REQUIRE(win.focused() == mid);

    ui::testing::seams(win).handle_key(key_down(Key::ArrowUp));
    REQUIRE(win.focused() == top);
    ui::testing::seams(win).handle_key(key_down(Key::ArrowRight));
    REQUIRE(win.focused() == page);
}

TEST_CASE("A control keeps the arrows it uses and releases the ones it doesn't", "[focus][xy]")
{
    HeadlessUI    ui    = create_headless();
    auto          stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl* root  = stack.get();
    ui.window().set_content(std::move(stack));

    ButtonControl* above        = root->add(ui.button("Above"));
    auto           slider_owned = std::make_unique<SliderControl>();
    slider_owned->set_range(0.0f, 10.0f);
    slider_owned->set_value(5.0f);
    SliderControl* slider    = root->add(std::move(slider_owned));
    auto           box_owned = std::make_unique<TextBoxControl>();
    box_owned->set_text("abc");
    TextBoxControl* box   = root->add(std::move(box_owned));
    ButtonControl*  below = root->add(ui.button("Below"));
    ui.layout({400, 400});

    Window& win = ui.window();

    REQUIRE(win.focus_element(slider));
    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    REQUIRE(win.focused() == slider);
    REQUIRE(slider->value() < 5.0f); // the key reached the control instead
    ui::testing::seams(win).handle_key(key_down(Key::ArrowRight));
    REQUIRE(win.focused() == slider);

    REQUIRE(win.focus_element(box));
    ui::testing::seams(win).handle_key(key_down(Key::ArrowLeft));
    REQUIRE(win.focused() == box);
    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    REQUIRE(win.focused() == below);
    ui::testing::seams(win).handle_key(key_down(Key::ArrowUp));
    REQUIRE(win.focused() == box);

    REQUIRE(win.focus_element(above));
    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    REQUIRE(win.focused() == slider);
}

TEST_CASE("Directional navigation stays inside an entered scope", "[focus][xy][scope]")
{
    HeadlessUI    ui    = create_headless();
    auto          stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl* root  = stack.get();
    ui.window().set_content(std::move(stack));

    const Group    group   = make_group(ui, *root, /*expanded=*/true, /*scope=*/true);
    ButtonControl* outside = root->add(ui.button("Outside"));
    ui.layout({400, 600});

    Window& win = ui.window();
    win.focus_content();
    ui::testing::seams(win).handle_key(key_down(Key::Enter));
    REQUIRE(win.focused() == group.first);

    // Inside a scope the search runs over the scope's own ring, so Down stops at the last child.
    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    REQUIRE(win.focused() == group.second);
    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    REQUIRE(win.focused() == group.second);
    REQUIRE(win.entered_focus_scope() == group.expander);

    ui::testing::seams(win).handle_key(key_down(Key::Escape));
    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    REQUIRE(win.focused() == outside); // out of the scope, the whole ring is in play again
}

TEST_CASE("Reactivating the host window gives focus back to the stop that had it", "[focus]")
{
    HeadlessUI     ui     = create_headless();
    auto           stack  = std::make_unique<StackControl>(Orientation::Vertical);
    ButtonControl* first  = stack->add(ui.button("One"));
    ButtonControl* second = stack->add(ui.button("Two"));
    ui.window().set_content(std::move(stack));
    ui.layout({400, 300});

    Window& win = ui.window();
    win.focus_content();
    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    REQUIRE(win.focused() == second);

    win.suspend_focus(); // WM_KILLFOCUS
    CHECK(win.focused() == nullptr);
    win.restore_focus(); // WM_SETFOCUS
    CHECK(win.focused() == second);

    // Nothing focused when the window was deactivated: the first stop, as before.
    win.blur();
    win.suspend_focus();
    win.restore_focus();
    CHECK(win.focused() == first);
}

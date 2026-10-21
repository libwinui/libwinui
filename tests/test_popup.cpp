// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/Popup.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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

struct PopupWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    ButtonControl*       page_button  = nullptr;
    PopupControl*        popup        = nullptr;
    ButtonControl*       popup_button = nullptr;
    int                  page_clicks  = 0;
    int                  opened       = 0;
    int                  closed       = 0;

    PopupWindow()
    {
        auto stack  = std::make_unique<StackControl>(Orientation::Vertical);
        page_button = stack->add(std::make_unique<ButtonControl>("Page"));
        page_button->on_click([this] { ++page_clicks; });
        popup        = stack->add(std::make_unique<PopupControl>());
        popup_button = popup->set_child(std::make_unique<ButtonControl>("Close"));
        popup_button->on_click([this] { popup->set_open(false); });
        popup->on_opened([this] { ++opened; });
        popup->on_closed([this] { ++closed; });
        window.set_content(std::move(stack));
        relayout();
    }

    void relayout()
    {
        window.layout({400.0f, 400.0f}, measurer);
    }
};
} // namespace

TEST_CASE("A Popup takes no room and draws nothing in place", "[popup]")
{
    HeadlessTextMeasurer measurer;
    PopupControl         popup;
    popup.set_child(std::make_unique<ButtonControl>("Child"));
    popup.set_open(true);
    const Size desired = popup.measure(Size{400.0f, 400.0f}, measurer, Theme::default_light());
    CHECK(desired.width == 0.0f);
    CHECK(desired.height == 0.0f);

    TestRenderer r;
    popup.render(r, Theme::light());
    CHECK(r.count() == 0);
    CHECK_FALSE(ui::testing::ElementAccess::popup_request(popup)->chrome.shadow);
    CHECK_FALSE(ui::testing::ElementAccess::popup_request(popup)->light_dismiss); // light dismiss is off by default
}

TEST_CASE("The child sits at the Popup's position plus the offsets", "[popup]")
{
    PopupWindow w;
    w.popup->set_horizontal_offset(200.0f);
    w.popup->set_vertical_offset(-10.0f);
    w.popup->set_open(true);
    w.relayout();

    const Rect at = w.popup->bounds();
    CHECK_THAT(w.popup_button->bounds().x, WithinAbs(at.x + 200.0f, 0.01));
    CHECK_THAT(w.popup_button->bounds().y, WithinAbs(at.y - 10.0f, 0.01));
    CHECK(w.opened == 1);
}

TEST_CASE("Without light dismiss an outside click reaches the page and the popup stays", "[popup]")
{
    PopupWindow w;
    w.popup->set_horizontal_offset(200.0f);
    w.popup->set_open(true);
    w.relayout();

    const Point page = w.page_button->bounds().center();
    ui::testing::seams(w.window).handle_pointer(down(page));
    ui::testing::seams(w.window).handle_pointer(up(page));
    CHECK(w.page_clicks == 1);
    CHECK(w.popup->is_open());

    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    CHECK(w.popup->is_open()); // Escape closes only light-dismiss popups

    w.relayout();
    const Point inside = w.popup_button->bounds().center();
    ui::testing::seams(w.window).handle_pointer(down(inside));
    ui::testing::seams(w.window).handle_pointer(up(inside));
    CHECK_FALSE(w.popup->is_open());
    CHECK(w.closed == 1);
}

TEST_CASE("With light dismiss an outside click closes it and is swallowed", "[popup]")
{
    PopupWindow w;
    w.popup->set_light_dismiss(true);
    w.popup->set_horizontal_offset(200.0f);
    w.popup->set_open(true);
    w.relayout();

    const Point page = w.page_button->bounds().center();
    ui::testing::seams(w.window).handle_pointer(down(page));
    ui::testing::seams(w.window).handle_pointer(up(page));
    CHECK_FALSE(w.popup->is_open());
    CHECK(w.closed == 1);
    CHECK(w.page_clicks == 0);
}

TEST_CASE("A light-dismiss popup takes focus and Escape gives it back", "[popup][focus]")
{
    PopupWindow w;
    ui::testing::seams(w.window).handle_pointer(down(w.page_button->bounds().center()));
    ui::testing::seams(w.window).handle_pointer(up(w.page_button->bounds().center()));
    REQUIRE(w.window.focused() == w.page_button);

    w.popup->set_light_dismiss(true);
    w.popup->set_horizontal_offset(200.0f);
    w.popup->set_open(true);
    w.relayout();
    CHECK(w.window.focused() == w.popup_button);

    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    CHECK_FALSE(w.popup->is_open());
    w.relayout();
    CHECK(w.window.focused() == w.page_button);
}

TEST_CASE("Setting the open state it already has raises nothing", "[popup]")
{
    PopupWindow w;
    w.popup->set_open(false);
    CHECK(w.closed == 0);
    w.popup->set_open(true);
    w.popup->set_open(true);
    CHECK(w.opened == 1);
}

TEST_CASE("Input after the focused element was destroyed, before the next layout, finds no focus", "[popup][focus]")
{
    PopupWindow   w;
    const Point   page  = w.page_button->bounds().center();
    StackControl* stack = static_cast<StackControl*>(w.page_button->parent());
    ui::testing::seams(w.window).handle_pointer(down(page));
    ui::testing::seams(w.window).handle_pointer(up(page));
    REQUIRE(w.window.focused() == w.page_button);

    REQUIRE(stack->remove(w.page_button)); // outside any dispatch: destroyed on the spot
    w.page_button = nullptr;
    CHECK(w.window.focused() == nullptr);
    ui::testing::seams(w.window).handle_key({Key::Space, KeyAction::Down});
    ui::testing::seams(w.window).handle_key({Key::Space, KeyAction::Up});
    ui::testing::seams(w.window).handle_text("x");
    CHECK(w.window.focused() == nullptr);
}

TEST_CASE("Input after an open popup's owner was destroyed, before the next layout, skips the popup", "[popup]")
{
    PopupWindow w;
    w.popup->set_horizontal_offset(200.0f);
    w.popup->set_open(true);
    w.relayout();
    const Point inside = w.popup_button->bounds().center();

    StackControl* stack = static_cast<StackControl*>(w.popup->parent());
    REQUIRE(stack->remove(w.popup));
    w.popup        = nullptr;
    w.popup_button = nullptr;

    ui::testing::seams(w.window).handle_pointer({inside, PointerButton::None, PointerAction::Move});
    CHECK(w.window.cursor_at(inside) == CursorShape::Arrow);
    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    w.relayout();
    TestRenderer r;
    w.window.render(r);
}

// A Popup takes no room and draws nothing where it sits, but a Grid cell stretches it over
// whatever shares the cell.
TEST_CASE("A button sharing a grid cell with a Popup still takes the click", "[popup][input]")
{
    HeadlessUI     ui;
    bool           clicked = false;
    Property<bool> open{false};
    ui.window().set_content(ui::build(ui::Grid{
        .columns              = {ui::AutoSize},
        .rows                 = {ui::AutoSize},
        .horizontal_alignment = ui::Alignment::Leading,
        .vertical_alignment   = ui::Alignment::Leading,
        .content =
            {ui::Cell{.row = 0, .column = 0, .content = ui::Button{.text = "Show", .on_click = [&] { clicked = true; }}},
             ui::Cell{.row = 0, .column = 0, .content = ui::Popup{.is_open = open, .content = ui::Button{.text = "Close"}}}},
    }));
    ui.layout(Size{400.0f, 300.0f});
    const Point at{20.0f, 10.0f};
    seams(ui.window()).handle_pointer({at, PointerButton::None, PointerAction::Move});
    seams(ui.window()).handle_pointer({at, PointerButton::Left, PointerAction::Down});
    seams(ui.window()).handle_pointer({at, PointerButton::Left, PointerAction::Up});
    CHECK(clicked);
}

// A Popup casts no shadow, so nothing the window draws itself changes when it closes; the area its
// content covered has to be repainted all the same.
TEST_CASE("Closing a Popup repaints where its content was", "[popup][damage]")
{
    HeadlessUI     ui;
    Property<bool> open{true};
    ui.window().set_content(ui::build(ui::VerticalStack{
        .content = {ui::Popup{.is_open           = open,
                              .horizontal_offset = 200.0f,
                              .vertical_offset   = 100.0f,
                              .content           = ui::Button{.text = "Close"}}},
    }));
    const auto damage = [&]
    {
        ui.layout(Size{400.0f, 300.0f});
        return ui.window().take_damage();
    };
    (void)damage();
    (void)damage();
    REQUIRE(damage().empty()); // settled

    open                           = false;
    const std::vector<Rect> rects  = damage();
    const auto              covers = [&rects](Point p)
    { return std::ranges::any_of(rects, [p](const Rect& r) { return r.contains(p); }); };
    CHECK(covers(Point{210.0f, 110.0f})); // inside the closed popup's button
    CHECK_FALSE(covers(Point{20.0f, 280.0f}));
}

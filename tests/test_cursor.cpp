// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/panes/ResizeGripper.hpp"
#include "winui/ui/panes/Splitter.hpp"
#include "winui/ui/text/PasswordBox.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"

#include <memory>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

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

struct CursorWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    StackControl*        stack;

    CursorWindow()
    {
        auto owned = std::make_unique<StackControl>(Orientation::Vertical);
        stack      = owned.get();
        window.set_content(std::move(owned));
    }
    void layout()
    {
        window.layout({400.0f, 600.0f}, measurer);
    }
};
} // namespace

TEST_CASE("The cursor is an arrow where nothing asks for one", "[cursor]")
{
    CursorWindow w;
    auto*        button = w.stack->add(std::make_unique<ButtonControl>("Plain"));
    w.layout();
    REQUIRE(w.window.cursor_at(button->bounds().center()) == CursorShape::Arrow);
    REQUIRE(w.window.cursor_at({390.0f, 590.0f}) == CursorShape::Arrow);
}

TEST_CASE("Each control's own cursor: I-beam over text, a hand over a link, resize arrows over handles", "[cursor]")
{
    CursorWindow w;
    auto*        box      = w.stack->add(std::make_unique<TextBoxControl>());
    auto*        link     = w.stack->add(std::make_unique<ButtonControl>("Open the docs"));
    auto*        text     = w.stack->add(std::make_unique<TextControl>("Selectable"));
    auto*        plain    = w.stack->add(std::make_unique<TextControl>("Plain"));
    auto*        vertical = w.stack->add(std::make_unique<SplitterControl>(Orientation::Vertical));
    auto*        across   = w.stack->add(std::make_unique<SplitterControl>(Orientation::Horizontal));
    auto*        gripper  = w.stack->add(std::make_unique<ResizeGripperControl>());
    auto*        password = w.stack->add(std::make_unique<PasswordBoxControl>());
    link->set_variant(ButtonVariant::Hyperlink);
    text->set_selection_enabled(true);
    w.layout();

    REQUIRE(w.window.cursor_at(box->bounds().center()) == CursorShape::IBeam);
    REQUIRE(w.window.cursor_at(link->bounds().center()) == CursorShape::Hand);
    REQUIRE(w.window.cursor_at(text->bounds().center()) == CursorShape::IBeam);
    REQUIRE(w.window.cursor_at(plain->bounds().center()) == CursorShape::Arrow);
    REQUIRE(w.window.cursor_at(vertical->bounds().center()) == CursorShape::SizeWestEast);
    REQUIRE(w.window.cursor_at(across->bounds().center()) == CursorShape::SizeNorthSouth);
    REQUIRE(w.window.cursor_at(gripper->bounds().center()) == CursorShape::SizeNorthwestSoutheast);
    REQUIRE(w.window.cursor_at(password->bounds().center()) == CursorShape::IBeam);
}

TEST_CASE("A TextBox's DeleteButton is an arrow; the rest of the field is an I-beam", "[cursor][textbox]")
{
    CursorWindow w;
    auto*        box = w.stack->add(std::make_unique<TextBoxControl>());
    box->set_text("some text");
    w.layout();
    ui::testing::seams(w.window).handle_pointer(down(box->bounds().center()));
    ui::testing::seams(w.window).handle_pointer(up(box->bounds().center()));
    w.layout();
    REQUIRE(box->delete_button_visible());
    REQUIRE(w.window.cursor_at(box->delete_button_rect().center()) == CursorShape::Arrow);
    REQUIRE(w.window.cursor_at({box->bounds().x + 2.0f, box->bounds().center().y}) == CursorShape::IBeam);
}

TEST_CASE("A hyperlink run shows the hand once the pointer is over it", "[cursor][textblock]")
{
    CursorWindow w;
    auto*        block = w.stack->add(std::make_unique<TextControl>());
    block->set_inlines({{.text = "see "}, {.text = "link", .on_click = [] {}}});
    w.layout();
    const float before = w.measurer.measure("see ", Theme::light().text_style).width;
    const Point on_link{block->bounds().x + before + 2.0f, block->bounds().center().y};
    ui::testing::seams(w.window).handle_pointer(move(on_link));
    REQUIRE(w.window.cursor_at(on_link) == CursorShape::Hand);
    const Point on_text{block->bounds().x + 2.0f, block->bounds().center().y};
    ui::testing::seams(w.window).handle_pointer(move(on_text));
    REQUIRE(w.window.cursor_at(on_text) == CursorShape::Arrow); // not selectable: no opinion
}

TEST_CASE("set_cursor: the innermost element that asks decides, its own cursor before its control's", "[cursor]")
{
    CursorWindow w;
    auto*        button = w.stack->add(std::make_unique<ButtonControl>("Card"));
    auto*        box    = w.stack->add(std::make_unique<TextBoxControl>());
    w.stack->set_cursor(CursorShape::Hand);
    w.layout();

    REQUIRE(w.window.cursor_at(button->bounds().center()) == CursorShape::Hand);
    REQUIRE(w.window.cursor_at(box->bounds().center()) == CursorShape::IBeam);
    box->set_cursor(CursorShape::Wait);
    REQUIRE(w.window.cursor_at(box->bounds().center()) == CursorShape::Wait);
    box->set_cursor(std::nullopt);
    REQUIRE(w.window.cursor_at(box->bounds().center()) == CursorShape::IBeam);

    button->set_cursor(CursorShape::Cross);
    REQUIRE(w.window.cursor_at(button->bounds().center()) == CursorShape::Cross);
    button->set_hit_test_visible(false);
    REQUIRE(w.window.cursor_at(button->bounds().center()) == CursorShape::Hand);
}

TEST_CASE("A press keeps the cursor it started with until the release", "[cursor]")
{
    CursorWindow w;
    auto*        splitter = w.stack->add(std::make_unique<SplitterControl>(Orientation::Vertical));
    auto*        button   = w.stack->add(std::make_unique<ButtonControl>("Elsewhere"));
    w.layout();
    const Point on_splitter = splitter->bounds().center();
    const Point elsewhere   = button->bounds().center();

    ui::testing::seams(w.window).handle_pointer(move(on_splitter));
    ui::testing::seams(w.window).handle_pointer(down(on_splitter));
    ui::testing::seams(w.window).handle_pointer(move(elsewhere));
    REQUIRE(w.window.cursor_at(elsewhere) == CursorShape::SizeWestEast);
    ui::testing::seams(w.window).handle_pointer(up(elsewhere));
    REQUIRE(w.window.cursor_at(elsewhere) == CursorShape::Arrow);
}

TEST_CASE("An open popup is hit first, and does not inherit the cursor of the tree under it", "[cursor][popup]")
{
    CursorWindow w;
    auto*        flyout = w.stack->add(std::make_unique<FlyoutControl>());
    auto*        anchor = flyout->set_anchor(std::make_unique<ButtonControl>("Open"));
    auto*        box    = flyout->set_content(std::make_unique<TextBoxControl>());
    (void)anchor;
    w.stack->set_cursor(CursorShape::Hand);
    w.layout();
    flyout->show();
    w.layout();
    const Rect popup = ui::testing::popup_content(*flyout)->bounds();
    REQUIRE(w.window.cursor_at(box->bounds().center()) == CursorShape::IBeam);
    // The popup's own frame asks for nothing, so it shows the arrow, not the panel's hand.
    REQUIRE(w.window.cursor_at({popup.x + 2.0f, popup.y + 2.0f}) == CursorShape::Arrow);
}
